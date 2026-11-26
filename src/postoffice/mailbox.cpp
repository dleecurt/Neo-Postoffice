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

#include "privatefile.h"
#include <string>
#include <vector>
#include "mailbox.h"
#include "search.h"
#include "mailheaders.h"
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
        snprintf (path, sizeof (path), "%s/.postoffice", HOME);
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
#ifdef PO_HAVE_LIBETPAN
		Handler = new IMAP4Handler (this, PREFROOT);
#else
		// Built without libetpan: the settings are kept for a build with it.
		Handler = new UnavailableHandler (this, PREFROOT, "IMAP4");
#endif
	}
    else
    {
        // A protocol this version does not have (or a damaged setting).
        Handler = new UnavailableHandler (this, PREFROOT, PREFROOT->Get_char ("protocol"));
    }
	
    // Open all views

    WordList *Views = PREFROOT->Get_wordlist ("folders");

    // 0.9.2 saved the temporary Search results folder in the settings (a
    // folder with nothing but an empty filter): it is removed.
    std::vector<std::string> stale;
    for (i=0;i<Views->Count;++i)
    {
        const char *v = Views->Get(i);
        if (strncmp (v, "Search results", 14) != 0 || (v[14] && strncmp (v + 14, " (", 2) != 0)) continue;
        WordList *Keys = PREFROOT->Get_collection ("folders")->Get_wordlist (v);
        if (Keys->Count == 1 && strcmp (Keys->Get(0), "filters") == 0
            && !*PREFROOT->Get_collection ("folders")->Get_collection (v)->Get_char ("filters"))
            stale.push_back (v);
    }
    for (const std::string &v : stale) PREFROOT->Get_collection ("folders")->Delete (v.c_str());
    Views = PREFROOT->Get_wordlist ("folders");

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

    // Maildir and MH mailboxes read their message list right away: that
    // creates their folders and brings in changes made elsewhere meanwhile.
    if (dynamic_cast<LocalMailHandler *> (Handler))
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
        snprintf (path, sizeof (path), "%s/.postoffice", HOME);
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
    CreateMailView ("Drafts");		// messages that could not be sent

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
// The index's flags field: the message's own bits, and from bit 8 its
// flagged, replied and forwarded flags (Neo-PostOffice; indexes of 0.7
// have none there, and only the low bits are read back as message bits).
static int index_flags (MailMessage *M)
{
    return (M->Flags & 0xff) | ((M->Envelope->GetInt (ENVELOPE_I_FLAGS) & (EFLAG_FLAGGED | EFLAG_REPLIED | EFLAG_PASSED)) << 8);
}

static int index_envelope_flags (int flags)
{
    return (flags >> 8) & (EFLAG_FLAGGED | EFLAG_REPLIED | EFLAG_PASSED);
}

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

    bool from_store = dynamic_cast<LocalMailHandler *> (Handler) != NULL;
#ifdef PO_HAVE_LIBETPAN
    // IMAP folders come from the server the same way.
    if (dynamic_cast<IMAP4Handler *> (Handler)) from_store = true;
