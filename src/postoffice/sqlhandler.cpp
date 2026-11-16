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
// SQLHandler
// ----------
// The SQL mailbox: reads mail from a PostgreSQL table (po_mail, see
// docs/sql-mailbox-postgresql.sql) that another program fills, normally
// getmail or fetchmail through tools/po-sqldeliver.
//
// Several users share the table; the server's row-level security shows
// each login only its own rows. Folders are taken from the table's folder
// column and created in the client as they appear. Messages deleted in the
// database (by another client) disappear here on the next check.
//
// Neo-PostOffice rewrite of the original MySQLHandler.
//===============================================================================

#include "mailbox.h"
#include "var.h"
#include "podb.h"
#include "status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "debug.h"

extern JobList JOBQ;

// The table name comes from the mailbox settings and cannot be a query
// parameter, so only plain identifiers are accepted.
static bool valid_table_name (const char *t)
{
	if (!t || !*t || strlen (t) > 63) return false;
	for (const char *p = t; *p; ++p)
		if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
		      (*p >= '0' && *p <= '9') || *p == '_'))
			return false;
	return true;
}

// Quotes a value for a libpq connection string.
static std::string conninfo_value (const std::string &v)
{
	std::string out = "'";
	for (char c : v)
	{
		if (c == '\\' || c == '\'') out += '\\';
		out += c;
	}
	return out + "'";
}

SQLHandler::SQLHandler (MailBox *_Parent, class Var *_Parameters)
{
	SQL = NULL;
	FoldersChanged = false;
	Parameters = new Var (_Parameters);
	Parent = _Parent;
}

SQLHandler::~SQLHandler (void)
{
	if (SQL) CloseConnection ();
}

// Builds the libpq connection string from the mailbox settings. A server
// name means a network connection, which always uses SSL with full
// certificate checking; a socket directory (or nothing) is a local
// connection without SSL. Passwords come from ~/.pgpass.
std::string SQLHandler::ConnInfo (void)
{
	std::string host = Parameters->Get_char ("host");
	std::string port = Parameters->Get_char ("port");
	std::string user = Parameters->Get_char ("user");
	std::string db = Parameters->Get_char ("database");
	std::string ca = Parameters->Get_char ("cafile");

	// "server:port"
	size_t colon = host.rfind (':');
	if (colon != std::string::npos && host.find (':') == colon && host[0] != '/')
	{
		port = host.substr (colon + 1);
		host.erase (colon);
	}

	std::string ci = "application_name='Neo-PostOffice' connect_timeout=20";
	if (!host.empty()) ci += " host=" + conninfo_value (host);
	if (!port.empty()) ci += " port=" + conninfo_value (port);
	if (!user.empty()) ci += " user=" + conninfo_value (user);
	if (!db.empty())   ci += " dbname=" + conninfo_value (db);
	if (!host.empty() && host[0] != '/')
	{
		ci += " sslmode=verify-full";
		if (!ca.empty())
		{
			if (ca[0] == '~' && getenv ("HOME")) ca = std::string (getenv ("HOME")) + ca.substr (1);
			ci += " sslrootcert=" + conninfo_value (ca);
		}
	}
	return ci;
}

void SQLHandler::OpenConnection (void)
{
	if (SQL) return;

	if (!podb::HavePostgreSQL())
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "sqlhandler",
								   "This build of Neo-PostOffice has no PostgreSQL support.");
		return;
	}
	if (!valid_table_name (Parameters->Get_char ("table")))
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "sqlhandler",
								   "Invalid table name (only letters, digits and _ are allowed).");
		return;
	}

	const char *host = Parameters->Get_char ("host");
	std::string msg = std::string ("Contacting database server ") + (*host ? host : "(local)");
	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Stat (jid, msg.c_str());
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	std::string error;
	SQL = podb::ConnectPostgreSQL (ConnInfo(), error).release();
	if (!SQL)
	{
		std::string e = "Cannot connect to the mail database: " + error;
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "sqlhandler", e.c_str());
	}
	else Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Connected");

	JOBQ.EndJob (jid);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
}

