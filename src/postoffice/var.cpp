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

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include "var.h"
#include "reg.h"
#include "wordlist.h"
#include "checksum.h"

#include "debug.h"

//#include "semaphore.h"

Value::Value (const char *theName)
{
	strncpy (Name, theName, MAXVARNAMELEN-2);
	Name[MAXVARNAMELEN-1] = 0;
	Checksum = checksum (Name);
	cValue = NULL;
	sValue = 0;
	iValue = 0;
	lValue = 0;
	TYPE   = 0;
	wValue = NULL;
	Pointer = NULL;
	vValue = NULL;

	Parent = NULL;
	HigherNode = NULL;
	LowerNode = NULL;
	SameNode = NULL;
	
	Altered = 0;
}

Value::~Value (void)
{

	if (cValue) delete[] (cValue);
	if (wValue) delete (wValue);
	if (vValue) delete (vValue);
}

void Value::Rename (const char *newName)
{
	strncpy (Name, newName, MAXVARNAMELEN-1);
	Name[MAXVARNAMELEN-1] = 0;
	Checksum = checksum (Name);
}

void Value::Set_int (int Val) { iValue = Val; TYPE = VT_INT; Altered=1; }
void Value::Set_short (u_short Val) { sValue = Val; TYPE = VT_SHORT; Altered=1; }
void Value::Set_long (u_long Val) { lValue = Val; TYPE = VT_LONG; Altered=1; }
void Value::Set_char (const char *Val)
{
	int len = strlen (Val);
	if (cValue) delete[] (cValue);
	cValue = new char[len+1];
	strcpy (cValue, Val);
	TYPE = VT_CHAR;
	
	Altered=1;
}
void Value::Set_wordlist (WordList *Val)
{
	if (wValue) delete (wValue);
	wValue = new WordList (Val);
	TYPE = VT_WORDLIST;
	
	Altered=1;
}

void Value::Set_binary (const char *Val, size_t len)
{
	//_OBJECT = VAR_O;
	
	if (cValue) delete[] (cValue);
	cValue = new char[len+1];
	bcopy (Val, cValue, len);
	BSize = len;
	cValue[len] = 0;
	TYPE = VT_BINARY;
	
	Altered=1;
}

int Value::Get_int (void)
{
	switch (TYPE)
	{
		case VT_INT			: return iValue;
		case VT_LONG		: return (int) lValue;
		case VT_SHORT		: return (int) sValue;
		case VT_CHAR		: return atoi (cValue);
		case VT_BINARY		: return atoi (cValue);
		case VT_WORDLIST	: return (int) 0;
		case VT_COLLECTION	: return (vValue->Count);
	}
	return (int) 0;
}

u_long Value::Get_long (void)
{
	switch (TYPE)
	{
		case VT_INT			: return (u_long) iValue;
		case VT_LONG		: return lValue;
		case VT_SHORT		: return (u_long) sValue;
		case VT_CHAR		: return (u_long) atol (cValue);
		case VT_BINARY		: return (u_long) atol (cValue);
		case VT_WORDLIST	: return (u_long) 0;
		case VT_COLLECTION	: return ((u_long) vValue->Count);
	}
	return ((u_long) 0);
}

u_short Value::Get_short (void)
{
	switch (TYPE)
	{
		case VT_INT			: return (u_short) iValue; break;
		case VT_LONG		: return (u_short) lValue; break;
		case VT_SHORT		: return sValue; break;
		case VT_CHAR		: return (u_short) atoi (cValue);
		case VT_BINARY		: return (u_short) atoi (cValue);
		case VT_WORDLIST	: return (u_short) 0;
		case VT_COLLECTION	: return ((u_short) vValue->Count);
	}
	return ((u_short) 0);
}

