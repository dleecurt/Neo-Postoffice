//===============================================================================
// POP3Handler (Neo-PostOffice)
// ----------------------------
// Collects mail from a POP3 server (RFC 1939), over TLS (port 995), after
// STLS (RFC 2595, port 110); never without encryption, as the password
// would be sent in the clear. Rewritten for Neo-PostOffice on NetStream; the settings and
// the uidl.dat file of PostOffice 0.7 are kept.
//
// - Every operation (check, fetching a body, deleting, expiring) is one
//   session: connect, log in, UIDL, the work, QUIT. POP3 servers lock the
//   mailbox during a session and only show new mail to a new one, and
//   deletions only take effect at QUIT.
// - A message is known by its UIDL. uidl.dat maps each UIDL to the
//   Message-ID it was filed under; a UIDL in it is never fetched again.
//   Messages without a Message-ID get a stable one made from the UIDL.
// - Collection: 0 fetches headers (the body when the message is opened),
//   1 whole messages, 2 whole messages that are then deleted from the
//   server. Deleting happens only after the messages and uidl.dat are
//   saved, so a crash cannot lose mail.
// - The password is the "pass" setting, or the first line printed by the
//   "passcmd" setting (pass show ..., secret-tool lookup ...).
//
// GNU LGPL like PostOffice itself.
//===============================================================================

#include "mailbox.h"
#include "mailheaders.h"
#include "netstream.h"
#include "sendpipe.h"
#include "var.h"
#include "status.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "debug.h"

extern JobList JOBQ;

POP3Handler::POP3Handler (MailBox *_Parent, class Var *_Parameters)
{
	Parameters = new Var (_Parameters);
	Parent = _Parent;
	ConnectionOpen = 0;
	Net = new NetStream;
	Net->Cancelled = [this]() { return Parent->ToCollector->UrgentPending; };
	LoadUidl ();
	Parent->ToCollector->AddPriorityClass (CLASS_GETBODY);
}

POP3Handler::~POP3Handler (void)
{
	delete Net;
	delete Parameters;
}

std::string POP3Handler::UidlFile (void)
{
	return std::string (Parent->DirectoryPrefix) + "/uidl.dat";
}

// uidl.dat has a line 'UIDL = "Message-ID"' for each message, as in
// PostOffice 0.7 (which kept it in a Var).
void POP3Handler::LoadUidl (void)
{
	Uidl.clear ();
	FILE *F = fopen (UidlFile().c_str(), "r");
	if (!F) return;
	char line[1024];
	while (fgets (line, sizeof (line), F))
	{
		char *eq = strstr (line, " = \"");
		char *end = strrchr (line, '"');
		if (!eq || !end || end <= eq + 3) continue;
		std::string uid (line, eq - line);
		size_t a = uid.find_first_not_of (" \t");
		if (a == std::string::npos) continue;
		Uidl[uid.substr (a)] = std::string (eq + 4, end - (eq + 4));
	}
	fclose (F);
}

// Written whole to a private file, then renamed over the old one.
void POP3Handler::SaveUidl (void)
{
	std::string tmp = UidlFile() + ".new";
	int fd = open (tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	FILE *F = fd >= 0 ? fdopen (fd, "w") : NULL;
	if (!F)
	{
		if (fd >= 0) close (fd);
		eprintf ("[pop3] cannot save %s\n", tmp.c_str());
		return;
	}
	for (auto &kv : Uidl) fprintf (F, "%s = \"%s\"\n", kv.first.c_str(), kv.second.c_str());
	bool ok = fflush (F) == 0 && fsync (fileno (F)) == 0;
	ok = (fclose (F) == 0) && ok;
	if (!ok || rename (tmp.c_str(), UidlFile().c_str()) != 0)
		eprintf ("[pop3] cannot save %s\n", UidlFile().c_str());
}

// Errors are shown once, until a different one happens or an operation
// succeeds, so that a server that is down does not bring up a dialog at
// every automatic check.
void POP3Handler::Report (const std::string &what)
{
	if (Quiet || what == LastError || Parent->ToCollector->UrgentPending) return;
	LastError = what;
	Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, Parent->Name, what.c_str());
}

void POP3Handler::ShowStatus (int jid, const std::string &text)
{
	JOBQ.Stat (jid, (char *) text.c_str());
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}

//-------------------------------------------------------------------------------
// Protocol
//-------------------------------------------------------------------------------

