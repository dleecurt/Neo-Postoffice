//===============================================================================
// po-sqldeliver: deliver one e-mail message into a PostOffice SQL mailbox
// -------------------------------------------------------------------------
// Reads a message on standard input and stores it in the "messages" table
// that Neo-PostOffice's SQL mailbox type reads (see docs/sql-mailbox-*.sql).
// Use it wherever a mail server can pipe mail to a program: a Postfix or
// sendmail alias, ~/.forward, procmail, fetchmail's --mda, and so on.
//
//   po-sqldeliver [-t TABLE] [-f FOLDER] [-H] pgsql CONNINFO
//   po-sqldeliver [-t TABLE] [-f FOLDER] [-H] mysql HOST USER DATABASE
//
// PostgreSQL passwords come from ~/.pgpass, MySQL passwords from the
// [client] section of ~/.my.cnf.
//
// Exit status follows sendmail conventions so a mail server retries later
// when the database is unavailable: 0 delivered, 64 usage error,
// 65 unusable message, 75 temporary failure.
//
// Part of Neo-PostOffice; GNU LGPL like PostOffice itself.
//===============================================================================

#include "podb.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

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

enum { EX_OK_ = 0, EX_USAGE_ = 64, EX_DATAERR_ = 65, EX_TEMPFAIL_ = 75 };

static void usage (void)
{
	fprintf (stderr,
		"usage: po-sqldeliver [-t TABLE] [-f FOLDER] [-H] pgsql CONNINFO\n"
		"       po-sqldeliver [-t TABLE] [-f FOLDER] [-H] mysql HOST USER DATABASE\n"
		"\n"
		"Reads one message on standard input and stores it for PostOffice's\n"
		"SQL mailbox type.\n"
		"\n"
		"  -t TABLE   table to deliver into (default: messages)\n"
		"  -f FOLDER  folder name, as configured in PostOffice (default: Inbox)\n"
		"  -H         also store each header line in the \"headers\" table\n"
		"\n"
		"PostgreSQL passwords come from ~/.pgpass, MySQL passwords from the\n"
		"[client] section of ~/.my.cnf.\n");
	exit (EX_USAGE_);
}

// The body encoding of the original PostOffice SQL mailbox (encode_mysql):
// letters, digits, space and .,:;/!? are kept, every other byte is written
// as '@' and two lowercase hex digits.
static std::string encode_body (const std::string &in)
{
	static const char hex[] = "0123456789abcdef";
	std::string out;
	out.reserve (in.size() + in.size() / 4);
	for (unsigned char c : in)
	{
		if (isalnum (c) || c == ' ' || c == '.' || c == ',' || c == ':' || c == ';' ||
		    c == '/' || c == '!' || c == '?')
		{
			if (c < 0x80) { out += (char) c; continue; }
		}
		out += '@';
		out += hex[c >> 4];
		out += hex[c & 15];
	}
	return out;
}

