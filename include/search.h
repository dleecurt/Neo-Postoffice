// Searching messages (Neo-PostOffice).
#ifndef _SEARCH_H
#define _SEARCH_H 1

#include <string>

// What a search looks in.
enum SearchField
{
	SEARCH_FROM = 0,
	SEARCH_TOCC = 1,		// To and CC
	SEARCH_SUBJECT = 2,
	SEARCH_BODY = 3,		// the message text
	SEARCH_ANYWHERE = 4		// the headers and the text
};

class MailMessage;

namespace search
{
	// Text folded for comparing without regard to case: ASCII, Latin-1,
	// Latin Extended-A, Greek and Cyrillic capitals become small letters.
	std::string		Fold (const std::string &utf8);

	// True if the message (headers as stored, text from its parts when
	// they are in memory) contains needle, which is already folded. The
	// text of M is taken from its parts; for a message whose text is not
	// in memory, pass the raw message as raw.
	bool			Matches (MailMessage *M, SearchField field, const std::string &needle,
							 const std::string *raw = NULL);

	// The text parts of a raw message (MIME walked), decoded to UTF-8.
	std::string		TextOfRaw (const std::string &raw);
}

#endif
