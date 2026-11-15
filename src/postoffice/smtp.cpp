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

#define ST_STARTUP 	0x01
#define ST_SENTHELO 0x02
#define ST_SENTFROM 0x03
#define ST_SENTRCPT 0x04
#define ST_SENTDATA 0x05
#define ST_SENTMAIL 0x06
#define ST_QUIT     0x07

#include "socketclasses.h"
#include "var.h"
#include "wordlist.h"
#include <time.h>
#include <unistd.h>
#include <FL/Fl.H>

#include "debug.h"

extern Var PREFERENCES;

char *send_message_smtp (WordList *HdrNames, WordList *HdrValues, char *mailfrom, WordList *Recipients, char *message)
{
	char		 buffer    [1024];
	char         hostname  [128];
	char         domainname[128];
	char         fqdn      [128];

	int			 state;
	int			 res;
	int			 successfully_sent;
	int			 i;
	int			 errors = 0;
	time_t		 ti;
	char		*error = NULL;
	char		*ct;
	WordList	 WD;

	// Initialize timezone stuff

	tzset();

	// Set state machine
	
	state = ST_STARTUP;
	successfully_sent = 0;

	// Open a connection with the SMTP server
	
	Connection *C = new Connection (1);
	C->Open (PREFERENCES.Get_char_tree ("postoffice/network/smtp"),25);

	if (gethostname(hostname,128) < 0) 
	{
	   strcpy(hostname,"unknown");
		dprintf ("[smtp] gethostname() error");
   }

	if (getdomainname(domainname,128) < 0)
	{
	   strcpy(domainname,"unknown");
		dprintf ("[smtp] getdomainname() error");
   }

   sprintf(fqdn,"%s.%s",hostname,domainname);

	// Bail out when the connection fails
	
	while ((res = C->Handle(buffer, 255, 1, 0, 0)) >= 0)
	{
		dprintf ("[smtp] state=%02x\n", state);
		if (res>0)
		{
			dprintf ("[smtp] %s\n", buffer);
			switch (state)
			{
				case ST_STARTUP:
				//-----------------------------------------------
				// This is the initial state once we connect
				// to the server.  We send a HELO message along
				// with our fully qualified domain name.
				//-----------------------------------------------
				
					C->SendF (10, "HELO %s\r\n",fqdn);
					state = ST_SENTHELO;
					break;
				
				case ST_SENTHELO:
				//-----------------------------------------------
				// We enter this state after having sent a HELO
				// greeting.
				//
				// If we get a 250 OK back, continue to send the
				// message.  If we get a 220 back, we're still
				// being greeted, and just hang on a minute for
				// the OK.
				//
				// If the reply is not 250 or 220, then there
				// is an error and backout of things.
				//-----------------------------------------------
				   if (strncmp(buffer,"250",3) == 0) 
					{
						C->SendF (10, "MAIL FROM: %s\r\n", mailfrom);
						state = ST_SENTFROM;
					}
					else if (strncmp(buffer,"220",3) != 0)
					{
						error = new char[strlen(buffer+4)+1];
						strcpy (error, buffer+4);
						state = ST_QUIT;
					}
					break;

				case ST_SENTFROM:
				//-----------------------------------------------
				// Once we've send the MAIL FROM: part, we check
				// the return code.  If it's 250, then we send
				// the RCPT TO:.  If not, we error out.
				//-----------------------------------------------
				
				   if (strncmp(buffer,"250",3) == 0) 
					{
						C->SendF (10, "RCPT TO: %s\r\n", Recipients->Get(0));
						Recipients->Delete (0);
						if (Recipients->Count == 0)
							state = ST_SENTRCPT;
					}
					else
					{
						error = new char[strlen(buffer+4)+1];
						strcpy (error, buffer+4);
						state = ST_QUIT;
					}
					break;

				case ST_SENTRCPT:
				//-----------------------------------------------
				// Once we've send the RCPT TO: part, we check
				// the return code.  If it's 250, then we send
				// the DATA.  If not, then we error out.
				//-----------------------------------------------
				
				   if (strncmp(buffer,"250",3) == 0) 
					{
						C->SendF (10, "DATA\r\n");
						state = ST_SENTDATA;
					}
					else
					{
						error = new char[strlen(buffer+4)+1];
						strcpy (error, buffer+4);
						state = ST_QUIT;
					}
					break;
					
				case ST_SENTDATA:
				//-----------------------------------------------
				// Once we've send the DATA line, we check the
				// return code.  If it's 354, then we send
				// the body of the message.  If not, then we 
				// error out.
				//-----------------------------------------------
				
					ti = time (NULL);
					ct = ctime (&ti);
				
				   if (strncmp(buffer,"354",3) == 0) 
					{
						for (i=0;i<HdrNames->Count;++i)
						{
							C->SendF (10, "%s: %s\r\n", HdrNames->Get(i), HdrValues->Get(i));
						}
						C->SendF (10, "X-Mailer: PostOffice 0.7\r\n\r\n");
						WD.MakeGeneric (message, '\n', 0);
						for (i=0;i<WD.Count;++i)
						{
							C->Send (WD.Get(i),10);
							C->Send ("\r\n", 10);
						}
						//puts (message);
						//C->Send (message, 10);
						C->SendF (10, "\r\n.\r\n");
						state = ST_SENTMAIL;
						successfully_sent = 1;
					}
					else
					{
						error = new char[strlen(buffer+4)+1];
						strcpy (error, buffer+4);
						state = ST_QUIT;
					}
					break;
					
				case ST_SENTMAIL:
				//-----------------------------------------------
				// Once we've sent the body of the message,
				// we check the error code and then QUIT.
				//-----------------------------------------------
			
				   if (strncmp(buffer,"250",3) != 0) 
					{
						error = new char[strlen(buffer+4)+1];
						strcpy (error, buffer+4);
					}
					C->SendF (10, "QUIT\r\n");
					state = ST_QUIT;
					break;
					
				case ST_QUIT:
					C->Close();
					break;
			}
		}
		else
		{
			//Fl::wait (1.0);
			if (++errors > 256)
			{
				error = new char[strlen (C->Address) + 64];
				sprintf (error, "Connection to %s timed out", C->Address);
				delete (C);
				return (error);
			}
		}
	}
    if (!successfully_sent)
	{
		error = new char[strlen (C->Address) + 64];
		sprintf (error, "Unspecified connection error to %s\n", C->Address);
	}
	delete (C);
	return (error);
}