// Sends a command (if any) and reads its one-line reply. A "-ERR" reply
// returns false with the server's text in reply; a connection problem
// returns false with the stream closed.
bool POP3Handler::Command (const std::string &cmd, std::string &reply)
{
	if (!cmd.empty() && !Net->Write (cmd + "\r\n")) { reply = Net->Error(); return false; }
	if (!Net->ReadLine (reply)) { reply = Net->Error(); return false; }
	if (reply.compare (0, 3, "+OK") == 0) return true;
	if (reply.compare (0, 4, "-ERR") == 0)
	{
		reply = reply.substr (4);
		size_t a = reply.find_first_not_of (' ');
		reply = a == std::string::npos ? "" : reply.substr (a);
		if (reply.empty()) reply = "no reason given";
		return false;
	}
	reply = "unexpected reply from the server: " + reply.substr (0, 200);
	Net->Close ();
	return false;
}

// The text after a positive multi-line reply, up to the line ".", with the
// leading dot of dot-stuffed lines removed and LF line ends.
bool POP3Handler::ReadMulti (std::string &text, int jid, long size)
{
	std::string line;
	size_t reported = 0;
	text.clear ();
	for (;;)
	{
		if (!Net->ReadLine (line)) return false;
		if (line == ".") return true;
		if (!line.empty() && line[0] == '.') text.append (line, 1, std::string::npos);
		else text += line;
		text += '\n';
		if (jid && text.size() - reported > 65536)
		{
			reported = text.size();
			JOBQ.XFer (jid, (int) text.size());
			if (size > 0) JOBQ.Progress (jid, (int) std::min (100L, (long) (100.0 * text.size() / size)));
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		}
	}
}

// Connects, logs in and reads the UIDL list into Numbers.
bool POP3Handler::Session (int jid)
{
	if (Net->IsOpen()) return true;
	Numbers.clear ();
	Order.clear ();

	std::string host = Parameters->Get_char ("host");
	std::string security = Parameters->Get_char ("security");
	// No unencrypted login: anything but TLS is STARTTLS (also "none" and
	// the empty setting of mailboxes from PostOffice 0.7), which fails
	// rather than sending the password in the clear.
	if (security != "tls") security = "starttls";
	int port = Parameters->Get_int ("port");
	if (port <= 0) port = security == "tls" ? 995 : 110;
	std::string cafile = Parameters->Get_char ("cafile");
	if (cafile.compare (0, 2, "~/") == 0 && getenv ("HOME")) cafile = getenv ("HOME") + cafile.substr (1);

	if (host.empty())
	{
		Report (std::string ("No POP3 server is set for ") + Parent->Name + " (Config \xe2\x86\x92 Mailboxes).");
		return false;
	}

	ShowStatus (jid, "Connecting to " + host);
	std::string reply;
	if (!Net->Connect (host, port) ||
		(security == "tls" && !Net->StartTLS (cafile)))
	{
		Report ("Cannot reach the POP3 server: " + Net->Error() + ".");
		return false;
	}
	if (!Command ("", reply))
	{
		Report ("The POP3 server " + host + " did not greet: " + reply + ".");
		Net->Close ();
		return false;
	}
	if (security == "starttls")
	{
		if (!Command ("STLS", reply))
		{
			Report ("The POP3 server " + host + " does not offer STARTTLS (STLS): " + reply +
					". Choose TLS (port 995) or ask the provider; mail is not fetched without encryption.");
			Net->Close ();
			return false;
		}
		if (!Net->StartTLS (cafile))
		{
			Report ("Cannot start TLS with " + host + ": " + Net->Error() + ".");
			return false;
		}
	}

	// Log in.
	std::string password, err;
	const char *passcmd = Parameters->Get_char ("passcmd");
	if (passcmd && *passcmd)
	{
		ShowStatus (jid, "Getting the password");
		if (!run_password_command (passcmd, password, err))
		{
			Report (std::string ("Cannot get the password for ") + Parent->Name + ": " + err);
			Net->Close ();
			return false;
		}
	}
	else password = Parameters->Get_char ("pass");
	if (password.empty())
	{
		Report (std::string ("No password is set for ") + Parent->Name +
				": enter one, or a password command, in Config \xe2\x86\x92 Mailboxes.");
		Net->Close ();
		return false;
	}

	ShowStatus (jid, "Logging in to " + host);
	if (!Command (std::string ("USER ") + Parameters->Get_char ("user"), reply) ||
		!Command ("PASS " + password, reply))
	{
		std::fill (password.begin(), password.end(), '\0');
		Report (Net->IsOpen() ? "The POP3 server " + host + " refused the login: " + reply + "."
							  : "The connection to " + host + " was lost while logging in: " + reply + ".");
		Net->Close ();
		return false;
	}
	std::fill (password.begin(), password.end(), '\0');

	// The messages in this session, by UIDL.
	std::string list;
	if (!Command ("UIDL", reply) || !ReadMulti (list, 0, 0))
	{
		Report (Net->IsOpen() ? "The POP3 server " + host + " does not support UIDL, which is needed: " + reply + "."
							  : "The connection to " + host + " was lost: " + Net->Error() + ".");
		Net->Close ();
		return false;
	}
	size_t pos = 0;
	while (pos < list.size())
	{
		size_t nl = list.find ('\n', pos);
		std::string line = list.substr (pos, nl - pos);
		pos = nl == std::string::npos ? list.size() : nl + 1;
		int n = 0;
		char uid[256];
		if (sscanf (line.c_str(), "%d %255s", &n, uid) == 2 && n > 0)
		{
			Numbers[uid] = n;
			Order.push_back (uid);
		}
	}
	ConnectionOpen = 1;
	return true;
}

