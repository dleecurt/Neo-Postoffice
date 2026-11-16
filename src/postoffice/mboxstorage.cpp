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
#include <fcntl.h>
#include <stdio.h>

#include <string>
#include <vector>

extern JobList JOBQ;
extern ITCPipe *PIPE;

MBoxStorage::MBoxStorage (const char *filename)
{
	Filename = Name = strdupp (filename);
	ReadOnly = false;
	HaveDotLock = false;
	DB = fopen (Filename, "r+");
	if (!DB && (errno == EACCES || errno == EROFS || errno == EPERM))
	{
		// Neo-PostOffice: a read-only mbox can still be read; deleting
		// then only hides messages.
		DB = fopen (Filename, "r");
		if (DB) ReadOnly = true;
	}
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
	
	if ((tsz-stp) < 16)
	{
		Unlock();	// Neo-PostOffice: the lock was kept on this path
		return;
	}
	
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
	(void) buffer;

	// Neo-PostOffice: the last known record must still be there with its
	// Message-ID (0.7.3 only checked for a "From " line, so a file rewritten
	// by another mail program kept wrong offsets). Then only new records
	// need reading; otherwise the whole file is indexed again.
	StorageNode *last = NULL;
	for (StorageNode *X = (StorageNode *) Root->BNext; X && X != Root; X = (StorageNode *) X->BNext)
		if ((long) X->ExtraKey == end_from) last = X;

	if (last && RecordMatches (end_from, (long) end_size, last->BKey))
	{
		fseek (DB, end_from + end_size, SEEK_SET); // Only check for new records
		Synchronize();
	}
	else
	{
		end_from = 0;
		end_size = 0;
		fseek (DB, 0L, SEEK_SET);
		Synchronize();
	}
	RetryPendingDeletes();
	
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

//===============================================================================
// Neo-PostOffice: deleting messages from the mbox file
// ------------------------------------------------------
// PostOffice 0.7.3 only hid deleted messages ("delete not implemented").
// Now the message's record ("From " line up to the next "From " line) is
// cut out of the file, the way mail programs do it:
//  - the file is locked with fcntl and, where the directory allows, a
//    "<mbox>.lock" dot-lock file (mail servers use both);
//  - the record is found at its indexed offset and checked by Message-ID;
//    if the file has changed meanwhile, it is searched for again;
//  - everything after the record is first copied to a safety file in
//    ~/.postoffice, then written back over the record, and the file is
//    truncated; the safety file is removed when all went well;
//  - the offsets of the following messages in the index are shifted.
// A read-only mbox, or one where any of this fails, falls back to hiding
// the message, as before, with an error message saying why.
//===============================================================================

// Reads one line (any length) at the current position; false at EOF.
static bool read_line (FILE *F, std::string &line)
{
	line.clear();
	int c;
	while ((c = fgetc (F)) != EOF)
	{
		line += (char) c;
		if (c == '\n') return true;
	}
	return !line.empty();
}

// "Message-ID: <x@y>" -> "<x@y>"; "" if the line is not a Message-ID header.
static std::string message_id_of (const std::string &line)
{
	if (strncasecmp (line.c_str(), "Message-ID:", 11) != 0) return "";
	std::string v = line.substr (11);
	size_t a = v.find_first_not_of (" \t"), b = v.find_last_not_of (" \t\r\n");
	return a == std::string::npos ? "" : v.substr (a, b - a + 1);
}

// Is there a record from start, size bytes long, with this Message-ID?
bool MBoxStorage::RecordMatches (long start, long size, const char *msgid)
{
	struct stat st;
	if (fstat (fileno (DB), &st) != 0 || start < 0 || size <= 0 || start + size > st.st_size) return false;
	std::string line;
	if (fseek (DB, start, SEEK_SET) != 0 || !read_line (DB, line) || line.compare (0, 5, "From ") != 0)
		return false;
	bool found = false;
	while (read_line (DB, line) && line != "\n" && line != "\r\n")
		if (message_id_of (line) == msgid) { found = true; break; }
	if (!found) return false;
	// The record must end at the next "From " line or at the end of the file.
	if (start + size == st.st_size) return true;
	return fseek (DB, start + size, SEEK_SET) == 0 && read_line (DB, line) && line.compare (0, 5, "From ") == 0;
}

// Searches the whole file for the record with this Message-ID. Records
// start at lines beginning with "From ", as in Synchronize().
bool MBoxStorage::FindRecord (const char *msgid, long &start, long &size)
{
	std::string line;
	long rec = -1;
	bool in_header = false, match = false;
	fseek (DB, 0, SEEK_SET);
	while (true)
	{
		long pos = ftell (DB);
		bool more = read_line (DB, line);
		if (!more || line.compare (0, 5, "From ") == 0)
		{
			if (match)
			{
				start = rec;
				size = pos - rec;
				return true;
			}
			if (!more) return false;
			rec = pos;
			in_header = true;
		}
		else if (in_header)
		{
			if (line == "\n" || line == "\r\n") in_header = false;
			else if (rec >= 0 && message_id_of (line) == msgid) match = true;
		}
	}
}

bool MBoxStorage::DotLock (std::string &error, bool wait)
{
	std::string lock = std::string (Filename) + ".lock";
	HaveDotLock = false;
	for (int tries = 0; tries < (wait ? 30 : 2); ++tries)
	{
		int fd = open (lock.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd >= 0)
		{
			char pid[32];
			int n = snprintf (pid, sizeof (pid), "%d\n", (int) getpid());
			if (write (fd, pid, n) < 0) { /* the lock is the file itself */ }
			close (fd);
			HaveDotLock = true;
			return true;
		}
		if (errno != EEXIST) return true;	// no rights in the directory: fcntl lock only
		struct stat st;
		if (stat (lock.c_str(), &st) == 0 && time (NULL) - st.st_mtime > 300)
		{
			unlink (lock.c_str());			// stale, left by a crashed program
			continue;
		}
		if (wait) sleep (1);
	}
	error = "the mailbox is locked by another program (" + lock + ")";
	return false;
}

void MBoxStorage::DotUnlock (void)
{
	if (HaveDotLock) unlink ((std::string (Filename) + ".lock").c_str());
	HaveDotLock = false;
}

bool MBoxStorage::Expunge (const char *msgid, std::string &error, bool wait)
{
	if (!DB) { error = "the mbox file is not open"; return false; }
	if (ReadOnly) { error = "the mbox file is read-only"; return false; }
	// A safety file left by an interrupted rewrite may hold the only good
	// copy of the end of the mbox: do nothing until it has been dealt with.
	char sname[512];
	snprintf (sname, sizeof (sname), "%s/.postoffice/.mbox_rewrite_%08x.tmp", getenv ("HOME"), checksum (Filename));
	if (access (sname, F_OK) == 0)
	{
		error = std::string ("an earlier delete from this file was interrupted.\nThe end of the file as it was "
							 "then is in ") + sname + ";\ncheck the mbox file, then remove that file";
		return false;
	}
	if (!DotLock (error, wait)) return false;
	LockRW();

	bool ok = false, reindex = false;
	std::string safety;
	do
	{
		long start = -1, size = 0;
		StorageNode *N = Find_ (msgid);
		if (N && RecordMatches ((long) N->ExtraKey, (long) N->Size, msgid))
		{
			start = (long) N->ExtraKey;
			size = (long) N->Size;
		}
		else if (FindRecord (msgid, start, size))
			reindex = true;		// the file changed since it was indexed
		else
		{
			error = "the message is no longer in the mbox file";
			break;
		}

		struct stat st;
		if (fstat (fileno (DB), &st) != 0) { error = strerror (errno); break; }
		long tail = st.st_size - (start + size);

		// 1. Everything after the record to a safety file.
		safety = sname;
		FILE *S = fopen (sname, "w+");
		if (!S) { error = std::string ("cannot create ") + sname + ": " + strerror (errno); break; }
		std::vector<char> buf (65536);
		fseek (DB, start + size, SEEK_SET);
		long copied = 0;
		while (copied < tail)
		{
			size_t n = fread (buf.data(), 1, std::min ((long) buf.size(), tail - copied), DB);
			if (n == 0 || fwrite (buf.data(), 1, n, S) != n) break;
			copied += (long) n;
		}
		if (copied != tail || fflush (S) != 0 || fsync (fileno (S)) != 0)
		{
			error = std::string ("cannot write ") + sname;
			fclose (S);
			unlink (sname);
			safety.clear();
			break;
		}

		// 2. Write it back over the record and cut the file.
		fseek (S, 0, SEEK_SET);
		fseek (DB, start, SEEK_SET);
		long written = 0;
		while (written < tail)
		{
			size_t n = fread (buf.data(), 1, buf.size(), S);
			if (n == 0 || fwrite (buf.data(), 1, n, DB) != n) break;
			written += (long) n;
		}
		fclose (S);
		if (written != tail || fflush (DB) != 0 || ftruncate (fileno (DB), start + tail) != 0)
		{
			error = std::string ("writing the mbox file failed (") + strerror (errno) +
					").\nThe end of the file is saved in " + safety;
			safety.clear();		// keep the safety file
			break;
		}
		fsync (fileno (DB));
		unlink (sname);
		safety.clear();

		// 3. The index: drop the record, shift the ones after it.
		if (N) Remove (N);
		end_from = 0;
		end_size = 0;
		for (StorageNode *X = (StorageNode *) Root->BNext; X && X != Root; X = (StorageNode *) X->BNext)
		{
			if ((long) X->ExtraKey > start) X->ExtraKey -= (unsigned int) size;
			if ((long) X->ExtraKey >= end_from)
			{
				end_from = X->ExtraKey;
				end_size = X->Size;
			}
		}
		ok = true;
	} while (false);

	fseek (DB, 0, SEEK_SET);
	Unlock();
	DotUnlock();
	if (ok && reindex)
	{
		// Offsets from before the outside change are not to be trusted.
		end_from = 0;
		end_size = 0;
		fseek (DB, 0L, SEEK_SET);
		Synchronize();
	}
	if (ok) StoreIndex (true);
	return ok;
}

void MBoxStorage::Delete (const char *x)
{
	StorageNode *X = Find_ (x);
	if (!X) return;		// not in this mbox file

	Lock->LockOnce();
	std::string error;
	bool done = Expunge (x, error);
	Lock->UnlockOnce();
	if (done) return;

	// Could not change the file: hide the message, as PostOffice 0.7.3 did.
	// Unless the file is read-only, the delete is tried again at each check.
	eprintf ("mboxstorage: cannot delete %s from %s: %s\n", x, Filename, error.c_str());
	std::string msg = std::string ("Cannot delete the message from ") + Filename + ":\n" + error +
					  (ReadOnly ? ".\nIt is hidden instead."
								: ".\nIt is hidden, and will be deleted at a later check.");
	PIPE->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mbox", msg.c_str());
	FreeList.Add ((char *) x);
	if ((X = Find_ (x))) Remove (X);
	StoreIndex (true);
}

// Messages hidden because deleting them failed (the mailbox was locked):
// try again, without waiting for a lock. Called with Lock held.
void MBoxStorage::RetryPendingDeletes (void)
{
	if (ReadOnly || !DB) return;
	for (int i = 0; i < FreeList.Count; )
	{
		std::string id = FreeList.Get (i), error;
		long start, size;
		LockR();
		bool there = FindRecord (id.c_str(), start, size);
		Unlock();
		if (!there || Expunge (id.c_str(), error, false))
		{
			FreeList.Delete (i);	// deleted now, or gone by other means
			StoreIndex (true);
			continue;
		}
		++i;
	}
	fseek (DB, 0L, SEEK_SET);
}

void MBoxStorage::Delete (StorageNode *X)
{
	if (X) Delete (std::string (X->BKey).c_str());
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
			if (*buffer && FreeList.Find (buffer) < 0) FreeList.Add (buffer);	// was !Find (inverted)
		}
		fclose (IF);
	}
}

void MBoxStorage::StoreIndex (bool /*x*/)
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

