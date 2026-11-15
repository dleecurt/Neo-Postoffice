//===============================================================================
// POSTOFFICE - X11 Office Communication Handler
// ---------------------------------------------
// All sourcecode, unless differently annotated, written by Pim van Riezen
// Sourcecode and resulting binaries are covered by the GNU Lesser General Public
// License (GNU LGPL). For more information and the literal text of the license
// agreement, check the Free Software Foundation's website on http://www.fsf.org/
// 
// Copyright (C) 1999 Mad Sciene Labs - Rotterdam, The Netherlands
//===============================================================================

//===============================================================================
// BTreeNode
// ---------
// This class implements a loosely coupled hashed binary tree table for variable
// table sizes with one key.
//
// Requirements:
//
// 1 * Efficiency must be spread between inserts and lookups. Delete operations
//     may take slightly longer.
// 2 * Class must be self-supportive. Subclasses of the class and users thereof
//     should not need to know about the inner workings of BTree.
//===============================================================================

#include "btree.h"
#include "strdupp.h"
#include "checksum.h"
#include "debug.h"

#include <string.h>
#include <stdlib.h>

//===============================================================================
// CONSTRUCTOR	BTreeNode
//
// SYNOPSIS		Creates a new node for the binary tree.
//
// ARGUMENTS	key			(char *)	The unique key to be used for indexing the
//										node in the tree. NULL means that this
//										is to be a root-node.
//				root		(BTreeNod)	Pointer to a root-node. Upon creation,
//										the node will link itself under the
//										root-node. If set to NULL, the node
//										itself will be created as a root
//===============================================================================

BTreeNode::BTreeNode (const char *key, BTreeNode *root)
{
	if ((key)&&(root)) // key and rootnode defined - we're not a rootnode
	{
		BKey = strdupp (key);
		BChecksum = checksum (key);
		BRoot = root;
		BLower = BHigher = NULL;

   		BPrev = BRoot->BPrev;
		BNext = BRoot;
		BPrev->BNext = this;
		BRoot->BPrev = this;
		
		LinkTo (root);
	}
	else // No key and/or no rootnode given, we assume the user intended this one as a root node
	{
		BKey = strdupp ("/");
		BChecksum = BTREE_ROOT_KEY;
		BRoot = NULL;
		BLower = BHigher = NULL;
		BPrev = BNext = this;
	}
}

//===============================================================================
// DESTRUCTOR	BTreeNode
// SYNOPSIS		If the node was a rootnode, deleting it will delete all the
//				children. If not, the children will be relinked to the rootnode..
//===============================================================================

BTreeNode::~BTreeNode (void)
{
	if (BNext)
	{
		BNext->BPrev = BPrev;
		BPrev->BNext = BNext;
	}
	if (BRoot)
	{
		BTreeNode *BParent = BRoot;
		bool done = false;

		// Find out what our Parent node is

		while ((!done) && (BParent))
		{
			if ((BParent->BLower == this) || (BParent->BHigher == this))
			{
				done = true;
			}
			else
			{
				if (BChecksum < BParent->BChecksum) BParent = BParent->BLower;
				else if (BChecksum > BParent->BChecksum) BParent = BParent->BHigher;
				else BParent = BParent->BLower;
			}
		}

		// No parent node? Weep.

		if (!BParent) // Can't happen situations rock :-)
		{
			eprintf ("<panic type=critical>btreenode orphaned in trail</panic>\n");
			exit (1);
		}

		// Untie ourselves from parental supervision

		if (BParent->BHigher == this) BParent->BHigher = NULL;
		else BParent->BLower = NULL;

		// Release our children into the big world

		if (BLower) BLower->LinkTo(BRoot);
		if (BHigher) BHigher->LinkTo(BRoot);
	}
	else // We're root - total destruction is imminent
	{
		if (BLower) // And that...
		{
			BLower->BRoot = NULL;
			delete (BLower);
		}
		if (BHigher) // And that!
		{
			BHigher->BRoot = NULL;
			delete (BHigher);
		}
	}
}

//===============================================================================
// METHOD		LinkTo
// SYNOPSIS		Add the node to a root hierarchy
//
// ARGUMENTS    root 		(BtreeNod)	The hierarchy's root node
//
// RETURNS		method returns no data
//===============================================================================

void BTreeNode::LinkTo(BTreeNode *root)
{
	BTreeNode *Crsr = BRoot = root;
	bool done = false;

	while (!done)
	{
		if (BChecksum < Crsr->BChecksum)
		{
			if (Crsr->BLower) Crsr = Crsr->BLower;
			else
			{
				Crsr->BLower = this;
				done = true;
			}
		}
		else if (BChecksum > Crsr->BChecksum)
		{
			if (Crsr->BHigher) Crsr = Crsr->BHigher;
			else
			{
				Crsr->BHigher = this;
				done = true;
			}
		}
		else
		{

			// If we have exactly the same key, this node must be assumed to be
			// replacing the old one. Rearrange the tree by deleting the old value
			// and starting all over.
			//
			// FIXME: This should elegantly replace the old node, there's no
			//        reason for this massive yoyoing with nodes. Footnote:
			//		  replacing will suck if we don't keep track of a parent.
			//		  Footfootnote: Keeping track of parents means more
			//		  ties to unstring with every fart you make.

			if (strcasecmp (Crsr->BKey, BKey) == 0)
			{
				delete (Crsr);
				Crsr = BRoot;
			}

			// Thank $DEITY, it's just a hash-collision (it seems that the hash
			// functions with a remarkably low collision-rate, though).

			else
			{
				if (Crsr->BLower)
				{
					Crsr = Crsr->BLower;
				}
				else
				{
					Crsr->BLower = this;
					done = true;
				}
			}
		}
	}
}

//===============================================================================
// METHOD		Find
// SYNOPSIS		Look up a key in the hierarchy
//
// ARGUMENTS    key 		(char *)	The key to be looked for
//
// RETURNS		A pointer to the corresponding BTreeNode, or NULL if not found
//===============================================================================

BTreeNode *BTreeNode::Find (const char *key)
{
	BTreeNode *Crsr = this;
	unsigned int chk = checksum (key);

	while (Crsr)
	{
		if ((Crsr->BChecksum == chk) && (!strcasecmp(Crsr->BKey, key)))
			return Crsr;

		if (chk < Crsr->BChecksum) Crsr = Crsr->BLower;
		else if (chk > Crsr->BChecksum) Crsr = Crsr->BHigher;
		else Crsr = Crsr->BLower;
	}

	return Crsr;
}
