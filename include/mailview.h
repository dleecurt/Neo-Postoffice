#ifndef _MAILVIEW_H
#define _MAILVIEW_H 1

#include "mailmessage.h"
#include "mailbox.h"
#include "semaphore.h"

//==========================================================================================
// MailViewNode
//
// An entity that represents a message as part of a folder (as opposed to a message as part of a
// mailbox, which is where this class is aimed at).
//==========================================================================================

class MailViewNode
{
	public:
							 MailViewNode (class MailMessage *, class MailView *);
							~MailViewNode (void);
		
		MailViewNode		*Next, *Prev;
		
		class MailMessage	*Message;
		class MailView		*Parent;
};

struct MailViewMutation
{
	MailViewMutation 		*Next;
	int						 AffectedRecord;
};

//==========================================================================================
// MailView
//
// A folder, in the GUI, represents a certain point-of-view towards the MailBox it is a part of. All
// messages that are received from the handler are children of the MailBox. The MailViews create
// MailViewNodes of any messages that are added to the MailBox if they are of relevance (which is,
// the filter rules match).
//==========================================================================================

class MailView
{
	public:
							 MailView (class MailBox *, const char *);
							~MailView (void);
		
		MailView			*Next, *Prev;
		
		ASemaphore			*Lock;
		ASemaphore			*CacheLock;
		ASemaphore			*MutationLock;
		
		class MailBox		*Parent;
		char				*Name;
		
		int					 ProcessRules (class MailMessage *);
		int					 ProcessScore (class MailMessage *);
		void				 Associate (class MailMessage *);
		void				 Unassociate (class MailMessage *);

		int					 Count (void);
		MailMessage			*GetMessage (int);
		void				 DoneWithMessage (void);
		
		void				 ClearMutations (void);
		int				 	 GetNextMutation (void);

		int					 NewCount;
		
	private:
		int					_CachePos;
		MailViewNode		*Cache;
		
		MailViewNode		*First, *Last;
		int					_Count;
		MailViewMutation	*MFirst, *MLast;
		
		bool				 Biff;
};

#endif

