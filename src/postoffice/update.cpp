//===============================================================================
// POSTOFFICE - X11 Office Communication Handler
// ---------------------------------------------
// All sourcecode, unless differently annotated, written by Pim van Riezen
// Sourcecode and resulting binaries are covered by the GNU Lesser General Public
// License (GNU LGPL). For more information and the literal text of the license
// agreement, check the Free Software Foundation's website on http://www.fsf.org/
// 
// Copyright (C) 1999 Mad Sciene Labs - Rotterdam, The Netherlands
//===============================================================================

#include "wordlist.h"
#include "mysqlclient.h"
#include <string.h>

main()
{
	void *dbh;
	char buf[256],buf2[256];
	WordList *res, *res1, *col;
	int i,src,dst;
	char *mail;
	
	dbh = dbdriver_open ("sql-1",3306,"pim","tepelrups","pim");
	res = dbdriver_execute (dbh, "SELECT msgid,value FROM headers WHERE name='From'");
	for (i=0;i<res->Count;++i)
	{
		col = res->GetWordList (i);
		if (strchr (mail = col->Get(1), '\''))
		{
			src=(strchr (mail, '\'') - mail)+1;
			dst=0;
			while ((mail[src])&&(mail[src]!='\''))
			{
				buf2[dst++] = mail[src++];
			}
			buf2[dst] = 0;
		}
		else if (strchr (mail, '<'))
		{
			src=0; dst=0;
			while ((mail[src])&&(mail[src]!='<')&&( (mail[src]!=' ')||(mail[src+1]!='<')))
			{
				buf2[dst++] = mail[src++];
			}
			buf2[dst] = 0;
		}
		else strcpy (buf2, mail);
		sprintf (buf, "UPDATE messages SET namefrom='%s' WHERE msgid=%s", buf2, col->Get(0));
		res1 = dbdriver_execute (dbh, buf);
		delete (res1);
	}
	delete (res);
	dbdriver_close (dbh);
}