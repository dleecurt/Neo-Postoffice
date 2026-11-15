#ifndef _BTREE_H
#define _BTREE_H 1

#define BTREE_ROOT_KEY 0x80000000

#include <sys/types.h>
#include <stdio.h>

class BTreeNode
{
	public:
					     BTreeNode (const char *, BTreeNode *root=NULL);
					    ~BTreeNode (void);
		
        void             LinkTo (BTreeNode *);
		BTreeNode		*Find (const char *);

		char		    *BKey;
		unsigned int     BChecksum;

        BTreeNode       *BRoot, *BNext, *BPrev;
        BTreeNode       *BLower, *BHigher;
};

class BTree
{
    public:
						 BTree (void);
						~BTree (void);

		BTreeNode		*BRoot;
};

#endif

