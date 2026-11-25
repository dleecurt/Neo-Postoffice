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
// Changes on the server (step 4): Delete moves a message to the server's
// Trash (found by its special-use flag or a usual name, made if missing);
// in the Trash it removes it for good. A move to another folder of the
// account is a UID MOVE (or UID COPY, \Deleted and an expunge on servers
// without MOVE). Both are queued in imap.dat like flag changes. Messages
// stored here from elsewhere (copies from other mailboxes, sent mail) come
// from other threads: they are written to a spool directory first and sent
// with APPEND by this mailbox's thread, so a move never loses a message and
// the server may be away meanwhile.
//
// Push (step 5): while the mailbox's thread has nothing to do, the session
// stays open in IDLE on the Inbox; news from the server brings the Inbox up
// to date at once. IDLE is renewed every 10 minutes. Any other work ends it
// first. A connection lost while pushing is made again quietly, with longer
// and longer waits; the user is told once if it is still down after 10
// minutes. Other folders are checked at the interval. With QRESYNC, a
// changed folder's removed messages come with its flag changes (VANISHED);
// without, the UIDs are only all listed when the count says that some were
// removed.
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

#include <dirent.h>
#include <poll.h>
#include <sys/stat.h>

#include <algorithm>
#include "wordlist.h"
#include <atomic>
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

// UTF-8 to modified UTF-7. Bytes that are not valid UTF-8 are taken as
// Latin-1, so every name can be sent.
std::string IMAP4Handler::EncodeMUTF7 (const std::string &in)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
	std::string out;
	std::vector<unsigned> units;		// UTF-16 waiting to be written
	auto flush = [&]()
	{
		if (units.empty ()) return;
		out += '&';
		unsigned long bits = 0;
		int nbits = 0;
		for (unsigned u : units)
		{
			bits = (bits << 16) | u;
			nbits += 16;
			while (nbits >= 6)
			{
				nbits -= 6;
				out += b64[(bits >> nbits) & 0x3f];
			}
			bits &= (1UL << nbits) - 1;
		}
		if (nbits) out += b64[(bits << (6 - nbits)) & 0x3f];
		out += '-';
		units.clear ();
	};
	for (size_t i = 0; i < in.size (); )
	{
		unsigned char c = in[i];
		if (c >= 0x20 && c <= 0x7e)
		{
			flush ();
			out += c == '&' ? "&-" : std::string (1, (char) c);
			++i;
			continue;
		}
		// One UTF-8 sequence; a bad one: the byte alone, as Latin-1.
		unsigned long cp = c;
		int more = c >= 0xf0 && c < 0xf8 ? 3 : c >= 0xe0 && c < 0xf0 ? 2 : c >= 0xc0 && c < 0xe0 ? 1 : 0;
		bool ok = more > 0 && i + more < in.size ();
		if (ok)
		{
			cp = c & (0x3f >> more);
			for (int k = 1; k <= more; ++k)
			{
				unsigned char d = in[i + k];
				if ((d & 0xc0) != 0x80) { ok = false; break; }
				cp = (cp << 6) | (d & 0x3f);
			}
			static const unsigned long least[] = { 0, 0x80, 0x800, 0x10000 };
			if (ok && (cp < least[more] || cp > 0x10ffff || (cp >= 0xd800 && cp < 0xe000))) ok = false;
		}
		if (ok) i += more + 1;
		else { cp = c; ++i; }
		if (cp >= 0x10000)
		{
			cp -= 0x10000;
			units.push_back (0xd800 + (cp >> 10));
			units.push_back (0xdc00 + (cp & 0x3ff));
		}
		else units.push_back (cp);
	}
	flush ();
	return out;
}