char *Value::Get_char (void)
{
	char Buffer[1024];
	int i,j,cnt;
	
	switch (TYPE)
	{
		case VT_INT		: snprintf (Buffer, sizeof (Buffer), "%i", iValue);
						  Set_char (Buffer);
						  TYPE = VT_INT;
						  return cValue;
						  break;
		case VT_LONG	: snprintf (Buffer, sizeof (Buffer), "%ld", lValue);
						  Set_char (Buffer);
						  TYPE = VT_LONG;
						  return cValue;
						  break;
		case VT_SHORT	: snprintf (Buffer, sizeof (Buffer), "%i", sValue);
						  Set_char (Buffer);
						  TYPE = VT_SHORT;
						  return cValue;
						  break;
		case VT_CHAR	: return cValue;
						  break;
		
		case VT_BINARY	: return cValue;
						  break;
		
		case VT_WORDLIST: cnt=0;
						  Buffer[cnt]=0;
						  char *V;
						  for (i=0;i<wValue->Count;++i)
						  {
						  	for (j=0;j<(int) strlen ((V=wValue->Get(i)));++j)
							{
								if (cnt<1022)
								{
									if ((V[j]==',')||(V[j]=='\\')) Buffer[cnt++]='\\';
									Buffer[cnt++] = V[j];
								}
							}
							if ((i+1)<wValue->Count)
								if (cnt<1022) Buffer[cnt++]=',';
						  }
						  Buffer[cnt] = 0;
						  Set_char (Buffer);
						  TYPE = VT_WORDLIST;
						  return cValue;
						  break;
						  
		case VT_COLLECTION:
						  if (cValue) return (cValue); 
						  return (char *) "";
	}
	return (char *) "";
}

WordList *Value::Get_wordlist (void)
{
	if (TYPE!=VT_WORDLIST)
	{
		if (!wValue)
		{
			wValue = new WordList;
		}
		else wValue->Clear();
		if (TYPE!=VT_COLLECTION)
		{
			wValue->MakeGeneric (Get_char(),',');
			TYPE = VT_WORDLIST;
		}
		else
		{
			if (vValue) wValue = vValue->GatherNames();
			else wValue = new WordList;
		}
	}
	return wValue;
}

char *Value::Get_binary (void)
{
	switch (TYPE)
	{
		case VT_INT			: return NULL;
		case VT_LONG			: return NULL;
		case VT_SHORT		: return NULL; break;
		case VT_CHAR			: return cValue;
		case VT_BINARY		: return cValue;
		case VT_COLLECTION	: return NULL;
	}
    return NULL;
}

size_t Value::Get_size (void)
{
	switch (TYPE)
	{
		case VT_INT		: return 0;
		case VT_LONG		: return 0;
		case VT_SHORT	: return 0;
		case VT_CHAR		: return strlen (cValue);
		case VT_BINARY	: return BSize;
	}
	return 0;
}

Var *Value::Get_collection (void)
{
	if (vValue == NULL)
	{
		vValue = new Var;
		TYPE = VT_COLLECTION;
	}
	return vValue;
}

void Value::Set_collection (Var *V)
{
	if (vValue) delete (vValue);
	vValue = new Var (V);
	TYPE = VT_COLLECTION;
}

u_short Value::Get_type (void)
{
	return TYPE;
}

//===========================================================

//Var::Var (void)

Var::Var (Var *Org)
{
	//Lock = new ASemaphore ("var");

	First = Last = NULL;
	Count = 0;
	int i;
	
	for (i=0;i<16;++i)
	{
		CacheChecksum[i] = 0;
		CacheValue[i] = NULL;
	}

	CopyFrom (Org);

}

void Var::CopyFrom (Var *Org)
{
	Value *myValue, *nextValue;

	Lock.LockRW();
	
	myValue = First;
	
	while (myValue)
	{
		nextValue = myValue->Next;
		delete (myValue);
		myValue = nextValue;
	}
	
	First = NULL;
	Last = NULL;
	Count = 0;
	for (int i = 0; i < 16; ++i)
	{
		CacheChecksum[i] = 0;
		CacheValue[i] = NULL;
	}

	Value *X;
	Value *V = Org->First;
	while (V)
	{
		X = new Value (V->Name);
		//printf ("copying new value: %s type %i\n", V->Name, V->Get_type());
		switch (V->Get_type())
		{
			case VT_INT: X->Set_int (V->Get_int()); break;
			case VT_BINARY: break;
			case VT_COLLECTION: X->Set_collection (V->Get_collection()); break;
			default: X->Set_char (V->Get_char()); break;
		}
		Add (X);
		V = V->Next;
	}

	Lock.UnlockRW();
}

