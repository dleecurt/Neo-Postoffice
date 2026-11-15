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

#include "mailbox.h"
#include "var.h"
#include "reg.h"
#include "chop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(SOLARIS) || defined(IRIX)
  #include <strings.h>
#endif

#include "debug.h"
#include "status.h"

extern JobList JOBQ;

MailHandler::~MailHandler (void)
{
}

void MailHandler::OpenConnection (void)
{
}

void MailHandler::CloseConnection (void)
{
}

void MailHandler::GetBody (char */*x*/)
{
}

void MailHandler::CheckGroup (const char */*x*/)
{
}

void MailHandler::DeleteMessage (char */*x*/)
{
}

void MailHandler::Expire (void)
{
}

//WordList *MailHandler::ServerGrouplist (void)
//{
//	return (WordList *) NULL;
//}

int MailHandler::PostMessage (WordList */*x*/, MailMessage */*m*/)
{
	return 0;
}

POP3Handler::POP3Handler (MailBox *_Parent, class Var *_Parameters)
{
	char buf[64];
	Parameters = new Var (_Parameters);
	Parent = _Parent;
	Con = new Connection (1);
	ConnectionOpen = 0;
	LastChecked = 0;
	HighestHandled = 0;
	TransMsgID = new Var;
	UIMsgID = new Var;
	sprintf (buf, "%s/uidl.dat", Parent->DirectoryPrefix);
	UIMsgID->Load (buf);
	Parent->ToCollector->AddPriorityClass (CLASS_GETBODY);
}

// So long and thanks for all the fish.

POP3Handler::~POP3Handler (void)
{
	if (Con)
	{
		if (ConnectionOpen) // Hmm, last one out turns off the lights.
		{
			Con->Close();
			ConnectionOpen = 0;
		}
		delete (Con);
		Con = NULL;
	}

	delete (Parameters); // We don't need pants where we're going.
	delete (TransMsgID);
}

void POP3Handler::OpenConnection (void)
{
	char *msg;
	char *pHost = Parameters->Get_char ("host");
	int   pPort = Parameters->Get_int ("port");
	int	  failures = 0;

	// Verify parameters

	if (pPort == 0) pPort = 110;

	if ( (strlen (pHost) == 0) || (pPort == 0) )
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
								   "pop3handler", "Invalid Hostname or Port in preferences. You blew it.");
		return;
	}

	int jid = JOBQ.NewJob (Parent->Name);

	// Send State information - An Informed User Is A Happy User(TM). Not that I care.

	msg = new char[ strlen (pHost) + 32];
	sprintf (msg, "Contacting host %s", pHost);
	JOBQ.Stat (jid, msg);
	JOBQ.Progress (jid, 18);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
	delete[] (msg);	

	// Open Connection

	iprintf ("[pop3handler] Contacting pop3://%s:*@%s:%i/\n", Parameters->Get_char ("user"), pHost, pPort);
	dprintf ("[clue zJzxh47]\n");

	Con->Open (pHost, pPort);
	
	msg = new char[64];
	while ((Con->Connected == ST_CONNECTING)&&(failures++<96)&&(!Parent->ToCollector->UrgentPending))
	{
		JOBQ.Progress (jid, 18+failures);
        Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		dprintf ("[clue fails=%i]\n", failures);
		Con->Handle (msg, 63); // Wait till it's *really* open
	}
	delete[] (msg);
	if (failures >95)
	{
		iprintf ("[pop3handler] Timeout during Connection::Open()\n");
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "pop3handler", "Connection to POP3 server timed out.");
		JOBQ.EndJob (jid);
		return;
	}

	JOBQ.Progress (jid, 50);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	if (Parent->ToCollector->UrgentPending)
	{
		JOBQ.EndJob (jid);
		return;
	}
	
	// Verify connection

	if (Con->Connected == ST_CONNECTED) // Send state information
	{
		JOBQ.Stat (jid, "Connection established");
        Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		//Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Connection established");
	}
	else // Connection failed, bail out with an annoying error.
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
								   "pop3handler", "Could not connect to POP3 server, try smoke signals.");
		JOBQ.EndJob (jid);
		return;
	}

	msg = new char[255];
	msg[0] = 0;

	if (CheckReply (msg, 255)) // POP3 servers say hello first
	{
		iprintf ("[pop3handler] < %s", msg);
		sprintf (msg, "USER %s\n", Parameters->Get_char ("user"));
		iprintf ("[pop3handler] > %s", msg);
		Con->Send (msg, 10); // Send username, quelle surprise.
		
		if (CheckReply (msg, 255))
		{
			iprintf ("[pop3handler] < %s", msg);
			if (Parent->ToCollector->UrgentPending) return;
			JOBQ.Progress (jid, 75);
			JOBQ.Stat (jid, "Sending Password");
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

			sprintf (msg, "PASS %s\n", Parameters->Get_char ("pass"));
			iprintf ("[pop3handler] > PASS *\n");
			Con->Send (msg, 10); // Send password.
			if (CheckReply (msg, 255))
			{
				JOBQ.Progress (jid, 100);
				JOBQ.Stat (jid, "Authentication succeeded");
				Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
				ConnectionOpen = 1; // The eagle has landed.
			}
		}
		else
		{
			iprintf ("[pop3handler] < %s", msg);
			iprintf ("[pop3handler] Transaction aborted\n");
		}
	}
   	else
	{
		iprintf ("[pop3handler] < %s", msg);
		iprintf ("[pop3handler] Transaction aborted\n");
	}
	if (!ConnectionOpen) // If it's not open, it must be closed.
	{
		Con->Close(); // Confirm this narrow view of life.
	}
	
	JOBQ.EndJob (jid);

	delete[] (msg); // Clean up the mess
	HighestHandled = 0; // Make a fresh start
}

