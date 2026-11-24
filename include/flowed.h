// Line wrapping and format=flowed (RFC 3676) for the text you write
// (Neo-PostOffice).
#ifndef _FLOWED_H
#define _FLOWED_H 1

#include <string>

class Var;

namespace flowed
{
	// The composer's line width from the settings (postoffice/gui/wrap):
	// 72 when unset, kept within 60 to 100.
	int				WrapWidth (Var *prefs);

	// Characters (not bytes) in UTF-8 text.
	size_t			Length (const std::string &text);

	// The editor's text (soft line breaks as \r, hard ones as \n) as a
	// format=flowed body: each paragraph wrapped at width characters, the
	// lines of a paragraph ending in a space. Quoted lines (starting with
	// '>') are re-wrapped with their quote marks and sent as fixed lines.
	// Lines starting with a space or "From " are space-stuffed, and other
	// trailing spaces removed, so nothing flows by accident.
	std::string		Flow (const std::string &text, int width);

	// Text sent unwrapped (the signature) made safe for format=flowed:
	// no trailing spaces (except the "-- " separator), stuffed as above.
	std::string		Fixed (const std::string &text);

	// A format=flowed body back as plain paragraphs, one line each (for
	// editing a draft again): flowed lines joined, space-stuffing removed.
	std::string		Unflow (const std::string &text);
}

#endif
