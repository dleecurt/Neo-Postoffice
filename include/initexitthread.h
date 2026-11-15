#ifndef _INITEXITTHREAD_H
#define _INITEXITTHREAD_H 1

#include "posixthread.h"
#include "mailbox.h"

struct InitArgument
{
	class ITCPipe	*Pipe;
	const char		*Name;
};

class InitThread : public PosixThread
{
public:
					 InitThread (void);
					~InitThread (void);
	void			 Run (void *);
	static void		*InitMain (void *);
	
	MailBox			*Result;
	InitArgument	*Arg;
};

class ExitThread : public PosixThread
{
public:
					 ExitThread (void);
					~ExitThread (void);
	void			 Run (void *);
	void			 Run (void *, bool);
	static void		*ExitMain (void *);
	
	bool			 Quit;
	MailBox			*Box;
};

/*
class ExitThread : public PosixThread
{
					~ExitThread (void);
	void			 Run (void *);
	static void		 ExitMain (void *);
};
*/

#endif

