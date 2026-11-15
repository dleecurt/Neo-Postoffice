#ifndef _MYSQL_H
#define _MYSQL_H 1
#ifndef NO_MYSQL
#include "wordlist.h"

extern "C" int		 dbdriver_init (void);
extern "C" void 	*dbdriver_open (char *, int, char *, char *, char *);
extern "C" WordList	*dbdriver_execute (void *, char *);
extern "C" void		 dbdriver_close (void *);
extern "C" char		*dbdriver_error (void *);
#endif
#endif
