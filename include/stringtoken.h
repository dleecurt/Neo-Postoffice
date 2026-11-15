#ifndef _STRINGTOKEN_H
#define _STRINGTOKEN_H 1

#include "btree.h"

class StringToken : public BTreeNode
{
	public:
				 StringToken (const char *key, StringToken *root);
				~StringToken (void);
				
		int		 refcount;
};

class StringTokenList
{
	public:
					 StringTokenList (void);
					~StringTokenList (void);
				
		const char	*open_token (const char *string);
		void		 close_token (const char *string);
		
	protected:
		StringToken	*root;
};

extern StringTokenList TOKEN;

#endif
