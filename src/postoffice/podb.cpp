//===============================================================================
// Neo-PostOffice database layer: common code
//===============================================================================

#include "podb.h"

#include <stdlib.h>

namespace podb
{

long Result::GetLong (int r, int c) const
{
	if (r < 0 || r >= Rows() || c < 0 || c >= Columns || Nulls[r][c]) return 0;
	return strtol (Data[r][c].c_str(), NULL, 10);
}

// Checks whether s is well-formed UTF-8 (no overlong forms or surrogates).
static bool valid_utf8 (const unsigned char *s)
{
	while (*s)
	{
		unsigned char c = *s;
		int n;
		unsigned int cp;

		if (c < 0x80) { ++s; continue; }
		else if ((c & 0xe0) == 0xc0) { n = 1; cp = c & 0x1f; }
		else if ((c & 0xf0) == 0xe0) { n = 2; cp = c & 0x0f; }
		else if ((c & 0xf8) == 0xf0) { n = 3; cp = c & 0x07; }
		else return false;

		for (int i = 1; i <= n; ++i)
		{
			if ((s[i] & 0xc0) != 0x80) return false;
			cp = (cp << 6) | (s[i] & 0x3f);
		}
		if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
		    cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
			return false;
		s += n + 1;
	}
	return true;
}

std::string Utf8 (const char *s)
{
	if (!s) return std::string();
	if (valid_utf8 ((const unsigned char *) s)) return std::string (s);

	// Not UTF-8: treat it as ISO-8859-1, which maps byte for byte to U+0000..U+00FF.
	std::string out;
	for (const unsigned char *p = (const unsigned char *) s; *p; ++p)
	{
		if (*p < 0x80) out += (char) *p;
		else
		{
			out += (char) (0xc0 | (*p >> 6));
			out += (char) (0x80 | (*p & 0x3f));
		}
	}
	return out;
}

#ifndef PO_HAVE_POSTGRESQL
bool HavePostgreSQL (void) { return false; }
std::unique_ptr<Connection> ConnectPostgreSQL (const std::string &, std::string &error)
{
	error = "this build of Neo-PostOffice has no PostgreSQL support";
	return NULL;
}
#endif

}
