//===============================================================================
// POSTOFFICE - X11 Office Communication Handler
// ---------------------------------------------
// All sourcecode, unless differently annotated, written by Pim van Riezen
// Sourcecode and resulting binaries are covered by the GNU Lesser General Public
// License (GNU LGPL). For more information and the literal text of the license
// agreement, check the Free Software Foundation's website on http://www.fsf.org/
// 
// Copyright (C) 1999 Mad Sciene Labs - Rotterdam, The Netherlands
//===============================================================================

#include "mailbox.h"
#include "var.h"
#include "reg.h"
#include "chop.h"
#include "eaddr.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "strdupp.h"
#include "smtpclient.h"
#include "sendpipe.h"
#include "style.h"
#include "debug.h"
#include "mailheaders.h"

#include <algorithm>
#include <set>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <map>
#include <string>
#include "status.h"

extern JobList JOBQ;

extern void extract_recipients (WordList *, char *);
bool is_redirect (MailMessage *M);
extern MailBox *MailBoxList[];

// The identity whose sender address is mailfrom (Config -> Identities,
// "Email From"), or NULL.
static Var *identity_for (const char *mailfrom, std::string &name)
{
	Var *ids = PREFERENCES.Get_collection ("postoffice")->Get_collection ("identities");
	WordList *W = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("identities");
	for (int i = 0; i < W->Count; ++i)
	{
		Var *I = ids->Get_collection (W->Get (i));
		char *addr = eaddr2addr (I->Get_char ("mailfrom"));
		bool match = addr && strcasecmp (addr, mailfrom) == 0;
		delete[] addr;
		if (match)
		{
			name = I->Get_char ("shortname");
			if (name.empty ()) name = W->Get (i);
			return I;
		}
	}
	return NULL;
}

// A copy of a sent message for the Sent folder of the IMAP mailbox the
// identity names ("Save sent mail to"). The mailbox keeps it in its spool
// and stores it on the server at its next check, which is started now.
// Bcc stays in the copy, as a record of who got it.
// mime_type: the Content-Type of a multipart body (a message with
// attachments), which then replaces the message's own content headers.
static void save_sent_copy (MailBox *Parent, MailMessage *M, const char *mailfrom, const char *body,
							const std::string &mime_type)
{
	std::string idname;
	Var *I = identity_for (mailfrom, idname);
	if (!I || !body) return;
	const char *target = I->Get_char ("sent_imap");
	if (!target || !*target) return;
	MailBox *T = NULL;
	for (int k = 0; k < 16 && MailBoxList[k]; ++k)
		if (strcmp (MailBoxList[k]->Name, target) == 0) T = MailBoxList[k];
	if (!T || !T->Handler)
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Outbox",
			("A copy of the message sent could not be saved: the mailbox \"" + std::string (target) +
			 "\" (Config -> Identities -> Save sent mail to) does not exist.").c_str ());
		return;
	}
	std::string raw;
	bool have_id = false;
	for (int i = 0; i < M->HeaderNames->Count; ++i)
	{
		const char *n = M->HeaderNames->Get (i);
		if (strcasecmp (n, "Message-ID") == 0) have_id = true;
		if (!mime_type.empty () && (strcasecmp (n, "Content-Type") == 0 || strcasecmp (n, "MIME-Version") == 0 ||
									strcasecmp (n, "Content-Transfer-Encoding") == 0))
			continue;
		raw += std::string (n) + ": " + M->HeaderValues->Get (i) + "\n";
	}
	if (!have_id) raw += std::string ("Message-ID: ") + M->MsgID + "\n";
	if (!mime_type.empty ()) raw += "MIME-Version: 1.0\nContent-Type: " + mime_type + "\n";
	raw += "\n";
	raw.append (body, strlen (body));
	std::string err;
	if (!T->Handler->DeliverRaw (raw, "\\Sent", "S", err))
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Outbox",
			("A copy of the message sent could not be saved for " + std::string (target) + ": " + err).c_str ());
	else
		T->ToCollector->PostMessage (CLASS_CHECKGROUP, SUB_CHECK_NOFETCH, "*");
}

// How a try to send one message went.
enum SendOutcome { SEND_OK, SEND_LATER, SEND_SETUP, SEND_MESSAGE, SEND_STOPPED };