void SQLHandler::CloseConnection (void)
{
	if (SQL)
	{
		delete SQL;
		SQL = NULL;
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Connection closed");
	}
}

std::string SQLHandler::Table (void)
{
	return std::string ("\"") + Parameters->Get_char ("table") + "\"";
}

// The client's message id for a row: "<id.table.database@server>".
std::string SQLHandler::MsgID (long long id)
{
	const char *host = Parameters->Get_char ("host");
	char buf[512];
	snprintf (buf, sizeof (buf), "<%lld.%s.%s@%s>", id, Parameters->Get_char ("table"),
			  Parameters->Get_char ("database"), *host ? host : "local");
	return buf;
}

long long SQLHandler::MsgToSQL (const char *msgid)
{
	if (!msgid || msgid[0] != '<') return 0;
	return atoll (msgid + 1);
}

// Reads the message list of the whole mailbox (whatever folder is
// selected): creates missing folders, adds new messages, takes over the
// "seen" state, and reports messages that were deleted in the database.
void SQLHandler::CheckGroup (const char */*groupname*/)
{
	podb::Result res;

	if (!SQL) OpenConnection(); // Open connection on demand
	if (!SQL) return;

	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Stat (jid, "Reading message list...");
	JOBQ.Progress (jid, 10);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	bool ok = SQL->Exec ("SELECT id, folder, from_name, from_addr, subject,"
						 " extract (epoch FROM coalesce (sent, received))::bigint,"
						 " extract (epoch FROM received)::bigint, seen"
						 " FROM " + Table() + " ORDER BY id", podb::Params(), &res);
	if (!CheckError (ok))
	{
		JOBQ.EndJob (jid);
		return;
	}

	// 1. Folders, before any message is delivered into them.
	FoldersChanged = false;
	std::set<std::string> folders;
	for (int i = 0; i < res.Rows(); ++i) folders.insert (res.Get (i, 1));
	for (const std::string &f : folders) if (Parent->EnsureFolder (f)) FoldersChanged = true;

	// 2. Messages that are gone from the database. The window removes
	//    them, as it does for its own Delete button.
	std::set<std::string> present;
	for (int i = 0; i < res.Rows(); ++i) present.insert (MsgID (atoll (res.Get (i, 0).c_str())));
	std::vector<std::string> gone;
	Parent->Lock->LockR();
	for (MailMessage *M = (MailMessage *) Parent->Root->BNext; M && M != Parent->Root;
		 M = (MailMessage *) M->BNext)
		if (!present.count (M->MsgID)) gone.push_back (M->MsgID);
	Parent->Lock->UnlockR();
	for (const std::string &id : gone)
		Parent->ToUI->PostMessage (CLASS_GONE, NO_SUBCLASS, Parent->Name, id.c_str());

	// 3. New messages, and the read state of known ones.
	int added = 0;
	std::vector<long long> mark_seen;
	for (int i = 0; i < res.Rows(); ++i)
	{
		long long id = atoll (res.Get (i, 0).c_str());
		bool seen = res.Get (i, 7) == "t";
		std::string msgid = MsgID (id);

		if (MailMessage *M = Parent->Find ((char *) msgid.c_str()))
		{
			// Read on another computer, or here while the database was away.
			if (seen && !M->Envelope->GetInt (ENVELOPE_I_READ)) M->Envelope->SetInt (ENVELOPE_I_READ, 1);
			else if (!seen && M->Envelope->GetInt (ENVELOPE_I_READ)) mark_seen.push_back (id);
			continue;
		}

		if ((i & 31) == 0)
		{
			std::string st = "Reading message " + std::to_string (i + 1) + " of " + std::to_string (res.Rows());
			JOBQ.Progress (jid, 20 + (70 * i) / res.Rows());
			JOBQ.Stat (jid, st.c_str());
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		}

		time_t sent = (time_t) atoll (res.Get (i, 5).c_str());
		char date[64];
		struct tm tmv;
		localtime_r (&sent, &tmv);
		strftime (date, sizeof (date), "%a, %d %b %Y %H:%M:%S %z", &tmv);

		std::string from_name = res.Get (i, 2), from_addr = res.Get (i, 3);
		MailMessage *M = new MailMessage (Parent, msgid.c_str());
		M->Envelope->Set (ENVELOPE_NAMEFROM, (from_name.empty() ? from_addr : from_name).c_str());
		M->Envelope->Set (ENVELOPE_MAILFROM, from_addr.c_str());
		M->Envelope->Set (ENVELOPE_SUBJECT,  res.Get (i, 4).c_str());
		M->Envelope->Set (ENVELOPE_DATE,     date);
		M->Envelope->Set (ENVELOPE_GROUPS,   res.Get (i, 1).c_str());
		M->Envelope->SetInt (ENVELOPE_I_READ, seen ? 1 : 0);
		M->ReceiveDate = (time_t) atoll (res.Get (i, 6).c_str());
		Parent->Deliver (M);
		++added;

		Parent->Lock->LockRW();
		for (int j = 0; j < M->Subscribers->Count(); ++j)
		{
			ViewListNode *VLN = M->Subscribers->Get (j);
			if (VLN) Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, VLN->View->Name, M->MsgID);
		}
		Parent->Lock->UnlockRW();
	}
	for (long long id : mark_seen)
		CheckError (SQL->Exec ("UPDATE " + Table() + " SET seen = true WHERE id = $1", { (long) id }));

	if (added || !gone.empty()) Parent->SaveMailBox();
	// Redraw the folder tree (new folders, message counts) after the
	// window has handled the removals posted above.
	if (FoldersChanged || !gone.empty())
		Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	JOBQ.EndJob (jid);
}

