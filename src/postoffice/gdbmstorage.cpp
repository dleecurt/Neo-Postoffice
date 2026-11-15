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
// GDBMStorage
// -----------
// A MailStorage implementation that saves mail in a GDBM-file as a mailbox.
//===============================================================================

#include "mailstorage.h"
#include "mailbox.h"
#include "strdupp.h"
#include "debug.h"
#include "gdbmtools.h"

#include <gdbm.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

//===============================================================================
// CONSTRUCTOR	GDBMStorage
// SYNOPSIS		Tries to open the gdbm file by name and initializes the
// 				object.
//
// ARGUMENTS	Constructor needs no arguments.
//===============================================================================

GDBMStorage::GDBMStorage (const char *filename)// : MailStorage()
{
	// Acquire a local copy of the filename.

	Filename = Name = strdupp (filename);
	
	// Attempt to open the gdbm file.
	
	DB = gdbm_open (Filename, 0, GDBM_WRCREAT | GDBM_FAST, 0600, NULL);
	
	if (!DB) // Something went wrong
	{
		// FIXME: Should this not be a fatal exception?
	
		eprintf ("gdbmstorage: could not open: %s\n", gdbm_strerror (gdbm_errno));
	}
	
	// Set the last sync time to now.
	
	lastsync = time (NULL);
	
	// Create an AsyncBuffer object to be used for intermediate object
	// storage.
	
	BF = new AsyncBuffer (4096);

	// Create a root-BTreeNode for StorageNodes.

	Root = new StorageNode (this, NULL, 0, 0);
	
	// Initialize internal variables.
	
	SFirst = SLast = NULL;
	Size_ = 0;
	Count_ = 0;
	Limit_ = 0;
	SCount_ = 0;
	
	// Create a Lock object.
	
	Lock = new ASemaphore ("mailstorage");
}

//===============================================================================
// DESTRUCTOR	StorageList
// SYNOPSIS		Saves the storage index to disk, frees up the Lock object,
//				closes the gdbm-file and deletes allocated memory.
//===============================================================================

GDBMStorage::~GDBMStorage (void)
{
	// Store the index-file

	StoreIndex(true);
	
	// If the database-file is open, close it.
	
	if (DB) gdbm_close(DB);
	dprintf ("gdbmstorage [%s] deleted\n", Filename);
	
	// Release memory
	
	delete[] Filename;
	delete (BF);
}

//===============================================================================
// METHOD		Store
// SYNOPSIS		Stores all available data in a MailMessage inside the gdbm
//				database.
//
// ARGUMENTS	M		(MailMessage *)	The Message to be stored
//
// RETURNS		Method returns no data
//===============================================================================

