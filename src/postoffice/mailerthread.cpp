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

#define ST_CHECKMESSAGES 0x01
#define ST_POSTMESSAGES  0x02

extern void displaymessage (MailMessage *);
extern void updatefolder (void);
extern MailBox *currentmailbox;

MailerThread::~MailerThread (void)
{
	if (Started) Kill();
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
				
				case CLASS_CHECKGROUP:
					strcpy (currentgroup, M->Message->Get(0));
					//printf ("class_checkgroup handler=%08x m=%08x m->message=%08x\n", Box->Handler, M, M->Message);
					Box->Handler->Lock.LockOnce();
					Box->Handler->CloseConnection();
					Box->Handler->OpenConnection();
					Box->Handler->CheckGroup (M->Message->Get(0));
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
					
				case CLASS_DELETE:
					Box->Handler->Lock.LockOnce();
					Box->Handler->DeleteMessage (M->Message->Get(0));
					Box->Handler->Lock.UnlockOnce();
					//Box->ToUI->PostMessage (CLASS_DELETE, NO_SUBCLASS, M->Message->Get(0));
					break;
					
				case CLASS_MOVE:
					//Box->Handler->MoveMessage (M->Message->Get(0), M->Message->Get(1), M->Message->Get(2));
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
			if (++idlecount == 20000) // On excessive idletime it is better to close the connection
								  // before the server gets any funny ideas.
			{
				dprintf ("idle-out mailhandler after %i counts\n", idlecount);
				Box->Handler->Lock.LockOnce();
				Box->Handler->CloseConnection ();
				Box->Handler->Lock.UnlockOnce();
			}
			_usleep (15000);
		}
			
	}
	Box->Collector->Started = 0;
	return NULL;
}

