//===============================================================================
// IMAP4Handler (Neo-PostOffice)
// -----------------------------
// An IMAP4rev1 client (RFC 3501) built on libetpan. libetpan parses the
// protocol; the connection is a NetStream, the same one the POP3 client
// uses, so TLS, STARTTLS and certificate checks are done one way only:
// against the system's CAs plus an optional CA file, and against the host
// name, with no way to turn that off.
//
// It connects, logs in (with the "pass" setting or the first line printed
// by "passcmd") and keeps the mailbox's folders in step with the folders
// subscribed to on the server; INBOX is always shown, as "Inbox".
//
// Reading (step 2): each folder is opened read-only (EXAMINE). A folder whose
// UIDVALIDITY, UIDNEXT and message count are what they were at the last
// complete check is skipped. Otherwise the UIDs on the server are listed:
// those that are gone are removed here, and the headers of new ones are
// fetched, newest first, in batches. A body is fetched (BODY.PEEK, so the
// message stays unread on the server) when the message is opened, and kept
// in the local store. What is known of each folder is kept in imap.dat.
//
// Flags (step 3): the read state and the flagged, answered and forwarded
// flags go both ways. Changes made here are queued in imap.dat and sent at
// once (UID STORE) when the server can be reached, else at the start of the
// next check; until sent they win over the server's. Changes made on the
// server are read at each check: with CONDSTORE only those since the last
// check (and an unchanged folder costs one EXAMINE), without it all flags
// of the folder.
//
// GNU LGPL like PostOffice itself.
//===============================================================================

#ifdef PO_HAVE_LIBETPAN

#include "mailbox.h"
#include "mailheaders.h"
#include "netstream.h"
#include "sendpipe.h"
#include "var.h"
#include "status.h"

#include <libetpan/libetpan.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "debug.h"

extern JobList JOBQ;

//-------------------------------------------------------------------------------
// libetpan stream driver: libetpan reads and writes through a NetStream.
//-------------------------------------------------------------------------------

static ssize_t net_read (mailstream_low *s, void *buf, size_t count)
{
	return ((NetStream *) s->data)->ReadSome (buf, count);
}

static ssize_t net_write (mailstream_low *s, const void *buf, size_t count)
{
	NetStream *net = (NetStream *) s->data;
	if (!net->Write (std::string ((const char *) buf, count))) return -1;
	return (ssize_t) count;
}

static int net_close (mailstream_low *s)
{
	((NetStream *) s->data)->Close ();
	return 0;
}

static int net_get_fd (mailstream_low *s)
{
	return ((NetStream *) s->data)->Fd ();
}

// The NetStream belongs to the handler; only the driver's record is freed.
static void net_free (mailstream_low *s)
{
	s->data = NULL;
	free (s);
}

static void net_cancel (mailstream_low *)
{
}

static struct mailstream_cancel *net_get_cancel (mailstream_low *)
{
	return NULL;
}

static mailstream_low_driver net_driver = {
	net_read, net_write, net_close, net_get_fd, net_free, net_cancel, net_get_cancel,
	NULL,		// certificate chain: NetStream has checked it
	NULL, NULL, NULL	// IDLE: step 5
};

//-------------------------------------------------------------------------------
// Folder names
//-------------------------------------------------------------------------------

static void put_utf8 (std::string &out, unsigned long c)
{
	if (c < 0x80) out += (char) c;
	else if (c < 0x800) { out += (char) (0xc0 | (c >> 6)); out += (char) (0x80 | (c & 0x3f)); }
	else if (c < 0x10000)
	{
		out += (char) (0xe0 | (c >> 12));
		out += (char) (0x80 | ((c >> 6) & 0x3f));
		out += (char) (0x80 | (c & 0x3f));
	}
	else
	{
		out += (char) (0xf0 | (c >> 18));
		out += (char) (0x80 | ((c >> 12) & 0x3f));
		out += (char) (0x80 | ((c >> 6) & 0x3f));
		out += (char) (0x80 | (c & 0x3f));
	}
}

// Modified UTF-7: printable ASCII as is, "&-" for "&", and other text as
// "&" + base64 of UTF-16BE (with "," for "/") + "-". False (with out as far
// as it got) for a name that breaks those rules.
bool IMAP4Handler::DecodeMUTF7 (const std::string &in, std::string &out)
{
	static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
	out.clear ();
	size_t i = 0;
	while (i < in.size ())
	{
		char c = in[i];
		if (c != '&')
		{
			if ((unsigned char) c < 0x20 || (unsigned char) c > 0x7e) return false;
			out += c;
			++i;
			continue;
		}
		size_t end = in.find ('-', i + 1);
		if (end == std::string::npos) return false;
		if (end == i + 1) { out += '&'; i = end + 1; continue; }

		// base64 to UTF-16 units
		unsigned long bits = 0;
		int nbits = 0;
		std::vector<unsigned> units;
		for (size_t k = i + 1; k < end; ++k)
		{
			size_t v = b64.find (in[k]);
			if (v == std::string::npos) return false;
			bits = (bits << 6) | v;
			nbits += 6;
			if (nbits >= 16)
			{
				nbits -= 16;
				units.push_back ((bits >> nbits) & 0xffff);
			}
		}
		// Left-over bits are padding: fewer than six, all zero.
		if (units.empty () || nbits >= 6 || (bits & ((1UL << nbits) - 1)) != 0) return false;

		for (size_t k = 0; k < units.size (); ++k)
		{
			unsigned long u = units[k];
			if (u >= 0xd800 && u < 0xdc00)
			{
				if (k + 1 >= units.size () || units[k + 1] < 0xdc00 || units[k + 1] >= 0xe000) return false;
				u = 0x10000 + ((u - 0xd800) << 10) + (units[k + 1] - 0xdc00);
				++k;
			}
			else if (u >= 0xdc00 && u < 0xe000) return false;
			put_utf8 (out, u);
		}
		i = end + 1;
	}
	return true;
}

// "INBOX" becomes "Inbox". The personal namespace prefix ("INBOX." on
// Courier and Cyrus) is left out, and the server's hierarchy delimiter
// becomes "/", as in the Maildir and MH mailboxes.
std::string IMAP4Handler::ClientName (const std::string &server, char delim, const std::string &prefix)
{
	if (strcasecmp (server.c_str (), "INBOX") == 0) return "Inbox";
	std::string name = server;
	if (!prefix.empty () && name.size () > prefix.size () &&
		strncasecmp (name.c_str (), prefix.c_str (), prefix.size ()) == 0)
		name = name.substr (prefix.size ());

	std::string utf8;
	if (!DecodeMUTF7 (name, utf8)) utf8 = name;	// not valid: show it as it is

	if (delim && delim != '/')
	{
		// A "/" inside a name (the delimiter being "."), and the
		// delimiter itself, swap places, so the client's "/" is still the
		// level separator.
		for (char &c : utf8)
		{
			if (c == '/') c = delim;
			else if (c == delim) c = '/';
		}
	}
	return utf8;
}

//-------------------------------------------------------------------------------
// The handler
//-------------------------------------------------------------------------------

IMAP4Handler::IMAP4Handler (MailBox *_Parent, Var *_Parameters)
{
	Parameters = new Var (_Parameters);
	Parent = _Parent;
	ConnectionOpen = 0;
	Imap = NULL;
	Net = new NetStream;
	Net->Cancelled = [this]() { return Parent->ToCollector->UrgentPending; };
	Parent->ToCollector->AddPriorityClass (CLASS_GETBODY);
}

IMAP4Handler::~IMAP4Handler (void)
{
	EndSession (false);
	delete Net;
	delete Parameters;
}

void IMAP4Handler::Report (const std::string &what)
{
	if (Quiet || what == LastError || Parent->ToCollector->UrgentPending) return;
	LastError = what;
	Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, Parent->Name, what.c_str());
}

