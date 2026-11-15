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

#include "semaphore.h"
#include "mailbox.h"

Scheduler::Scheduler (void)
{
	Lock = new ASemaphore;
	First = Last = NULL;
	Count = 0;
}

Scheduler::~Scheduler (void)
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
}

void Scheduler::Schedule (int Delay, int Interval, unsigned long Class, unsigned long Subclass, WordList *Message)
{
	ITCMessage *M = new ITCMessage (Class, Subclass, Message);
	M->Scheduled = time (NULL) + Delay;
	M->Reschedule = Interval;
	Lock->LockOnce();
	if (First)
	{
		M->Prev = Last;
		M->Next = NULL;
		Last->Next = M;
		Last = M;
		++Count;
	}
	else
	{
		M->Prev = NULL;
		M->Next = NULL;
		First = Last = M;
		Count = 1;
	}
	Lock->UnlockOnce();
}

void Scheduler::Schedule (int Delay, int Interval, unsigned long Class, unsigned long Subclass, char *msg1)
{
	WordList *W = new WordList;
	W->Add (msg1);
	Schedule (Delay, Interval, Class, Subclass, W);
}

void Scheduler::Schedule (int Delay, int Interval, unsigned long Class, unsigned long Subclass, char *msg1, char *msg2)
{
	WordList *W = new WordList;
	W->Add (msg1);
	W->Add (msg2);
	Schedule (Delay, Interval, Class, Subclass, W);
}

void Scheduler::Schedule (int Delay, int Interval, unsigned long Class, unsigned long Subclass, char *msg1, char *msg2, char *msg3)
{
	WordList *W = new WordList;
	W->Add (msg1);
	W->Add (msg2);
	W->Add (msg3);
	Schedule (Delay, Interval, Class, Subclass, W);
}

ITCMessage *Scheduler::CheckSchedule (void)
{
	ITCMessage *M;
	ITCMessage *result = NULL;
	time_t ti;
	
	Lock->LockOnce();
	
	M = First;
	ti = time (NULL);
	
	while (M)
	{
		if (M->Scheduled < ti)
		{
			if (! M->Reschedule)
			{
				if (M->Next)
				{
					if (M->Prev)
					{
						M->Prev->Next = M->Next;
						M->Next->Prev = M->Prev;
					}
					else
					{
						M->Next->Prev = NULL;
						First = M->Next;
					}
				}
				else
				{
					if (M->Prev)
					{
						M->Prev->Next = NULL;
						Last = M->Prev;
					}
					else
					{
						Last = First = NULL;
					}
				}
				--Count;
				result = M;
			}
			else
			{
				WordList *W = new WordList (M->Message);
				result = new ITCMessage (M->Class, M->Subclass, W);
				M->Scheduled += M->Reschedule;
			}
			M = NULL;
		}
		else M = M->Next;
	}
	Lock->UnlockOnce();
	return (result);
}