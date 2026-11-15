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
#include "eaddr.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "debug.h"
#include "status.h"

extern JobList JOBQ;

NNTPHandler::NNTPHandler (MailBox *_Parent, class Var *_Parameters)
{
	Parameters = new Var (_Parameters);
	Parent = _Parent;
	Con = new Connection (1);
	ConnectionOpen = 0;
	Parent->ToCollector->AddPriorityClass (CLASS_GETBODY);
}

NNTPHandler::~NNTPHandler (void)
{
	if (ConnectionOpen) Con->Close();
	delete (Con);
	delete (Parameters);
}

void NNTPHandler::OpenConnection (void)
{
	char *msg;
	char *pHost = Parameters->Get_char ("host");
	int   pPort = Parameters->Get_int ("port");
	int	  failures = 0;

	if (!ConnectionOpen)
	{
		if (pPort == 0) pPort = 119;
		if ( (strlen (pHost) == 0) || (pPort == 0) )
		{
			Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
									   "nntphandler", "Invalid Hostname or Port in preferences. You blew it.");
			return;
		}

		int jid = JOBQ.NewJob (Parent->Name);
		
		msg = new char[ strlen (pHost) + 32];
		sprintf (msg, "Contacting host %s", pHost);
		JOBQ.Stat (jid, msg);
		JOBQ.Progress (jid, 10);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		delete[] (msg);	

		Con->Open (pHost, pPort);

		msg = new char[64];
		while ((Con->Connected == ST_CONNECTING)&&(failures++<96))
		{
			Con->Handle (msg, 63); // Wait till it's *really* open
			JOBQ.Progress (jid, 10+(0.6*failures));
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		}
		delete[] (msg);
		if (failures >95)
		{
			Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "nntphandler", "Connection to NNTP server timed out.");
			JOBQ.EndJob (jid);
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
			return;
		}

		if (Con->Connected == ST_CONNECTED) // Send state information
		{
			JOBQ.Progress (jid, 75);
			JOBQ.Stat (jid, "Connection established");
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
			ConnectionOpen = 1;
		}
		else // Connection failed, bail out with an annoying error.
		{
			Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL,
									   "nntphandler", "Could not connect to NNTP server, try smoke signals.");
			JOBQ.EndJob (jid);
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
			return;
		}
		CheckReply (200, 201);
		JOBQ.EndJob (jid);
		Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
	}
}

void NNTPHandler::CloseConnection (void)
{

	if (Con)
	{
		dprintf ("[closeconnection()]\n");
		if (ConnectionOpen)
		{
			Con->SendF (10, "QUIT\n");
			CheckReply (205);
		}
		Con->Close();
		ConnectionOpen = 0;
	}
}