void GDBMStorage::Store (MailMessage *M)
{
	// Make sure we have the gdbm file open

	if (DB)
	{
		// Lock resources
	
		Lock->LockOnce();
		
		// Find the associated StorageNode
		
		StorageNode *N = Find_ (M->MsgID);
		
		// Create a search key;
		
		char *key = new char[strlen (M->MsgID) + 16];
		datum mydat, keydat;
		
		bool stored_data = false;
		int total_size = 0;
		int partcount = 0;
		
		// If there are any real headers, store them
		
		if (!N && (M->HeaderNames->Count))
		{
			BF->Pos = 0;
			
			dprintf ("gdbmstorage: storehdr %s\n", M->MsgID);
			
			// Generate a header-key and create a datum from it
			
			sprintf (key, "%s/hdr", M->MsgID);
			keydat.dptr = key;
			keydat.dsize = strlen (key);
			
			// Dump all headers in an AsyncBuffer block
			
			for (int i=0; i<M->HeaderNames->Count;++i)
			{
				BF->Add (M->HeaderNames->Get(i));
				BF->Add ((unsigned char) '\n');
				BF->Add (M->HeaderValues->Get(i));
				BF->Add ((unsigned char) '\n');
			}
			BF->Add ((unsigned char) 0);
			
			// Turn the block into a datum
			
			mydat.dptr = (char *) BF->Data;
			mydat.dsize = BF->Pos;
			
			// Increase the total recorded database size
			
			total_size += BF->Pos;

			// Store the gdbm record
			
			gdbm_store (DB, keydat, mydat, GDBM_INSERT);
			
			// Let there be truth
			
			stored_data = true;
		}
		
		// If the message contents are in RAM, store them
		
		if ((N && (N->Count < M->Count)) && ((M->Flags & (MMFL_HAVEBODY | MMFL_INRAM))))
		{
			dprintf ("gdbmstorage: store %s\n", M->MsgID);
			MailPart *C;
			
			// Iterate all parts
			
			for (int i=0; i<M->Count;++i)
			{
				keydat.dptr = key;
				
				if ((C = M->GetPart (i))) // Part exists?
				{
					++partcount;
					
					// Generate a data-key and make it a datum
					
					sprintf (key, "%s.%02i/dat", M->MsgID, i);
					keydat.dsize = strlen (key);
					
					int sz = C->ContentSize +1;

					// Create the gdbm node
					
					char *dat = gdbm_createnode (C->ContentType, C->FileName, C->Description, C->Contents, &sz);
					
					mydat.dptr = dat;
					mydat.dsize = sz;
					
					// Increase the total recorded database size
					
					total_size += sz;
					
					// Store the gdbm record
					
					gdbm_store (DB, keydat, mydat, GDBM_INSERT);
					stored_data = true;
					
					// Remove stray memory
					
					delete[] dat;
					dprintf ("gdbmstorage: store (%s)\n", keydat.dptr);
				}
			}
		}
		else // Nothing to save
		{
			dprintf ("gdbmstorage: cannot save empty message %s\n", M->MsgID);
		}
		
		if (stored_data)
		{
			//gdbm_sync (DB);
			
			// Create a StorageNode for this
			
			Add (M->MsgID, (size_t) total_size, partcount);
		}
		
		// Unlock resources
		
		Lock->UnlockOnce();
		
		// Give back claimed memory
		
		delete[] key;
	}
	else eprintf ("gdbmstorage: GDBM not open\n");
}

//===============================================================================
// METHOD		Retrieve
// SYNOPSIS		Retrieves all available data for a MailMessage from the gdbm
//				database.
//
// ARGUMENTS	M		(MailMessage *)	The Message to be retrieved
//
// RETURNS		true on success, false on failure
//===============================================================================

bool GDBMStorage::Retrieve (MailMessage *M)
{
	// Verify that we have the gdbm file open

	if (!DB)
	{
		eprintf ("gdbmstorage: GDBM db not open\n");
		return false;
	}
	
	bool result = true;
	
	// Lock resources
	
	Lock->LockOnce();
	
	// Get the associated StorageNode
	
	StorageNode *Nod = Find_ (M->MsgID);
	
	if (Nod)
	{
		// Key storage
	
		char *key = new char[strlen (M->MsgID) + 16];
		datum mydat, keydat;
		
		// If the message has no headers, retrieve them
		
		if (!M->HeaderNames->Count)
		{
			WordList WL;
			
			// Create a header-key and datum
			
			sprintf (key, "%s/hdr", M->MsgID);
			keydat.dptr = key;
			keydat.dsize = strlen (key);
			
			// Try to retrieve the gdbm record
			
			mydat = gdbm_fetch (DB, keydat);
			
			// Record exists?
			
			if (mydat.dptr)
			{
				// Split data up in lines
			
				WL.MakeGeneric (mydat.dptr, '\n', 1);
				
				for (int i=0; (i+1)<WL.Count; i+=2) // Iterate through them all
				{
					M->HeaderNames->Add (WL.Get(i));
					M->HeaderValues->Add (WL.Get(i+1));
				}
				
				// Release data
				
				free (mydat.dptr);
				result = true;
			}
			else // Emit bogons
			{
				eprintf ("[gdbmstorage] (%s) gdbm error: %s\n", key, gdbm_strerror (gdbm_errno));
				result = false;
			}
		}
		
		// If the message is not in RAM but is recorded to have a body
		
		if ( (!(M->Flags & MMFL_INRAM)) && (M->Flags & MMFL_HAVEBODY) )
		{
			char *filename;
			char *contenttype;
			char *description;
			char *nc;
			
			// Be informative for -DVERBOSE fetishists
			
			iprintf ("[gdbmstorage] downloading %i parts\n", M->Count);
			
			// Get all message parts
			
			for (int i=0; i<M->Count; ++i)
			{
				// Create a data-key and datum
			
				sprintf (key, "%s.%02i/dat", M->MsgID, i);
				keydat.dptr = key;
				keydat.dsize = strlen (key);
				
				// Retrieve the gdbm record
				
				mydat = gdbm_fetch (DB, keydat);
				
				// Record exists?
				
				if (mydat.dptr)
				{
					// Process the data
					
					contenttype = gdbm_node2contenttype (mydat.dptr, mydat.dsize);
					filename = gdbm_node2filename (mydat.dptr, mydat.dsize);
					description = gdbm_node2description (mydat.dptr, mydat.dsize);
					nc = gdbm_nodecontents (mydat.dptr, mydat.dsize);
					
					// Build a MailPart
					
					MailPart *P = M->GetPart (i);
					if (P)
					{
						P->Set (contenttype, filename, description, (unsigned long) strlen (nc), nc);
					}
					else
					{
						M->AddPart (contenttype, description, filename, nc);
						M->Count--;
					}
					
					// Clean up allocated memory
					
					free (mydat.dptr);
					//delete[] nc;
					delete[] description;
					delete[] contenttype;
					delete[] filename;
					result = true;
				}
				else // Emit bogons
				{
					eprintf ("[gdbmstorage] (%s) gdbm error: %s\n", key, gdbm_strerror (gdbm_errno));
					result = false;
				}
			}
		}
		delete[] key;
	}
	
	// Release resources
	
	Lock->UnlockOnce();
	
	// Say forth the good knews
	
	return result;
}

