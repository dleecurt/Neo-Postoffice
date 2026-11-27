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

#include "indexview.h"
#include <string.h>

//===============================================================================
// CONSTRUCTOR	Fl_IndexNode
// SYNOPSIS		Initializes a node in the tree view.
//
// ARGUMENTS	_View	(Fl_IndexView*)	The root view
//				_Parent (Fl_IndexNode*) The parent node (NULL if none)
//				id		(char*)			The key
//				contents(WordList*)		The column data
//===============================================================================

Fl_IndexNode::Fl_IndexNode (Fl_IndexView *_View, Fl_IndexNode *_Parent, const char *id,WordList *contents)
{
	Selected = false;
	Count = 0;
	Key = strdupp (id);
	CKey = checksum (Key);

	View = _View;

	Next = Prev = First = Last = NULL;
	Parent = _Parent;

	if (contents) Contents = new WordList (contents);
	else Contents = new WordList;
	Sort = -1;
}

//===============================================================================
// DESTRUCTOR	Fl_IndexNode
// SYNOPSIS		Frees up resources
//===============================================================================

Fl_IndexNode::~Fl_IndexNode (void)
{
	Fl_IndexNode *N, *NN;

	N = First;

	// Delete all children

	while (N)
	{
		NN = N->Next;
		delete (N);
		N = NN;
	}

	delete Contents;
	delete[] Key;
}

//===============================================================================
// METHOD		Column
// SYNOPSIS		Returns one column from the node's data fields
//
// ARGUMENTS    cnt			(int)		Number of the column (0=first)
//
// RETURNS		constant string containing the data (if no data is available
//				this string is empty).
//===============================================================================

const char *Fl_IndexNode::Column (int cnt)
{
	return (const char *) Contents->Get (cnt);
}

//===============================================================================
// METHOD		Height
// SYNOPSIS		Calculates the height (in units) of a node and all its children.
//				This can be used to recursively calculate the tree size.
//
// ARGUMENTS    Method requires no arguments
//
// RETURNS		The number of elements (including self)
//===============================================================================

int Fl_IndexNode::Height (void)
{
	int result = 1;
	if (Opened)
	{
		Fl_IndexNode *N = First;
		while (N)
		{
			result += N->Height();
			N = N->Next;
		}
	}
	return (result);
}

//===============================================================================
// METHOD		Contains
// SYNOPSIS		Ascertains whether a certain key is among the offspring nodes
//				of this node.
//
// ARGUMENTS    key			(char *)	The hash key we are looking for
//
// RETURNS		true if the key was found, false otherwise
//===============================================================================

bool Fl_IndexNode::Contains (const char *key)
{
	unsigned int ckey = checksum (key);
	Fl_IndexNode *N = First;

	while (N)
	{
		if ((N->CKey == ckey) && (strcmp (N->Key, key) == 0))
		{
			return true;
		}
		if (N->Contains (key)) return true;
		N = N->Next;
	}
	
	return false;
}

//===============================================================================
// METHOD		Find
// SYNOPSIS		Searches the subtree for a keyed node
//
// ARGUMENTS    key			(char *)	The hash key we are looking for
//
// RETURNS		A pointer to the node, or NULL if it was not found.
//===============================================================================

Fl_IndexNode *Fl_IndexNode::Find (const char *key)
{
	unsigned int ckey = checksum (key);
	Fl_IndexNode *N = First;
	Fl_IndexNode *result = NULL;

	while (N)
	{
		if ((N->CKey == ckey) && (strcmp (N->Key, key) == 0))
		{
			return N;
		}
		if (result = N->Find (key)) return result;
		N = N->Next;
	}
	return NULL;
}

void Fl_IndexNode::Add (const char *key, WordList *w)
{
	Fl_IndexNode *N = new Fl_IndexNode (View, this, key, pkey, w);
	Add (N);
}

void Fl_IndexNode::Add (const char *key, int count, ...)
{
	WordList *W = new WordList;
	va_list ap;
	va_start (ap, count);
	for (int i=0;i<count;++i)
	{
		W->Add (va_arg (ap, char *));
	}
	va_end (ap);

	Add (key, W);
	delete (W);
}

void Fl_IndexNode::Remove (const char *key)
{
	Fl_IndexNode *N = Find (key);
	if (N)
	{
		while (N->Count)
		{
			Fl_IndexNode *NN = N->First;
			N->Unlink (NN);
			Add (NN);
		}
		if (N->Parent)
		{
			N->Parent->Unlink (N);
			delete (N);
		}
		else
		{
			printf ("[fail] Fl_IndexNode::Remove (%s): no parent node\n", key);
		}
	}
	else
	{
		printf ("[fail] Fl_IndexNode::Remove (%s): not found\n", key);
	}
}

void Fl_IndexNode::RemoveR (const char *key)
{
	Fl_IndexNode *N = Find (key);
	if (N)
	{
		if (N->Parent)
		{
			N->Parent->Unlink (N);
			delete (N);
		}
		else
		{
			printf ("[fail] Fl_IndexNode::RemoveR (%s): no parent node\n", key);
		}
	}
	else
	{
		printf ("[fail] Fl_IndexNode::RemoveR (%s): not found\n", key);
	}
}

void Fl_IndexNode::SortKey (int k)
{
	Sort = k;
	Fl_IndexNode *N = First;
	Fl_IndexNode *NN;

	while (N)
	{
		if (N->Count) N->SortKey (k);
		NN = N->Next;
		
		Unlink (N); // This will re-sort the list, trust me :-)
		Add (N);
		N = NN;
	}
}

void Fl_IndexNode::Draw (int xx, int yy, int ww, int hh)
{
	int ycursor = 0;
	
}
