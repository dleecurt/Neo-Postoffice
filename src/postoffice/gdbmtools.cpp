#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gdbmtools.h"
#include "strdupp.h"
#include "substr.h"

char *gdbm_node2contenttype (const char *dat, int size)
{
	char *eol = strchr (dat, '\n');
	if (eol)
	{
		return substr ((char *) dat, eol);
	}
	return strdupp ("");
}

char *gdbm_node2filename (const char *dat, int size)
{
	char *bol, *eol;
	bol = strchr (dat, '\n');
	if (bol)
	{
		++bol;
		eol = strchr (bol, '\n');
		if (eol)
		{
			return substr (bol, eol);
		}
	}
	return strdupp ("");
}

char *gdbm_node2description (const char *dat, int size)
{
	char *bol, *eol;
	bol = strchr (dat, '\n');
	if (bol)
	{
		++bol;
		bol = strchr (bol, '\n');
		if (bol)
		{
			++bol;
			eol = strchr (bol, '\n');
			if (eol)
			{
				return substr (bol, eol);
			}
		}
	}
	return strdupp ("");
}

char *gdbm_nodecontents (const char *dat, int size)
{
	char *bol;//, *eol;
	bol = strchr (dat, '\n');
	if (bol)
	{
		++bol;
		bol = strchr (bol, '\n');
		if (bol)
		{
			++bol;
			bol = strchr (bol, '\n');
			if (bol)
			{
				++bol;
				return bol;
			}
		}
	}
	return NULL;
}

char *gdbm_createnode (const char *contenttype,
					   const char *filename,
					   const char *description,
					   const char *data,
					   int *size)
{
	int sz = strlen (contenttype)+strlen(filename)+strlen (description)+(*size)+8;
	char *buffer = new char [sz];
	sprintf (buffer, "%s\n%s\n%s\n", contenttype, filename, description);
	char *pos = buffer+strlen(buffer);
	memcpy (pos, data, *size);
	*size += (pos-buffer);
	return buffer;
}