static bool valid_identifier (const char *t)
{
	if (!*t || strlen (t) > 64) return false;
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

struct Header { std::string name, value; };

// Splits "Name <addr>", "addr (Name)" or "addr" into name and address.
static void split_address (const std::string &from, std::string &name, std::string &addr)
{
	size_t lt = from.find ('<'), gt = from.find ('>', lt == std::string::npos ? 0 : lt);
	if (lt != std::string::npos && gt != std::string::npos)
	{
		addr = trim (from.substr (lt + 1, gt - lt - 1));
		name = trim (from.substr (0, lt));
	}
	else
	{
		size_t lp = from.find ('('), rp = from.rfind (')');
		if (lp != std::string::npos && rp != std::string::npos && rp > lp)
		{
			name = trim (from.substr (lp + 1, rp - lp - 1));
			addr = trim (from.substr (0, lp));
		}
		else addr = trim (from);
	}
	if (name.size() >= 2 && name.front() == '"' && name.back() == '"')
		name = name.substr (1, name.size() - 2);
	if (name.empty()) name = addr;
}

int main (int argc, char **argv)
{
	std::string table = "messages", folder = "Inbox";
	bool store_headers = false;
	int opt;

	while ((opt = getopt (argc, argv, "t:f:Hh")) != -1)
	{
		switch (opt)
		{
			case 't': table = optarg; break;
			case 'f': folder = optarg; break;
			case 'H': store_headers = true; break;
			default: usage();
		}
	}
	if (optind >= argc) usage();
	std::string driver = argv[optind];
	bool pg = driver == "pgsql" || driver == "postgresql";
	if (pg ? (argc - optind != 2) : (driver != "mysql" || argc - optind != 4)) usage();
	if (!valid_identifier (table.c_str()))
	{
		fprintf (stderr, "po-sqldeliver: table names may only contain letters, digits and _\n");
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
	// Drop an mbox "From " separator line if the mail server added one.
	if (raw.compare (0, 5, "From ") == 0)
	{
		size_t nl = raw.find ('\n');
		raw.erase (0, nl == std::string::npos ? raw.size() : nl + 1);
	}

	// Parse the header (unfolding continuation lines).
	std::vector<Header> headers;
	size_t pos = 0;
	while (pos < raw.size())
	{
		size_t nl = raw.find ('\n', pos);
		std::string line = raw.substr (pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? raw.size() : nl + 1;
		if (line.empty()) break;
		if ((line[0] == ' ' || line[0] == '\t') && !headers.empty())
			headers.back().value += " " + trim (line);
		else
		{
			size_t colon = line.find (':');
			if (colon == std::string::npos || colon == 0) continue;
			headers.push_back ({ line.substr (0, colon), trim (line.substr (colon + 1)) });
		}
	}
	if (headers.empty())
	{
		fprintf (stderr, "po-sqldeliver: input is not an e-mail message (no header)\n");
		return EX_DATAERR_;
	}

	auto header = [&](const char *name) -> std::string
	{
		for (const Header &h : headers)
			if (strcasecmp (h.name.c_str(), name) == 0) return h.value;
		return "";
	};

	std::string namefrom, mailfrom;
	split_address (header ("From"), namefrom, mailfrom);
	std::string received = header ("Date");
	if (received.empty())
	{
		char buf[64];
		time_t now = time (NULL);
		strftime (buf, sizeof (buf), "%a, %d %b %Y %H:%M:%S %z", localtime (&now));
		received = buf;
	}

	// Connect.
	std::string error;
	std::unique_ptr<podb::Connection> db;
	if (pg) db = podb::ConnectPostgreSQL (argv[optind + 1], error);
	else db = podb::ConnectMySQL (argv[optind + 1], 3306, argv[optind + 2], "", argv[optind + 3], error);
	if (!db)
	{
		fprintf (stderr, "po-sqldeliver: cannot connect: %s\n", error.c_str());
		return EX_TEMPFAIL_;
	}

	std::string qtable = pg ? "\"" + table + "\"" : "`" + table + "`";
	std::string insert = "INSERT INTO " + qtable +
		" (folder, namefrom, mailfrom, mailto, subject, received, msgread, body)"
		" VALUES ($1, $2, $3, $4, $5, $6, 0, $7)";
	if (pg) insert += " RETURNING msgid";

	podb::Params values = {
		podb::Utf8 (folder.c_str()), podb::Utf8 (namefrom.c_str()), podb::Utf8 (mailfrom.c_str()),
		podb::Utf8 (header ("To").c_str()), podb::Utf8 (header ("Subject").c_str()),
		podb::Utf8 (received.c_str()), encode_body (raw)
	};

	podb::Result r;
	bool ok = db->Begin() && db->Exec (insert, values, &r);
	if (ok && store_headers)
	{
		long id;
		if (pg) id = r.GetLong (0, 0);
		else
		{
			ok = db->Exec ("SELECT LAST_INSERT_ID()", podb::Params(), &r);
			id = ok ? r.GetLong (0, 0) : 0;
		}
		for (size_t i = 0; ok && i < headers.size(); ++i)
			ok = db->Exec ("INSERT INTO headers (msgid, name, value) VALUES ($1, $2, $3)",
						   { id, podb::Utf8 (headers[i].name.c_str()),
							 podb::Utf8 (headers[i].value.c_str()) });
	}
	if (ok) ok = db->Commit();
	if (!ok)
	{
		fprintf (stderr, "po-sqldeliver: %s\n", db->Error().c_str());
		db->Rollback();
		return EX_TEMPFAIL_;
	}
	return EX_OK_;
}