// Sends one message the way Config -> Network says: the built-in SMTP
// client (also when nothing is set) to the sender identity's mail server,
// the system MTA, or msmtp. err says why it was not sent.
static SendOutcome deliver_message (MailBox *Parent, WordList *HeaderNames, WordList *HeaderValues,
									char *mailfrom, WordList *RCPT, char *body, std::string &err)
{
	const char *method = PREFERENCES.Get_char_tree ("postoffice/network/delivery");
	const char *program = NULL;
	if (strcasecmp (method, "sendmail") == 0)
	{
		program = PREFERENCES.Get_char_tree ("postoffice/network/sendmail_path");
		if (!*program) program = "/usr/sbin/sendmail";
	}
	else if (strcasecmp (method, "msmtp") == 0)
	{
		program = PREFERENCES.Get_char_tree ("postoffice/network/msmtp_path");
		if (!*program) program = "msmtp";
	}

	// The system MTA and msmtp: a failure is taken as passing (a local MTA
	// rarely refuses for good, and their exit codes are not reliable).
	if (program)
		return send_message_pipe (program, HeaderNames, HeaderValues, mailfrom, RCPT, body, err) ? SEND_OK : SEND_LATER;

	std::string idname;
	Var *I = identity_for (mailfrom, idname);
	if (!I)
	{
		err = std::string ("no identity has the sender address ") + mailfrom + ", so there is no mail server"
			  " to send it to. Set one up in Config \xe2\x86\x92 Identities.";
		return SEND_SETUP;
	}
	if (!*I->Get_char ("smtp_host"))
	{
		err = "the identity " + idname + " has no outgoing mail server. Set one in Config \xe2\x86\x92"
			  " Identities \xe2\x86\x92 Outgoing mail server.";
		return SEND_SETUP;
	}

	SmtpServer srv;
	srv.Host = I->Get_char ("smtp_host");
	srv.Port = atoi (I->Get_char ("smtp_port"));
	srv.Security = I->Get_char ("smtp_security");
	srv.User = I->Get_char ("smtp_user");
	// The password: the identity's password command (its first line), or
	// the stored password.
	const char *passcmd = I->Get_char ("smtp_passcmd");
	if (!srv.User.empty () && passcmd && *passcmd)
	{
		std::string perr;
		if (!run_password_command (passcmd, srv.Password, perr))
		{
			err = "cannot get the password for " + idname + ": " + perr;
			return SEND_SETUP;
		}
	}
	else srv.Password = I->Get_char ("smtp_pass");
	if (!srv.User.empty () && srv.Password.empty ())
	{
		err = "the identity " + idname + " has a user name for its mail server but no password. Enter one,"
			  " or a password command, in Config \xe2\x86\x92 Identities.";
		return SEND_SETUP;
	}

	std::vector<std::string> rcpts;
	for (int i = 0; i < RCPT->Count; ++i) rcpts.push_back (RCPT->Get (i));
	int jid = JOBQ.NewJob ("OutBox");
	SmtpResult res = smtp_send (srv, mailfrom, rcpts, build_message (HeaderNames, HeaderValues, body),
		[&](const std::string &st)
		{
			JOBQ.Stat (jid, (char *) st.c_str ());
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		},
		[Parent]() { return Parent->ToCollector->UrgentPending != 0; });
	JOBQ.EndJob (jid);
	std::fill (srv.Password.begin (), srv.Password.end (), '\0');
	err = res.Error;
	if (res.Sent) return SEND_OK;
	if (res.Cancelled) return SEND_STOPPED;
	if (res.Temporary) return SEND_LATER;
	if (res.Setup) return SEND_SETUP;
	return SEND_MESSAGE;
}

//-------------------------------------------------------------------------------
// When to try a message again (Neo-PostOffice). Kept in retry.dat in the
// outbox's directory, one line per message: Message ID, time of the first
// failure, tries so far, time of the next try, and "held" (1: wait for a
// check the user starts). A message is retried after 1, 5, 15 and 30
// minutes and then every hour, and moved to Drafts after 3 days.
//-------------------------------------------------------------------------------

