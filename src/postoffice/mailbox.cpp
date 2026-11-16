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

#include <string>
#include <vector>
#include "mailbox.h"
#include "wordlist.h"
#include "var.h"
#include "semaphore.h"
#include "chop.h"
#include "checksum.h"
#include "usleep.h"
#include "strdupp.h"

#include "debug.h"

#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#if defined(SOLARIS) || defined(IRIX)
  #include <strings.h>
#endif

extern Var PREFERENCES;

#include "encode.h"

MailBox::MailBox (ITCPipe *ui, char *name)
{
    int i;
    struct stat mystat;

    // And our name shall be such and such

    Name = new char[strlen(name)+1];
    strcpy (Name, name);

    // Seperate locks for the message-list and the mailview-list

    Lock = new ASemaphore ("mailbox::lock");
    VLock = new ASemaphore ("mailbox::vlock");

    // Store a local pointer to the preferences

    Var *PREFROOT = PREFERENCES.
                    Get_collection ("postoffice")->
                    Get_collection ("mailboxes")->
                    Get_collection (Name);

    // Initialize the linked list pointers

    Root = NULL;
    Root = new MailMessage (this, NULL);
    VFirst = VLast = NULL;

    // Set up the ITCPipe for communication with the collector thread

    ToCollector = new ITCPipe;

    // Inherit the ITCPipe to the GUI

    ToUI = ui;

    // Create the scheduler for automated actions

    Schedule = new Scheduler;

    // Register the "Save Mailbox" event with the scheduler

    Schedule->Schedule (120, 120, SCHED_SAVEBOX, NO_SUBCLASS, "");

    // See if there's an interval for automatic retrieval set, if so then schedule

    i = PREFROOT->Get_int ("interval");
    if (i) Schedule->Schedule (5, i, SCHED_CHECKNEW, NO_SUBCLASS, "");

    // Initialize counters

    MemoryUsage = 0;
    Count = 0;
    CacheToDisk = 0;
    NewCount = 0;
    Open = 0;

    // Create collector thread

    Collector = new MailerThread;

    // Extract the directory from the prefs, or make on up if it wasn't there.

    char *dp = PREFROOT->Get_char ("prefix");
    if (strlen (dp)) DirectoryPrefix = strdupp (dp);
    else
    {
        const char *HOME = getenv ("HOME");
        if (!HOME) HOME = "/tmp";
        DirectoryPrefix = new char [strlen (HOME) + strlen ("/.postoffice/.box") + strlen (Name) + 3];
        sprintf (DirectoryPrefix, "%s/.postoffice/%s.box", HOME, Name);
        PREFROOT->Set_char ("prefix", DirectoryPrefix);
    }

    char path[256];
    const char *HOME = getenv ("HOME");

    if (!HOME) HOME = "/tmp";

    if (stat (HOME, &mystat)==0)
    {
        sprintf (path, "%s/.postoffice", HOME);
        if (stat (path, &mystat)!=0)
        {
            mkdir (path, 0700);
        }
        if (stat (DirectoryPrefix, &mystat)!=0) mkdir (DirectoryPrefix, 0700);
    }

#ifdef USE_GDBM
    // Neo-PostOffice: each mailbox stores its mail either in local GDBM
    // files (the default) or in PostgreSQL ("storage" = "pgsql").
    if (strcasecmp (PREFROOT->Get_char ("storage"), "pgsql") == 0)
    {
        std::string storagename = std::string ("pgsql:") + Name;
        DB = STORAGE.Open (storagename.c_str(), this, STORAGE_PGSQL, Name,
                           PREFROOT->Get_char ("pgsql_conninfo"));
    }
    else
    {
        char *gdbm_filename = new char[strlen(DirectoryPrefix)+strlen("/box.gdbm")+1];
        sprintf (gdbm_filename, "%s/box.gdbm", DirectoryPrefix);
        DB = STORAGE.Open (gdbm_filename, this, STORAGE_GDBM);
        delete[] gdbm_filename;
    }
#endif

    // Initialize the collector with the proper handler

    if (strcasecmp (PREFROOT->Get_char ("protocol"), "pop3") == 0) // POP3 box
    {
        Handler = new POP3Handler (this, PREFROOT);
    }
    else if (strcasecmp (PREFROOT->Get_char ("protocol"), "sql") == 0 ||
             strcasecmp (PREFROOT->Get_char ("protocol"), "mysql") == 0)
    {
        Handler = new SQLHandler (this, PREFROOT);
    }
    else if (strcasecmp (PREFROOT->Get_char ("protocol"), "nntp") == 0)
    {
        Handler = new NNTPHandler (this, PREFROOT);
    }
    else if (strcasecmp (PREFROOT->Get_char ("protocol"), "mbox") == 0)
    {
        Handler = new MBoxHandler (this, PREFROOT);
    }
	else if (strcasecmp (PREFROOT->Get_char ("protocol"), "imap4") == 0)
	{
		Handler = new IMAP4Handler (this, PREFROOT);
	}
	
    // Open all views

    WordList *Views = PREFROOT->Get_wordlist ("folders");

    for (i=0;i<Views->Count;++i)
    {
        CreateMailView (Views->Get(i));
    }

    // Load MailBox into memory

    LoadMailBox();
	ClearBiff();
	
#ifdef USE_GDBM
    // Without usable storage, fetching mail could lose it (POP3 may delete
    // it from the server), so the collector is not started.
    if (DB && !DB->Usable())
    {
        std::string msg = std::string ("Mailbox ") + Name + " is not available: " +
                          DB->LastError() + "\nNo mail will be fetched for it.";
        ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mailbox", msg.c_str());
        return;
    }
#endif

    // Start the collector thread

    Collector->Run((void *) this);

    // An SQL mailbox reads its message list right away: that creates its
    // folders and brings in changes made by other clients meanwhile.
    if (dynamic_cast<SQLHandler *> (Handler))
        ToCollector->PostMessage (CLASS_CHECKGROUP, NO_SUBCLASS, "*");
}

