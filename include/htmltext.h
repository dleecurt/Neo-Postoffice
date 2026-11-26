#ifndef _HTMLTEXT_H
#define _HTMLTEXT_H 1
//===============================================================================
// Neo-PostOffice: readable text from HTML mail
// ---------------------------------------------
// For messages that have only an HTML version. Paragraphs, line breaks,
// headings, lists, quotes (as "> "), preformatted text and tables are kept
// as plain-text layout; entities are decoded; links are numbered [1], [2]
// and listed at the end; images show their alt text. Scripts, styles and
// hidden elements (display:none) are left out, and nothing is fetched.
//===============================================================================

#include <string>

namespace htmltext
{
	// html is UTF-8. Lines are wrapped at width characters (0: not wrapped).
	std::string ToText (const std::string &html, int width = 76);

	// The charset named in a <meta> tag, or "".
	std::string MetaCharset (const std::string &html);
}

#endif
