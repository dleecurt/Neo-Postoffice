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

#include "chop.h"
#include <string.h>
#include <stdio.h>
extern "C" {
	#include <ctype.h>
}

//===============================================================================
// FUNCTION		escape_html
// SYNOPSIS		Create a string from a text/plain type string that is HTML-safe
//
// ARGUMENTS	string		(char *)	The string to be checksumed
//
// RETURNS		A pointer to the resulting, escaped string. Caller is responsible
// 				for deleting after use.
//===============================================================================

char *escape_html (char *str)
{
	int i,j;
	int slen = strlen (str);
	int len = slen;
	char *result;
	
	for (i=0;i<slen;++i)
	{
		if ( (str[i]=='<') || (str[i]=='>') ) len += 3;
		if (str[i] == '&') len += 4;
	}
	result = new char[len+1];
	j=0;
	for (i=0;i<slen;++i)
	{
		switch (str[i])
		{
			case '<':
				result[j++] = '&';
				result[j++] = 'l';
				result[j++] = 't';
				result[j++] = ';';
				break;

			case '>':
				result[j++] = '&';
				result[j++] = 'g';
				result[j++] = 't';
				result[j++] = ';';
				break;

			case '&':
				result[j++] = '&';
				result[j++] = 'a';
				result[j++] = 'm';
				result[j++] = 'p';
				result[j++] = ';';
				break;

			default:
				result[j++] = str[i];
		}
	}
	result[j] = 0;
	return result;
}

//===============================================================================
// FUNCTION		choplf
// SYNOPSIS		Removes redundant linefeed information from a string.
//
// ARGUMENTS	string		(char *)	The string to be altered
//
// RETURNS		No return values, direct data manipulation is used.
//===============================================================================

void choplf (char *str)
{
	int ln = strlen (str);
	while ( (ln>0) && (str[ln-1] < 32) ) str[--ln] = 0;
}

//===============================================================================
// FUNCTION		chopwhitespace
// SYNOPSIS		Removes redundant trailing and leading whitespace from a string.
//
// ARGUMENTS	string		(char *)	The string to be altered
//
// RETURNS		No return values, direct data manipulation is used.
//===============================================================================

void chopwhitespace (char *str)
{
	int ln = strlen (str);
	while ( (ln>0) && ( (str[ln-1]<32) || (str[ln]==' ') ) ) str[--ln] = 0;
	while ((str[0] == ' ')||(str[0] == '\t'))
	{
		memmove (str,str+1,(size_t) strlen(str));
	}
}

//===============================================================================
// FUNCTION		chopwincrap
// SYNOPSIS		Cleans up entirely bogus-formatted text as sent out by Microsoft
// 				mail- and newsclients.
//
// ARGUMENTS	string		(char *)	The original string.
//
// RETURNS		A mpointer to the result string, or NULL in case of an error.
// 				Caller is responsible for deleting the data after use.
//===============================================================================

char *chopwincrap (char *org)
{
	if (!org) return (NULL);
	char *dst = new char [strlen(org)+2];
	int src_crsr = 0;
	int dst_crsr = 0;
	int digit1, digit2;
	
	while (org[src_crsr])
	{
		if (org[src_crsr]=='=')
		{
			if (org[src_crsr+1] < 32) ++src_crsr;
			else if (org[src_crsr+1]==' ')
			{
				if (org[src_crsr+2] == 0)
					++src_crsr;
				else
				{
					dst[dst_crsr++] = '=';
					++src_crsr;
				}
			}
			else
			{
				digit2 = -1;
				digit1 = tolower(org[src_crsr+1]);
				if (digit1) digit2 = tolower(org[src_crsr+2]);

				if ( (digit1 >= '0') && (digit1 <= '9') ) digit1 = digit1 - '0';
				else if ( (digit1 >='a') && (digit1 <='f') ) digit1 = 10 + digit1 - 'a';
				else digit1 = -1;

				if ( (digit2 >= '0') && (digit2 <= '9') ) digit2 = digit2 - '0';
				else if ( (digit2 >='a') && (digit2 <='f') ) digit2 = 10 + digit2 - 'a';
				else digit2 = -1;

				if ( (digit1<0)||(digit2<0) )
				{
					dst[dst_crsr++] = '=';
					++src_crsr;
				}
				else
				{
					dst[dst_crsr++] = ( (digit1 & 0x0f) << 4) + (digit2 & 0x0f);
					++src_crsr;
					if (org[src_crsr]) ++src_crsr;
					if (org[src_crsr]) ++src_crsr;
				}
			}
		}
		else dst[dst_crsr++] = org[src_crsr++];
	}
	dst[dst_crsr] = 0;
	return dst;
}
