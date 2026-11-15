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
	StringToken *tok = (StringToken *) root->Find (string);
	
	if (tok)
	{
		++(tok->refcount);
		return (const char *) tok->BKey;
	}
	
	tok = new StringToken (string, root);
	return (const char *) tok->BKey;
}

void StringTokenList::close_token (const char *string)
{
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
