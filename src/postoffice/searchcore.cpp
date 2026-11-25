// Searching messages (Neo-PostOffice): matching a message against a search
// text, without regard to case. Used by MailBox::Search for mailboxes the
// server cannot search (all but IMAP).

#include "search.h"
#include "mailbox.h"
#include "mailheaders.h"

#include <string.h>
#include <strings.h>
#include <vector>

namespace search
{

// One UTF-8 character at s[i] (advancing i) as a code point; a broken
// byte is taken as itself.
static unsigned long next_cp (const std::string &s, size_t &i)
{
	unsigned char c = s[i++];
	if (c < 0x80) return c;
	int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
	if (!extra || i + extra > s.size ()) return c;
	unsigned long cp = c & (0x3F >> extra);
	for (int k = 0; k < extra; ++k)
	{
		unsigned char d = s[i];
		if ((d & 0xC0) != 0x80) return c;
		cp = (cp << 6) | (d & 0x3F);
		++i;
	}
	return cp;
}

static void put_cp (std::string &out, unsigned long cp)
{
	if (cp < 0x80) out += (char) cp;
	else if (cp < 0x800) { out += (char) (0xC0 | (cp >> 6)); out += (char) (0x80 | (cp & 0x3F)); }
	else if (cp < 0x10000)
	{
		out += (char) (0xE0 | (cp >> 12));
		out += (char) (0x80 | ((cp >> 6) & 0x3F));
		out += (char) (0x80 | (cp & 0x3F));
	}
	else
	{
		out += (char) (0xF0 | (cp >> 18));
		out += (char) (0x80 | ((cp >> 12) & 0x3F));
		out += (char) (0x80 | ((cp >> 6) & 0x3F));
		out += (char) (0x80 | (cp & 0x3F));
	}
}

static unsigned long lower_cp (unsigned long c)
{
	if (c >= 'A' && c <= 'Z') return c + 32;
	if (c < 0x80) return c;
	if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;			// Latin-1
	if (c >= 0x100 && c <= 0x137 && !(c & 1)) return c + 1;			// Latin Extended-A
	if (c >= 0x139 && c <= 0x148 && (c & 1)) return c + 1;
	if (c >= 0x14A && c <= 0x177 && !(c & 1)) return c + 1;
	if (c == 0x178) return 0xFF;
	if (c >= 0x179 && c <= 0x17E && (c & 1)) return c + 1;
	if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;		// Greek
	if (c >= 0x410 && c <= 0x42F) return c + 32;						// Cyrillic
	if (c >= 0x400 && c <= 0x40F) return c + 80;
	return c;
}

std::string Fold (const std::string &s)
{
	std::string out;
	out.reserve (s.size ());
	size_t i = 0;
	while (i < s.size ())
	{
		unsigned long cp = next_cp (s, i);
		if (cp == '\r' || cp == '\n' || cp == '\t') cp = ' ';
		put_cp (out, lower_cp (cp));
	}
	return out;
}

static bool has (const std::string &hay, const std::string &needle)
{
	return Fold (hay).find (needle) != std::string::npos;
}

// The text of an HTML part, roughly: tags left out, entities kept.
static std::string strip_tags (const std::string &html)
{
	std::string out;
	bool tag = false;
	for (char c : html)
	{
		if (c == '<') { tag = true; out += ' '; continue; }
		if (c == '>') { tag = false; continue; }
		if (!tag) out += c;
	}
	return out;
}

static std::string part_text (const std::string &type, const std::string &body, const std::string &cte)
{
	std::string t = type;
	for (char &c : t) c = (char) tolower ((unsigned char) c);
	if (t.compare (0, 5, "text/") != 0 && !t.empty ()) return "";
	std::string text = mailheaders::ToUTF8 (mailheaders::DecodeBody (body, cte), mailheaders::Param (type, "charset"));
	if (t.compare (0, 9, "text/html") == 0) text = strip_tags (text);
	return text;
}

// Walks a MIME entity (headers and body) for its text parts.
static void walk (const std::string &entity, std::string &out, int depth)
{
	if (depth > 20) return;
	size_t sep = entity.find ("\n\n"), crsep = entity.find ("\r\n\r\n");
	size_t hdrend, bodypos;
	if (crsep != std::string::npos && (sep == std::string::npos || crsep < sep)) { hdrend = crsep; bodypos = crsep + 4; }
	else if (sep != std::string::npos) { hdrend = sep; bodypos = sep + 2; }
	else { hdrend = entity.size (); bodypos = entity.size (); }
	std::vector<mailheaders::Header> h = mailheaders::Parse (entity.substr (0, hdrend + 1));
	std::string type = mailheaders::Get (h, "Content-Type");
	std::string body = entity.substr (bodypos);
	std::string lt = type;
	for (char &c : lt) c = (char) tolower ((unsigned char) c);
	if (lt.compare (0, 10, "multipart/") == 0)
	{
		std::string b = mailheaders::Param (type, "boundary");
		if (b.empty ()) return;
		std::string delim = "--" + b;
		size_t pos = body.find (delim);
		while (pos != std::string::npos)
		{
			pos += delim.size ();
			if (body.compare (pos, 2, "--") == 0) break;
			size_t start = body.find ('\n', pos);
			if (start == std::string::npos) break;
			++start;
			size_t next = body.find ("\n" + delim, start);
			std::string sub = body.substr (start, next == std::string::npos ? std::string::npos : next - start);
			walk (sub, out, depth + 1);
			pos = next == std::string::npos ? next : next + 1;
		}
		return;
	}
	if (lt.compare (0, 14, "message/rfc822") == 0) { walk (body, out, depth + 1); return; }
	std::string t = part_text (type, body, mailheaders::Get (h, "Content-Transfer-Encoding"));
	if (!t.empty ()) { out += t; out += '\n'; }
}

std::string TextOfRaw (const std::string &raw)
{
	std::string out;
	walk (raw, out, 0);
	return out;
}

// The text of a message whose parts are in memory.
static std::string text_of_parts (MailMessage *M)
{
	std::string out;
	for (int i = 0; i < M->Count; ++i)
	{
		MailPart *P = M->GetPart (i);
		if (!P || !P->Contents) continue;
		std::string type = P->ContentType ? P->ContentType : "text/plain";
		std::string t = part_text (type, std::string (P->Contents, P->ContentSize), mailheaders::Param (type, "x-neo-cte"));
		if (!t.empty ()) { out += t; out += '\n'; }
	}
	return out;
}

static std::string header (MailMessage *M, const char *name, bool addresses)
{
	const char *v = M->Header ((char *) name);
	if (!v || !*v) return "";
	return addresses ? mailheaders::DecodeAddresses (v) : mailheaders::Decode (v);
}

bool Matches (MailMessage *M, SearchField field, const std::string &needle, const std::string *raw)
{
	switch (field)
	{
		case SEARCH_FROM:
			return has (header (M, "From", true), needle);
		case SEARCH_TOCC:
			return has (header (M, "To", true), needle) || has (header (M, "Cc", true), needle);
		case SEARCH_SUBJECT:
			return has (header (M, "Subject", false), needle);
		case SEARCH_BODY:
		case SEARCH_ANYWHERE:
			if (field == SEARCH_ANYWHERE)
				for (int i = 0; i < M->HeaderNames->Count; ++i)
				{
					const char *n = M->HeaderNames->Get (i);
					bool a = !strcasecmp (n, "From") || !strcasecmp (n, "To") || !strcasecmp (n, "Cc") ||
							 !strcasecmp (n, "Reply-To");
					const char *v = M->HeaderValues->Get (i);
					if (v && has (a ? mailheaders::DecodeAddresses (v) : mailheaders::Decode (v), needle)) return true;
				}
			return has (raw ? TextOfRaw (*raw) : text_of_parts (M), needle);
	}
	return false;
}

}
