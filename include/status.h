#ifndef _STATUS_H
#define _STATUS_H 1

#include <time.h>
#include <sys/types.h>
#include "semaphore.h"

class MailJob
{
	public:
							 MailJob (const char *);
							~MailJob (void);

		char				 _Owner[32];
		char				 _Status[64];
		int					 _Progress;
		int					 _XFer;
		bool				 _Changed;

		time_t				 Started;
};

class JobList
{
	public:
							 JobList (void);
							~JobList (void);
		int					 NewJob (const char *);
		void				 EndJob (int);

		const char			*Owner (int);

		void				 Stat (int, const char *);
		const char			*Stat (int);

		void				 Progress (int, int);
		int					 Progress (int);

		void				 XFer (int, int);
		int					 XFer (int);

		time_t				 Started (int);
		bool				 Changed (int);

	private:
		MailJob				*Queue[32];
		ASemaphore			*Lock;
};

#endif