void NNTPHandler::GetBody (char *msgid)
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
			}
			if (ConnectionOpen) // Check to see if the connection is really open now
			{
				int jid = JOBQ.NewJob (Parent->Name);

				sprintf (msg, "Fetching NNTP message %s", msgid);
				JOBQ.Stat (jid, msg);
				JOBQ.Progress (jid, 1);
                Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

				sprintf (msg, "ARTICLE %s\n", msgid);
				Con->Send (msg, 10); // Tell the POP3 server to tango
				msg[0] = 0;
				if (CheckReply (220))
				{
					JOBQ.Progress (jid, 5);
					JOBQ.XFer (jid, 1);
                    Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
					done = 0; // Reaffirm our position in the Time Space Continuum
					int lines = 0;
					int tlines = 0;
					while (!done)
					{
						if (Parent->ToCollector->UrgentPending) done=1;
						msg[0] = 0;
						if ( (result = Con->Handle (msg, 511)) < 0 ) // Bad mojo in the connection handler
						{
							dprintf ("[conerror1]\n");
							ConnectionOpen = 0; // Burn our bridges
							Con->Close();

							Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, // And whine to the user
													   "nntphandler",
													   "Server disconnected during body retrieval. That bastard.");
							done = 1;
							headersdone = 0;
						}
						if (result > 0) // Runes were scribed by the server
						{
							if ((msg[0]=='.')&&(msg[1]<32)) done = 1; // Beware of the Death Dot
							else
							{
								choplf (msg);
								if (!tlines)
								{
									if (wild_match ("Lines:*", msg))
									{
										char *tmp = strchr (msg, ':');
										while ((*tmp==':')||(*tmp<=' ')) ++ tmp;
										tlines = atoi (tmp);
									}
								}
								msgbuffer.Add (msg); // Or feed the force
								msgbuffer.Add ("\n");
								if (tlines) ++lines;
							}
							xfer += strlen (msg);
							if (xfer - oldxfer > 256)
							{
								oldxfer = xfer;
								if (tlines) JOBQ.Progress (jid, 15 + ((80*lines)/tlines));
								JOBQ.XFer (jid, xfer);
								sprintf (msg, "Fetch: %d bytes", xfer);
								JOBQ.Stat (jid, msg);
                                Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
							}
							msg[0] = 0;
						}
					}
				}	
				if ((ConnectionOpen)&&(!Parent->ToCollector->UrgentPending)) // Did we survive El Tango del Muerte?
				{
					msgbuffer.Add ((unsigned char) 0);
					X->ProcessMessage ((char *) msgbuffer.Data); // Let the message absorb the data
					X->StoreFile();
				}
				else if (!ConnectionOpen)
				{
					Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, // Complain
											   "nntphandler",
											   "That B*TCH closed the connection!");
				}
				JOBQ.EndJob (jid);
                Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
			}
		}
	}
}

