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

#include "posixthread.h"
#include <stdio.h>
#include <pthread.h>
#include <signal.h>

PosixThread::PosixThread (void)
{
	Started = 0;
	ThreadID = (pthread_t) 0;
}

PosixThread::~PosixThread (void)
{
	Kill();
}

void PosixThread::Kill (void)
{
	if (Started)
	{
		pthread_kill (ThreadID, SIGKILL);
		pthread_join (ThreadID, NULL);
		Started = 0;
	}
}

void PosixThread::Run (void */*data*/)
{
}

