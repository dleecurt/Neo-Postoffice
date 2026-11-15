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

#include "prefjournal.h"
#include "strdupp.h"

//===============================================================================
// CONSTRUCTOR	PrefJournalEntry
// SYNOPSIS		Initializes a journal entry.
//
// ARGUMENTS	_Type		(u_char)	The kind of action that is to be
// 										journaled/
// 				_Argument	(char*)		The mandatory first argument to the action
// 				_Second		(char*)		The optional second argument
//===============================================================================

PrefJournalEntry::PrefJournalEntry (unsigned char _Type, const char *_Argument, const char *_Second)
{
	Argument = strdupp ((char *) _Argument);
	if (_Second) Second = strdupp ((char *) _Second);
	else Second = NULL;
	Type = _Type;
}

//===============================================================================
// DESTRUCTOR	PrefJournalEntry
// SYNOPSIS		Frees up memory.
//===============================================================================

PrefJournalEntry::~PrefJournalEntry (void)
{
	delete[] Argument;
	if (Second) delete[] Second;
}

//===============================================================================
// CONSTRUCTOR	PrefJournal
// SYNOPSIS		Initializes the PrefJournal class
//===============================================================================

PrefJournal::PrefJournal (void)
{
	First = Last = Cursor = NULL;
}

//===============================================================================
// DESTRUCTOR	PrefJournal
// SYNOPSIS		Frees up all PrefJournalEntry children.
//===============================================================================

PrefJournal::~PrefJournal (void)
{
	PrefJournalEntry *E, *NE;

	E = First;
	while (E)
	{
		NE = E->Next;
		delete (E);
		E = NE;
	}
}

//===============================================================================
// METHOD		Add
// SYNOPSIS		Creates a new journal entry and adds it to the end of the log.
// 				The cursor will be pointed to this last entry.
//
// ARGUMENTS	_Type		(u_char)	The kind of action that is to be
// 										journaled
// 				_Argument	(char*)		The mandatory first argument to the action
// 				_Second		(char*)		The optional second argument
//
// RETURNS		Method returns no data
//===============================================================================

void PrefJournal::Add (unsigned char _Type, const char *_Argument, const char *_Second)
{
	PrefJournalEntry *E = new PrefJournalEntry (_Type, _Argument);
	if (Last)
	{
		E->Prev = Last;
		E->Next = NULL;
		Last->Next = E;
		Last = Cursor = E;
	}
	else
	{
		E->Prev = E->Next = NULL;
		First = Last = Cursor = E;
	}
}

//===============================================================================
// METHOD		Rewind
// SYNOPSIS		Resets the readout cursor of the journal and points it to the
// 				first entry.
//
// ARGUMENTS	This method cannot be argued with.
// RETURNS		Method returns no data
//===============================================================================

void PrefJournal::Rewind (void)
{
	Cursor = First;
}

//===============================================================================
// METHOD		Replay
// SYNOPSIS		Reads the journal entry from under the cursor and moves the
// 				cursor one position forward.
//
// ARGUMENTS	No arguments (this is a likable method).
// RETURNS		A pointer to the PrefJournalEntry
//===============================================================================

PrefJournalEntry *PrefJournal::Replay (void)
{
	if (Cursor)
	{
		if (Cursor->Prev)
		{
			if (Cursor->Next)
			{
				Cursor->Next->Prev = Cursor->Prev;
				Cursor->Prev->Next = Cursor->Next;
			}
			else
			{
				Cursor->Prev->Next = NULL;
				Last = Cursor->Prev;
			}
		}
		else
		{
			if (Cursor->Next)
			{
				Cursor->Next->Prev = NULL;
				First = Cursor->Next;
			}
			else
			{
				First = Last = NULL;
			}
		}
		PrefJournalEntry *Result = Cursor;
		Cursor = Cursor->Next;
		return (Result);
	}
	return (NULL);
}

//===============================================================================
// METHOD		Erase
// SYNOPSIS		Removes and frees all PrefJournalEntry children from the journal.
//
// ARGUMENTS	No arguments (this method has authority).
// RETURNS		Method returns no data
//===============================================================================

void PrefJournal::Erase (void)
{
	PrefJournalEntry *E, *NE;

	E = First;
	while (E)
	{
		NE = E->Next;
		delete (E);
		E = NE;
	}
    First = Last = Cursor = NULL;
}
