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

//#include "semaphore.h"

char *Var::Get_char_tree (int amount, ...)
{
	char *vlu;
	int pos = 0;
	Var *X = this;
	
	va_list ap;
	va_start (ap, amount);
		pos += 1;
		while (pos < amount)
		{
			X = X->Get_collection (va_arg (ap, char *));
			++pos;
		}
		vlu = X->Get_char (va_arg (ap, char *));
	va_end(ap);

	return (vlu);			
}

void Var::Set_char_tree (const char *path, const char *value)
{
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
	
	X->Set_char (W->Get(i), value);
	delete (W);
}

void Var::Set_char_tree (const char */*value*/, int /*amount*/, const char */*node*/, ...)
{
//	int pos = 0;
//	Var *X = this;
}

WordList *Var::GatherValues (void)
{
	Lock.LockR();
	
	Value *myValue;
	WordList *result = new WordList;
	
	myValue = First;
	while (myValue)
	{
		result->Add (myValue->Get_char());
		myValue = myValue->Next;
	}
	Lock.UnlockR();
	return result;
}

void Var::Load (const char *fname)
{
	FILE *F = fopen (fname, "r");
	
	if (F)
	{
		Load (F);
		fclose (F);
	}
	else
	{
		printf ("Load error: %s\n", fname);
	}
}

void Var::Load (FILE *F)
{
	char Buffer[1024];
	char Name[256], Value[768];
	int done=0;
	int ps;
	int ps2;
	
	while (!done)
	{
		if (!fgets (Buffer, 1023, F) || feof (F)) done = 1;
		else
		{
			
			if ((strchr (Buffer, '}') && !(strchr (Buffer, '\"')))) done = 1;
			else if (strchr (Buffer, '='))
			{
				ps = 0;
				while ((Buffer[ps])&&(Buffer[ps]<=' ')) ++ps;
				if (Buffer[ps])
				{
					ps2=0;
					while ((Buffer[ps]) && (Buffer[ps] > ' ') && (Buffer[ps] != '=')) Name[ps2++] = Buffer[ps++];
					Name[ps2] = 0;
					
					while (Buffer[ps] != '=') ++ps;
					if (Buffer[ps] == '=') ++ps;
					while ((Buffer[ps]) && (Buffer[ps] <= ' ')) ++ps;
					if ((Buffer[ps]) && (Buffer[ps]=='\"')) ++ps;
					ps2=0;
					while ((Buffer[ps]) && (Buffer[ps]!='\"') && (Buffer[ps]>=' ')) Value[ps2++] = Buffer[ps++];
					Value[ps2] = 0;
					//printf ("setchar: %s = %s\n", Name, Value);
					Set_char (Name, Value);
				}
			}
			else if (strchr (Buffer, '{'))
			{
				ps = 0;
				while ((Buffer[ps])&&(Buffer[ps]<=' ')) ++ps;
				ps2=0;
				while ((Buffer[ps]) && (Buffer[ps] > ' ')) Name[ps2++] = Buffer[ps++];
				Name[ps2] = 0;
				//printf ("new collection %s\n", Name);
				Get_collection (Name)->Load (F);
			}
		}
	}
	//printf ("unbranch\n");
}

void Var::Save (const char *fname)
{
//	Value *myValue;
	FILE *F;
	
	F = fopen (fname, "w");
	if (F)
	{
		Save (F, 0);
		fclose (F);
	}
}

void Var::Save (FILE *F, int indent)
{
	Value *myValue = First;
	int i;
	
	while (myValue)
	{
		for (i=0;i<indent;++i) fputc (' ',F);
		if (myValue->Get_type() == VT_COLLECTION)
		{
			fprintf (F, "%s {\n", myValue->Name);
			myValue->Get_collection()->Save (F, indent+4);
			for (i=0;i<indent;++i) fputc (' ',F);
			fprintf (F, "}\n");
		}
		else
		{
			fprintf (F, "%s = \"%s\"\n", myValue->Name, myValue->Get_char());
		}
		myValue = myValue->Next;
	}
}

void Var::CopyTo (Var *dest)
{
	Value *myValue = First;
	
	dest->Clear();
	
	while (myValue)
	{
//		myValue->CopyTo (dest);
		myValue = myValue->Next;
	}
}
