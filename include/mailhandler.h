#ifndef _MAILHANDLER_H
#define _MAILHANDLER_H 1

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
	void					 EnsureFolder (const std::string &);
	bool					 FoldersChanged;
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
