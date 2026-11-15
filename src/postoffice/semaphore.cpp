//===============================================================================
// POSTOFFICE - X11 Office Communication Handler
// ---------------------------------------------
// All sourcecode, unless differently annotated, written by Pim van Riezen
// Sourcecode and resulting binaries are covered by the GNU Lesser General Public
// License (GNU LGPL). For more information and the literal text of the license
// agreement, check the Free Software Foundation's website on http://www.fsf.org/
// 
// Copyright (C) 1999 Mad Sciene Labs - Rotterdam, The Netherlands
//===============================================================================

#include "semaphore.h"
#include "defs.h"
#include <unistd.h>
#include "usleep.h"

#ifdef THREADTRAP
  #include <stdio.h>
  #include <time.h>
#endif

ASemaphore::ASemaphore (void)
{
	attr = new pthread_mutexattr_t;
	mutex = new pthread_mutex_t;
	pthread_mutexattr_init (attr);
	pthread_mutex_init (mutex, attr);
	for (int i=0;i<16;++i) lockers[i] = (char *) "<empty slot>";

#ifdef THREADTRAP
	lastcollision = time (NULL);
	collisions = 0;
#endif

	debugname = (char *) "unidentified";

	Status = LOCK_NONE;
	Numlocks = 0;
	timewait = 20;
}

ASemaphore::ASemaphore (char *dname)
{
	attr = new pthread_mutexattr_t;
	mutex = new pthread_mutex_t;
	pthread_mutexattr_init (attr);
	pthread_mutex_init (mutex, attr);

#ifdef THREADTRAP
	lastcollision = time (NULL);
	collisions = 0;
#endif

	debugname = dname;

	Status = LOCK_NONE;
	Numlocks = 0;
	timewait = 10;
}


ASemaphore::~ASemaphore (void)
{
	pthread_mutex_destroy (mutex);
	delete (mutex);
	pthread_mutexattr_destroy (attr);
	delete (attr);
	Status = LOCK_NONE;
	Numlocks = 0;
}

void ASemaphore::LockOnce (void)
{
	pthread_mutex_lock (mutex);
}

void ASemaphore::UnlockOnce (void)
{
	pthread_mutex_unlock (mutex);
}

int ASemaphore::TrylockOnce (void)
{
	int             res;

	res = pthread_mutex_trylock (mutex);
	if (res == 0)
		return 1;
	return 0;
}

void ASemaphore::LockR (void)
{
	int             goahead = 0;
	int             turns = 0;

	while (!goahead)
	{
		pthread_mutex_lock (mutex);
		if ((Status == LOCK_NONE) || (Status == LOCK_READ))
		{
			Status = LOCK_READ;
			lockers[Numlocks] = "<unknown>";
			++Numlocks;
			goahead = 1;
		}
#ifdef THREADTRAP
		if (!goahead)
			Collide ();
#endif
		pthread_mutex_unlock (mutex);
		if (!goahead)
		{
			_usleep (timewait);
			++turns;
		}
	}
	if (turns > 1)
	{
		pthread_mutex_lock (mutex);
		timewait = (turns) * timewait;
		pthread_mutex_unlock (mutex);
	}
}

void ASemaphore::LockR (const char *locker)
{
	int             goahead = 0;
	int             turns = 0;

	while (!goahead)
	{
		pthread_mutex_lock (mutex);
		if ((Status == LOCK_NONE) || (Status == LOCK_READ))
		{
			Status = LOCK_READ;
			lockers[Numlocks] = (char *) locker;
			++Numlocks;
			goahead = 1;
		}
#ifdef THREADTRAP
		if (!goahead)
			Collide ();
#endif
		pthread_mutex_unlock (mutex);
		if (!goahead)
		{
			_usleep (timewait);
			++turns;
		}
	}
	if (turns > 1)
	{
		pthread_mutex_lock (mutex);
		timewait = (turns) * timewait;
		pthread_mutex_unlock (mutex);
	}
}

void ASemaphore::LockRW (void)
{
	int             goahead = 0;
	int             turns = 0;

	while (!goahead)
	{
		pthread_mutex_lock (mutex);
		if ((Status == LOCK_NONE) || (Status == LOCK_READYWRITE))
		{
			Status = LOCK_WRITE;
			goahead = 1;
		}
		else if (Status == LOCK_READ)
			Status = LOCK_PREWRITE;
#ifdef THREADTRAP
		if (!goahead)
			Collide ();
#endif
		pthread_mutex_unlock (mutex);
		if (!goahead)
		{
			_usleep (timewait);
			++turns;
		}
	}
	if (turns > 1)
	{
		pthread_mutex_lock (mutex);
		timewait = (turns) * timewait;
		pthread_mutex_unlock (mutex);
	}

	// dprintf (5,"THREAD_LOCK_RW_SUCCESS %x\n", this);
}

void ASemaphore::RelockRW (void)					// From LOCK_READ or LOCK_PREWRITE
{
	int             goahead = 0;
	int             turns = 0;

	// dprintf (3,"THREAD_RELOCK_RW %x\n", this);

	pthread_mutex_lock (mutex);
	--Numlocks;
	// dprintf (4,"THREAD_NUMLOCKS %d\n", Numlocks);
	if (Numlocks == 0)
	{
		Status = LOCK_NONE;
	}
	while (!goahead)
	{
		if ((Status == LOCK_NONE) || (Status == LOCK_READYWRITE))
		{
			Status = LOCK_WRITE;
			goahead = 1;
		}
		else if (Status == LOCK_READ)
			Status = LOCK_PREWRITE;
#ifdef THREADTRAP
		if (!goahead)
			Collide ();
#endif
		pthread_mutex_unlock (mutex);
		if (!goahead)
		{
			_usleep (timewait);
			++turns;
		}
		pthread_mutex_lock (mutex);
	}
	if (turns > 1)
		timewait = (turns) * timewait;
	pthread_mutex_unlock (mutex);
}

void ASemaphore::RelockR (void)					// From LOCK_WRITE
{
	pthread_mutex_lock (mutex);
	++Numlocks;
	Status = LOCK_READ;
	pthread_mutex_unlock (mutex);
}

void ASemaphore::UnlockR (void)
{
	pthread_mutex_lock (mutex);
	--Numlocks;
	if (Numlocks == 0)
	{
		if (Status == LOCK_PREWRITE)
			Status = LOCK_READYWRITE;
		else
			Status = LOCK_NONE;
	}
	pthread_mutex_unlock (mutex);
}

void ASemaphore::UnlockRW (void)
{
	pthread_mutex_lock (mutex);
	Status = LOCK_NONE;
	pthread_mutex_unlock (mutex);
}

#ifdef THREADTRAP

void ASemaphore::Collide (void)
{
	FILE           *tp;

	if (time (NULL) - lastcollision == 0)
	{
		if (++collisions > 10)
		{
			tp = fopen ("threadpanic.log", "a");
			if (tp)
			{
				fprintf (tp, "%d panic: %s (%i)\n", time (NULL), debugname, collisions);
				fclose (tp);
			}
		}
	}
	else
	{
		lastcollision = time (NULL);
		collisions = 0;
	}
}

#endif