namespace
{
	struct RetryState
	{
		time_t		 First = 0;
		int			 Tries = 0;
		time_t		 Next = 0;
		bool		 Held = false;
	};

	const time_t GIVE_UP = 3 * 24 * 3600;

	time_t retry_delay (int tries)
	{
		static const time_t steps[] = { 60, 5 * 60, 15 * 60, 30 * 60 };
		return tries >= 1 && tries <= 4 ? steps[tries - 1] : 3600;
	}

	std::string when_text (time_t delay)
	{
		if (delay < 3600) return std::to_string (delay / 60) + (delay < 120 ? " minute" : " minutes");
		return std::to_string (delay / 3600) + (delay < 7200 ? " hour" : " hours");
	}

	std::string retry_file (MailBox *B)
	{
		return std::string (B->DirectoryPrefix) + "/retry.dat";
	}

	std::map<std::string, RetryState> load_retries (MailBox *B)
	{
		std::map<std::string, RetryState> m;
		FILE *f = fopen (retry_file (B).c_str (), "r");
		if (!f) return m;
		char line[1024];
		while (fgets (line, sizeof (line), f))
		{
			char id[768];
			long first, next;
			int tries, held;
			if (sscanf (line, "%767s %ld %d %ld %d", id, &first, &tries, &next, &held) == 5)
			{
				RetryState &r = m[id];
				r.First = first; r.Tries = tries; r.Next = next; r.Held = held != 0;
			}
		}
		fclose (f);
		return m;
	}

	void save_retries (MailBox *B, const std::map<std::string, RetryState> &m)
	{
		std::string path = retry_file (B), tmp = path + ".tmp";
		if (m.empty ()) { unlink (path.c_str ()); return; }
		int fd = open (tmp.c_str (), O_WRONLY | O_CREAT | O_TRUNC, 0600);
		FILE *f = fd >= 0 ? fdopen (fd, "w") : NULL;
		if (!f) { if (fd >= 0) close (fd); return; }
		for (const auto &e : m)
			fprintf (f, "%s %ld %d %ld %d\n", e.first.c_str (), (long) e.second.First, e.second.Tries,
					 (long) e.second.Next, e.second.Held ? 1 : 0);
		if (fclose (f) == 0) rename (tmp.c_str (), path.c_str ());
		else unlink (tmp.c_str ());
	}

	// The reason for a message is shown once per message and reason, not at
	// every round.
	std::set<std::string> reported;

	void report (MailBox *Parent, const char *msgid, const std::string &key, const std::string &text)
	{
		if (!reported.insert (std::string (msgid) + "\n" + key).second) return;
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Outbox", text.c_str ());
	}

	void forget_reports (const char *msgid)
	{
		std::string prefix = std::string (msgid) + "\n";
		for (auto it = reported.begin (); it != reported.end ();)
			it = it->compare (0, prefix.size (), prefix) == 0 ? reported.erase (it) : std::next (it);
	}

	MailView *view_named (MailBox *B, const char *name)
	{
		for (MailView *V = B->VFirst; V; V = V->Next)
			if (strcmp (V->Name, name) == 0) return V;
		return NULL;
	}

	// Moves M from the queue Q to another of the outbox's folders.
	void move_message (MailBox *Parent, MailView *Q, MailView *To, MailMessage *M)
	{
		Parent->UnTie (M);
		Parent->ToUI->PostMessage (CLASS_REMARTICLE, NO_SUBCLASS, Q->Name, M->MsgID);
		if (To)
		{
			To->Associate (M);
			Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, To->Name, M->MsgID);
		}
	}
}

OutBoxHandler::OutBoxHandler (MailBox *_Parent, class Var *_Parameters)
{
	Parameters = new Var (_Parameters);
	Parent = _Parent;
}

OutBoxHandler::~OutBoxHandler (void)
{
	delete (Parameters);
}

void OutBoxHandler::OpenConnection (void)
{
}

void OutBoxHandler::CloseConnection (void)
{
}

void OutBoxHandler::GetBody (char */*msgid*/)
{
}