void POP3Handler::CloseConnection (void)
{
	char msg[64];
	
	iprintf ("[pop3handler] Connection::Close()\n");
	if (ConnectionOpen)
	{
		Con->SendF (10, "QUIT\n");
		CheckReply (msg, 63);
	}
	Con->Close();
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "Connection closed");
	ConnectionOpen = 0;
}

//====================================================================================================
// Internal handler to take care of pop3 protocol replies. In case of an error reply it automatically
// takes care of informing the UI thread.
//====================================================================================================

int POP3Handler::CheckReply (char *buf, int len)
{
	int res;
	buf[0] = 0;

	while (((res = Con->Handle (buf, len)) == 0) && (!Parent->ToCollector->UrgentPending));
	if (Parent->ToCollector->UrgentPending) return 0;
	if (res < 0) eprintf ("connection error\n");
	//if (buf[0]) printf ("<<< %s", buf);
	if (buf[0] == '+') return 1;
	else
	{
		if (buf[0] == '-') // Some kind of oddbals protocol error, let the user figure it out.
		{
			choplf (buf);
			char *errorstring = new char[strlen (buf+1) + 96];
			sprintf (errorstring, "I wanted it to zig, and the POPserver zagged. \"Behold,\" he "
								  "whispered, \"%s\".", buf+1);
			Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "pop3handler", errorstring);
			delete[] (errorstring);
		}
	}
	return 0;
}

//====================================================================================================
// POP3 has no methods to check for a particular group. The groupname argument is therefor lost
// in the line noise. Unfortunately POP3 numbers messages in a mailbox in quite an unsafe way,
// which means we need to keep track of a temporary translation table from MsgIds to each message
// in the mailbox. This is kludged together by keeping a seperate table mapping UIDs to message-
// ids, so that between sessions the first table can be reliably rebuilt.
//====================================================================================================

