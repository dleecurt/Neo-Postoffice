#ifndef _GDBMTOOLS_H
#define _GDBMTOOLS_H 1

char *gdbm_node2contenttype (const char *, int);
char *gdbm_node2filename (const char *, int);
char *gdbm_node2description (const char *, int);
char *gdbm_nodecontents (const char *, int);
char *gdbm_createnode (const char *, const char *, const char *, const char *, int *);

#endif
