/*
$doc About Lists
		This file contains routines for managing linked lists.  Specifically,
	doubly-linked lists, since I usually find these more useful than
	singly-linked lists.
		These routines exist to facilitate easy list management by the programmer,
	no matter what sort of items are in the list.  The LISTS.H file defines a
	ListNode structure as follows:

		struct ListNode
			{
				struct ListNode *Next;
				struct ListNode *Previous;
			};

		On the surface this appears useless, but it really isn't.  Since all the
	list management routines needs is this short bit of data, you can define
	just about any kind of data object you like, yet still have the list be
	maintained by these list routines.  It's easy.  Suppose you want to track
	a list of names and addresses.  Simply define your data structure like so:

		struct DataItem
			{
				struct ListNode Node;
				char Name[20];
				char Address[40];
				char City[20];
				char State[3];
				char Zip[10];
				char Phone[13];
			};

		Then, even though the data object itself is actually quite a bit more
	than just a ListNode, you can still use the list management functions on it
	by simply calling (for example) AddTail((struct ListNode *)&DataItem);.  In
	this case, since the first member of the data structure is in fact a listnode,
	the list functions will work just fine.
		In fact, as long as the first two members of your structure are
	pointers to next and previous (respectively), they will work without
	even having a ListNode defined.
	    For an easier (IMO) way to do it, check out CreateNode();
		The reason for using these list routines is that everything done with lists
	can be dynamic.  There are no arbitrary limits set at compile time, and no worries
	about limited static storage space, since lists can be allocated from the heap
	as needed.
$end
*/

#include <rcl/library/lists.h>

/*
$doc AddHead()
	Syntax:     RootNode = AddHead(List, Node);

	Inputs:     struct ListNode *List;      Any existing node in the list.
				struct ListNode *Node;      The Node to add as the head of the list.

	Synopsis:   This routine inserts the specified node at the head of linked
				list which contains List.  (List may be any node in the list,
				as this routine will automatically rewind the list.)

	Returns:    Returns a pointer to the Root Node of the list.  (This should
				be equal to the specified Node.)

	Notes:      If the list pointer is NULL, then the node pointer is returned.
$end
*/

RCLDLL void * AddHead(void *IList, void *INode)
{
	struct ListNode *temp;
	struct ListNode *List, *Node;

	List = (struct ListNode *)IList;
	Node = (struct ListNode *)INode;
	temp = List;
	if(temp)
		{
			if(Node && (!Node->Next && !Node->Previous))    /* node can only be on one list at a time */
				{
					while(temp->Previous)
						temp = temp->Previous;      /* rewind to top  */
					Node->Previous = NULL;          /* insert Node */
					Node->Next = temp;
					temp->Previous = Node;
					temp = Node;                    /* change pointer to new head */
				}
		}
	else
		temp = Node;
	return(temp);                           /* return pointer to head */
}

/*
$doc AddTail()
	Syntax:     RootNode = AddTail(List,Node);

	Inputs:     struct ListNode *List;      Any existing node in the desired list.
				struct Node *Node;          Node you want added as tail.

	Synopsis:   This function inserts the specified Node as the tail of
				the specified List.

	Returns:    Pointer to the Root node (head) of the List.

	Notes:      If the list pointer is NULL, then the node pointer is returned.
$end
*/

RCLDLL void * AddTail(void *IList, void *INode)
{
	struct ListNode *temp;
	struct ListNode *List, *Node;

	List = (struct ListNode *)IList;
	Node = (struct ListNode *)INode;
	temp = List;
	if(temp)
		{
			if(Node && (!Node->Next && !Node->Previous))    /* node can only be on one list at a time */
				{
					while(temp->Next)           /* find last node in void */
						temp = temp->Next;
					temp->Next = Node;          /* insert node as tail      */
					Node->Previous = temp;
					Node->Next = NULL;
					while(temp->Previous)       /* find first (Root) node in list  */
						temp = temp->Previous;  /* set to return Root of void */
				}
			else
				temp = NULL;
		}
	else
		temp = Node;
	return(temp);
}