void IMAP4Handler::ShowStatus (int jid, const std::string &text)
{
	JOBQ.Stat (jid, (char *) text.c_str());
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}

// Why a libetpan call failed, in words.
std::string IMAP4Handler::Why (int r)
{
	std::string server = Imap && Imap->imap_response ? Imap->imap_response : "";
	switch (r)
	{
		case MAILIMAP_ERROR_STREAM:
			return Net->Error ().empty () ? "the connection was lost" : Net->Error ();
		case MAILIMAP_ERROR_PARSE:
			return "the server's reply could not be understood";
		case MAILIMAP_ERROR_MEMORY:
			return "out of memory";
		default:
			if (!server.empty ()) return "the server said: " + server;
			return "error " + std::to_string (r) + " from libetpan";
	}
}

bool IMAP4Handler::Capability (const char *name)
{
	if (!Imap || !Imap->imap_connection_info || !Imap->imap_connection_info->imap_capability) return false;
	clist *caps = Imap->imap_connection_info->imap_capability->cap_list;
	for (clistiter *it = clist_begin (caps); it; it = clist_next (it))
	{
		struct mailimap_capability *c = (struct mailimap_capability *) clist_content (it);
		if (c->cap_type == MAILIMAP_CAPABILITY_NAME && c->cap_data.cap_name &&
			strcasecmp (c->cap_data.cap_name, name) == 0)
			return true;
	}
	return false;
}

int IMAP4Handler::RefreshCapabilities (void)
{
	struct mailimap_capability_data *caps = NULL;
	int r = mailimap_capability (Imap, &caps);
	if (caps) mailimap_capability_data_free (caps);
	return r;
}

void IMAP4Handler::EndSession (bool logout)
{
	if (Imap)
	{
		if (logout && Net->IsOpen ()) mailimap_logout (Imap);
		mailimap_free (Imap);		// closes the stream (and the NetStream)
		Imap = NULL;
	}
	Net->Close ();
	ConnectionOpen = 0;
	Selected.clear ();
	Broken = false;
}

void IMAP4Handler::OpenConnection (void)
{
}

void IMAP4Handler::CloseConnection (void)
{
	EndSession (true);
}

// Connects and logs in, unless a session is open and still answers.
bool IMAP4Handler::Session (int jid)
{
	if (Imap && Net->IsOpen () && !Broken)
	{
		if (mailimap_noop (Imap) == MAILIMAP_NO_ERROR) return true;
		EndSession (false);
	}
	EndSession (false);

	Host = Parameters->Get_char ("host");
	// Mailboxes from PostOffice 0.7 have no "security": they used port 143
	// without encryption, so they get STARTTLS rather than sending the
	// password in the clear.
	std::string security = Parameters->Get_char ("security");
	if (security.empty ()) security = "starttls";
	if (security != "tls" && security != "starttls") security = "none";
	int port = Parameters->Get_int ("port");
	if (port <= 0) port = security == "tls" ? 993 : 143;
	std::string cafile = Parameters->Get_char ("cafile");
	if (cafile.compare (0, 2, "~/") == 0 && getenv ("HOME")) cafile = getenv ("HOME") + cafile.substr (1);

	if (Host.empty ())
	{
		Report (std::string ("No IMAP server is set for ") + Parent->Name + " (Config \xe2\x86\x92 Mailboxes).");
		return false;
	}

	ShowStatus (jid, "Connecting to " + Host);
	if (!Net->Connect (Host, port) || (security == "tls" && !Net->StartTLS (cafile)))
	{
		Report ("Cannot reach the IMAP server: " + Net->Error () + ".");
		Net->Close ();
		return false;
	}

	mailstream_low *low = mailstream_low_new (Net, &net_driver);
	mailstream *stream = low ? mailstream_new (low, 8192) : NULL;
	Imap = stream ? mailimap_new (0, NULL) : NULL;
	if (!Imap)
	{
		if (stream) mailstream_close (stream);
		else if (low) mailstream_low_free (low);
		Net->Close ();
		Report ("Cannot set up the IMAP connection: out of memory.");
		return false;
	}

	mailimap_set_progress_callback (Imap, &IMAP4Handler::BodyProgress, NULL, this);
	int r = mailimap_connect (Imap, stream);		// Imap owns the stream from here
	bool preauth = r == MAILIMAP_NO_ERROR_AUTHENTICATED;
	if (r != MAILIMAP_NO_ERROR_NON_AUTHENTICATED && !preauth)
	{
		Report ("The IMAP server " + Host + " did not greet: " + Why (r) + ".");
		EndSession (false);
		return false;
	}

	if (security == "starttls")
	{
		// A server that greets as already logged in (PREAUTH) before
		// encryption cannot be secured with STARTTLS (RFC 9051 7.1.4).
		if (preauth)
		{
			Report ("The IMAP server " + Host + " logged in without encryption (PREAUTH), so STARTTLS"
					" cannot be used. Choose TLS (port 993); mail is not read without encryption.");
			EndSession (false);
			return false;
		}
		RefreshCapabilities ();
		if (!Capability ("STARTTLS"))
		{
			Report ("The IMAP server " + Host + " does not offer STARTTLS. Choose TLS (port 993) or ask"
					" the provider; mail is not read without encryption.");
			EndSession (true);
			return false;
		}
		r = mailimap_starttls (Imap);
		// Anything read past the reply would be unprotected text pretending
		// to come after the handshake (STARTTLS injection). libetpan checks
		// this too (and then returns MAILIMAP_ERROR_STARTTLS).
		if (Imap->imap_stream->read_buffer_len != 0)
		{
			Report ("The IMAP server " + Host + " sent data before TLS was started; not continuing.");
			EndSession (false);
			return false;
		}
		if (r != MAILIMAP_NO_ERROR)
		{
			Report ("The IMAP server " + Host + " refused STARTTLS: " + Why (r) + ".");
			EndSession (r != MAILIMAP_ERROR_STREAM);
			return false;
		}
		if (!Net->StartTLS (cafile))
		{
			Report ("Cannot start TLS with " + Host + ": " + Net->Error () + ".");
			EndSession (false);
			return false;
		}
	}

	// Capabilities: again after TLS (they may change, and earlier ones
	// cannot be trusted), and again after logging in.
	r = RefreshCapabilities ();
	if (r != MAILIMAP_NO_ERROR)
	{
		Report ("The IMAP server " + Host + " did not list its capabilities: " + Why (r) + ".");
		EndSession (false);
		return false;
	}
	if (!Capability ("IMAP4rev1") && !Capability ("IMAP4rev2"))
	{
		Report ("The server " + Host + " does not speak IMAP4rev1.");
		EndSession (true);
		return false;
	}

	if (!preauth)
	{
		if (Capability ("LOGINDISABLED"))
		{
			Report ("The IMAP server " + Host + " does not accept a password on this connection"
					" (LOGINDISABLED). Choose TLS or STARTTLS.");
			EndSession (true);
			return false;
		}

		std::string password, err;
		const char *passcmd = Parameters->Get_char ("passcmd");
		if (passcmd && *passcmd)
		{
			ShowStatus (jid, "Getting the password");
			if (!run_password_command (passcmd, password, err))
			{
				Report (std::string ("Cannot get the password for ") + Parent->Name + ": " + err);
				EndSession (true);
				return false;
			}
		}
		else password = Parameters->Get_char ("pass");
		if (password.empty ())
		{
			Report (std::string ("No password is set for ") + Parent->Name +
					": enter one, or a password command, in Config \xe2\x86\x92 Mailboxes.");
			EndSession (true);
			return false;
		}

		ShowStatus (jid, "Logging in to " + Host);
		r = mailimap_login (Imap, Parameters->Get_char ("user"), password.c_str ());
		std::fill (password.begin (), password.end (), '\0');
		if (r != MAILIMAP_NO_ERROR)
		{
			if (r == MAILIMAP_ERROR_LOGIN)
				Report ("The IMAP server " + Host + " refused the login for " + Parameters->Get_char ("user") +
						(Imap->imap_response && *Imap->imap_response ? std::string (": ") + Imap->imap_response : std::string ()) +
						". Check the user name and password in Config \xe2\x86\x92 Mailboxes.");
			else
				Report ("Logging in to " + Host + " failed: " + Why (r) + ".");
			EndSession (r != MAILIMAP_ERROR_STREAM);
			return false;
		}
		RefreshCapabilities ();
	}

	// The personal namespace prefix, so "INBOX.Sent" shows as "Sent".
	Prefix.clear ();
	if (Capability ("NAMESPACE"))
	{
		struct mailimap_namespace_data *ns = NULL;
		if (mailimap_namespace (Imap, &ns) == MAILIMAP_NO_ERROR && ns)
		{
			if (ns->ns_personal && ns->ns_personal->ns_data_list)
			{
				clistiter *it = clist_begin (ns->ns_personal->ns_data_list);
				if (it)
				{
					struct mailimap_namespace_info *info = (struct mailimap_namespace_info *) clist_content (it);
					if (info && info->ns_prefix) Prefix = info->ns_prefix;
				}
			}
			mailimap_namespace_data_free (ns);
		}
	}
	ConnectionOpen = 1;
	return true;
}