MailBox::MailBox (ITCPipe *ui)
{
//	int i;
    struct stat mystat;

    Name = new char[16];
    strcpy (Name, "OutBox");

    Lock = new ASemaphore ("mailbox::lock");
    VLock = new ASemaphore ("mailbox::vlock");

    Root = NULL;
    Root = new MailMessage (this, NULL);
    VFirst = VLast = NULL;

    ToCollector = new ITCPipe;
    ToUI = ui;
    Schedule = new Scheduler;

    Schedule->Schedule (120, 120, SCHED_SAVEBOX, NO_SUBCLASS, "");
    Schedule->Schedule (10, 15, SCHED_CHECKNEW, NO_SUBCLASS, "");   
    MemoryUsage = 0;
    Count = 0;
    CacheToDisk = 0;
    Collector = new MailerThread;
    NewCount = 0;
    Open = 0;

    const char *HOME = getenv ("HOME");
    if (!HOME) HOME = "/tmp";
    DirectoryPrefix = new char [strlen (HOME) + strlen ("/.postoffice/.box") + strlen (Name) + 3];
    sprintf (DirectoryPrefix, "%s/.postoffice/%s.box", HOME, Name);

    char path[256];

    if (!HOME) HOME = "/tmp";

    if (stat (HOME, &mystat)==0)
    {
        sprintf (path, "%s/.postoffice", HOME);
        if (stat (path, &mystat)!=0)
        {
            mkdir (path, 0700);
        }
        if (stat (DirectoryPrefix, &mystat)!=0) mkdir (DirectoryPrefix, 0700);
    }

#ifdef USE_GDBM
    char *gdbm_filename = new char[strlen(DirectoryPrefix)+strlen("/box.gdbm")+1];
    sprintf (gdbm_filename, "%s/box.gdbm", DirectoryPrefix);
    DB = STORAGE.Open (gdbm_filename, this, STORAGE_GDBM);
    delete[] gdbm_filename;
#endif

    Handler = new OutBoxHandler (this, PREFERENCES.Get_collection ("postoffice")->Get_collection ("network"));
    CreateMailView ("Queue");
    CreateMailView ("Sent");

    LoadMailBox();
    Collector->Run((void *) this);
}