Var::~Var (void)
{
	Value *myValue, *nextValue;
	
	myValue = First;
	
	while (myValue)
	{
		nextValue = myValue->Next;
		delete (myValue);
		myValue = nextValue;
	}
	
	First = NULL;
	Last = NULL;
	
	//delete (Lock);
}

void Var::Clear (void)
{
	Value *myValue, *nextValue;
	int i;
	
	myValue = First;
	
	while (myValue)
	{
		nextValue = myValue->Next;
		dprintf ("[VAR] delete (%08x -> %08x = [%s])\n", this, myValue, myValue->Name);
		delete (myValue);
		myValue = nextValue;
	}

	for (i=0;i<16;++i)
	{
		CacheChecksum[i] = 0;
		CacheValue[i] = NULL;
	}
	
	First = NULL;
	Last = NULL;
	Count = 0;
}

Value *Var::Find (const char *Val)
{
	Value *Result;
	Lock.LockR();
	Result = _Find (Val);
	Lock.UnlockR();
	return (Result);
}

Value *Var::_Find (const char *Val)
{
	unsigned int cVal = checksum (Val);
	int cV = (cVal & 0x0f);			// 4-bit hash deriviate used for the cached nodes.

	if ((CacheValue[cV])&&(cVal == CacheChecksum[cV]))
	{
		if (strcasecmp (Val, CacheValue[cV]->Name) == 0)
			return (CacheValue[cV]);
	}
	
	Value *result = NULL;
	Value *theValue = First;
	
	while (theValue)
	{
		if (theValue->Checksum == cVal)
		{
			if (strcasecmp (Val, theValue->Name)==0)
			{
				result = theValue;
				theValue = NULL;
			}
			else
			{
				if (theValue->SameNode)
				{
					if (theValue->SameNode == theValue) theValue = NULL;
					else theValue = theValue->SameNode;
				}
				else theValue = NULL;
			}
		}
		else if (cVal < theValue->Checksum)
		{
			if (theValue->LowerNode) theValue = theValue->LowerNode;
			else theValue = NULL;
		}
		else
		{
			if (theValue->HigherNode) theValue = theValue->HigherNode;
			else theValue = NULL;
		}
	}
	if (result)
	{
		CacheValue[cV] = result;
		CacheChecksum[cV] = cVal;
	}
	return result;
}

void Var::Add (Value *Val)
{
	Value *V;
	
	if (First)
	{
		Val->Prev = Last;
		Val->Next = NULL;
		Last->Next = Val;
		Last = Val;
	}
	else
	{
		Val->Prev = NULL;
		Val->Next = NULL;
		First = Val;
		Last = Val;
	}
	++Count;
	
	// If there is more than one branch, we can start growing a tree :-)
	
	if (First)
	{
		V = First;
		while (V)
		{
			if (Val->Checksum == V->Checksum)
			{
				if (Val != V)
				{
					if (V->SameNode) V=V->SameNode;
					else
					{
						V->SameNode = Val;
						Val->Parent = V;
						V = NULL;
					}
				}
				else V = NULL;		// the first entry is the root itself
			}
			else if (Val->Checksum < V->Checksum)
			{
				if (V->LowerNode) V=V->LowerNode;
				else
				{
					V->LowerNode = Val;
					Val->Parent = V;
					V = NULL;
				}
			}
			else
			{
				if (V->HigherNode) V=V->HigherNode;
				else
				{
					V->HigherNode = Val;
					Val->Parent = V;
					V = NULL;
				}
			}
		}
	}
}

void Var::Set_int (const char *Val, int Arg)
{
	Lock.LockRW();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	if (theValue)
	{
		theValue->Set_int (Arg);
	}
	else
	{
		theValue = new Value (Val);
		theValue->Set_int (Arg);
		Add (theValue);
	}
	Lock.UnlockRW();
}

