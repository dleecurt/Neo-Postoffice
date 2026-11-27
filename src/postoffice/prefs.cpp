#include <string.h>
/*

typedef void * mysqlhandle;

int PrefsFromDB (char *hostname, char *username, char *password, char *database)
{
	mysqlhandle	  db;
	char 			*table = new char[strlen(username)+32];
	WordList		*result;

	sprintf (table, "%s_prefs", username);

	db = dbdriver_open (hostname, 3306, username, password, database);
	if (db)
	{
		AddChildNodes (&PREFERENCES, db, 0, table);
	}
}

void AddChildNodes (Var *V, mysqlhandle db, int id, char *table)
{
	WordList		*result;
	WordList		*row;
	char			  type;
	Var			*nV;
	int			  nid;
	
	result = dbdriver_executef ("SELECT id,name,value,type FROM %s WHERE parent=%i", table, id);
	if (result->Count)
	{
		for (i=0; i<result->Count;++i)
		{
			row = result->GetWordList (i);
			
			type = row->Get(3)[0];
			nid = atoi (row->Get(0));
			
			switch (type)
			{
				case 'k' :
					V->Set_char (row->Get(1), row->Get(2));
					break;
				
				case 's' :
					nV = new Var;
					V->Set_var (row->Get(1), nV);
					AddChildNodes (nV, db, nid, table);
					break;
			}
		}
	}
	delete (result);
}

*/