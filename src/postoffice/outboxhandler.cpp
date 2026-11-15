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
#include "smtp.h"
#include "debug.h"
#include "status.h"

extern JobList JOBQ;

extern void extract_recipients (WordList *, char *);
extern MailBox *MailBoxList[];

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
				
				if (!send_message_smtp (HeaderNames, HeaderValues, mailfrom, RCPT, P->Contents))
				{
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
