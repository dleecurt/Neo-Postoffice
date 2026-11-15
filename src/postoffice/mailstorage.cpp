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

#include "mailstorage.h"
#include "mailbox.h"
#include "strdupp.h"
#include "checksum.h"
#include "debug.h"

StorageNode::StorageNode (MailStorage *Parent, const char *msgid, size_t sz, int cnt) : BTreeNode (msgid, Parent->Root)
{
	//Next = Prev = NULL;
	//Msgid = strdupp (msgid);
	//Checksum = checksum (Msgid);
	Size = sz;
	Count = cnt;
}

StorageNode::~StorageNode (void)
{
	//delete[] Msgid;
}

MailBoxRef::MailBoxRef (MailBox *M)
{
	Ref = M;
}

MailStorage::MailStorage (void)
{
	Root = NULL;
	Root = new StorageNode (this, NULL, 0, 0);
	SFirst = SLast = NULL;
	Size_ = 0;
	Count_ = 0;
	Limit_ = 0;
	SCount_ = 0;
	Lock = new ASemaphore ("mailstorage");
}

MailStorage::~MailStorage (void)
{
	delete (Lock);
	if (Root) delete (Root);
}

//===============================================================================
// METHOD		Store
// SYNOPSIS		Overloadable placeholder for storing the body information in
//				a mail message.
//
// ARGUMENTS    M			(MailMessage *)	The message containing the data
// 											to be stored.
//
// RETURNS		method returns no data
//===============================================================================

void MailStorage::Store (MailMessage *M)
{
	eprintf ("Unoverloaded MailStorage::Store called\n");
}

//===============================================================================
// METHOD		Retrieve
// SYNOPSIS		Overloadable placeholder for retrieving the body information in
//				a mail message.
//
// ARGUMENTS    M			(MailMessage *)	The message the result data should
// 											be stored in.
//
// RETURNS		true on success, false on failure.
//===============================================================================

bool MailStorage::Retrieve (MailMessage *M)
{
	eprintf ("Unoverloaded MailStorage::Retrieve called\n");
	return false;
}

//===============================================================================
// METHOD		Delete
// SYNOPSIS		Overloadable placeholder for deleting the body information 
//				connected to a certain key.
//
// ARGUMENTS    key			(char *)		The key (message-id) of the message
// 											to be flushed.
// 
// RETURNS		method returns no data
//===============================================================================

void MailStorage::Delete (const char *key)
{
	eprintf ("Unoverloaded MailStorage::Delete called\n");
	Remove (key);
}

void MailStorage::Delete (StorageNode *key)
{
	eprintf ("Unoverloaded MailStorage::Delete called\n");
	//Remove (key);
}

//===============================================================================
// METHOD		LoadIndex
// SYNOPSIS		Overloadable placeholder for loading the index connected to 
//				a storage object.
//
// RETURNS		method returns no data
//===============================================================================

void MailStorage::LoadIndex (void)
{
	eprintf ("Unoverloaded MailStorage::LoadIndex called\n");
}

//===============================================================================
// METHOD		StoreIndex
// SYNOPSIS		Overloadable placeholder for saving the index connected to 
//				a storage object.
//
// RETURNS		method returns no data
//===============================================================================

void MailStorage::StoreIndex (bool foo)
{
	eprintf ("Unoverloaded MailStorage::StoreIndex called\n");
}

//===============================================================================
// METHOD		Sizeof
// SYNOPSIS		Returns the data size of an object in storage.
//
// ARGUMENTS    key			(char *)		The key (message-id) of the object
//
// RETURNS		the size (or 0 on failure)
//===============================================================================

size_t MailStorage::Sizeof (const char *key)
{
	StorageNode *N = Find (key);
	if (N)
	{
		return N->Size;
	}
	return 0;
}

//===============================================================================
// METHOD		Have
// SYNOPSIS		Checks storage for the existance of an object key.
//
// ARGUMENTS    key			(char *)		The key (message-id) of the object
//
// RETURNS		true on success, false on failure.
//===============================================================================

bool MailStorage::Have (const char *key)
{
	StorageNode *N = Find (key);
	
	if (N) return true;
	return false;
}

//===============================================================================
// METHOD		Add
// SYNOPSIS		Adds a reference to a keyed object to the index. Should be
// 				called from within the "Store" virtual method.
//
// ARGUMENTS    key			(char *)		The key (message-id) of the object
// 				sz			(size_t)		Size of the object's data
// 				cnt			(int)			Number of parts
//
// RETURNS		method returns no data
//===============================================================================

