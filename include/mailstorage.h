#ifndef _MAILSTORAGE_H
#define _MAILSTORAGE_H 1

#include <memory>
#include <string>
#include <vector>
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
#define STORAGE_PGSQL	0x0004

// One message's entry in a mailbox index: where it is filed and its envelope.
struct IndexEntry
{
	std::string					MsgID, ParentID, MailFrom, NameFrom, Subject, Date;
	std::vector<std::string>	Folders;
	int							Flags = 0, Read = 0, Count = 0;
	long						Received = 0;
};

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

		// Storage that also keeps the mailbox index (folders, read state) itself.
		// The default, used by GDBM, leaves the index in the mailbox's index file.
		virtual bool	 KeepsMailboxIndex (void) { return false; }
		virtual bool	 LoadMailboxIndex (std::vector<IndexEntry> &) { return false; }
		virtual bool	 SaveMailboxIndex (const std::vector<IndexEntry> &) { return false; }
		virtual void	 ForgetMessage (const char *) {}
		virtual bool	 Usable (void) { return true; }
		virtual std::string LastError (void) { return std::string(); }

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

		// Neo-PostOffice: removing a message from the mbox file itself.
		bool			 Expunge (const char *msgid, std::string &error, bool wait = true);
		void			 RetryPendingDeletes (void);
		bool			 FindRecord (const char *msgid, long &start, long &size);
		bool			 RecordMatches (long start, long size, const char *msgid);
		bool			 DotLock (std::string &error, bool wait);
		void			 DotUnlock (void);
		bool			 ReadOnly;		// opened read-only: deleting only hides
		bool			 HaveDotLock;
		
		ASemaphore		*Lock;
		char			*Filename;
		FILE			*DB;		
		time_t			 lastsync;
		
		long			 end_from;
		size_t			 end_size;
		
		WordList		 FreeList; // Store Deleted items
		struct flock	 FLock;
};

namespace podb { class Connection; }

// Neo-PostOffice: keeps a mailbox's messages and index in PostgreSQL.
class PGStorage : public MailStorage
{
	public:
						 PGStorage (const char *name, const char *mailbox, const char *conninfo);
						~PGStorage (void);
		void			 Store (class MailMessage *) override;
		bool			 Retrieve (class MailMessage *) override;
		void			 Delete (const char *) override;
		void			 Delete (StorageNode *) override;
		void			 LoadIndex (void) override;
		void			 StoreIndex (bool force=false) override;

		bool			 KeepsMailboxIndex (void) override { return true; }
		bool			 LoadMailboxIndex (std::vector<IndexEntry> &) override;
		bool			 SaveMailboxIndex (const std::vector<IndexEntry> &) override;
		void			 ForgetMessage (const char *) override;
		bool			 Usable (void) override;
		std::string		 LastError (void) override;

	private:
		bool			 Check (bool ok, const char *what);
		bool			 EnsureSchema (void);

		std::unique_ptr<podb::Connection>	Conn;
		std::string							Mailbox;
		std::string							Error;
		bool								Ready;
};

class StorageList
{
	public:
						 StorageList (void);
						~StorageList (void);

		MailStorage		*Open (const char *, class MailBox *, short,
								   const char *mailbox = NULL, const char *conninfo = NULL);
		void			 Close (class MailBox *);

	protected:
		MailStorage		*First, *Last;
		MailStorage		*Find (const char *);
		ASemaphore		*Lock;
};

#endif

