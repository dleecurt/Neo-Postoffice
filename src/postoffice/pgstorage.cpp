//===============================================================================
// PGStorage
// ---------
// Neo-PostOffice: a MailStorage implementation that keeps a mailbox's
// messages, and its index (folders, envelope, read state), in PostgreSQL.
//
// Tables (created on first use, see docs/postgresql-storage.sql):
//
//   po_messages  one row per message: index columns, stored headers
//   po_parts     the message parts (bytea, so binary data survives exactly)
//
// Rows are keyed by (owner, mailbox, msgid), with owner defaulting to the
// database user, so several people can share one database. The password
// comes from libpq (~/.pgpass or the PGPASSWORD environment), never from
// PostOffice's own settings.
//
// Saving the index only inserts or updates rows. Rows are deleted only when
// a message is deleted, so a failed load can never wipe out stored mail.
//===============================================================================

#include "mailstorage.h"
#include "mailbox.h"
#include "mailmessage.h"
#include "strdupp.h"
#include "debug.h"
#include "podb.h"
#include "itc.h"

#include <string.h>

static const char *SCHEMA_MESSAGES =
	"CREATE TABLE po_messages ("
	"  owner        text    NOT NULL DEFAULT current_user,"
	"  mailbox      text    NOT NULL,"
	"  msgid        text    NOT NULL,"
	"  parentid     text,"
	"  mailfrom     text,"
	"  namefrom     text,"
	"  subject      text,"
	"  msgdate      text,"
	"  folders      text[]  NOT NULL DEFAULT '{}',"
	"  flags        integer NOT NULL DEFAULT 0,"
	"  readstate    integer NOT NULL DEFAULT 0,"
	"  partcount    integer NOT NULL DEFAULT 0,"
	"  received     timestamptz,"
	"  headers      bytea,"
	"  stored_size  bigint  NOT NULL DEFAULT 0,"
	"  stored_parts integer NOT NULL DEFAULT 0,"
	"  PRIMARY KEY (owner, mailbox, msgid))";

static const char *SCHEMA_PARTS =
	"CREATE TABLE po_parts ("
	"  owner        text    NOT NULL,"
	"  mailbox      text    NOT NULL,"
	"  msgid        text    NOT NULL,"
	"  partno       integer NOT NULL,"
	"  contenttype  text,"
	"  filename     text,"
	"  description  text,"
	"  contents     bytea   NOT NULL,"
	"  PRIMARY KEY (owner, mailbox, msgid, partno),"
	"  FOREIGN KEY (owner, mailbox, msgid) REFERENCES po_messages"
	"      ON DELETE CASCADE ON UPDATE CASCADE)";

// Splits "a\nb\nc" into its lines.
static std::vector<std::string> split_lines (const std::string &s)
{
	std::vector<std::string> out;
	size_t start = 0;
	if (s.empty()) return out;
	for (;;)
	{
		size_t nl = s.find ('\n', start);
		out.push_back (s.substr (start, nl == std::string::npos ? std::string::npos : nl - start));
		if (nl == std::string::npos) break;
		start = nl + 1;
	}
	return out;
}

PGStorage::PGStorage (const char *name, const char *mailbox, const char *conninfo)
{
	Name = strdupp (name);
	Mailbox = podb::Utf8 (mailbox);
	Ready = false;

	std::string err;
	Conn = podb::ConnectPostgreSQL (conninfo ? conninfo : "", err);
	if (!Conn)
	{
		Error = "Cannot connect to PostgreSQL: " + err;
		eprintf ("[pgstorage] %s\n", Error.c_str());
		return;
	}
	Ready = EnsureSchema();
}

PGStorage::~PGStorage (void)
{
	dprintf ("pgstorage [%s] deleted\n", Name);
	delete[] Name;
}

bool PGStorage::Usable (void)
{
	return Ready && Conn && Conn->Connected();
}

std::string PGStorage::LastError (void)
{
	return Error;
}

// Records and reports a failed statement. Returns ok.
bool PGStorage::Check (bool ok, const char *what)
{
	if (ok) return true;
	std::string e = std::string (what) + ": " + (Conn ? Conn->Error() : std::string ("not connected"));
	eprintf ("[pgstorage] %s\n", e.c_str());

	// Report each new error once to the mailboxes using this storage.
	if (e != Error)
	{
		Error = e;
		std::string msg = "PostgreSQL storage for " + Mailbox + ": " + e;
		for (MailBoxRef *R = SFirst; R; R = R->Next)
			if (R->Ref && R->Ref->ToUI)
				R->Ref->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "pgstorage", msg.c_str());
	}
	return false;
}

