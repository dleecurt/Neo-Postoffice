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

#include "wordlist.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

typedef Word* WordPtr;

//===============================================================================
// CONSTRUCTOR	Word
// SYNOPSIS		Currently no initialization necessary
//===============================================================================

Word::Word (void)
{
	Contents = NULL;
	WContents = NULL;
}

//===============================================================================
// DESTRUCTOR	Word
// SYNOPSIS		Currently no actions performed.
//===============================================================================

Word::~Word (void)
{
	if (Contents) delete[] (Contents);
	if (WContents) delete (WContents);
}

//===============================================================================
// CONSTRUCTOR	WordList
// SYNOPSIS		Initializes memory and variables.
//===============================================================================

WordList::WordList (void)
{
	ArraySize=4;
	Array = (WordPtr *) malloc (4 * sizeof (WordPtr));
	Array[0] = Array[1] = Array[2] = Array[3] = NULL;
	
	//First = NULL;
	//Last = NULL;
	//int i;
	
	//for (i=0;i<4;++i) Cache[i] = NULL;
	
//	Protect = 0;
	Count = 0;
}

//===============================================================================
// CONSTRUCTOR	WordList
// SYNOPSIS		Initializes memory and variables. Copies another wordlist into
//				the newly created structure.
//===============================================================================

WordList::WordList (WordList *Org)
{
	ArraySize=4;
	Array = (WordPtr *) malloc (4 * sizeof (WordPtr));
	Array[0] = Array[1] = Array[2] = Array[3] = NULL;

	Count = 0;
	int i;
	
	for (i=0;i<Org->Count;++i)
	{
		if (Org->IsWordList (i)) AddWordList (Org->GetWordList (i));
		else Add (Org->Get(i));
	}
}

//===============================================================================
// DESTRUCTOR	WordList
// SYNOPSIS		Frees up memory.
//===============================================================================

WordList::~WordList (void)
{
	Clear ();
	free (Array);
}

//===============================================================================
// METHOD		Make
// SYNOPSIS		Turns a string into a wordlist. The default seperation scheme is
//				used (rfc1459/irc style, split by spaces up to the first colon).
//
// ARGUMENTS	theLine		(char *)	String that needs to be split up
//
// RETURNS		method returns no data.
//===============================================================================

void WordList::Make (char *theLine)
{
	char            myWord[2048];
	int             SourcePosition = 0;
	int             DestPosition = 0;

	myWord[0] = 0;

	Clear ();

	if (theLine[0] == ':')
		++SourcePosition;

	while (theLine[SourcePosition] != 0)
	{
		while ((theLine[SourcePosition] != 0) && (theLine[SourcePosition] != ' ') && (DestPosition < 1023))
		{
			if ((theLine[SourcePosition] > 3) && (theLine[SourcePosition] < 30))
				++SourcePosition;
			else
				myWord[DestPosition++] = theLine[SourcePosition++];
		}
		myWord[DestPosition] = 0;
		Add (myWord);
		DestPosition = 0;
		if (theLine[SourcePosition] != 0)
			++SourcePosition;
		if (theLine[SourcePosition] == ':')
		{
			//++SourcePosition;
			while (theLine[SourcePosition] != 0)
			{
				if ((theLine[SourcePosition] > 3) && (theLine[SourcePosition] < 30))
					++SourcePosition;
				else
					myWord[DestPosition++] = theLine[SourcePosition++];
			}
			myWord[DestPosition] = 0;
			Add (myWord);
		}
	}
}

//===============================================================================
// METHOD		MakeDB
// SYNOPSIS		Turns a string into a wordlist. The database seperation scheme is
//				used (seperated by '|', backslash is escape character).
//
// ARGUMENTS	theLine		(char *)	String that needs to be split up
//
// RETURNS		method returns no data.
//===============================================================================

void WordList::MakeDB (char *theLine)
{
	char           *myWord;
	int             SourcePosition = 0;
	int             DestPosition = 0;
	int				wsz;
	char		   *tmp;

	Clear ();

	while (theLine[SourcePosition] != 0)
	{
		tmp = theLine+SourcePosition;

		while ((*tmp) &&(*tmp != '|'))
		{
			if (*tmp == '\\') ++tmp;
			++tmp;
		}

		if (*tmp) wsz = tmp - (theLine+SourcePosition);
		else wsz = strlen (theLine+SourcePosition);

		myWord = new char[wsz+4];

		while ((theLine[SourcePosition] != 0) && (theLine[SourcePosition] != '|'))
		{
			if ((theLine[SourcePosition] == '\\') && (theLine[SourcePosition + 1] != 0))
				++SourcePosition;
			myWord[DestPosition++] = theLine[SourcePosition++];
		}

		if ((DestPosition > 2) && (myWord[DestPosition - 1] == ' '))
			--DestPosition;
		myWord[DestPosition] = 0;

		Add_ (myWord);

		DestPosition = 0;
		if (theLine[SourcePosition] != 0)
			++SourcePosition;
	}
}

