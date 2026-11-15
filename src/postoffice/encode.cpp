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
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "encode.h"
#include "debug.h"

inline int needsescape (char x)
{
	char y;
	
	if (x<32) return 1;
	if ( (x=='.') || (x==',') || (x==':') || (x==';') || (x=='/') || (x=='!') || (x=='?')) return 0;
	y = tolower (x);
	
	if (y>='a' && y<='z') return 0;
	if (y>='0' && y<='9') return 0;
	if (y==' ') return 0;
	return 1;
}

const char *hexadeci = "0123456789abcdef";

char *encode_mysql (char *input)
{
	int   tlen = 0;
	char *crsr;
	char *result;
	int   pos;
	
	crsr = input;
	while ( (crsr) && (*crsr) )
	{
		++tlen;
		if (needsescape (*crsr)) tlen +=2;
		++crsr;
	}
	
	result = new char[tlen+24];
	
	crsr = input;
	pos  = 0;
	
	while ( (crsr) && (*crsr) )
	{
		dprintf ("encoding: %04i\r", pos);
		if (needsescape (*crsr))
		{
			result[pos++] = '@';
			result[pos++] = hexadeci[(((*crsr) & 0xf0) >> 4)];
			result[pos++] = hexadeci[((*crsr) & 0x0f)];
		}
		else
		{
			result[pos++] = *crsr;
		}
		++crsr;
	}
	result[pos] = 0;
	
	return (result);
}

char *decode_mysql (char *input)
{
	char *result;
	char *crsr;
	int   tlen = 0;
	int   pos  = 0;
	
	int   digit1, digit2;

	crsr = input;
		
	while ( (crsr) && (*crsr) )
	{
		if (*crsr == '@')
		{
			if ( (*(crsr+1)) && (*(crsr+2)) ) crsr +=2;
		}
		++crsr;
		++tlen;
	}
	
	result = new char[tlen+2];
	
	crsr = input;
		
	while ( (crsr) && (*crsr) )
	{
		if (*crsr == '@')
		{
			if ( (*(crsr+1)) && (*(crsr+2)) )
			{
				digit1 = *(crsr+1);
				if ( (digit1 >= '0') && (digit1 <= '9') ) digit1 = digit1 - '0';
				else if ( (digit1 >='a') && (digit1 <='f') ) digit1 = 10 + digit1 - 'a';
				else digit1 = -1;
				
				digit2 = *(crsr+2);
				if ( (digit2 >= '0') && (digit2 <= '9') ) digit2 = digit2 - '0';
				else if ( (digit2 >='a') && (digit2 <='f') ) digit2 = 10 + digit2 - 'a';
				else digit2 = -1;
				
				if ((digit1 < 0) || (digit2 < 0)) result[pos++] = ' ';
				else result[pos++] = ( (digit1 & 0x0f) << 4) + (digit2 & 0x0f);
				++crsr;
				++crsr;
			}
		}
		else
		{
			result[pos++] = *crsr;
		}
		++crsr;
	}
	result[pos] = 0;
	
	return (result);
}

WordList *extract_lines (char *message)
{
	WordList *result = new WordList;
	result->MakeGeneric (message, '\n', 0);
	return (result);
}

char *escape_db (const char *src)
{
	int len = 0;
	int crsr = 0;
	bool done = false;
	int destlen = 0;
	
	len = strlen (src);
	destlen = len + 16;
	len = 0;
	
	char *result = (char *) malloc ((size_t) destlen + 32);
	crsr = 0;
	while (src[crsr])
	{
		if ((src[crsr]<32) || (src[crsr]=='|')) result[len++] = '\\';
		result[len++] = src[crsr++];
		if ( (len+1) >= destlen)
		{
			destlen += 16;
			result = (char *) realloc (result, destlen);
		}
	}
	result[len] = 0;
	return (result);
}

