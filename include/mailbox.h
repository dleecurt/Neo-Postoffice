#ifndef _MAILBOX_H
#define _MAILBOX_H 1

#include <string>
#include "wordlist.h"
#include "var.h"
#include "mailerthread.h"
#include "semaphore.h"
#include "socketclasses.h"
#include "mailmessage.h"
#include "mailview.h"
#include "mailhandler.h"
#include "itc.h"
#include "scheduler.h"
#include "mailstorage.h"

extern StorageList STORAGE;

#ifdef USE_GDBM
  #include <gdbm.h>
#endif

#define MBOX_MAXMEM (256*1024)

class MailBox
{
	public:
							 MailBox (ITCPipe *, char *);
							 MailBox (ITCPipe *); // Outbox
							~MailBox (void);

		void				 Deliver (MailMessage *);
		void				 UnTie (MailMessage *);
		int					 Have (char *);

		void				 CreateMailView (const char *);
		// For mailboxes whose folders come from the source (SQL, Maildir):
		// makes sure there is a folder taking exactly the messages whose
		// folder field is value. Returns true if it had to be created.
		bool				 EnsureFolder (const std::string &value);
		// True for folders EnsureFolder made (not the user's own filter folders).
		bool				 IsAutoFolder (const char *name);
		// Runs the "fetchcmd" setting (getmail, fetchmail) before a check.
		// automatic: from the check timer (skipped if one ran just before).
		void				 RunFetchCommand (bool automatic);
		std::string			 LastFetchError;
		time_t				 LastFetch = 0;
		// Copies or moves a message into a folder of a Maildir mailbox
		// (runs in this mailbox's collector thread).
		bool				 FetchRaw (const char *msgid, std::string &raw, std::string &flags, std::string &error);
		void				 RedirectMessage (const char *msgid, const char *from, const char *to);
		void				 TransferMessage (const char *msgid, const char *target, const char *folder,
											  bool move, bool last);
		void				 DestroyMailView (char *);
		void				 RenameMailView (char *, char *);
		
		void				 ClearBiff (void);
		
		MailView			*GetView (const char *);
		MailMessage			*Find (char *);
		void				 LoadMailBox();
		void				 SaveMailBox();
		void				 Quit();
		void				 ExpireLocal();
		
		void				 RemoveMessage (char *);
		void				 RemoveMessage (MailMessage *);

        void                 GrimReaper (void);

		int				 	 Open;
		
		class MailHandler	*Handler;
		class MailerThread	*Collector;
		
		ITCPipe				*ToCollector;
		ITCPipe				*ToUI;
		Scheduler			*Schedule;

		unsigned long		 MemoryUsage;
		int					 Count;
		int					 NewCount;
		char				*Name;
		char				*DirectoryPrefix;
		int					 CacheToDisk;

		bool				 Biff;

#ifdef USE_GDBM
		MailStorage			*DB;
#endif
				
		MailMessage			*Root;
		MailView			*VFirst, *VLast;

		ASemaphore			*Lock;
		ASemaphore			*VLock;
};

// Neo-PostOffice: a multipart/mixed body of M's parts and its Content-Type
// value (mailbox.cpp).
#include <string>
std::string mime_multipart_body (MailMessage *M, std::string &content_type);

#endif