#endif

    for (const IndexEntry &e : entries)
    {
        // An entry in no folder cannot be shown. For Maildir, MH and IMAP it is
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
        M->Envelope->SetInt (ENVELOPE_I_FLAGS, index_envelope_flags (e.Flags));
        M->ReceiveDate = e.Received;
        if (!M->ReceiveDate) M->ReceiveDate = time (NULL) - 864000;

        for (size_t i=0;i<e.Folders.size();++i)
        {
            V = GetView (e.Folders[i].c_str()); // Find the named view
            // Maildir, MH and IMAP folders are made from what the store has, so
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
            e.Flags    = index_flags (M);
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
        snprintf (path, sizeof (path), "%s/.postoffice", HOME);
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
		
		FILE *OLD = fopen_private (path, "a");
		if (!OLD)
		{
			eprintf ("could not touch %s\n", path);
			return;
		}
		fclose (OLD);
		
		strcpy (oldpath, path);
		
		// create the "index.new" file to dump the database to.
		
		strcat (path, ".new");
		
        FILE *OUT = fopen_private (path, "w");
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
                fprintf (OUT, "|%i|%i|%i|%d\n", index_flags (M), M->Envelope->GetInt (ENVELOPE_I_READ), M->Count, (unsigned int) M->ReceiveDate);

                //M->SaveHeaders();

                M = (MailMessage *) M->BNext;
            }

            // On disk before it replaces the old one (Neo-PostOffice).
            fflush (OUT);
            fsync (fileno (OUT));
            fclose (OUT);
			
			// move the new file on top of the old one. Congratulations, your index
			// dump is now atomical.
			
			rename (path, oldpath);

			// And the message texts with it: GDBM keeps recent writes in
			// memory and syncs at most every 15 seconds on its own, so a
			// crash could lose messages fetched just before (and deleted
			// from a POP3 server since).
#ifdef USE_GDBM
			if (DB) DB->StoreIndex (true);
#endif

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
        if (!V->IsSearch) delivered = V->ProcessRules (M);
        V = V->Next;
    }

    if (!delivered)
    {
        char *fld = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Name)->Get_char ("defaultfolder");
        V = GetView (fld);
        if (!V) V = GetView ("Inbox");
        if (!V) V = VFirst;
        if (V && V->IsSearch) V = NULL;
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

// The filter pattern that matches exactly this folder name. "*" and "?" are
// escaped; a comma (the filter list's separator) is matched by "?".
static std::string folder_pattern (const std::string &value)
{
    std::string pattern;
    for (char c : value)
    {
        if (c == '*' || c == '?') pattern += '\\';
        pattern += c == ',' ? '?' : c;
    }
    return pattern;
}

