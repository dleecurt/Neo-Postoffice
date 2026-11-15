#ifndef _CCACHE_H
#define _CCACHE_H 1

#include "wordlist.h"
#include "FL/Fl_Toggle_Node_Base.H"
#include "mailbox.h"
#include "btree.h"

char *stripre (char *);

class CCacheNode : public BTreeNode
{
	public:
						 CCacheNode (const char *, CCacheNode *N = NULL, MailMessage *M = NULL, Fl_Toggle_Node_Base *No = NULL);
						~CCacheNode (void);

       	MailMessage		*msg;
		Fl_Toggle_Node_Base			*node;
};

class CCache
{
	public:
						 CCache (void);
						~CCache (void);

		void			 Add (MailMessage *, Fl_Toggle_Node_Base *);
		CCacheNode		*FindMsgid (char *);
		CCacheNode		*FindMsgid (WordList *);
		CCacheNode		*FindSubject (char *);
		CCacheNode		*FindNode (Fl_Toggle_Node_Base *);
		void			 Clear (void);

	private:
		CCacheNode		*MsgRoot, *SubjRoot;
};

#endif