// Ends the session; QUIT makes the server carry out the deletions.
bool POP3Handler::EndSession (void)
{
	bool ok = true;
	if (Net->IsOpen())
	{
		std::string reply;
		ok = Command ("QUIT", reply);
		if (!ok && Net->IsOpen()) Report ("The POP3 server did not accept QUIT (deletions were not done): " + reply + ".");
		Net->Close ();
	}
	ConnectionOpen = 0;
	Numbers.clear ();
	Order.clear ();
	return ok;
}

void POP3Handler::OpenConnection (void)
{
	// Sessions are opened by each operation.
}

void POP3Handler::CloseConnection (void)
{
	EndSession ();
}

// The UIDL a message was filed under.
std::string POP3Handler::UidOf (const char *msgid)
{
	for (auto &kv : Uidl)
		if (kv.second == msgid) return kv.first;
	return "";
}

// The Message-ID of a fetched message, or a stable one made from its UIDL.
static std::string message_id (const std::string &text, const std::string &uid, const std::string &host)
{
	std::string id = mailheaders::Get (mailheaders::Parse (text), "Message-ID");
	size_t a = id.find ('<'), b = id.find ('>', a == std::string::npos ? 0 : a);
	if (a != std::string::npos && b != std::string::npos) id = id.substr (a, b - a + 1);
	else id.clear ();
	// Characters the index and settings files cannot hold.
	for (char &c : id) if (c == '"' || c == '\\' || (unsigned char) c < ' ') c = '_';
	if (id.size() > 2 && id.size() < 250) return id;

	std::string safe;
	for (char c : uid)
		safe += (isalnum ((unsigned char) c) || c == '.' || c == '-') ? std::string (1, c) : "_";
	return "<pop3." + safe + "@" + host + ">";
}

//-------------------------------------------------------------------------------
// Checking
//-------------------------------------------------------------------------------

