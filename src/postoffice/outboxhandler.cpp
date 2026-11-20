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

#include <algorithm>
#include <set>
#include <string>
#include "status.h"

extern JobList JOBQ;

extern void extract_recipients (WordList *, char *);
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

// Sends one message the way Config -> Network says: the built-in SMTP
// client (also when nothing is set) to the sender identity's mail server,
// the system MTA, or msmtp. Returns true when it was handed over.
static bool deliver_message (MailBox *Parent, const char *msgid, WordList *HeaderNames, WordList *HeaderValues,
							 char *mailfrom, WordList *RCPT, char *body)
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

	std::string err;
	bool ok = false;
	if (program) ok = send_message_pipe (program, HeaderNames, HeaderValues, mailfrom, RCPT, body, err);
	else
	{
		std::string idname;
		Var *I = identity_for (mailfrom, idname);
		if (!I)
			err = std::string ("no identity has the sender address ") + mailfrom + ", so there is no mail server"
				  " to send it to. Set one up in Config \xe2\x86\x92 Identities.";
		else if (!*I->Get_char ("smtp_host"))
			err = "the identity " + idname + " has no outgoing mail server. Set one in Config \xe2\x86\x92"
				  " Identities \xe2\x86\x92 Outgoing mail server.";
		else
		{
			SmtpServer srv;
			srv.Host = I->Get_char ("smtp_host");
			srv.Port = atoi (I->Get_char ("smtp_port"));
			srv.Security = I->Get_char ("smtp_security");
			srv.User = I->Get_char ("smtp_user");
			// The password: the identity's password command (its first line),
			// or the stored password.
			const char *passcmd = I->Get_char ("smtp_passcmd");
			std::string perr;
			bool have_password = true;
			if (!srv.User.empty () && passcmd && *passcmd)
			{
				if (!run_password_command (passcmd, srv.Password, perr))
				{
					err = "cannot get the password for " + idname + ": " + perr;
					have_password = false;
				}
			}
			else srv.Password = I->Get_char ("smtp_pass");
			if (have_password && !srv.User.empty () && srv.Password.empty ())
			{
				err = "the identity " + idname + " has a user name for its mail server but no password. Enter one,"
					  " or a password command, in Config \xe2\x86\x92 Identities.";
				have_password = false;
			}
			if (have_password)
			{
			std::vector<std::string> rcpts;
			for (int i = 0; i < RCPT->Count; ++i) rcpts.push_back (RCPT->Get (i));
			int jid = JOBQ.NewJob ("OutBox");
			SmtpResult res = smtp_send (srv, mailfrom, rcpts, build_message (HeaderNames, HeaderValues, body),
				[&](const std::string &s)
				{
					JOBQ.Stat (jid, (char *) s.c_str ());
					Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
				},
				[Parent]() { return Parent->ToCollector->UrgentPending != 0; });
			JOBQ.EndJob (jid);
			ok = res.Sent;
			err = res.Error;
			}
			std::fill (srv.Password.begin (), srv.Password.end (), '\0');
		}
	}

	// The message stays in the outbox when it fails; the reason is shown
	// once per message and reason, not at every check.
	static std::set<std::string> reported;
	if (ok)
	{
		for (auto it = reported.begin (); it != reported.end ();)
			it = it->compare (0, strlen (msgid) + 1, std::string (msgid) + "\n") == 0 ? reported.erase (it) : std::next (it);
		return true;
	}
	std::string key = std::string (msgid) + "\n" + err;
	if (reported.insert (key).second)
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Outbox",
								   ("The message stays in the outbox: " + err).c_str());
	return false;
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
	int			 i,j;
	MailMessage *M;
	MailPart	*P;
	char		*mailfrom;
	WordList	*RCPT, *HeaderNames, *HeaderValues;
	int			 pos;
	MailView 	*Q = Parent->VFirst;
	bool		 sent = false;
	
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

			pos = HeaderNames->Find ("To");
			if (pos>=0) extract_recipients (RCPT, HeaderValues->Get (pos));

			pos = HeaderNames->Find ("From");
			if (pos>=0) mailfrom = eaddr2addr (HeaderValues->Get (pos));
			else mailfrom = strdupp ("nobody@nowhere.com");

			pos = HeaderNames->Find ("Cc");
			if (pos>=0) extract_recipients (RCPT, HeaderValues->Get (pos));

			pos = HeaderNames->Find ("BCC");
			if (pos>=0)
			{
				extract_recipients (RCPT, HeaderValues->Get (pos));
				HeaderValues->Delete (pos);
				HeaderNames->Delete (pos);
			}

			HeaderNames->Add ("Message-ID");
			HeaderValues->Add (M->MsgID);

			P = M->GetPart (0);
			

			if (P && RCPT->Count)
			{
				dprintf ("count=%i\n", RCPT->Count);
				for (j=0;j<RCPT->Count;++j) dprintf ("    RCPT: %s\n", RCPT->Get(j));
				
				JOBQ.Stat (jid, "Sending message from queue...");
                Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

				//Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Sending message from queue...");
				
				if (deliver_message (Parent, M->MsgID, HeaderNames, HeaderValues, mailfrom, RCPT, P->Contents))
				{
					sent = true;
					Q->DoneWithMessage();
					Parent->UnTie (M);
					Parent->ToUI->PostMessage (CLASS_REMARTICLE, NO_SUBCLASS, Q->Name, M->MsgID);
					--i;
					if (Q->Next)
					{
						Q->Next->Associate (M);
						Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, Q->Next->Name, M->MsgID);
					}
				}
				else Q->DoneWithMessage();
				JOBQ.Stat (jid, "Emptying Queue");
				Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
			}
			else if (P)
			{
				int didit=0;
				RCPT->Clear();
				pos = HeaderNames->Find ("Newsgroups");
				if (pos>=0)
				{
					WordList *TW = new WordList;
					TW->MakeGeneric (HeaderValues->Get (pos), ',');
					for (j=0;j<TW->Count;++j)
					{
						char *tmp = TW->Get(j);
						chopwhitespace (tmp);
						while (*tmp == ' ') ++tmp;
						if (strlen (tmp)>0) RCPT->Add (tmp);
					}
					delete (TW);
					dprintf ("newsfroups: %s\n", HeaderValues->Get (pos));
					extract_recipients (RCPT, HeaderValues->Get (pos));
					dprintf ("considering post, rcptcount=%i\n", RCPT->Count);
					if (RCPT->Count)
					{
						char *xbox;
						dprintf ("post-to: %i\n", HeaderNames->Find ("X-Post-To"));
						pos = HeaderNames->Find ("X-Post-To");
						if (pos>=0)
						{
							xbox = HeaderValues->Get (pos);
							dprintf ("post-to: %s\n", xbox);
							for (j=0;(MailBoxList[j] && (!didit));++j)
							{
								if (strcmp (MailBoxList[j]->Name, xbox) == 0)
								{
									dprintf ("foundbox, posting\n");
									MailBoxList[j]->Handler->Lock.LockOnce();
									if (MailBoxList[j]->Handler->PostMessage (RCPT, M))
									{
										dprintf ("posted\n");
										Q->DoneWithMessage();
										didit = 1;
										sent = true;
										Parent->UnTie (M);
										Parent->ToUI->PostMessage (CLASS_REMARTICLE, NO_SUBCLASS, Q->Name, M->MsgID);
										--i;
										if (Q->Next)
										{
											Q->Next->Associate (M);
											Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, Q->Next->Name, M->MsgID);
										}
									}
									MailBoxList[j]->Handler->Lock.UnlockOnce();
								}
							}
						}
					}
				}
				if (!didit) Q->DoneWithMessage();
			}
			delete (RCPT);
			delete (HeaderNames);
			delete (HeaderValues);
			delete[] mailfrom;
		}
		JOBQ.EndJob (jid);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
	}
	// Record at once that these are sent (the regular save is every two
	// minutes): after a crash they would otherwise be sent again.
	if (sent) Parent->SaveMailBox ();
}

void OutBoxHandler::DeleteMessage (char */*msgid*/)
{
}


void OutBoxHandler::Expire (void)
{
}

int OutBoxHandler::PostMessage (WordList */*rcpt*/, MailMessage */*m*/)
{
	return 0;
}