/*
$doc RemHead()
	Syntax:     RootNode = RemHead(List);

	Inputs:     struct ListNode *List;      Any node in list to remove head of.

	Synopsis:   This routine will first find the first (root) node in the list
				specified, then remove it from the list.

	Returns:    Pointer to the NEW root node of the list, or NULL if there are
				no more nodes in the list.

	Notes:      None.
$end
*/

RCLDLL void * RemHead(void *Ilist)
{
	struct ListNode *temp, *newhead = NULL;
	struct ListNode *list;

	list = (struct ListNode *)Ilist;
	temp = list;
	if(temp)
		{
			while(temp->Previous)       /* find root of void */
				temp = temp->Previous;
			if(temp->Next)
				{
					temp->Next->Previous = NULL;    /* break link to next one */
					newhead = temp->Next;           /* identify new root node */
				}
			temp->Next = NULL;          /* break link on removed node */
			temp = newhead;             /* set to return new root     */
		}
	return(temp);
}

/*
$doc RemTail()
	Syntax:     RootNode = RemTail(List);

	Inputs:     struct ListNode *List;      Any node in list to remove tail of.

	Synopsis:   This routine will first find the last (tail) node in the list
				specified, then remove it from the list.

	Returns:    Pointer to the root node of the list, or NULL if there are
				no more nodes in the list.

	Notes:      None.
$end
*/

RCLDLL void * RemTail(void *Ilist)
{
	struct ListNode *temp,*temp2=NULL;
	struct ListNode *list;

	list = (struct ListNode *)Ilist;
	temp = list;
	if(temp)
		{
			while(temp->Next)
				temp = temp->Next;                  /* find last node */
			if(temp->Previous)
				temp->Previous->Next = NULL;        /* break link to previous node */
			temp2 = temp->Previous;                 /* identify previous */
			temp2->Previous = NULL;                 /* unlink node */
			if(temp2)                               /* if there was a previous node */
				while(temp2->Previous)              /* find the head of the list    */
					temp2 = temp2->Previous;
		}
	return(temp2);                                  /* return head of void */
}

/*
$doc InsertNode()
	Syntax:     RootNode = InsertNode(InsertAfter,Node);

	Inputs:     struct ListNode *InsertAfter;       Existing list node to insert after.
				struct ListNode *Node;              the node to be inserted.

	Synopsis:   The routine inserts the specified node into the existing
				list following the node identified as InsertAfter.

	Returns:    Pointer to the head of the list or NULL if either parameter
				is NULL.

	Notes:      None.
$end
*/

RCLDLL void * InsertNode(void *IInsertAfter, void *INode)
{
	struct ListNode *temp;
	struct ListNode *InsertAfter, *Node;

	InsertAfter = (struct ListNode *)IInsertAfter;
	Node = (struct ListNode *)INode;
	temp = InsertAfter;
	if(temp)
		if(Node && (!Node->Previous && !Node->Next))
			{
				if(temp->Next)                      /* link Next node to this one */
					{
						Node->Next = temp->Next;
						temp->Next->Previous = Node;
					}
				temp->Next = Node;                  /* link to temp (InsertAfter) node */
				Node->Previous = temp;
				while(temp->Previous)               /* find root */
					temp = temp->Previous;          /* and set to return it */
			}
	return(temp);
}

/*
$doc RemoveNode()
	Syntax:     RootNode = RemoveNode(Node);

	Inputs:     struct ListNode *Node;      Node to remove from list.

	Synopsis:   Removes the specified Node from its list.

	Returns:    Pointer to the root node of the list, or NULL if there are
				no more nodes in the list.

	Notes:      None.
$end
*/