bool MailBox::EnsureFolder (const std::string &value)
{
    std::string name = folder_view_name (value);
    if (GetView (name.c_str())) return false;

    // New folders get a filter that takes exactly the messages whose folder
    // field (ENVELOPE_GROUPS) is value, and are saved with the settings at
    // exit.
    Var *box = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (Name);
    Var *F = box->Get_collection ("folders")->Get_collection (name.c_str());
    if (!*F->Get_char ("filters"))
    {
        F->Set_char ("filters", ("7,glob,accept,0," + folder_pattern (value)).c_str());
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
    return strcmp (F->Get_char ("filters"), ("7,glob,accept,0," + pattern).c_str()) == 0 ||
           strcmp (F->Get_char ("filters"), ("7,glob,accept,0," + folder_pattern (name)).c_str()) == 0;
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

// One part for a multipart body: its MIME headers, a blank line and its
// body. Each part gets its type, file name (RFC 2231 for non-ASCII) and
// transfer encoding (kept in the x-neo-cte parameter); Neo-PostOffice's own
// parameters (x-neo-...) are left out.
static std::string mime_part (MailPart *P)
{
    std::string out;
    std::string body (P->Contents, P->ContentSize);
    // The 0.7.3 parser leaves the closing boundary's "--" at the end of
    // the last part; drop it and trailing blank lines.
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) body.pop_back();
    if (body.size() >= 2 && body.compare (body.size() - 2, 2, "--") == 0 &&
        (body.size() == 2 || body[body.size() - 3] == '\n'))
        body.erase (body.size() - 2);
    while (!body.empty() && (body.back() == '\n' || body.back() == '\r')) body.pop_back();
    body += "\n";
    // Parts parsed by Neo-PostOffice carry their transfer encoding as
    // the parameter x-neo-cte; older ones do not.
    std::string type = P->ContentType && *P->ContentType ? P->ContentType : "text/plain";
    std::string cte = mailheaders::Param (type, "x-neo-cte");
    size_t cut = type.find ("; x-neo-");
    if (cut != std::string::npos) type.erase (cut);
    out += "Content-Type: " + type + "\n";
    if (P->FileName && *P->FileName)
    {
        // Plain ASCII as is, anything else as UTF-8 (RFC 2231).
        bool plain = true;
        for (const char *c = P->FileName; *c; ++c)
            if ((unsigned char) *c < 0x20 || (unsigned char) *c > 0x7e || *c == '"' || *c == '\\') plain = false;
        if (plain)
            out += std::string ("Content-Disposition: attachment; filename=\"") + P->FileName + "\"\n";
        else
        {
            out += "Content-Disposition: attachment; filename*=utf-8''";
            for (const char *c = P->FileName; *c; ++c)
            {
                unsigned char u = *c;
                if (isalnum (u) || strchr ("-._~", u)) out += (char) u;
                else { char hex[4]; snprintf (hex, sizeof (hex), "%%%02X", u); out += hex; }
            }
            out += "\n";
        }
    }
    bool text = strncasecmp (type.c_str(), "text/", 5) == 0;
    if (!cte.empty())
        out += "Content-Transfer-Encoding: " + cte + "\n";
    // A non-text part that is all base64 was base64 in the original.
    else if (!text && !body.empty() &&
        body.find_first_not_of ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\r\n") == std::string::npos)
        out += "Content-Transfer-Encoding: base64\n";
    out += "\n" + body;
    if (body.empty() || body.back() != '\n') out += "\n";
    return out;
}

static std::string new_boundary (void)
{
    static unsigned serial = 0;
    char boundary[80];
    snprintf (boundary, sizeof (boundary), "neo-postoffice-%lx-%x-%u", (long) time (NULL), (unsigned) rand(), ++serial);
    return boundary;
}

// The body of a multipart/mixed message made of M's parts, and the value
// for its Content-Type header (with the boundary). Used to send messages
// with attachments.
std::string mime_multipart_body (MailMessage *M, std::string &content_type)
{
    std::string boundary = new_boundary ();
    content_type = std::string ("multipart/mixed; boundary=\"") + boundary + "\"";
    std::string out;
    for (int i = 0; i < M->Count; ++i)
    {
        MailPart *P = M->GetPart (i);
        if (!P) break;
        out += std::string ("--") + boundary + "\n" + mime_part (P);
    }
    out += std::string ("--") + boundary + "--\n";
    return out;
}

// The same, but with the multiparts the parts were in when the message came
// in (mixed around alternative around text and HTML, and so on), as kept in
// their x-neo-mp parameter since 0.9.4. Parts stored by older versions have
// none and end up side by side in one multipart/mixed, as before.
static std::string mime_nested_body (MailMessage *M, std::string &content_type)
{
    struct Level { std::string id, boundary; };
    std::vector<Level> open;
    std::string out;
    for (int i = 0; i < M->Count; ++i)
    {
        MailPart *P = M->GetPart (i);
        if (!P) break;
        // The chain, "1:mixed,2:alternative": number and kind of each multipart.
        std::vector<std::pair<std::string, std::string>> chain;
        std::string c = mailheaders::Param (P->ContentType ? P->ContentType : "", "x-neo-mp");
        for (size_t pos = 0; pos < c.size (); )
        {
            size_t e = c.find (',', pos);
            if (e == std::string::npos) e = c.size ();
            std::string link = c.substr (pos, e - pos);
            size_t colon = link.find (':');
            if (colon != std::string::npos && colon > 0)
            {
                std::string kind;
                for (char k : link.substr (colon + 1))
                    if (isalnum ((unsigned char) k) || k == '-' || k == '.') kind += k;
                chain.push_back ({ link.substr (0, colon), kind.empty () ? "mixed" : kind });
            }
            pos = e + 1;
        }
        if (chain.empty ()) chain.push_back ({ "0", "mixed" });

        if (open.empty ())
        {
            open.push_back ({ chain[0].first, new_boundary () });
            content_type = "multipart/" + chain[0].second + "; boundary=\"" + open[0].boundary + "\"";
        }
        chain[0].first = open[0].id;		// one outermost multipart, whatever the parts say

        // Close the multiparts this part is not in; open the ones it is in.
        size_t common = 1;
        while (common < open.size () && common < chain.size () && open[common].id == chain[common].first) ++common;
        while (open.size () > common)
        {
            out += "--" + open.back ().boundary + "--\n";
            open.pop_back ();
        }
        for (size_t k = common; k < chain.size (); ++k)
        {
            std::string b = new_boundary ();
            out += "--" + open.back ().boundary + "\n";
            out += "Content-Type: multipart/" + chain[k].second + "; boundary=\"" + b + "\"\n\n";
            open.push_back ({ chain[k].first, b });
        }
        out += "--" + open.back ().boundary + "\n" + mime_part (P);
    }
    while (!open.empty ())
    {
        out += "--" + open.back ().boundary + "--\n";
        open.pop_back ();
    }
    return out;
}

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

    // A message that came in as a multipart (even with one part) is rebuilt
    // as one (0.9.4 keeps the multiparts' nesting with the parts).
    MailPart *first = M->GetPart (0);
    bool multi = M->Count > 1 || (first && first->ContentType && strstr (first->ContentType, "; x-neo-mp="));
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

    std::string ctype;
    std::string body = mime_nested_body (M, ctype);
    out += "MIME-Version: 1.0\n";
    out += "Content-Type: " + ctype + "\n\n";
    out += body;
    return out;
}

// The original text of a message as it came in (Maildir/MH file, or the
// message fetched again with its raw text kept), else rebuilt from the
// local copy; flags are its Maildir flag letters. False with error set
// when there is none. Runs on the mailbox's own thread (Neo-PostOffice).
bool MailBox::FetchRaw (const char *msgid, std::string &raw, std::string &flags, std::string &error)
{
    if (Handler->ExportRaw ((char *) msgid, raw, flags)) return true;
    // Not a Maildir: fetch the message again and keep the raw text.
    MailMessage *M = Find ((char *) msgid);
    if (!M) { error = "The message is no longer in this mailbox."; return false; }
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
    {
        error = "The original of this message is not available (it is no longer at its source "
                "and was not downloaded).";
        return false;
    }
    return true;
}

void MailBox::TransferMessage (const char *msgid, const char *target, const char *folder, bool move, bool last)
{
    std::string raw, flags, error;
    MailBox *T = NULL;
    for (int k = 0; k < 16 && MailBoxList[k]; ++k)
        if (strcmp (MailBoxList[k]->Name, target) == 0) T = MailBoxList[k];

    if (!T || !T->Handler)
        error = std::string ("The mailbox \"") + target + "\" does not exist.";
    else
        FetchRaw (msgid, raw, flags, error);

    std::string err;
    if (error.empty() && !T->Handler->DeliverRaw (raw, folder, flags, err))
        error = "Cannot store the message in " + std::string (target) + ": " + err;

    if (!error.empty())
        ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "transfer", error.c_str());
    else if (move)
    {
        // Only now that the copy is safe: remove the original, as Delete does.
        Handler->RemoveAfterTransfer ((char *) msgid);
        ToUI->PostMessage (CLASS_GONE, NO_SUBCLASS, Name, msgid);
    }
    // Show the new arrivals; this is no reason to fetch mail.
    if (last && T) T->ToCollector->PostMessage (CLASS_CHECKGROUP, SUB_CHECK_NOFETCH, "*");
}