// Creates the tables if they do not exist yet. Existing tables are left
// alone, so a database administrator can create them in advance (and the
// user then needs no CREATE privilege).
bool PGStorage::EnsureSchema (void)
{
	std::lock_guard<std::recursive_mutex> g (Conn->Mutex);
	podb::Result r;

	if (!Check (Conn->Exec ("SELECT to_regclass('po_messages') IS NOT NULL,"
							"       to_regclass('po_parts') IS NOT NULL", podb::Params(), &r),
				"checking for tables"))
		return false;
	if (r.Get (0, 0) == "t" && r.Get (0, 1) == "t") return true;

	// Serialize creation between clients starting at the same time.
	bool ok = Check (Conn->Begin(), "creating tables")
		&& Check (Conn->Exec ("SELECT pg_advisory_xact_lock(hashtext('neo-postoffice schema'))"),
				  "creating tables")
		&& Check (Conn->Exec ("SELECT to_regclass('po_messages') IS NOT NULL,"
							  "       to_regclass('po_parts') IS NOT NULL", podb::Params(), &r),
				  "creating tables");
	if (ok && r.Get (0, 0) != "t") ok = Check (Conn->Exec (SCHEMA_MESSAGES), "creating po_messages");
	if (ok && r.Get (0, 1) != "t") ok = Check (Conn->Exec (SCHEMA_PARTS), "creating po_parts");
	if (ok) ok = Check (Conn->Commit(), "creating tables");
	else Conn->Rollback();
	return ok;
}

//===============================================================================
// Message storage (mirrors GDBMStorage: headers first, the parts once the
// body has been downloaded).
//===============================================================================

void PGStorage::Store (MailMessage *M)
{
	if (!Ready) return;

	Lock->LockOnce();
	std::lock_guard<std::recursive_mutex> g (Conn->Mutex);

	StorageNode *N = Find_ (M->MsgID);
	std::string msgid = podb::Utf8 (M->MsgID);
	size_t total_size = 0;
	int partcount = 0;
	bool stored = false;

	// Store the headers the first time we see this message.
	if (!N && M->HeaderNames->Count)
	{
		std::string hdr;
		for (int i = 0; i < M->HeaderNames->Count; ++i)
		{
			hdr += M->HeaderNames->Get(i);
			hdr += '\n';
			hdr += M->HeaderValues->Get(i);
			hdr += '\n';
		}
		total_size = hdr.size();
		stored = Check (Conn->Exec (
			"INSERT INTO po_messages (mailbox, msgid, headers, stored_size, stored_parts)"
			" VALUES ($1, $2, $3, $4, 0)"
			" ON CONFLICT (owner, mailbox, msgid) DO UPDATE"
			"   SET headers = EXCLUDED.headers, stored_size = EXCLUDED.stored_size",
			{ Mailbox, msgid, podb::Param::Bytes (hdr.data(), hdr.size()), (long) total_size }),
			"storing headers");
	}

	// Store the parts once the body is in memory.
	if ((N && (N->Count < M->Count)) && (M->Flags & (MMFL_HAVEBODY | MMFL_INRAM)))
	{
		bool ok = Check (Conn->Begin(), "storing message")
			&& Check (Conn->Exec ("DELETE FROM po_parts WHERE owner = current_user"
								  " AND mailbox = $1 AND msgid = $2", { Mailbox, msgid }),
					  "storing message");
		for (int i = 0; ok && i < M->Count; ++i)
		{
			MailPart *C = M->GetPart (i);
			if (!C) continue;
			++partcount;
			total_size += C->ContentSize;
			ok = Check (Conn->Exec (
				"INSERT INTO po_parts (owner, mailbox, msgid, partno, contenttype,"
				"                      filename, description, contents)"
				" VALUES (current_user, $1, $2, $3, $4, $5, $6, $7)",
				{ Mailbox, msgid, i, podb::Utf8 (C->ContentType), podb::Utf8 (C->FileName),
				  podb::Utf8 (C->Description),
				  podb::Param::Bytes (C->Contents, C->ContentSize) }),
				"storing message part");
		}
		if (ok)
		{
			total_size += N->Size;
			ok = Check (Conn->Exec ("UPDATE po_messages SET stored_parts = $3, stored_size = $4"
									" WHERE owner = current_user AND mailbox = $1 AND msgid = $2",
									{ Mailbox, msgid, partcount, (long) total_size }),
						"storing message")
				&& Check (Conn->Commit(), "storing message");
		}
		if (!ok) Conn->Rollback();
		stored = ok;
	}
	else dprintf ("pgstorage: nothing to store for %s\n", M->MsgID);

	if (stored) Add (M->MsgID, total_size, partcount);
	Lock->UnlockOnce();
}