// The subscribed folders, plus INBOX, without those that cannot hold
// messages (\Noselect, \NonExistent).
bool IMAP4Handler::ListFolders (int jid)
{
	ShowStatus (jid, "Listing the folders");
	clist *list = NULL;
	int r = mailimap_lsub (Imap, "", "*", &list);
	if (r != MAILIMAP_NO_ERROR)
	{
		Report ("Cannot list the folders on " + Host + ": " + Why (r) + ".");
		return false;
	}

	std::vector<Folder> found;
	std::set<std::string> seen;
	bool inbox = false;
	for (clistiter *it = list ? clist_begin (list) : NULL; it; it = clist_next (it))
	{
		struct mailimap_mailbox_list *mb = (struct mailimap_mailbox_list *) clist_content (it);
		if (!mb || !mb->mb_name) continue;
		// A name with a line break cannot be kept in imap.dat (nor typed).
		if (strpbrk (mb->mb_name, "\r\n")) continue;
		bool selectable = true;
		if (mb->mb_flag)
		{
			if (mb->mb_flag->mbf_type == MAILIMAP_MBX_LIST_FLAGS_SFLAG &&
				mb->mb_flag->mbf_sflag == MAILIMAP_MBX_LIST_SFLAG_NOSELECT)
				selectable = false;
			for (clistiter *o = mb->mb_flag->mbf_oflags ? clist_begin (mb->mb_flag->mbf_oflags) : NULL; o; o = clist_next (o))
			{
				struct mailimap_mbx_list_oflag *of = (struct mailimap_mbx_list_oflag *) clist_content (o);
				if (of && of->of_type == MAILIMAP_MBX_LIST_OFLAG_FLAG_EXT && of->of_flag_ext &&
					(strcasecmp (of->of_flag_ext, "NonExistent") == 0 || strcasecmp (of->of_flag_ext, "Noselect") == 0))
					selectable = false;
			}
		}
		if (!selectable) continue;
		Folder f;
		f.Server = mb->mb_name;
		f.Delim = mb->mb_delimiter;
		f.Client = ClientName (f.Server, f.Delim, Prefix);
		if (f.Client.empty () || !seen.insert (f.Client).second) continue;
		if (f.Client == "Inbox") inbox = true;
		found.push_back (f);
	}
	if (list) mailimap_list_result_free (list);

	if (!inbox)
	{
		Folder f;
		f.Server = "INBOX";
		f.Client = "Inbox";
		f.Delim = found.empty () ? 0 : found[0].Delim;
		found.push_back (f);
	}
	std::sort (found.begin (), found.end (), [](const Folder &a, const Folder &b)
	{
		if (a.Client == "Inbox") return b.Client != "Inbox";
		if (b.Client == "Inbox") return false;
		return a.Client < b.Client;
	});
	Folders = found;
	return true;
}

// Folders for every server folder; folders this mailbox made whose server
// folder is gone (or no longer subscribed) are removed by the window, after
// the messages in them.
void IMAP4Handler::SyncFolders (void)
{
	bool changed = false;
	for (const Folder &f : Folders)
		if (Parent->EnsureFolder (f.Client)) changed = true;

	// What is known of folders that are gone: their messages go too.
	for (auto it = State.begin (); it != State.end (); )
	{
		bool kept = false;
		for (const Folder &f : Folders) if (f.Server == it->first) { kept = true; break; }
		if (kept) { ++it; continue; }
		Gone (it->second, it->second.Uids);
		it = State.erase (it);
	}

	std::vector<std::string> gone;
	Parent->VLock->LockR ();
	for (MailView *V = Parent->VFirst; V; V = V->Next)
	{
		if (!Parent->IsAutoFolder (V->Name)) continue;
		bool kept = false;
		for (const Folder &f : Folders) if (f.Client == V->Name) { kept = true; break; }
		if (!kept) gone.push_back (V->Name);
	}
	Parent->VLock->UnlockR ();
	for (const std::string &name : gone)
		Parent->ToUI->PostMessage (CLASS_FOLDERGONE, NO_SUBCLASS, Parent->Name, name.c_str ());
	if (changed || !gone.empty ())
		Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
}

//-------------------------------------------------------------------------------
// What is known of the folders (imap.dat)
//-------------------------------------------------------------------------------

// "1:5,9,12:13" for {1,2,3,4,5,9,12,13}.
std::string IMAP4Handler::UidRanges (const std::set<uint32_t> &uids)
{
	std::string out;
	for (auto it = uids.begin (); it != uids.end (); )
	{
		uint32_t a = *it, b = a;
		auto n = std::next (it);
		while (n != uids.end () && b != 0xffffffffu && *n == b + 1) { b = *n; ++n; }
		if (!out.empty ()) out += ',';
		out += std::to_string (a);
		if (b != a) out += ":" + std::to_string (b);
		it = n;
	}
	return out;
}

bool IMAP4Handler::ParseRanges (const std::string &in, std::set<uint32_t> &out)
{
	out.clear ();
	size_t i = 0;
	while (i < in.size ())
	{
		size_t comma = in.find (',', i);
		std::string part = in.substr (i, comma == std::string::npos ? std::string::npos : comma - i);
		i = comma == std::string::npos ? in.size () : comma + 1;
		if (part.empty () || part.find_first_not_of ("0123456789:") != std::string::npos) return false;
		size_t colon = part.find (':');
		if (colon != part.rfind (':') || colon == 0 || colon + 1 == part.size ()) return false;
		if (part.size () > 21) return false;
		unsigned long long a = strtoull (part.c_str (), NULL, 10);
		unsigned long long b = colon == std::string::npos ? a : strtoull (part.c_str () + colon + 1, NULL, 10);
		if (a == 0 || b < a || b > 0xffffffffULL || b - a > 50000000ULL) return false;
		auto hint = out.end ();
		for (unsigned long long u = a; u <= b; ++u) hint = out.insert (hint, (uint32_t) u);
	}
	return true;
}

// 64-bit FNV-1a of the server's folder name, in hex.
std::string IMAP4Handler::FolderHash (const std::string &server)
{
	unsigned long long h = 14695981039346656037ULL;
	for (unsigned char c : server) { h ^= c; h *= 1099511628211ULL; }
	char buf[17];
	snprintf (buf, sizeof (buf), "%016llx", h);
	return buf;
}

std::string IMAP4Handler::MakeMsgID (const std::string &hash, uint32_t validity, uint32_t uid)
{
	return "<imap." + hash + "." + std::to_string (validity) + "." + std::to_string (uid) + "@imap>";
}

