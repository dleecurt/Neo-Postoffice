#include "socketclasses.h"
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include "event.h"
#include "wordlist.h"
#include "reg.h"
#include "dbdriver.h"
#include "encode.h"

// System default defines (yeah yeah this should be in a config file, call me in Hawaii and we'll discuss
// version 2 :-)

#define SERVERNAME "hoorn.vuurwerk.nl"
#define SQLSERVER  "sql-1"
#define SQLUSER    "pim"
#define SQLPASS    "tepelrups"
#define SQLDB      "pim"
#define ALLOWFILE  "./mailer.allow"
#define CWFILE     "./mailer.cw"
#define MAXSIZE	   256*1024

// libstealthnet mandatory externs

char *CWD = ".";
int  DEBUGLEVEL = 0;
XEventPipe *EVENTPIPE = NULL;

// Database Driver List

DBList DBL;

class Message
{
	public:
					 Message (void);
					~Message (void);

		char		*From;
		char		*To;
		int			 msgid;
		WordList	*Body;
};

Message		*vwm_get_message (int);
WordList	*vwm_querymail (char *, char *);

int main(int argc, char *argv[])
{
	WordList		*res;
	int				 i,j;
	
	if (argc<3)
	{
		fprintf (stderr, "Usage: %s <header> <expression>\n", argv[0]);
		return 1;
	}
	
	DBL.Addsource ("default","mysql",SQLSERVER,3306,SQLUSER,SQLPASS,SQLDB);
	
	res = vwm_querymail (argv[1], argv[2]);

	printf ("%i messages for %s\n", res->Count, argv[1]);
	
	for (i=0;i<res->Count;++i)
	{
		Message *msg;
		
		msg = vwm_get_message (atoi (res->Get(i)));
		if (msg)
		{
			if (msg->Body->Count > 0)
			{
				printf ("--- Message %i (%i lines) MsgID %08d From %s To %s\n", i, msg->Body->Count, msg->msgid, msg->From, msg->To);
				for (j=0;j<msg->Body->Count;++j)
				{
					printf ("%03i %s\n", j, msg->Body->Get(j));
				}
			}
		}
		delete (msg);
	}
}

Message::Message (void)
{
	From = NULL;
	To = NULL;
	Body = NULL;
}

Message::~Message (void)
{
	if (From) delete (From);
	if (To) delete (To);
	if (Body) delete (Body);
}

// Get a Message as a WordList of lines out of the database by its key (message-id).

Message *vwm_get_message (int id)
{
	Message			*result = NULL;
	DBConnection	*DB;
	WordList		*dbr;
	WordList		*dbc;
	char			*body;
	
	DB = DBL.Open ("default");
	dbr = DBL.Execute (DB, "SELECT body FROM messages WHERE msgid=%i", id);
	if (dbr->Count > 0)
	{
		result = new Message;
		body = decode_mysql (dbr->Get(0));
		result->Body = extract_lines (body);
		delete (body);
		delete (dbr);
		
		dbr = DBL.Execute (DB, "SELECT mailfrom,mailto FROM messages WHERE msgid=%i", id);
		dbc = new WordList;
		dbc->MakeDB (dbr->Get(0));
		
		result->From = new char[strlen(dbc->Get(0))+1];
		strcpy (result->From, dbc->Get(0));
		result->To = new char[strlen(dbc->Get(1))+1];
		strcpy (result->To, dbc->Get(1));
		delete (dbr);
		delete (dbc);
		result->msgid = id;
	}
	else
	{
		delete (dbr);
	}
	DBL.Close (DB);
	
	return (result);
}

// Query the message database for email matching a certain header-description.

WordList *vwm_querymail (char *hdr, char *match)
{
	WordList		*result;
	DBConnection	*DB;
	
	DB = DBL.Open ("default");
	result = DBL.Execute (DB, "SELECT DISTINCT msgid FROM headers WHERE name='%s' AND value LIKE '%s'", hdr, match);
	DBL.Close (DB);
	return (result);
}

