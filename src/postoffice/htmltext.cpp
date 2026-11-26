//===============================================================================
// Neo-PostOffice: readable text from HTML mail (see htmltext.h)
//===============================================================================

#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include <string>
#include <vector>
#include <algorithm>

#include "htmltext.h"

namespace
{
	std::string lower (std::string s)
	{
		for (char &c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		return s;
	}

	void put_utf8 (std::string &o, unsigned long c)
	{
		if (c == 0 || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
		if (c < 0x80) o += (char) c;
		else if (c < 0x800) { o += (char) (0xC0 | (c >> 6)); o += (char) (0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { o += (char) (0xE0 | (c >> 12)); o += (char) (0x80 | ((c >> 6) & 0x3F)); o += (char) (0x80 | (c & 0x3F)); }
		else { o += (char) (0xF0 | (c >> 18)); o += (char) (0x80 | ((c >> 12) & 0x3F)); o += (char) (0x80 | ((c >> 6) & 0x3F)); o += (char) (0x80 | (c & 0x3F)); }
	}

	struct Entity { const char *name; unsigned long code; };
	const Entity ENTITIES[] =
	{
		{ "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
		{ "nbsp", 0xA0 }, { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "thinsp", 0x2009 },
		{ "shy", 0xAD }, { "zwnj", 0x200C }, { "zwj", 0x200D }, { "lrm", 0x200E }, { "rlm", 0x200F },
		{ "copy", 0xA9 }, { "reg", 0xAE }, { "trade", 0x2122 }, { "deg", 0xB0 }, { "plusmn", 0xB1 },
		{ "times", 0xD7 }, { "divide", 0xF7 }, { "micro", 0xB5 }, { "para", 0xB6 }, { "sect", 0xA7 },
		{ "middot", 0xB7 }, { "bull", 0x2022 }, { "hellip", 0x2026 }, { "prime", 0x2032 },
		{ "ndash", 0x2013 }, { "mdash", 0x2014 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
		{ "sbquo", 0x201A }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bdquo", 0x201E },
		{ "laquo", 0xAB }, { "raquo", 0xBB }, { "lsaquo", 0x2039 }, { "rsaquo", 0x203A },
		{ "dagger", 0x2020 }, { "Dagger", 0x2021 }, { "permil", 0x2030 },
		{ "euro", 0x20AC }, { "pound", 0xA3 }, { "yen", 0xA5 }, { "cent", 0xA2 }, { "curren", 0xA4 },
		{ "iexcl", 0xA1 }, { "iquest", 0xBF }, { "brvbar", 0xA6 }, { "uml", 0xA8 }, { "ordf", 0xAA },
		{ "not", 0xAC }, { "macr", 0xAF }, { "sup1", 0xB9 }, { "sup2", 0xB2 }, { "sup3", 0xB3 },
		{ "acute", 0xB4 }, { "cedil", 0xB8 }, { "ordm", 0xBA }, { "frac14", 0xBC }, { "frac12", 0xBD },
		{ "frac34", 0xBE }, { "larr", 0x2190 }, { "uarr", 0x2191 }, { "rarr", 0x2192 }, { "darr", 0x2193 },
		{ "harr", 0x2194 }, { "hearts", 0x2665 }, { "check", 0x2713 }, { "star", 0x2606 },
		{ "Agrave", 0xC0 }, { "Aacute", 0xC1 }, { "Acirc", 0xC2 }, { "Atilde", 0xC3 }, { "Auml", 0xC4 },
		{ "Aring", 0xC5 }, { "AElig", 0xC6 }, { "Ccedil", 0xC7 }, { "Egrave", 0xC8 }, { "Eacute", 0xC9 },
		{ "Ecirc", 0xCA }, { "Euml", 0xCB }, { "Igrave", 0xCC }, { "Iacute", 0xCD }, { "Icirc", 0xCE },
		{ "Iuml", 0xCF }, { "ETH", 0xD0 }, { "Ntilde", 0xD1 }, { "Ograve", 0xD2 }, { "Oacute", 0xD3 },
		{ "Ocirc", 0xD4 }, { "Otilde", 0xD5 }, { "Ouml", 0xD6 }, { "Oslash", 0xD8 }, { "Ugrave", 0xD9 },
		{ "Uacute", 0xDA }, { "Ucirc", 0xDB }, { "Uuml", 0xDC }, { "Yacute", 0xDD }, { "THORN", 0xDE },
		{ "szlig", 0xDF }, { "agrave", 0xE0 }, { "aacute", 0xE1 }, { "acirc", 0xE2 }, { "atilde", 0xE3 },
		{ "auml", 0xE4 }, { "aring", 0xE5 }, { "aelig", 0xE6 }, { "ccedil", 0xE7 }, { "egrave", 0xE8 },
		{ "eacute", 0xE9 }, { "ecirc", 0xEA }, { "euml", 0xEB }, { "igrave", 0xEC }, { "iacute", 0xED },
		{ "icirc", 0xEE }, { "iuml", 0xEF }, { "eth", 0xF0 }, { "ntilde", 0xF1 }, { "ograve", 0xF2 },
		{ "oacute", 0xF3 }, { "ocirc", 0xF4 }, { "otilde", 0xF5 }, { "ouml", 0xF6 }, { "oslash", 0xF8 },
		{ "ugrave", 0xF9 }, { "uacute", 0xFA }, { "ucirc", 0xFB }, { "uuml", 0xFC }, { "yacute", 0xFD },
		{ "thorn", 0xFE }, { "yuml", 0xFF }, { "OElig", 0x152 }, { "oelig", 0x153 }, { "Scaron", 0x160 },
		{ "scaron", 0x161 }, { "Yuml", 0x178 }, { "fnof", 0x192 }, { "circ", 0x2C6 }, { "tilde", 0x2DC },
		{ "Alpha", 0x391 }, { "Beta", 0x392 }, { "Gamma", 0x393 }, { "Delta", 0x394 }, { "Omega", 0x3A9 },
		{ "alpha", 0x3B1 }, { "beta", 0x3B2 }, { "gamma", 0x3B3 }, { "delta", 0x3B4 }, { "pi", 0x3C0 },
		{ "mu", 0x3BC }, { "sigma", 0x3C3 }, { "omega", 0x3C9 }, { "infin", 0x221E }, { "ne", 0x2260 },
		{ "le", 0x2264 }, { "ge", 0x2265 }, { "minus", 0x2212 },
	};

	// Decodes the entity at s[i] ('&'); returns false if it is not one.
	bool entity (const std::string &s, size_t &i, std::string &o)
	{
		size_t j = i + 1;
		if (j < s.size () && s[j] == '#')
		{
			++j;
			bool hex = j < s.size () && (s[j] == 'x' || s[j] == 'X');
			if (hex) ++j;
			size_t k = j;
			unsigned long c = 0;
			while (k < s.size () && k - j < 8 && (hex ? isxdigit ((unsigned char) s[k]) : isdigit ((unsigned char) s[k])))
			{
				c = c * (hex ? 16 : 10) + (isdigit ((unsigned char) s[k]) ? s[k] - '0' : (tolower (s[k]) - 'a' + 10));
				++k;
			}
			if (k == j) return false;
			if (k < s.size () && s[k] == ';') ++k;
			// Windows-1252 numbers that are wrongly used for punctuation.
			static const unsigned short W1252[32] = { 0x20AC,0,0x201A,0x192,0x201E,0x2026,0x2020,0x2021,0x2C6,0x2030,0x160,0x2039,0x152,0,0x17D,0,
													  0,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x2DC,0x2122,0x161,0x203A,0x153,0,0x17E,0x178 };
			if (c >= 0x80 && c < 0xA0 && W1252[c - 0x80]) c = W1252[c - 0x80];
			put_utf8 (o, c);
			i = k;
			return true;
		}
		size_t k = j;
		while (k < s.size () && k - j < 10 && isalnum ((unsigned char) s[k])) ++k;
		if (k == j) return false;
		std::string name = s.substr (j, k - j);
		for (const Entity &e : ENTITIES)
			if (name == e.name)
			{
				put_utf8 (o, e.code);
				i = k + ((k < s.size () && s[k] == ';') ? 1 : 0);
				return true;
			}
		return false;
	}

	std::string decode (const std::string &s)
	{
		std::string o;
		for (size_t i = 0; i < s.size (); )
		{
			if (s[i] == '&' && entity (s, i, o)) continue;
			o += s[i++];
		}
		return o;
	}

	// Characters on the screen in a UTF-8 string.
	size_t width_of (const std::string &s)
	{
		size_t n = 0;
		for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
		return n;
	}

	struct Tag
	{
		std::string name;			// lower case, without '/'
		bool		close = false, selfclose = false;
		std::vector<std::pair<std::string, std::string>> attrs;

		std::string attr (const char *n) const
		{
			for (auto &a : attrs) if (a.first == n) return a.second;
			return "";
		}
	};

	// Parses the tag starting at s[i] ('<'); i is moved past it.
	bool parse_tag (const std::string &s, size_t &i, Tag &T)
	{
		size_t j = i + 1;
		if (j < s.size () && s[j] == '/') { T.close = true; ++j; }
		size_t n = j;
		while (n < s.size () && (isalnum ((unsigned char) s[n]) || s[n] == ':' || s[n] == '-')) ++n;
		if (n == j || !isalpha ((unsigned char) s[j])) return false;
		T.name = lower (s.substr (j, n - j));
		size_t k = n;
		while (k < s.size () && s[k] != '>')
		{
			if (isspace ((unsigned char) s[k]) || s[k] == '/')
			{
				if (s[k] == '/' && k + 1 < s.size () && s[k + 1] == '>') T.selfclose = true;
				++k;
				continue;
			}
			size_t a = k;
			while (k < s.size () && !isspace ((unsigned char) s[k]) && s[k] != '=' && s[k] != '>' && s[k] != '/') ++k;
			std::string an = lower (s.substr (a, k - a)), av;
			while (k < s.size () && isspace ((unsigned char) s[k])) ++k;
			if (k < s.size () && s[k] == '=')
			{
				++k;
				while (k < s.size () && isspace ((unsigned char) s[k])) ++k;
				if (k < s.size () && (s[k] == '"' || s[k] == '\''))
				{
					char q = s[k++];
					size_t e = s.find (q, k);
					if (e == std::string::npos) e = s.size ();
					av = s.substr (k, e - k);
					k = e < s.size () ? e + 1 : e;
				}
				else
				{
					size_t v = k;
					while (k < s.size () && !isspace ((unsigned char) s[k]) && s[k] != '>') ++k;
					av = s.substr (v, k - v);
				}
			}
			if (!an.empty ()) T.attrs.push_back ({ an, decode (av) });
			if (k == a) ++k;
		}
		i = k < s.size () ? k + 1 : k;
		return true;
	}

	bool is_void (const std::string &n)
	{
		static const char *V[] = { "br", "img", "hr", "meta", "link", "input", "area", "base", "col",
								   "embed", "source", "track", "wbr", "param", NULL };
		for (int i = 0; V[i]; ++i) if (n == V[i]) return true;
		return false;
	}

	bool hidden (const Tag &T)
	{
		std::string st;
		for (char c : lower (T.attr ("style"))) if (!isspace ((unsigned char) c)) st += c;
		return st.find ("display:none") != std::string::npos || st.find ("mso-hide:all") != std::string::npos
			|| st.find ("visibility:hidden") != std::string::npos || (std::find_if (T.attrs.begin (), T.attrs.end (),
					[](const std::pair<std::string, std::string> &a) { return a.first == "hidden"; }) != T.attrs.end ());
	}

	//---------------------------------------------------------------------------
	// The text being built
	//---------------------------------------------------------------------------

	struct List { bool ordered; int next; };

	class Writer
	{
		public:
			int width;
			std::vector<std::string> lines;
			std::string	para;				// inline text of the paragraph, spaces collapsed
			std::string	bullet;				// for the first line of the next paragraph
			int			quote = 0;			// blockquote depth
			int			indent = 0;			// list indentation, in spaces
			bool		blank = false;		// a blank line before the next text
			bool		pre = false;

			std::string prefix (bool first)
			{
				std::string p;
				for (int i = 0; i < quote; ++i) p += "> ";
				p += std::string (indent, ' ');
				if (first) p += bullet;
				else p += std::string (width_of (bullet), ' ');
				return p;
			}

			void emit (const std::string &l)
			{
				if (blank && !lines.empty () && !is_blank (lines.back ())) lines.push_back (quote_only ());
				blank = false;
				std::string t = l;
				while (!t.empty () && t.back () == ' ') t.pop_back ();
				lines.push_back (t);
			}

			std::string quote_only (void)
			{
				std::string p;
				for (int i = 0; i < quote; ++i) p += ">";
				return p;
			}

			static bool is_blank (const std::string &l)
			{
				for (char c : l) if (c != ' ' && c != '>') return false;
				return true;
			}

			void text (const std::string &t)
			{
				if (pre)
				{
					for (char c : t)
					{
						if (c == '\n') { emit (prefix (true) + para); para.clear (); bullet.clear (); }
						else if (c != '\r') para += (c == '\t') ? std::string (8 - (width_of (para) % 8), ' ') : std::string (1, c);
					}
					return;
				}
				for (size_t i = 0; i < t.size (); ++i)
				{
					unsigned char c = t[i];
					bool sp = isspace (c);
					// U+00A0 (no-break space) is a space that is not collapsed.
					if (c == 0xC2 && i + 1 < t.size () && (unsigned char) t[i + 1] == 0xA0) { para += ' '; ++i; continue; }
					if (sp)
					{
						if (!para.empty () && para.back () != ' ') para += ' ';
						continue;
					}
					para += (char) c;
				}
			}

			// Ends the paragraph: wraps it into lines.
			void flush (void)
			{
				if (pre)
				{
					if (!para.empty ()) { emit (prefix (true) + para); para.clear (); bullet.clear (); }
					return;
				}
				size_t a = para.find_first_not_of (' ');
				if (a == std::string::npos) { para.clear (); return; }
				std::string p = para.substr (a);
				while (!p.empty () && p.back () == ' ') p.pop_back ();
				para.clear ();

				bool first = true;
				std::string line;
				size_t avail = width > 0 ? std::max (20, width - (int) width_of (prefix (true))) : (size_t) -1;
				size_t pos = 0;
				while (pos < p.size ())
				{
					size_t e = p.find (' ', pos);
					if (e == std::string::npos) e = p.size ();
					std::string word = p.substr (pos, e - pos);
					pos = e + 1;
					if (!line.empty () && width_of (line) + 1 + width_of (word) > avail)
					{
						emit (prefix (first) + line);
						first = false;
						line.clear ();
					}
					if (!line.empty ()) line += ' ';
					line += word;
				}
				if (!line.empty ()) emit (prefix (first) + line);
				bullet.clear ();
			}

			void block (bool with_blank)
			{
				flush ();
				if (with_blank) blank = true;
			}

			void br (void)
			{
				if (pre) { text ("\n"); return; }
				if (para.find_first_not_of (' ') == std::string::npos)
				{
					// An empty line, but not more than one in a row.
					if (!lines.empty () && !is_blank (lines.back ())) lines.push_back (quote_only ());
					para.clear ();
					return;
				}
				flush ();
			}
	};
}

namespace htmltext
{
	std::string MetaCharset (const std::string &html)
	{
		std::string h = lower (html.substr (0, 8192));
		for (size_t p = h.find ("<meta"); p != std::string::npos; p = h.find ("<meta", p + 5))
		{
			size_t e = h.find ('>', p);
			std::string tag = h.substr (p, e == std::string::npos ? std::string::npos : e - p);
			size_t c = tag.find ("charset=");
			if (c == std::string::npos) continue;
			c += 8;
			while (c < tag.size () && (tag[c] == '"' || tag[c] == '\'' || tag[c] == ' ')) ++c;
			size_t z = c;
			while (z < tag.size () && (isalnum ((unsigned char) tag[z]) || tag[z] == '-' || tag[z] == '_' || tag[z] == ':' || tag[z] == '.')) ++z;
			if (z > c) return tag.substr (c, z - c);
		}
		return "";
	}

	std::string ToText (const std::string &s, int width)
	{
		Writer W;
		W.width = width;
		std::vector<List> lists;
		std::vector<std::string> links;
		std::string skip;			// element whose content is left out
		int skipdepth = 0;
		std::string href;			// of the link being read
		size_t link_start = 0;		// where its text starts in para
		int heading = 0;
		size_t heading_line = 0;
		int cells = 0;				// cells so far in the table row

		size_t i = 0;
		std::string text;
		auto flush_text = [&]()
		{
			if (!text.empty ()) { W.text (decode (text)); text.clear (); }
		};

		while (i < s.size ())
		{
			char c = s[i];
			if (c != '<')
			{
				if (skip.empty ()) text += c;
				++i;
				continue;
			}
			// Comments, <!DOCTYPE>, <![CDATA[...]]>, <?xml?>
			if (s.compare (i, 4, "<!--") == 0)
			{
				size_t e = s.find ("-->", i + 4);
				i = e == std::string::npos ? s.size () : e + 3;
				continue;
			}
			if (s.compare (i, 9, "<![CDATA[") == 0)
			{
				size_t e = s.find ("]]>", i + 9);
				if (skip.empty ()) text += s.substr (i + 9, (e == std::string::npos ? s.size () : e) - i - 9);
				i = e == std::string::npos ? s.size () : e + 3;
				continue;
			}
			if (i + 1 < s.size () && (s[i + 1] == '!' || s[i + 1] == '?'))
			{
				size_t e = s.find ('>', i);
				i = e == std::string::npos ? s.size () : e + 1;
				continue;
			}
			Tag T;
			size_t at = i;
			if (!parse_tag (s, i, T))
			{
				if (skip.empty ()) text += '<';
				i = at + 1;
				continue;
			}
			const std::string &n = T.name;

			// Inside something left out: only count nesting of its kind.
			if (!skip.empty ())
			{
				if (n == skip && !is_void (n) && !T.selfclose)
				{
					if (T.close) { if (--skipdepth == 0) skip.clear (); }
					else ++skipdepth;
				}
				// A <head> without </head>: the body starts at <body>.
				if (skip == "head" && n == "body" && !T.close) { skip.clear (); skipdepth = 0; }
				continue;
			}

			flush_text ();

			if (!T.close && (n == "script" || n == "style" || n == "head" || n == "title" || n == "template"
							 || n == "svg" || n == "math" || n == "object" || n == "iframe" || n == "select"
							 || (hidden (T) && !is_void (n))))
			{
				if (!T.selfclose) { skip = n; skipdepth = 1; }
				continue;
			}
			// Raw-text content of script/style ends only at the closing tag.
			if (n == "br") { W.br (); continue; }
			if (n == "wbr") continue;
			if (n == "hr" && !T.close) { W.block (true); W.emit (W.prefix (true) + "* * *"); W.blank = true; continue; }
			if (n == "img" && !T.close)
			{
				std::string alt = T.attr ("alt");
				size_t a = alt.find_first_not_of (" \t\r\n");
				if (a != std::string::npos && !hidden (T)) W.text ("[" + alt.substr (a) + "] ");
				continue;
			}
			if (n == "a")
			{
				if (!T.close)
				{
					href = T.attr ("href");
					link_start = W.para.size ();
				}
				else if (!href.empty ())
				{
					std::string h = href;
					href.clear ();
					std::string lh = lower (h);
					std::string shown = link_start <= W.para.size () ? W.para.substr (link_start) : "";
					while (!shown.empty () && shown.back () == ' ') shown.pop_back ();
					size_t f = shown.find_first_not_of (' ');
					shown = f == std::string::npos ? "" : shown.substr (f);
					std::string bare = h.compare (0, 7, "mailto:") == 0 ? h.substr (7) : h;
					if (h[0] == '#' || lh.compare (0, 11, "javascript:") == 0 || shown == bare || shown == h
						|| shown.empty ())
					{
						if (shown.empty () && lh.compare (0, 4, "http") == 0) W.text (h);
						continue;
					}
					// The same address again gets the same number.
					size_t num = 0;
					for (size_t k = 0; k < links.size (); ++k) if (links[k] == h) num = k + 1;
					if (!num) { links.push_back (h); num = links.size (); }
					if (!W.para.empty () && W.para.back () == ' ') W.para.pop_back ();
					W.para += "[" + std::to_string (num) + "]";
				}
				continue;
			}
			if (n.size () == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6')
			{
				if (!T.close)
				{
					W.block (true);
					heading = n[1] - '0';
					heading_line = W.lines.size ();
				}
				else
				{
					W.flush ();
					// Underline the first two levels.
					if (heading && heading <= 2 && W.lines.size () > heading_line)
					{
						size_t len = 0;
						for (size_t k = heading_line; k < W.lines.size (); ++k)
							len = std::max (len, width_of (W.lines[k]) - width_of (W.prefix (true)));
						if (len > 0 && len < 200)
							W.lines.push_back (W.prefix (false) + std::string (len, heading == 1 ? '=' : '-'));
					}
					heading = 0;
					W.blank = true;
				}
				continue;
			}
			if (n == "p" || n == "blockquote" || n == "pre" || n == "table" || n == "ul" || n == "ol"
				|| n == "dl" || n == "address" || n == "figure" || n == "center")
			{
				bool nested_list = (n == "ul" || n == "ol") && !lists.empty ();
				W.block (!nested_list);
				if (n == "blockquote")
				{
					// The blank line goes outside the quote that starts or ends.
					if (T.close && W.quote > 0) --W.quote;
					if (W.blank && !W.lines.empty () && !Writer::is_blank (W.lines.back ()))
						W.lines.push_back (W.quote_only ());
					else if (T.close && !W.lines.empty () && Writer::is_blank (W.lines.back ()))
						W.lines.back () = W.quote_only ();
					W.blank = false;
					if (!T.close) ++W.quote;
					W.blank = true;
				}
				if (n == "pre") W.pre = !T.close;
				if (n == "ul" || n == "ol")
				{
					if (!T.close)
					{
						int start = atoi (T.attr ("start").c_str ());
						lists.push_back ({ n == "ol", start > 0 ? start : 1 });
						if (lists.size () > 1) W.indent += 4;
					}
					else if (!lists.empty ())
					{
						if (lists.size () > 1) W.indent -= 4;
						lists.pop_back ();
					}
				}
				continue;
			}
			if (n == "li")
			{
				W.block (false);
				if (!T.close)
				{
					if (lists.empty ()) W.bullet = "* ";
					else if (lists.back ().ordered) W.bullet = std::to_string (lists.back ().next++) + ". ";
					else W.bullet = lists.size () % 2 ? "* " : "- ";
					if (W.indent == 0) W.bullet = "  " + W.bullet;
				}
				continue;
			}
			if (n == "dt") { W.block (false); continue; }
			if (n == "dd") { W.block (false); if (!T.close) W.bullet = "    "; continue; }
			if (n == "tr") { W.block (false); cells = 0; continue; }
			if (n == "td" || n == "th")
			{
				if (!T.close)
				{
					// Cells side by side, two spaces apart.
					if (cells++ && !W.para.empty () && W.para.back () != ' ') W.para += "  ";
					else if (cells > 1 && !W.para.empty ()) W.para += ' ';
				}
				continue;
			}
			if (n == "div" || n == "section" || n == "article" || n == "header" || n == "footer"
				|| n == "nav" || n == "aside" || n == "main" || n == "form" || n == "fieldset"
				|| n == "caption" || n == "figcaption" || n == "tbody" || n == "thead" || n == "tfoot"
				|| n == "body" || n == "html")
			{
				W.block (false);
				continue;
			}
			// Everything else (span, b, i, font, ...) is inline.
		}
		flush_text ();
		W.flush ();

		// Drop blank lines at the start and the end, and runs of them.
		std::vector<std::string> out;
		for (auto &l : W.lines)
		{
			bool b = Writer::is_blank (l);
			if (b && (out.empty () || Writer::is_blank (out.back ()))) continue;
			out.push_back (l);
		}
		while (!out.empty () && Writer::is_blank (out.back ())) out.pop_back ();

		std::string r;
		for (auto &l : out) r += l + "\n";
		if (!links.empty ())
		{
			r += "\nLinks:\n";
			for (size_t k = 0; k < links.size (); ++k)
				r += "[" + std::to_string (k + 1) + "] " + links[k] + "\n";
		}
		// Invisible characters used as padding in newsletters.
		std::string clean;
		for (size_t k = 0; k < r.size (); ++k)
		{
			unsigned char a = r[k];
			if (a == 0xC2 && k + 1 < r.size () && (unsigned char) r[k + 1] == 0xAD) { ++k; continue; }			// soft hyphen
			if (a == 0xCD && k + 1 < r.size () && (unsigned char) r[k + 1] == 0x8F) { ++k; continue; }			// U+034F
			if (a == 0xE2 && k + 2 < r.size () && (unsigned char) r[k + 1] == 0x80
				&& ((unsigned char) r[k + 2] >= 0x8B && (unsigned char) r[k + 2] <= 0x8F)) { k += 2; continue; }	// U+200B..200F
			if (a == 0xEF && k + 2 < r.size () && (unsigned char) r[k + 1] == 0xBB && (unsigned char) r[k + 2] == 0xBF) { k += 2; continue; }	// BOM
			clean += (char) a;
		}
		return clean;
	}
}
