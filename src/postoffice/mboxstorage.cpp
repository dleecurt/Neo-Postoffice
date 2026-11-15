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

#include "mailstorage.h"
#include "mailbox.h"
#include "strdupp.h"
#include "debug.h"
#include "chop.h"
#include "checksum.h"
#include "status.h"

#include <gdbm.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

extern JobList JOBQ;
extern ITCPipe *PIPE;

MBoxStorage::MBoxStorage (const char *filename)
{
	Filename = Name = strdupp (filename);
	DB = fopen (Filename, "r+");
	if (!DB)
	{
		eprintf ("mboxstorage: could not open: %s\n", strerror (errno));	
	}
	FLock.l_len = 0;
	FLock.l_whence = 0;
	FLock.l_start = 0;
	
	lastsync = time (NULL);
	Root = new StorageNode (this, NULL, 0, 0);
	SFirst = SLast = NULL;
	Size_ = 0;
	Count_ = 0;
	Limit_ = 0;
	SCount_ = 0;
	Lock = new ASemaphore ("mailstorage");
	end_from = 0;
	end_size = 0;
	LoadIndex();
};

MBoxStorage::~MBoxStorage (void)
{
	StoreIndex (true);
	if (DB) fclose (DB);
	dprintf ("mboxstorage [%s] removed\n", Filename);
	delete[] Filename;
};

#define ST_SEEKFROM 0
#define ST_SEEKMSGID 1

void MBoxStorage::Synchronize (void)
{
	long last_from = 0;
	long last_msgid = 0;
	long curpos;
	char msg[256];
	
	int State = ST_SEEKFROM;
	char buf[1024];
	char messageid[256];
	
	if (!DB)
	{
		eprintf ("mboxstorage: not open\n"); return;
	}
	
	LockR();
	
	struct stat xstat;
	fstat (fileno(DB), &xstat);
	
	long tsz = xstat.st_size;
	long stp = ftell (DB);
	
	if ((tsz-stp) < 16) return;
	
	int jid = JOBQ.NewJob ("MBoxStorage");
	sprintf (msg, "Syncing %s", Filename);
	JOBQ.Stat (jid, msg);
	
	while (!feof (DB)) // Go through till the EOF
	{
		buf[0] = 0;
		curpos = ftell (DB);
		if (!fgets (buf, 1023, DB)) buf[0] = 0;
		
		JOBQ.Progress (jid, ((curpos-stp)*100)/(tsz-stp));
		if (JOBQ.Changed (jid)) PIPE->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		
		if (strncmp (buf, "From ", 5) == 0)
		{
			if (last_msgid) // A new beginning, but also the end of another message :)
			{
				if (last_from > end_from)
				{
					end_from = last_from;
					end_size = (size_t) curpos - last_from;
				}
				if (FreeList.Find (messageid) < 0)
				{
					Add (messageid, (size_t) curpos - last_from, 1, last_from);
				}
				last_from = curpos;
				last_msgid = 0;
				messageid[0] = 0;
			}
			else
			{
				dprintf ("[mboxstorage] first From encountered\n");
				last_from = curpos;
			}
			State = ST_SEEKMSGID;
		}
		else if ((State == ST_SEEKMSGID) && (strncasecmp (buf, "Message-ID: ", 12) == 0))
		{
			char *tmp = strchr (buf, ' ') +1;
			strncpy (messageid, tmp, 255);
			choplf (messageid);
			dprintf ("[mboxstorage] Message-ID: %s\n", messageid);
			State = ST_SEEKFROM;
			last_msgid = curpos;
		}
		else if ((State == ST_SEEKMSGID) && (strlen (buf) < 2)) State = ST_SEEKFROM;
		else if (feof (DB))
		{
			if (last_msgid) // A new beginning, but also the end of another message :)
			{
				if (last_from > end_from)
				{
					end_from = last_from;
					end_size = (size_t) curpos - last_from;
				}
				if (FreeList.Find (messageid)<0)
				{
					Add (messageid, (size_t) curpos - last_from, 1, last_from);
				}
				last_msgid = 0;
				messageid[0] = 0;
			}
		}
	}
	fseek (DB, 0L, SEEK_SET);
	
	Unlock();
	
	JOBQ.EndJob (jid);
}

//===============================================================================
// METHOD		CheckSync
// SYNOPSIS		Checks the status of the mboxfile and updates the database if
//				necessary.
//
// ARGUMENTS    Method needs no arguments
//
// RETURNS		method returns no data
//===============================================================================

