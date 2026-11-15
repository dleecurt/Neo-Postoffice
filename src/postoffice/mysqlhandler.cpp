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

#include "mailbox.h"
#include "var.h"
#include "reg.h"
#include "chop.h"
#ifndef NO_MYSQL
#include "mysqlclient.h"
#endif
#include "encode.h"
#include "status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

extern JobList JOBQ;

MySQLHandler::MySQLHandler (MailBox *_Parent, class Var *_Parameters)
{
#ifndef NO_MYSQL
	SQL = NULL;
	Parameters = new Var (_Parameters);
	Parent = _Parent;
#endif
}

MySQLHandler::~MySQLHandler (void)
{
#ifndef NO_MYSQL
	if (SQL) CloseConnection ();
#endif
}

void MySQLHandler::OpenConnection (void)
{
#ifndef NO_MYSQL
	char *msg;
	
	char *pHost = Parameters->Get_char ("host");
	int   pPort = Parameters->Get_int ("port");
	char *pUser = Parameters->Get_char ("user");
	char *pPass = Parameters->Get_char ("pass");
	char *pTable = Parameters->Get_char ("table");
	char *pDB = Parameters->Get_char ("database");

	if (!pPort) pPort = 3306;

	if (!SQL)
	{
		if ( (strlen (pHost) == 0) || (pPort == 0) ||
		     (strlen (pUser) == 0) || (strlen (pTable) == 0))
		{
			Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
									   "mysqlhandler", "Invalid Hostname, Username, "
									   "Database or Table. You blew it.");
			return;
		}

		msg = new char[ strlen (pHost) + 32];
		int jid = JOBQ.NewJob (Parent->Name);
		sprintf (msg, "Contacting MySQL host %s", pHost);
		JOBQ.Stat (jid, msg);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

		delete[] (msg);	

		SQL = dbdriver_open (pHost, pPort, pUser, pPass, pDB);
		if (!SQL)
		{
			Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
									   "mysqlhandler", "Error connecting to database");
		}
		else Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Connected");
		JOBQ.EndJob (jid);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
	}
#endif
}

void MySQLHandler::CloseConnection (void)
{
#ifndef NO_MYSQL
	if (SQL)
	{
		dbdriver_close (SQL);
		SQL = NULL;
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Connection closed");
	}
#endif
}

void MySQLHandler::CheckGroup (char *groupname)
{
#ifndef NO_MYSQL
	char 			*_groupname;
	WordList		*res, *dbr, *dbc;
	char			 qry[512];
	char 			*pDB = Parameters->Get_char ("database");
	char 			*pHost = Parameters->Get_char ("host");
	char 			*pTable = Parameters->Get_char ("table");
	MailMessage		*M;
	char			 msgid[64];
	int				 i,j;
	
	// If no groupname is specified, override it with a wildcard (check all)
	
	if (strlen (groupname) < 2) _groupname = (char *) "%";
	else _groupname = groupname;
	
	if (!SQL) OpenConnection(); // Open connection on demand
	if (SQL)
	{
		int jid = JOBQ.NewJob (Parent->Name);
		JOBQ.Stat (jid, "Opening folder...");
		JOBQ.Progress (jid, 10);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
	
		// Find about all active message-ids the server has for us
	
		sprintf (qry, "SELECT msgid FROM %s WHERE folder='%s' ORDER BY msgid", pTable, _groupname);
		res = dbdriver_execute (SQL, qry);
		
		if (CheckError()) // Make sure my SQL majick didn't leave us
		{
			sprintf (qry, "%i Messages found...", res->Count);
			JOBQ.Progress (jid, 20);
			JOBQ.Stat (jid, qry);
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		
			if (res->Count) // Did we get a meaningful amount of message-ids?
			{
				for (i=0;i<res->Count;++i) 
				{
					// Create a unique message-id for this message
				
					sprintf (msgid, "<MySQL.%s:%s.%08i@%s>", pDB, pTable, res->GetInt (i), pHost);
					
					if (!Parent->Have (msgid)) // Do we have one yet?
					{
						sprintf (qry, "Processing message %i %s", i, msgid);
						JOBQ.Progress (jid, 20+((70*i)/(res->Count)));
						JOBQ.Stat (jid, qry);
						Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
						
						// Retrieve the envelope headers
						
						sprintf (qry, "SELECT namefrom,mailfrom,subject,received,folder FROM %s "
									  "WHERE msgid=%i", pTable, res->GetInt (i));
						
						dprintf ("[sql] %s\n", qry);
						dbr = dbdriver_execute (SQL, qry);
						dprintf ("[sql] done\n");
						
						if (CheckError()) // In case they want to complain about my syntax
						{
							if (dbr->Count) // One of those "can't happen" situations ;)
							{
								dbc = dbr->GetWordList (0); // Break up the result row.
								M = new MailMessage (Parent, msgid);
								M->Envelope->Set (ENVELOPE_NAMEFROM, dbc->Get (0));
								M->Envelope->Set (ENVELOPE_MAILFROM, dbc->Get (1));
								M->Envelope->Set (ENVELOPE_SUBJECT,  dbc->Get (2));
								M->Envelope->Set (ENVELOPE_DATE,     dbc->Get (3));
								M->Envelope->Set (ENVELOPE_GROUPS,	 dbc->Get (4));
								Parent->Deliver (M); // Send the message into limbo
								
								Parent->Lock->LockRW();
								for (j=0;j<M->Subscribers->Count();++j)
								{
									ViewListNode *VLN = M->Subscribers->Get(j);
									if (VLN) Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, VLN->View->Name, M->MsgID);
								}
								Parent->Lock->UnlockRW();
							}
							else
							{
								Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
														   "mysqldriver", "Message select failed.");
							}
						}
						delete (dbr);
					}
				}
			}
			Parent->SaveMailBox();
		}
		delete (res);
		JOBQ.EndJob (jid);
	}
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
#endif
}