bool IMAP4Handler::SplitMsgID (const char *msgid, std::string &hash, uint32_t &validity, uint32_t &uid)
{
	char h[17];
	unsigned long v = 0, u = 0;
	int end = 0;
	if (!msgid || sscanf (msgid, "<imap.%16[0-9a-f].%lu.%lu@imap>%n", h, &v, &u, &end) != 3) return false;
	if (msgid[end] != 0 || strlen (h) != 16 || v == 0 || u == 0 || v > 0xffffffffUL || u > 0xffffffffUL) return false;
	hash = h;
	validity = (uint32_t) v;
	uid = (uint32_t) u;
	return true;
}

std::string IMAP4Handler::ServerOf (const std::string &hash)
{
	for (auto &kv : State) if (kv.second.Hash == hash) return kv.first;
	return "";
}

std::string IMAP4Handler::StateFile (void)
{
	return std::string (Parent->DirectoryPrefix) + "/imap.dat";
}

// Lines "F HASH UIDVALIDITY UIDNEXT MESSAGES name" then "U ranges".
void IMAP4Handler::LoadState (void)
{
	StateLoaded = true;
	State.clear ();
	Pending.clear ();
	FILE *F = fopen (StateFile ().c_str (), "r");
	if (!F) return;
	std::string line, current;
	bool havecurrent = false;
	int c;
	for (;;)
	{
		line.clear ();
		while ((c = fgetc (F)) != EOF && c != '\n') line += (char) c;
		if (c == EOF && line.empty ()) break;
		if (line.compare (0, 2, "F ") == 0)
		{
			char hash[17];
			unsigned long v, n, m;
			int off = 0;
			havecurrent = false;
			if (sscanf (line.c_str (), "F %16s %lu %lu %lu %n", hash, &v, &n, &m, &off) != 4 || off <= 0) continue;
			current = line.substr (off);
			if (current.empty ()) continue;
			FolderState &st = State[current];
			st.Hash = FolderHash (current);
			st.Validity = (uint32_t) v;
			st.UidNext = (uint32_t) n;
			st.Messages = (uint32_t) m;
			havecurrent = true;
		}
		else if (line.compare (0, 2, "M ") == 0 && havecurrent)
		{
			State[current].ModSeq = strtoull (line.c_str () + 2, NULL, 10);
		}
		else if (line.compare (0, 2, "P ") == 0)
		{
			// "P <msgid> +S-F": changes not yet on the server.
			size_t sp = line.find (' ', 2);
			if (sp == std::string::npos) continue;
			std::string id = line.substr (2, sp - 2), ch = line.substr (sp + 1);
			std::string hash;
			uint32_t v, u;
			if (!SplitMsgID (id.c_str (), hash, v, u)) continue;
			for (size_t k = 0; k + 1 < ch.size (); k += 2)
				if ((ch[k] == '+' || ch[k] == '-') && strchr ("SFRP", ch[k + 1]))
					Pending[id][ch[k + 1]] = ch[k] == '+';
		}
		else if (line.compare (0, 2, "U ") == 0 && havecurrent)
		{
			FolderState &st = State[current];
			if (!ParseRanges (line.substr (2), st.Uids))
			{
				// Damaged: read the folder again.
				st.Uids.clear ();
				st.UidNext = st.Messages = 0;
			}
		}
		if (c == EOF) break;
	}
	fclose (F);
}

