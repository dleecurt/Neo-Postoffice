#include "status.h"
#include <time.h>
#include <sys/types.h>
#include <string.h>
#include <strings.h>

MailJob::MailJob (const char *name)
{
	strncpy (_Owner, name, 31);
	strcpy (_Status, "Job started");
	_Progress = 0;
	_XFer = 0;
	_Changed = true;
	Started = time (NULL);
}

MailJob::~MailJob (void)
{
}

JobList::JobList (void)
{
	Lock = new ASemaphore ("joblist");

	for (int i=0;i<32;++i)
	{
		Queue[i] = NULL;
	}
}

JobList::~JobList (void)
{
	for (int i=0;i<32;++i)
		if (Queue[i]) EndJob(i);
}


int JobList::NewJob (const char *owner)
{
	Lock->LockRW();
		int i = 0;
		while ( (i<32) && (Queue[i]!=NULL) ) ++i;
		if (i>=32)
		{
			Lock->UnlockRW();
			return -1;
		}
		if (Queue[i])
		{
			i = 0;
			while ( (i<32) && (Queue[i]) ) ++i;
			if (i>=32)
			{
				Lock->UnlockRW();
				return -1;
			}
		}
		Queue[i] = new MailJob (owner);
	Lock->UnlockRW();
	return i;
}

void JobList::EndJob (int jid)
{
	if (jid < 0) return;

	Lock->LockRW();
		if (Queue[jid & 0x1f])
		{
			delete (Queue[jid & 0x1f]);
			Queue[jid & 0x1f] = NULL;
		}
	Lock->UnlockRW();
}

void JobList::Stat (int jid, const char *stat)
{
	if (jid < 0) return;

	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
		strncpy (Queue[jid & 0x1f]->_Status, stat, 63);
		Queue[jid & 0x1f]->_Changed = true;
		//Queue[jid & 0x1f] = NULL;
	}
	Lock->UnlockR();
}

const char *JobList::Stat (int jid)
{
	if (jid < 0) return (const char *) "<invalid jid>";

	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
        Lock->UnlockR();
		return (Queue[jid & 0x1f]->_Status);
	}
	Lock->UnlockR();
	return (const char *) "";
}

const char *JobList::Owner (int jid)
{
	if (jid < 0) return (const char *) "<invalid jid>";

	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
        Lock->UnlockR();
		return (Queue[jid & 0x1f]->_Owner);
	}
	Lock->UnlockR();
	return (const char *) "";
}

void JobList::Progress (int jid, int val)
{
	if (jid < 0) return;
	if (val>100) val=100;
	if (val<0) val=0;

	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
		if (val != Queue[jid & 0x1f]->_Progress) Queue[jid & 0x1f]->_Changed = true;
		Queue[jid & 0x1f]->_Progress = val;
	}
	Lock->UnlockR();
}

int JobList::Progress (int jid)
{
	if (jid < 0) return 0;

	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
        Lock->UnlockR();
		return (Queue[jid & 0x1f]->_Progress);
	}
	Lock->UnlockR();
	return 0;
}

bool JobList::Changed (int jid)
{
	if (jid < 0) return false;
	
	bool res = false;
	
	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
		res = Queue[jid & 0x1f]->_Changed;
		Queue[jid & 0x1f]->_Changed = false;
	}
	Lock->UnlockR();
	return res;
}

void JobList::XFer (int jid, int val)
{
	if (jid < 0) return;

	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
		if (val != Queue[jid & 0x1f]->_XFer) Queue[jid & 0x1f]->_Changed = true;
		Queue[jid & 0x1f]->_XFer = val;
	}
	Lock->UnlockR();
}

int JobList::XFer (int jid)
{
	if (jid < 0) return 0;

	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
        Lock->UnlockR();
		return (Queue[jid & 0x1f]->_XFer);
	}
	Lock->UnlockR();
	return 0;
}

time_t JobList::Started (int jid)
{
	if (jid < 0) return (time_t) 0;

	Lock->LockR();
	if (Queue[jid & 0x1f])
	{
        Lock->UnlockR();
		return (Queue[jid & 0x1f]->Started);
	}
	Lock->UnlockR();
	return (time_t) 0;
}