//===============================================================================
// METHOD		MakeGeneric
// SYNOPSIS		Turns a string into a wordlist. The seperation character to be
//				used is passed as an argument.
//
// ARGUMENTS	theLine		(char *)	String that needs to be split up
//				Seperator	(char)		Seperation character
//
// RETURNS		method returns no data.
//===============================================================================

void WordList::MakeGeneric (char *theLine, char Seperator, char Escape)
{
	char           *myWord;
	int             SourcePosition = 0;
	int             DestPosition = 0;
//	int				i;
	int				wsz;
	char		   *tmp;

	if (theLine)
	{
		Clear ();

		while (theLine[SourcePosition] != 0)
		{
			if ((tmp = strchr (theLine+SourcePosition, Seperator)))
			{
				wsz = tmp - (theLine+SourcePosition);
				while ((tmp) && (*tmp-1 == Escape))
				{
					tmp = strchr (tmp, Seperator);
					if (tmp)
					{
						wsz = tmp - (theLine+SourcePosition);
					}
					else wsz = strlen (theLine);
				}
			}
			else wsz = strlen (theLine);
			
			myWord = new char[wsz+4];
			myWord[0] = 0;
			while ((theLine[SourcePosition] != 0) && (theLine[SourcePosition] != Seperator))
			{
				if ((theLine[SourcePosition] == Escape) && (theLine[SourcePosition + 1] != 0))
					++SourcePosition;
				
				if (DestPosition < wsz)
					myWord[DestPosition++] = theLine[SourcePosition++];
				else ++SourcePosition;
			}

			if ((DestPosition > 2) && (myWord[DestPosition - 1] < 32))
				--DestPosition;
			myWord[DestPosition] = 0;

			Add_ (myWord);
			DestPosition = 0;
			if (theLine[SourcePosition] != 0)
				++SourcePosition;
		}
	}
}

//===============================================================================
// METHOD		MakeGenericQuoted
// SYNOPSIS		Turns a string into a wordlist. The seperation character to be
//				used is passed as an argument. Seperators between quotes are
//				ignored.
//
// ARGUMENTS	theLine		(char *)	String that needs to be split up
//				Seperator	(char)		Seperation character
//				[Escape]	(char)		Escape character (default is backslash)
//
// RETURNS		method returns no data.
//===============================================================================

void WordList::MakeGenericQuoted (char *theLine, char Seperator, char Escape)
{
	char           *myWord;
	int             SourcePosition = 0;
	int             DestPosition = 0;
//	int				i;
	int				wsz;
	char		   *tmp;
	char		   *qp, *sqp;
	int				cpos;

	if (theLine)
	{
		Clear ();

		while (theLine[SourcePosition] != 0)
		{
			tmp = strchr (theLine+SourcePosition, Seperator);
			sqp = strchr (theLine+SourcePosition, '\'');
			qp = strchr (theLine+SourcePosition, '\"');

			while (sqp && tmp && (sqp < tmp))
			{
				sqp = strchr (sqp+1, '\'');
				if (sqp)
				{
					if (sqp > tmp)
					{
						tmp = strchr (sqp, Seperator);
					}
					sqp = strchr (sqp+1, '\'');
						
				}
				else tmp = NULL;
			}
			while (qp && tmp && (qp < tmp))
			{
				dprintf ("quote found before seperator: qp=%x & tmp=%x\n", (unsigned int) qp, (unsigned int)tmp);
				qp = strchr (qp+1, '\"');
				if (qp)
				{
					if (qp > tmp)
					{
						dprintf ("second quote after tmp, shifting tmp\n");
						tmp = strchr (qp, Seperator);
					}
					qp = strchr (qp+1, '\"');
						
				}
				else tmp = NULL;
			}
			if (tmp)
			{
				wsz = tmp - (theLine+SourcePosition);
			}
			else wsz = strlen (theLine);
			cpos = 0;
			
			myWord = new char[wsz+4];
			myWord[0] = 0;
			while ((theLine[SourcePosition] != 0) && (cpos < wsz))
			{
				if ((theLine[SourcePosition] == Escape) && (theLine[SourcePosition + 1] != 0))
				{
					++cpos;
					++SourcePosition;
				}
				if (DestPosition < wsz)
					myWord[DestPosition++] = theLine[SourcePosition++];
				else ++SourcePosition;
				++cpos;
			}

			if ((DestPosition > 2) && (myWord[DestPosition - 1] < 32))
				--DestPosition;
			myWord[DestPosition] = 0;

			Add_ (myWord);
			DestPosition = 0;
			if (theLine[SourcePosition] != 0)
				++SourcePosition;
		}
	}
}

