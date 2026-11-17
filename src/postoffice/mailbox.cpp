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
#include <strings.h>

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
    else if (strcasecmp (PREFROOT->Get_char ("protocol"), "maildir") == 0)
    {
        Handler = new MaildirHandler (this, PREFROOT);
    }
    else if (strcasecmp (PREFROOT->Get_char ("protocol"), "mh") == 0)
    {
        Handler = new MHHandler (this, PREFROOT);
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

    // SQL, Maildir and MH mailboxes read their message list right away: that
    // creates their folders and brings in changes made elsewhere meanwhile.
    if (dynamic_cast<SQLHandler *> (Handler) || dynamic_cast<LocalMailHandler *> (Handler))
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

    bool from_store = dynamic_cast<SQLHandler *> (Handler) || dynamic_cast<LocalMailHandler *> (Handler);

    for (const IndexEntry &e : entries)
    {
        // An entry in no folder cannot be shown. For SQL, Maildir and MH it is
        // left out: the first check finds the message in the store again and
        // files it in its folder.
        if (from_store && e.Folders.empty()) continue;

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
            // SQL, Maildir and MH folders are made from what the store has, so
            // one missing from the settings (not saved after a crash, say) is
            // made again here instead of the message being lost from view.
            if (!V && from_store)
            {
                EnsureFolder (e.Folders[i]);
                V = GetView (e.Folders[i].c_str());
            }
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

// The client's name for a folder: the settings file cannot store
// = { } " or control characters in a name, and folder names are kept in
// 64-byte buffers.
static std::string folder_view_name (const std::string &folder)
{
    std::string out;
    for (char c : folder)
    {
        if ((unsigned char) c < ' ' || strchr ("={}\"", c)) out += '_';
        else out += c;
    }
    if (out.size() > 63)
    {
        out.resize (63);
        while (!out.empty() && ((unsigned char) out.back() & 0xC0) == 0x80) out.pop_back();
        if (!out.empty() && ((unsigned char) out.back() & 0x80)) out.pop_back();
    }
    return out.empty() ? "_" : out;
}

bool MailBox::EnsureFolder (const std::string &value)
{
    std::string name = folder_view_name (value);
    if (GetView (name.c_str())) return false;

    // New folders get a filter that takes exactly the messages whose folder
    // field (ENVELOPE_GROUPS) is value, and are saved with the settings at
    // exit. The filter list is separated by commas, so a comma in the name
    // is matched by the wildcard '?'.
    Var *box = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Name);
    Var *F = box->Get_collection ("folders")->Get_collection (name.c_str());
    if (!*F->Get_char ("filters"))
    {
        std::string pattern = value;
        for (char &c : pattern) if (c == ',') c = '?';
        F->Set_char ("filters", ("7,glob,accept,0," + pattern).c_str());
        F->Set_int ("autofolder", 1);
    }
    CreateMailView (name.c_str());
    return true;
}

bool MailBox::IsAutoFolder (const char *name)
{
    Var *folders = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Name)->Get_collection ("folders");
    if (!folders->Exists (name)) return false;
    Var *F = folders->Get_collection (name);
    if (F->Get_int ("autofolder")) return true;
    // Made before the marker existed: it has the exact-match filter.
    std::string pattern = name;
    for (char &c : pattern) if (c == ',') c = '?';
    return strcmp (F->Get_char ("filters"), ("7,glob,accept,0," + pattern).c_str()) == 0;
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

//===============================================================================
// Neo-PostOffice: copying and moving messages into a Maildir mailbox
//===============================================================================

extern MailBox *MailBoxList[16];

// Best-effort rebuild of a message from the local copy, for when its source
// no longer has it (e.g. deleted from a POP3 server after downloading).
// The local copy keeps the headers and the body split into parts, but not
// each part's transfer encoding, so the result may differ from the original.
static std::string rebuild_raw (MailMessage *M)
{
    if (!M || !(M->Flags & MMFL_HAVEBODY)) return "";
    if (!(M->Flags & MMFL_INRAM)) M->LoadBody();
    if (!(M->Flags & MMFL_INRAM)) return "";
    if (M->HeaderNames->Count == 0) M->LoadHeaders();

    bool multi = M->Count > 1;
    std::string out = "X-Neo-PostOffice-Rebuilt: the original was no longer available; rebuilt from the local copy\n";
    for (int i = 0; i < M->HeaderNames->Count; ++i)
    {
        const char *n = M->HeaderNames->Get (i);
        if (multi && (strcasecmp (n, "Content-Type") == 0 || strcasecmp (n, "Content-Transfer-Encoding") == 0 ||
                      strcasecmp (n, "MIME-Version") == 0 || strcasecmp (n, "Content-Length") == 0))
            continue;
        out += std::string (n) + ": " + M->HeaderValues->Get (i) + "\n";
    }
    if (!multi)
    {
        out += "\n";
        if (MailPart *P = M->GetPart (0)) out.append (P->Contents, P->ContentSize);
        return out;
    }

    char boundary[64];
    snprintf (boundary, sizeof (boundary), "neo-postoffice-%lx-%x", (long) time (NULL), (unsigned) rand());
    out += "MIME-Version: 1.0\n";
    out += std::string ("Content-Type: multipart/mixed; boundary=\"") + boundary + "\"\n\n";
    for (int i = 0; i < M->Count; ++i)
    {
        MailPart *P = M->GetPart (i);
        if (!P) break;
        std::string body (P->Contents, P->ContentSize);
        // The 0.7.3 parser leaves the closing boundary's "--" at the end of
        // the last part; drop it and trailing blank lines.
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) body.pop_back();
        if (body.size() >= 2 && body.compare (body.size() - 2, 2, "--") == 0 &&
            (body.size() == 2 || body[body.size() - 3] == '\n'))
            body.erase (body.size() - 2);
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
        body += "\n";
        out += std::string ("--") + boundary + "\n";
        out += std::string ("Content-Type: ") + (P->ContentType && *P->ContentType ? P->ContentType : "text/plain") + "\n";
        if (P->FileName && *P->FileName)
            out += std::string ("Content-Disposition: attachment; filename=\"") + P->FileName + "\"\n";
        // A non-text part that is all base64 was base64 in the original.
        bool text = !P->ContentType || !*P->ContentType || strncasecmp (P->ContentType, "text/", 5) == 0;
        if (!text && !body.empty() &&
            body.find_first_not_of ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\r\n") == std::string::npos)
            out += "Content-Transfer-Encoding: base64\n";
        out += "\n" + body;
        if (body.empty() || body.back() != '\n') out += "\n";
    }
    out += std::string ("--") + boundary + "--\n";
    return out;
}

void MailBox::TransferMessage (const char *msgid, const char *target, const char *folder, bool move, bool last)
{
    std::string raw, flags, error;
    MailBox *T = NULL;
    for (int k = 0; k < 16 && MailBoxList[k]; ++k)
        if (strcmp (MailBoxList[k]->Name, target) == 0) T = MailBoxList[k];

    if (!T || !T->Handler)
        error = std::string ("The mailbox \"") + target + "\" does not exist.";
    else if (!Handler->ExportRaw ((char *) msgid, raw, flags))
    {
        // Not a Maildir: fetch the message again and keep the raw text.
        MailMessage *M = Find ((char *) msgid);
        if (!M) error = "The message is no longer in this mailbox.";
        else
        {
            if (M->Envelope->GetInt (ENVELOPE_I_READ)) flags += 'S';
            int ef = M->Envelope->GetInt (ENVELOPE_I_FLAGS);
            if (ef & EFLAG_FLAGGED) flags += 'F';
            if (ef & EFLAG_REPLIED) flags += 'R';
            if (ef & EFLAG_PASSED) flags += 'P';

            M->CaptureRaw = &raw;
            Handler->GetBody ((char *) msgid);
            if ((M = Find ((char *) msgid))) M->CaptureRaw = NULL;
            if (raw.empty()) raw = rebuild_raw (M);
            if (raw.empty())
                error = "The original of this message is not available (it is no longer at its source "
                        "and was not downloaded).";
        }
    }

    std::string err;
    if (error.empty() && !T->Handler->DeliverRaw (raw, folder, flags, err))
        error = "Cannot store the message in " + std::string (target) + ": " + err;

    if (!error.empty())
        ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "transfer", error.c_str());
    else if (move)
    {
        // Only now that the copy is safe: remove the original, as Delete does.
        Handler->DeleteMessage ((char *) msgid);
        ToUI->PostMessage (CLASS_GONE, NO_SUBCLASS, Name, msgid);
    }
    if (last && T) T->ToCollector->PostMessage (CLASS_CHECKGROUP, NO_SUBCLASS, "*");
}
