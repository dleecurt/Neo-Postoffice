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
//===============================================================================
// MySQLHandler
// ------------
// A mailbox type that reads mail from a MySQL table filled by another program.
// Neo-PostOffice: uses the podb database layer with query parameters instead
// of the original dbdriver_* functions.
//===============================================================================

#include "mailbox.h"
#include "var.h"
#include "reg.h"
#include "chop.h"
#include "podb.h"
#include "encode.h"
#include "status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include "debug.h"

extern JobList JOBQ;

// The table name comes from the mailbox settings and cannot be a query
// parameter, so only plain identifiers are accepted.
static bool valid_table_name (const char *t)
{
	if (!t || !*t || strlen (t) > 64) return false;
	for (const char *p = t; *p; ++p)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '_'))
			return false;
	return true;
}

MySQLHandler::MySQLHandler (MailBox *_Parent, class Var *_Parameters)
{
	SQL = NULL;
	Parameters = new Var (_Parameters);
	Parent = _Parent;
}

MySQLHandler::~MySQLHandler (void)
{
	if (SQL) CloseConnection ();
}

void MySQLHandler::OpenConnection (void)
{
	char *pHost = Parameters->Get_char ("host");
	int   pPort = Parameters->Get_int ("port");
	char *pUser = Parameters->Get_char ("user");
	char *pPass = Parameters->Get_char ("pass");
	char *pTable = Parameters->Get_char ("table");
	char *pDB = Parameters->Get_char ("database");

	if (!pPort) pPort = 3306;
	if (SQL) return;

	if (!podb::HaveMySQL())
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mysqlhandler",
								   "This build of Neo-PostOffice has no MySQL support "
								   "(build with -DWITH_MYSQL=ON).");
		return;
	}
	if ( (strlen (pHost) == 0) || (strlen (pUser) == 0) || !valid_table_name (pTable))
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
								   "mysqlhandler", "Invalid host name, user name or table "
								   "(table names may only contain letters, digits and _).");
		return;
	}

	std::string msg = std::string ("Contacting MySQL host ") + pHost;
	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Stat (jid, msg.c_str());
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	std::string error;
	SQL = podb::ConnectMySQL (pHost, pPort, pUser, pPass, pDB, error).release();
	if (!SQL)
	{
		std::string e = "Error connecting to database: " + error;
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mysqlhandler", e.c_str());
	}
	else Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Connected");

	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}

void MySQLHandler::CloseConnection (void)
{
	if (SQL)
	{
		delete SQL;
		SQL = NULL;
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Connection closed");
	}
}

void MySQLHandler::CheckGroup (const char *groupname)
{
	podb::Result	 res, dbr;
	char 			*pDB = Parameters->Get_char ("database");
	char 			*pHost = Parameters->Get_char ("host");
	char 			*pTable = Parameters->Get_char ("table");
	MailMessage		*M;
	char			 msgid[256];
	int				 i,j;

	if (!SQL) OpenConnection(); // Open connection on demand
	if (!SQL) return;

	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Stat (jid, "Opening folder...");
	JOBQ.Progress (jid, 10);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	// Find all message ids the server has for us. Without a folder name,
	// check all folders.
	std::string table = std::string ("`") + pTable + "`";
	bool ok;
	if (strlen (groupname) < 2)
		ok = SQL->Exec ("SELECT msgid FROM " + table + " ORDER BY msgid", podb::Params(), &res);
	else
		ok = SQL->Exec ("SELECT msgid FROM " + table + " WHERE folder=$1 ORDER BY msgid",
						{ groupname }, &res);

	if (CheckError (ok))
	{
		std::string st = std::to_string (res.Rows()) + " Messages found...";
		JOBQ.Progress (jid, 20);
		JOBQ.Stat (jid, st.c_str());
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

		for (i=0;i<res.Rows();++i)
		{
			long id = res.GetLong (i, 0);

			// Create a unique message-id for this message
			snprintf (msgid, sizeof (msgid), "<MySQL.%s:%s.%08li@%s>", pDB, pTable, id, pHost);

			if (Parent->Have (msgid)) continue; // Do we have one yet?

			std::string pr = std::string ("Processing message ") + std::to_string (i) + " " + msgid;
			JOBQ.Progress (jid, 20+((70*i)/(res.Rows())));
			JOBQ.Stat (jid, pr.c_str());
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

			// Retrieve the envelope headers
			ok = SQL->Exec ("SELECT namefrom,mailfrom,subject,received,folder FROM " + table +
							" WHERE msgid=$1", { id }, &dbr);
			if (!CheckError (ok)) break;

			if (dbr.Rows())
			{
				M = new MailMessage (Parent, msgid);
				M->Envelope->Set (ENVELOPE_NAMEFROM, dbr.Get (0, 0).c_str());
				M->Envelope->Set (ENVELOPE_MAILFROM, dbr.Get (0, 1).c_str());
				M->Envelope->Set (ENVELOPE_SUBJECT,  dbr.Get (0, 2).c_str());
				M->Envelope->Set (ENVELOPE_DATE,     dbr.Get (0, 3).c_str());
				M->Envelope->Set (ENVELOPE_GROUPS,	 dbr.Get (0, 4).c_str());
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
		Parent->SaveMailBox();
	}
	JOBQ.EndJob (jid);
}

void MySQLHandler::GetBody (char *x)
{
	long		 idx = MsgToSQL (x); // We only need the numbers of the msgid
	podb::Result res;
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
	if (!SQL) return;

	std::string table = std::string ("`") + pTable + "`";
	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Stat (jid, "Updating status");
	JOBQ.Progress (jid, 5);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	// Mark the message as read
	if (CheckError (SQL->Exec ("UPDATE " + table + " SET msgread=1 WHERE msgid=$1", { idx })))
	{
		JOBQ.Progress (jid, 10);
		JOBQ.Stat (jid, "Downloading message");
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

		// Download the message
		if (CheckError (SQL->Exec ("SELECT body FROM " + table + " WHERE msgid=$1", { idx }, &res)))
		{
			if (res.Rows() && !res.IsNull (0, 0))
			{
				// Undo the original @xx encoding of the body column
				char *ascii_message = decode_mysql ((char *) res.Get (0, 0).c_str());
				M->ProcessMessage (ascii_message); // And let the message figure it out
				delete[] (ascii_message);
			}
			else Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
										    "mysqlhandler", "Message does not exist");
		}
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
	}
	JOBQ.EndJob (jid);
}

void MySQLHandler::DeleteMessage (char *msgid)
{
	long	 idx = MsgToSQL (msgid);
	char	*pTable = Parameters->Get_char ("table");

	if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y"))
	{
		if (!SQL) OpenConnection();
		if (SQL)
		{
			std::string table = std::string ("`") + pTable + "`";
			CheckError (SQL->Exec ("DELETE FROM " + table + " WHERE msgid=$1", { idx }));
		}
	}
}

long MySQLHandler::MsgToSQL (char *msgid)
{
	// msgid looks like <MySQL.db:table.00000042@host>
	char *crsr;

	if (!(crsr = strchr (msgid, '.'))) return 0;
	if (!(crsr = strchr (crsr+1, '.'))) return 0;
	return (atol (crsr+1));
}

int MySQLHandler::CheckError (bool ok)
{
	if (ok) return 1;
	if (SQL)
	{
		std::string msg = "MySQL error: " + SQL->Error();
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "mysqlhandler", msg.c_str());
	}
	return 0;
}

void MySQLHandler::Expire (void)
{
}

int MySQLHandler::PostMessage (WordList */*groups*/, MailMessage */*M*/)
{
	return 0;
}
