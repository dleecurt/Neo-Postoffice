#include <stdio.h>
#include <stdarg.h>

#include "semaphore.h"
#include "debug.h"

char DBBUF[1024];
ASemaphore DBLOCK ("debug_buffer");

#ifdef DEBUG

void dprintf (char *fmt, ...)
{
	va_list argptr;
	DBLOCK.LockOnce();
		va_start (argptr, fmt);
		vsprintf (DBBUF, fmt, argptr);
		va_end (argptr);
		printf ("dbg>%s", DBBUF);
	DBLOCK.UnlockOnce();
}

void eprintf (char *fmt, ...)
{
	va_list argptr;
	DBLOCK.LockOnce();
		va_start (argptr, fmt);
		vsprintf (DBBUF, fmt, argptr);
		va_end (argptr);
		printf ("err>%s", DBBUF);
	DBLOCK.UnlockOnce();
}

void iprintf (char *fmt, ...)
{
	va_list argptr;
	DBLOCK.LockOnce();
		va_start (argptr, fmt);
		vsprintf (DBBUF, fmt, argptr);
		va_end (argptr);
		printf ("inf>%s", DBBUF);
	DBLOCK.UnlockOnce();
}

#else

void dprintf (char *fmt, ...)
{
}

void eprintf (char *fmt, ...)
{
	va_list argptr;
	DBLOCK.LockOnce();
		va_start (argptr, fmt);
		vsprintf (DBBUF, fmt, argptr);
		va_end (argptr);
		fputs (DBBUF, stderr);
	DBLOCK.UnlockOnce();
}

void iprintf (char *fmt, ...)
{
	if (VERBOSE)
	{
		va_list argptr;
		DBLOCK.LockOnce();
			va_start (argptr, fmt);
			vsprintf (DBBUF, fmt, argptr);
			va_end (argptr);
			printf ("%s", DBBUF);
		DBLOCK.UnlockOnce();
	}
}

#endif
