#ifndef _MAILBOX_H
#define _MAILBOX_H 1

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

		void				 CreateMailView (char *);
		void				 DestroyMailView (char *);
		void				 RenameMailView (char *, char *);
		
		void				 ClearBiff (void);
		
		MailView			*GetView (char *);
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

#endif