RCLDLL void * RemoveNode(void *INode)
{
	struct ListNode *temp, *temp2 = NULL;
	struct ListNode *Node;

	Node = (struct ListNode *)INode;
	temp = Node;
	if(temp)
		{
			if(temp->Previous)                  /* unlink from list first */
				{
					temp->Previous->Next = temp->Next;
					temp2 = temp->Previous;     /* identify previous node */
				}
			if(temp->Next)
				{
					temp->Next->Previous = temp->Previous;
					if(!temp2)
						temp2 = temp->Next;
				}
			temp->Next = temp->Previous = NULL; /* remove link pointers   */
			if(temp2)                           /* if there was a previous node */
				while(temp2->Previous)          /* find the root node           */
					temp2 = temp2->Previous;
		}
	return(temp2);
}

/*
$doc FindHead()
	Syntax:     Root = FindHead(Node);

	Inputs:     struct ListNode *Node;          Any node in the target list.

	Synopsis:   Searches back through the list to find the first (root) node.

	Returns:    Pointer to the root node of the list (returns NULL if the
				passed parameter is NULL).

	Notes:      If the specified node is the only one in the list, it will
				be returned as being the root node.
$end
*/

RCLDLL void * FindHead(void *INode)
{
	struct ListNode *Node;

	Node = (struct ListNode *)INode;
	if(Node)
		while(Node->Previous)
			Node = Node->Previous;
	return(Node);
}

/*
$doc FindTail()
	Syntax:     Tail = FindTail(Node);

	Inputs:     struct ListNode *Node;          Any node in the target list.

	Synopsis:   Searches forward through the list to find the last (tail) node.

	Returns:    Pointer to the tail node of the list (returns NULL if the
				passed parameter is NULL).

	Notes:      If the specified node is the only one in the list, it will
				be returned as being the tail node.
$end
*/

RCLDLL void * FindTail(void *INode)
{
	struct ListNode *Node;

	Node = (struct ListNode *)INode;
	if(Node)
		while(Node->Next)
			Node = Node->Next;
	return(Node);
}

/*
$doc CountList()
	Syntax:     NumItems = CountList(listnode);

	Inputs:     struct ListNode *ListNode;  any node in the list to count.

	Synopsis:   This function counts the number of items in a list.

	Returns:    The total number of items in the list (integer).

	Notes:      If there are more than 32,767 items in the list, the count
				returned will not be accurate.  
$end
*/

RCLDLL short CountList(void far *INode)
{
	struct ListNode far *Node;
	short count = 0;

	Node = (ListNode *)FindHead(INode);
	if(Node)
		{
			while(Node)
				{
					count++;
					Node = (struct ListNode far *)Node->Next;
				}
			return (count);
		}
	return 0;
}

/*
$doc CreateNode()
	Syntax:     node = CreateNode(size);

	Inputs:     unsigned short size;      size of UserData pointer (see below).

	Synopsis:   This routine allocates and clears a GenericListNode as
				defined in the lists.h header file.  It then allocates
				a block of memory of size bytes and points the
				GenericListNode->UserData to this block.  In this way, you
				can easily wrap a list around any desired data object, and
				then use the list management routines to manipulate it.

	Returns:    Pointer to the created GenericListNode, or NULL if one
				could not be allocated.

	Notes:      Memory for the node and its UserData are allocated in a
				single block (to reduce fragmentation).  Therefore, unless
				you really know what you're doing, it is not recommended that
				you try to cast the returned pointer to some other type.
				However, you can (and should) cast the UserData pointer to
				anything you like.
				   Also note that size should be less than 65524 (since this
				plus sizeof(struct GenericListNode) would roll over the size
				of a 16-bit integer...).

	See Also:   DisposeNode().
$end
*/

RCLDLL struct GenericListNode * CreateNode(unsigned short size)
{
	struct GenericListNode *Node;

	Node = (struct GenericListNode *)malloc(sizeof(struct GenericListNode)+size);
	if(Node)
		{
			memset(Node,0,sizeof(struct GenericListNode)+size);
			Node->NodeSize = size;
			Node->UserData = (char *)Node + sizeof(struct GenericListNode);
			Node->Next = Node->Previous = NULL;
		}
	return(Node);
}

/*
$doc DisposeNode()
	Syntax:     DisposeNode(node);

	Inputs:     struct GenericListNode *node;       Node to dispose of

	Synopsis:   This routine will first remove the node from any list that
				it happens to be in, then it will free all memory associated
				with the GenericListNode.

	Returns:    None.

	Notes:      None.
$end
*/

