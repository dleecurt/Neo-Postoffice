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

ITCMessage::ITCMessage (unsigned int _Class, unsigned int _Subclass, WordList *_Message)
{
	Class = _Class;
	Subclass = _Subclass;
	Scheduled = 0;
	Reschedule = 0;
	Message = _Message;
}

ITCMessage::~ITCMessage (void)
{
	delete (Message);
}

ITCPipe::ITCPipe (void)
{
	Lock = new ASemaphore;
	First = Last = NULL;
	Count = 0;
	UrgentPending = false;
	NumPriorityClasses = 0;
	PriorityPending = false;
}

ITCPipe::~ITCPipe (void)
{
	ITCMessage *M, *nM;
	M = First;
	while (M)
	{
		nM = M->Next;
		delete (M);
		M = nM;
	}
	First = Last = NULL;
	Count = 0;
}

ITCMessage *ITCPipe::GetNextMessage (void)
{
	ITCMessage *M;
	Lock->LockOnce();
	M = First;
	if (M)
	{
		if (M->Next)
		{
			M->Next->Prev = NULL;
			First = M->Next;
			--Count;
		}
		else
		{
			First = Last = NULL;
			Count = 0;
		}
	}
	if (UrgentPending) UrgentPending = false;
	if ((PriorityPending) && (First))
	{
		PriorityPending = false;
		for (int ix=0; ix < NumPriorityClasses; ++ix)
		{
			if (PriorityClasses[ix] == First->Class)
			{
				ix = NumPriorityClasses;
				PriorityPending = true;
			}
		}
	}
	Lock->UnlockOnce();
	return (M);
}

ITCMessage *ITCPipe::GetNextMessage (unsigned int _Class)
{
	ITCMessage *M;
	Lock->LockOnce();
	M = First;
	while (M)
	{
		if (M->Class == _Class)
		{
			if (M->Prev)
			{
				if (M->Next)
				{
					M->Next->Prev = M->Prev;
					M->Prev->Next = M->Next;
				}
				else
				{
					M->Prev->Next = NULL;
					Last = M->Prev;
				}
			}
			else
			{
				if (M->Next)
				{
					M->Next->Prev = NULL;
					First = M->Next;
				}
				else
				{
					First = Last = NULL;
				}
			}
			--Count;
			Lock->UnlockOnce();
			return (M);
		}
		M = M->Next;
	}
	if ((UrgentPending) && (First) && !(First->Class & CLASS_URGENT)) UrgentPending = false;
	Lock->UnlockOnce();
	return (NULL);
}

void ITCPipe::PostMessage (unsigned int _Class, unsigned int _Subclass, WordList *_Message)
{
	ITCMessage *M 	= new ITCMessage (_Class, _Subclass, _Message);
	bool		pri	= false;
	
	Lock->LockOnce();
		if (_Class & CLASS_URGENT)
		{
			UrgentPending = true;
			if (First)
			{
				M->Prev = NULL;
				M->Next = First;
				First->Prev = M;
				First = M;
				++Count;
			}
			else
			{
				M->Next = NULL;
				M->Prev = NULL;
				First = Last = M;
				Count = 1;
			}
		}
		else
		{
			for (int i=0;i<NumPriorityClasses;++i)
			{
				if (_Class == PriorityClasses[i])
				{
					i = NumPriorityClasses;
					pri = true;
				}
			}
			if (!pri)
			{
				if (First)
				{
					M->Next = NULL;
					M->Prev = Last;
					Last->Next = M;
					Last = M;
					++Count;
				}
				else
				{
					M->Next = NULL;
					M->Prev = NULL;
					First = Last = M;
					Count = 1;
				}
			}
			else
			{
				PriorityPending = true;
				if (First)
				{
					M->Prev = NULL;
					M->Next = First;
					First->Prev = M;
					First = M;
					++Count;
				}
				else
				{
					M->Next = NULL;
					M->Prev = NULL;
					First = Last = M;
					Count = 1;
				}
			}
		}
	Lock->UnlockOnce();
}

void ITCPipe::PostMessage (unsigned int _Class, unsigned int _Subclass, char *msg1)
{
	WordList *W = new WordList;
	W->Add (msg1);
	PostMessage (_Class, _Subclass, W);
}

void ITCPipe::PostMessage (unsigned int _Class, unsigned int _Subclass, char *msg1, char *msg2)
{
	WordList *W = new WordList;
	W->Add (msg1);
	W->Add (msg2);
	PostMessage (_Class, _Subclass, W);
}

void ITCPipe::PostMessage (unsigned int _Class, unsigned int _Subclass, char *msg1, char *msg2, char *msg3)
{
	WordList *W = new WordList;
	W->Add (msg1);
	W->Add (msg2);
	W->Add (msg3);
	PostMessage (_Class, _Subclass, W);
}

int ITCPipe::Queue (void)
{
	return Count;
}

void ITCPipe::AddPriorityClass (unsigned int _Class)
{
	if (NumPriorityClasses<16) PriorityClasses[NumPriorityClasses++] = _Class;
}


