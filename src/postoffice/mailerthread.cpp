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

#include "mailerthread.h"
#include "mailbox.h"
#include "usleep.h"
#include "debug.h"
#include "sendpipe.h"
#include "style.h"

#include <string>

#define ST_CHECKMESSAGES 0x01
#define ST_POSTMESSAGES  0x02

extern void displaymessage (MailMessage *);
extern void updatefolder (void);
extern MailBox *currentmailbox;

MailerThread::~MailerThread (void)
{
	if (Started) Kill();
}

// Runs the mailbox's fetch command, if it has one. A failure is shown
// once (until the error changes or a run succeeds); the mailbox is
// checked anyway. Called in the mailbox's own thread only.
void MailBox::RunFetchCommand (bool automatic)
{
	Var *prefs = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Name);
	const char *command = prefs->Get_char ("fetchcmd");
	if (!command || !*command) return;
	// The timer's first check comes right after the one at startup.
	if (automatic && time (NULL) - LastFetch < 10) return;

	std::string status = std::string ("Fetching mail for ") + Name + "...";
	ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, status.c_str());
	std::string err;
	bool ok = run_fetch_command (command, err);
	LastFetch = time (NULL);
	ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	if (!ok && err != LastFetchError)
		ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, Name,
						   (std::string ("Could not fetch the mail for ") + Name + ": " + err).c_str());
	LastFetchError = ok ? "" : err;
}

void MailerThread::Run (void *data)
{
	pthread_t *id = new pthread_t;
	if (!Started)
	{
		if (pthread_create (id, NULL, &MailerMain, data))
		{
			ThreadID = *id;
			Started = 1;
			
			//pthread_detach (ThreadID);
		}
	}
}

