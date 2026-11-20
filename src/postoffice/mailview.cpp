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
#include "reg.h"
#include "debug.h"

#include <string.h>
#include <strings.h>

extern Var PREFERENCES;
extern const char *strcasestr (const char *, const char *);

MailViewNode::MailViewNode (class MailMessage *Msg, class MailView *MV)
{
	Message = Msg;
	Parent = MV;
	if (Msg)
	{
		Msg->Subscribers->Subscribe (this);
	}
};

MailViewNode::~MailViewNode (void)
{
	if (Message)
	{
		Message->Subscribers->Unsubscribe (this);
	}
};

MailView::MailView (class MailBox *P, const char *N)
{
	Name = new char[strlen(N)+1];
	strcpy (Name, N);
	Parent = P;
	Lock = new ASemaphore;
	CacheLock = new ASemaphore;
	MutationLock = new ASemaphore;
	_Count = 0;
	First = NULL;
	Last = NULL;
	MFirst = NULL;
	_CachePos = -2;
	Cache = NULL;
	NewCount = 0;
	Biff = false;
}

MailView::~MailView (void)
{
	MailViewNode *N, *NN;
	
	N = First;
	
	while (N)
	{
		NN = N->Next;
		delete (N);
		N = NN;
	}
	
	ClearMutations ();
	delete (MutationLock);
	delete (CacheLock);
	
}

// Check if a mailmessage is matching this view's rules, associate to it if so.

int MailView::ProcessRules (MailMessage *M)
{
	int		  result = 0;
//	char 		*bitwise;
	int		  efield;
//	char 		*eval;
	int			i;
	int			x;

	//printf ("processing filter: %s\n", PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Parent->Name)->Get_collection ("folders")->Get_collection (Name)->Get_char ("filters"));
	WordList *Rules = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Parent->Name)->Get_collection ("folders")->Get_collection (Name)->Get_wordlist ("filters");
	if (Rules)
	{
		if (Rules->Count == 0) // If no filter is defined for this view, assume it to have a groups header containing its name
		{
			Rules->AddInt (ENVELOPE_GROUPS);
			Rules->Add ("subs");
			Rules->Add ("accept");
			Rules->AddInt (0);
			Rules->Add (Name);
		}
		if (Rules->Count > 4)
		{
			const char *fieldvalue;
			char *matchtype;
			char *action;
			int   validmatch;
			char *ourvalue;
			
			result = 1;
			for (i=0; (i+4) < Rules->Count; i+= 5)
			{
				efield = Rules->GetInt (i);
				validmatch = 0;
				matchtype = Rules->Get (i+1);
				action = Rules->Get (i+2);
				ourvalue = Rules->Get (i+4);

				if ((efield == 0) && (*(Rules->Get(i)) != '0'))
				{
					x = M->HeaderNames->Find (Rules->Get(i));
					if (x) fieldvalue = M->HeaderValues->Get(x);
					else fieldvalue = (char *) "";
				}
				else fieldvalue = M->Envelope->Get (efield);
				
				matchtype = Rules->Get (i+1);
				action    = Rules->Get (i+2);

				if (!ourvalue[0]) validmatch = 0;
				else if (strcasecmp (matchtype, "glob") == 0) validmatch = wild_match (ourvalue, (char *) fieldvalue);
				else if (strcasecmp (matchtype, "case") == 0)
				{
					if (strcasestr (fieldvalue, ourvalue)) validmatch = 1;
					else validmatch = 0;
				}
				else validmatch = (strstr (fieldvalue,ourvalue)) ? 1 : 0;

				//printf ("%s('%s','%s') = %i\n", matchtype, ourvalue, fieldvalue, validmatch);

				if (validmatch)
				{
					if (strcasecmp (action, "accept") == 0)
					{
						result = 1;
						i = Rules->Count;
					}
					else if (strcasecmp (action, "deny") == 0)
					{
						result = 0;
						i = Rules->Count;
					}
					else if (strcasecmp (action, "accepts") == 0)
					{
						result = -1;
						i = Rules->Count;
					}
					else if (strcasecmp (action, "steal") == 0)
					{
						result = -2;
						i = Rules->Count;
					}
				}
				else result = 0;
			}
		}
	}
	if (result!=0)
	{
		Associate (M);
		Lock->LockRW();
		++NewCount;
		Lock->UnlockRW();
	}
	if (result == -1) result = 0;
	return (result);
}