void NNTPHandler::CheckGroup (char *groupname)
{
//	char 		 msg[128];
	int  		 numberofmessages;
	AsyncBuffer	 msgbuffer(4096);
	char		 buf[1024];
	char		 msgid[256];
	int			 done = 0;
	int			 /*i,*/j,k,art;
	int			 fetchbodies = 0;
//	int			 deletemail = 0;
	WordList	 ST;
	WordList	 MID;
//	char		*tmp;
	int			 gr_start;
	int			 gr_end;
	Var			 XOverFmt;
	WordList	 Tmp;
	
	int			 xmsgid,xfrom,xsubject,xdate, xrefs;
	char		*xnamefrom, *xemailfrom;

	WordList	 xMID;

//	int			start;
	ITCMessage *M;
	
	msgid[0] = 0;

	if (Parameters->Get_int ("collection") > 0) fetchbodies = 1;
	//if (Parameters->Get_int ("collection") > 1) deletemail = 1;

	if (!ConnectionOpen) OpenConnection(); // Open the connection on demand

	if (ConnectionOpen) // Verify the connection is now really open
	{
		if (Parent->ToCollector->UrgentPending) return;

		int jid = JOBQ.NewJob (Parent->Name);
		JOBQ.Stat (jid,"Opening Group");
		JOBQ.Progress (jid,1);
		Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, "");

		dprintf ("GROUP %s\n", groupname);
		Con->SendF (10, "GROUP %s\n", groupname); // Determine the amount of messages waiting for us
		if (CheckReply (211, buf, 255))
		{
			JOBQ.Progress (jid,3);
            Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, "");
			if (Parent->ToCollector->UrgentPending)
			{
				JOBQ.EndJob (jid);
                Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, "");
				return;
			}
			ST.MakeGeneric (buf, ' ');
			gr_start = ST.GetInt (2);
			gr_end = ST.GetInt (3);
			if ((gr_end - gr_start) > 500) gr_start = gr_end - 500;
			numberofmessages = 0;
			
			Con->SendF (10, "LIST OVERVIEW.FMT\n");
			if (CheckReply (215))
			{
				JOBQ.Progress (jid,4);
                Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, "");
				if (Parent->ToCollector->UrgentPending) return;
				done = 0; k = 0;
				while (!done)
				{
					buf[0] = 0;
					while ((j = Con->Handle (buf, 1023)) == 0);
					if (j<0)
					{
						dprintf ("[conerror2]\n");
						sprintf (buf, "Connection error: %s", strerror (errno));
						ConnectionOpen = 0;
						Con->Close();
						Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "nntphandler", buf);
						JOBQ.EndJob (jid);
                        Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, "");
						return;
					}
					choplf (buf);
					if ((buf[0] == '.')&&(buf[1]<32)) done = 1;
					else
					{
						MID.MakeGeneric (buf, ':');
						++k;
						XOverFmt.Set_int (MID.Get(0), k);
					}
				}
			}
			JOBQ.Progress (jid,5);
            Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, "");
			xmsgid = XOverFmt.Get_int ("Message-ID");
			xsubject = XOverFmt.Get_int ("Subject");
			xfrom = XOverFmt.Get_int ("From");
			xdate = XOverFmt.Get_int ("Date");
			xrefs = XOverFmt.Get_int ("References");

			int rounddone = 0;

			for (art=gr_start; art<gr_end;art+=32)
			{
				rounddone = 0;
				dprintf ("XOVER %i-%i\n", art, (art+31) > gr_end ? gr_end : art+31);
				Con->SendF (10, "XOVER %i-%i\n", art, (art+31) > gr_end ? gr_end : art+31);
				if (CheckReply (224))
				{
					//JOBQ.Progress (jid, ((80*(art-gr_start))/(gr_end+31-gr_start);
                    //Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, "");

					done = 0;
					while ((!done) && (gr_end > gr_start))
					{
						buf[0] = 0;
						while (((j = Con->Handle (buf, 1023)) == 0) || (j < 0 && (errno == EINTR || errno == EINPROGRESS || errno == EAGAIN)));
						if (j<0)
						{
							dprintf ("[conerror3]\n");
							sprintf (buf, "Connection error: %s", strerror (errno));
							ConnectionOpen = 0;
							Con->Close();
							Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "nntphandler", buf);
							JOBQ.EndJob (jid);
							Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, "");
							return;
						}
						choplf (buf);
						if ((buf[0] == '.')&&(buf[1]<32)) done = 1;
						else
						{
							++rounddone;
							MID.MakeGeneric (buf, '\t');
							xnamefrom = eaddr2name (MID.Get (xfrom));
							xemailfrom = eaddr2addr (MID.Get (xfrom));
							if (!Parent->Have (MID.Get (xmsgid)))
							{
								MailMessage *M = new MailMessage (Parent, MID.Get (xmsgid));
								M->Envelope->Set (ENVELOPE_NAMEFROM, xnamefrom);
								M->Envelope->Set (ENVELOPE_MAILFROM, xemailfrom);
								M->Envelope->Set (ENVELOPE_SUBJECT, MID.Get (xsubject));
								M->Envelope->Set (ENVELOPE_DATE, MID.Get (xdate));
								M->Envelope->Set (ENVELOPE_GROUPS, groupname);
								Tmp.MakeGeneric (MID.Get (xrefs), ' ');
								if (Tmp.Count) M->Envelope->Set (ENVELOPE_I_PARENTID, Tmp.Get (Tmp.Count-1));
								
								JOBQ.Progress (jid, 20+((70*numberofmessages)/((gr_end-gr_start)+1)));

								if ((++numberofmessages & 0x07) == 0)
								{
									sprintf (buf, "Processing message header (%i/%i) %s", numberofmessages, gr_end-gr_start, MID.Get (xmsgid));
									JOBQ.Stat (jid, buf);
								}
								
								if (JOBQ.Changed (jid)) Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

								Parent->Deliver (M);
								Parent->Lock->LockRW();
								for (j=0;j<M->Subscribers->Count();++j)
								{
									ViewListNode *VLN = M->Subscribers->Get(j);
									if (VLN) Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, VLN->View->Name, M->MsgID);
								}
								Parent->Lock->UnlockRW();

								if (fetchbodies) xMID.Add (MID.Get (xmsgid));
								delete[] xnamefrom;
								delete[] xemailfrom;
							}
							else
							{
								++numberofmessages;
								if ((numberofmessages & 0x07) == 0)
								JOBQ.Progress (jid, 20+((75*numberofmessages)/((gr_end-gr_start)+1)));
								{
									sprintf (buf, "Processing message header (%i/%i)", numberofmessages, gr_end-gr_start);
									JOBQ.Stat (jid, buf);
									Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
								}
							}
						}
					}
				}
				if (Parent->ToCollector->UrgentPending)
				{
					art = gr_end;
					M = NULL;
					//Parent->ToCollector->PostMessage (CLASS_CHECKGROUP, groupname, art);
				}
				if ( (Parent->ToCollector->PriorityPending) && 
				     ((M = Parent->ToCollector->GetNextMessage (CLASS_GETBODY))) )
				{
					GetBody (M->Message->Get(0));
					Parent->ToUI->PostMessage (CLASS_GOTBODY, NO_SUBCLASS, M->Message->Get(0));
					delete (M);
				}
			}
		}
		JOBQ.EndJob (jid);
	}
	if (fetchbodies)
	{
		int jid = JOBQ.NewJob (Parent->Name);
		JOBQ.Stat (jid, "Download bodies");
        Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

		for (int ij=0; (ij<xMID.Count)&&(!Parent->ToCollector->UrgentPending);++ij)
		{
			JOBQ.Progress (jid, (100*ij)/(xMID.Count + 1));
            if (JOBQ.Changed (jid)) Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

			GetBody (xMID.Get(ij));
			if ( (Parent->ToCollector->PriorityPending) && 
			     ((M = Parent->ToCollector->GetNextMessage (CLASS_GETBODY))) )
			{
				GetBody (M->Message->Get(0));
				Parent->ToUI->PostMessage (CLASS_GOTBODY, NO_SUBCLASS, M->Message->Get(0));
				delete (M);
			}
		}
		JOBQ.EndJob (jid);
	}
	//Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