// Written whole to a private file, then renamed over the old one.
void IMAP4Handler::SaveState (void)
{
	std::string path = StateFile (), tmp = path + ".new";
	int fd = open (tmp.c_str (), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	FILE *F = fd >= 0 ? fdopen (fd, "w") : NULL;
	if (!F)
	{
		if (fd >= 0) close (fd);
		eprintf ("[imap] cannot save %s\n", tmp.c_str ());
		return;
	}
	fprintf (F, "# Neo-PostOffice IMAP state, version 1\n");
	for (auto &kv : State)
	{
		const FolderState &st = kv.second;
		fprintf (F, "F %s %lu %lu %lu %s\n", st.Hash.c_str (), (unsigned long) st.Validity,
				 (unsigned long) st.UidNext, (unsigned long) st.Messages, kv.first.c_str ());
		if (st.ModSeq) fprintf (F, "M %llu\n", (unsigned long long) st.ModSeq);
		fprintf (F, "U %s\n", UidRanges (st.Uids).c_str ());
	}
	for (auto &p : Pending)
	{
		std::string ch;
		for (auto &f : p.second) { ch += f.second ? '+' : '-'; ch += f.first; }
		if (!ch.empty ()) fprintf (F, "P %s %s\n", p.first.c_str (), ch.c_str ());
	}
	bool ok = fflush (F) == 0 && fsync (fileno (F)) == 0;
	ok = (fclose (F) == 0) && ok;
	if (!ok || rename (tmp.c_str (), path.c_str ()) != 0)
		eprintf ("[imap] cannot save %s\n", path.c_str ());
}

// Messages kept here that are no longer known on the server. They are
// removed when found gone, but the window does that after the mailbox was
// saved, so a crash or kill can bring them back; and a folder that has not
// changed is not looked at again. Only folders checked completely this time
// are judged (and those no longer subscribed), so a lost imap.dat or a
// folder that could not be opened costs nothing.
void IMAP4Handler::Prune (void)
{
	std::set<std::string> current;
	for (const Folder &f : Folders) current.insert (FolderHash (f.Server));
	std::vector<std::string> stale;
	Parent->Lock->LockR ();
	for (MailMessage *M = (MailMessage *) Parent->Root->BNext; M && M != Parent->Root; M = (MailMessage *) M->BNext)
	{
		std::string hash;
		uint32_t validity, uid;
		if (!SplitMsgID (M->MsgID, hash, validity, uid)) continue;
		if (!current.count (hash)) { stale.push_back (M->MsgID); continue; }
		std::string server = ServerOf (hash);
		if (!Checked.count (server)) continue;
		const FolderState &st = State[server];
		if (st.Validity != validity || !st.Uids.count (uid)) stale.push_back (M->MsgID);
	}
	Parent->Lock->UnlockR ();
	for (const std::string &id : stale)
		Parent->ToUI->PostMessage (CLASS_GONE, NO_SUBCLASS, Parent->Name, id.c_str ());
}

// The window removes these messages, as for its own Delete button.
void IMAP4Handler::Gone (const FolderState &st, const std::set<uint32_t> &uids)
{
	for (uint32_t uid : uids)
	{
		std::string id = MakeMsgID (st.Hash, st.Validity, uid);
		if (Parent->Have (&id[0]))
			Parent->ToUI->PostMessage (CLASS_GONE, NO_SUBCLASS, Parent->Name, id.c_str ());
	}
}

//-------------------------------------------------------------------------------
// Reading
//-------------------------------------------------------------------------------

// Reports a failed command. A lost connection or a reply libetpan could not
// read leaves the session unusable.
bool IMAP4Handler::Failed (int r, const std::string &what)
{
	if (r == MAILIMAP_ERROR_STREAM || r == MAILIMAP_ERROR_PARSE || !Net->IsOpen ()) Broken = true;
	Report (what + ": " + Why (r) + ".");
	return false;
}

// Opens a folder read-only (EXAMINE), or for changes (SELECT), unless it is
// open already (a folder open for changes can be read too). With CONDSTORE
// the folder's HIGHESTMODSEQ comes with it.
static bool open_folder (mailimap *imap, const std::string &server, bool writable, bool condstore, uint64_t &modseq, int &r)
{
	modseq = 0;
	if (condstore)
		r = writable ? mailimap_select_condstore (imap, server.c_str (), &modseq)
					 : mailimap_examine_condstore (imap, server.c_str (), &modseq);
	else
		r = writable ? mailimap_select (imap, server.c_str ()) : mailimap_examine (imap, server.c_str ());
	return r == MAILIMAP_NO_ERROR;
}

bool IMAP4Handler::Examine (const std::string &server)
{
	if (Broken) return false;
	if (Selected == server) return true;
	Selected.clear ();
	int r;
	uint64_t modseq;
	if (!open_folder (Imap, server, false, Capability ("CONDSTORE"), modseq, r))
	{
		if (r == MAILIMAP_ERROR_STREAM || r == MAILIMAP_ERROR_PARSE || !Net->IsOpen ()) Broken = true;
		return false;
	}
	struct mailimap_selection_info *info = Imap->imap_selection_info;
	SelValidity = info ? info->sel_uidvalidity : 0;
	SelUidNext = info ? info->sel_uidnext : 0;
	SelExists = info && info->sel_has_exists ? info->sel_exists : 0;
	SelModSeq = modseq;
	SelWritable = false;
	Selected = server;
	return true;
}

// Opens a folder for changes. False also for a folder the server only lets
// us read.
bool IMAP4Handler::Select (const std::string &server)
{
	if (Broken) return false;
	if (Selected == server && SelWritable) return true;
	Selected.clear ();
	int r;
	uint64_t modseq;
	if (!open_folder (Imap, server, true, Capability ("CONDSTORE"), modseq, r))
	{
		if (r == MAILIMAP_ERROR_STREAM || r == MAILIMAP_ERROR_PARSE || !Net->IsOpen ()) Broken = true;
		return false;
	}
	struct mailimap_selection_info *info = Imap->imap_selection_info;
	SelValidity = info ? info->sel_uidvalidity : 0;
	SelUidNext = info ? info->sel_uidnext : 0;
	SelExists = info && info->sel_has_exists ? info->sel_exists : 0;
	SelModSeq = modseq;
	SelWritable = !(info && info->sel_perm == MAILIMAP_MAILBOX_READONLY);
	Selected = server;
	return SelWritable;
}

// Text from the server as the store keeps it: LF line ends, no NUL bytes.
static std::string local_text (const char *data, size_t len)
{
	std::string out;
	out.reserve (len);
	for (size_t i = 0; i < len; ++i)
	{
		char c = data[i];
		if (c == '\r' && i + 1 < len && data[i + 1] == '\n') continue;
		out += c ? c : ' ';
	}
	return out;
}

static time_t imap_time (const struct mailimap_date_time *d)
{
	if (!d) return 0;
	struct tm t;
	memset (&t, 0, sizeof (t));
	t.tm_year = d->dt_year - 1900;
	t.tm_mon = d->dt_month - 1;
	t.tm_mday = d->dt_day;
	t.tm_hour = d->dt_hour;
	t.tm_min = d->dt_min;
	t.tm_sec = d->dt_sec;
	time_t utc = timegm (&t);
	if (utc == (time_t) -1) return 0;
	// dt_zone is the offset as written: -700 for "-0700".
	int z = d->dt_zone < 0 ? -d->dt_zone : d->dt_zone;
	long off = (z / 100) * 3600L + (z % 100) * 60L;
	return d->dt_zone < 0 ? utc + off : utc - off;
}

// One message of a FETCH reply, picked apart.
struct Fetched
{
	uint32_t			 Uid = 0;
	bool				 HaveFlags = false, Seen = false, Answered = false, Flagged = false, Forwarded = false;
	time_t				 Internal = 0;
	const char			*Section = NULL;	// the BODY[...] data
	size_t				 SectionLen = 0;
	bool				 HaveSection = false;
};

static Fetched pick_apart (struct mailimap_msg_att *att)
{
	Fetched f;
	for (clistiter *it = att && att->att_list ? clist_begin (att->att_list) : NULL; it; it = clist_next (it))
	{
		struct mailimap_msg_att_item *item = (struct mailimap_msg_att_item *) clist_content (it);
		if (!item) continue;
		if (item->att_type == MAILIMAP_MSG_ATT_ITEM_DYNAMIC && item->att_data.att_dyn)
		{
			f.HaveFlags = true;
			clist *fl = item->att_data.att_dyn->att_list;
			for (clistiter *k = fl ? clist_begin (fl) : NULL; k; k = clist_next (k))
			{
				struct mailimap_flag_fetch *ff = (struct mailimap_flag_fetch *) clist_content (k);
				if (!ff || ff->fl_type != MAILIMAP_FLAG_FETCH_OTHER || !ff->fl_flag) continue;
				switch (ff->fl_flag->fl_type)
				{
					case MAILIMAP_FLAG_SEEN: f.Seen = true; break;
					case MAILIMAP_FLAG_ANSWERED: f.Answered = true; break;
					case MAILIMAP_FLAG_FLAGGED: f.Flagged = true; break;
					case MAILIMAP_FLAG_KEYWORD:
						if (ff->fl_flag->fl_data.fl_keyword && strcasecmp (ff->fl_flag->fl_data.fl_keyword, "$Forwarded") == 0)
							f.Forwarded = true;
						break;
					default: break;
				}
			}
		}
		else if (item->att_type == MAILIMAP_MSG_ATT_ITEM_STATIC && item->att_data.att_static)
		{
			struct mailimap_msg_att_static *st = item->att_data.att_static;
			switch (st->att_type)
			{
				case MAILIMAP_MSG_ATT_UID: f.Uid = st->att_data.att_uid; break;
				case MAILIMAP_MSG_ATT_INTERNALDATE: f.Internal = imap_time (st->att_data.att_internal_date); break;
				case MAILIMAP_MSG_ATT_BODY_SECTION:
					f.HaveSection = true;
					if (st->att_data.att_body_section)
					{
						f.Section = st->att_data.att_body_section->sec_body_part;
						f.SectionLen = st->att_data.att_body_section->sec_length;
					}
					break;
				default: break;
			}
		}
	}
	return f;
}

// The header fields the message list needs; the rest come with the body.
static const char *list_headers[] = {
	"Date", "From", "Sender", "Reply-To", "To", "Cc", "Subject", "Message-ID", "In-Reply-To",
	"References", "Newsgroups", "Content-Type", "MIME-Version", "List-Id", "X-Priority", "Importance", NULL
};

// Brings one folder up to date. False when checking must stop (Stop was
// pressed, or the session failed); a folder that cannot be opened is
// reported and skipped.
bool IMAP4Handler::SyncFolder (int jid, const Folder &f, size_t index, size_t count, int &added)
{
	const size_t batch = 200;
	FolderState &st = State[f.Server];
	st.Hash = FolderHash (f.Server);

	std::string where = count > 1 ? " (folder " + std::to_string (index + 1) + " of " + std::to_string (count) + ")" : "";
	ShowStatus (jid, "Checking " + f.Client + where);
	Selected.clear ();		// read it fresh
	if (!Examine (f.Server))
	{
		if (Broken) return Failed (MAILIMAP_ERROR_STREAM, "The connection to " + Host + " was lost");
		Report ("Cannot open the folder " + f.Client + " on " + Host + ": " + Why (MAILIMAP_ERROR_EXAMINE) + ".");
		return true;
	}
	uint32_t validity = SelValidity, uidnext = SelUidNext, exists = SelExists;
	uint64_t modseq = SelModSeq;
	if (validity == 0)
	{
		Report ("The IMAP server " + Host + " gives no UIDVALIDITY for " + f.Client + ", so its messages cannot be read.");
		return true;
	}
	// New UIDs: everything known of the folder is out of date.
	if (st.Validity != validity)
	{
		if (st.Validity) Gone (st, st.Uids);
		st.Uids.clear ();
		st.UidNext = st.Messages = 0;
		st.ModSeq = 0;
		st.Validity = validity;
	}
	// Nothing added or removed: same UIDNEXT and count. No flag changed: the
	// same HIGHESTMODSEQ (CONDSTORE); without it flags are always read.
	bool listsame = uidnext && st.UidNext == uidnext && st.Messages == exists;
	bool flagssame = modseq && st.ModSeq == modseq;
	if (listsame && flagssame)
	{
		Checked.insert (f.Server);
		return true;
	}

	// Flags of the messages known here (with CONDSTORE, only changes since
	// the last check).
	if (!flagssame && !st.Uids.empty () && exists > 0)
	{
		int changed = 0;
		if (!SyncFlags (f, st, modseq && st.ModSeq ? st.ModSeq : 0, changed)) return !Broken;
		if (changed) Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	}
	if (listsame)
	{
		st.ModSeq = modseq;
		Checked.insert (f.Server);
		return true;
	}

	// The messages on the server.
	std::set<uint32_t> server;
	if (exists > 0)
	{
		clist *res = NULL;
		struct mailimap_search_key *key = mailimap_search_key_new_all ();
		int r = key ? mailimap_uid_search (Imap, NULL, key, &res) : MAILIMAP_ERROR_MEMORY;
		if (key) mailimap_search_key_free (key);
		if (r != MAILIMAP_NO_ERROR) return Failed (r, "Cannot list the messages in " + f.Client) || !Broken;
		for (clistiter *it = res ? clist_begin (res) : NULL; it; it = clist_next (it))
		{
			uint32_t *u = (uint32_t *) clist_content (it);
			if (u && *u) server.insert (*u);
		}
		if (res) mailimap_search_result_free (res);
	}

	std::set<uint32_t> gone;
	for (uint32_t u : st.Uids) if (!server.count (u)) gone.insert (u);
	if (!gone.empty ())
	{
		Gone (st, gone);
		for (uint32_t u : gone) st.Uids.erase (u);
	}

	// New ones, newest first, keeping the folder in UID order: mail newer
	// than everything known is added at the end (oldest of it first); the
	// rest (the whole folder at the first check) is fetched newest first and
	// added at the start.
	uint32_t newest = st.Uids.empty () ? 0xffffffffu : *st.Uids.rbegin ();
	std::vector<uint32_t> fresh;
	size_t above = 0;		// fresh[0..above) are appended
	for (uint32_t u : server)
		if (!st.Uids.count (u) && u > newest) fresh.push_back (u);
	above = fresh.size ();
	for (auto it = server.rbegin (); it != server.rend (); ++it)
		if (!st.Uids.count (*it) && *it <= newest) fresh.push_back (*it);

	size_t done = 0;
	bool all = true;
	for (size_t b = 0, n = 0; b < fresh.size (); b += n)
	{
		if (Parent->ToCollector->UrgentPending) return false;
		ServiceBodies (jid);
		if (Broken) return false;
		if (!Examine (f.Server))
		{
			if (Broken) return Failed (MAILIMAP_ERROR_STREAM, "The connection to " + Host + " was lost");
			return true;
		}
		if (SelValidity != validity) return true;	// changed meanwhile: next check

		// A batch is all appended or all prepended.
		n = std::min (batch, fresh.size () - b);
		if (b < above) n = std::min (n, above - b);
		bool front = b >= above;
		ShowStatus (jid, "Fetching headers in " + f.Client + ": " + std::to_string (done) + " of " +
					std::to_string (fresh.size ()) + where);
		JOBQ.Progress (jid, (int) (5 + 90 * done / fresh.size ()));

		struct mailimap_set *set = mailimap_set_new_empty ();
		std::set<uint32_t> asked;
		for (size_t k = 0; k < n; ++k) { mailimap_set_add_single (set, fresh[b + k]); asked.insert (fresh[b + k]); }
		struct mailimap_fetch_type *ft = mailimap_fetch_type_new_fetch_att_list_empty ();
		mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_uid ());
		mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_flags ());
		mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_internaldate ());
		clist *names = clist_new ();
		for (int k = 0; list_headers[k]; ++k) clist_append (names, strdup (list_headers[k]));
		struct mailimap_section *sec = mailimap_section_new_header_fields (mailimap_header_list_new (names));
		mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_body_peek_section (sec));

		clist *res = NULL;
		int r = mailimap_uid_fetch (Imap, set, ft, &res);
		mailimap_fetch_type_free (ft);
		mailimap_set_free (set);
		if (r != MAILIMAP_NO_ERROR)
		{
			if (res) mailimap_fetch_list_free (res);
			return Failed (r, "Cannot fetch the messages in " + f.Client) || !Broken;
		}

		// Only answers to what was asked (a server may add FLAGS of others),
		// in the order they are to be filed.
		std::vector<Fetched> list;
		for (clistiter *it = res ? clist_begin (res) : NULL; it; it = clist_next (it))
		{
			Fetched m = pick_apart ((struct mailimap_msg_att *) clist_content (it));
			if (!m.Uid || !asked.count (m.Uid) || !m.HaveSection) continue;
			asked.erase (m.Uid);
			list.push_back (m);
		}
		std::sort (list.begin (), list.end (), [front](const Fetched &x, const Fetched &y)
				   { return front ? x.Uid > y.Uid : x.Uid < y.Uid; });

		size_t got = 0;
		for (const Fetched &m : list)
		{
			++got;
			std::string id = MakeMsgID (st.Hash, validity, m.Uid);
			st.Uids.insert (m.Uid);
			if (Parent->Have (&id[0])) continue;

			std::string head = local_text (m.Section ? m.Section : "", m.Section ? m.SectionLen : 0);
			while (!head.empty () && head.back () == '\n') head.pop_back ();
			head += '\n';
			MailMessage *M = new MailMessage (Parent, id.c_str ());
			if (front)
			{
				// Saved (and so shown after a restart) in UID order too.
				Parent->Lock->LockRW ();
				M->ChainToFront ();
				Parent->Lock->UnlockRW ();
			}
			M->ProcessHeaders (&head[0]);
			M->ReceiveDate = m.Internal ? m.Internal : time (NULL);
			if (!*M->Header ("Date"))
			{
				char date[64];
				struct tm tmv;
				localtime_r (&M->ReceiveDate, &tmv);
				strftime (date, sizeof (date), "%a, %d %b %Y %H:%M:%S %z", &tmv);
				M->Envelope->Set (ENVELOPE_DATE, date);
			}
			M->Envelope->Set (ENVELOPE_GROUPS, f.Client.c_str ());
			M->Envelope->SetInt (ENVELOPE_I_READ, m.Seen ? 1 : 0);
			M->Envelope->SetInt (ENVELOPE_I_FLAGS, (m.Flagged ? EFLAG_FLAGGED : 0) | (m.Answered ? EFLAG_REPLIED : 0) |
								 (m.Forwarded ? EFLAG_PASSED : 0));
			M->AtFront = front;
			Parent->Deliver (M);
			M->AtFront = false;
			++added;
			// Older mail is shown when the folder is redrawn after the batch;
			// the window adds announced messages at the end of the list.
			if (front) continue;

			Parent->Lock->LockRW ();
			for (int j = 0; j < M->Subscribers->Count (); ++j)
			{
				ViewListNode *VLN = M->Subscribers->Get (j);
				if (VLN) Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, VLN->View->Name, M->MsgID);
			}
			Parent->Lock->UnlockRW ();
		}
		// The data the list pointed into.
		if (res) mailimap_fetch_list_free (res);
		// Not sent: deleted meanwhile, or the server could not give it.
		if (!asked.empty ()) all = false;
		done += got;

		// Save as we go, so that a crash does not fetch everything again.
		Parent->SaveMailBox ();
		SaveState ();
		if (got) Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	}
	if (all)
	{
		st.UidNext = uidnext;
		st.Messages = exists;
		st.ModSeq = modseq;
	}
	Checked.insert (f.Server);
	return true;
}

