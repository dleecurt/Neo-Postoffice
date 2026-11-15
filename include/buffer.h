#ifndef _ASYNCBUFFER_H
#define _ASYNCBUFFER_H 1
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>

class AsyncBuffer
{
	public:
					 AsyncBuffer (int);
					~AsyncBuffer (void);
					
		void		 Add (const char *);
		void		 Add (u_char *, int);
		void		 Add (unsigned char);
		void		 AddF (const char *, ...);
		void		 Add16 (u_long);
		void		 Add32 (u_long);
		void		 Shift (int);
		void		 Grow (int);
					 
		u_char		*Data;
		int			 Pos;
		
		int			 MaxSize;
};
#endif
