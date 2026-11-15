/**************************************************************************/
/*                              LISTS.H                                   */
/**************************************************************************/
/*  This file describes the function prototypes and data structures used  */
/*  by the list management functions.                                     */
/**************************************************************************/

#ifndef LISTS_H

#define LISTS_H 1

#include <memory.h>
#include <stdlib.h>
#include <rcl/library/vartypes.h>


/**************************************************************************/
/*                              ListNode                                  */
/**************************************************************************/
/*  This structure is the basic form of a doubly-linked list node.  Note  */
/*  that while this structure alone is not useful at all, it becomes      */
/*  fairly powerful when you consider that you can have a linked list of  */
/*  any sort you desire, so long as its first member is a ListNode.  In   */
/*  this way, you can create linked lists of anything you want, yet still */
/*  leave the list management up to these routines...                     */
/*     The GenericListNode which follows is that which is obtained by     */
/*  calls to CreateNode().                                                */
/**************************************************************************/

struct ListNode
	{
		struct ListNode *Next;
		struct ListNode *Previous;
	};

struct GenericListNode
	{
		struct GenericListNode *Next;
		struct GenericListNode *Previous;
		unsigned short NodeSize;
		void *UserData;
	};

typedef struct GenericListNode LIST;

RCLDLL void * AddHead(void *List, void *Node);
RCLDLL void * AddTail(void *List, void *Node);
RCLDLL void * RemHead(void *Node);
RCLDLL void * RemTail(void *Node);
RCLDLL void * InsertNode(void *InsertAfter, void *Node);
RCLDLL void * RemoveNode(void *Node);
RCLDLL void * FindHead(void *Node);
RCLDLL void * FindTail(void *Node);
RCLDLL struct GenericListNode * CreateNode(unsigned short size);
RCLDLL void DisposeNode(struct GenericListNode *Node);
RCLDLL void DisposeList(struct GenericListNode *Root);
RCLDLL short CountList(void *Node);
RCLDLL struct GenericListNode * InsertNodeSorted(struct GenericListNode *List, struct GenericListNode *Node, short (*Compare)(struct GenericListNode *node1, struct GenericListNode *node2));
RCLDLL struct GenericListNode * DuplicateList(struct GenericListNode *list);

inline LIST * AddHead(LIST *List, LIST *Node)      { return((LIST *)AddHead((void *)List, (void *)Node)); }
inline LIST * AddTail(LIST *List, LIST *Node)      { return((LIST *)AddTail((void *)List, (void *)Node)); }
inline LIST * RemHead(LIST *Node)                  { return((LIST *)RemHead((void*)Node)); }
inline LIST * RemTail(LIST *Node)                  { return((LIST *)RemTail((void*)Node)); }
inline LIST * InsertNode(LIST *InsertAfter, LIST *Node)   { return((LIST *)InsertNode((void *)InsertAfter, (void *)Node)); }
inline LIST * RemoveNode(LIST *Node)               { return((LIST *)RemoveNode((void*)Node)); }
inline LIST * FindHead(LIST *Node)                 { return((LIST *)FindHead((void*)Node)); }
inline LIST * FindTail(LIST *Node)                 { return((LIST *)FindTail((void*)Node)); }
inline short  CountList(LIST *Node)                   { return(CountList((void*)Node)); }
RCLDLL LIST * AddLists(LIST *list1, LIST *list2);


#endif
