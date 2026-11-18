//===============================================================================
// IMAP4Handler (Neo-PostOffice)
// -----------------------------
// An IMAP4rev1 client (RFC 3501) built on libetpan. libetpan parses the
// protocol; the connection is a NetStream, the same one the POP3 client
// uses, so TLS, STARTTLS and certificate checks are done one way only:
// against the system's CAs plus an optional CA file, and against the host
// name, with no way to turn that off.
//
// This is step 1 of the IMAP client: it connects, logs in (with the "pass"
// setting or the first line printed by "passcmd") and keeps the mailbox's
// folders in step with the folders subscribed to on the server. INBOX is
// always shown, as "Inbox". Messages come in step 2.
//
// GNU LGPL like PostOffice itself.
//===============================================================================

#ifdef PO_HAVE_LIBETPAN

#include "mailbox.h"
#include "netstream.h"
#include "sendpipe.h"
#include "var.h"
#include "status.h"

#include <libetpan/libetpan.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <algorithm>
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
	if (Imap && Net->IsOpen ())
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
// folder is gone (or no longer subscribed) are removed by the window.
void IMAP4Handler::SyncFolders (void)
{
	bool changed = false;
	for (const Folder &f : Folders)
		if (Parent->EnsureFolder (f.Client)) changed = true;

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

void IMAP4Handler::CheckGroup (const char * /*group*/)
{
	int jid = JOBQ.NewJob (Parent->Name);
	if (Session (jid) && ListFolders (jid))
	{
		SyncFolders ();
		LastError.clear ();
		ShowStatus (jid, std::to_string (Folders.size ()) + (Folders.size () == 1 ? " folder" : " folders"));
	}
	// Step 1 keeps no connection open between checks.
	EndSession (true);
	JOBQ.EndJob (jid);
}

#endif // PO_HAVE_LIBETPAN
