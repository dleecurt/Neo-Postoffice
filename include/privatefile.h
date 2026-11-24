#ifndef _PRIVATEFILE_H
#define _PRIVATEFILE_H 1
//===============================================================================
// Neo-PostOffice: fopen for files with mail in them (index, mbox state,
// rewrites). Created readable by the user only (0600), and an existing file
// is made so too, whatever the umask: they hold subjects, senders and text.
//===============================================================================

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static inline FILE *fopen_private (const char *path, const char *mode)
{
	bool plus = strchr (mode, '+') != NULL;
	int flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | (mode[0] == 'a' ? O_APPEND : O_TRUNC);
	int fd = open (path, flags | O_CLOEXEC, 0600);
	if (fd < 0) return NULL;
	fchmod (fd, 0600);
	FILE *f = fdopen (fd, mode);
	if (!f) close (fd);
	return f;
}

#endif
