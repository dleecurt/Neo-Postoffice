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

//===============================================================================
// StorageList
// -----------
// Keeps track of a list of open StorageNode objects, re-using objects that
// might be shared between multiple mailboxes.
//===============================================================================

#include "mailstorage.h"
#include "mailbox.h"
#include "debug.h"
#include "checksum.h"

//===============================================================================
// CONSTRUCTOR	StorageList
// SYNOPSIS		Initializes the object and creates a Lock object.
//
// ARGUMENTS	Constructor needs no arguments.
//===============================================================================

StorageList::StorageList (void)
{
	First = Last = NULL;
	Lock = new ASemaphore ("storagelist");
}

//===============================================================================
// DESTRUCTOR	StorageList
// SYNOPSIS		Frees up the Lock object.
//===============================================================================

StorageList::~StorageList (void)
{
	if (First) eprintf ("storagelist still had subscribers on termination!\n");
	delete (Lock);
}

//===============================================================================
// METHOD		Open
// SYNOPSIS		Searches for a MailStorage object matching the specifications
// 				or instantiates a new one.
//
// ARGUMENTS	name		(char *)	Canonical name of the storage
//				subs		(MailBox *)	A pointer to the MailBox that wants
//										to use the storage.
//				Type		(short)		The kind of storage to be opened, one
//										of STORAGE_GDM or STORAGE_MBOX.
//
// RETURNS		A pointer to the open MailStorage.
//===============================================================================

MailStorage *StorageList::Open (const char *name, class MailBox *subs, short Type)
{
	// Set up a mutex lock to prevent nasty races
	
	Lock->LockOnce();
	
	// Try to see if the requested MailStorage is already in our pool.
	
	MailStorage *N = Find (name);
	
	if (!N) // Appearantly not
	{
		switch (Type) // What kind of MailStorage class do we need?
		{
			case STORAGE_GDBM: // GDBMStorage
				N = new GDBMStorage (name);
				N->LoadIndex ();
				N->Subscribe (subs);
				break;

			case STORAGE_MBOX: // MBOXStorage
				N = new MBoxStorage (name);
				N->LoadIndex ();
				N->Subscribe (subs);
				break;

			default: // BogonStorage
				eprintf ("unsupported storage class: %02x\n", Type);
				exit (1);
				break;
		}
		if (N) // Did we create anyhing meaningful?
		{
			if (Last) // We're not a virgin Pool, are we?
			{
				N->Prev = Last;
				N->Next = NULL;
				Last->Next = N;
				Last = N;
			}
			else // Doh, well lift that promdress.
			{
				N->Prev = N->Next = NULL;
				First = Last = N;
			}
		}
	}
	else // Okay so we already had one, in which case we'll just subscribe
	{
		N->Subscribe (subs);
	}

	// We're done, next one can enter.

	Lock->UnlockOnce();
	
	// Return pointer to the MailStorage object.
	
	return N;
}

//===============================================================================
// METHOD		Close
// SYNOPSIS		Unsubscribes a MailBox from a all its MailStorage objects. If the
//				reference count reaches 0, a MailStorage object is
//				deleted.
//
// ARGUMENTS	M			(MailBox *)	A pointer to the MailBox that wants to
//										unsubscribe from all MailStorage objects.
//
// RETURNS		Method returns no data.
//===============================================================================

void StorageList::Close (MailBox *M)
{
	// Perform a mutex lock
	
	Lock->LockOnce();
	
		// Iterate through the entire list
	
		MailStorage *N = First;
		MailStorage *NN;
		while (N)
		{
			// Acquire a pointer to the next object
		
			NN = N->Next;
			
			// Unsubscribe the MailBox from the storage
			
			N->Unsubscribe (M);
			
			if (!N->SCount()) // No subscribers left
			{
				// Remove the storage
			
				if (N->Prev)
				{
					if (N->Next)
					{
						N->Prev->Next = N->Next;
						N->Next->Prev = N->Prev;
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
				
				// And burn it...
				
				delete (N);
			}
			
			// Move cursor to next object
			
			N = NN;
		}
	
	// Release mutex lock.	
	
	Lock->UnlockOnce();
}

//===============================================================================
// METHOD		Find
// SYNOPSIS		Tries to find a MailStorage object in the list by name.
//
// ARGUMENTS	nm			(char *)	The name of the storage to search for.
//
// RETURNS		A pointer to the MailStorage or NULL if the search failed.
//===============================================================================

MailStorage *StorageList::Find (const char *nm)
{
	MailStorage		*result = NULL;
	MailStorage		*N;

		// Iterate through the list

		N = First;
		while (N)
		{
			// Is this the one we are looking for?
		
			if ( strcmp (N->Name, nm) == 0)
			{
				// Store a pointer to the object and end the search
			
				result = N;
				N = NULL;
			}
			else // Alas, let's keep on looking
			{
				N=N->Next;
			}
		}

	// Return the (un-)happy news.

	return result;
}