//===============================================================================
// METHOD		Find
// SYNOPSIS		Returns the index of the first item in the list that matches
//				a string.
//
// ARGUMENTS	theMatch		(char *)	String to match
//
// RETURNS		index value. -2 for error
//===============================================================================

int WordList::Find (char *theMatch)
{
	Word           *myWord;

	int             result = -2;
	int             i = 0;

	if (theMatch)
	{
		i=0;

		while ((i<Count) && (result < 0))
		{
			myWord = Array[i];
			if (myWord->Contents)
			{
				if (strcasecmp (theMatch, myWord->Contents) == 0)
					result = i;
			}
			++i;
		}
		return result;
	}
	return -2;
}

//===============================================================================
// METHOD		Add
// SYNOPSIS		Adds an item to the wordlist
//
// ARGUMENTS	theWord		(char *)	String to be added
//
// RETURNS		method returns no data.
//===============================================================================

void WordList::Add (char *theWord)
{
	int             len;

	if (theWord)
	{
		Word           *myWord = new Word;

		if (!myWord) eprintf ("out of memory!!\n");

		len = strlen (theWord);
		myWord->Contents = new char[len +2];

		if (strlen (theWord) == 0)
			myWord->Contents[0] = 0;
		else
			strcpy (myWord->Contents, theWord);

		if ((Count+1) >= ArraySize)
		{
			Array = (WordPtr *) realloc (Array, 2 * ArraySize * sizeof (WordPtr));
			for (int i=ArraySize; i<(2*ArraySize);++i) Array[i] = NULL;
			ArraySize = 2 * ArraySize;
		}
		Array[Count] = myWord;
		++Count;
	}
}

//===============================================================================
// METHOD		Add
// SYNOPSIS		Adds an item to the wordlist
//
// ARGUMENTS	theWord		(char *)	String to be added (will not be copied)
//
// RETURNS		method returns no data.
//===============================================================================

void WordList::Add_ (char *theWord)
{
	if (theWord)
	{
		Word           *myWord = new Word;

		myWord->Contents = theWord;

		if ((Count+1) >= ArraySize)
		{
			Array = (WordPtr *) realloc (Array, 2 * ArraySize * sizeof (WordPtr));
			for (int i=ArraySize; i<(2*ArraySize);++i) Array[i] = NULL;
			ArraySize = 2 * ArraySize;
		}
		Array[Count] = myWord;
		++Count;
	}
}

//===============================================================================
// METHOD		Trim
// SYNOPSIS		Trims a wordlist only to contain a maximum number of items
//
// ARGUMENTS	theNumber	(int)		Number of items the wordlist may
//										contain. If negative, cut from the end.
//
// RETURNS		Pointer to requested item. Pointer to empty string if item
//				was not found.
//===============================================================================

void WordList::Trim (int theNumber)
{
	int _theNumber;
	
	if (theNumber<0)
	{
		_theNumber = -theNumber;
		while (Count > _theNumber)
		{
			Delete (Count-1);
		}
	}
	else
	{
		_theNumber = theNumber;
		while (Count > _theNumber)
		{
			Delete (0);
		}
	}
}

//===============================================================================
// METHOD		Get
// SYNOPSIS		Returns a numbered item
//
// ARGUMENTS	theNumber	(int)		Index value
//
// RETURNS		Pointer to requested item. Pointer to empty string if item
//				was not found.
//===============================================================================

char *WordList::Get (int theNumber)
{
	int             i = 0;
	Word           *myWord;

	if (theNumber<0) return (char *) "";
	myWord = NULL;
	if (theNumber < Count) myWord = Array[theNumber];

	if (myWord)
	{
		if (myWord->Contents) return myWord->Contents;
		else if (myWord->WContents) return myWord->WContents->Get(0);
	}
	return (char *) "";
}

//===============================================================================
// METHOD		Delete
// SYNOPSIS		Removes a numbered item from the list
//
// ARGUMENTS	theNumber	(int)		Index value
//
// RETURNS		Method returns no data.
//===============================================================================

void WordList::Delete (int theNumber)
{
	int             i = 0;

	if (theNumber>=Count) return;
	if (theNumber<0) return;

	delete (Array[theNumber]);
	for (i=theNumber; (i+1)<Count; ++i) Array[i] = Array[i+1];
	--Count;
}

//===============================================================================
// METHOD		Set
// SYNOPSIS		Replaces the contents of a numbered item with something else.
//
// ARGUMENTS	theNumber	(int)		Index value
//				theContents	(char *)	New string
//
// RETURNS		Method returns no data.
//===============================================================================

