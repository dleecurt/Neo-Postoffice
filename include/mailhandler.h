#ifndef _MAILHANDLER_H
#define _MAILHANDLER_H 1

#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>
#include "var.h"
#include "mailbox.h"
#include "mailmessage.h"
#include "mailstorage.h"
#include "semaphore.h"
#include "wordlist.h"
#include "socketclasses.h"

namespace podb { class Connection; }

class MailHandler
{
public:
	virtual					~MailHandler (void);
	virtual void			 OpenConnection (void);
	virtual void			 CloseConnection (void);
	virtual void			 GetBody (char *);
	virtual void			 CheckGroup (const char *);
	virtual void			 DeleteMessage (char *);
	// The original of a message moved to another mailbox, now stored there.
	virtual void			 RemoveAfterTransfer (char *msgid) { DeleteMessage (msgid); }
	// Called a few times a second while the mailbox thread has nothing to
	// do (IMAP IDLE). A handler that keeps its connection open on purpose
	// says so, and the thread does not close it when idle.
	virtual void			 Idle (void) {}
	virtual bool			 KeepsConnection (void) { return false; }
	bool					 UserCheck = false;	// this check was asked for (not scheduled)
	virtual void			 Expire (void);
	// The user opened a message whose body was already here (Neo-PostOffice).
	virtual void			 MarkRead (char *) {}
	// Mailboxes that keep flags and real folders (Maildir).
	virtual bool			 KeepsFlags (void) { return false; }
	virtual bool			 CanMove (void) { return false; }
	virtual void			 SetFlag (char * /*msgid*/, char /*flag*/, bool /*on*/) {}
	virtual void			 MoveMessage (char * /*msgid*/, const char * /*folder*/) {}
	// The exact original message and its Maildir flags, if this mailbox can
	// give it directly (Maildir). Others are fetched again with GetBody.
	virtual bool			 ExportRaw (char * /*msgid*/, std::string & /*raw*/, std::string & /*flags*/) { return false; }
	// Stores a message in a folder of this mailbox (Maildir). Called from
	// other mailboxes' threads: must only use the file system and settings.
	virtual bool			 DeliverRaw (const std::string &, const char *, const std::string &, std::string &err)
							 { err = "this mailbox cannot store messages"; return false; }
	// Folder management (Maildir): op is "create", "rename" or "delete".
	virtual bool			 CanManageFolders (void) { return false; }
	virtual void			 FolderOp (const char * /*op*/, const char * /*a*/, const char * /*b*/) {}
	// Format rules for new folder names: "" if fine, else why not. Called
	// from the window thread, so it must only use the settings.
	virtual std::string		 CheckFolderName (const std::string &) { return ""; }
	// A folder that cannot be renamed or deleted (the inbox).
	virtual bool			 IsFixedFolder (const char * /*name*/) { return false; }
	// Deleting a folder deletes its messages for good (IMAP), rather than
	// moving them to the Trash (Maildir, MH).
	virtual bool			 FolderDeleteIsFinal (void) { return false; }
	// Subscriptions (IMAP): FolderOp "list" answers with CLASS_FOLDERLIST,
	// "subscriptions" takes "+name" / "-name" lines of server names.
	virtual bool			 CanSubscribe (void) { return false; }
	// Search on the server (Neo-PostOffice): the Message-IDs of the
	// messages in the given folders that match. -1 when the mailbox has no
	// server search (the mailbox then searches its own copies); 0 with
	// error set when the search failed; 1 when it was done.
	virtual int				 Search (const std::vector<std::string> & /*folders*/, int /*field*/,
									 const std::string & /*text*/, std::vector<std::string> & /*ids*/,
									 std::string & /*error*/) { return -1; }

		
	char					 ConnectionOpen;
	Var						*Parameters;
	MailBox					*Parent;
	ASemaphore				 Lock;
};

class NetStream;

// POP3 (pop3handler.cpp), over TLS, STLS or plain.
class POP3Handler : public MailHandler
{
public:
	POP3Handler (MailBox *, Var *);
	~POP3Handler (void);
	
	void					 OpenConnection (void);
	void					 CloseConnection (void);
	void					 GetBody (char *);
	void					 CheckGroup (const char *);
	void					 DeleteMessage (char *);
	void					 Expire (void);
	
private:
	NetStream				*Net;
	std::map<std::string, std::string> Uidl;	// UIDL -> Message-ID (uidl.dat)
	std::map<std::string, int> Numbers;			// UIDL -> number, this session
	std::vector<std::string> Order;				// UIDLs in server order
	std::string				 LastError;			// shown once
	bool					 Quiet = false;		// no error dialogs (best-effort fetches)