void MailStorage::Add (const char *key, size_t sz, int cnt, unsigned int xkey)
{
	StorageNode *N;
	if (!(N = Find_ (key)))
	{
		StorageNode *N = new StorageNode (this, key, sz, cnt);
		N->ExtraKey = xkey;
		
		Size_ += sz;
		++Count_;
		if (Limit_)
		{
            /* FIXME
   			while ((Size_ > Limit_) && (First != N)) 
   			{
   				dprintf ("gdbmstorage: size_limit remove (%s)\n", First->Msgid);
   				Delete (First);
   			} */
		}
	}
	else
	{
		if (cnt != N->Count)
		{
			Size_ += (sz - N->Size);
			if (Limit_)
			{
                /* FIXME: still need a linked list? 
				while ((Size_ > Limit_) && (First != N)) 
				{
					dprintf ("gdbmstorage: size_limit remove (%s)\n", First->Msgid);
					Delete (First);
				} */
			}
		}
		N->Size = sz;
		N->Count = cnt;
		N->ExtraKey = xkey;
	}
}

//===============================================================================
// METHOD		Remove
// SYNOPSIS		Removes a reference to a keyed object from the index. Should be
// 				called from within the "Delete" virtual method.
//
// ARGUMENTS    key			(char *)		The key (message-id) of the object
//
// RETURNS		method returns no data
//===============================================================================

void MailStorage::Remove (const char *key)
{
	//Lock->LockOnce(); // Locking not needed, always called from ::Delete
	StorageNode *N = Find_ (key);
	Remove (N);
	//Lock->UnlockOnce();
}

void MailStorage::Remove (StorageNode *N)
{
	//Lock->LockOnce(); // Locking not needed, always called from ::Delete
	if (N)
	{
		--Count_;
		Size_ -= N->Size;
		delete (N);
	}
	else
	{
		eprintf ("MailStorage::Remove() on NULL object\n");
	}
	//Lock->UnlockOnce();
}

//===============================================================================
// METHOD		Find_
// SYNOPSIS		Looks up a storage node by its key
//
// ARGUMENTS    key			(char *)		The key (message-id) of the object
//
// RETURNS		pointer to the storage object, NULL on failure
//===============================================================================

StorageNode *MailStorage::Find_ (const char *key)
{
	StorageNode *N = (StorageNode *) Root->Find (key);
	return N;
}

//===============================================================================
// METHOD		Find
// SYNOPSIS		Looks up a storage node by its key, locks the index
//
// ARGUMENTS    key			(char *)		The key (message-id) of the object
//
// RETURNS		pointer to the storage object, NULL on failure
//===============================================================================

StorageNode *MailStorage::Find (const char *key)
{
	StorageNode *N;
	
	Lock->LockOnce();
	N = Find_ (key);
	Lock->UnlockOnce();
	return (N);
}

//===============================================================================
// METHOD		Size
// SYNOPSIS		Calculate the size of the entire cache
//
// RETURNS		The cache size
//===============================================================================

size_t MailStorage::Size (void)
{
	return Size_;
}

//===============================================================================
// METHOD		Count
// RETURNS		The number of storage objects in the cache
//===============================================================================

int MailStorage::Count (void)
{
	return Count_;
}

//===============================================================================
// METHOD		SCount
// RETURNS		The number of subscribers to this storage
//===============================================================================

int MailStorage::SCount (void)
{
	return SCount_;
}

//===============================================================================
// METHOD		Subscribe
// SYNOPSIS		Attach the Storage to a MailBox
//
// ARGUMENTS    B			(MailBox *)		The MailBox object to attach to
//
// RETURNS		method returns no data
//===============================================================================

void MailStorage::Subscribe (MailBox *B)
{
	MailBoxRef *R = new MailBoxRef (B);
	Lock->LockOnce();
	if (SLast)
	{
		R->Prev = SLast;
		R->Next = NULL;
		SLast->Next = R;
		SLast = R;
	}
	else
	{
		R->Prev = R->Next = NULL;
		SFirst = SLast = R;
	}
	++SCount_;
	Lock->UnlockOnce();
}

//===============================================================================
// METHOD		Unsubscribe
// SYNOPSIS		Detach the Storage from a MailBox
//
// ARGUMENTS    B			(MailBox *)		The MailBox object to detach from
//
// RETURNS		method returns no data
//===============================================================================

void MailStorage::Unsubscribe (MailBox *B)
{
	// Lock resouces

	Lock->LockOnce();
	
	// Iterate Subscriber list
	
	MailBoxRef *R = SFirst;

	while (R)
	{
		// Is this the node pointing to the mailbox?
	
		if (R->Ref == B)
		{
			// Unlink please
		
			if (R->Prev)
			{
				if (R->Next)
				{
					R->Next->Prev = R->Prev;
					R->Prev->Next = R->Next;
				}
				else
				{
					R->Prev->Next = NULL;
					SLast = R->Prev;
				}
			}
			else
			{
				if (R->Next)
				{
					R->Next->Prev = NULL;
					SFirst = R->Next;
				}
				else
				{
					SFirst = SLast = NULL;
				}
			}
			
			// Destroy the node
			
			delete (R);
			
			// Terminate loop
			
			R = NULL;
		}
		else R = R->Next;
	}
	
	// Decrease reference-count
	
	--SCount_;
	
	// Unlock resources
	
	Lock->UnlockOnce();
}