void WordList::Set (int theNumber, char *theContents)
{
	int             i = 0;
	Word           *myWord;


	if (theNumber >= Count)
	{
		while (Count <= theNumber)
			Add (".");
	}

	if (theNumber<0) return;

	myWord = NULL;
	if (theNumber <= Count) myWord = Array[theNumber];

	if (myWord)
	{
		if (myWord->WContents)
		{
			delete (myWord->WContents);
			myWord->WContents = NULL;
		}
		if (myWord->Contents != NULL) delete[] (myWord->Contents);
		myWord->Contents = new char[strlen (theContents) + 3];

		strcpy (myWord->Contents, theContents);
	}
}

//===============================================================================
// METHOD		SetInt
// SYNOPSIS		Replaces the contents of a numbered item with an integer.
//
// ARGUMENTS	theNumber	(int)		Index value
//				theContents	(int)		New integer
//
// RETURNS		Method returns no data.
//===============================================================================

void WordList::SetInt (int theNumber, int I)
{
	int             i = 0;
	Word           *myWord;

	if (theNumber >= Count)
	{
		while (Count <= theNumber)
			Add (".");
	}

	if (theNumber < 0) return;

	myWord = NULL;
	if (theNumber <= Count) myWord = Array[theNumber];

	if (myWord)
	{
		if (myWord->WContents)
		{
			delete (myWord->WContents);
			myWord->WContents = NULL;
		}
		if (myWord->Contents) delete[] (myWord->Contents);
		myWord->Contents = new char[16];;
		sprintf (myWord->Contents, "%i", I);
	}
}

//===============================================================================
// METHOD		Gather
// SYNOPSIS		Turns the wordlist back into a string.
//
// ARGUMENTS	Start		(int)		Index value of first item
//				Buffer		(char *)	Storage for the string
//				maxlen		(int)		Size of the buffer
//
// RETURNS		Returns pointer to Buffer.
//===============================================================================

char *WordList::Gather (int Start, char *Buffer, int maxlen)
{
	int             i = Start;

	if (Buffer)
	{

		Buffer[0] = 0;

			while (i < Count)
			{
				strncat (Buffer, Get (i++), maxlen);
				if (i < Count)
					strncat (Buffer, " ", maxlen);
			}

			Buffer[maxlen] = 0;
		return Buffer;
	}
	else
	{
	}
	Buffer[0] = 0;
	return Buffer;
}

//===============================================================================
// METHOD		Clear
// SYNOPSIS		Deletes all items in the list.
//
// RETURNS		Method returns no data.
//===============================================================================

void WordList::Clear (void)
{
	int i;

	for (i=0;i<Count;++i)
	{
		delete (Array[i]);
		Array[i] = NULL;
	}
	Count = 0;
}

void WordList::AddInt (int I)
{
	Word *Wo = new Word;
	Wo->Contents = new char[16];
	sprintf (Wo->Contents, "%i", I);
	
	if ((Count+1) >= ArraySize)
	{
		Array = (WordPtr *) realloc (Array, 2 * ArraySize * sizeof (WordPtr));
		ArraySize = 2*ArraySize;
	}
	
	Array[Count++] = Wo;
}

void WordList::AddWordList (WordList *W)
{
	WordList *WW = new WordList (W);
	AddWordList_ (WW);
}

void WordList::AddWordList_ (WordList *W)
{
	Word *Wo = new Word;
	Wo->WContents = W;
	
	if ((Count+1) >= ArraySize)
	{
		Array = (WordPtr *) realloc (Array, 2 * ArraySize * sizeof (WordPtr));
		ArraySize = 2*ArraySize;
	}
	
	Array[Count++] = Wo;
}

int WordList::GetInt (int theNumber)
{
	int		  i=0;
	Word		*myWord;
	
	if (theNumber>=Count) return 0;
	if (theNumber<0) return 0;
	
	myWord = Array[theNumber];

	if (myWord)
	{
		if (myWord->Contents)
		{
			return atoi (myWord->Contents);
		}
		if (myWord->WContents)
		{
			return atoi (myWord->WContents->Get(0));
		}
	}
	else
		return 0;
}

WordList *WordList::GetWordList (int theNumber)
{
	int             i = 0;
	Word           *myWord;

	if (theNumber>=Count) return 0;
	
	myWord = Array[theNumber];

	if (myWord)
	{
		if (myWord->WContents) return myWord->WContents;
		else if (myWord->Contents)
		{
			myWord->WContents = new WordList;
			myWord->WContents->MakeDB (myWord->Contents);
			return myWord->WContents;
		}
	}
	else
		return NULL;
}

int WordList::IsWordList (int theNumber)
{
	int             i = 0;
	Word           *myWord;

	if (theNumber>=Count) return 0;
	
	myWord = Array[theNumber];

	if (myWord)
	{
		if (myWord->WContents) return 1;
		return 0;
	}
	else
		return 0;
}