// Takes the flags of known messages from the server (all of them, or with
// CONDSTORE those changed since modseq "since"). A flag with a change of
// ours not yet sent keeps our value.
bool IMAP4Handler::SyncFlags (const Folder &f, FolderState &st, uint64_t since, int &changed)
{
	struct mailimap_set *set = mailimap_set_new_interval (1, 0);		// 1:*
	struct mailimap_fetch_type *ft = mailimap_fetch_type_new_fetch_att_list_empty ();
	mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_uid ());
	mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_flags ());
	clist *res = NULL;
	int r = since ? mailimap_uid_fetch_changedsince (Imap, set, ft, since, &res) : mailimap_uid_fetch (Imap, set, ft, &res);
	mailimap_fetch_type_free (ft);
	mailimap_set_free (set);
	if (r != MAILIMAP_NO_ERROR)
	{
		if (res) mailimap_fetch_list_free (res);
		return Failed (r, "Cannot read the flags in " + f.Client);
	}
	for (clistiter *it = res ? clist_begin (res) : NULL; it; it = clist_next (it))
	{
		Fetched m = pick_apart ((struct mailimap_msg_att *) clist_content (it));
		if (!m.Uid || !m.HaveFlags || !st.Uids.count (m.Uid)) continue;
		std::string id = MakeMsgID (st.Hash, st.Validity, m.Uid);
		MailMessage *M = Parent->Find (&id[0]);
		if (!M) continue;
		auto pend = Pending.find (id);
		auto mine = [&](char c) { return pend != Pending.end () && pend->second.count (c); };

		if (!mine ('S') && M->Envelope->GetInt (ENVELOPE_I_READ) != (m.Seen ? 1 : 0))
		{
			M->Envelope->SetInt (ENVELOPE_I_READ, m.Seen ? 1 : 0);
			++changed;
		}
		int old = M->Envelope->GetInt (ENVELOPE_I_FLAGS), now = old;
		struct { char c; int bit; bool on; } map[] = {
			{ 'F', EFLAG_FLAGGED, m.Flagged }, { 'R', EFLAG_REPLIED, m.Answered }, { 'P', EFLAG_PASSED, m.Forwarded } };
		for (auto &x : map)
			if (!mine (x.c)) now = x.on ? (now | x.bit) : (now & ~x.bit);
		if (now != old)
		{
			M->Envelope->SetInt (ENVELOPE_I_FLAGS, now);
			++changed;
		}
	}
	if (res) mailimap_fetch_list_free (res);
	return true;
}

