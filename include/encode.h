#ifndef _ENCODE_H
#define _ENCODE_H 1

#include "wordlist.h"

char *encode_mysql (char *);
char *decode_mysql (char *);
WordList *extract_lines (char *);
char *escape_db (const char *);

#endif
