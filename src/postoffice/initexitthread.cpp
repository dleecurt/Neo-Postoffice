#include "initexitthread.h"
#include "debug.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

InitThread::InitThread (void) : PosixThread()
{
	Started = 0;
	Arg = NULL;
	Result = NULL;
}

InitThread::~InitThread (void)
{
	if (Started) Kill();
}

void InitThread::Run (void *arg)
{
	Arg = (InitArgument *) arg;
	pthread_t *id = new pthread_t;
	if (!Started)
	{
		Result = NULL;
		if (!pthread_create (id, NULL, &InitMain, (void *) this))
		{
			pthread_detach (*id);
			ThreadID = *id;
			Started = 1;
		}
	}
}

void *InitThread::InitMain (void *_obj)
{
	InitThread *obj = (InitThread *) _obj;
	obj->Result = new MailBox (obj->Arg->Pipe, (char *) obj->Arg->Name);
	obj->Started = 0;
	return (NULL);
}

ExitThread::ExitThread (void) : PosixThread()
{
	Started = 0;
	Box = NULL;
}

ExitThread::~ExitThread (void)
{
	if (Started) Kill();
}

void ExitThread::Run (void *arg)
{
	Box = (MailBox *) arg;
	pthread_t *id = new pthread_t;
	if (!Started)
	{
		Quit = false;
		if (!pthread_create (id, NULL, &ExitMain, (void *) this))
		{
			pthread_detach (*id);
			ThreadID = *id;
			Started = 1;
		}
	}
}

void ExitThread::Run (void *arg, bool q)
{
	Box = (MailBox *) arg;
	pthread_t *id = new pthread_t;
	if (!Started)
	{
		Quit = q;
		if (!pthread_create (id, NULL, &ExitMain, (void *) this))
		{
			pthread_detach (*id);
			ThreadID = *id;
			Started = 1;
		}
	}
}

void *ExitThread::ExitMain (void *arg)
{
	ExitThread *obj = (ExitThread *) arg;
	
	if (!obj->Quit) obj->Box->SaveMailBox();
	else obj->Box->Quit();
	obj->Started = 0;
	return NULL;
}

