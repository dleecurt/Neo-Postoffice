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
// IndexTreeNode
// -------------
// This class is intended for storing a table of data with loose hierarchical
// relationships in a tree structure. It has to obide to the following design
// criteria:
//
// 1 * Nodes of the tree should be structured in the same order regardless of the
//     chronological order in which they were added to the tree.
// 2 * Nodes must be able to calculate their own vertical positions in a graphical
//     representation of the tree.
// 3 * The tree must be able to re-sort itself without any further information.
// 4 * The resulting interface must be as easy to implement as any other
//     BtreeNode deriviate.
//
// The link relationships inside a node can be a bit confusing for the untrained
// eye. We must destinguish two levels of linkage. At the first level, we have
// the flat "database", represented within BTree space. In an entirely different
// system, the nodes are tied together in hierarchical fashion. Thanks to the
// flexibility of the BTreeNode system, we can act as if it wasn't there (except
// for the occasion of destruction, where we have to follow the BTreeNode's
// behaviour of considering the construction of the root node as a global
// happening).
//===============================================================================

#include "indextree.h"
#include "strdupp.h"

//===============================================================================
// CONSTRUCTOR	IndexTreeNode
//
// SYNOPSIS		Link the node into the hierarchy of the root node, or
//				alternatively set things up to become a rootnode..
//
// ARGUMENTS	sortkey		(int)		The column number that should be looked
//										at when inserting ourselves in a parent's
//										hierarchy. If less than 0, chronological
//										ordering is performed instead.
//				fwsort		(bool)		If true, sort a-z, if false, sort z-a.
//				key			(char *)	The key of this node
//				parent_key	(char *)	The key of the parent node, NULL if no
//										parent is defined.
//				fields		(WordList*)	All of the columns of this row.
//				root		(Node *)	This hierarchy's root node, or NULL if
//										this node is to be the root node.
//===============================================================================

IndexTreeNode::IndexTreeNode (int			 sortkey,
							  bool			 fwsort,
							  const char 	*key,
							  const char 	*parent_key,
							  WordList   	*fields,
							  IndexTreeNode	*root) : BTreeNode (key, root)
{
	if (root) // This is not to be itself the root node
	{
		IndexTreeNode *ParentNode = NULL;
		
		TSequence = 0; // This will cheat SnuggleInto() to take a shortcut
						// if chronological sorting is selected. During
						// construction we can safely rely on the parent
						// node's TLast pointer (or TFirst) as a point of
						// reference.
		
		if (parent_key)
		{
			ParentNode = (IndexTreeNode *) root->BFind (parent_key);
		}
		if (!ParentNode) ParentNode = root; // Fixme? In Fl_IndexView context we would
											// want to merge subjects.
		
		IndexTreeNode *N = root->First;
		IndexTreeNode *NN;
		SortKey = sortkey;
		FWSort = fwsort;
		
		while (N)
		{
			NN = N->TNext;
			
			if (strcasecmp (N->TParentKey, key) == 0)
			{
				N->UnlinkFromParent();
				N->SnuggleInto(this);
			}
			
			N = NN;
		}
				
		SnuggleInto (ParentNode, sortkey, fwsort);
	
		TSequence = ++root->TSequence; // Get the next sequence number
	}
	else
	{
		TParent = NULL;
		TNext = TPrev = TFirst = TLast = NULL;
		TParentKey = NULL;
		TFields = new WordList;
		TSequence = 0;
		Position = -1;
		SortKey = sortkey;
		FWSort = fwsort;
	}
	TOpen = true;
}

//===============================================================================
// METHOD		SnuggleInto
// SYNOPSIS		Add the node to a root hierarchy
//
// ARGUMENTS    ParentNode  (IndexNod)	The hierarchy's root node
//
// RETURNS		method returns no data
//===============================================================================