//===============================================================================
// METHOD		Delete
// SYNOPSIS		Remove a message from storage
//
// ARGUMENTS	key			(char *)	The message id.
//
// RETURNS		Method returns no data
//===============================================================================

void GDBMStorage::Delete (const char *key)
{
	// Database must be open

	if (!DB)
	{
		eprintf ("gdbmstorage: GDBM db not open\n");
		return;
	}
	
	// Lock resources
	
	Lock->LockOnce();
	
	// Find and destroy the node.
	
	StorageNode *N = Find (key);
	if (N) Delete (N);
	
	// Unlock resources
	
	Lock->UnlockOnce();
}

//===============================================================================
// METHOD		Delete
// SYNOPSIS		Remove a message from storage
//
// ARGUMENTS	N		(StorageNode *)	The message id.
//
// RETURNS		Method returns no data
//===============================================================================

void GDBMStorage::Delete (StorageNode *N)
{
	// Database must be open

	if (!DB)
	{
		eprintf ("gdbmstorage: GDBM db not open\n");
		return;
	}
	
	// Input parameter sanity check
	
	if (N)
	{
		// Initialize storage for key and datum
		
		char *key = new char[strlen (N->BKey) + 16];
		datum keydat;
		
		// Generate a header-key
		
		sprintf (key, "%s/hdr", N->BKey);
		keydat.dptr = key;
		keydat.dsize = strlen(key);
		
		// Delete gdbm record
		
		gdbm_delete (DB, keydat);
		dprintf ("gdbmstorage: deleted (%s)\n", key);

		// Iterate through MailParts
		
		for (int i=0; i<N->Count; ++i)
		{
			// Generate data-key and datum
		
			sprintf (key, "%s.%02i/dat", N->BKey, i);
			keydat.dsize = strlen(key);
			
			// Delete gdbm record
			
			gdbm_delete (DB, keydat);
			dprintf ("gdbmstorage: deleted (%s)\n", key);
		}
		
		// Sync gdbm database
		
		gdbm_sync (DB);
		
		// Return allocated memory
		
		delete[] key;
		
		// Unlink StorageNode
		
		Remove (N);
	}
	else // Emit bogons
	{
		eprintf ("gdbmstorage: delete: does not exist\n");
	}
}

//===============================================================================
// METHOD		StoreIndex
// SYNOPSIS		Saves the index-structure of the database to disk if it hasn't
// 				been saved in the last 15 seconds.
//
//				The GDBMStorage's index is stored inside the gdbm database
//				itself under the key "index".
//
// ARGUMENTS	force		(bool)	If set to true, the index will be saved
//									regardless of the lastsync timer.
//
// RETURNS		Method returns no data
//===============================================================================

