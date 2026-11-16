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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "debug.h"
#include "mailbox.h"
#include "var.h"
#include "reg.h"
#include "chop.h"
#include "eaddr.h"
#include "checksum.h"
#include "status.h"
#include "debug.h"

// The MBox handler doesn't have to keep track of the more tedious bits of a MailHander,
// being anything "connection"-related. All the infrastructure is in the CheckGroup method.

typedef MBoxStorage *MBoxStoragePtr;
extern JobList JOBQ;

MBoxHandler::MBoxHandler (MailBox *_Parent, class Var *_Parameters)
{
	Parameters = new Var (_Parameters);
	Parent = _Parent;
	ConnectionOpen = 0;
	MBFiles = new WordList (Parameters->Get_wordlist ("files"));
	MBStorage = new MBoxStoragePtr[MBFiles->Count];
	
	for (int i=0;i<MBFiles->Count;++i)
	{
		MBStorage[i] = new MBoxStorage (MBFiles->Get(i));
	}
}

MBoxHandler::~MBoxHandler (void)
{
	delete (Parameters);
	for (int i=0;i<MBFiles->Count;++i) delete MBStorage[i];
	delete MBStorage;
	delete MBFiles;
}

void MBoxHandler::OpenConnection (void)
{
}

void MBoxHandler::CloseConnection (void)
{
	//	if( lockfd > 0 ) {
	//close( lockfd );
	//unlink( lock_filename );
}

// Messages are read whole during the check, so the window never asks for a
// body. Neo-PostOffice uses it to read a message again from its mbox file
// (to copy it into a Maildir exactly).
void MBoxHandler::GetBody (char *msgid)
{
	MailMessage *M = Parent->Find (msgid);
	if (!M) return;
	WordList *MBFiles = Parameters->Get_wordlist ("files");
	for (int ix = 0; ix < MBFiles->Count; ++ix)
		if (MBStorage[ix] && MBStorage[ix]->Retrieve (M)) return;
}

// The groupname is irrelevant. All mbox-files will be queried when this method is called.

void MBoxHandler::CheckGroup (const char */*groupname*/)
{
	WordList	*MBFiles = Parameters->Get_wordlist ("files");
	
	MailMessage *M;
	char		 msg[256];
	
	int jid = JOBQ.NewJob (Parent->Name);
	
	for (int ix=0;ix<MBFiles->Count;++ix)
	{
		sprintf (msg, "Checking MBox");
		JOBQ.Stat (jid, msg);
		JOBQ.Progress (jid, (100*ix)/(MBFiles->Count));
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		dprintf ("[mboxhandler] checking mbox <%s>\n", MBFiles->Get(ix));
		
		MBStorage[ix]->LoadIndex();
		MBStorage[ix]->CheckSync();
		StorageNode *N = (StorageNode *) MBStorage[ix]->Root->BNext;
		
		while ((N) && (N != MBStorage[ix]->Root))
		{
			dprintf ("[mboxhandler] checking message %s\n", N->BKey);
			if (!Parent->Have (N->BKey))
			{
				M = new MailMessage (Parent, N->BKey);
				MBStorage[ix]->Retrieve (M);
				Parent->Deliver (M);
				Announce (M);
				M = NULL;
			}
			N = (StorageNode *) N->BNext;
		}
		
		MBStorage[ix]->StoreIndex(true);
	}
	lock_filename = NULL;
	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}

void MBoxHandler::DeleteMessage (char *msgid)
{
	for (int ix=0;ix<MBFiles->Count;++ix)
	{
		MBStorage[ix]->Delete (msgid);
	}
}

void MBoxHandler::Expire (void)
{
}

int MBoxHandler::PostMessage (WordList */*groups*/, MailMessage */*M*/)
{
	return 0;
}

void MBoxHandler::Announce (MailMessage *M)
{
	for (int j=0; j<M->Subscribers->Count();++j)
	{
		ViewListNode *VLN = M->Subscribers->Get (j);
		if (VLN) Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, VLN->View->Name, M->MsgID);
	}
}