void MBoxStorage::CheckSync (void)
{
	char buffer[1023];
	if (!DB)
	{
		eprintf ("mboxstorage: not open\n"); return;
	}
	
	Lock->LockOnce();
	
	if (fseek (DB, end_from, SEEK_SET) < 0) // Last known record does not exist anymore. Full resync.
	{
		fseek (DB, 0L, SEEK_SET);
		
		Synchronize();
	}
	else
	{
		if (!fgets (buffer, 1023, DB)) buffer[0] = 0;
		if (strncmp (buffer, "From ", 5) == 0) // Record is still valid
		{
			fseek (DB, end_from + end_size, SEEK_SET); // Only check for new records
			Synchronize();
		}
		else // Record invalid, full resync
		{
			fseek (DB, 0L, SEEK_SET);
			Synchronize();
		}
	}
	
	Lock->UnlockOnce();
}

void MBoxStorage::Store (MailMessage *M)
{
	// Check to see if the message is complete
	
	char seperator[60];
	
	if (M->HeaderNames->Count == 0)
	{
		eprintf ("attempt to store an empty message\n");
		return;
	}
	if (M->Count == 0)
	{
		eprintf ("attempt to store a message with only headers");
		return;
	}
	if (DB)
	{
		int i;
		
		LockRW();
		fseek (DB, 0, SEEK_END);
		fprintf (DB, "From %s\n", M->Envelope->ENV_MAILFROM);
		for (i=0; i< M->HeaderNames->Count; ++i)
		{
			if (strcasecmp (M->HeaderNames->Get(i), "Content-type"))
				fprintf (DB, "%s: %s\n", M->HeaderNames->Get(i), M->HeaderValues->Get(i));
		}
		if (M->Count > 1)
		{
			sprintf (seperator, "-----8<-----CUT-HERE-%02x%02x%05i", rand()&266,rand()&255,rand()&65535);
			fprintf (DB, "Content-type: multipart/mixed; seperator=\"%s\"\n", seperator);
		}
		fprintf (DB, "\n");
		if (M->Count > 1)
		{
			fprintf (DB, "This is a multipart MIME message\n");
		}
		for (i=0; i<M->Count; ++i)
		{
			MailPart *P = M->GetPart (i);
			if (P)
			{
				if (M->Count > 1)
				{
					fprintf (DB, "\n%s\n", seperator);
					fprintf (DB, "Content-Type: %s\n", P->ContentType);
					fprintf (DB, "Content-Description: %s\n\n", P->Description);
				}
				fputs (P->Contents, DB);
			}
			fflush (DB);
		}
		Unlock();
	}
}

bool MBoxStorage::Retrieve (MailMessage *M)
{
	char buffer[1023];
	bool result = false;
	
	if (!DB)
	{
		eprintf ("mboxstorage: not open\n"); return false;
	}
	
	CheckSync();
	
	LockR();
	
	Lock->LockOnce();
	dprintf ("[mboxstorage] retrieve %s\n", M->MsgID);
	StorageNode *Nod = Find_ (M->MsgID);
	if (Nod)
	{
		dprintf ("[mboxstorage] start=%d size=%d\n", Nod->ExtraKey, Nod->Size);
		if (fseek (DB, Nod->ExtraKey, SEEK_SET) == 0) // The start of the record is accessible
		{
			buffer[0] = 0;
			if (!fgets (buffer, 1023, DB)) buffer[0] = 0;
			if (strncmp (buffer, "From ", 5) == 0) // The record is valid
			{
				dprintf ("[mboxstorage] valid from-line\n");
				char *tmp = new char[Nod->Size+16];
				size_t totsize = (Nod->Size - strlen (buffer));
				size_t gotsz   = fread (tmp, 1, totsize, DB);
				if (gotsz < totsize)
				{
					eprintf ("mboxstorage: incomplete fetch (%d < %d)\n", gotsz, totsize);
				}
				else
				{
					tmp[totsize]=0;
					M->ProcessMessage (tmp);
					result = true;
				}
				delete[] tmp;
			}
			else
			{
				dprintf ("[mboxstorage] offset mismatch: (%d) %s", Nod->ExtraKey, buffer);
			}
		}
		else
		{
			dprintf ("[mboxstorage] message not found\n");
		}
	}
	
	Unlock();
	
	Lock->UnlockOnce();
	return result;
}

