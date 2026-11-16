//===============================================================================
// Neo-PostOffice: helpers for reading e-mail header fields
// (see include/mailheaders.h). GNU LGPL like PostOffice itself.
//===============================================================================

#include "mailheaders.h"
#include "podb.h"

#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace mailheaders
{

static std::string trim (const std::string &s)
{
	size_t a = s.find_first_not_of (" \t\r");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of (" \t\r");
	return s.substr (a, b - a + 1);
}

std::vector<Header> Parse (const std::string &raw)
{
	std::vector<Header> headers;
	size_t pos = 0;
	while (pos < raw.size())
	{
		size_t nl = raw.find ('\n', pos);
		std::string line = raw.substr (pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? raw.size() : nl + 1;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.empty()) break;
		if ((line[0] == ' ' || line[0] == '\t') && !headers.empty())
			headers.back().Value += " " + trim (line);
		else
		{
			size_t colon = line.find (':');
			if (colon == std::string::npos || colon == 0) continue;
			headers.push_back ({ line.substr (0, colon), trim (line.substr (colon + 1)) });
		}
	}
	return headers;
}

std::string Get (const std::vector<Header> &headers, const char *name)
{
	for (const Header &h : headers)
		if (strcasecmp (h.Name.c_str(), name) == 0) return h.Value;
	return "";
}

//-------------------------------------------------------------------------------
// RFC 2047 encoded words
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
		val = ((val << 6) | d) & 0xffffff;
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

std::string Decode (const std::string &in)
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
		out += to_utf8 (charset, enc == 'B' ? decode_b64 (text) : decode_q (text));
		pos = end + 2;
		last_was_word = true;
	}
	return out;
}

//-------------------------------------------------------------------------------
// Addresses and dates
//-------------------------------------------------------------------------------

void SplitAddress (const std::string &from, std::string &name, std::string &addr)
{
	name.clear(); addr.clear();
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

time_t ParseDate (const std::string &in)
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
	if (n < 6)
	{
		ss = 0;
		n = sscanf (s.c_str(), " %d %15s %d %d:%d %15s", &day, mon, &year, &hh, &mm, zone);
		if (n < 5) return 0;
	}
	int m = -1;
	for (int i = 0; i < 12; ++i)
		if (strncasecmp (mon, months[i], 3) == 0) m = i;
	if (m < 0 || day < 1 || day > 31 || hh > 23 || mm > 59 || ss > 60) return 0;
	if (year < 50) year += 2000;
	else if (year < 1000) year += 1900;
	if (ss == 60) ss = 59;	// leap second

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

	struct tm t = {};
	t.tm_year = year - 1900;
	t.tm_mon = m;
	t.tm_mday = day;
	t.tm_hour = hh;
	t.tm_min = mm;
	t.tm_sec = ss;
	return timegm (&t) - (time_t) offset * 60;
}

}