// Records a change made here, on the message and in the queue for the
// server. False if the message is not one of this mailbox's.
bool IMAP4Handler::QueueFlag (const char *msgid, char letter, bool on)
{
	std::string hash;
	uint32_t v, u;
	if (!strchr ("SFRP", letter) || !SplitMsgID (msgid, hash, v, u)) return false;
	MailMessage *M = Parent->Find ((char *) msgid);
	if (!M) return false;
	bool changed = false;
	if (letter == 'S')
	{
		changed = M->Envelope->GetInt (ENVELOPE_I_READ) != (on ? 1 : 0);
		M->Envelope->SetInt (ENVELOPE_I_READ, on ? 1 : 0);
	}
	else
	{
		int bit = letter == 'F' ? EFLAG_FLAGGED : letter == 'R' ? EFLAG_REPLIED : EFLAG_PASSED;
		int old = M->Envelope->GetInt (ENVELOPE_I_FLAGS), now = on ? (old | bit) : (old & ~bit);
		changed = now != old;
		M->Envelope->SetInt (ENVELOPE_I_FLAGS, now);
	}
	if (changed) Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	Pending[msgid][letter] = on;
	return true;
}

static struct mailimap_flag *imap_flag (char letter)
{
	switch (letter)
	{
		case 'S': return mailimap_flag_new_seen ();
		case 'F': return mailimap_flag_new_flagged ();
		case 'R': return mailimap_flag_new_answered ();
		default: return mailimap_flag_new_flag_keyword (strdup ("$Forwarded"));
	}
}

// Sends the queued changes, folder by folder, one UID STORE per flag and
// direction. False if the session failed (the rest stays queued).
bool IMAP4Handler::PushPending (int jid)
{
	if (Pending.empty ()) return true;
	// folder -> (letter, on) -> UIDs
	std::map<std::string, std::map<std::pair<char, bool>, std::set<uint32_t>>> work;
	std::vector<std::string> stale;
	for (auto &p : Pending)
	{
		std::string hash, server;
		uint32_t v, u;
		if (SplitMsgID (p.first.c_str (), hash, v, u)) server = ServerOf (hash);
		// Renumbered or gone on the server: nothing to change there.
		if (server.empty () || State[server].Validity != v || !State[server].Uids.count (u))
		{
			stale.push_back (p.first);
			continue;
		}
		for (auto &f : p.second) work[server][{ f.first, f.second }].insert (u);
	}
	for (const std::string &id : stale) Pending.erase (id);

	bool ok = true;
	for (auto &w : work)
	{
		const std::string &server = w.first;
		const FolderState &st = State[server];
		std::string name = ClientName (server, 0, Prefix);
		for (const Folder &f : Folders) if (f.Server == server) name = f.Client;
		if (jid) ShowStatus (jid, "Sending changes in " + name);
		if (!Select (server))
		{
			if (Broken) { ok = false; break; }
			// Not there (any more): the changes wait; the next check finds
			// out what became of the folder.
			if (Selected != server) continue;
			// Read-only for us: the changes stay here only.
			Report ("The folder " + name + " on " + Host + " cannot be changed (read-only), so read state and"
					" flags changed here are kept here only.");
			for (auto &c : w.second)
				for (uint32_t u : c.second)
				{
					auto it = Pending.find (MakeMsgID (st.Hash, st.Validity, u));
					if (it != Pending.end () && (it->second.erase (c.first.first), it->second.empty ())) Pending.erase (it);
				}
			continue;
		}
		if (SelValidity != st.Validity) continue;	// renumbered: the next check sorts it out
		for (auto &c : w.second)
		{
			struct mailimap_set *set = mailimap_set_new_empty ();
			for (uint32_t u : c.second) mailimap_set_add_single (set, u);
			struct mailimap_flag_list *fl = mailimap_flag_list_new_empty ();
			mailimap_flag_list_add (fl, imap_flag (c.first.first));
			struct mailimap_store_att_flags *att = c.first.second ? mailimap_store_att_flags_new_add_flags_silent (fl)
																 : mailimap_store_att_flags_new_remove_flags_silent (fl);
			int r = mailimap_uid_store (Imap, set, att);
			mailimap_store_att_flags_free (att);
			mailimap_set_free (set);
			if (r != MAILIMAP_NO_ERROR)
			{
				if (r == MAILIMAP_ERROR_STREAM || r == MAILIMAP_ERROR_PARSE || !Net->IsOpen ()) { Broken = true; ok = false; break; }
				// Refused (a server without $Forwarded, say): not tried again.
				Report ("The IMAP server " + Host + " did not take a flag change in " + name + ": " + Why (r) + ".");
			}
			for (uint32_t u : c.second)
			{
				auto it = Pending.find (MakeMsgID (st.Hash, st.Validity, u));
				if (it == Pending.end ()) continue;
				auto f = it->second.find (c.first.first);
				// Only if not changed again meanwhile.
				if (f != it->second.end () && f->second == c.first.second) it->second.erase (f);
				if (it->second.empty ()) Pending.erase (it);
			}
		}
		if (!ok) break;
	}
	SaveState ();
	return ok;
}

