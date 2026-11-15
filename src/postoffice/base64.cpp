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

//#include <string.h>
#include <sys/types.h>
#include <string.h>
#include "base64.h"
#include "substr.h"

//===============================================================================
// FUNCTION		Base64
// SYNOPSIS		Converts a string of data encoded with the base64 system into
// 				a binary object.
//
// ARGUMENTS	Source		(char *)        The encoded string
// 				SourceLen   (u_long)        Size of the encoded string
//
// RETURNS		A pointer to the block of binary data is returned. The caller
//				is expected to free the memory when no longer needed.
//===============================================================================

void *Base64 (unsigned char *Source, unsigned long SourceLen, unsigned long *Destlen)
{
	//_OBJECT = BASE64_O;

	unsigned long   Length;
	char            mych;
	void           *result = (void *) new char[(size_t) (Length = 4 + ((SourceLen * 3) / 4))+2];
	char           *resultch = (char *) result;
	short           quant = 0;

	memset (result, 0, (size_t) Length);

	while (SourceLen--)
	{
		mych = *Source++;

		if (mych >='A' && mych <='Z')
			mych -= 'A';
		else if (mych >='a' && mych <='z')
			mych -= 'a' - 26;
		else if (mych >='0' && mych <='9')
			mych -= '0' - 52;
		else if (mych == '+')
			mych = 62;
		else if (mych == '/')
			mych = 63;
		else if (mych == '=')
		{
			switch (quant++)
			{
			case 3:
				quant = 0;
				break;

			case 2:
				if (*Source == '=')
					break;

			default:
				delete[] (result);
				return NULL;
			}
			continue;
		}
		else
			continue;

		switch (quant++)
		{
		case 0:
			*resultch = mych << 2;		// byte 1: high 6 bits
			break;

		case 1:
			*resultch++ |= mych >> 4;	// byte 1: low 2 bits
			*resultch = mych << 4;		// byte 2: high 4 bits
			break;

		case 2:
			*resultch++ |= mych >> 2;	// byte 2: low 4 bits
			*resultch = mych << 6;		// byte 3: high 2 bits
			break;

		case 3:
			*resultch++ |= mych;		// byte 3: low 6 bits
			quant = 0;					// and round and round and round :)
			break;
		}
	}
	*Destlen = (unsigned long) (resultch - (char *) result);
	return result;
}

void *UUdecode (unsigned char *Source, unsigned long SourceLen, unsigned long *Destlen)
{
	//_OBJECT = BASE64_O;

	unsigned long   Length;
	char            mych;
	void           *result = (void *) new char[(size_t) (Length = 4 + ((SourceLen * 3) / 4))+2];
	char           *resultch = (char *) result;
	short           quant = 0;
	int				done = 0;

	memset (result, 0, (size_t) Length);

	char *crsr = (char *) Source;
//	char *ln;
	char *eol;
//	int xx;

	//Source[SourceLen-1] = 0;

	while ((crsr) && (!done))
	{
		while (*crsr && (*crsr < ' ')) ++crsr;
		eol = strchr (crsr, '\n');
		//ln = substr (crsr, eol);
		//delete[] ln;
		if (eol)
		{
			int amt = *crsr - ' ';
			if ((eol - crsr) < 2) amt = 0;
			if (amt<=0)
			{
				crsr=0;
				done=1;
			}
			while ((amt > 0) && ( (unsigned long) (resultch - ((char *) result)) < Length))
			{
				++crsr;
				if (!(*crsr)) amt = 0;
				else if (( (*crsr) >= ' ' ) && ( (*crsr) <= (' '+077+1) ))
				{
					mych = ((*crsr) - ' ') & 077;
					switch (quant++)
					{
						case 0:
							*resultch = mych << 2;		// byte 1: high 6 bits
							break;
				
						case 1:
							*resultch++ |= mych >> 4;	// byte 1: low 2 bits
							*resultch = mych << 4;		// byte 2: high 4 bits
							break;
				
						case 2:
							*resultch++ |= mych >> 2;	// byte 2: low 4 bits
							*resultch = mych << 6;		// byte 3: high 2 bits
							break;
				
						case 3:
							*resultch++ |= mych;		// byte 3: low 6 bits
							quant = 0;					// and round and round and round :)
							break;
					}
				}
				else
				{
					amt = 0;
				}
			}
			if (crsr) crsr = eol+1;
		}
		else (crsr = NULL);
	}
	*Destlen = (unsigned long) (resultch - (char *) result);
	return result;
}
