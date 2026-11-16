#ifndef _MAILHEADERS_H
#define _MAILHEADERS_H 1
//===============================================================================
// Neo-PostOffice: helpers for reading e-mail header fields
// ---------------------------------------------------------
// Shared by the Maildir mailbox and po-sqldeliver.
//===============================================================================

#include <time.h>
#include <string>
#include <vector>

namespace mailheaders
{
	struct Header { std::string Name, Value; };

	// Parses the header of a message (up to the first empty line), with
	// continuation lines unfolded. CR before LF is ignored.
	std::vector<Header>	Parse (const std::string &message);

	// The value of the first header with this name (case-insensitive), or "".
	std::string			Get (const std::vector<Header> &, const char *name);

	// Decodes RFC 2047 encoded words (=?charset?B|Q?...?=) and returns
	// UTF-8. Text that is not UTF-8 is taken as ISO-8859-1.
	std::string			Decode (const std::string &value);

	// Splits "Name <addr>", "addr (Name)" or "addr" into name and address.
	void				SplitAddress (const std::string &from, std::string &name, std::string &addr);

	// Reads an RFC 5322 date ("Mon, 10 Nov 2025 10:00:00 +0100", obsolete
	// zone names and comments tolerated). Returns 0 if it cannot be read.
	time_t				ParseDate (const std::string &value);
}

#endif
