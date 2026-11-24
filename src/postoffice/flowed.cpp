// Line wrapping and format=flowed (RFC 3676) for the text you write
// (Neo-PostOffice).

#include "flowed.h"
#include "var.h"

#include <stdlib.h>
#include <vector>

namespace flowed
{

int WrapWidth (Var *prefs)
{
	int w = 0;
	if (prefs)
	{
		const char *v = prefs->Get_char_tree ((char *) "postoffice/gui/wrap");
		if (v && *v) w = atoi (v);
	}
	if (w <= 0) return 72;
	if (w < 60) return 60;
	if (w > 100) return 100;
	return w;
}

size_t Length (const std::string &text)
{
	size_t n = 0;
	for (unsigned char c : text) if ((c & 0xC0) != 0x80) ++n;
	return n;
}

static std::vector<std::string> split_lines (const std::string &text, char sep)
{
	std::vector<std::string> out;
	std::string cur;
	for (char c : text)
	{
		if (c == sep) { out.push_back (cur); cur.clear (); }
		else cur += c;
	}
	out.push_back (cur);
	return out;
}

static void strip_trailing (std::string &s)
{
	while (!s.empty () && (s.back () == ' ' || s.back () == '\t')) s.pop_back ();
}

static std::string stuff (const std::string &line)
{
	if (!line.empty () && (line[0] == ' ' || line.compare (0, 5, "From ") == 0))
		return " " + line;
	return line;
}

// Wraps one paragraph at width characters. Words are split at single
// spaces (runs of spaces are kept); a word longer than the width gets a
// line of its own. Returns the lines without their breaking spaces.
static std::vector<std::string> wrap (const std::string &para, size_t width)
{
	std::vector<std::string> lines;
	std::vector<std::string> words = split_lines (para, ' ');
	std::string line;
	size_t len = 0;
	bool have = false;
	for (const std::string &w : words)
	{
		size_t wl = Length (w);
		if (!have) { line = w; len = wl; have = true; continue; }
		// (never break after leading spaces only: the word joins them)
		if (len + 1 + wl <= width || line.find_first_not_of (' ') == std::string::npos)
		{
			line += ' '; line += w; len += 1 + wl; continue;
		}
		lines.push_back (line);
		line = w; len = wl;
	}
	lines.push_back (line);
	return lines;
}

std::string Flow (const std::string &text, int width)
{
	std::string out;
	if (width < 20) width = 20;
	std::vector<std::string> hard = split_lines (text, '\n');
	// The editor's text ends with a line break: no empty paragraph for it.
	if (hard.size () > 1 && hard.back ().empty ()) hard.pop_back ();
	for (std::string para : hard)
	{
		// Soft line breaks stand for the space the editor took out.
		for (char &c : para) if (c == '\r') c = ' ';
		strip_trailing (para);

		if (!para.empty () && para[0] == '>')
		{
			// Quoted: keep the quote marks ("> > ") on every line.
			size_t p = 0;
			while (p < para.size () && (para[p] == '>' || para[p] == ' ')) ++p;
			std::string prefix = para.substr (0, p), rest = para.substr (p);
			strip_trailing (prefix);
			prefix += ' ';
			size_t w = (size_t) width > Length (prefix) + 20 ? width - Length (prefix) : 20;
			if (rest.empty ()) { std::string q = prefix; strip_trailing (q); out += q + "\n"; continue; }
			for (std::string &l : wrap (rest, w))
			{
				strip_trailing (l);
				out += prefix + l + "\n";
			}
			continue;
		}

		std::vector<std::string> lines = wrap (para, width);
		for (size_t i = 0; i < lines.size (); ++i)
		{
			std::string l = lines[i];
			bool last = i + 1 == lines.size ();
			if (last) strip_trailing (l);
			out += stuff (l) + (last ? "" : " ") + "\n";
		}
	}
	return out;
}

std::string Fixed (const std::string &text)
{
	std::string out;
	std::vector<std::string> lines = split_lines (text, '\n');
	if (lines.size () > 1 && lines.back ().empty ()) lines.pop_back ();
	for (std::string l : lines)
	{
		if (!l.empty () && l.back () == '\r') l.pop_back ();
		if (l != "-- ") strip_trailing (l);
		out += (l == "-- " ? l : stuff (l)) + "\n";
	}
	return out;
}

std::string Unflow (const std::string &text)
{
	std::string out, para;
	bool open = false;
	std::vector<std::string> lines = split_lines (text, '\n');
	if (lines.size () > 1 && lines.back ().empty ()) lines.pop_back ();
	for (std::string l : lines)
	{
		if (!l.empty () && l.back () == '\r') l.pop_back ();
		bool quoted = !l.empty () && l[0] == '>';
		if (!quoted && !l.empty () && l[0] == ' ') l.erase (0, 1);	// stuffing
		bool flows = !quoted && l != "-- " && !l.empty () && l.back () == ' ';
		para += l;
		open = flows;
		if (!flows) { out += para + "\n"; para.clear (); }
	}
	if (open) out += para + "\n";
	return out;
}

}
