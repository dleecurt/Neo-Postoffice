//===============================================================================
// po-sqldeliver: deliver one e-mail message into a PostOffice SQL mailbox
// -------------------------------------------------------------------------
// Reads a message on standard input and stores it in the po_mail table that
// Neo-PostOffice's SQL mailbox reads (see docs/sql-mailbox-postgresql.sql).
// Use it wherever mail can be piped to a program: getmail's MDA_external,
// fetchmail's mda, procmail, a Postfix or sendmail alias, ~/.forward.
//
//   po-sqldeliver [-u OWNER] [-f FOLDER] [-t TABLE] [CONNINFO]
//
// CONNINFO is a libpq connection string, e.g.
//   "host=db.example.org dbname=postoffice user=getmail sslmode=verify-full"
// Missing values come from the PG* environment variables, the password
// from ~/.pgpass.
//
// Exit status follows sendmail conventions so the caller keeps the message
// and retries later when the database is unavailable: 0 delivered,
// 64 usage error, 65 unusable message, 67 unknown owner, 75 temporary failure.
//
// Part of Neo-PostOffice; GNU LGPL like PostOffice itself.
//===============================================================================

#include "podb.h"
#include "mailheaders.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

// The database layer's log functions (the client has its own).
void dprintf (const char *, ...) {}
void iprintf (const char *, ...) {}
void eprintf (const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stderr, fmt, ap);
	va_end (ap);
}

enum { EX_OK_ = 0, EX_USAGE_ = 64, EX_DATAERR_ = 65, EX_NOUSER_ = 67, EX_TEMPFAIL_ = 75 };

static void usage (void)
{
	fprintf (stderr,
		"usage: po-sqldeliver [-u OWNER] [-f FOLDER] [-t TABLE] [CONNINFO]\n"
		"\n"
		"Reads one message on standard input and stores it in Neo-PostOffice's\n"
		"SQL mailbox (PostgreSQL).\n"
		"\n"
		"  -u OWNER   the PostgreSQL user the mail is for (default: the user\n"
		"             connecting, i.e. deliver to yourself)\n"
		"  -f FOLDER  folder to put it in (default: Inbox)\n"
		"  -t TABLE   table name (default: po_mail)\n"
		"\n"
		"CONNINFO is a libpq connection string such as\n"
		"\"host=db.example.org dbname=postoffice user=getmail sslmode=verify-full\".\n"
		"Passwords come from ~/.pgpass.\n");
	exit (EX_USAGE_);
}

static bool valid_identifier (const char *t)
{
	if (!*t || strlen (t) > 63) return false;
	for (const char *p = t; *p; ++p)
		if (!isalnum ((unsigned char) *p) && *p != '_') return false;
	return true;
}

static std::string trim (const std::string &s)
{
	size_t a = s.find_first_not_of (" \t");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of (" \t");
	return s.substr (a, b - a + 1);
}

// The Date header as ISO 8601 for PostgreSQL, or "" if it cannot be read
// (the column is then left NULL).
static std::string iso_date (const std::string &value)
{
	time_t t = mailheaders::ParseDate (value);
	if (!t) return "";
	struct tm tmv;
	gmtime_r (&t, &tmv);
	char buf[64];
	strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M:%S+00:00", &tmv);
	return buf;
}