RCLDLL void DisposeNode(struct GenericListNode *node)
{
	RemoveNode(node);
	free(node);
}

/*
$doc DisposeList()
	Syntax:     DisposeList(node);

	Inputs:     struct GenericListNode *node;       Root Node of list to dispose of

	Synopsis:   This routine will free the entire list, including the
				specified root node.

	Returns:    None.

	Notes:      The specified node need not be the root.  It will be
				unlinked and everything from that node on will be
				freed if it is not the root.  It *is* safe to call this
				with a NULL pointer.
$end
*/

RCLDLL void DisposeList(struct GenericListNode *node)
{
	struct GenericListNode *next;

	if(node && node->Previous)
		node->Previous->Next = NULL;
	while(node)
		{
			next = node->Next;
			free(node);
			node = next;
		}
}

/*
$doc InsertNodeSorted()
	Syntax:     root = InsertNodeSorted(list,node,compare);

	Inputs:     struct ListNode *list;      list to insert into
				struct ListNode *node;      node to be inserted
				short (*compare)(node1, node2);   sorting function to use

	Synopsis:   This function will insert a node into a list in sorted
				order, based on whatever sorting criteria you want.

	Returns:    Pointer to the root node of the list (which may have
				changed if the new node became the root node).

	Notes:      The comparison function should be of the form
				short = compare(struct GenericListNode *, struct GenericListNode *);
				where the integer value returned describes the relationship
				of the two nodes in the same way a strcmp() function does.
				(That is, return negative if node1 is less than node2, zero if the
				two nodes are equal, or positive if node1 is greater than node2.)
$end
*/

RCLDLL struct GenericListNode * InsertNodeSorted(struct GenericListNode *list, struct GenericListNode *node, short (*CompareNodes)(struct GenericListNode *node1,struct GenericListNode *node2))
{
	while(list)
		if(CompareNodes(list,node) <= 0) /* keep looking until we find the spot to insert */
			{
				if(!list->Next)
					{
						AddTail(list,node);
						break;
					}
				else
					list = list->Next;
			}
		else                            /* insert and quit */
			{
				if(list->Previous)
					InsertNode(list->Previous,node);
				else
					list = AddHead(list,node);
				break;
			}
	return(FindHead(node));
}

/*
$doc DuplicateList()
	Syntax:     newlist = DuplicateList(list);

	Inputs:     struct GenericListNode *list;   list to copy

	Synopsis:   Makes a duplicate of the list and associated
				data.  Returns a pointer to this new list.

	Returns:    Pointer to the newly created list, or NULL if no
				list could not be created.

	Notes:      This ONLY works on real GenericListNodes!  Unlike
				the AddTail() and similar functions, this one requires
				all the info that is in a GenericListNode.  So only
				real nodes will work with this!
$end
*/

RCLDLL struct GenericListNode * DuplicateList(struct GenericListNode *list)
{
	struct GenericListNode *newlist = NULL, *newnode;
	list = FindHead(list);
	while(list)
		{
			newnode = CreateNode(list->NodeSize);
			if(newnode)
				{
					if(list->UserData)
						memcpy(newnode->UserData,list->UserData,list->NodeSize);
					else
						newnode->UserData = NULL;
					if(newlist)
						AddTail(newlist,newnode);
					else
						newlist = newnode;
				}
			else
				{
					if(newlist)
						DisposeList(newlist);
					newlist = NULL;
				}
			list = list->Next;
		}
	return(newlist);
}

RCLDLL struct GenericListNode * AddLists(struct GenericListNode *list1, struct GenericListNode *list2)
{
	if(!list1)		// if only second list, just use it.
		list1 = list2;
	else if(!list2)	// if only first list, just use it
		;
	else
		{
			struct GenericListNode *root = list2;
			while(root)
				{
					list2 = RemHead(list2);
					AddTail(list1, root);
					root = list2;
				}
		}
	return(list1);
}