// Redirect: the original's raw text goes to the user interface, which
// queues it in the OutBox with Resent- headers (Neo-PostOffice).
void MailBox::RedirectMessage (const char *msgid, const char *from, const char *to)
{
    std::string raw, flags, error;
    if (!FetchRaw (msgid, raw, flags, error))
    {
        ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Redirect", error.c_str());
        return;
    }
    WordList *W = new WordList;
    W->Add (raw.c_str());
    W->Add (from);
    W->Add (to);
    ToUI->PostMessage (CLASS_REDIRECTED, NO_SUBCLASS, W);
}

// Refilter (Neo-PostOffice): every message of the mailbox is sorted into
// its folders again by the folders' filters, as when the mailbox is
// loaded at start-up; the window then shows the result.
void MailBox::Refilter (void)
{
    VLock->LockR();
    for (MailView *V = VFirst; V; V = V->Next) if (!V->IsSearch) V->UnassociateAll ();
    VLock->UnlockR();
    Lock->LockR();
    for (MailMessage *M = (MailMessage *) Root->BNext; M && M != Root; M = (MailMessage *) M->BNext)
        Deliver (M);
    Lock->UnlockR();
    ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Name, "");
}

// Search (Neo-PostOffice): the messages of one folder ("" = all of the
// mailbox's folders) that match; the results go to the window, which
// shows them in a "Search results" folder. The server searches IMAP
// folders; otherwise the stored messages are searched here, their text
// read without marking them read.
void MailBox::Search (int field, const char *text, const char *folder)
{
    std::vector<std::string> folders;
    VLock->LockR();
    for (MailView *V = VFirst; V; V = V->Next)
        if (!V->IsSearch && (!*folder || strcmp (V->Name, folder) == 0)) folders.push_back (V->Name);
    VLock->UnlockR();

    std::vector<std::string> ids;
    std::string error, note;
    int r = Handler->Search (folders, field, text, ids, error);
    if (r == 0)
    {
        ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "Search", error.c_str());
        WordList *W = new WordList;
        W->Add (Name); W->Add ((char *) "-1"); W->Add (text); W->Add (folder); W->Add ("");
        ToUI->PostMessage (CLASS_SEARCHED, NO_SUBCLASS, W);
        return;
    }
    if (r < 0)
    {
        std::string needle = search::Fold (text);
        int unread = 0;
        std::vector<std::string> cand;
        for (const std::string &f : folders)
        {
            MailView *V = GetView (f.c_str());
            if (!V) continue;
            for (int i = 0; i < V->Count(); ++i)
            {
                MailMessage *M = V->GetMessage (i);
                if (M && M->MsgID) cand.push_back (M->MsgID);
            }
        }
        for (const std::string &id : cand)
        {
            MailMessage *M = Find ((char *) id.c_str());
            if (!M) continue;
            // Headers are read from the store when a message is first shown;
            // until then From, To and Subject are not in memory.
            if (field != SEARCH_BODY && M->HeaderNames->Count == 0) M->LoadHeaders ();
            bool body = field == SEARCH_BODY || field == SEARCH_ANYWHERE;
            if (!body || (M->Flags & MMFL_INRAM))
            {
                if (search::Matches (M, (SearchField) field, needle)) ids.push_back (id);
                continue;
            }
            // The text from the store, freed again afterwards; else the
            // original (Maildir, MH), read without marking it read.
            if ((M->Flags & MMFL_HAVEBODY) && (M->Flags & MMFL_ONDISK))
            {
                M->LoadBody ();
                if (M->Flags & MMFL_INRAM)
                {
                    if (search::Matches (M, (SearchField) field, needle)) ids.push_back (id);
                    M->Flush ();
                    continue;
                }
            }
            std::string raw, flags;
            if (Handler->ExportRaw ((char *) id.c_str(), raw, flags))
            {
                if (search::Matches (M, (SearchField) field, needle, &raw)) ids.push_back (id);
            }
            else
            {
                // Headers only (text never fetched): the headers still count
                // for Anywhere.
                std::string none;
                if (field == SEARCH_ANYWHERE && search::Matches (M, SEARCH_ANYWHERE, needle, &none)) ids.push_back (id);
                ++unread;
            }
        }
        if (unread)
            note = std::to_string (unread) + (unread == 1 ? " message has" : " messages have") +
                   " no text stored here and could not be searched.";
    }
    WordList *W = new WordList;
    W->Add (Name); W->Add (std::to_string (field).c_str()); W->Add (text); W->Add (folder); W->Add (note.c_str());
    for (const std::string &id : ids) W->Add (id.c_str());
    ToUI->PostMessage (CLASS_SEARCHED, NO_SUBCLASS, W);
}