void MySQLHandler::GetBody (char *x)
{
#ifndef NO_MYSQL
	int			 idx = MsgToSQL (x); // We only need the numbers of the msgid
	WordList	*res;
	char		 qry[256];
	char 		*pTable = Parameters->Get_char ("table");
	MailMessage	*M;
	
	M = Parent->Find (x); // Make sure there's a mailmessage to put this all in
	if (!M) // OK, so the GUI side goofed
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mysqlhandler",
								   "Message not found in internal database");
		return;
	}
	
	if (!SQL) OpenConnection (); // Open connection on demand
	if (SQL) // At least we hope so
	{
		int jid = JOBQ.NewJob (Parent->Name);
		JOBQ.Stat (jid, "Updating status");
		JOBQ.Progress (jid, 5);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

		// Mark the message as read
		
		sprintf (qry, "UPDATE %s SET msgread=1 WHERE msgid=%i", pTable, idx);
		res = dbdriver_execute (SQL, qry);
		delete (res);
		
		if (CheckError()) // They let us do that
		{
			JOBQ.Progress (jid, 10);
			JOBQ.Stat (jid, "Downloading message");
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		
			// Download the binary goo
		
			sprintf (qry, "SELECT body FROM %s WHERE msgid=%i", pTable, idx);
			res = dbdriver_execute (SQL, qry);
			
			if (CheckError()) // If $DEITY allows
			{
				if (res->Count) // And there's any data
				{
					char *ascii_message = decode_mysql (res->Get(0)); // Turn it into 8bit
					M->ProcessMessage (ascii_message); // And let the message figure it out
					delete[] (ascii_message);
				}
				else Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
											    "mysqlhandler", "Message does not exist");
			}
			
			delete (res);
			
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
		}
		JOBQ.EndJob (jid);
	}
#endif
}

void MySQLHandler::DeleteMessage (char *msgid)
{
#ifndef NO_MYSQL
	int		 idx = MsgToSQL (msgid);
	char	 qry[128];
	char	*pTable = Parameters->Get_char ("table");
	
	if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y"))
	{
		if (!SQL) OpenConnection();
		if (SQL)
		{
			sprintf (qry, "DELETE FROM %s WHERE msgid=%i", pTable, idx);
			delete (dbdriver_execute (SQL, qry));
			CheckError();
		}
	}
#endif
}

int MySQLHandler::MsgToSQL (char *msgid)
{
#ifndef NO_MYSQL
	char *crsr;
	
	if (!(crsr = strchr (msgid, '.'))) return 0;
	if (!(crsr = strchr (crsr+1, '.'))) return 0;
	return (atoi (crsr+1));
#else
	(void) msgid;
	return 0;
#endif
}

int MySQLHandler::CheckError (void)
{
#ifndef NO_MYSQL
	char *err;
	
	if (SQL)
	{
		err = dbdriver_error(SQL);
		if (strlen (err))
		{
			char *msg = new char[strlen(err)+64];
			sprintf (msg, "MySQL is nagging me with: %s", err);
			Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mysqlhandler", msg);
			delete[] msg;
			return 0;
		}
		return 1;
	}
	return 0;
#else
	return 0;
#endif
}

void MySQLHandler::Expire (void)
{
}

int MySQLHandler::PostMessage (WordList *groups, MailMessage *M)
{
	return 0;
}