MailBox::~MailBox (void)
{
    // Save the index first: with PostgreSQL storage, saving needs the storage
    // that STORAGE.Close() may delete.
    SaveMailBox();

#ifdef USE_GDBM
    STORAGE.Close (this);
    DB = NULL;
#endif

    Collector->Kill();
}

/*
#define ENVELOPE_I_READ			0
#define ENVELOPE_I_PARENTID		1
#define ENVELOPE_MAILFROM		2
#define ENVELOPE_NAMEFROM		3
#define ENVELOPE_SUBJECT		4
#define ENVELOPE_DATE			5
#define ENVELOPE_NEWSGROUPS		6
#define ENVELOPE_GROUPS			6
*/

// Reads the mailbox's index file into entries.
static void read_index_file (const char *DirectoryPrefix, std::vector<IndexEntry> &entries)
{
    FILE *IDX;
    char buf[1024];
    WordList Line, Subs;
    std::string fname = std::string (DirectoryPrefix) + "/index";

    if (!(IDX = fopen (fname.c_str(), "r"))) return; // No index yet

    while (!feof (IDX)) // Browse through the index file
    {
        buf[0] = 0;
        if (!fgets (buf, 1023, IDX)) buf[0] = 0; // Read a line
        buf[1023] = 0;
        if ((strlen (buf)>1)&&(!feof (IDX)))
        {
            choplf (buf);

            Line.MakeDB (buf); // The arguments to a message in the index
            Subs.MakeGeneric (Line.Get(6),','); // The names of all views it was a part of

            IndexEntry e;
            e.MsgID    = Line.Get(0);
            e.ParentID = Line.Get(1);
            e.MailFrom = Line.Get(2);
            e.NameFrom = Line.Get(3);
            e.Subject  = Line.Get(4);
            e.Date     = Line.Get(5);
            for (int i=0;i<Subs.Count;++i) e.Folders.push_back (Subs.Get(i));
            e.Flags    = Line.GetInt(7);
            e.Read     = Line.GetInt(8);
            e.Count    = Line.GetInt(9);
            e.Received = atol (Line.Get (10));
            entries.push_back (e);
        }
    }
    fclose (IDX);
}

void MailBox::LoadMailBox (void)
{
    std::vector<IndexEntry> entries;
    MailView *V;

#ifdef USE_GDBM
    if (DB && DB->KeepsMailboxIndex()) // The storage keeps the index (PostgreSQL)
    {
        if (!DB->LoadMailboxIndex (entries)) return;
    }
    else
#endif
    read_index_file (DirectoryPrefix, entries);

    for (const IndexEntry &e : entries)
    {
        std::string groups;
        for (size_t i=0;i<e.Folders.size();++i)
        {
            if (i) groups += ',';
            groups += e.Folders[i];
        }

        // Initialize a MailMessage with all information from the index

        MailMessage *M = new MailMessage (this, e.MsgID.c_str());
        M->Envelope->Set (ENVELOPE_I_PARENTID, e.ParentID.c_str());
        M->Envelope->Set (ENVELOPE_MAILFROM, e.MailFrom.c_str());
        M->Envelope->Set (ENVELOPE_NAMEFROM, e.NameFrom.c_str());
        M->Envelope->Set (ENVELOPE_SUBJECT, e.Subject.c_str());
        M->Envelope->Set (ENVELOPE_DATE, e.Date.c_str());
        M->Envelope->Set (ENVELOPE_GROUPS, groups.c_str());
        M->Envelope->SetInt (ENVELOPE_I_READ, e.Read);
        M->Count = e.Count;
        M->Flags = e.Flags & (MMFL_ORIGINAL | MMFL_HAVEBODY | MMFL_ONDISK);
        M->ReceiveDate = e.Received;
        if (!M->ReceiveDate) M->ReceiveDate = time (NULL) - 864000;

        for (size_t i=0;i<e.Folders.size();++i)
        {
            V = GetView (e.Folders[i].c_str()); // Find the named view
            if (!V) // If it doesn't exist
            {
                dprintf ("%s does not exist, using Deliver()\n", e.Folders[i].c_str());
                M->LoadHeaders();
                UnTie (M); // Undo direct deliveries
                Deliver (M); // And fall back to filter rules
                break; // Exit direct delivery cycle
            }
            else // But of course it exists
            {
                V->Associate (M); // Direct delivery
            }
        }
    }
}

