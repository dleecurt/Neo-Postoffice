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

#include "substr.h"
#include <string.h>
#include <stdio.h>

char *substr (const char *start, const char *end)
{
	int ln = (end - start);
	char *result = new char [ln +2];
	memcpy (result, start, ln);
	result[ln] = 0;
	return result;
}

char *substr (const char *str, int start, int len)
{
	int ln;
	char *result = new char[len+2];
	ln = strlen (str+start);
	if (ln > len) ln = len;
	strncpy (result, str+start, ln);
	result[ln] = 0;

	return (result);
}

const char *strcasestr (const char *haystack, const char *needle)
{
	int haypos, needlepos, crsr;
	int result;

	needlepos = haypos = crsr = 0;

	printf ("strcasestr ('%s','%s')\n", haystack, needle);

	while (haystack[haypos])
	{
		if (haystack[haypos] == needle[needlepos])
		{
			printf ("possible match at %i\n", haypos);
			result = 0;
			crsr = 0;
			needlepos = 0;
			
			while ((needle[needlepos])&&(haystack[haypos + crsr])&&(!result))
			{
				if (needle[needlepos]==haystack[haypos+crsr])
				{
					++needlepos;
					++crsr;
					if (!(needle[needlepos]))
					{
						return haystack+haypos;
					}
				}
				else result = 1;
			}
		}
		++haypos;
	}
	return NULL;
}
