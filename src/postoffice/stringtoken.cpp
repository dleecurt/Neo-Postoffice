#include "stringtoken.h"

#include <string.h>

StringToken::StringToken (const char *key, StringToken *root) : BTreeNode (key, root)
{
	refcount = 0;
}

StringToken::~StringToken (void)
{
	if (refcount)
	{
		fprintf (stderr, "!!! stringtoken deleted with refcount=%i\n", refcount);
	}
}

StringTokenList::StringTokenList (void)
{
	root = new StringToken (NULL, NULL);
}

StringTokenList::~StringTokenList (void)
{
}

const char *StringTokenList::open_token (const char *string)
{
	std::lock_guard<std::mutex> g (Mutex);
	StringToken *tok = (StringToken *) root->Find (string);
	
	if (tok)
	{
		++(tok->refcount);
		return (const char *) tok->BKey;
	}
	
	// Neo-PostOffice: a new token has one user. It started at 0, so the
	// first close of a string shared by two envelopes deleted it while the
	// other still pointed to it.
	tok = new StringToken (string, root);
	tok->refcount = 1;
	return (const char *) tok->BKey;
}

void StringTokenList::close_token (const char *string)
{
	std::lock_guard<std::mutex> g (Mutex);
	StringToken *tok = (StringToken *) root->Find (string);
	if (!tok)
	{
		fprintf (stderr, "!!! attempt to dereference non-existant stringtoken\n");
		return;
	}
	
	--(tok->refcount);
	if (!tok->refcount)
	{
		delete (tok);
	}
}
