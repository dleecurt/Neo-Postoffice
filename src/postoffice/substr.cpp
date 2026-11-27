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

// (PostOffice 0.7 had its own strcasestr here, case-sensitive and printing
// debug lines; every libc it runs on has the real one, from <string.h>.)