void MBoxStorage::Delete (const char *x)
{
	eprintf ("mboxstorage: delete not implemented\n");
	FreeList.Add ((char *) x);
	StorageNode *X = Find_ (x);
	
	if (X)
	{
		Remove (X);
	}
}

void MBoxStorage::Delete (StorageNode *X)
{
	eprintf ("mboxstorage: delete not implemented\n");
	FreeList.Add (X->BKey);
	if (X) Remove (X);
}

void MBoxStorage::LoadIndex (void)
{
	char indexfilename[256];
	char buffer[512];
	FILE *IF;
	WordList W;
	
	sprintf (indexfilename, "%s/.postoffice/.mbox_%08x", getenv ("HOME"), checksum (Filename));
	IF = fopen (indexfilename, "r");
	if (IF)
	{
		while (!feof (IF))
		{
			buffer[0] = 0;
			if (!fgets (buffer, 511, IF)) buffer[0] = 0;
			choplf (buffer);
			W.MakeDB (buffer);
			if (W.Count == 3)
			{
				dprintf ("[mboxstorage] index:add (%s,%ld,%ld)\n", W.Get(0), (size_t) atol (W.Get(1)),atol (W.Get(2)));
				if (FreeList.Find (W.Get(0)) < 0)
					Add (W.Get(0), (size_t) atol (W.Get(1)), 1, (unsigned long) atol (W.Get(2)));
				if (atol (W.Get(2)) > end_from)
				{
					end_from = atol (W.Get(2));
					end_size = atol (W.Get(1));
				}
			}
		}
		fclose (IF);
	}
	
	sprintf (indexfilename, "%s/.postoffice/.delete_list_%08x", getenv ("HOME"), checksum (Filename));

	IF = fopen (indexfilename, "r");
	if (IF)
	{
		while (!feof (IF))
		{
			buffer[0] = 0;
			if (!fgets (buffer, 511, IF)) buffer[0] = 0;
			choplf (buffer);
			if (!FreeList.Find (buffer)) FreeList.Add (buffer);
		}
		fclose (IF);
	}
}

void MBoxStorage::StoreIndex (bool x)
{
	char indexfilename[256];
	char oldindexfilename[256];
	FILE *IF;
	
	sprintf (indexfilename, "%s/.postoffice/.mbox_%08x", getenv ("HOME"), checksum (Filename));
	strcpy (oldindexfilename, indexfilename);
	strcat (indexfilename, ".tmp");
	
	IF = fopen (indexfilename, "w");
	dprintf ("[mboxstorage] writing %s\n", indexfilename);
	if (IF)
	{
		StorageNode *N = (StorageNode *) Root->BNext;
		
		while ((N) && (N != Root))
		{
			fprintf (IF, "%s|%zu|%u\n", N->BKey, N->Size, N->ExtraKey);
			N = (StorageNode *) N->BNext;
		}
		fclose (IF);
		rename (indexfilename, oldindexfilename);
	}

	sprintf (indexfilename, "%s/.postoffice/.delete_list_%08x", getenv ("HOME"), checksum (Filename));
	strcpy (oldindexfilename, indexfilename);
	strcat (indexfilename, ".tmp");
	
	IF = fopen (indexfilename, "w");
	dprintf ("[mboxstorage] writing %s\n", indexfilename);
	if (IF)
	{
		for (int i=0;i<FreeList.Count;++i)
		{
			fprintf (IF, "%s\n", FreeList.Get(i));
		}
		fclose (IF);
		rename (indexfilename, oldindexfilename);
	}

}

void MBoxStorage::LockR (void)
{
	FLock.l_type = F_RDLCK;
	if (fcntl (fileno(DB), F_SETLKW, &FLock) == -1)
	{
		eprintf ("!!! serious locking error!\n");
	}
}

void MBoxStorage::LockRW (void)
{
	FLock.l_type = F_WRLCK;
	if (fcntl (fileno(DB), F_SETLKW, &FLock) == -1)
	{
		eprintf ("!!! serious locking error!\n");
	}
}

void MBoxStorage::Unlock (void)
{
	FLock.l_type = F_UNLCK;
	if (fcntl (fileno(DB), F_SETLKW, &FLock) == -1)
	{
		eprintf ("!!! serious locking error!\n");
	}
}