void MailBox::SaveMailBox (void)
{
#ifdef USE_GDBM
    if (DB && DB->KeepsMailboxIndex()) // The storage keeps the index (PostgreSQL)
    {
        std::vector<IndexEntry> entries;
        ViewListNode *V;

        Lock->LockR();
        VLock->LockR();
        for (MailMessage *M = (MailMessage *) Root->BNext; M && (M!=Root); M = (MailMessage *) M->BNext)
        {
            IndexEntry e;
            e.MsgID    = M->MsgID;
            e.ParentID = M->Envelope->Get (ENVELOPE_I_PARENTID);
            e.MailFrom = M->Envelope->Get (ENVELOPE_MAILFROM);
            e.NameFrom = M->Envelope->Get (ENVELOPE_NAMEFROM);
            e.Subject  = M->Envelope->Get (ENVELOPE_SUBJECT);
            e.Date     = M->Envelope->Get (ENVELOPE_DATE);
            for (int i=0;i<M->Subscribers->Count();++i)
                if ((V=M->Subscribers->Get(i))) e.Folders.push_back (V->View->Name);
            e.Flags    = M->Flags;
            e.Read     = M->Envelope->GetInt (ENVELOPE_I_READ);
            e.Count    = M->Count;
            e.Received = (long) M->ReceiveDate;
            entries.push_back (std::move (e));
        }
        Lock->UnlockR();
        VLock->UnlockR();

        // Written outside the mailbox locks: this talks to the server.
        DB->SaveMailboxIndex (entries);
        return;
    }
#endif

    struct stat mystat;
    char path[256];
	char oldpath[256];
    const char *HOME = getenv ("HOME");

    if (!HOME) HOME = "/tmp";

	// build the path to the index file

    if (stat (HOME, &mystat)==0)
    {
        sprintf (path, "%s/.postoffice", HOME);
        if (stat (path, &mystat)!=0)
        {
            mkdir (path, 0700);
        }
        strcat (path, "/");
        strcat (path, Name);
        strcat (path, ".box");
        if (stat (path, &mystat)!=0) mkdir (path, 0700);
		strcat (path, "/index");
		
		// touch the original index file to ensure it exists.
		
		FILE *OLD = fopen (path, "a");
		if (!OLD)
		{
			eprintf ("could not touch %s\n", path);
			return;
		}
		fclose (OLD);
		
		strcpy (oldpath, path);
		
		// create the "index.new" file to dump the database to.
		
		strcat (path, ".new");
		
        FILE *OUT = fopen (path, "w");
        if (OUT)
        {
            MailMessage *M;
            ViewListNode *V;
            int i;

            Lock->LockR();
            VLock->LockR();

			// Iterate through the mailbox;

            M = (MailMessage *) Root->BNext;
            while (M && (M!=Root))
            {
				char *esc_msgid, *esc_parentid, *esc_mailfrom, *esc_namefrom;
				char *esc_subject, *esc_date;
				
				esc_msgid		= escape_db (M->MsgID);
				esc_parentid	= escape_db (M->Envelope->Get (ENVELOPE_I_PARENTID));
				esc_mailfrom	= escape_db (M->Envelope->Get (ENVELOPE_MAILFROM));
				esc_namefrom	= escape_db (M->Envelope->Get (ENVELOPE_NAMEFROM));
				esc_subject		= escape_db (M->Envelope->Get (ENVELOPE_SUBJECT));
				esc_date		= escape_db (M->Envelope->Get (ENVELOPE_DATE));
				
                fprintf (OUT, "%s|%s|%s|%s|%s|%s|",
                         esc_msgid, esc_parentid, esc_mailfrom,
						 esc_namefrom, esc_subject, esc_date);

				free (esc_msgid);
				free (esc_parentid);
				free (esc_mailfrom);
				free (esc_namefrom);
				free (esc_subject);
				free (esc_date);

                for (i=0;i<M->Subscribers->Count();++i)
                {
                    if ((V=M->Subscribers->Get(i)))
                    {
                        fprintf (OUT, "%s", V->View->Name);
                        if (i+1 < M->Subscribers->Count()) fputc (',', OUT);
                    }
                }
                fprintf (OUT, "|%i|%i|%i|%d\n", M->Flags, M->Envelope->GetInt (ENVELOPE_I_READ), M->Count, (unsigned int) M->ReceiveDate);

                //M->SaveHeaders();

                M = (MailMessage *) M->BNext;
            }

            fclose (OUT);
			
			// move the new file on top of the old one. Congratulations, your index
			// dump is now atomical.
			
			rename (path, oldpath);

            Lock->UnlockR();
            VLock->UnlockR();
        }
        else
        {
            ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mailboxhandler", "Could not create index file");
            eprintf ("error creating index file\n");
        }
    }
}