void POP3Handler::CheckGroup (const char *groupname)
{
	char 		 msg[64];
	int  		 numberofmessages;
	AsyncBuffer	 msgbuffer(4096);
	char		 buf[256];
	char		 msgid[128];
	int			 done = 0;
	int			 i,j,k;
	int			 fetchbodies = 0;
	int			 deletemail = 0;
	WordList	 UIDL;
	char		*tmp;
	Var			*nUIMsgID;
	ITCMessage	*M;

	int			start;
	
	msgid[0] = 0;

	iprintf ("[pop3handler] CheckGroup (%s)\n", groupname);

	if (!ConnectionOpen) OpenConnection(); // Open the connection on demand
	if (Parent->ToCollector->UrgentPending) return;
	
	if (ConnectionOpen) // Verify the connection is now really open
	{
		int jid = JOBQ.NewJob (Parent->Name);
		JOBQ.Stat (jid, "Peeking at pop3 box");
		JOBQ.Progress (jid, 5);
        Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

		Con->SendF (10, "STAT\n"); // Determine the amount of messages waiting for us
		iprintf ("[pop3handler] > STAT\n");
		if (CheckReply (msg, 63))
		{

			iprintf ("[pop3handler] < %s", msg);
			if (Parent->ToCollector->UrgentPending) return;
			numberofmessages = atoi (msg+4);
			//if (numberofmessages>32) numberofmessages=32;

			sprintf (buf, "%i messages on pop3 server", numberofmessages);
			JOBQ.Progress (jid, 10);
			JOBQ.Stat (jid, buf);
            Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
			
			Con->SendF (10, "UIDL\n");
			iprintf ("[pop3handler] > UIDL\n");
			if (CheckReply (msg, 63))
			{
				JOBQ.Progress (jid, 15);
                Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

				iprintf ("[pop3handler] < %s", msg);
				if (Parent->ToCollector->UrgentPending) return;
				for (i=0;i<numberofmessages;++i)
				{
					JOBQ.Progress (jid, 15 + ((20*i)/numberofmessages));
                    if (JOBQ.Changed (jid)) Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

					buf[0] = 0;
					while ((j = Con->Handle (buf, 255)) == 0);
					if (j<0)
					{
						ConnectionOpen = 0;
						Con->Close();
						Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "pop3handler", "peer closed my connection!");
						JOBQ.EndJob (jid);
						return;
					}
					choplf (buf);
					if ((tmp = strchr (buf, ' ')))
					{
						UIDL.Add (tmp+1);
					}
				}
				while ((j = Con->Handle (buf, 255)) == 0);
			}
			else
			{
				iprintf ("[pop3handler] < %s", msg);
				iprintf ("[pop3handler] Transaction possibly aborted\n");
			}
			JOBQ.Progress (jid, 35);

			if (Parameters->Get_int ("collection") > 0) fetchbodies = 1;
			if (Parameters->Get_int ("collection") > 1) deletemail = 1;

			//if (HighestHandled) start = HighestHandled-1;
			start = LastChecked;
			//start = 0; // else start = 0;

			nUIMsgID = new Var;
			
			for (k=0;k<start;++k)
			{
				tmp = UIMsgID->Get_char (UIDL.Get(k));
				nUIMsgID->Set_char (UIDL.Get(k), tmp);
			}
			for (k=start;(k<numberofmessages)&&(!Parent->ToCollector->UrgentPending);k+=16) // Split into groups of 16
			{
				for (i=k;(i<numberofmessages)&&(i-k < 16)&&(!Parent->ToCollector->UrgentPending);++i) // Queue up RETR/TOP commands (breakfast serials are served)
				{
					JOBQ.Progress (jid, 35 + ((60 * i)/numberofmessages));
                    if (JOBQ.Changed (jid)) Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
					
					if (strlen (tmp = UIMsgID->Get_char (UIDL.Get(i))) > 0)
					{
						LastChecked = i;
						nUIMsgID->Set_char (UIDL.Get(i), tmp);
						TransMsgID->Set_int (tmp, i+1);
						if (!Parent->Have (tmp))
						{
							sprintf (buf, "Checking %s", tmp);
							JOBQ.Stat (jid, buf);
							Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
							
							UIMsgID->Delete (UIDL.Get(i));
							if (fetchbodies) sprintf (msg, "RETR %i\n", i+1);
							else sprintf (msg, "TOP %i 0\n", i+1);
							iprintf ("[pop3handler] > %s", msg);
							Con->Send (msg, 10);
						}
						if (Parent->ToCollector->UrgentPending) i = k = numberofmessages;
					}
					else
					{
						Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
						
						if (fetchbodies) sprintf (msg, "RETR %i\n", i+1);
						else sprintf (msg, "TOP %i 0\n", i+1);
						Con->Send (msg, 10);
						iprintf ("[pop3handler] > %s", msg);
					}
					
					//printf ("cmmd %i\n", i);
				}
				for (i=k;(i<numberofmessages)&&(i-k < 16)&&(!Parent->ToCollector->UrgentPending);++i) // Iterate through each sent RETR command
				{
					JOBQ.Progress (jid, 36 + ((60 * i)/numberofmessages));
                    if (JOBQ.Changed (jid)) Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
					if (strlen (tmp = UIMsgID->Get_char (UIDL.Get(i))) == 0)
					{			
						if (CheckReply (msg, 63)) // See if it generated a positive reply
						{
							sprintf (buf, "processing message %i", i);
							Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, buf);
							
							msgid[0] = 0; // Clean msgid
							
							done = 0;
							msgbuffer.Pos = 0;
							
							while (!done) // For each line in the message
							{
								buf[0] = 0;
								if (Con->Handle (buf, 255) < 0) // Make sure the connection is still alive
								{
									done = 1;
									Con->Close();
									ConnectionOpen = 0;
									Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
															   "pop3handler",
															   "There I was, collecting mail, minding my own business, and this punkass "
															   "POP3 daemon started acting like a pain, dropping connections all over.");
								}
								else
								{
									if (buf[0]) // See if we received actual data
									{
										if ((buf[0]=='.') && (buf[1] < 32)) done = 1; // End of message reached
										else
										{
											choplf (buf);
											strcat (buf, "\n");
											msgbuffer.Add (buf);
											if (!msgid[0]) // Check if we already have the msgid
											{
												if (wild_match ("Message-ID:*", buf)) // See of this is the message-id
												{
													char *tmp;
													
													choplf (buf);

													// Seperate value from headername

													tmp = strchr (buf,':');
													if (*tmp) ++tmp;
													while ( (*tmp==' ') || (*tmp=='\t') ) ++tmp;

													// copy over msgid
													
													strncpy (msgid, tmp, 127);
													msgid[127] = 0;

													// store the msgid in the translation table
													
													TransMsgID->Set_int (msgid, i+1);
												}
											}
										}
									}
								}
							}
							if (ConnectionOpen) // Only interested in completely transfered messages thank you
							{
								if (strlen (msgid) < 1) // In the odd case there is no msgid we'll have to make one up
								{
									sprintf (msgid, "<POP3_%i-%08d@%s>", (unsigned int) time(NULL), i, Parameters->Get_char ("host"));
																			   // Of course this is inherently unstable and
																			   // should not happen, but we need _some_
																			   // id for the message. Oh heck, any message
																			   // evil enough not to have a Message-ID set by
																			   // the server deserves to haunt the user,
																			   // begging him to LART the sender AND the
																			   // author of the mailerdaemon.
								}
								nUIMsgID->Set_char (UIDL.Get(i), msgid);
								if (! (Parent->Have (msgid))) // Make sure this isn't a double
								{
									sprintf (buf, "processing message %i msgid %s", i, msgid);
									Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, buf);
								
									MailMessage *X = new MailMessage (Parent, msgid);

									if (!msgbuffer.Data)
									{
										eprintf ("!!!!!!!!!!!!!!!!!!!!!!!!! Assert failed: msgbuffer.Data\n");
									}
									else
									{
										msgbuffer.Add ((unsigned char) 0);
										if (fetchbodies) X->ProcessMessage ((char *) msgbuffer.Data);
										else X->ProcessHeaders ((char *) msgbuffer.Data);
									}
									if (deletemail) X->Flags |= MMFL_ORIGINAL; // If we're going to delete the mail from the
																			   // server it's essential to turn on the preservation
																			   // bits to prevent the message from getting flushed away.
									
									Parent->Deliver (X);  // And now it's safely out of our hands.
									Parent->Lock->LockRW();
									for (j=0;j<X->Subscribers->Count();++j)
									{
										ViewListNode *VLN = X->Subscribers->Get(j);
										if (VLN) Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, VLN->View->Name, X->MsgID);
									}
									Parent->Lock->UnlockRW();
									HighestHandled = i+1; // Save session state
									msgbuffer.Pos = 0;
									msgid[0] = 0;
								}
							}
						}
						else
						{
							eprintf ("error: %s\n", msg);
						}
						if (!ConnectionOpen) i = numberofmessages; // In case things messed up, bail out
						msgbuffer.Pos = 0; // Clear the buffer
					}
				}
				if ( (Parent->ToCollector->PriorityPending) &&
				     ((M = Parent->ToCollector->GetNextMessage (CLASS_GETBODY))) )
				{
					GetBody (M->Message->Get(0));
					Parent->ToUI->PostMessage (CLASS_GOTBODY, NO_SUBCLASS, M->Message->Get(0));
					delete (M);
				}
			}

			delete (UIMsgID);
			UIMsgID = nUIMsgID;

			// Save configuration in backup file

			Parent->SaveMailBox();
			sprintf (buf, "%s/uidl.dat.new", Parent->DirectoryPrefix);
			UIMsgID->Save (buf);
			
			// Atomically replace the original file
			
			char buf2[256];
			sprintf (buf2, "%s/uidl.dat", Parent->DirectoryPrefix);
			rename (buf, buf2);
			
			if (fetchbodies && deletemail) // Delete mail from server?
			{
				if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y")) // Make sure write protect isn't on
				{
					for (i=start; (i<HighestHandled)&&(!Parent->ToCollector->UrgentPending); ++i) // Queue up the DELE commands
					{
						Con->SendF (10, "DELE %i\n", i+1);
					}
					for (i=start; (i<HighestHandled)&&(!Parent->ToCollector->UrgentPending); ++i) // And verify confirmation
					{
						if (CheckReply (msg, 63) < 0) // Connection-related error
						{
							i = HighestHandled; // Bail out and stuff
							ConnectionOpen = 0;
							Con->Close();
						}
						
					}
					Con->SendF (10, "QUIT\n");
					CheckReply (msg, 63);
					Con->Close();
					ConnectionOpen = 0;
				}
			}
		}
		JOBQ.EndJob (jid);
	}
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