	bool					 Session (int jid);
	bool					 EndSession (void);
	bool					 Command (const std::string &cmd, std::string &reply);
	bool					 ReadMulti (std::string &text, int jid, long size);
	bool					 FetchBody (const char *msgid, int jid);
	std::string				 UidOf (const char *msgid);
	std::string				 UidlFile (void);
	void					 LoadUidl (void);
	void					 SaveUidl (void);
	void					 Report (const std::string &what);
	void					 ShowStatus (int jid, const std::string &text);
};

#ifdef PO_HAVE_LIBETPAN
struct mailimap;

// IMAP4rev1 (imap4handler.cpp), with libetpan over NetStream: TLS, STARTTLS
// or plain, with the same certificate checks as POP3.
class IMAP4Handler : public MailHandler
{
public:
	IMAP4Handler (MailBox *, Var *);
	~IMAP4Handler (void);

	void					 OpenConnection (void);
	void					 CloseConnection (void);
	void					 CheckGroup (const char *);
	void					 GetBody (char *);
	// Read state and flags, kept on the server (step 3).
	bool					 KeepsFlags (void) { return true; }
	void					 MarkRead (char *);
	void					 SetFlag (char *, char, bool);
	// Changes on the server (step 4).
	void					 DeleteMessage (char *);
	void					 RemoveAfterTransfer (char *);
	bool					 CanMove (void) { return true; }
	void					 MoveMessage (char *, const char *);
	bool					 DeliverRaw (const std::string &, const char *, const std::string &, std::string &);
	// Push (step 5): IDLE on the Inbox while nothing else is going on.
	void					 Idle (void);
	bool					 KeepsConnection (void);
	// Folder management (step 6): CREATE, RENAME, DELETE, subscriptions.
	bool					 CanManageFolders (void) { return true; }
	void					 FolderOp (const char *op, const char *a, const char *b);
	std::string				 CheckFolderName (const std::string &);
	int						 Search (const std::vector<std::string> &folders, int field, const std::string &text,
									 std::vector<std::string> &ids, std::string &error);
	bool					 IsFixedFolder (const char *name);
	bool					 FolderDeleteIsFinal (void) { return true; }
	bool					 CanSubscribe (void) { return true; }

	// A server folder (one LSUB/LIST line) and its name in the client.
	struct Folder
	{
		std::string			 Server;	// as the server names it (modified UTF-7)
		std::string			 Client;	// "Inbox", "Lists/2025" (UTF-8)
		char				 Delim;		// hierarchy delimiter, 0 if none
		std::string			 Special;	// special use (RFC 6154): "Trash", "Sent", ... or ""
	};

	// Exposed for the tests (imap4test): modified UTF-7 (RFC 3501 5.1.3)
	// to UTF-8, and a server name to the client's.
	static bool				 DecodeMUTF7 (const std::string &in, std::string &out);
	static std::string		 ClientName (const std::string &server, char delim, const std::string &prefix);
	// And back: UTF-8 to modified UTF-7, and a client name to the server's.
	static std::string		 EncodeMUTF7 (const std::string &in);
	static std::string		 ServerName (const std::string &client, char delim, const std::string &prefix);

	// What is known of one server folder (imap.dat). Messages are filed
	// under made-up Message-IDs "<imap.HASH.UIDVALIDITY.UID@imap>", HASH
	// standing for the server folder name.
	struct FolderState
	{
		std::string			 Hash;
		uint32_t			 Validity = 0;	// UIDVALIDITY the UIDs belong to
		uint32_t			 UidNext = 0;	// as of the last complete check
		uint32_t			 Messages = 0;	// as of the last complete check
		uint64_t			 ModSeq = 0;	// HIGHESTMODSEQ as of the last complete check (CONDSTORE)
		std::set<uint32_t>	 Uids;			// fetched (or deleted here), still on the server
	};
	// Exposed for the tests: UID sets as "1:5,9" and back, and the IDs.
	static std::string		 UidRanges (const std::set<uint32_t> &);
	static bool				 ParseRanges (const std::string &, std::set<uint32_t> &);
	static std::string		 FolderHash (const std::string &server);
	static std::string		 MakeMsgID (const std::string &hash, uint32_t validity, uint32_t uid);
	static bool				 SplitMsgID (const char *msgid, std::string &hash, uint32_t &validity, uint32_t &uid);

private:
	NetStream				*Net;
	mailimap				*Imap;
	std::string				 Host;
	std::string				 Prefix;		// personal namespace prefix ("INBOX." on some servers)
	std::vector<Folder>		 Folders;		// subscribed, as of the last check
	char					 NameDelim = '/';	// the server's delimiter, for CheckFolderName
	std::string				 LastError;		// shown once
	bool					 Quiet = false;
	std::map<std::string, FolderState> State;	// by server folder name
	bool					 StateLoaded = false;
	std::string				 Selected;		// folder open (EXAMINE) in this session
	uint32_t				 SelValidity = 0, SelUidNext = 0, SelExists = 0;
	uint64_t				 SelModSeq = 0;	// 0: no CONDSTORE for this folder
	bool					 SelWritable = false;	// opened with SELECT, not EXAMINE
	// Flag changes made here, not yet on the server: Message-ID -> letter
	// (S seen, F flagged, R answered, P forwarded) -> set or cleared.
	std::map<std::string, std::map<char, bool>> Pending;
	// Deletes and moves made here, not yet on the server, in order. Kind:
	// T to the Trash, X removed for good, M moved to Target (server name).
	struct Op
	{
		char				 Kind;
		std::string			 MsgID;
		std::string			 Target;
	};
	std::vector<Op>			 Ops;
	std::map<std::string, std::string> SpecialCache;	// "Trash" -> server name, this session
	// Push: IDLE, and getting back after the connection was lost.
	bool					 Idling = false;
	time_t					 IdleStarted = 0;
	bool					 Dropped = false;	// lost while pushing: reconnect quietly
	time_t					 DownSince = 0, NextTry = 0;
	int						 Tries = 0;
	bool					 ToldDown = false;
	std::string				 DownWhy;
	bool					 Qresync = false;	// QRESYNC enabled in this session
	bool					 PushBegun = false;	// the first Idle() call has been
	bool					 Broken = false;	// the session failed: stop using it
	std::set<std::string>	 Checked;		// folders fully checked by this check
	int						 ProgressJob = -1;	// job shown while a body comes in
	double					 ProgressShown = 0;

