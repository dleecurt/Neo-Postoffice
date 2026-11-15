#ifndef _POSIXTHREAD_H
#define _POSIXTHREAD_H 1

#include <pthread.h>

class PosixThread
{
	public:
							 PosixThread (void);
		virtual				~PosixThread (void);

		virtual void		 Run (void *);
		void				 Kill (void);

		pthread_t			 ThreadID;
		int					 Started;
};

#endif