void POP3Handler::GetBody (char *msgid)
{
	MailMessage 	*X = Parent->Find (msgid);
	char			 msg[512];
	int				 done;
	int				 headersdone;
	int				 result;
	int				 xfer = 0;
	int				 oldxfer = 0;
	AsyncBuffer		 msgbuffer(3072);

	if (X) // If the msgid could be found...
	{
		if (!(X->Flags & MMFL_HAVEBODY)) // And is not pointing to an already downloaded message...
		{
			if (!ConnectionOpen) // Open the connection as needed
			{
				OpenConnection();
				if (ConnectionOpen) CheckGroup (""); // And update the translation table (Ireland awards one Kludge point).
			}
			if (ConnectionOpen) // Check to see if the connection is really open now
			{
				int jid = JOBQ.NewJob (Parent->Name);
				int popid = TransMsgID->Get_int (msgid); // Lookup in translation table
				if (popid)
				{
					sprintf (msg, "Fetching POP message %i", popid);
					JOBQ.Stat (jid, msg);
					Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
					
					sprintf (msg, "RETR %i\n", popid);
					Con->Send (msg, 10); // Tell the POP3 server to tango
					msg[0] = 0;
					if (CheckReply (msg, 511)) 			 // And wait for its stern lead
					{
						int totalsize = 0;
						if (strchr (msg, ' '))
						{
							totalsize = atoi (strchr (msg, ' ') +1);
						}
						done = headersdone = 0; // Reaffirm our position in the Time Space Continuum
						while (!done)
						{
							msg[0] = 0;
							if ( (result = Con->Handle (msg, 511)) < 0 ) // Bad mojo in the connection handler
							{
								ConnectionOpen = 0; // Burn our bridges
								Con->Close();
								
								Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, // And whine to the user
														   "pop3handler",
														   "Server disconnected during body retrieval. That bastard.");
								done = 1;
								headersdone = 0;
							}
							if (result > 0) // Runes were scribed by the server
							{
								if (headersdone) // Are we processing real data yet?
								{
									if ((msg[0]=='.')&&(msg[1]<32)) done = 1; // Beware of the Death Dot
									else
									{
										choplf (msg);
										msgbuffer.Add (msg); // Or feed the force
										msgbuffer.Add ("\n");
									}
									//if (msg[0]) printf ("<<< %s", msg);
								}
								else if ((msg[0]=='\n')||(msg[0]=='\r')) // Nope, is there an end to the header misery yet?
								{
									headersdone = 1;
								}
								xfer += strlen (msg);
								if (xfer - oldxfer > 256)
								{
									oldxfer = xfer;
									sprintf (msg, "Fetching POP message %i: %d bytes", popid, xfer);
									JOBQ.Stat (jid, msg);
									JOBQ.XFer (jid, xfer);
									if (totalsize) JOBQ.Progress (jid, (100*xfer)/totalsize);
									Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
								}
								msg[0] = 0;
								//else if (msg[0]) printf ("<H> (%i) %s", strlen (msg), msg);
							}
							if (Parent->ToCollector->UrgentPending) done=1;
						}
						if ((!Parent->ToCollector->UrgentPending) && (ConnectionOpen)) // Did we survive El Tango del Muerte?
						{
							msgbuffer.Add ((unsigned char) 0);
							X->ProcessBody ((char *) msgbuffer.Data); // Let the message absorb the data
							X->StoreFile();
							Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
						}
						else if (!Parent->ToCollector->UrgentPending)
						{
							Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, // Complain
													   "pop3handler",
													   "That B*TCH closed the connection!");
						}
					} else dprintf ("fail 3: error reply: %s\n", msg);
				}
				JOBQ.EndJob (jid);
			} else dprintf ("fail 2: connection closed\n");
		}
		else dprintf ("fail 1: already have body\n");
	}
	else // No such message in our database, let's moan a bit for old time's sake.
	{
		sprintf (msg, "Could not locate Message \"%s\" in internal database. You probably did not want to hear this.", msgid);
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "pop3handler", msg);
	}
}

