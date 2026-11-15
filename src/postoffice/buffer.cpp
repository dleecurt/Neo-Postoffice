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

#include "debug.h"
#include "buffer.h"
#include <string.h>
#include <sys/types.h>
#if defined(SOLARIS) || defined(IRIX)
  #include <strings.h>
#endif

//void bzero(void *s, int n);

//===============================================================================
// CONSTRUCTOR	AsyncBuffer
// SYNOPSIS		Initializes the buffer storage.
//
// ARGUMENTS	_MaxSize	(int)		The desired maximum size of the buffer.
//										This is not an absolute number as much as
//										a guideline.
//===============================================================================

AsyncBuffer::AsyncBuffer (int _MaxSize)
{
	//dprintf (10, "allocbuffer: %x\n", this);
	Data = new u_char[_MaxSize + 1];

	MaxSize = _MaxSize;
	Pos = 0;
}

//===============================================================================
// DESTRUCTOR	AsyncBuffer
// SYNOPSIS		Frees up memory.
//===============================================================================

AsyncBuffer::~AsyncBuffer (void)
{
//	dprintf (10, "freebuffer: %x\n", this);
	delete[] (Data);
	Data = (unsigned char *) NULL;
}

//===============================================================================
// METHOD		Add
// SYNOPSIS		Concatenates a string to the buffer
//
// ARGUMENTS    C			(char *)	The string
//
// RETURNS		method returns no data
//===============================================================================

void AsyncBuffer::Add (const char *C)
{
	Add ((u_char *) C, (int) strlen (C));
}

//===============================================================================
// METHOD		Add
// SYNOPSIS		Concatenates a formattedstring to the buffer
//
// ARGUMENTS    C			(char *)	The format string
//				...						arguments
//
// RETURNS		method returns no data
//===============================================================================

void AsyncBuffer::AddF (const char *C, ...)
{
	char	OutString[4096];
	va_list argptr;
	va_start (argptr, C);
	vsprintf (OutString, C, argptr);
	va_end (argptr);
	
	Add (OutString);
}

//===============================================================================
// METHOD		Add
// SYNOPSIS		Concatenates a byte to the buffer
//
// ARGUMENTS    C			(u_char)	The byte value
//
// RETURNS		method returns no data
//===============================================================================

void AsyncBuffer::Add (u_char C)
{
	if (Pos < MaxSize)
		Data[Pos++] = C;
	else Add (&C, 1);
}

//===============================================================================
// METHOD		Add32
// SYNOPSIS		Concatenates a 32-bit word to the buffer in a platform-
//				independant fashion
//
// ARGUMENTS    L			(u_long)	The word
//
// RETURNS		method returns no data
//===============================================================================

void AsyncBuffer::Add32 (u_long L)
{
	Add ((u_char) ((L&0xff000000)>>24));
	Add ((u_char) ((L&0x00ff0000)>>16));
	Add ((u_char) ((L&0x0000ff00)>>8));
	Add ((u_char) (L&0x000000ff));
}

//===============================================================================
// METHOD		Add16
// SYNOPSIS		Concatenates a 16-bit word to the buffer in a platform-
//				independant fashion
//
// ARGUMENTS    L			(u_long)	The word
//
// RETURNS		method returns no data
//===============================================================================

void AsyncBuffer::Add16 (u_long L)
{
	Add ((u_char) ((L&0x0000ff00)>>8));
	Add ((u_char) (L&0x000000ff));
}

//===============================================================================
// METHOD		Add
// SYNOPSIS		Concatenates binary data to the buffer
//
// ARGUMENTS	C			(char *)	The data
//				S			(int)		The size
//
// RETURNS		method returns no data
//===============================================================================

void AsyncBuffer::Add (u_char *C, int S)
{
//	dprintf (1, "buffer:Add %d\n", S);
	if (S > 0)
	{
		if (Pos + S < MaxSize)	// Verify that we have enough memory
		{
			memcpy (Data + Pos, C, S);
			Pos += S;
		}
		else	// we don't, so reallocate the buffer (Note that this is more CPU-
				// intensive, optimizing the Buffersize at creation time is still the
				// best solution. I just don't believe in a stupid compile-time option
				// totally ruining it :-)
		{
			int		 NewMaxSize;
			u_char	*NewData;
			
			NewMaxSize = MaxSize*2;
			if (NewMaxSize < Pos+S) NewMaxSize = Pos+S+100;
			
			dprintf ("asyncbuffer: grow (%d,%d)\n", MaxSize, NewMaxSize);
			
			NewData = new u_char[NewMaxSize+2];
			memcpy (NewData, Data, Pos);
			delete[] (Data);
			Data = NewData;
			MaxSize = NewMaxSize;
			
			memcpy (Data + Pos, C, S);
			Pos += S;
		}
	}
}

void AsyncBuffer::Grow (int S)
{
	if (Pos + S < MaxSize)
	{
		bzero (Data+Pos, S);
		Pos += S;
	}
	else
	{
		int		 NewMaxSize;
		u_char	*NewData;
		
		NewMaxSize = MaxSize*2;
		if (NewMaxSize < Pos+S) NewMaxSize = Pos+S+100;
		
		NewData = new u_char[NewMaxSize+2];
		memcpy (NewData, Data, Pos);
		delete[] (Data);
		Data = NewData;
		MaxSize = NewMaxSize;
		
		bzero (Data + Pos, S);
		Pos += S;
	}
}

//===============================================================================
// METHOD		Shift
// SYNOPSIS		Releases data at the beginning of the buffer and shifts it to
//				the left.
//
// ARGUMENTS    S			(int)		The number of bytes to shift
//
// RETURNS		method returns no data
//===============================================================================

void AsyncBuffer::Shift (int S)
{
//	dprintf (1, "buffer:Shift %d\n", S);
	if (Pos - S > 0)
	{
		memcpy (Data, Data + S, (Pos - S));
		Pos -= S;
	}
	else
	{
		Pos = 0;
	}
}
