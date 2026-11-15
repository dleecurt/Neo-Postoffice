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

#include <sys/types.h>
#include <time.h>
#include "usleep.h"
#include <unistd.h>
#include <sys/types.h>
#ifdef IRIX
#include <bstring.h>
#endif
#include <sys/time.h>
#include <stdio.h>

void _usleep (unsigned int usec)
{
	struct timeval timeout;
	
	timeout.tv_sec = (time_t) 0;
	
#ifndef FREEBSD	
	timeout.tv_usec = usec;
#else
	timeout.tv_usec = 10*usec;
#endif
	select (0, NULL, NULL, NULL, &timeout);
}