int MailView::ProcessScore (MailMessage *M)
{
	int		  result = 1;
//	char 		*bitwise;
	int		  efield;
//	char 		*eval;
	int			i;
	int			x;

	//printf ("processing filter: %s\n", PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Parent->Name)->Get_collection ("folders")->Get_collection (Name)->Get_char ("filters"));
	WordList *Rules = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Parent->Name)->Get_collection ("folders")->Get_collection (Name)->Get_wordlist ("filters");
	if (Rules)
	{
		if (Rules->Count > 4)
		{
			const char *fieldvalue;
			char *matchtype;
			char *action;
			int   score;
			int   validmatch;
			char *ourvalue;
			
			result = 1;
			for (i=0; (i+4) < Rules->Count; i+= 5)
			{
				efield = Rules->GetInt (i);
				validmatch = 0;
				matchtype = Rules->Get (i+1);
				action = Rules->Get (i+2);
				ourvalue = Rules->Get (i+4);
				score = Rules->GetInt (i+3);

				if (strcasecmp (action, "score") == 0)
				{
					if (strcasecmp (Rules->Get(i), "Message-ID") == 0) fieldvalue = M->MsgID;
					else if (strcasecmp (Rules->Get(i), "Subject") == 0) fieldvalue = M->Envelope->Get (ENVELOPE_SUBJECT);
					else if (strcasecmp (Rules->Get(i), "From") == 0) fieldvalue = M->Envelope->Get (ENVELOPE_MAILFROM);
					else if ((efield == 0) && (*(Rules->Get(i)) != '0'))
					{
						x = M->HeaderNames->Find (Rules->Get(i));
						if (x) fieldvalue = M->HeaderValues->Get(x);
						else fieldvalue = (char *) "";
					}
					else fieldvalue = M->Envelope->Get (efield);
					
					matchtype = Rules->Get (i+1);
					action    = Rules->Get (i+2);
					score     = Rules->GetInt (i+3);

					if (!ourvalue[0]) validmatch = 0;
					else if (strcasecmp (matchtype, "glob") == 0) validmatch = wild_match (ourvalue, (char *) fieldvalue);
					else if (strcasecmp (matchtype, "case") == 0)
					{
						if (strcasestr (fieldvalue, ourvalue)) validmatch = 1;
						else validmatch = 0;
					}
					else validmatch = (strstr (fieldvalue,ourvalue)) ? 1 : 0;

					//printf ("%s('%s','%s') = %i\n", matchtype, ourvalue, fieldvalue, validmatch);

					if (validmatch)
					{
						result += score;
						dprintf ("... score updated to %i\n", score);
					}
				}
			}
		}
	}
	return (result);
}

// Adds the message at the end, or at the start for a message marked
// AtFront (older mail fetched after newer, by IMAP).
void MailView::Associate (MailMessage *M)
{
	MailViewNode *N = new MailViewNode (M, this);
	Lock->LockRW();
	if (M->AtFront && First)
	{
		N->Prev = NULL;
		N->Next = First;
		First->Prev = N;
		First = N;
		// Every position moved up by one.
		CacheLock->LockOnce();
		_CachePos = -2;
		CacheLock->UnlockOnce();
	}
	else if (Last)
	{
		N->Next = NULL;
		N->Prev = Last;
		Last->Next = N;
		Last = N;
	}
	else
	{
		N->Next = NULL;
		N->Prev = NULL;
		First = Last = N;
	}
	++_Count;
	Lock->UnlockRW();
}

void MailView::Unassociate (MailMessage *M)
{
	MailViewNode *N;
	int done = 0;
	int position = 0;
	
	Lock->LockRW();
	
	N = First;
	while ((N) && (!done))
	{
		if (N->Message == M)
		{
			_CachePos = -2;
			if (N->Prev)
			{
				if (N->Next)
				{
					N->Next->Prev = N->Prev;
					N->Prev->Next = N->Next;
				}
				else
				{
					N->Prev->Next = NULL;
					Last = N->Prev;
				}
			}
			else
			{
				if (N->Next)
				{
					N->Next->Prev = NULL;
					First = N->Next;
				}
				else
				{
					First = Last = NULL;
				}
			}
			--_Count;
			delete (N);
			N = NULL;
			done = 1;
		}
		else
		{
			N=N->Next;
			++position;
		}
	}
	
	Lock->UnlockRW();
}