void *MailerThread::MailerMain (void *_box)
{
	char currentgroup[256];
	int dead = 0;
	ITCMessage *M = NULL;
	MailBox *Box = (MailBox *) _box;
	currentgroup[0] = '*';
	currentgroup[1] = 0;
	int idlecount = 0;

	while (!dead)
	{
		Box->Collector->Started = 1;
		M = Box->ToCollector->GetNextMessage();
		if (!M) M = Box->Schedule->CheckSchedule();
		if (M)
		{
			idlecount = 0;
			switch (M->Class)
			{
				case CLASS_DIE:
					dead = 1;
					break;
					
				case CLASS_STOP:
					Box->Handler->Lock.LockOnce();
					Box->Handler->CloseConnection();
					Box->Handler->Lock.UnlockOnce();
					break;
				
				case SCHED_CHECKNEW:
					M->Message->Set (0, currentgroup);
					[[fallthrough]];
				
				case CLASS_CHECKGROUP:
					snprintf (currentgroup, sizeof (currentgroup), "%s", M->Message->Get(0));
					// A Maildir, MH or mbox mailbox may have a command that
					// fetches its mail (getmail, fetchmail): run it first.
					if (M->Subclass != SUB_CHECK_NOFETCH) Box->RunFetchCommand (M->Class == SCHED_CHECKNEW);
					//printf ("class_checkgroup handler=%08x m=%08x m->message=%08x\n", Box->Handler, M, M->Message);
					Box->Handler->Lock.LockOnce();
					Box->Handler->CloseConnection();
					Box->Handler->OpenConnection();
					Box->Handler->UserCheck = M->Class == CLASS_CHECKGROUP;
					Box->Handler->CheckGroup (M->Message->Get(0));
					Box->Handler->UserCheck = false;
					Box->Handler->Lock.UnlockOnce();
					Box->ToUI->PostMessage (CLASS_CHECKEDGROUP, NO_SUBCLASS, M->Message->Get(0));
					break;
				
				case CLASS_GETBODY:
					dprintf ("class getbody msgid=\"%s\"\n", M->Message->Get(0));
					Box->Handler->Lock.LockOnce();
					Box->Handler->GetBody (M->Message->Get(0));
					Box->Handler->Lock.UnlockOnce();
					Box->ToUI->PostMessage (CLASS_GOTBODY, NO_SUBCLASS, M->Message->Get(0));
					break;
					
				case CLASS_SETFLAG:
					if (M->Message->Count >= 2 && strlen (M->Message->Get(1)) == 2)
					{
						Box->Handler->Lock.LockOnce();
						Box->Handler->SetFlag (M->Message->Get(0), M->Message->Get(1)[1], M->Message->Get(1)[0] == '+');
						Box->Handler->Lock.UnlockOnce();
					}
					break;

				case CLASS_MOVE:
					if (M->Message->Count >= 2)
					{
						Box->Handler->Lock.LockOnce();
						Box->Handler->MoveMessage (M->Message->Get(0), M->Message->Get(1));
						Box->Handler->Lock.UnlockOnce();
					}
					break;

				case CLASS_TRANSFER:
					if (M->Message->Count >= 5)
					{
						Box->Handler->Lock.LockOnce();
						Box->TransferMessage (M->Message->Get(0), M->Message->Get(1), M->Message->Get(2),
											  strcmp (M->Message->Get(3), "move") == 0,
											  strcmp (M->Message->Get(4), "1") == 0);
						Box->Handler->Lock.UnlockOnce();
					}
					break;

				case CLASS_REFILTER:
					Box->Handler->Lock.LockOnce();
					Box->Refilter ();
					Box->Handler->Lock.UnlockOnce();
					break;

				case CLASS_REDIRECT:
					if (M->Message->Count >= 3)
					{
						Box->Handler->Lock.LockOnce();
						Box->RedirectMessage (M->Message->Get(0), M->Message->Get(1), M->Message->Get(2));
						Box->Handler->Lock.UnlockOnce();
					}
					break;

				case CLASS_FOLDEROP:
					if (M->Message->Count >= 3)
					{
						Box->Handler->Lock.LockOnce();
						Box->Handler->FolderOp (M->Message->Get(0), M->Message->Get(1), M->Message->Get(2));
						Box->Handler->Lock.UnlockOnce();
					}
					break;

				case CLASS_MARKREAD:
					Box->Handler->Lock.LockOnce();
					Box->Handler->MarkRead (M->Message->Get(0));
					Box->Handler->Lock.UnlockOnce();
					break;

				case CLASS_DELETE:
					Box->Handler->Lock.LockOnce();
					Box->Handler->DeleteMessage (M->Message->Get(0));
					Box->Handler->Lock.UnlockOnce();
					//Box->ToUI->PostMessage (CLASS_DELETE, NO_SUBCLASS, M->Message->Get(0));
					break;
					

				
				case CLASS_OPENSERVER:
					Box->Handler->Lock.LockOnce();
					Box->Handler->OpenConnection ();
					Box->Handler->Lock.UnlockOnce();
					break;
				
				case CLASS_CLOSESERVER:
					Box->Handler->Lock.LockOnce();
					Box->Handler->CloseConnection ();
					Box->Handler->Lock.UnlockOnce();
					break;
					
				case CLASS_EXPIRE:
					Box->Handler->Lock.LockOnce();
					Box->Handler->Expire ();
					Box->Handler->Lock.UnlockOnce();
					break;
				
				case SCHED_SAVEBOX:
					Box->SaveMailBox();
					Box->DB->StoreIndex();
					break;

			}
			delete (M);
		}
		else 
		{
			++idlecount;
			// Neo-PostOffice: a handler waiting for its server (IMAP IDLE)
			// looks for news about three times a second.
			if (idlecount % 20 == 0)
			{
				Box->Handler->Lock.LockOnce();
				Box->Handler->Idle ();
				Box->Handler->Lock.UnlockOnce();
			}
			if (idlecount == 20000) // On excessive idletime it is better to close the connection
								  // before the server gets any funny ideas.
			{
				dprintf ("idle-out mailhandler after %i counts\n", idlecount);
				Box->Handler->Lock.LockOnce();
				// Unless it is kept open on purpose (IDLE renews itself).
				if (!Box->Handler->KeepsConnection ()) Box->Handler->CloseConnection ();
				Box->Handler->Lock.UnlockOnce();
			}
			_usleep (15000);
		}
			
	}
	Box->Collector->Started = 0;
	return NULL;
}