void SQLHandler::GetBody (char *x)
{
	long long	 id = MsgToSQL (x);
	podb::Result res;
	MailMessage	*M;

	M = Parent->Find (x); // Make sure there's a mailmessage to put this all in
	if (!M) // OK, so the GUI side goofed
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "sqlhandler",
								   "Message not found in internal database");
		return;
	}

	if (!SQL) OpenConnection (); // Open connection on demand
	if (!SQL) return;

	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Stat (jid, "Downloading message");
	JOBQ.Progress (jid, 10);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	if (CheckError (SQL->Exec ("SELECT raw FROM " + Table() + " WHERE id = $1", { (long) id }, &res)))
	{
		if (res.Rows() && !res.IsNull (0, 0))
		{
			// The message as received; a NUL byte in it would end it early.
			std::vector<char> text (res.Get (0, 0).begin(), res.Get (0, 0).end());
			text.push_back (0);
			M->ProcessMessage (text.data());
			CheckError (SQL->Exec ("UPDATE " + Table() + " SET seen = true WHERE id = $1 AND NOT seen",
								   { (long) id }));
		}
		else Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "sqlhandler",
										"This message is no longer in the database.");
	}
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
	JOBQ.EndJob (jid);
}

void SQLHandler::DeleteMessage (char *msgid)
{
	if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y") == 0) return;
	if (!SQL) OpenConnection();
	if (SQL) CheckError (SQL->Exec ("DELETE FROM " + Table() + " WHERE id = $1", { (long) MsgToSQL (msgid) }));
}

int SQLHandler::CheckError (bool ok)
{
	if (ok) return 1;
	if (SQL)
	{
		std::string msg = "Mail database error: " + SQL->Error();
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "sqlhandler", msg.c_str());
	}
	return 0;
}

void SQLHandler::Expire (void)
{
}

int SQLHandler::PostMessage (WordList */*groups*/, MailMessage */*M*/)
{
	return 0;
}