void OutBoxHandler::CheckGroup (const char */*groupname*/)
{
	int			 i;
	MailMessage *M;
	MailPart	*P;
	char		*mailfrom;
	WordList	*RCPT, *HeaderNames, *HeaderValues;
	int			 pos;
	MailView 	*Q = Parent->VFirst;
	bool		 sent = false;
	bool		 moved = false;
	bool		 stopped = false;
	MailView	*drafts = view_named (Parent, "Drafts");
	std::map<std::string, RetryState> retries = load_retries (Parent);
	
	if (Q)
	{
		int jid = JOBQ.NewJob ("OutBox");
		JOBQ.Stat (jid, "Emptying Queue");
        Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		for (i=0; i< Q->Count(); ++i)
		{
			JOBQ.Progress (jid, (100*i)/Q->Count());
            Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

			M = Q->GetMessage (i);
			Q->DoneWithMessage();
			dprintf ("message %s\n", M->MsgID);
			RCPT = new WordList;

			if (!M->HeaderNames->Count) M->LoadHeaders();
			if (!(M->Flags & MMFL_INRAM)) M->LoadBody();
			
			HeaderNames = new WordList (M->HeaderNames);
			HeaderValues = new WordList (M->HeaderValues);

			// A redirected message (Redirect) goes to its Resent- recipients
			// from the Resent-From address; its own To, CC and From are the
			// original's and stay as they are (Neo-PostOffice).
			bool redirect = is_redirect (M);
			const char *h_to = redirect ? "Resent-To" : "To";
			const char *h_from = redirect ? "Resent-From" : "From";
			const char *h_cc = redirect ? "Resent-Cc" : "Cc";

			pos = HeaderNames->Find (h_to);
			if (pos>=0) extract_recipients (RCPT, HeaderValues->Get (pos));

			pos = HeaderNames->Find (h_from);
			if (pos>=0) mailfrom = eaddr2addr (HeaderValues->Get (pos));
			else mailfrom = strdupp ("nobody@nowhere.com");

			pos = HeaderNames->Find (h_cc);
			if (pos>=0) extract_recipients (RCPT, HeaderValues->Get (pos));

			if (!redirect)
			{
				pos = HeaderNames->Find ("BCC");
				if (pos>=0)
				{
					extract_recipients (RCPT, HeaderValues->Get (pos));
					HeaderValues->Delete (pos);
					HeaderNames->Delete (pos);
				}
			}

			if (HeaderNames->Find ("Message-ID") < 0)
			{
				HeaderNames->Add ("Message-ID");
				HeaderValues->Add (M->MsgID);
			}

			P = M->GetPart (0);

			// A message with attachments: its parts as a multipart/mixed
			// body, whose Content-Type (with the boundary) replaces the
			// message's own content headers.
			std::string mime_body, mime_type;
			char *send_body = P ? P->Contents : NULL;
			if (P && M->Count > 1)
			{
				mime_body = mime_multipart_body (M, mime_type);
				for (const char *h : { "Content-Type", "Content-Transfer-Encoding", "MIME-Version" })
					for (int k; (k = HeaderNames->Find (h)) >= 0; )
					{
						HeaderNames->Delete (k);
						HeaderValues->Delete (k);
					}
				HeaderNames->Add ("MIME-Version");
				HeaderValues->Add ("1.0");
				HeaderNames->Add ("Content-Type");
				HeaderValues->Add ((char *) mime_type.c_str ());
				send_body = &mime_body[0];
			}

			if (P && RCPT->Count)
			{
				time_t now = time (NULL);
				auto rit = retries.find (M->MsgID);
				bool waiting = rit != retries.end () && (rit->second.Held || now < rit->second.Next);
				if (stopped || (waiting && !UserCheck))
				{
					Q->DoneWithMessage();		// not now
				}
				else
				{
					JOBQ.Stat (jid, "Sending message from queue...");
					Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
					std::string err;
					SendOutcome how = deliver_message (Parent, HeaderNames, HeaderValues, mailfrom, RCPT, send_body, err);
					std::string subject = M->Header ("Subject") ? mailheaders::Decode (M->Header ("Subject")) : "";
					std::string what = "\"" + subject + "\"";
					Q->DoneWithMessage();
					switch (how)
					{
						case SEND_OK:
							save_sent_copy (Parent, M, mailfrom, send_body, mime_type);
							sent = true;
							retries.erase (M->MsgID);
							forget_reports (M->MsgID);
							move_message (Parent, Q, Q->Next, M);
							--i;
							break;

						case SEND_STOPPED:
							// Stop: nothing more this round, and this message
							// waits for a check the user starts.
							stopped = true;
							retries[M->MsgID].Held = true;
							break;

						case SEND_LATER:
						{
							RetryState &st = retries[M->MsgID];
							if (!st.First) st.First = now;
							++st.Tries;
							st.Held = false;
							if (now - st.First >= GIVE_UP)
							{
								retries.erase (M->MsgID);
								forget_reports (M->MsgID);
								moved = true;
								move_message (Parent, Q, drafts, M);
								--i;
								Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Outbox",
									("The message " + what + " could not be sent for three days and was moved to"
									 " OutBox \xe2\x86\x92 Drafts. The last try failed: " + err).c_str ());
								break;
							}
							time_t delay = retry_delay (st.Tries);
							st.Next = now + delay;
							report (Parent, M->MsgID, err, "The message " + what + " stays in the outbox and is"
									" tried again in " + when_text (delay) + " (or at Check Mail): " + err);
							break;
						}

						case SEND_SETUP:
							// Trying again will not help until the settings or
							// the server change: wait for a check the user starts.
							retries[M->MsgID].Held = true;
							report (Parent, M->MsgID, err, "The message " + what + " stays in the outbox until"
									" this is fixed; then use File \xe2\x86\x92 Check Mail: " + err);
							break;

						case SEND_MESSAGE:
							retries.erase (M->MsgID);
							forget_reports (M->MsgID);
							moved = true;
							move_message (Parent, Q, drafts, M);
							--i;
							if (redirect)
								// The server may refuse a From line that is not the
								// user's own (the original sender's, kept by Redirect).
								Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Outbox",
									("The redirected message " + what + " was not sent and was moved to OutBox "
									 "\xe2\x86\x92 Drafts: " + err + "\n\nRedirect keeps the original sender in "
									 "the From line; some mail servers only send messages from your own address. "
									 "Through such a server, use Forward instead.").c_str ());
							else
							Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Outbox",
								("The message " + what + " was not sent and was moved to OutBox \xe2\x86\x92"
								 " Drafts, where Message \xe2\x86\x92 Edit Draft opens it to correct and send"
								 " again: " + err).c_str ());
							break;
					}
				}
				JOBQ.Stat (jid, "Emptying Queue");
				Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
			}
			else if (P)
			{
				// No recipient to send to (a Usenet article from PostOffice
				// 0.7, whose news support is gone): to the Drafts.
				std::string subject = M->Header ("Subject") ? mailheaders::Decode (M->Header ("Subject")) : "";
				Q->DoneWithMessage();
				retries.erase (M->MsgID);
				forget_reports (M->MsgID);
				moved = true;
				move_message (Parent, Q, drafts, M);
				--i;
				Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Outbox",
					("The message \"" + subject + "\" has no recipients (To, CC or BCC) and was moved to"
					 " OutBox \xe2\x86\x92 Drafts.").c_str ());
			}
			delete (RCPT);
			delete (HeaderNames);
			delete (HeaderValues);
			delete[] mailfrom;
		}
		JOBQ.EndJob (jid);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
	}
	// Retry times of messages no longer in the queue are dropped.
	if (Q)
		for (auto it = retries.begin (); it != retries.end ();)
		{
			bool queued = false;
			for (int k = 0; k < Q->Count () && !queued; ++k)
			{
				MailMessage *X = Q->GetMessage (k);
				Q->DoneWithMessage ();
				if (X && it->first == X->MsgID) queued = true;
			}
			it = queued ? std::next (it) : retries.erase (it);
		}
	save_retries (Parent, retries);
	// Record at once that these are sent (the regular save is every two
	// minutes): after a crash they would otherwise be sent again.
	// A message queued since the last save is recorded too: the index is
	// otherwise saved only every two minutes, and a crash lost the message.
	if (sent || moved || (Q && Q->Count () > 0)) Parent->SaveMailBox ();
}

void OutBoxHandler::DeleteMessage (char */*msgid*/)
{
}


void OutBoxHandler::Expire (void)
{
}

