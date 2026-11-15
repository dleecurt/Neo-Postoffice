#ifndef _MAILSTORAGE_H
#define _MAILSTORAGE_H 1

#include <sys/types.h>
#include <stdio.h>
#include <gdbm.h>
#include <fcntl.h>

#include "semaphore.h"
#include "buffer.h"
#include "btree.h"
#include "wordlist.h"

class StorageNode : public BTreeNode
{
	public:
						 StorageNode (class MailStorage *, const char *, size_t, int);
						~StorageNode (void);

		size_t			 Size;
		short			 Count;
		unsigned int	 ExtraKey;
};

class MailBoxRef
{
	public:
						 MailBoxRef (class MailBox *);

		MailBoxRef		*Next, *Prev;
		class MailBox	*Ref;
};

#define STORAGE_GDBM	0x0001
#define STORAGE_MAILDIR	0x0002
#define STORAGE_MBOX	0x0003

class MailStorage
{
	public:
						 MailStorage (void);
		virtual			~MailStorage (void);
		virtual void	 Store (class MailMessage *);
		virtual bool	 Retrieve (class MailMessage *);
		virtual void	 Delete (const char *);
		virtual void	 Delete (StorageNode *);
		virtual void	 LoadIndex (void);
		virtual void	 StoreIndex (bool force=false);

		void			 Limit (size_t);
		void			 Add (const char *, size_t, int, unsigned int xkey=0);
		void			 Remove (const char *);
		void			 Remove (StorageNode *);
		size_t			 Sizeof (const char *);
		bool			 Have (const char *);
		size_t			 Size (void);
		int				 Count (void);
		int				 SCount (void);
		void			 Subscribe (class MailBox *);
		void			 Unsubscribe (class MailBox *);
		
		StorageNode		*Find (const char *);
		StorageNode		*Find_ (const char *);

		MailStorage		*Next, *Prev;
		char			*Name;

		int				 SCount_;
		StorageNode		*Root;
		MailBoxRef		*SFirst, *SLast;
		size_t			 Size_;
		size_t			 Limit_;
		int				 Count_;

		ASemaphore		*Lock;
};

class GDBMStorage : public MailStorage
{
	public:
						 GDBMStorage (const char *);
						~GDBMStorage (void);
		void			 Store (class MailMessage *);
		bool			 Retrieve (class MailMessage *);
		void			 Delete (const char *);
		void			 Delete (StorageNode *);
		void			 LoadIndex (void);
		void			 StoreIndex (bool);
		
	protected:
        void             StoreNode (StorageNode *);

		ASemaphore		*Lock;
		char			*Filename;
		GDBM_FILE		 DB;
		time_t			 lastsync;
        AsyncBuffer     *BF;
};

class MBoxStorage : public MailStorage
{
	public:
						 MBoxStorage (const char *);
						~MBoxStorage (void);
		void			 Store (class MailMessage *);
		bool			 Retrieve (class MailMessage *);
		void			 Delete (const char *);
		void			 Delete (StorageNode *);
		void			 LoadIndex (void);
		void			 StoreIndex (bool);
		void			 CheckSync (void);		
		
	protected:

		void			 Synchronize (void);
		void			 LockR (void);
		void			 LockRW (void);
		void			 Unlock (void);
		
		ASemaphore		*Lock;
		char			*Filename;
		FILE			*DB;		
		time_t			 lastsync;
		
		long			 end_from;
		size_t			 end_size;
		
		WordList		 FreeList; // Store Deleted items
		struct flock	 FLock;
};

class StorageList
{
	public:
						 StorageList (void);
						~StorageList (void);

		MailStorage		*Open (const char *, class MailBox *, short);
		void			 Close (class MailBox *);

	protected:
		MailStorage		*First, *Last;
		MailStorage		*Find (const char *);
		ASemaphore		*Lock;
};

#endif