bool PGStorage::Retrieve (MailMessage *M)
{
	if (!Ready) return false;

	bool result = true;
	Lock->LockOnce();
	std::lock_guard<std::recursive_mutex> g (Conn->Mutex);

	StorageNode *Nod = Find_ (M->MsgID);
	if (Nod)
	{
		std::string msgid = podb::Utf8 (M->MsgID);
		podb::Result r;

		// Headers
		if (!M->HeaderNames->Count)
		{
			result = Check (Conn->Exec ("SELECT headers FROM po_messages WHERE owner = current_user"
										" AND mailbox = $1 AND msgid = $2", { Mailbox, msgid }, &r),
							"loading headers");
			if (result && r.Rows() && !r.IsNull (0, 0))
			{
				std::vector<std::string> lines = split_lines (r.Get (0, 0));
				for (size_t i = 0; i + 1 < lines.size(); i += 2)
				{
					M->HeaderNames->Add (lines[i].c_str());
					M->HeaderValues->Add (lines[i+1].c_str());
				}
			}
			else result = false;
		}

		// Parts, if the message has a body that is not in memory
		if ((!(M->Flags & MMFL_INRAM)) && (M->Flags & MMFL_HAVEBODY))
		{
			bool ok = Check (Conn->Exec ("SELECT partno, contenttype, filename, description, contents"
										 " FROM po_parts WHERE owner = current_user"
										 " AND mailbox = $1 AND msgid = $2 ORDER BY partno",
										 { Mailbox, msgid }, &r),
							 "loading message");
			if (!ok || r.Rows() == 0) result = false;
			for (int row = 0; ok && row < r.Rows(); ++row)
			{
				int i = (int) r.GetLong (row, 0);
				const std::string &contents = r.Get (row, 4);
				MailPart *P = M->GetPart (i);
				if (!P)
				{
					M->AddPart (r.Get (row, 1).c_str(), r.Get (row, 3).c_str(),
								r.Get (row, 2).c_str(), "");
					M->Count--;
					P = M->GetPart (i);
				}
				if (P) P->Set (r.Get (row, 1).c_str(), r.Get (row, 2).c_str(), r.Get (row, 3).c_str(),
							   (unsigned long) contents.size(), contents.c_str());
			}
		}
	}
	Lock->UnlockOnce();
	return result;
}

void PGStorage::Delete (const char *key)
{
	Lock->LockOnce();
	StorageNode *N = Find_ (key); // Find() would take the lock again
	if (N) Delete (N);
	else ForgetMessage (key);
	Lock->UnlockOnce();
}

void PGStorage::Delete (StorageNode *N)
{
	if (!N) return;
	ForgetMessage (N->BKey);
	Remove (N);
}

// Removes a message's row (and, by cascade, its parts).
void PGStorage::ForgetMessage (const char *msgid)
{
	if (!Ready || !msgid) return;
	Check (Conn->Exec ("DELETE FROM po_messages WHERE owner = current_user"
					   " AND mailbox = $1 AND msgid = $2", { Mailbox, podb::Utf8 (msgid) }),
		   "deleting message");
}

// Builds the in-memory list of stored messages (the storage index).
void PGStorage::LoadIndex (void)
{
	if (!Ready) return;
	podb::Result r;
	if (!Check (Conn->Exec ("SELECT msgid, stored_size, stored_parts FROM po_messages"
							" WHERE owner = current_user AND mailbox = $1 AND headers IS NOT NULL",
							{ Mailbox }, &r),
				"loading storage index"))
		return;
	Lock->LockOnce();
	for (int i = 0; i < r.Rows(); ++i)
		Add (r.Get (i, 0).c_str(), (size_t) r.GetLong (i, 1), (int) r.GetLong (i, 2));
	Lock->UnlockOnce();
}

