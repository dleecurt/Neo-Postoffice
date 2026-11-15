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

#ifndef NO_MYSQL

#include "wordlist.h"
#include "mysql.h"
#include <string.h>
#include <stdio.h>

char *dbencode (char *src);

extern "C" int		 dbdriver_init (void);
extern "C" void 	*dbdriver_open (char *, int, char *, char *, char *);
extern "C" WordList	*dbdriver_execute (void *, char *);
extern "C" void		 dbdriver_close (void *);
extern "C" char		*dbdriver_error (void *);


struct mysql_environment
{
	MYSQL			*mysql;
	MYSQL_RES		*mysql_res;
	MYSQL_ROW		 mysql_row;
};

extern "C" int dbdriver_init (void)
{
	return 1;
}

extern "C" void *dbdriver_open (char *host, int port, char *username, char *password, char *database)
{
	mysql_environment *myenv;
	char error [256];
	
	myenv = new mysql_environment;
	
	myenv->mysql = new MYSQL;
	if (!mysql_connect (myenv->mysql, host, username, password))
	{
		delete (myenv->mysql);
		delete (myenv);
		sprintf (error, "unable to connect to database [%s@%s:%s]\n", username, host, database);
		fputs (error, stderr);
		return (NULL);
	}
	
	if (mysql_select_db (myenv->mysql, database))
	{
		mysql_close (myenv->mysql);
		delete (myenv->mysql);
		delete (myenv);
		sprintf (error, "unable to select database [%s@%s:%s]\n", username, host, database);
		fputs (error, stderr);
		return (NULL);
	}
	
	return ((void *) myenv);
}

extern "C" WordList *dbdriver_execute (void *E, char *sql)
{
	WordList *result = new WordList;
	mysql_environment *env = (mysql_environment *) E;
	WordList *row;
	int i;
	int tsz;
//	int rsz;
	int numf;
	
	if (env->mysql)
	{
		if (mysql_query (env->mysql, sql) == 0)
		{
			if ((env->mysql_res = mysql_store_result (env->mysql)))
			{
				while ((env->mysql_row = mysql_fetch_row (env->mysql_res)))
				{
					numf = mysql_num_fields (env->mysql_res);
					tsz = 24+numf;
					
					for (i=0; i<numf; ++i)
					{
						if (env->mysql_row[i]) tsz+= strlen (env->mysql_row[i]);
					}
					
					row = new WordList;
					
					for (i=0; i<numf; ++i)
					{
//						char *enc;
						if (env->mysql_row[i])
						{
							row->Add (env->mysql_row[i]);
						}
						else row->Add ("");
					}
					result->AddWordList_ (row);
				}
				mysql_free_result (env->mysql_res);
			}
		}
		else
		{
			
		}
	}
	return (result);
}

extern "C" void dbdriver_close (void *E)
{
	mysql_environment *env = (mysql_environment *) E;
	
	if (E)
	{
		if (env->mysql)
		{
			mysql_close (env->mysql);
			delete (env->mysql);
		}
		delete (env);
	}
}

extern "C" char *dbdriver_error (void *E)
{
	mysql_environment *env = (mysql_environment *) E;
	
	if (E)
	{
		if (env->mysql)
		{
			return mysql_error (env->mysql);
		}
	}
	return (char *) "";
}

char *dbencode (char *src)
{
	char *result = new char[strlen(src)*2];
	int ln = strlen(src);
	int i;
	int p=0;
	
	for (i=0;i<ln;++i)
	{
		if (src[i]=='|') result[p++]='\\';
		result[p++] = src[i];
	}
	result[p] = 0;
	
	return result;
}

#endif