void POP3Handler::CheckGroup (const char * /*groupname*/)
{
	int collection = Parameters->Get_int ("collection");
	bool fetchbodies = collection > 0;
	bool deletemail = collection > 1 && strcasecmp (Parameters->Get_char ("writeprotect"), "Y") != 0;
	std::string host = Parameters->Get_char ("host");

	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Progress (jid, 1);
	if (!Session (jid))
	{
		JOBQ.EndJob (jid);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		return;
	}

	std::vector<std::string> fresh;
	for (const std::string &uid : Order)
		if (!Uidl.count (uid)) fresh.push_back (uid);
	ShowStatus (jid, std::to_string (Order.size()) + " messages on the server, " + std::to_string (fresh.size()) + " new");

	bool complete = true;
	size_t done = 0;
	for (const std::string &uid : fresh)
	{
		if (Parent->ToCollector->UrgentPending) { complete = false; break; }

		// A message the user is waiting for goes first.
		ITCMessage *M;
		while (Parent->ToCollector->PriorityPending && (M = Parent->ToCollector->GetNextMessage (CLASS_GETBODY)))
		{
			FetchBody (M->Message->Get(0), jid);
			Parent->ToUI->PostMessage (CLASS_GOTBODY, NO_SUBCLASS, M->Message->Get(0));
			delete M;
		}
		if (!Net->IsOpen()) { complete = false; break; }

		int n = Numbers[uid];
		JOBQ.Progress (jid, (int) (5 + 90 * done / fresh.size()));
		ShowStatus (jid, "Fetching message " + std::to_string (done + 1) + " of " + std::to_string (fresh.size()));
		std::string reply, text;
		if (!Command ((fetchbodies ? "RETR " : "TOP ") + std::to_string (n) + (fetchbodies ? "" : " 0"), reply))
		{
			if (!Net->IsOpen())
			{
				Report ("The connection to " + host + " was lost: " + reply + ".");
				complete = false;
				break;
			}
			eprintf ("[pop3] message %d: %s\n", n, reply.c_str());
			continue;			// this one is skipped and tried again next time
		}
		long size = atol (reply.c_str() + 3);
		if (!ReadMulti (text, jid, size))
		{
			Report ("The connection to " + host + " was lost: " + Net->Error() + ".");
			complete = false;
			break;
		}

		std::string msgid = message_id (text, uid, host);
		Uidl[uid] = msgid;
		if (!Parent->Have (&msgid[0]))
		{
			MailMessage *X = new MailMessage (Parent, &msgid[0]);
			if (fetchbodies) X->ProcessMessage (&text[0]);
			else X->ProcessHeaders (&text[0]);
			// Kept locally even when it is gone from the server.
			if (deletemail) X->Flags |= MMFL_ORIGINAL;
			Parent->Deliver (X);
			Parent->Lock->LockRW();
			for (int j = 0; j < X->Subscribers->Count(); ++j)
			{
				ViewListNode *VLN = X->Subscribers->Get (j);
				if (VLN) Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, VLN->View->Name, X->MsgID);
			}
			Parent->Lock->UnlockRW();
		}
		// Save as we go, so that a crash does not fetch everything again.
		if (++done % 25 == 0)
		{
			Parent->SaveMailBox ();
			SaveUidl ();
		}
	}

	// Forget UIDLs that are no longer on the server (only after a complete
	// list, so that an interrupted check cannot cause fetching twice).
	if (complete)
	{
		for (auto it = Uidl.begin(); it != Uidl.end(); )
			it = Numbers.count (it->first) ? std::next (it) : Uidl.erase (it);
	}
	Parent->SaveMailBox ();
	SaveUidl ();

	// Only now, with everything saved, delete from the server.
	if (deletemail && Net->IsOpen() && !Parent->ToCollector->UrgentPending)
	{
		int count = 0;
		for (const std::string &uid : Order)
		{
			if (!Uidl.count (uid)) continue;		// not fetched (yet)
			std::string reply;
			if (!Command ("DELE " + std::to_string (Numbers[uid]), reply))
			{
				if (!Net->IsOpen()) break;
				eprintf ("[pop3] DELE %d: %s\n", Numbers[uid], reply.c_str());
			}
			else ++count;
			if (count % 20 == 0) ShowStatus (jid, "Deleting fetched messages from the server (" + std::to_string (count) + ")");
		}
	}

	bool ok = Net->IsOpen() && EndSession ();
	if (!ok) EndSession ();
	if (ok && complete) LastError.clear ();
	// Redraw the mailbox tree, for the new message counts.
	if (done > 0) Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name);
	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}

//-------------------------------------------------------------------------------
// Bodies, deleting, expiring
//-------------------------------------------------------------------------------

// Fetches the body of a message that was collected as headers, within the
// open session.
bool POP3Handler::FetchBody (const char *msgid, int jid)
{
	MailMessage *X = Parent->Find ((char *) msgid);
	if (!X) return true;
	// A copy to another mailbox asks for the original text (CaptureRaw),
	// also of a message whose text was fetched before.
	bool have = X->Flags & MMFL_HAVEBODY;
	if (have && !X->CaptureRaw) return true;
	std::string uid = UidOf (msgid);
	if (uid.empty() || !Numbers.count (uid))
	{
		if (!have) Report ("This message is no longer on the POP3 server, so its text cannot be fetched.");
		return have;
	}
	int n = Numbers[uid];
	ShowStatus (jid, "Fetching the message text");
	std::string reply, text;
	if (!Command ("RETR " + std::to_string (n), reply))
	{
		Report ("The POP3 server did not send the message: " + reply + ".");
		return false;
	}
	if (!ReadMulti (text, jid, atol (reply.c_str() + 3)))
	{
		Report ("The connection was lost while fetching the message: " + Net->Error() + ".");
		return false;
	}
	if (X->CaptureRaw) X->CaptureRaw->assign (text);
	if (have) return true;
	size_t body = text.find ("\n\n");
	body = body == std::string::npos ? text.size() : body + 2;
	std::string b = text.substr (body);
	X->ProcessBody (&b[0]);
	X->StoreFile ();
	return true;
}