// The storage index lives in the database already.
void PGStorage::StoreIndex (bool /*force*/)
{
}

//===============================================================================
// Mailbox index
//===============================================================================

bool PGStorage::LoadMailboxIndex (std::vector<IndexEntry> &entries)
{
	if (!Ready) return false;
	podb::Result r;
	if (!Check (Conn->Exec ("SELECT msgid, parentid, mailfrom, namefrom, subject, msgdate,"
							"       array_to_string(folders, chr(10)), flags, readstate, partcount,"
							"       coalesce(extract(epoch from received)::bigint, 0)"
							" FROM po_messages WHERE owner = current_user AND mailbox = $1"
							" ORDER BY received, msgid", { Mailbox }, &r),
				"loading mailbox"))
		return false;

	entries.clear();
	entries.reserve (r.Rows());
	for (int i = 0; i < r.Rows(); ++i)
	{
		IndexEntry e;
		e.MsgID		= r.Get (i, 0);
		e.ParentID	= r.Get (i, 1);
		e.MailFrom	= r.Get (i, 2);
		e.NameFrom	= r.Get (i, 3);
		e.Subject	= r.Get (i, 4);
		e.Date		= r.Get (i, 5);
		e.Folders	= split_lines (r.Get (i, 6));
		e.Flags		= (int) r.GetLong (i, 7);
		e.Read		= (int) r.GetLong (i, 8);
		e.Count		= (int) r.GetLong (i, 9);
		e.Received	= r.GetLong (i, 10);
		entries.push_back (std::move (e));
	}
	return true;
}

bool PGStorage::SaveMailboxIndex (const std::vector<IndexEntry> &entries)
{
	if (!Ready) return false;
	std::lock_guard<std::recursive_mutex> g (Conn->Mutex);

	// Only rows whose index columns changed are written.
	static const char *UPSERT =
		"INSERT INTO po_messages (mailbox, msgid, parentid, mailfrom, namefrom, subject, msgdate,"
		"                         folders, flags, readstate, partcount, received)"
		" VALUES ($1, $2, $3, $4, $5, $6, $7, string_to_array($8, chr(10)), $9, $10, $11,"
		"         to_timestamp($12))"
		" ON CONFLICT (owner, mailbox, msgid) DO UPDATE SET"
		"   parentid = EXCLUDED.parentid, mailfrom = EXCLUDED.mailfrom,"
		"   namefrom = EXCLUDED.namefrom, subject = EXCLUDED.subject,"
		"   msgdate = EXCLUDED.msgdate, folders = EXCLUDED.folders, flags = EXCLUDED.flags,"
		"   readstate = EXCLUDED.readstate, partcount = EXCLUDED.partcount,"
		"   received = EXCLUDED.received"
		" WHERE (po_messages.parentid, po_messages.mailfrom, po_messages.namefrom,"
		"        po_messages.subject, po_messages.msgdate, po_messages.folders,"
		"        po_messages.flags, po_messages.readstate, po_messages.partcount,"
		"        po_messages.received)"
		"   IS DISTINCT FROM"
		"       (EXCLUDED.parentid, EXCLUDED.mailfrom, EXCLUDED.namefrom, EXCLUDED.subject,"
		"        EXCLUDED.msgdate, EXCLUDED.folders, EXCLUDED.flags, EXCLUDED.readstate,"
		"        EXCLUDED.partcount, EXCLUDED.received)";

	bool ok = Check (Conn->Begin(), "saving mailbox");
	for (size_t i = 0; ok && i < entries.size(); ++i)
	{
		const IndexEntry &e = entries[i];
		std::string folders;
		for (size_t f = 0; f < e.Folders.size(); ++f)
		{
			if (f) folders += '\n';
			folders += podb::Utf8 (e.Folders[f].c_str());
		}
		ok = Check (Conn->Exec (UPSERT, { Mailbox, podb::Utf8 (e.MsgID.c_str()),
										  podb::Utf8 (e.ParentID.c_str()),
										  podb::Utf8 (e.MailFrom.c_str()),
										  podb::Utf8 (e.NameFrom.c_str()),
										  podb::Utf8 (e.Subject.c_str()),
										  podb::Utf8 (e.Date.c_str()), folders,
										  e.Flags, e.Read, e.Count, e.Received }),
					"saving mailbox");
	}
	if (ok) ok = Check (Conn->Commit(), "saving mailbox");
	else Conn->Rollback();
	return ok;
}
