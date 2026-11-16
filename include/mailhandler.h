#ifndef _MAILHANDLER_H
#define _MAILHANDLER_H 1

#include <map>
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
	virtual void			 Expire (void);
	virtual int				 PostMessage (WordList *, MailMessage *);
//	virtual WordList		*ServerGrouplist (void);
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

	WordList				*GroupList (void);
		
	char					 ConnectionOpen;
	Var						*Parameters;
	MailBox					*Parent;
	ASemaphore				 Lock;
};

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
	int						 PostMessage (WordList *, MailMessage *);
//	WordList				*ServerGrouplist (void);
	
private:
	
	Var						*UIMsgID;
	Connection				*Con;
	int						 State;
	Var						*TransMsgID;
	int						 HighestHandled;
	
	int						 CheckReply (char *, int);
	int						 LastChecked;
};

class IMAP4Handler : public MailHandler
{
public:
	IMAP4Handler (MailBox *, Var *);
	~IMAP4Handler (void);
	
	void					 OpenConnection (void);
	void					 CloseConnection (void);
	void					 GetBody (char *);
	void					 CheckGroup (const char *);
	void					 DeleteMessage (char *);
	void					 Expire (void);
	int						 PostMessage (WordList *, MailMessage *);
//	WordList				*ServerGrouplist (void);
	
		//void					 MoveMessage (char *, char *, char *);
	
private:
	
	Var						*UIMsgID;
	Connection				*Con;
	int						 State;
	Var						*TransMsgID;
	int						 HighestHandled;
	
	int						CheckReply();
	int						 CheckReply (char *, int);
	int						CmdNum;
	char					CmdPrefix;
	void					SendCmd(const char*);
	
};
class SQLHandler : public MailHandler
{
public:
	SQLHandler (MailBox *, Var *);
	~SQLHandler (void);
	
	void					 OpenConnection (void);
	void					 CloseConnection (void);
	void					 GetBody (char *);
	void					 CheckGroup (const char *);
	void					 DeleteMessage (char *);
	void					 Expire (void);
	int						 PostMessage (WordList *, MailMessage *);
//	WordList				*ServerGrouplist (void);
	
private:
	
	podb::Connection		*SQL;
	int						 CheckError (bool ok);
	std::string				 ConnInfo (void);
	std::string				 Table (void);
	std::string				 MsgID (long long);
	long long				 MsgToSQL (const char *);
	bool					 FoldersChanged;
};

class MaildirHandler : public MailHandler
{
public:
							 MaildirHandler (MailBox *, Var *);
							~MaildirHandler (void);

	void					 OpenConnection (void);
	void					 CloseConnection (void);
	void					 GetBody (char *);
	void					 CheckGroup (const char *);
	void					 DeleteMessage (char *);
	void					 Expire (void);
	int						 PostMessage (WordList *, MailMessage *);
	void					 MarkRead (char *);
	bool					 KeepsFlags (void) { return true; }
	bool					 CanMove (void) { return true; }
	void					 SetFlag (char *, char, bool);
	void					 MoveMessage (char *, const char *);
	bool					 ExportRaw (char *, std::string &, std::string &);
	bool					 DeliverRaw (const std::string &, const char *, const std::string &, std::string &);
	bool					 CanManageFolders (void) { return true; }
	void					 FolderOp (const char *, const char *, const char *);

private:

	struct Entry
	{
		std::string			 Dir;	// "" (top level) or ".Work"
		std::string			 Sub;	// "new" or "cur"
		std::string			 File;	// file name, with ":2,flags"
		std::string			 Flags;	// "FS"
	};

	std::string				 Root (void);
	std::string				 Path (const Entry &);
	std::string				 MsgID (const std::string &dir, const std::string &uniq);
	bool					 Scan (void);
	Entry					*Locate (const char *msgid);
	bool					 MarkSeen (Entry &);
	bool					 ChangeFlags (Entry &, const std::string &add, const std::string &remove);
	bool					 ApplyFlags (MailMessage *, const Entry &);

	std::map<std::string, Entry> Files;	// msgid -> file, as of the last scan
	std::vector<std::string> Dirs;		// "" and the Maildir++ subfolders
	bool					 FoldersChanged;
	bool					 ReportedMissing;
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
	int						 PostMessage (WordList *, MailMessage *);
//	WordList				*ServerGrouplist (void);
};

class NNTPHandler : public MailHandler
{
public:
							 NNTPHandler (MailBox *, Var *);
							~NNTPHandler (void);
	
	void					 OpenConnection (void);
	void					 CloseConnection (void);
	void					 GetBody (char *);
	void					 CheckGroup (const char *);
	void					 DeleteMessage (char *);
	void					 Expire (void);
	int						 PostMessage (WordList *, MailMessage *);
//	WordList				*ServerGrouplist (void);
	
private:
	
	Connection				*Con;
	int						 CheckReply (int, char *, int, int alternative=1);
	int						 CheckReply (int, int alternative=1);
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
	int						 PostMessage (WordList *, MailMessage *);
	
private:
	
	void					 Announce (MailMessage *);
	WordList				*MBFiles;
	MBoxStorage			   **MBStorage;
	int						 lockfd; /* so that we can cleanup */
	char					*lock_filename;
};

#endif
