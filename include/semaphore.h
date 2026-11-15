#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H 1
#include <pthread.h>

class ASemaphore
{
	public:
							 ASemaphore (void);
							 ASemaphore (char *);
							~ASemaphore (void);
		
		void				 LockOnce (void);
		void				 UnlockOnce (void);
		int					 TrylockOnce (void);
		void				 LockR (void);
		void				 LockR (const char *);
		void				 LockRW (void);
		void				 UnlockR (void);
		void				 UnlockRW (void);
		void				 RelockRW (void);
		void				 RelockR (void);
		
	private:
		pthread_mutexattr_t	*attr;
		pthread_mutex_t		*mutex;
		char				*debugname;
		
		int					 Status;
		int					 Numlocks;
		int					 timewait;
		char				*lockers[16];

	#ifdef THREADTRAP
		time_t				 lastcollision;
		int					 collisions;
		void				 Collide (void);
	#endif
};

#define LOCK_NONE			0x00
#define LOCK_READ			0x01
#define LOCK_WRITE			0x02
#define LOCK_PREWRITE		0x03
#define LOCK_READYWRITE		0x04
#define LOCK_WRITEPREWRITE	0x05
#endif
