#ifndef _VIEWLIST_H
#define _VIEWLIST_H 1

#include "mailview.h"
#include "mailmessage.h"

//==========================================================================================
// ViewList/ViewListNode
//
// This is a child of the MailMessage. It is a pointer to a *subscriber* of the particular MailMessage.
// This class is the twin brother of the MailViewNode.
//==========================================================================================

class ViewListNode
{
	public:
						 ViewListNode (MailViewNode *);
						~ViewListNode (void);
		
		ViewListNode	*Next, *Prev;
		
		MailViewNode	*ViewNode;
		MailView		*View;
};

class ViewList
{
	public:
						 ViewList (MailMessage *);
						~ViewList (void);
		
		void			 Subscribe (MailViewNode *);
		void			 Unsubscribe (MailViewNode *);
		void			 UnTie (void);
		int				 Count (void);
		ViewListNode	*Get (int);
		
	private:
		MailMessage		*Parent;
		ViewListNode	*First, *Last;
		int				_Count;
};

#endif