void GDBMStorage::StoreIndex (bool force)
{
	// Database must be open

	if (!DB)
	{
		eprintf ("gdbmstorage: GDBM not open\n");
		return;
	}
	
	// If force is false and not enough time has passed, exit quietly.
	
	if ((!force) && ((time (NULL) - lastsync)<15))
	{
		return;
	}
	
	// Record new lastsync time.
	
	lastsync = time (NULL);

	// Reset AsyncBuffer
	
	BF->Pos = 0;

	datum keydat, mydat;

	// Recursively store the Node contents in BF
	
	if (Root->BHigher) StoreNode ((StorageNode *)Root->BHigher);
	if (Root->BLower) StoreNode ((StorageNode *)Root->BLower);
	
	// Add a nullbyte to end the string.
	
	BF->Add ((unsigned char) 0);
	
	// Pad to 4096-byte boundary
	
	int padding = 4096 - (BF->Pos & 4095);
	if (padding>0) BF->Grow (padding);
	
	// Create datum for key and data
	
	keydat.dptr = (char *) "index"; // gdbm does not modify keys
	keydat.dsize = 5;
	mydat.dptr = (char *) BF->Data;
	mydat.dsize = BF->Pos;
	
	// Store gdbm record
	
	gdbm_store (DB, keydat, mydat, GDBM_REPLACE);
	
	// Sync gdbm database
	
	gdbm_sync (DB);
}

//===============================================================================
// METHOD		LoadIndex
// SYNOPSIS		Loads the index-structure of the database from disk.
//
//				The GDBMStorage's index is stored inside the gdbm database
//				itself under the key "index".
//
// RETURNS		Method returns no data
//===============================================================================

void GDBMStorage::LoadIndex (void)
{
	// Database must exist;

	if (!DB)
	{
		eprintf ("gdbmstorage: GDBM not open\n");
		return;
	}
	
	WordList WL;
	WordList WWL;
	datum keydat, mydat;
	
	// Create datum for key
	
	keydat.dptr = (char *) "index"; // gdbm does not modify keys
	keydat.dsize = 5;
	
	// Lock resources
	
	Lock->LockOnce();
	
	// Fetch index record
	
	mydat = gdbm_fetch (DB, keydat);
	
	// Record exists?
	
	if (mydat.dptr)
	{
		// Split up in lines
	
		WL.MakeGeneric (mydat.dptr, '\n');
		
		// Iterate lines
		
		for (int i=0; i<WL.Count; ++i)
		{
			// Split up in tabs
		
			WWL.MakeGeneric (WL.Get(i), '\t');
			dprintf ("gdbmstorage: inserting key: %s\n", WWL.Get(0));
			
			// Add the StorageNode to memory
			
			Add (WWL.Get(0), (size_t) atoi (WWL.Get(1)), atoi (WWL.Get(2)));
		}
		iprintf ("\n");
	}
	else // Emit bogons
	{
		iprintf ("gdbmstorage: no index segment found\n");
	}
	
	// Unlock resources
	
	Lock->UnlockOnce();
}

//===============================================================================
// METHOD		StoreNode
// SYNOPSIS		Stores formatted data representing a StorageNode, recursing
//				through its children.
//
// ARGUMENTS	N		(StorageNode *)	The node to be dumped
//
// RETURNS		Method returns no data
//===============================================================================

void GDBMStorage::StoreNode (StorageNode *N)
{
	char buf[32];
	
	// Bogus-check argument
	
	if (N)
	{
		// Field one: key
		
		BF->Add (N->BKey);
		BF->Add ((unsigned char) '\t');
		
		// Field two: size
		
		sprintf (buf, "%zu", N->Size);
		BF->Add (buf);
		BF->Add ((unsigned char) '\t');
		
		// Field three: MailPart count
		
		sprintf (buf, "%i", N->Count);
		BF->Add (buf);
		BF->Add ((unsigned char) '\n');
		
		// Enable recursion, it's better than jolt!

		if (N->BHigher) StoreNode ((StorageNode *)N->BHigher);
		if (N->BLower) StoreNode ((StorageNode *)N->BLower);
	}
}
