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
#include <sys/types.h>

//===============================================================================
// FUNCTION		checksum
// SYNOPSIS		Turns a string into a rough hash-index.
//
// ARGUMENTS	string		(char *)	The string to be checksumed
//
// RETURNS		The checksum value.
//===============================================================================

const unsigned char obox[32] = {1,27,5,17,3,19,12,4,18,11,4,25,22,2,20,6,24,
								7,21,13,0,19,8,16,10,15,4,21,7,18,3,23};

unsigned int checksum (const char *string)
{
	register int len,pos;
	register char c;
	len = strlen (string);
	
	register unsigned int  result = (((len&0xff)^0x88) << 24) + (((len&0xff)^0x24) << 16) +
							 (((len&0xff)^0x41) << 8) + ((len&0xff)^0x12);
	
	for (pos=0;pos<len;++pos)
	{
		c = string[pos];
		if (c & 0xc0) result ^= (((c^obox[(pos+7) & 0x1f]) & 0x3f) << obox[pos & 0x1f]);
		else result ^= (((c^obox[(pos+17) & 0x1f]) & 0x3f) << obox[(pos^c) & 0x1f]);
	}
	return result;
}

// The old checksum hasher is a lot crappier. A collision rate of 15% is not uncommon.
// It's also not reallly much faster than the new one.

u_long old_checksum (const char *string)
{
	register char   c;
	register int    len, pos;
	register u_long result, max,mult;

	result = 0;
	len = strlen (string);
	max = (8*len*32);
	mult = (0xc0000000 / (max+1));
	
	for (pos = 0; pos < len; ++pos)
	{
		c = string[pos];
		if ((c >= '0') && (c <= '9'))
			result += (((pos & 0x0f)+1) * (2 ^ (c - '0' + 1)));
		else
			result += ((((pos & 0x0f)*2)+1) * (c & 31));
	}
	result = result * mult;
	return result;
}

