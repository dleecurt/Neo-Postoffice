#ifndef _ITC_H
#define _ITC_H 1

#include "wordlist.h"
#include "semaphore.h"

//==========================================================================================
// ITCMessage/ITCPipe
//
// Relatively simple FIFO buffers to be used for communication between the GUI thread and the
// backend threads.
//==========================================================================================

// defines for ui-to-collector communication

#define CLASS_CHECKGROUP	0x1001 // [1] arg1 = groupname
#define CLASS_GETBODY		0x1002 // [1] arg1 = intmsgid
#define CLASS_DELETE		0x1003 // [1] arg1 = intmsgid
#define CLASS_MOVE			0x1004 // [2] arg1 = intmsgid    arg2 = target folder name
#define CLASS_OPENSERVER	0x1005 // [0]
#define CLASS_CLOSESERVER	0x1006 // [0]
#define CLASS_EXPIRE		0x1007 // [0]
#define CLASS_MARKREAD		0x1008 // [1] arg1 = intmsgid (opened from the local copy)
#define CLASS_SETFLAG		0x1009 // [2] arg1 = intmsgid     arg2 = "+F" / "-F" (Maildir flag letter)
#define CLASS_TRANSFER		0x100A // [5] msgid, target mailbox, target folder, "move"/"copy", "1" if last
#define CLASS_FOLDEROP		0x100B // [3] "create"/"rename"/"delete", name, new name
#define CLASS_URGENT		0x8000
#define CLASS_DIE			0x8008 // [0]
#define CLASS_STOP			0x8001 // [0]

// defines for collector-to-ui communication

#define CLASS_CHECKEDGROUP	0x2001 // [1] arg1 = groupname
#define CLASS_NEWARTICLE	0x2002 // [2] arg1 = groupname   arg2 = intmsgid
#define CLASS_REMARTICLE	0x2003 // [2] arg1 = groupname   arg2 = intmsgid
#define CLASS_ERROR			0x2004 // [2] arg1 = subsystem   arg2 = error msg
#define CLASS_STATUS		0x2005 // [1] arg1 = status msg
#define CLASS_GOTBODY		0x2006 // [1] arg1 = msgid
#define CLASS_NEWFOLDER		0x2007 // [2] arg1 = mailbox     arg2 = "" (folders or counts changed: redraw the tree)
#define CLASS_GONE			0x2008 // [2] arg1 = mailbox     arg2 = intmsgid (deleted on the server)
#define CLASS_FOLDERGONE	0x2009 // [2] arg1 = mailbox     arg2 = foldername (removed at the source)
#define CLASS_FOLDERLIST	0x200A // [1+3n] arg1 = mailbox, then per folder: "1"/"0" subscribed, name here, server name

// defines for scheduler events

#define SCHED_SAVEBOX		0x3001
#define SCHED_CHECKNEW		0x3002
#define SCHED_EXPIRE		0x3003
#define SCHED_CLEANGARBAGE	0x3004



#define SUB_ERROR_CRITICAL  0x2001 // critical error message
#define SUB_ERROR_LIMIT		0x2002 // operation cannot be performed due to limitations of the underlying protocol
#define SUB_ERROR_INFORM	0x2003 // informative message about a non-critical error event

// global defines

#define NO_SUBCLASS			0x0000 // placeholder
#define SUB_CHECK_NOFETCH	0x1001 // CLASS_CHECKGROUP: rescan only, without the mailbox's fetch command

class ITCMessage
{
	public:
						 ITCMessage (unsigned int, unsigned int, WordList *);
						~ITCMessage (void);
		
		ITCMessage		*Next, *Prev;
		unsigned int	 Class;
		unsigned int	 Subclass;
		time_t			 Scheduled;
		int				 Reschedule;
		WordList		*Message;
};

class ITCPipe
{
	public:
						 ITCPipe (void);
						~ITCPipe (void);
		
		ITCMessage		*GetNextMessage (void);
		ITCMessage		*GetNextMessage (unsigned int);
		void			 PostMessage (unsigned int, unsigned int, WordList *);
		void			 PostMessage (unsigned int, unsigned int, const char *);
		void			 PostMessage (unsigned int, unsigned int, const char *, const char *);
		void			 PostMessage (unsigned int, unsigned int, const char *, const char *, const char *);
		int				 Queue (void);

		void			 AddPriorityClass (unsigned int);
		
		bool			 UrgentPending;
		bool			 PriorityPending;
	
	private:
		unsigned int	 PriorityClasses[32];
		int				 NumPriorityClasses;
		ASemaphore		*Lock;
		ITCMessage		*First, *Last;
		int				 Count;
};

#endif