void Var::Set_long (const char *Val, u_long Arg)
{
	Lock.LockRW();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	if (theValue)
	{
		theValue->Set_long (Arg);
	}
	else
	{
		theValue = new Value (Val);
		theValue->Set_long (Arg);
		Add (theValue);
	}
	Lock.UnlockRW();
}

void Var::Set_char (const char *Val, const char *Arg)
{
	Lock.LockRW();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	if (theValue)
	{
		theValue->Set_char (Arg);
	}
	else
	{
		theValue = new Value (Val);
		theValue->Set_char (Arg);
		Add (theValue);
	}
	Lock.UnlockRW();
}

void Var::Set_wordlist (const char *Val, WordList *Arg)
{
	Lock.LockRW();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	if (theValue)
	{
		theValue->Set_wordlist (Arg);
	}
	else
	{
		theValue = new Value (Val);
		theValue->Set_wordlist (Arg);
		Add (theValue);
	}
	Lock.UnlockRW();
}


void Var::Set_binary (const char *Val, const char *Arg, size_t Sz)
{
	Lock.LockRW();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	if (theValue)
	{
		theValue->Set_binary (Arg, Sz);
	}
	else
	{
		theValue = new Value (Val);
		theValue->Set_binary (Arg, Sz);
		Add (theValue);
	}
	Lock.UnlockRW();
}

void Var::Set_short (const char *Val, u_short Arg)
{
	Lock.LockRW();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	if (theValue)
	{
		theValue->Set_short (Arg);
	}
	else
	{
		theValue = new Value (Val);
		theValue->Set_short (Arg);
		Add (theValue);
	}
	Lock.UnlockRW();
}

int Var::Get_int (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	
	if (theValue) return theValue->Get_int();
	else return 0;
}

WordList *Var::Get_wordlist (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	WordList *W;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	
	if (theValue) return theValue->Get_wordlist();
	else
	{
		W = new WordList;
		theValue = new Value (Val);
		theValue->Set_wordlist (W);
		delete (W);
		Add (theValue);
		return (theValue->Get_wordlist());
	}
}

Var *Var::Get_collection (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	Var *V;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	
	if (theValue) return theValue->Get_collection();
	else
	{
		theValue = new Value (Val);
		V = theValue->Get_collection ();
		Add (theValue);
	}
	return (V);
}

u_long Var::Get_long (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	
	if (theValue) return theValue->Get_long();
	else return 0;
}

u_short Var::Get_short (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	
	if (theValue) return theValue->Get_short();
	else return 0;
}

char *Var::Get_char (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	if (theValue) return theValue->Get_char();
	else return ((char *) "");
}

char *Var::Get_binary (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	
	if (theValue) return theValue->Get_binary();
	else return ((char *) "");
}

size_t Var::Get_size (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	
	if (theValue) return theValue->Get_size();
	else return 0;
}

u_short Var::Get_type (const char *Val)
{
	Lock.LockR();
	
	Value *theValue;
	
	theValue = this->_Find (Val);
	Lock.UnlockR();
	if (theValue) return theValue->Get_type();
	else return 0;
}

// Rebuilds the lookup tree (rooted at First) and the cache from the list,
// after an entry was removed or renamed. The original code relinked the
// tree by hand and lost or duplicated entries when the first one was
// removed (Neo-PostOffice).
void Var::Rebuild (void)
{
	Count = 0;
	for (Value *V = First; V; V = V->Next)
		V->Parent = V->LowerNode = V->HigherNode = V->SameNode = NULL;
	for (Value *V = First; V; V = V->Next)
	{
		++Count;
		if (V != First) Relink (V);
	}
	for (int i = 0; i < 16; ++i)
	{
		CacheChecksum[i] = 0;
		CacheValue[i] = NULL;
	}
}

void Var::Delete (const char *Name)
{
	Lock.LockRW();
	Value *V = _Find (Name);
	if (V)
	{
		if (V->Prev) V->Prev->Next = V->Next; else First = V->Next;
		if (V->Next) V->Next->Prev = V->Prev; else Last = V->Prev;
		delete V;
		Rebuild ();
	}
	Lock.UnlockRW();
}