	static void				 BodyProgress (size_t current, size_t maximum, void *context);

	bool					 Session (int jid);
	void					 EndSession (bool logout);
	bool					 Capability (const char *name);
	int						 RefreshCapabilities (void);	// libetpan result
	bool					 ListFolders (int jid);
	void					 SyncFolders (void);
	bool					 SyncFolder (int jid, const Folder &, size_t index, size_t count, int &added);
	bool					 Examine (const std::string &server);
	bool					 Select (const std::string &server);
	bool					 SyncFlags (const Folder &, FolderState &, uint64_t since, int &changed,
										std::set<uint32_t> &vanished);
	bool					 PushWanted (void);
	bool					 StartIdle (void);
	void					 StopIdle (void);
	void					 Lost (const std::string &why = "");
	void					 QuickSync (int jid);
	bool					 Refresh (int jid);
	bool					 FolderChange (int jid, const char *op, const std::string &a, const std::string &b);
	struct Listed
	{
		std::string			 Server;
		char				 Delim;
		bool				 Selectable;
	};
	bool					 ListAll (std::vector<Listed> &all, std::set<std::string> *subscribed);
	void					 SendFolderList (int jid);
	bool					 QueueFlag (const char *msgid, char letter, bool on);
	bool					 PushPending (int jid);
	void					 PushQuietly (void);
	bool					 PushOps (int jid);
	bool					 FlushSpool (int jid);
	std::string				 SpecialFolder (const std::string &use, bool create);
	bool					 RemoveUids (const std::string &server, const std::vector<uint32_t> &uids);
	void					 QueueOp (char kind, const char *msgid, const std::string &target);
	std::string				 SpoolDir (void);
	bool					 FetchBody (const char *msgid, int jid);
	void					 ServiceBodies (int jid);
	void					 Gone (const FolderState &, const std::set<uint32_t> &uids);
	void					 Prune (void);
	std::string				 ServerOf (const std::string &hash);
	std::string				 StateFile (void);
	void					 LoadState (void);
	void					 SaveState (void);
	bool					 Failed (int r, const std::string &what);
	std::string				 Why (int r);
	void					 Report (const std::string &what);
	void					 ShowStatus (int jid, const std::string &text);
};
#endif

// A mailbox whose protocol is not available in this build (IMAP4 without
// libetpan). Its settings are kept; checking it shows a note once.
class UnavailableHandler : public MailHandler
{
public:
	UnavailableHandler (MailBox *, Var *, const char *what);
	~UnavailableHandler (void);
	void					 CheckGroup (const char *);
private:
	std::string				 What;
	bool					 Told;
};

// Neo-PostOffice: mailboxes kept as files on this computer, one file per
// message, with real folders (Maildir, MH). The checking, reading and flag
// handling are shared here; the formats supply scanning and file changes.
class LocalMailHandler : public MailHandler
{
public:
	void					 OpenConnection (void) {}
	void					 CloseConnection (void) {}
	void					 GetBody (char *);
	void					 CheckGroup (const char *);
	void					 Expire (void) {}
	void					 MarkRead (char *);
	bool					 KeepsFlags (void) { return true; }
	bool					 CanMove (void) { return true; }
	bool					 CanManageFolders (void) { return true; }
	void					 SetFlag (char *, char, bool);
	bool					 ExportRaw (char *, std::string &, std::string &);

protected:
	struct Entry
	{
		std::string			 Folder;	// the folder's name in the client ("Inbox", "Lists/2025")
		std::string			 Dir;		// format-specific: Maildir ".Work", MH "lists/2025"
		std::string			 Sub;		// Maildir "new"/"cur"
		std::string			 File;		// file name
		std::string			 Flags;		// Maildir letters, sorted: S seen, F flagged, R replied, P passed
	};