int MailView::Count (void)
{
	return (_Count);
}

MailMessage *MailView::GetMessage (int id)
{
	MailViewNode *result;
	int pos;
	
	Lock->LockR();
	if ( (id < 0) || (id >= Count()) )
	{
		Lock->UnlockR();
		return ((MailMessage *) NULL);
	}
	CacheLock->LockOnce();
	if (id == _CachePos)
	{
		result = Cache;
		CacheLock->UnlockOnce();
		Lock->UnlockR();
		return (result->Message);
	}
	if (id == _CachePos+1)
	{
		++_CachePos;
		result = Cache = Cache->Next;
		CacheLock->UnlockOnce();
		Lock->UnlockR();
		return (result->Message);
	}
	CacheLock->UnlockOnce();
	
	result = First;
	pos = 0;
	
	while ((result) && (pos++ < id)) result = result->Next;

	if (result)
	{
		CacheLock->LockOnce();
		_CachePos = id;
		Cache = result;
		CacheLock->UnlockOnce();
		Lock->UnlockR();
		return (result->Message);
	}

	Lock->UnlockR();	
	return NULL;
}

void MailView::DoneWithMessage (void)
{
	//Lock->UnlockR();
}

void MailView::ClearMutations (void)
{
	MailViewMutation *Mu, *Mn;
	
	MutationLock->LockOnce();
	
	Mu = MFirst;
	while (Mu)
	{
		Mn = Mu->Next;
		delete (Mu);
		Mu = Mn;
	}
	MFirst = MLast = NULL;
	
	MutationLock->UnlockOnce();
}

int MailView::GetNextMutation (void)
{
	int result = -1;
	MailViewMutation *NX;
	
	MutationLock->LockOnce();

	if (MFirst)
	{
		result = MFirst->AffectedRecord;
		NX = MFirst->Next;
		delete (NX);
		MFirst = NX;
		if (!MFirst) MLast = NULL;
	}
	
	MutationLock->UnlockOnce();
	
	return (result);
}

ViewListNode::ViewListNode (MailViewNode *N)
{
	ViewNode = N;
	View = N->Parent;
}

ViewListNode::~ViewListNode (void)
{
}

ViewList::ViewList (MailMessage *_Parent)
{
	First = Last = NULL;
	_Count = 0;
	Parent = _Parent;
}

ViewList::~ViewList (void)
{
	ViewListNode *N, *nN;
	
	N = First;
	while (N)
	{
		nN = N->Next;
		delete (N);
		N = nN;
	}
	First = Last = NULL;
}

void ViewList::Subscribe (MailViewNode *V)
{
	ViewListNode *N = new ViewListNode (V);
	if (First)
	{
		N->Prev = Last;
		N->Next = NULL;
		Last->Next = N;
		Last = N;
		++_Count;
	}
	else
	{
		N->Prev = N->Next = NULL;
		First = Last = N;
		_Count = 1;
	}
}

void ViewList::UnTie (void)
{
	while (First)
	{
		First->ViewNode->Parent->Unassociate (Parent);
		//Unsubscribe (First->ViewNode);
	}
}

void ViewList::Unsubscribe (MailViewNode *V)
{
	ViewListNode *N = First;
	while (N)
	{
		if (N->ViewNode == V)
		{
			if (N->Prev)
			{
				if (N->Next)
				{
					N->Next->Prev = N->Next;
					N->Prev->Next = N->Prev;
				}
				else
				{
					N->Prev->Next = NULL;
					Last = N->Prev;
				}
			}
			else
			{
				if (N->Next)
				{
					N->Next->Prev = NULL;
					First = N->Next;
				}
				else
				{
					First = Last = NULL;
				}
			}
			--_Count;
			delete (N);
			N = NULL;
		}
		else N = N->Next;
	}
}

ViewListNode *ViewList::Get (int cnt)
{
	ViewListNode *N = First;
	int i = 0;

	while ((N)&&(i<cnt))
	{
		N = N->Next;
		++i;
	}
	return N;
}

int ViewList::Count (void)
{
	return (_Count);
}