// Sends queued changes now if the server can be reached; if not, they wait
// for the next check (no error dialog: the check reports problems).
void IMAP4Handler::PushQuietly (void)
{
	SaveState ();
	bool quiet = Quiet;
	Quiet = true;
	int jid = JOBQ.NewJob (Parent->Name);
	if (Session (jid)) PushPending (jid);
	if (Broken) EndSession (false);
	JOBQ.EndJob (jid);
	Quiet = quiet;
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

// The window changed a flag (Mark as unread, Flag, a reply or forward sent).
// Other changes waiting in the queue go along.
void IMAP4Handler::SetFlag (char *msgid, char flag, bool on)
{
	if (!StateLoaded) LoadState ();
	bool any = QueueFlag (msgid, flag, on);
	ITCMessage *M;
	while ((M = Parent->ToCollector->GetNextMessage (CLASS_SETFLAG)))
	{
		if (M->Message->Count >= 2 && strlen (M->Message->Get (1)) == 2)
			any = QueueFlag (M->Message->Get (0), M->Message->Get (1)[1], M->Message->Get (1)[0] == '+') || any;
		delete M;
	}
	while ((M = Parent->ToCollector->GetNextMessage (CLASS_MARKREAD)))
	{
		any = QueueFlag (M->Message->Get (0), 'S', true) || any;
		delete M;
	}
	if (!any) return;
	Parent->SaveMailBox ();		// the flags are kept in the index
	PushQuietly ();
}

// A message whose text was here already was opened.
void IMAP4Handler::MarkRead (char *msgid)
{
	if (!Parent->Find (msgid)) return;
	SetFlag (msgid, 'S', true);
}

// Fetches the bodies the user is waiting for, during a check.
void IMAP4Handler::ServiceBodies (int jid)
{
	ITCMessage *M;
	bool opened = false;
	while (!Broken && Parent->ToCollector->PriorityPending && (M = Parent->ToCollector->GetNextMessage (CLASS_GETBODY)))
	{
		MailMessage *X = Parent->Find (M->Message->Get (0));
		bool have = X && (X->Flags & MMFL_HAVEBODY);
		if (FetchBody (M->Message->Get (0), jid) && !have && QueueFlag (M->Message->Get (0), 'S', true)) opened = true;
		Parent->ToUI->PostMessage (CLASS_GOTBODY, NO_SUBCLASS, M->Message->Get (0));
		delete M;
	}
	// Opened means read, on the server too.
	if (opened && !Broken) PushPending (jid);
}

// libetpan reports the bytes of a large reply as they come in. Shown for a
// message being opened, a few times a second.
void IMAP4Handler::BodyProgress (size_t current, size_t maximum, void *context)
{
	IMAP4Handler *H = (IMAP4Handler *) context;
	if (!H || H->ProgressJob < 0 || maximum < 65536) return;
	struct timespec now;
	clock_gettime (CLOCK_MONOTONIC, &now);
	double t = now.tv_sec + now.tv_nsec / 1e9;
	if (t - H->ProgressShown < 0.3 && current < maximum) return;
	H->ProgressShown = t;
	char text[96];
	snprintf (text, sizeof (text), "Fetching the message text: %.1f of %.1f MB", current / 1048576.0, maximum / 1048576.0);
	JOBQ.Progress (H->ProgressJob, (int) (100.0 * current / maximum));
	H->ShowStatus (H->ProgressJob, text);
}

// Fetches a message's text within the open session. True if the message
// has its text (now or already).
bool IMAP4Handler::FetchBody (const char *msgid, int jid)
{
	MailMessage *X = Parent->Find ((char *) msgid);
	if (!X) return true;
	bool have = X->Flags & MMFL_HAVEBODY;
	if (have && !X->CaptureRaw) return true;

	std::string hash, server;
	uint32_t validity = 0, uid = 0;
	if (SplitMsgID (msgid, hash, validity, uid)) server = ServerOf (hash);
	if (server.empty () || State[server].Validity != validity || !State[server].Uids.count (uid))
	{
		if (!have) Report ("This message is no longer on the IMAP server, so its text cannot be fetched.");
		return have;
	}
	if (!Examine (server))
	{
		if (!Broken) Report ("Cannot open the folder of this message on " + Host + ": " + Why (MAILIMAP_ERROR_EXAMINE) + ".");
		else Report ("The connection to " + Host + " was lost.");
		return have;
	}
	if (SelValidity != validity)
	{
		if (!have) Report ("This message is no longer on the IMAP server, so its text cannot be fetched.");
		return have;
	}

	ShowStatus (jid, "Fetching the message text");
	struct mailimap_set *set = mailimap_set_new_single (uid);
	struct mailimap_fetch_type *ft = mailimap_fetch_type_new_fetch_att_list_empty ();
	mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_uid ());
	mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_body_peek_section (mailimap_section_new (NULL)));
	clist *res = NULL;
	ProgressJob = jid;
	ProgressShown = 0;
	int r = mailimap_uid_fetch (Imap, set, ft, &res);
	ProgressJob = -1;
	mailimap_fetch_type_free (ft);
	mailimap_set_free (set);
	if (r != MAILIMAP_NO_ERROR)
	{
		if (res) mailimap_fetch_list_free (res);
		Failed (r, "The IMAP server did not send the message");
		return have;
	}

	bool found = false;
	std::string text;
	for (clistiter *it = res ? clist_begin (res) : NULL; it; it = clist_next (it))
	{
		Fetched m = pick_apart ((struct mailimap_msg_att *) clist_content (it));
		if (m.Uid != uid || !m.HaveSection) continue;
		text = local_text (m.Section ? m.Section : "", m.Section ? m.SectionLen : 0);
		found = true;
		break;
	}
	if (res) mailimap_fetch_list_free (res);
	if (!found)
	{
		if (!have) Report ("This message was deleted on the IMAP server, so its text cannot be fetched.");
		return have;
	}

	X = Parent->Find ((char *) msgid);		// still here?
	if (!X) return true;
	if (X->CaptureRaw)
	{
		*X->CaptureRaw = text;
		if (have) return true;
	}
	// The whole header replaces the few fields fetched for the list.
	std::string *capture = X->CaptureRaw;
	X->CaptureRaw = NULL;
	std::vector<char> buf (text.begin (), text.end ());
	buf.push_back (0);
	X->ProcessMessage (buf.data ());
	X->CaptureRaw = capture;
	X->StoreFile ();
	return true;
}

void IMAP4Handler::GetBody (char *msgid)
{
	MailMessage *X = Parent->Find (msgid);
	if (!X) return;
	bool have = X->Flags & MMFL_HAVEBODY;
	if (have && !X->CaptureRaw) return;
	if (!StateLoaded) LoadState ();
	// Only the original for a copy: there is a local one to fall back on.
	Quiet = have;
	int jid = JOBQ.NewJob (Parent->Name);
	bool copy = X->CaptureRaw != NULL;
	if (Session (jid))
	{
		if (FetchBody (msgid, jid) && !have && !Broken)
		{
			LastError.clear ();
			// Opened: read, here and on the server.
			if (!copy && QueueFlag (msgid, 'S', true))
			{
				Quiet = true;
				PushPending (jid);
			}
		}
		// The session stays open for the next message; it is closed when
		// idle for a few minutes, and before each check.
		if (Broken) EndSession (false);
	}
	JOBQ.EndJob (jid);
	Quiet = false;
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

void IMAP4Handler::CheckGroup (const char * /*group*/)
{
	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Progress (jid, 1);
	if (!StateLoaded) LoadState ();
	if (Session (jid) && ListFolders (jid))
	{
		SyncFolders ();
		// Changes made here go first, so the server's flags read below
		// include them.
		PushPending (jid);
		int added = 0;
		bool complete = true;
		Checked.clear ();
		for (size_t i = 0; i < Folders.size (); ++i)
		{
			if (!SyncFolder (jid, Folders[i], i, Folders.size (), added) || Broken) { complete = false; break; }
		}
		ServiceBodies (jid);
		Prune ();
		SaveState ();
		Parent->SaveMailBox ();
		if (complete && !Broken) LastError.clear ();
		ShowStatus (jid, added ? std::to_string (added) + (added == 1 ? " new message" : " new messages")
							   : std::string ("No new messages"));
		Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	}
	if (Broken || Parent->ToCollector->UrgentPending) EndSession (false);
	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}

#endif // PO_HAVE_LIBETPAN