	virtual std::string		 Root (void) = 0;
	// Lists all messages into Files and all folder names into Folders;
	// false if the mailbox is not there.
	virtual bool			 Scan (void) = 0;
	virtual std::string		 Path (const Entry &) = 0;
	// Adds and removes flag letters (S, F, R, P) of a message.
	virtual bool			 ChangeFlags (Entry &, const std::string &add, const std::string &remove) = 0;
	virtual std::string		 MissingText (void) = 0;	// why Scan failed
	virtual const char		*Kind (void) = 0;			// for error messages

	void					 Init (MailBox *, Var *);
	Entry					*Locate (const char *msgid);
	bool					 MarkSeen (Entry &);
	bool					 ApplyFlags (MailMessage *, const Entry &);
	void					 Error (const std::string &);
	static bool				 WriteFile (const std::string &path, const std::string &data, std::string &err);

	std::map<std::string, Entry> Files;		// msgid -> file, as of the last scan
	std::vector<std::string> Folders;		// all folder names, as of the last scan
	bool					 FoldersChanged;
	bool					 ReportedMissing;
};

class MaildirHandler : public LocalMailHandler
{
public:
							 MaildirHandler (MailBox *, Var *);
							~MaildirHandler (void);

	void					 DeleteMessage (char *);
	void					 MoveMessage (char *, const char *);
	bool					 DeliverRaw (const std::string &, const char *, const std::string &, std::string &);
	void					 FolderOp (const char *, const char *, const char *);
	std::string				 CheckFolderName (const std::string &);
	bool					 IsFixedFolder (const char *name);

protected:
	std::string				 Root (void);
	bool					 Scan (void);
	std::string				 Path (const Entry &);
	bool					 ChangeFlags (Entry &, const std::string &add, const std::string &remove);
	std::string				 MissingText (void);
	const char				*Kind (void) { return "maildir"; }

private:
	std::string				 MsgID (const std::string &dir, const std::string &uniq);
	std::vector<std::string> Dirs;		// "" and the Maildir++ subfolders
};

// MH folders as used by nmh: a directory per folder (nested folders are
// subdirectories), one file per message named by its number, and the
// sequences (unseen, flagged, replied, ...) in each folder's .mh_sequences.
class MHHandler : public LocalMailHandler
{
public:
							 MHHandler (MailBox *, Var *);
							~MHHandler (void);

	void					 DeleteMessage (char *);
	void					 MoveMessage (char *, const char *);
	bool					 DeliverRaw (const std::string &, const char *, const std::string &, std::string &);
	void					 FolderOp (const char *, const char *, const char *);
	std::string				 CheckFolderName (const std::string &);
	bool					 IsFixedFolder (const char *name);

protected:
	std::string				 Root (void);
	bool					 Scan (void);
	std::string				 Path (const Entry &);
	bool					 ChangeFlags (Entry &, const std::string &add, const std::string &remove);
	std::string				 MissingText (void);
	const char				*Kind (void) { return "mh"; }

public:
	struct Profile
	{
		std::string			 Path;			// mail directory
		std::string			 Inbox;			// inbox folder name
		std::vector<std::string> Unseen;	// Unseen-Sequence names (the first is used)
		std::string			 Rmmproc;		// program that "deletes" messages
		std::string			 SeqFile;		// sequences file name
	};
	Profile					 ReadProfile (void);

private:
	bool					 Store (const std::string &dir, const std::string &raw,
									const std::string &flags, std::string &err);
	int						 NextNumber (const std::string &dir);
};

class OutBoxHandler : public MailHandler
{
public:
							 OutBoxHandler (MailBox *, Var *);
							~OutBoxHandler (void);
	
	void					 OpenConnection (void);
	void					 CloseConnection (void);
	void					 GetBody (char *);
	void					 CheckGroup (const char *);
	void					 DeleteMessage (char *);
	void					 Expire (void);
};

class MBoxHandler : public MailHandler
{
public:
							 MBoxHandler (MailBox *, Var *);
							~MBoxHandler (void);
	
	void					 OpenConnection (void);
	void					 CloseConnection (void);
	void					 GetBody (char *);
	void					 CheckGroup (const char *);
	void					 DeleteMessage (char *);
	void					 Expire (void);
	
private:
	
	void					 Announce (MailMessage *);
	WordList				*MBFiles;
	MBoxStorage			   **MBStorage;
	char					*lock_filename;
};

#endif
