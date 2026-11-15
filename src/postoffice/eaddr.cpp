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

#include "eaddr.h"
#include <string.h>

//===============================================================================
// FUNCTION		eaddr2name
// SYNOPSIS		Extracts the "real name" portion of an email address in one of
// 				the usual formats.
//
// ARGUMENTS	string		(char *)	The FQEA (Fully Qualified Email Address)
//
// RETURNS		A pointer to a string containing the Real Name portion.
// 				Caller is responsible for deleting the data.
//===============================================================================

char *eaddr2name (char *eaddr)
{
	char *result;
	char *tmp;
	char *left, *right;
	if ( (tmp = strchr (eaddr, '<')) )
	{
		if ( tmp - eaddr > 0 ) --tmp;
		while ( *tmp == ' ' ) --tmp;
		left = eaddr;
		right = tmp;
		if ( *left=='\"' ) ++left;
		while ( (*right=='\"') && (right > left) ) --right;
		result = substr (left, right+1);
	}
	else if ( (tmp = strchr (eaddr, '(')) )
	{
		++tmp;
		right = strchr (tmp, ')');
		if ( right ) result = substr (tmp, right);
		else
		{
			result = new char[1]; result[0] = 0;
		}

	}
	else
	{
		result = new char[1]; result[0] = 0;
	}
	return result;
}

//===============================================================================
// FUNCTION		eaddr2addr
// SYNOPSIS		Extracts the "user@domain" portion of an email address in one of
// 				the usual formats.
//
// ARGUMENTS	string		(char *)	The FQEA (Fully Qualified Email Address)
//
// RETURNS		A pointer to a string containing the "user@domain" portion.
// 				Caller is responsible for deleting the data.
//===============================================================================

char *eaddr2addr (char *eaddr)
{
	char *result = NULL;
	char *right;
	char *left;
	left = strchr (eaddr, '<');
	if ( left )
	{
		++left;
		if ( *left )
		{
			right = strchr (left, '>');
			if ( right )
			{
				result = substr (left, right);
			}
		}
	}
	else if ( (right = strchr (eaddr, '(')) )
	{
		if ( right > eaddr ) --right;
		result = substr (eaddr, right);
	}
	else
	{
		left = eaddr;
		while ( *left == ' ' ) ++left;
		right = left + strlen (left);
		while ( (*(right-1) == ' ') && right > left ) *(--right) = 0;
		result = substr (left, right);
	}
	if ( !result )
	{
		result = new char[1];
		result[0] = 0;
	}
	return result;
}