bool Var::Exists (const char *name)
{
	if (Find (name)) return true;
	return false;
}

// Renames OldName to NewName, keeping its place in the list (and so in
// the saved file). Nothing happens if OldName does not exist or NewName
// is already taken by another entry. (The original looked up NewName.)
void Var::Rename (const char *OldName, const char *NewName)
{
	Lock.LockRW();
	Value *V = _Find (OldName);
	Value *Taken = _Find (NewName);
	if (V && (!Taken || Taken == V))
	{
		V->Rename (NewName);
		Rebuild ();
	}
	Lock.UnlockRW();
}

void Var::Relink (Value *V)
{
	Value *X = First;
	
	while (X)
	{
		if (V->Checksum > X->Checksum)
		{
			if (X->HigherNode) X = X->HigherNode;
			else
			{
				V->Parent = X;
				X->HigherNode = V;
				X = NULL;
			}
		}
		else if (V->Checksum < X->Checksum)
		{
			if (X->LowerNode) X = X->LowerNode;
			else
			{
				V->Parent = X;
				X->LowerNode = V;
				X = NULL;
			}
		}
		else
		{
			if (X->SameNode) X = X->SameNode;
			else
			{
				V->Parent = X;
				X->SameNode = V;
				X = NULL;
			}
		}
	}
}

WordList *Var::GatherNames (void)
{
	Lock.LockR();
	
	Value *myValue;
	WordList *result = new WordList;
	
	myValue = First;
	while (myValue)
	{
		result->Add (myValue->Name);
		myValue = myValue->Next;
	}
	Lock.UnlockR();
	return result;
}

char *Var::Get_char_tree (const char *path)
{
	char *result;
	Var *X;
	int i;
	WordList *W = new WordList;
	
	if (strchr (path, '/')) W->MakeGeneric ((char *) path, '/');
	else W->MakeGeneric ((char *) path, '.');
	
	X = this;
	
	for (i=0; i<(W->Count)-1; ++i)
	{
		X = X->Get_collection (W->Get(i));
	}
	
	result = X->Get_char (W->Get(i));
	delete (W);
	
	return result;
}

char *Var::Get_char_tree (const char *path, char sep)
{
	char *result;
	Var *X;
	int i;
	WordList *W = new WordList;
	
	W->MakeGeneric ((char *) path, sep);
	
	X = this;
	
	for (i=0; i<(W->Count)-1; ++i)
	{
		X = X->Get_collection (W->Get(i));
	}
	
	result = X->Get_char (W->Get(i));
	delete (W);
	
	return result;
}

char *Var::Get_char_backmatch (const char *matchval, const char *realval)
{
	Lock.LockR();
	Value *V, *VVV;
	Var *VV;
	char *result = (char *) "";
	int done=0;
	
	V = Last;
	
	while ((V)&&(!done))
	{
		if (wild_match (V->Name, (char *) matchval))
		{
			VV = V->Get_collection();
			if ((VVV = VV->Find (realval)))
			{
				result = VVV->Get_char();
				done=1;
			}
		}
		V = V->Prev;
	}
	
	Lock.UnlockR();
	return (result);
}

WordList *Var::Get_wordlist_backmatch (const char *matchval, const char *realval)
{
	Lock.LockR();
	Value *V = Last;
	Value *VVV;
	Var *VV;
	WordList *result = NULL;
	int done=0;

	
	
	while ((V)&&(!done))
	{
		if (wild_match ((char *) V->Name, (char *) matchval))
		{
			VV = V->Get_collection();
			if ((VVV = VV->Find (realval)))
			{
				result = VVV->Get_wordlist();
				done=1;
			}
		}
		V = V->Prev;
	}

	Lock.UnlockR();
	
	if (!result)
	{
		V = First;
		if (V)
		{
			V->Get_collection()->Set_char (realval, "");
			result = V->Get_collection()->Get_wordlist (realval);
		}
		else
		{
			Set_char (realval, "");
			result = Get_wordlist (realval);
		}
	}
	
	return (result);
}

