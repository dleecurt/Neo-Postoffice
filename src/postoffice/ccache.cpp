//

#include <string.h>
#include "ccache.h"
#include "checksum.h"

char *stripre (char *src)
{
	if (!src) return NULL;
	char *tmp = strchr (src, ':');
	if (!tmp) return src;
	if (!(*(tmp+1))) return (src);
	return ((int)(tmp-src)<5) ? tmp+2 : src;
}

CCacheNode::CCacheNode (const char *key, CCacheNode *Root, MailMessage *M, Fl_Toggle_Node_Base *N) : BTreeNode (key, Root)
{
	//Next = Prev = NULL;
	msg = M;
	node = N;
	//chk_msgid = M->BKey;
	//chk_subject = checksum (M->Envelope->Get (ENVELOPE_SUBJECT));
}

CCacheNode::~CCacheNode (void)
{
}

CCache::CCache (void)
{
	MsgRoot = new CCacheNode (NULL);
    SubjRoot = new CCacheNode (NULL);
}

CCache::~CCache (void)
{
	delete (MsgRoot);
    delete (SubjRoot);
}

void CCache::Clear (void)
{
    delete (MsgRoot);
    delete (SubjRoot);
    MsgRoot = new CCacheNode (NULL);
    SubjRoot = new CCacheNode (NULL);
}

void CCache::Add (MailMessage *M, Fl_Toggle_Node_Base *N)
{
	new CCacheNode (M->Envelope->Get (ENVELOPE_SUBJECT), SubjRoot, M,N);
	new CCacheNode (M->MsgID, MsgRoot, M, N);
}

CCacheNode *CCache::FindMsgid (char *msgid)
{
//	unsigned long	 chk;
//	CCacheNode		*N;
	
	if (msgid)
	{
        return (CCacheNode *) MsgRoot->Find (msgid);
	}
	return NULL;
}

CCacheNode *CCache::FindMsgid (WordList *M)
{
	CCacheNode		*N;
	int				 i;

	for (i=0;i<M->Count;++i)
    {
        if ((N = FindMsgid (M->Get(i))))
        {
            return N;
        }
    }
	
	return NULL;
}

CCacheNode *CCache::FindSubject (char *subjc)
{
	CCacheNode		*N;
	char			*subj = stripre (subjc);
	
	if (subj)
	{
        N = (CCacheNode *) SubjRoot->Find (subjc);
        return N;
	}
	return NULL;
}

CCacheNode *CCache::FindNode (Fl_Toggle_Node_Base *nod)
{
	CCacheNode *N = (CCacheNode *) MsgRoot->BNext;
	while (N && (N!=MsgRoot))
	{
		if (N->node == nod) return N;
		N = (CCacheNode *) N->BNext;
	}
	return NULL;
}