void POP3Handler::GetBody (char *msgid)
{
	MailMessage *X = Parent->Find (msgid);
	if (!X) return;
	bool have = X->Flags & MMFL_HAVEBODY;
	if (have && !X->CaptureRaw) return;
	// Only the original for a copy: there is a local one to fall back on,
	// so no error dialogs, and none to ask for when it was deleted.
	if (have && (Parameters->Get_int ("collection") > 1 || UidOf (msgid).empty())) return;
	Quiet = have;
	int jid = JOBQ.NewJob (Parent->Name);
	if (Session (jid))
	{
		if (FetchBody (msgid, jid) && EndSession ()) { if (!have) LastError.clear (); }
		else EndSession ();
	}
	JOBQ.EndJob (jid);
	Quiet = false;
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

// Deletes the message on the server, together with any other deletions
// waiting in the queue (one session for all of them).
void POP3Handler::DeleteMessage (char *msgid)
{
	std::vector<std::string> ids = { msgid };
	ITCMessage *M;
	while ((M = Parent->ToCollector->GetNextMessage (CLASS_DELETE)))
	{
		ids.push_back (M->Message->Get(0));
		delete M;
	}
	if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y") == 0) return;
	// In the delete-after-fetching mode they are no longer on the server.
	if (Parameters->Get_int ("collection") > 1) return;

	std::vector<std::string> uids;
	for (const std::string &id : ids)
	{
		std::string uid = UidOf (id.c_str());
		if (!uid.empty()) uids.push_back (uid);
	}
	if (uids.empty()) return;

	int jid = JOBQ.NewJob (Parent->Name);
	if (Session (jid))
	{
		ShowStatus (jid, "Deleting " + std::to_string (uids.size()) + " message(s) on the server");
		for (const std::string &uid : uids)
		{
			if (!Numbers.count (uid)) continue;		// already gone
			std::string reply;
			if (!Command ("DELE " + std::to_string (Numbers[uid]), reply))
			{
				if (!Net->IsOpen()) { Report ("The connection was lost while deleting: " + reply + "."); break; }
				eprintf ("[pop3] DELE: %s\n", reply.c_str());
			}
		}
		if (Net->IsOpen() && EndSession ()) LastError.clear ();
		else EndSession ();
	}
	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

// Deletes messages left on the server: those fetched completely longer
// ago than "expire_onserver_received" days, and those only fetched as
// headers longer ago than "expire_onserver_notreceived" days (these are
// also removed here, as their text can no longer be fetched).
void POP3Handler::Expire (void)
{
	if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y") == 0) return;
	time_t received = 86400L * Parameters->Get_int ("expire_onserver_received");
	time_t notreceived = 86400L * Parameters->Get_int ("expire_onserver_notreceived");
	if (received <= 0 && notreceived <= 0) return;

	int jid = JOBQ.NewJob (Parent->Name);
	if (!Session (jid)) { JOBQ.EndJob (jid); return; }
	ShowStatus (jid, "Expiring messages on the server");
	time_t now = time (NULL);
	int count = 0;
	std::vector<std::string> remove;
	for (const std::string &uid : Order)
	{
		if (!Net->IsOpen() || Parent->ToCollector->UrgentPending) break;
		auto known = Uidl.find (uid);
		if (known == Uidl.end()) continue;
		const char *msgid = known->second.c_str();
		MailMessage *X = Parent->Find ((char *) msgid);
		if (!X) continue;
		bool have = X->Flags & MMFL_HAVEBODY;
		time_t age = now - X->ReceiveDate;
		if ((have && received > 0 && age > received) || (!have && notreceived > 0 && age > notreceived))
		{
			std::string reply;
			if (Command ("DELE " + std::to_string (Numbers[uid]), reply))
			{
				++count;
				if (!have) remove.push_back (msgid);
			}
		}
	}
	if (Net->IsOpen() && EndSession ())
	{
		// The window removes them (and updates the list).
		for (std::string &id : remove)
			Parent->ToUI->PostMessage (CLASS_GONE, NO_SUBCLASS, Parent->Name, id.c_str());
		if (!remove.empty()) Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name);
		LastError.clear ();
		ShowStatus (jid, "Expired " + std::to_string (count) + " message(s) on the server");
	}
	else EndSession ();
	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