int main (int argc, char **argv)
{
	std::string table = "po_mail", folder = "Inbox", owner;
	int opt;

	while ((opt = getopt (argc, argv, "u:f:t:h")) != -1)
	{
		switch (opt)
		{
			case 'u': owner = optarg; break;
			case 'f': folder = optarg; break;
			case 't': table = optarg; break;
			default: usage();
		}
	}
	if (argc - optind > 1) usage();
	std::string conninfo = optind < argc ? argv[optind] : "";
	if (!valid_identifier (table.c_str()))
	{
		fprintf (stderr, "po-sqldeliver: table names may only contain letters, digits and _\n");
		return EX_USAGE_;
	}
	// Folder names become folders in the client's settings file, which
	// cannot store these characters, and the client keeps them in 64 bytes.
	folder = trim (folder);
	if (folder.empty() || folder.size() > 63 || folder.find_first_of ("={}\",") != std::string::npos ||
		std::any_of (folder.begin(), folder.end(), [](char c) { return (unsigned char) c < ' '; }))
	{
		fprintf (stderr, "po-sqldeliver: folder names must be 1-63 bytes without = { } \" ,\n");
		return EX_USAGE_;
	}

	// Read the message, with Unix line endings.
	std::string raw;
	{
		char buf[65536];
		size_t n;
		while ((n = fread (buf, 1, sizeof (buf), stdin)) > 0) raw.append (buf, n);
		std::string fixed;
		fixed.reserve (raw.size());
		for (size_t i = 0; i < raw.size(); ++i)
			if (!(raw[i] == '\r' && i + 1 < raw.size() && raw[i+1] == '\n')) fixed += raw[i];
		raw.swap (fixed);
	}
	// Drop an mbox "From " separator line if the caller added one.
	if (raw.compare (0, 5, "From ") == 0)
	{
		size_t nl = raw.find ('\n');
		raw.erase (0, nl == std::string::npos ? raw.size() : nl + 1);
	}

	std::vector<mailheaders::Header> headers = mailheaders::Parse (raw);
	if (headers.empty())
	{
		fprintf (stderr, "po-sqldeliver: input is not an e-mail message (no header)\n");
		return EX_DATAERR_;
	}
	auto header = [&](const char *name) { return mailheaders::Get (headers, name); };

	std::string from_name, from_addr;
	mailheaders::SplitAddress (mailheaders::Decode (header ("From")), from_name, from_addr);
	std::string sent = iso_date (header ("Date"));
	std::string message_id = trim (header ("Message-ID"));

	std::unique_ptr<podb::Connection> db;
	std::string error;
	db = podb::ConnectPostgreSQL (conninfo, error);
	if (!db)
	{
		fprintf (stderr, "po-sqldeliver: cannot connect: %s\n", error.c_str());
		return EX_TEMPFAIL_;
	}

	// Refuse mail for a user who could never read it (a typo in -u, say).
	if (!owner.empty())
	{
		podb::Result r;
		if (!db->Exec ("SELECT pg_has_role (oid, 'po_users', 'MEMBER') FROM pg_roles"
					   " WHERE rolname = $1", { owner }, &r))
		{
			fprintf (stderr, "po-sqldeliver: %s\n", db->Error().c_str());
			return EX_TEMPFAIL_;
		}
		if (r.Rows() == 0 || r.Get (0, 0) != "t")
		{
			fprintf (stderr, "po-sqldeliver: \"%s\" is not a mail user (not a member of po_users)\n",
					 owner.c_str());
			return EX_NOUSER_;
		}
	}

	// No RETURNING: the delivery role has no SELECT right on the table.
	std::string insert = "INSERT INTO \"" + table + "\" (owner, folder, message_id, from_name,"
		" from_addr, to_addr, subject, sent, size, raw) VALUES (" +
		std::string (owner.empty() ? "current_user" : "$10") +
		", $1, $2, $3, $4, $5, $6, $7::timestamptz, $8, $9)";
	podb::Params values = {
		podb::Utf8 (folder.c_str()),
		message_id.empty() ? podb::Param::Null() : podb::Param (podb::Utf8 (message_id.c_str())),
		from_name, from_addr,
		mailheaders::Decode (header ("To")), mailheaders::Decode (header ("Subject")),
		sent.empty() ? podb::Param::Null() : podb::Param (sent),
		(long) raw.size(),
		podb::Param::Bytes (raw.data(), raw.size())
	};
	if (!owner.empty()) values.push_back (owner);

	if (!db->Exec (insert, values))
	{
		fprintf (stderr, "po-sqldeliver: %s\n", db->Error().c_str());
		return EX_TEMPFAIL_;
	}
	return EX_OK_;
}
