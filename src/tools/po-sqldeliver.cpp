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

#include <ctype.h>
#include <errno.h>
#include <iconv.h>
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

//-------------------------------------------------------------------------------
// RFC 2047 encoded words: =?charset?B|Q?text?=
//-------------------------------------------------------------------------------

static std::string to_utf8 (const std::string &charset, const std::string &in)
{
	if (strcasecmp (charset.c_str(), "utf-8") == 0 || strcasecmp (charset.c_str(), "us-ascii") == 0)
		return podb::Utf8 (in.c_str());
	iconv_t cd = iconv_open ("UTF-8", charset.c_str());
	if (cd == (iconv_t) -1) return podb::Utf8 (in.c_str());
	std::string out;
	std::vector<char> inbuf (in.begin(), in.end());
	char *ip = inbuf.data();
	size_t il = inbuf.size();
	char buf[1024];
	while (il > 0)
	{
		char *op = buf;
		size_t ol = sizeof (buf);
		size_t r = iconv (cd, &ip, &il, &op, &ol);
		out.append (buf, op - buf);
		if (r == (size_t) -1 && errno != E2BIG)
		{
			out += '?';			// skip a byte the charset cannot convert
			++ip; --il;
		}
	}
	iconv_close (cd);
	return out;
}

static std::string decode_b64 (const std::string &s)
{
	std::string out;
	int val = 0, bits = 0;
	for (unsigned char c : s)
	{
		int d;
		if (c >= 'A' && c <= 'Z') d = c - 'A';
		else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
		else if (c >= '0' && c <= '9') d = c - '0' + 52;
		else if (c == '+') d = 62;
		else if (c == '/') d = 63;
		else continue;
		val = (val << 6) | d;
		bits += 6;
		if (bits >= 8) { bits -= 8; out += (char) ((val >> bits) & 0xff); }
	}
	return out;
}

static std::string decode_q (const std::string &s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '_') out += ' ';
		else if (s[i] == '=' && i + 2 < s.size() && isxdigit ((unsigned char) s[i+1]) &&
				 isxdigit ((unsigned char) s[i+2]))
		{
			out += (char) strtol (s.substr (i + 1, 2).c_str(), NULL, 16);
			i += 2;
		}
		else out += s[i];
	}
	return out;
}

// Decodes the encoded words in a header value and returns UTF-8. Text
// outside encoded words that is not UTF-8 is taken as ISO-8859-1.
static std::string decode_header (const std::string &in)
{
	std::string out;
	size_t pos = 0;
	bool last_was_word = false;
	while (pos < in.size())
	{
		size_t start = in.find ("=?", pos);
		if (start == std::string::npos) { out += podb::Utf8 (in.substr (pos).c_str()); break; }
		size_t q1 = in.find ('?', start + 2);
		size_t q2 = q1 == std::string::npos ? q1 : in.find ('?', q1 + 1);
		size_t end = q2 == std::string::npos ? q2 : in.find ("?=", q2 + 1);
		if (end == std::string::npos || q2 != q1 + 2)
		{
			out += podb::Utf8 (in.substr (pos, start + 2 - pos).c_str());
			pos = start + 2;
			last_was_word = false;
			continue;
		}
		std::string between = in.substr (pos, start - pos);
		// Whitespace between two encoded words is not displayed.
		if (!(last_was_word && between.find_first_not_of (" \t") == std::string::npos))
			out += podb::Utf8 (between.c_str());
		std::string charset = in.substr (start + 2, q1 - start - 2);
		size_t star = charset.find ('*');				// RFC 2231 language suffix
		if (star != std::string::npos) charset.erase (star);
		char enc = toupper ((unsigned char) in[q1 + 1]);
		std::string text = in.substr (q2 + 1, end - q2 - 1);
		std::string bytes = enc == 'B' ? decode_b64 (text) : decode_q (text);
		out += to_utf8 (charset, bytes);
		pos = end + 2;
		last_was_word = true;
	}
	return out;
}

//-------------------------------------------------------------------------------
// Header parsing
//-------------------------------------------------------------------------------

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
}

// Converts an RFC 5322 date ("Mon, 10 Nov 2025 10:00:00 +0100", with
// obsolete zone names and comments tolerated) to ISO 8601 for PostgreSQL.
// Returns "" if the date cannot be read; the column is then left NULL.
static std::string parse_date (const std::string &in)
{
	static const char *months[] = { "jan","feb","mar","apr","may","jun",
									"jul","aug","sep","oct","nov","dec" };
	std::string s = in;
	size_t paren = s.find ('(');
	if (paren != std::string::npos) s.erase (paren);			// trailing comment
	size_t comma = s.find (',');
	if (comma != std::string::npos) s.erase (0, comma + 1);		// day name

	char mon[16] = "", zone[16] = "";
	int day, year, hh, mm, ss = 0;
	int n = sscanf (s.c_str(), " %d %15s %d %d:%d:%d %15s", &day, mon, &year, &hh, &mm, &ss, zone);
	if (n < 7)
	{
		ss = 0;
		n = sscanf (s.c_str(), " %d %15s %d %d:%d %15s", &day, mon, &year, &hh, &mm, zone);
		if (n < 5) return "";
	}
	int m = -1;
	for (int i = 0; i < 12; ++i)
		if (strncasecmp (mon, months[i], 3) == 0) m = i;
	if (m < 0 || day < 1 || day > 31 || hh > 23 || mm > 59 || ss > 60) return "";
	if (year < 50) year += 2000;
	else if (year < 1000) year += 1900;

	int offset = 0;		// minutes east of UTC
	if ((zone[0] == '+' || zone[0] == '-') && strlen (zone) >= 5)
	{
		int v = atoi (zone + 1);
		offset = (v / 100) * 60 + v % 100;
		if (zone[0] == '-') offset = -offset;
	}
	else
	{
		static const struct { const char *name; int hours; } zones[] =
			{ {"EDT",-4},{"EST",-5},{"CDT",-5},{"CST",-6},{"MDT",-6},{"MST",-7},
			  {"PDT",-7},{"PST",-8},{"GMT",0},{"UT",0},{"UTC",0},{"Z",0} };
		for (const auto &z : zones)
			if (strcasecmp (zone, z.name) == 0) offset = z.hours * 60;
	}
	if (ss == 60) ss = 59;	// leap second

	char buf[64];
	snprintf (buf, sizeof (buf), "%04d-%02d-%02d %02d:%02d:%02d%c%02d:%02d",
			  year, m + 1, day, hh, mm, ss, offset < 0 ? '-' : '+',
			  abs (offset) / 60, abs (offset) % 60);
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

	std::string from_name, from_addr;
	split_address (decode_header (header ("From")), from_name, from_addr);
	std::string sent = parse_date (header ("Date"));
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
		decode_header (header ("To")), decode_header (header ("Subject")),
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