void NNTPHandler::DeleteMessage (char *msgid)
{
}

void NNTPHandler::Expire (void)
{
}

int NNTPHandler::CheckReply (int ecode, char *buf, int bsize, int alternative)
{
	int j;
	int res;
	
	//printf ("checkreply %i\n", ecode);
	
	while ((j = Con->Handle (buf, 255)) == 0);
	if (j<0)
	{
		sprintf (buf, "Connection error: %s", strerror (errno));
		ConnectionOpen = 0;
		dprintf ("[conerror4]\n");
		Con->Close();
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "nntphandler", buf);
		return 0;
	}
	dprintf ("%s\n", buf);
	res = atoi (buf);
	if ((res  == ecode) || (res == alternative)) return 1;
	eprintf ("unexpected reply: %s\n", buf);
	if ((res > 200)&&(res < 300))
	{
		res = 0;
		while (!res)
		{
			while ((j = Con->Handle (buf, 255)) == 0);
			if (j<0)
			{
				sprintf (buf, "Connection error: %s", strerror (errno));
				ConnectionOpen = 0;
				dprintf ("[conerror5]\n");
				Con->Close();
				Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, buf);
				return 0;
			}
			if ((buf[0] == '.')&&(buf[1] < 32)) res = 1;
		}
	}
	return 0;
}

int NNTPHandler::CheckReply (int ecode, int alternative)
{
	char buf[256];
	return CheckReply (ecode, buf, 255, alternative);
}

int NNTPHandler::PostMessage (WordList *groups, MailMessage *M)
{
	char buf[255];
	int i;
	if (!(M->Flags && MMFL_INRAM)) M->LoadBody();
	MailPart *P = M->GetPart (0);
	dprintf ("nntp post P=%p\n", (void *) P);
	if (P)
	{
		if (!ConnectionOpen)
		{
			OpenConnection();
		}
		if (ConnectionOpen)
		{
			Con->SendF (10, "POST\n");
			if (CheckReply (340, buf, 255))
			{
				for (i=0;i<M->HeaderNames->Count;++i)
				{
					Con->SendF (10, "%s: %s\n", M->HeaderNames->Get(i), M->HeaderValues->Get(i));
				}
				Con->SendF (10, "\n");
				Con->Send (P->Contents, 10);
				Con->SendF (10, "\n.\n");
				if (CheckReply (240, buf, 255))
				{
					return 1;
				}
			}
		}
	}
	return 0;
}