void MailBox::UnTie (MailMessage *M)
{
    MailView *V = VFirst;

    while (V) // While the fat lady's still singing
    {
        V->Unassociate (M); // This procedure is expensive and should best be avoided, alas I'm lazy today.
        V = V->Next; // And on and on and on...
    }
}

void MailBox::Deliver (MailMessage *M)
{
    MailView *V;

    int delivered = 0;

    V = VFirst;

    while ((V)&&(!delivered))
    {
        delivered = V->ProcessRules (M);
        V = V->Next;
    }

    if (!delivered)
    {
        char *fld = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Name)->Get_char ("defaultfolder");
        V = GetView (fld);
        if (!V) V = GetView ("Inbox");
        if (!V) V = VFirst;
        if (V) V->Associate (M);
    }

    if (MemoryUsage > MBOX_MAXMEM)
    {
        //printf("[deliver] memusage=%d - calling grim reaper\n", MemoryUsage);
    }
}

void MailBox::GrimReaper (void)
{
    if (!Root) return;
    Lock->LockRW();
        MailMessage *V = (MailMessage *) Root->BNext;
        while ((MemoryUsage > (MBOX_MAXMEM * 0.8)) && (V && (V!=Root)))
        {
            if (V->Flags & MMFL_INRAM)
            {
                //printf("[grimreaper] reaping %s\n", V->MsgID);
                V->Flush();
            }
            V = (MailMessage *) V->BNext;
        }
    Lock->UnlockRW();
}

void MailBox::ExpireLocal (void)
{
    MailMessage *M, *NM;

    time_t expire_local_only = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Name)->Get_int ("expire_local_only");
    time_t expire_local = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Name)->Get_int ("expire_local");

    expire_local_only *= 86400;
    expire_local *= 86400;

    M = (MailMessage *) Root->BNext;
    Lock->LockR();
    while (M && (M!=Root))
    {
        NM = (MailMessage *) M->BNext;
        if ((expire_local_only) &&
            (!(M->Flags & MMFL_HAVEBODY)) &&
            ((time(NULL) - M->ReceiveDate) > expire_local_only))
        {
            Lock->RelockRW();
            RemoveMessage (M);
            Lock->RelockR();
        }
        else if ((expire_local) &&
                 ((time(NULL) - M->ReceiveDate) > expire_local))
        {
            Lock->RelockRW();
            RemoveMessage (M);
            Lock->RelockR();
        }
        M = NM;
    }
    Lock->UnlockR();
}

