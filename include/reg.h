#ifndef REG_H
#define REG_H 1
#include <sys/types.h>
int _wild_match(u_char *, u_char *);
int match (char *, char *);
int wild_match (const char *, const char *);
int wild_match_end (const char *, const char *);
#endif