void IndexTreeNode::SnuggleInto (IndexTreeNode *ParentNode)
{
	TParent = ParentNode;
	
	if (!ParentNode->TLast) // If we're the first child, no sorting work is needed
	{
		TPrev = TNext = NULL;
		ParentNode->TLast = ParentNode->TFirst = this;
	}
	else // We are not that fortunate
	{
		if (SortKey<0) // we need to sort by sequence number
		{
			if (!TSequence) // construction-time chrono-insert, use shortcut
			{
				if (FWSort) // forward chronological sort
				{
					TNext = NULL;
				  
   					TPrev = ParentNode->TLast;
					ParentNode->TLast->TNext = this;
					ParentNode->TLast = this;
				}
				else // backward chronological sort
				{
					TPrev = NULL;
				  
					TNext = ParentNode->TFirst;
					ParentNode->TFirst->TPrev = this;
					ParentNode->TFirst = this;
				}
			}
			else // Called through Resort(), do it the hard way
			{
				IndexTreeNode *N;
				
				N = ParentNode->TFirst;
				while (N)
				{
					if ((FWSort ? (N->TSequence > TSequence) : (N->TSequence < TSequence)))
					{
						TPrev = N->TPrev;
						TNext = N;
						N->TPrev = this;
						if (TPrev)
						{
							TPrev->TNext = this;
						}
						else
						{
							ParentNode->TFirst = this;
						}
						N = NULL;
					}
					else
					{
						if (!N->TNext)
						{
							TPrev = N;
							TNext = NULL;
							ParentNode->TLast
						}
						N = N->TNext;
					}
				} // endwhile (N)
			} // endif/else (!tsequence)
		}
		else // sortkey >=0, time to bring out strcasecmp
		{
   			IndexTreeNode *N;
   			
   			N = ParentNode->TFirst;
   			while (N)
   			{
   				if ((FSWort ? (strcasecmp (N->TFields->Get(SortKey),TFields->Get(SortKey)) >= 0) :
							  (strcasecmp (N->TFields->Get(SortKey),TFields->Get(SortKey)) <  0)))
   				{
   					TPrev = N->TPrev;
   					TNext = N;
   					N->TPrev = this;
   					if (TPrev)
   					{
   						TPrev->TNext = this;
   					}
   					else
   					{
   						ParentNode->TFirst = this;
   					}
   					N = NULL;
   				}
   				else
   				{
   					if (!N->TNext)
   					{
   						TPrev = N;
   						TNext = NULL;
   						ParentNode->TLast
   					}
   					N = N->TNext;
   				}
   			} // endwhile (N)
		}
	} // endif/else (TParentNode->TLast)
}

//===============================================================================
// METHOD		UnlinkFromParent
// SYNOPSIS		Removed a node from it's parent
//
// ARGUMENTS    (none)
//
// RETURNS		method returns no data
//===============================================================================

void IndexTreeNode::UnlinkFromParent (void)
{
	if (TParent) // There must be a parent to link from
	{
		if (TNext) // are we not the last?
		{
			if (TPrev) // nor the first?
			{
				TPrev->TNext = TNext;
				TNext->TPrev = TPrev;
			}
			else // we are first node
			{
				TNext->TPrev = NULL;
				TParent->TFirst = TNext;
			}
		}
		else // we are last node
		{
			if (TPrev) // are we not the only node?
			{
				TPrev->TNext = NULL;
				TParent->TLast = TPrev;
			}
			else // we are the only node
			{
				TParent->TFirst = NULL;
				TParent->TLast = NULL;
			}
		}
	}
	else // fubar, whine.
	{
		eprintf ("[indextreenode] attempt to UnlinkFromParent($ROOT)\n");
	}
}

IndexTreeNode::~IndexTreeNode (void)
{
	if (TParent)
	{
		UnlinkFromParent();
		
		if (TLast)
		{
			IndexTreeNode *I, *NI;
			I = TFirst;
			while (I)
			{
				NI = I->TNext;
				I->UnlinkFromParent();
				I->SnuggleInto (TParent);
				I = NI;
			}
		}
	}
	delete (TFields);
	delete (TParentKey);
}

void IndexTreeNode::Resort (int sortkey, bool fwsort)
{
	IndexTreeNode *N, *NN, *FIN;
	
	SortKey = sortkey;
	FWSort = fwsort;

	FIN = TLast;
	N = TFirst;
	
	while (N)
	{
		NN = N->TNext;
		N->UnlinkFromParent();
		N->Resort (sortkey, fwsort);
		N->SnuggleInto (this);
		if (N == FIN) N = NULL;
		else N = NN;
	}
}

int IndexTreeNode::Calculate (int value)
{
    Position = value;
	int result = value;
	IndexTreeNode *N = TFirst;
	if (TOpen)
	{
		while (N)
		{
			result = N->Calculate (result+1);
			N = N->TNext;
		}
	}
	return result;
}