int MailBox::Have (char *msgid)
{
    int result = 0;

    Lock->LockR();
        if (Root->Find (msgid)) result = 1;
    Lock->UnlockR();
    return (result);
}

void MailBox::CreateMailView (const char *name)
{
    MailView *V = new MailView (this, name);
    VLock->LockRW();
    if (VLast)
    {
        V->Next = NULL;
        V->Prev = VLast;
        VLast->Next = V;
        VLast = V;
    }
    else
    {
        V->Next = V->Prev = NULL;
        VFirst = VLast = V;
    }
    VLock->UnlockRW();
}

void MailBox::DestroyMailView (char *name)
{
    MailView *V = GetView (name);
    if (V)
    {
        VLock->LockRW();
        if (V->Next)
        {
            if (V->Prev)
            {
                V->Prev->Next = V->Next;
                V->Next->Prev = V->Prev;
            }
            else
            {
                V->Next->Prev = NULL;
                VFirst = V->Next;
            }
        }
        else
        {
            if (V->Prev)
            {
                V->Prev->Next = NULL;
                VLast = V->Prev;
            }
            else
            {
                VFirst = VLast = NULL;
            }
        }
        VLock->UnlockRW();
        for (int i=0; i<V->Count();++i)
        {
            MailMessage *M = V->GetMessage (i);
            V->DoneWithMessage();
            V->Unassociate (M);
            if (M->Subscribers->Count() == 0)
            {
                dprintf ("erasing: %s\n", M->MsgID);
                M->RemoveFiles();
                RemoveMessage (M);
                --i;
            }
        }
    }
}

MailView *MailBox::GetView (const char *Name)
{
    MailView *V;

    VLock->LockR();
    V = VFirst;
    while (V)
    {
        if (strcasecmp (V->Name, Name) == 0)
        {
            VLock->UnlockR();
            return(V);
        }
        V = V->Next;
    }
    VLock->UnlockR();
    return((MailView *) NULL);
}

MailMessage *MailBox::Find (char *msgid)
{
    Lock->LockR();
    MailMessage *M = (MailMessage *) Root->Find (msgid);
    Lock->UnlockR();
    return M;
}

void MailBox::Quit (void)
{
    int hitcount = 0;

    dprintf ("quitting mailbox started=%i\n", Collector->Started);
    if (Collector->Started)
    {
        ToCollector->PostMessage (CLASS_DIE | CLASS_URGENT, NO_SUBCLASS, "");
        iprintf ("[mailbox] Sent CLASS_DIE to Collector."); fflush (stdout);
        while (Collector->Started)
        {
            iprintf ("."); fflush (stdout);
            if (Collector->Started) _usleep(100000);
            if (++hitcount > 100)
            {
                iprintf ("<timeout>\n[mailbox] Killing.. "); fflush (stdout);
                Collector->Kill();
                Collector->Started = 0;
            }
        }
        iprintf ("done\n[mailbox] This is a death thread.\n");
    }
}

void MailBox::RemoveMessage (MailMessage *M)
{
    if (M)
    {
        M->Subscribers->UnTie();

        if (M->Flags & (MMFL_HAVEBODY | MMFL_ONDISK))
        {
            M->RemoveFiles();
        }
#ifdef USE_GDBM
        if (DB && DB->KeepsMailboxIndex()) DB->ForgetMessage (M->MsgID);
#endif

        delete (M);
        --Count;
    }
}

void MailBox::RemoveMessage (char *msgid)
{
    MailMessage *M = Find (msgid);
    if (!M)
    {
        ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mailboxhandler", "RemoveMessage(): Message does not exist!");
        return;
    }

    Lock->LockRW();
    RemoveMessage (M);
    Lock->UnlockRW();
}

void MailBox::ClearBiff (void)
{
	Biff = false;
	VLock->LockR();
		MailView *V = VFirst;
		while (V)
		{
			//V->Biff = false;
			V = V->Next;
		}
	VLock->UnlockR();
}