void POP3Handler::DeleteMessage (char *msgid)
{
	int popid;		// Translated id number in the popbox
	char msg[63];

	if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y")) // Make sure write protect isn't on
	{
		if (!ConnectionOpen)
		{
			OpenConnection(); // Open connection on demand
			if (ConnectionOpen) CheckGroup ("*");
		}
		if (ConnectionOpen)
		{
			popid = TransMsgID->Get_int (msgid);
			dprintf ("popid = %i\n", popid);
			if (popid)
			{
				Con->SendF (10, "DELE %i\n", popid);
				sprintf (msg, "Deleting message %i", popid);
				Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, msg);
				dprintf ("%s\n", msg);
				
				if (CheckReply (msg, 63) < 0)
				{
					Con->Close();
					ConnectionOpen = 0;
				}
			}
		}
	}
	else eprintf ("[error] Write Protect\n");
}

void POP3Handler::Expire (void)
{
	time_t expire_onserver_received = 86400 * Parameters->Get_int ("expire_onserver_received");
	time_t expire_onserver_notreceived = 86400 * Parameters->Get_int ("expire_onserver_notreceived");
	Value *X;
	MailMessage *M;
	int count = 0;
	int popid = 0;
	int total_to_delete=0;
//	int total_deleted=0;
//	int i;
	char msg[256];
	WordList WH;
	
	if (!ConnectionOpen) OpenConnection();
	if (ConnectionOpen)
	{
		CheckGroup ("blah");
		X = UIMsgID->First;
		total_to_delete = (UIMsgID->Count - 500);
		dprintf ("%i messages, deleting %i\n", UIMsgID->Count, total_to_delete);
		while ((X) && (total_to_delete > 0))
		{
			if (WH.Find (X->Get_char()) < 0)
			{
				WH.Add (X->Get_char());
				M = Parent->Find (X->Get_char());
				if (M)
				{
					if ( (expire_onserver_received>0) &&
					     (M->Flags & MMFL_HAVEBODY) && 
					     ((time(NULL) - M->ReceiveDate) > expire_onserver_received))
					{
						if ((popid = TransMsgID->Get_int (M->MsgID)))
						{
							++count;
							sprintf (msg, "Expiring %i %s", popid, M->MsgID);
							Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, msg);
							
							Con->SendF (10, "DELE %i\n", popid);
							dprintf ("DELE %i\n", popid);
							if (count == 16)
							{
								for (count=0;count<16;++count)
								{
									if (CheckReply (msg, 255) < 0)
									{
										Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "pop3handler", "got hung up during expiry phase");
										return;
									}
									--total_to_delete;
								}
								count = 0;
							}
						}
					}
					else if ((expire_onserver_notreceived>0) && (!(M->Flags & MMFL_HAVEBODY)) && ( (time(NULL) - M->ReceiveDate) > expire_onserver_notreceived))
					{
						if ((popid = TransMsgID->Get_int (M->MsgID)))
						{
							++count;
							sprintf (msg, "Expiring (unread) %i %s", popid, M->MsgID);
							Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, msg);
							
							Con->SendF (10, "DELE %i\n", popid);
							dprintf ("DELE %i\n", popid);
							if (count == 16)
							{
								for (count=0;count<16;++count)
								{
									if (CheckReply (msg, 255) < 0)
									{
										Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "pop3handler", "got hung up during expiry phase");
										return;
									}
									--total_to_delete;
								}
								count = 0;
							}
							Parent->RemoveMessage (M->MsgID);
						}
					}
				}
			}
			X = X->Next;
		}
		while (count)
		{
			if (CheckReply (msg, 255) < 0)
			{
				Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "pop3handler", "got hung up during expiry phase");
				return;
			}
			--count;			
		}
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "closing connection");
		//int popid = TransMsgID->Get_int (msgid);
		
		LastChecked = 0;
		
		Con->SendF (10, "QUIT\n");
		CheckReply (msg, 63);
		CloseConnection();
	}
}

int POP3Handler::PostMessage (WordList */*r*/, MailMessage */*m*/)
{
	dprintf ("[pop3handler] postmessage not implemented\n");
	return 0;
}
