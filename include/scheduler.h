#ifndef _SCHEDULER_H
#define _SCHEDULER_H 1

#include "itc.h"

//==========================================================================================
// Scheduler
//
// This class will be used by the "collector" thread connected to every mailbox. Repeating events,
// like an automatic mail check, are scheduled here. Future events can be scheduled once, or as
// repeating. Events are stored as ITCMessages, allowing for the same handler to be used.
//==========================================================================================

class Scheduler
{
	public:
							 Scheduler (void);
							~Scheduler (void);
		
		void				 Schedule (int, int, unsigned long, unsigned long, WordList *);
		void				 Schedule (int, int, unsigned long, unsigned long, char *);
		void				 Schedule (int, int, unsigned long, unsigned long, char *, char *);
		void				 Schedule (int, int, unsigned long, unsigned long, char *, char *, char *);
		ITCMessage			*CheckSchedule (void);
		
		int				 	 Queue (void);
		ITCMessage			*Get (int);
		void				 Remove (int);
	
	private:
		ASemaphore			*Lock;
		ITCMessage			*First, *Last;
		int					 Count;
};

#endif
