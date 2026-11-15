#ifndef _MAILERTHREAD_H
#define _MAILERTHREAD_H 1

#include "posixthread.h"

class MailerThread : public PosixThread
{
	public:
						~MailerThread (void);
		void			 Run (void *);
		static void		*MailerMain (void *);
};

#endif