// "Inbox" becomes "INBOX"; else the client's "/" becomes the delimiter (and
// a delimiter in the name a "/"), the name is encoded and the namespace
// prefix put in front.
std::string IMAP4Handler::ServerName (const std::string &client, char delim, const std::string &prefix)
{
	if (strcasecmp (client.c_str (), "Inbox") == 0) return "INBOX";
	std::string name = client;
	if (delim && delim != '/')
		for (char &c : name)
		{
			if (c == '/') c = delim;
			else if (c == delim) c = '/';
		}
	return prefix + EncodeMUTF7 (name);
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
	if (Quiet) { DownWhy = what; return; }
	if (what == LastError || Parent->ToCollector->UrgentPending) return;
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
	// No LOGOUT in the middle of IDLE: the connection is simply closed.
	if (Idling) logout = false;
	Idling = false;
	Qresync = false;
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
	SpecialCache.clear ();
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
	if (Idling) StopIdle ();
	if (Imap && Net->IsOpen () && !Broken)
	{
		if (mailimap_noop (Imap) == MAILIMAP_NO_ERROR) return true;
		EndSession (false);
	}
	EndSession (false);

	Host = Parameters->Get_char ("host");
	// No unencrypted login: anything but TLS is STARTTLS (also "none" and
	// the empty setting of mailboxes from PostOffice 0.7, which used port
	// 143 without encryption); it fails rather than sending the password
	// in the clear.
	std::string security = Parameters->Get_char ("security");
	if (security != "tls") security = "starttls";
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
			// The connection is always encrypted by now, so the server has
			// password logins turned off (Dovecot: no "plain" in
			// auth_mechanisms).
			Report ("The IMAP server " + Host + " does not accept a password login, even over the encrypted"
					" connection (LOGINDISABLED). Password logins are turned off on the server; with Dovecot,"
					" add \"plain\" to auth_mechanisms.");
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
	// QRESYNC (RFC 7162): removed messages are named in flag-change replies.
	if (Capability ("QRESYNC") && Capability ("CONDSTORE"))
	{
		clist *caps = clist_new ();
		clist_append (caps, mailimap_capability_new (MAILIMAP_CAPABILITY_NAME, NULL, strdup ("QRESYNC")));
		struct mailimap_capability_data *want = mailimap_capability_data_new (caps), *got = NULL;
		Qresync = mailimap_enable (Imap, want, &got) == MAILIMAP_NO_ERROR;
		mailimap_capability_data_free (want);
		if (got) mailimap_capability_data_free (got);
		if (!Net->IsOpen ()) { EndSession (false); return false; }
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
		std::string special;
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
				for (const char *use : { "Trash", "Sent", "Drafts", "Junk", "Archive", "All" })
					if (of && of->of_type == MAILIMAP_MBX_LIST_OFLAG_FLAG_EXT && of->of_flag_ext &&
						strcasecmp (of->of_flag_ext, use) == 0)
						special = use;
			}
		}
		if (!selectable) continue;
		Folder f;
		f.Special = special;
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
	// No delimiter on any folder: a server without subfolders.
	NameDelim = 0;
	for (const Folder &f : Folders) if (f.Delim) { NameDelim = f.Delim; break; }
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
	Ops.clear ();
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
		else if (line.compare (0, 2, "O ") == 0 && line.size () > 4 && strchr ("TXM", line[2]) && line[3] == ' ')
		{
			// "O kind <msgid> [target]": deletes and moves not yet sent.
			size_t sp = line.find (' ', 4);
			Op op;
			op.Kind = line[2];
			op.MsgID = line.substr (4, sp == std::string::npos ? std::string::npos : sp - 4);
			if (sp != std::string::npos) op.Target = line.substr (sp + 1);
			std::string hash;
			uint32_t v, u;
			if (SplitMsgID (op.MsgID.c_str (), hash, v, u) && (op.Kind != 'M' || !op.Target.empty ())) Ops.push_back (op);
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
	for (const Op &op : Ops)
		fprintf (F, "O %c %s%s%s\n", op.Kind, op.MsgID.c_str (), op.Target.empty () ? "" : " ", op.Target.c_str ());
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
	std::set<uint32_t> vanished;
	bool knowvanished = false;		// QRESYNC told which were removed
	if (!flagssame && !st.Uids.empty () && exists > 0)
	{
		int changed = 0;
		uint64_t since = modseq && st.ModSeq ? st.ModSeq : 0;
		if (!SyncFlags (f, st, since, changed, vanished)) return !Broken;
		knowvanished = since && Qresync;
		if (changed) Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	}
	if (listsame)
	{
		st.ModSeq = modseq;
		Checked.insert (f.Server);
		return true;
	}

	// The messages on the server. After a complete check, only the UIDs
	// above the last one known are asked for (UID SEARCH UID n:*); if the
	// count then adds up, after taking away what QRESYNC said was removed,
	// nothing else was. Else (and at the first check) all are listed.
	std::set<uint32_t> server;
	bool listed = false;
	if (exists > 0 && st.UidNext && !st.Uids.empty ())
	{
		clist *res = NULL;
		struct mailimap_search_key *key = mailimap_search_key_new_uid (mailimap_set_new_interval (st.UidNext, 0));
		int r = key ? mailimap_uid_search (Imap, NULL, key, &res) : MAILIMAP_ERROR_MEMORY;
		if (key) mailimap_search_key_free (key);
		if (r != MAILIMAP_NO_ERROR) return Failed (r, "Cannot list the messages in " + f.Client) || !Broken;
		std::set<uint32_t> above;
		for (clistiter *it = res ? clist_begin (res) : NULL; it; it = clist_next (it))
		{
			uint32_t *u = (uint32_t *) clist_content (it);
			// "n:*" means at least the highest UID, even below n.
			if (u && *u >= st.UidNext) above.insert (*u);
		}
		if (res) mailimap_search_result_free (res);
		if (!knowvanished) vanished.clear ();
		if (st.Uids.size () - vanished.size () + above.size () == exists)
		{
			for (uint32_t u : st.Uids) if (!vanished.count (u)) server.insert (u);
			server.insert (above.begin (), above.end ());
			listed = true;
		}
	}
	if (exists > 0 && !listed)
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
bool IMAP4Handler::SyncFlags (const Folder &f, FolderState &st, uint64_t since, int &changed,
							  std::set<uint32_t> &vanished)
{
	struct mailimap_set *set = mailimap_set_new_interval (1, 0);		// 1:*
	struct mailimap_fetch_type *ft = mailimap_fetch_type_new_fetch_att_list_empty ();
	mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_uid ());
	mailimap_fetch_type_new_fetch_att_list_add (ft, mailimap_fetch_att_new_flags ());
	clist *res = NULL;
	struct mailimap_qresync_vanished *gone = NULL;
	int r = since && Qresync ? mailimap_uid_fetch_qresync (Imap, set, ft, since, &res, &gone)
		  : since ? mailimap_uid_fetch_changedsince (Imap, set, ft, since, &res) : mailimap_uid_fetch (Imap, set, ft, &res);
	if (gone)
	{
		// UIDs removed since "since" (VANISHED (EARLIER)).
		clist *items = gone->qr_known_uids ? gone->qr_known_uids->set_list : NULL;
		for (clistiter *it = items ? clist_begin (items) : NULL; it; it = clist_next (it))
		{
			struct mailimap_set_item *si = (struct mailimap_set_item *) clist_content (it);
			if (!si) continue;
			uint32_t last = si->set_last ? si->set_last : si->set_first;
			for (auto u = st.Uids.lower_bound (si->set_first); u != st.Uids.end () && *u <= last; ++u)
				vanished.insert (*u);
		}
		mailimap_qresync_vanished_free (gone);
	}
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
// for the next check (no error dialog for that; refusals are shown).
void IMAP4Handler::PushQuietly (void)
{
	SaveState ();
	bool quiet = Quiet;
	int jid = JOBQ.NewJob (Parent->Name);
	// Quiet about not reaching the server only: what it refuses is shown.
	Quiet = true;
	bool up = Session (jid);
	Quiet = quiet;
	if (up)
	{
		if (Folders.empty ()) ListFolders (jid);
		if (PushPending (jid) && PushOps (jid)) FlushSpool (jid);
	}
	if (Broken) EndSession (false);
	JOBQ.EndJob (jid);
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

//-------------------------------------------------------------------------------
// Changes on the server (step 4)
//-------------------------------------------------------------------------------

// The folder with a special use ("Trash", "Sent"): the one the server
// marks so (RFC 6154), else one with a usual name, else (create) a new one,
// subscribed so that it shows. "" if there is none.
std::string IMAP4Handler::SpecialFolder (const std::string &use, bool create)
{
	auto cached = SpecialCache.find (use);
	if (cached != SpecialCache.end ()) return cached->second;
	for (const Folder &f : Folders)
		if (f.Special == use) return SpecialCache[use] = f.Server;

	static const char *trash[] = { "Trash", "Deleted Items", "Deleted Messages", "Deleted", "[Gmail]/Trash",
								   "[Google Mail]/Trash", NULL };
	static const char *sent[] = { "Sent", "Sent Items", "Sent Messages", "Sent Mail", "[Gmail]/Sent Mail",
								  "[Google Mail]/Sent Mail", NULL };
	const char **names = use == "Trash" ? trash : use == "Sent" ? sent : NULL;

	// All folders, subscribed or not: the flags, then the names.
	struct Found { std::string Name, Special; };
	std::vector<Found> all;
	clist *list = NULL;
	if (mailimap_list (Imap, "", "*", &list) == MAILIMAP_NO_ERROR)
	{
		for (clistiter *it = list ? clist_begin (list) : NULL; it; it = clist_next (it))
		{
			struct mailimap_mailbox_list *mb = (struct mailimap_mailbox_list *) clist_content (it);
			if (!mb || !mb->mb_name || strpbrk (mb->mb_name, "\r\n")) continue;
			Found f;
			f.Name = mb->mb_name;
			for (clistiter *o = mb->mb_flag && mb->mb_flag->mbf_oflags ? clist_begin (mb->mb_flag->mbf_oflags) : NULL; o;
				 o = clist_next (o))
			{
				struct mailimap_mbx_list_oflag *of = (struct mailimap_mbx_list_oflag *) clist_content (o);
				if (of && of->of_type == MAILIMAP_MBX_LIST_OFLAG_FLAG_EXT && of->of_flag_ext) f.Special = of->of_flag_ext;
			}
			all.push_back (f);
		}
	}
	if (list) mailimap_list_result_free (list);
	if (Broken || !Net->IsOpen ()) { Broken = true; return ""; }

	std::string found;
	for (const Found &f : all)
		if (strcasecmp (f.Special.c_str (), use.c_str ()) == 0) { found = f.Name; break; }
	for (int k = 0; found.empty () && names && names[k]; ++k)
		for (const Found &f : all)
			if (strcasecmp (f.Name.c_str (), names[k]) == 0 || strcasecmp (f.Name.c_str (), (Prefix + names[k]).c_str ()) == 0)
			{
				found = f.Name;
				break;
			}
	if (found.empty ())
	{
		if (!create) return "";
		std::string name = Prefix + use;
		int r = mailimap_create (Imap, name.c_str ());
		if (r != MAILIMAP_NO_ERROR)
		{
			Failed (r, "Cannot create the " + use + " folder on " + Host);
			return "";
		}
		found = name;
	}
	// Shown here like the subscribed folders.
	bool listed = false;
	for (const Folder &f : Folders) if (f.Server == found) listed = true;
	if (!listed)
	{
		mailimap_subscribe (Imap, found.c_str ());
		Folder f;
		f.Server = found;
		f.Delim = Folders.empty () ? '/' : Folders[0].Delim;
		f.Client = ClientName (found, f.Delim, Prefix);
		f.Special = use;
		Folders.push_back (f);
		if (Parent->EnsureFolder (f.Client))
			Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	}
	return SpecialCache[use] = found;
}

void IMAP4Handler::QueueOp (char kind, const char *msgid, const std::string &target)
{
	std::string hash;
	uint32_t v, u;
	if (!SplitMsgID (msgid, hash, v, u)) return;
	Op op;
	op.Kind = kind;
	op.MsgID = msgid;
	op.Target = target;
	Ops.push_back (op);
	Pending.erase (msgid);		// its flags go with it (a move keeps them on the server)
}

// Delete: to the Trash, or for good in the Trash. The window has removed
// the message here already; others deleted at the same time go along.
void IMAP4Handler::DeleteMessage (char *msgid)
{
	if (!StateLoaded) LoadState ();
	QueueOp ('T', msgid, "");
	ITCMessage *M;
	while ((M = Parent->ToCollector->GetNextMessage (CLASS_DELETE)))
	{
		QueueOp ('T', M->Message->Get (0), "");
		delete M;
	}
	PushQuietly ();
}

// Moved to another mailbox, which has it now: removed for good, not put in
// the Trash.
void IMAP4Handler::RemoveAfterTransfer (char *msgid)
{
	if (!StateLoaded) LoadState ();
	QueueOp ('X', msgid, "");
	PushQuietly ();
}

// A move to another folder of this account. The message leaves its folder
// here when the server has moved it, and shows up in the other at the
// check the window sends after the moves.
void IMAP4Handler::MoveMessage (char *msgid, const char *folder)
{
	if (!StateLoaded) LoadState ();
	auto target = [this](const char *client) -> std::string
	{
		for (const Folder &f : Folders) if (f.Client == client) return f.Server;
		return "";
	};
	if (Folders.empty ())
	{
		// No check yet since the start: the folders are needed.
		int jid = JOBQ.NewJob (Parent->Name);
		if (Session (jid)) ListFolders (jid);
		JOBQ.EndJob (jid);
	}
	std::string t = target (folder);
	if (t.empty ()) Report (std::string ("There is no folder \"") + folder + "\" on " + Host + ".");
	else QueueOp ('M', msgid, t);
	ITCMessage *M;
	while ((M = Parent->ToCollector->GetNextMessage (CLASS_MOVE)))
	{
		if (M->Message->Count >= 2)
		{
			std::string t2 = target (M->Message->Get (1));
			if (!t2.empty ()) QueueOp ('M', M->Message->Get (0), t2);
		}
		delete M;
	}
	PushQuietly ();
}

// Marks messages of the selected folder \Deleted and removes them: with
// UIDPLUS only those (UID EXPUNGE), else everything marked so (EXPUNGE).
bool IMAP4Handler::RemoveUids (const std::string &server, const std::vector<uint32_t> &uids)
{
	struct mailimap_set *set = mailimap_set_new_empty ();
	for (uint32_t u : uids) mailimap_set_add_single (set, u);
	struct mailimap_flag_list *fl = mailimap_flag_list_new_empty ();
	mailimap_flag_list_add (fl, mailimap_flag_new_deleted ());
	struct mailimap_store_att_flags *att = mailimap_store_att_flags_new_add_flags_silent (fl);
	int r = mailimap_uid_store (Imap, set, att);
	mailimap_store_att_flags_free (att);
	if (r == MAILIMAP_NO_ERROR)
		r = Capability ("UIDPLUS") ? mailimap_uid_expunge (Imap, set) : mailimap_expunge (Imap);
	mailimap_set_free (set);
	if (r != MAILIMAP_NO_ERROR)
	{
		std::string name = server;
		for (const Folder &f : Folders) if (f.Server == server) name = f.Client;
		return Failed (r, "Cannot remove messages from " + name + " on " + Host);
	}
	return true;
}

// Sends the queued deletes and moves, per folder and kind. A change the
// server refuses is reported and given up; its messages come back here at
// the next check.
bool IMAP4Handler::PushOps (int jid)
{
	if (Ops.empty ()) return true;
	std::vector<bool> done (Ops.size (), false);
	// folder -> (kind, target) -> indexes into Ops
	std::map<std::string, std::map<std::pair<char, std::string>, std::vector<size_t>>> work;
	for (size_t i = 0; i < Ops.size (); ++i)
	{
		std::string hash, server;
		uint32_t v, u;
		if (SplitMsgID (Ops[i].MsgID.c_str (), hash, v, u)) server = ServerOf (hash);
		// Gone or renumbered on the server meanwhile: nothing to do there.
		if (server.empty () || State[server].Validity != v || !State[server].Uids.count (u)) { done[i] = true; continue; }
		work[server][{ Ops[i].Kind, Ops[i].Target }].push_back (i);
	}

	bool ok = true;
	for (auto &w : work)
	{
		const std::string &server = w.first;
		FolderState &st = State[server];
		std::string name = server;
		for (const Folder &f : Folders) if (f.Server == server) name = f.Client;
		if (jid) ShowStatus (jid, "Changing messages in " + name);
		if (!Select (server))
		{
			if (Broken) { ok = false; break; }
			if (Selected == server)
				Report ("The folder " + name + " on " + Host + " cannot be changed (read-only), so its messages"
						" cannot be deleted or moved there.");
			// Read-only, or gone: given up; a folder still there shows its
			// messages again.
			for (auto &k : w.second)
				for (size_t i : k.second)
				{
					std::string hash;
					uint32_t v, u;
					SplitMsgID (Ops[i].MsgID.c_str (), hash, v, u);
					st.Uids.erase (u);
					st.UidNext = 0;
					done[i] = true;
				}
			continue;
		}
		if (SelValidity != st.Validity) continue;		// the next check sorts it out

		for (auto &k : w.second)
		{
			char kind = k.first.first;
			std::string target = k.first.second;
			if (kind == 'T')
			{
				target = SpecialFolder ("Trash", true);
				if (Broken) { ok = false; break; }
				if (target.empty ()) continue;			// reported; tried again later
				if (target == server) kind = 'X';		// deleted in the Trash: for good
				if (!Select (server)) { ok = !Broken; break; }
			}
			std::vector<uint32_t> uids;
			for (size_t i : k.second)
			{
				std::string hash;
				uint32_t v, u;
				SplitMsgID (Ops[i].MsgID.c_str (), hash, v, u);
				uids.push_back (u);
			}

			bool good;
			if (kind == 'X') good = RemoveUids (server, uids);
			else
			{
				struct mailimap_set *set = mailimap_set_new_empty ();
				for (uint32_t u : uids) mailimap_set_add_single (set, u);
				int r;
				if (Capability ("MOVE")) r = mailimap_uid_move (Imap, set, target.c_str ());
				else r = mailimap_uid_copy (Imap, set, target.c_str ());
				mailimap_set_free (set);
				std::string tname = target;
				for (const Folder &f : Folders) if (f.Server == target) tname = f.Client;
				if (r != MAILIMAP_NO_ERROR) good = Failed (r, "Cannot move messages from " + name + " to " + tname);
				// Without MOVE: copied, now removed from here.
				else good = Capability ("MOVE") || RemoveUids (server, uids);
			}
			if (Broken) { ok = false; break; }
			for (size_t i : k.second)
			{
				done[i] = true;
				std::string hash;
				uint32_t v, u;
				SplitMsgID (Ops[i].MsgID.c_str (), hash, v, u);
				if (good)
				{
					// Gone from this folder: from the list here too (a move).
					st.Uids.erase (u);
					if (Parent->Have (&Ops[i].MsgID[0]))
						Parent->ToUI->PostMessage (CLASS_GONE, NO_SUBCLASS, Parent->Name, Ops[i].MsgID.c_str ());
				}
				else
				{
					// Refused: the message is shown again at the next check.
					st.Uids.erase (u);
					st.UidNext = 0;
				}
			}
		}
		if (!ok) break;
	}
	std::vector<Op> left;
	for (size_t i = 0; i < Ops.size (); ++i) if (!done[i]) left.push_back (Ops[i]);
	Ops.swap (left);
	SaveState ();
	return ok;
}

//-------------------------------------------------------------------------------
// Messages stored here from elsewhere: the spool
//-------------------------------------------------------------------------------

std::string IMAP4Handler::SpoolDir (void)
{
	return std::string (Parent->DirectoryPrefix) + "/spool";
}

// Called from other mailboxes' threads (a copy or move to this mailbox, a
// copy of sent mail): the message is written to the spool, safely on disk,
// and sent at this mailbox's next check. folder is a folder here, or
// "\Sent" for the server's Sent folder; flags are letters S F R P.
bool IMAP4Handler::DeliverRaw (const std::string &raw, const char *folder, const std::string &flags, std::string &err)
{
	static std::atomic<unsigned> counter (0);
	if (!folder || !*folder || strpbrk (folder, "\t\r\n"))
	{
		err = "no folder to store it in";
		return false;
	}
	std::string dir = SpoolDir ();
	if (mkdir (dir.c_str (), 0700) != 0 && errno != EEXIST)
	{
		err = "cannot create " + dir + ": " + strerror (errno);
		return false;
	}
	char name[96];
	snprintf (name, sizeof (name), "/%010ld.%d.%u", (long) time (NULL), (int) getpid (), counter++);
	std::string path = dir + name, tmp = path + ".tmp";
	std::string keep;
	for (char c : flags) if (strchr ("SFRP", c)) keep += c;
	int fd = open (tmp.c_str (), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	FILE *F = fd >= 0 ? fdopen (fd, "w") : NULL;
	if (!F)
	{
		if (fd >= 0) close (fd);
		err = "cannot write " + tmp + ": " + strerror (errno);
		return false;
	}
	fprintf (F, "Neo-PostOffice-Spool: %s\t%s\n", folder, keep.c_str ());
	bool ok = fwrite (raw.data (), 1, raw.size (), F) == raw.size ();
	ok = fflush (F) == 0 && ok;
	ok = fsync (fileno (F)) == 0 && ok;
	ok = (fclose (F) == 0) && ok;
	if (!ok || rename (tmp.c_str (), (path + ".msg").c_str ()) != 0)
	{
		err = "cannot write " + tmp + ": " + strerror (errno);
		unlink (tmp.c_str ());
		return false;
	}
	return true;
}

// Sends the spooled messages with APPEND, oldest first. One the server
// refuses (over quota, too large, no such folder) is kept as
// "*.failed" and reported.
bool IMAP4Handler::FlushSpool (int jid)
{
	std::string dir = SpoolDir ();
	DIR *D = opendir (dir.c_str ());
	if (!D) return true;
	std::vector<std::string> files;
	while (struct dirent *e = readdir (D))
	{
		std::string n = e->d_name;
		if (n.size () > 4 && n.compare (n.size () - 4, 4, ".msg") == 0) files.push_back (n);
	}
	closedir (D);
	std::sort (files.begin (), files.end ());

	size_t k = 0;
	for (const std::string &n : files)
	{
		if (Parent->ToCollector->UrgentPending || Broken) return false;
		std::string path = dir + "/" + n;
		auto give_up = [&](const std::string &why)
		{
			std::string kept = path.substr (0, path.size () - 4) + ".failed";
			rename (path.c_str (), kept.c_str ());
			Report (why + " The message is kept in " + kept + ".");
		};
		FILE *F = fopen (path.c_str (), "r");
		if (!F) continue;
		std::string text;
		char buf[65536];
		size_t got;
		while ((got = fread (buf, 1, sizeof (buf), F)) > 0) text.append (buf, got);
		fclose (F);
		size_t nl = text.find ('\n'), tab = text.find ('\t');
		if (text.compare (0, 22, "Neo-PostOffice-Spool: ") != 0 || nl == std::string::npos || tab == std::string::npos || tab > nl)
		{
			give_up ("A message waiting to be stored on " + Host + " is damaged.");
			continue;
		}
		std::string folder = text.substr (22, tab - 22), flags = text.substr (tab + 1, nl - tab - 1);
		std::string server;
		if (folder == "\\Sent") server = SpecialFolder ("Sent", true);
		else for (const Folder &f : Folders) if (f.Client == folder) server = f.Server;
		if (Broken) return false;
		if (server.empty ())
		{
			give_up ("The folder \"" + (folder == "\\Sent" ? std::string ("Sent") : folder) + "\" does not exist on " + Host + ".");
			continue;
		}

		ShowStatus (jid, "Storing messages on the server: " + std::to_string (++k) + " of " + std::to_string (files.size ()));
		// As sent on the wire: CRLF line ends, and no mbox "From " line in
		// front (left by some Maildir and mbox tools; it is no header).
		size_t start = nl + 1;
		if (text.compare (start, 5, "From ") == 0)
		{
			size_t e = text.find ('\n', start);
			start = e == std::string::npos ? text.size () : e + 1;
		}
		std::string body;
		body.reserve (text.size () - start + text.size () / 30);
		for (size_t i = start; i < text.size (); ++i)
		{
			if (text[i] == '\n' && (i == start || text[i - 1] != '\r')) body += '\r';
			body += text[i];
		}
		struct mailimap_flag_list *fl = mailimap_flag_list_new_empty ();
		for (char c : flags) mailimap_flag_list_add (fl, imap_flag (c));
		int r = mailimap_append (Imap, server.c_str (), fl, NULL, body.data (), body.size ());
		mailimap_flag_list_free (fl);
		if (r == MAILIMAP_NO_ERROR) { unlink (path.c_str ()); continue; }
		if (r == MAILIMAP_ERROR_STREAM || r == MAILIMAP_ERROR_PARSE || !Net->IsOpen ())
		{
			Failed (r, "The connection to " + Host + " was lost");
			return false;
		}
		give_up ("The IMAP server " + Host + " did not store a message in " + folder + ": " + Why (r) + ".");
	}
	return true;
}

//-------------------------------------------------------------------------------
// Push (step 5)
//-------------------------------------------------------------------------------

// "Push new mail at once (IMAP IDLE)" in Config -> Mailboxes; on unless "N".
bool IMAP4Handler::PushWanted (void)
{
	return strcasecmp (Parameters->Get_char ("idle"), "N") != 0;
}

// The mailbox thread keeps the connection open: it is in IDLE, or about to
// be. A server without IDLE is checked at the interval only.
bool IMAP4Handler::KeepsConnection (void)
{
	return PushWanted () && (Dropped || (Imap && Capability ("IDLE")));
}

// IDLE on the Inbox. False if it could not be started.
bool IMAP4Handler::StartIdle (void)
{
	if (Idling) return true;
	if (!Imap || Broken || !Net->IsOpen () || !Capability ("IDLE")) return false;
	std::string inbox = "INBOX";
	for (const Folder &f : Folders) if (f.Client == "Inbox") inbox = f.Server;
	if (!Examine (inbox))
	{
		if (Broken || !Net->IsOpen ()) Lost ();
		return false;
	}
	int r = mailimap_idle (Imap);
	if (r != MAILIMAP_NO_ERROR)
	{
		if (r == MAILIMAP_ERROR_STREAM || r == MAILIMAP_ERROR_PARSE || !Net->IsOpen ()) Lost ();
		return false;
	}
	Idling = true;
	IdleStarted = time (NULL);
	return true;
}

// DONE: back to normal commands. What the server sent meanwhile has been
// read; a failure leaves the session broken.
void IMAP4Handler::StopIdle (void)
{
	if (!Idling) return;
	Idling = false;
	if (mailimap_idle_done (Imap) != MAILIMAP_NO_ERROR) Broken = true;
}

// The connection went away while pushing: try again later, quietly.
void IMAP4Handler::Lost (const std::string &why)
{
	time_t now = time (NULL);
	if (!Dropped)
	{
		Dropped = true;
		DownSince = now;
		Tries = 0;
		ToldDown = false;
		DownWhy = !why.empty () ? why : !Net->Error ().empty () ? Net->Error () : "the connection was lost";
	}
	NextTry = now + 10;
	EndSession (false);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

// The Inbox brought up to date after news from the server.
void IMAP4Handler::QuickSync (int jid)
{
	for (const Folder &f : Folders)
	{
		if (f.Client != "Inbox") continue;
		int added = 0;
		SyncFolder (jid, f, 0, 1, added);
		SaveState ();
		Parent->SaveMailBox ();
		if (added)
		{
			ShowStatus (jid, std::to_string (added) + (added == 1 ? " new message" : " new messages"));
			Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
		}
		break;
	}
}

// Called by the mailbox thread while it has nothing else to do, a few
// times a second. Cheap when nothing happens: one poll() of the socket.
void IMAP4Handler::Idle (void)
{
	if (!PushWanted ())
	{
		Dropped = false;
		return;
	}
	time_t now = time (NULL);

	// Push starts by itself after the program starts, even if the mailbox
	// is never checked on a timer.
	if (!PushBegun)
	{
		PushBegun = true;
		if (!Imap && !Dropped)
		{
			Dropped = true;
			DownSince = now;
			NextTry = now;
			DownWhy = "no connection yet";
		}
	}

	if (Dropped)
	{
		// Told once, 10 minutes after it went (whenever the next try is).
		if (!ToldDown && now - DownSince >= 600)
		{
			ToldDown = true;
			char when[16];
			struct tm tmv;
			localtime_r (&DownSince, &tmv);
			strftime (when, sizeof (when), "%H:%M", &tmv);
			std::string why = DownWhy;
			while (!why.empty () && (why.back () == '.' || why.back () == ' ')) why.pop_back ();
			Report ("The IMAP server " + (Host.empty () ? std::string (Parameters->Get_char ("host")) : Host) +
					" has been out of reach since " + when + " (" + why + "). It is tried again every few"
					" minutes, and new mail shows again when it is back.");
		}
		if (now < NextTry) return;
		if (!StateLoaded) LoadState ();
		int jid = JOBQ.NewJob (Parent->Name);
		bool quiet = Quiet;
		Quiet = true;
		bool up = Session (jid) && ListFolders (jid);
		Quiet = quiet;
		if (!up)
		{
			EndSession (false);
			static const int waits[] = { 10, 30, 60, 120, 300 };
			++Tries;
			NextTry = now + waits[std::min (Tries, 5) - 1];
			JOBQ.EndJob (jid);
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
			return;
		}
		// Back: what waited goes out, then the Inbox is brought up to date.
		Dropped = false;
		Tries = 0;
		if (ToldDown) LastError.clear ();
		ToldDown = false;
		SyncFolders ();
		if (PushPending (jid) && PushOps (jid)) FlushSpool (jid);
		if (!Broken) QuickSync (jid);
		JOBQ.EndJob (jid);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
		if (Broken) { Lost (); return; }
	}

	// No session (closed by Stop, or none yet): push starts after the next check.
	if (!Imap || Broken || !Net->IsOpen ()) return;
	if (!Idling)
	{
		StartIdle ();
		return;
	}

	// Anything from the server? Bytes already read (by libetpan or OpenSSL)
	// do not show on the socket.
	bool news = Net->Pending () > 0 || (Imap->imap_stream && Imap->imap_stream->read_buffer_len > 0);
	if (!news)
	{
		struct pollfd p;
		p.fd = Net->Fd ();
		p.events = POLLIN;
		p.revents = 0;
		news = poll (&p, 1, 0) > 0;
	}
	// Renewed every 10 minutes: servers end IDLE after 30, and routers
	// forget quiet connections; a dead one shows here.
	if (!news && now - IdleStarted < 600) return;

	StopIdle ();
	if (Broken || !Net->IsOpen ()) { Lost (); return; }
	if (news)
	{
		int jid = JOBQ.NewJob (Parent->Name);
		QuickSync (jid);
		JOBQ.EndJob (jid);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
		if (Broken || !Net->IsOpen ()) { Lost (); return; }
	}
	StartIdle ();
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

// The folders listed again and every folder brought up to date, after
// sending the changes made here: the work of a check once connected.
bool IMAP4Handler::Refresh (int jid)
{
	if (!ListFolders (jid)) return false;
	SyncFolders ();
	// Changes made here go first, so what is read below includes them.
	if (PushPending (jid) && PushOps (jid)) FlushSpool (jid);
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
	return !Broken;
}

void IMAP4Handler::CheckGroup (const char * /*group*/)
{
	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Progress (jid, 1);
	if (!StateLoaded) LoadState ();
	// With push on, a check of the timer that cannot connect (after the
	// computer slept, say) is quiet like the push: retried, and told after
	// 10 minutes. A check the user starts says what is wrong at once.
	bool quietly = PushWanted () && !UserCheck;
	bool quiet = Quiet;
	if (quietly) Quiet = true;
	bool up = Session (jid);
	Quiet = quiet;
	if (!up && quietly)
	{
		Lost (DownWhy);
		JOBQ.EndJob (jid);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		return;
	}
	if (up)
	{
		// Connected: no longer down.
		Dropped = false;
		Tries = 0;
		ToldDown = false;
	}
	if (up) Refresh (jid);
	if (Broken || Parent->ToCollector->UrgentPending) EndSession (false);
	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}

//-------------------------------------------------------------------------------
// Folder management (step 6)
//-------------------------------------------------------------------------------

// The window checks names with the Maildir rules first (no = { } " , and
// "/" only between levels); here what IMAP adds.
std::string IMAP4Handler::CheckFolderName (const std::string &name)
{
	if (name.find_first_of ("*%") != std::string::npos) return "IMAP folder names cannot contain * or %.";
	if (strcasecmp (name.c_str (), "Inbox") == 0 || strncasecmp (name.c_str (), "Inbox/", 6) == 0)
		return "Inbox is the server's own folder; choose another name.";
	if (!NameDelim && name.find ('/') != std::string::npos)
		return "This server has no subfolders: the name cannot contain /.";
	return "";
}

bool IMAP4Handler::IsFixedFolder (const char *name)
{
	return name && strcasecmp (name, "Inbox") == 0;
}

// Every folder on the server (LIST), and if asked the subscribed ones (LSUB).
bool IMAP4Handler::ListAll (std::vector<Listed> &all, std::set<std::string> *subscribed)
{
	all.clear ();
	clist *list = NULL;
	int r = mailimap_list (Imap, "", "*", &list);
	if (r != MAILIMAP_NO_ERROR)
	{
		if (list) mailimap_list_result_free (list);
		return Failed (r, "Cannot list the folders on " + Host);
	}
	for (clistiter *it = list ? clist_begin (list) : NULL; it; it = clist_next (it))
	{
		struct mailimap_mailbox_list *mb = (struct mailimap_mailbox_list *) clist_content (it);
		if (!mb || !mb->mb_name || strpbrk (mb->mb_name, "\r\n")) continue;
		Listed l;
		l.Server = mb->mb_name;
		l.Delim = mb->mb_delimiter;
		l.Selectable = true;
		if (mb->mb_flag)
		{
			if (mb->mb_flag->mbf_type == MAILIMAP_MBX_LIST_FLAGS_SFLAG &&
				mb->mb_flag->mbf_sflag == MAILIMAP_MBX_LIST_SFLAG_NOSELECT)
				l.Selectable = false;
			for (clistiter *o = mb->mb_flag->mbf_oflags ? clist_begin (mb->mb_flag->mbf_oflags) : NULL; o; o = clist_next (o))
			{
				struct mailimap_mbx_list_oflag *of = (struct mailimap_mbx_list_oflag *) clist_content (o);
				if (of && of->of_type == MAILIMAP_MBX_LIST_OFLAG_FLAG_EXT && of->of_flag_ext &&
					(strcasecmp (of->of_flag_ext, "NonExistent") == 0 || strcasecmp (of->of_flag_ext, "Noselect") == 0))
					l.Selectable = false;
			}
		}
		all.push_back (l);
	}
	if (list) mailimap_list_result_free (list);

	if (subscribed)
	{
		subscribed->clear ();
		list = NULL;
		r = mailimap_lsub (Imap, "", "*", &list);
		if (r != MAILIMAP_NO_ERROR)
		{
			if (list) mailimap_list_result_free (list);
			return Failed (r, "Cannot list the subscribed folders on " + Host);
		}
		for (clistiter *it = list ? clist_begin (list) : NULL; it; it = clist_next (it))
		{
			struct mailimap_mailbox_list *mb = (struct mailimap_mailbox_list *) clist_content (it);
			if (mb && mb->mb_name) subscribed->insert (mb->mb_name);
		}
		if (list) mailimap_list_result_free (list);
	}
	return true;
}

// For the Subscriptions dialog: the folders that can hold messages, but
// the Inbox, which is always shown.
void IMAP4Handler::SendFolderList (int jid)
{
	ShowStatus (jid, "Listing the folders");
	std::vector<Listed> all;
	std::set<std::string> subs;
	if (!ListAll (all, &subs)) return;
	std::vector<std::pair<std::string, const Listed *>> shown;
	for (const Listed &l : all)
	{
		if (!l.Selectable || strcasecmp (l.Server.c_str (), "INBOX") == 0) continue;
		std::string name = ClientName (l.Server, l.Delim, Prefix);
		if (name.empty () || name == "Inbox") continue;
		shown.push_back ({ name, &l });
	}
	std::sort (shown.begin (), shown.end ());
	WordList *W = new WordList;
	W->Add (Parent->Name);
	for (auto &s : shown)
	{
		W->Add (subs.count (s.second->Server) ? "1" : "0");
		W->Add (s.first.c_str ());
		W->Add (s.second->Server.c_str ());
	}
	Parent->ToUI->PostMessage (CLASS_FOLDERLIST, NO_SUBCLASS, W);
}

// One change on the server; a and b are names as shown here, except for
// "subscriptions", whose a is lines of "+server" or "-server". False (and
// told) if the server refused.
bool IMAP4Handler::FolderChange (int jid, const char *op, const std::string &a, const std::string &b)
{
	std::string inbox = "INBOX";
	for (const Folder &f : Folders) if (f.Client == "Inbox") inbox = f.Server;
	auto server_of = [this](const std::string &client, char &delim) -> std::string
	{
		for (const Folder &f : Folders)
			if (f.Client == client) { delim = f.Delim; return f.Server; }
		delim = NameDelim;
		return ServerName (client, NameDelim, Prefix);
	};
	// The folder open in this session must not be one that goes away.
	auto leave = [&](const std::string &server, char delim)
	{
		if (Selected.empty ()) return;
		if (Selected == server || (delim && Selected.compare (0, server.size () + 1, server + delim) == 0))
			Examine (inbox);
	};
	auto under = [](const std::string &name, const std::string &top, char delim)
	{
		return name == top || (delim && name.compare (0, top.size () + 1, top + delim) == 0);
	};
	SpecialCache.clear ();

	if (strcmp (op, "create") == 0)
	{
		char delim;
		std::string name = server_of (a, delim);
		ShowStatus (jid, "Creating the folder " + a);
		int r = mailimap_create (Imap, name.c_str ());
		if (r != MAILIMAP_NO_ERROR)
		{
			// Often the folder is there, but not subscribed to.
			std::vector<Listed> all;
			if (r == MAILIMAP_ERROR_CREATE && !Broken && Net->IsOpen () && ListAll (all, NULL))
				for (const Listed &l : all)
					if (l.Server == name)
					{
						Report ("The folder \"" + a + "\" is already on " + Host +
								", but not shown here. To show it, tick it in Folder \xe2\x86\x92 Subscriptions.");
						return false;
					}
			return Failed (r, "Cannot create the folder \"" + a + "\" on " + Host);
		}
		r = mailimap_subscribe (Imap, name.c_str ());
		if (r != MAILIMAP_NO_ERROR) return Failed (r, "The folder \"" + a + "\" was created, but cannot be subscribed to");
		return true;
	}
	if (strcmp (op, "rename") == 0)
	{
		char delim;
		std::string from = server_of (a, delim), to = ServerName (b, delim ? delim : NameDelim, Prefix);
		std::set<std::string> subs;
		std::vector<Listed> all;
		if (!ListAll (all, &subs)) return false;
		leave (from, delim);
		ShowStatus (jid, "Renaming the folder " + a);
		int r = mailimap_rename (Imap, from.c_str (), to.c_str ());
		if (r != MAILIMAP_NO_ERROR) return Failed (r, "Cannot rename the folder \"" + a + "\" on " + Host);
		// The server renames the subfolders too; the subscriptions follow
		// (some servers move them, others leave the old names).
		for (const std::string &s : subs)
		{
			if (!under (s, from, delim)) continue;
			mailimap_unsubscribe (Imap, s.c_str ());
			if (Broken || !Net->IsOpen ()) { Broken = true; return false; }
			r = mailimap_subscribe (Imap, (to + s.substr (from.size ())).c_str ());
			if (r != MAILIMAP_NO_ERROR) return Failed (r, "The folder was renamed, but cannot be subscribed to");
		}
		return true;
	}
	if (strcmp (op, "delete") == 0)
	{
		char delim;
		std::string top = server_of (a, delim);
		std::vector<Listed> all;
		if (!ListAll (all, NULL)) return false;
		std::vector<std::string> doomed;
		for (const Listed &l : all) if (under (l.Server, top, delim)) doomed.push_back (l.Server);
		if (doomed.empty ()) doomed.push_back (top);
		// Subfolders first, deepest first.
		std::sort (doomed.begin (), doomed.end (), [](const std::string &x, const std::string &y)
				   { return x.size () != y.size () ? x.size () > y.size () : x > y; });
		leave (top, delim);
		ShowStatus (jid, "Deleting the folder " + a);
		for (const std::string &d : doomed)
		{
			mailimap_unsubscribe (Imap, d.c_str ());
			if (Broken || !Net->IsOpen ()) { Broken = true; return false; }
			int r = mailimap_delete (Imap, d.c_str ());
			if (r != MAILIMAP_NO_ERROR)
				return Failed (r, "Cannot delete the folder \"" + ClientName (d, delim, Prefix) + "\" on " + Host);
		}
		return true;
	}
	if (strcmp (op, "subscriptions") == 0)
	{
		ShowStatus (jid, "Changing the subscriptions");
		std::string refused;
		size_t start = 0;
		while (start < a.size ())
		{
			size_t end = a.find ('\n', start);
			if (end == std::string::npos) end = a.size ();
			std::string line = a.substr (start, end - start);
			start = end + 1;
			if (line.size () < 2 || (line[0] != '+' && line[0] != '-')) continue;
			std::string name = line.substr (1);
			int r = line[0] == '+' ? mailimap_subscribe (Imap, name.c_str ()) : mailimap_unsubscribe (Imap, name.c_str ());
			if (r == MAILIMAP_ERROR_STREAM || r == MAILIMAP_ERROR_PARSE || !Net->IsOpen ())
				return Failed (r, "Cannot change the subscriptions on " + Host);
			if (r != MAILIMAP_NO_ERROR)
				refused += (refused.empty () ? "" : ", ") + ClientName (name, NameDelim, Prefix);
		}
		if (!refused.empty ())
		{
			Report ("The server " + Host + " refused to change the subscription of: " + refused + ".");
			return false;
		}
		return true;
	}
	return false;
}

// Folder management from the window (CLASS_FOLDEROP): "create", "rename",
// "delete", "subscriptions", or "list" for the Subscriptions dialog.
// Changes waiting to be sent go first; after a change everything is read
// again, as at a check.
void IMAP4Handler::FolderOp (const char *op, const char *a, const char *b)
{
	if (!StateLoaded) LoadState ();
	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Progress (jid, 1);
	bool up = Session (jid);
	if (up && Folders.empty ()) up = ListFolders (jid);
	if (up)
	{
		Dropped = false;
		Tries = 0;
		ToldDown = false;
		if (strcmp (op, "list") == 0) SendFolderList (jid);
		else
		{
			if (PushPending (jid) && PushOps (jid)) FlushSpool (jid);
			if (!Broken) FolderChange (jid, op, a ? a : "", b ? b : "");
			if (!Broken) Refresh (jid);
		}
	}
	if (Broken) EndSession (false);
	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}


//===============================================================================
// Search (Neo-PostOffice): UID SEARCH in each folder, the server matching
// without regard to case (FROM, TO or CC, SUBJECT, BODY, TEXT); the UIDs
// found become the messages' Message-IDs here. Only messages the client
// has listed are returned.
//===============================================================================

int IMAP4Handler::Search (const std::vector<std::string> &folders, int field, const std::string &text,
						  std::vector<std::string> &ids, std::string &error)
{
	if (!StateLoaded) LoadState ();
	int jid = JOBQ.NewJob (Parent->Name);
	if (!Session (jid))
	{
		JOBQ.EndJob (jid);
		error = "Cannot reach the IMAP server " + Host + " to search" +
				(LastError.empty () ? std::string (".") : ": " + LastError);
		LastError.clear ();
		return 0;
	}
	bool ascii = true;
	for (unsigned char c : text) if (c >= 0x80) ascii = false;
	// CHARSET UTF-8 only when needed: some servers know no charsets.
	const char *charset = ascii ? NULL : "UTF-8";
	int done = 0;
	size_t k = 0;
	for (const std::string &client : folders)
	{
		++k;
		std::string server;
		for (const Folder &f : Folders) if (f.Client == client) { server = f.Server; break; }
		if (server.empty ()) server = ServerName (client, NameDelim, Prefix);
		auto st = State.find (server);
		if (st == State.end ()) continue;
		ShowStatus (jid, "Searching " + client + " (" + std::to_string (k) + " of " + std::to_string (folders.size ()) + ")");
		if (!Examine (server))
		{
			if (Broken) break;
			continue;
		}
		if (SelValidity != st->second.Validity) continue;

		auto key = [&] (int f) -> struct mailimap_search_key * {
			char *t = strdup (text.c_str ());
			switch (f)
			{
				case 0: return mailimap_search_key_new_from (t);
				case 2: return mailimap_search_key_new_subject (t);
				case 3: return mailimap_search_key_new_body (t);
				case 4: return mailimap_search_key_new_text (t);
				case 1:
				{
					char *t2 = strdup (text.c_str ());
					return mailimap_search_key_new_or (mailimap_search_key_new_to (t),
													   mailimap_search_key_new_cc (t2));
				}
			}
			free (t);
			return NULL;
		};
		struct mailimap_search_key *sk = key (field);
		if (!sk) break;
		clist *res = NULL;
		int r = mailimap_uid_search (Imap, charset, sk, &res);
		mailimap_search_key_free (sk);
		if (r != MAILIMAP_NO_ERROR)
		{
			if (res) mailimap_search_result_free (res);
			if (r == MAILIMAP_ERROR_STREAM || r == MAILIMAP_ERROR_PARSE || !Net->IsOpen ()) { Broken = true; break; }
			error = "The IMAP server " + Host + " could not search " + client + ": " + Why (r) + ".";
			continue;
		}
		++done;
		for (clistiter *it = res ? clist_begin (res) : NULL; it; it = clist_next (it))
		{
			uint32_t uid = *(uint32_t *) clist_content (it);
			std::string id = MakeMsgID (st->second.Hash, st->second.Validity, uid);
			if (Parent->Find ((char *) id.c_str ())) ids.push_back (id);
		}
		if (res) mailimap_search_result_free (res);
	}
	bool broken = Broken;
	if (Broken) EndSession (false);
	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
	if (broken)
	{
		error = "The connection to " + Host + " was lost during the search.";
		return 0;
	}
	if (!done && !folders.empty () && !error.empty ()) return 0;
	error.clear ();
	return 1;
}

#endif // PO_HAVE_LIBETPAN
