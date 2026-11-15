// ===============================================================================
// POSTOFFICE - X11 Office Communication Handler
// ---------------------------------------------
// All sourcecode, unless differently annotated, written by Pim van Riezen
// Sourcecode and resulting binaries are covered by the GNU Lesser General
// Public
// License (GNU LGPL). For more information and the literal text of the
// license
// agreement, check the Free Software Foundation's website on
// http://www.fsf.org/
// 
// Copyright (C) 1999 Mad Sciene Labs - Rotterdam, The Netherlands
// ===============================================================================


// ===============================================================================
// Right now, it does not reset the command number between connections.  
// Perhaps this could be changed so that command numbers are meaningful, if
// necessary.

#include "mailbox.h"
#include "var.h"
#include "reg.h"
#include "chop.h"
#include <stdio.h>
#include <stdlib.h>
#include <iostream.h>
#include <string.h>
#if defined (SOLARIS) || defined (IRIX)
  #include <strings.h>
#endif


#define DEBUG 1

IMAP4Handler::IMAP4Handler(MailBox * _Parent, Var * _Parameters)
{
  cout << "Making an imap handler" << endl;
  char
    buf[64];
  Parameters = new Var(_Parameters);
  Parent = _Parent;
  Con = new Connection(1);
  ConnectionOpen = 0;
  HighestHandled = 0;
  TransMsgID = new Var;
  UIMsgID = new Var;
  sprintf(buf, "%s/uids.dat", Parent->DirectoryPrefix);
  UIMsgID->Load(buf);

  CmdNum = 1;
  CmdPrefix = 'A';
}

IMAP4Handler::~IMAP4Handler(void)
{
  if (Con) {
    if (ConnectionOpen) {
      Con->Close();	// should I be checking the return stats??
      ConnectionOpen = 0;
    }
    delete(Con);
    Con = NULL;
  }
  delete(Parameters);
  delete(TransMsgID);
}


void
IMAP4Handler::OpenConnection(void)
{
  char           *msg;
  char           *pHost = Parameters->Get_char("host");
  int pPort = Parameters->Get_int("port");
  int failures = 0;

  cout << pHost << ":" << pPort << endl;
  if (pPort == 0)
    pPort = 143;

  // check to see if we're all straight in the head.

  if ((strlen(pHost) == 0) || (pPort == 0))	// is second compare
    // necessary?
    {
      Parent->ToUI->PostMessage(CLASS_ERROR, SUB_ERROR_CRITICAL,
				"IMAP4Handler",
				"Invalid hostname or port in preferences.");
      return;
    }
  // Send state info.
  cout << "Contacting host" << endl;
  msg = new char[strlen(pHost) + 32];
  sprintf(msg, "Contacting host %s", pHost);
  Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, Parent->Name,
			    msg);
  delete[](msg);

  // open connection

  Con->Open(pHost, pPort);
  cout << "opening connection" << endl;
  msg = new char[64];
  while ((Con->Connected == ST_CONNECTING) && (failures++ < 32)
	 && (!Parent->ToCollector->UrgentPending))
    Con->Handle(msg, 63);	// waiting until really open.  
  cout << msg << " " << failures << endl;
  delete[](msg);

  if (failures > 31) {
    Parent->ToUI->PostMessage(CLASS_ERROR, SUB_ERROR_CRITICAL,
			      "IMAP4Handler",
			      "Connection to IMAP4 server timed out.");
    cout << "failed" << endl;
    return;
  }

  if (Parent->ToCollector->UrgentPending)
    return;

  // Verify connection

  if (Con->Connected == ST_CONNECTED)	// send state info
    {
      Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, Parent->Name,
				"Connection established");
      cout << "connection established" << endl;
    } else {
      Parent->ToUI->PostMessage(CLASS_ERROR, SUB_ERROR_CRITICAL,
				"IMAP4Handler",
				"Could not connect to IMAP4 server, try again.");
      return;
    }

  msg = new char[255];
  msg[0] = 0;

  if (CheckReply(msg, 255))	// should respond with "*blah blah blah"
    {
      cout << msg << endl;
      sprintf(msg, "LOGIN %s %s", Parameters->Get_char("user"),
	      Parameters->Get_char("pass"));
      cout << msg << endl;
      SendCmd(msg);
      Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, Parent->Name,
				"Logging in");
      if (Parent->ToCollector->UrgentPending)
	return;

      if (CheckReply(msg, 255)) {
	Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS,
				  Parent->Name,
				  "Authentication accepted");
	cout << msg << "authentication accepted" << endl;
	ConnectionOpen = 1;
      }
    } else {
      cout << msg << endl;
    }
  if (!ConnectionOpen) {
    Con->Close();
  }

  delete[](msg);
}


void
IMAP4Handler::CloseConnection(void)
{
  if (ConnectionOpen) {
    SendCmd("LOGOUT");
    CheckReply();
    Con->Close();
    Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, Parent->Name,
			      "Connection closed");
    ConnectionOpen = 0;
  }
}

void
IMAP4Handler::GetBody(char *msgid)
{
  MailMessage    *X = Parent->Find(msgid);
  char msg[ 512 ], check[ 64 ];
  int done;
  int headersdone;
  int result;
  int xfer = 0;
  int oldxfer = 0;
  AsyncBuffer msgbuffer(3072);

  if (X)			// if msgid is found
    {
      if (!(X->Flags & MMFL_HAVEBODY))	// and is not pointing to an
	// already downloaded message
	{
	  if (!ConnectionOpen) {
	    OpenConnection();
	  }
	  if (ConnectionOpen) {
	    sprintf(msg, "Fetching IMAP message %s", msgid);
	    Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS,
				      Parent->Name, msg);


	    sprintf(msg, "SELECT INBOX");
	    SendCmd(msg);
	    msg[0] = 0;

	    if (CheckReply()) {
	      sprintf(msg, "FETCH %d (BODY[])",
		      UIMsgID->Get_int(msgid));
	      done = 0;

	      while (!done) {
		if (Parent->ToCollector->UrgentPending)
		  done = 1;
		msg[0] = 0;
		if ((result = Con->Handle(msg, 511)) < 0)	// error
		  {
		    ConnectionOpen = 0;
		    Con->Close();
		    Parent->ToUI->PostMessage(CLASS_ERROR,
					      SUB_ERROR_CRITICAL,
					      "IMAP4Handler",
					      "Server disconnected during message retrieval.");
		    done = 1;
		    headersdone = 0;
		  }
		if (result > 0) {

		  sprintf(check, "%d OK FETCH", CmdNum);
		  if (strncmp(msg, check, 10))
		    done = 1;
		  else {
		    choplf(msg);
		    msgbuffer.Add(msg);
		    msgbuffer.Add("\n");
		  }
		  if (xfer - oldxfer > 256) {
		    oldxfer = xfer;
		    sprintf(msg,
			    "Fetching imap message (%d bytes transfered",
			    xfer);
		    Parent->ToUI->PostMessage(CLASS_STATUS,
					      NO_SUBCLASS,
					      Parent->Name,
					      msg);
		  }
		  msg[0] = 0;
		}
	      }
	    }
	    if ((ConnectionOpen)
		&& (!Parent->ToCollector->UrgentPending)) {
	      msgbuffer.Add((unsigned char) 0);
	      X->ProcessMessage((char *) msgbuffer.Data);
	      X->StoreFile();
	      Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS,
					Parent->Name, "");
	    } else if (!ConnectionOpen) {
	      Parent->ToUI->PostMessage(CLASS_ERROR,
					SUB_ERROR_CRITICAL,
					"IMAP4Handler",
					"Connection closed by server");
	    }
	  }
	}
    }
}

void
IMAP4Handler::CheckGroup(char *groupname)
{

  char
    msg[64];
  int
    numberofmessages,
    numberofnew;
  AsyncBuffer msgbuffer(4096);
  char
    buf[256];
  char
    msgid[128];
  int
    done =
    0;
  int
    i,
    j,
    k;
  int
    fetchbodies =
    0;
  int
    deletemail =
    0;
  WordList UIDS;
  char           *
    tmp;
  Var            *
    nUIMsgID;

  int
    start;

  msgid[0] = 0;
  numberofmessages = 0;

  if (!ConnectionOpen)
    OpenConnection();	// Open the connection on demand
  if (Parent->ToCollector->UrgentPending)
    return;

  if (ConnectionOpen)		// Verify the connection is now really open
    {
      sprintf(msg, "SELECT %s", groupname);
      SendCmd(msg);		// Determine the amount of messages waiting
      // for us
      if (CheckReply(msg, 64)) {
	numberofmessages = atoi(msg + 2);
	cout << msg << ":" << numberofmessages << endl;
      }
      if (CheckReply(msg, 64)) {
	numberofnew = atoi(msg + 2);
	cout << msg << ":" << numberofnew << endl;
      }
      while ((j = Con->Handle(buf, 255)) == 0);
      if (numberofmessages > 0) {
	if (Parent->ToCollector->UrgentPending)
	  return;

	sprintf(buf, "%i messages on imap4 server", numberofmessages);
	Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS,
				  Parent->Name, buf);	// Let the UI 
	// 
	// 
	// 
	// thread
	// know
	// what's
	// going on

	sprintf(msg, "UID SEARCH 1:%d", numberofmessages);
	SendCmd(msg);
	int result = 0;
	do {
		result = CheckReply(msg, 63);
	} while ((strncmp (msg, "* SEAR", 6) != 0) && (result != 0));
	
	if (msg) {
	  UIDS.Make(msg);
	  UIDS.Delete(1);  // delete the junk that the server tosses on.
	  if (Parent->ToCollector->UrgentPending)
	    return;

	}

	if (Parameters->Get_int("collection") > 0)
	  fetchbodies = 1;
	if (Parameters->Get_int("collection") > 1)
	  deletemail = 1;

	// if (HighestHandled) start = HighestHandled-1;
	start = 0;		// else start = 0;

	nUIMsgID = new Var;

	for (k = start; (k < numberofmessages)
	       && (!Parent->ToCollector->UrgentPending); k += 16)	// Split 
	  // 
	  // 
	  // 
	  // into 
	  // groups 
	  // of 
	  // 16
	  {
	    for (i = k;
		 (i < numberofmessages) && (i - k < 16) && (!Parent->ToCollector->UrgentPending); ++i)	// Queue 
	      // 
	      // 
	      // 
	      // up 
	      // RETR/TOP 
	      // commands 
	      // (breakfast 
	      // serials 
	      // are 
	      // served)
	      {
		if (strlen(tmp = UIMsgID->Get_char(UIDS.Get(i))) > 0) {
		  nUIMsgID->Set_char(UIDS.Get(i), tmp);
		  TransMsgID->Set_int(tmp, i + 1);
		  TransMsgID->Set_char(tmp, groupname);
		  if (!Parent->Have(tmp)) {
		    UIMsgID->Delete(UIDS.Get(i));
		    if (fetchbodies)
		      sprintf(msg, "FETCH %d (BODY[])", i + 1);
		    else
		      sprintf(msg, "FETCH %d (BODY[HEADER])", i + 1);
		    SendCmd(msg);
		  }
		  if (Parent->ToCollector->UrgentPending)
		    i = k = numberofmessages;
		} else {
		  if (fetchbodies)
		    sprintf(msg, "FETCH %d (BODY[])", i + 1);
		  else
		    sprintf(msg, "FETCH %d (BODY[HEADER])", i + 1);
		  SendCmd(msg);
		}

		// printf ("cmmd %i\n", i);
	      }
	    for (i = k;
		 (i < numberofmessages) && (i - k < 16) && (!Parent->ToCollector->UrgentPending); ++i)	// Iterate 
	      // 
	      // 
	      // 
	      // through 
	      // each 
	      // sent 
	      // RETR 
	      // command
	      {
		if (strlen(tmp = UIMsgID->Get_char(UIDS.Get(i))) == 0) {
		  if (CheckReply(msg, 63))	// See if it
		    // generated a
		    // positive reply
		    {
		      sprintf(buf, "processing message %i", i);
		      Parent->ToUI->PostMessage(CLASS_STATUS,
						NO_SUBCLASS,
						Parent->Name, buf);

		      msgid[0] = 0;	// Clean msgid

		      done = 0;
		      msgbuffer.Pos = 0;

		      while (!done)	// For each line in the
				// message
			{
			  buf[0] = 0;
			  if (Con->Handle(buf, 255) < 0)	// Make sure
			    // the
			    // connection 
			    // is still
			    // alive
			    {
			      done = 1;
			      Con->Close();
			      ConnectionOpen = 0;
			      Parent->ToUI->PostMessage(CLASS_ERROR,
							SUB_ERROR_CRITICAL,
							"imap4handler",
							"There I was, collecting mail, minding my own business, and this punkass "
							"IMAP4 daemon started acting like a pain, dropping connections all over.");
			    } else {
			      if (buf[0])	// See if we received actual
				// data
				{
				  if ((buf[0] == ')')
				      && (buf[1] < 32)) done = 1;	// End 
				  // 
				  // of 
				  // message 
				  // reached
				  else {
				    choplf(buf);
				    cout << buf << endl;
				    strcat(buf, "\n");
				    msgbuffer.Add(buf);
				    if (!msgid[0])	// Check if
				      // we already 
				      // have the
				      // msgid
				      {
					if (wild_match
					    ("Message-ID:*", buf))	// See 
					  // 
					  // 
					  // 
					  // of 
					  // this 
					  // is 
					  // the 
					  // message-id
					  {
					    char           *
					      tmp;

					    choplf(buf);

					    // Seperate value from
					    // headername

					    tmp = strchr(buf, ':');
					    if (*tmp)
					      ++tmp;
					    while ((*tmp == ' ')
						   || (*tmp ==
						       '\t'))
					      ++tmp;

					    // copy over msgid

					    strncpy(msgid, tmp,
						    127);
					    msgid[127] = 0;

					    // store the msgid in the 
					    // 
					    // 
					    // 
					    // translation table

					    TransMsgID->Set_int
					      (msgid, i + 1);
					  }
				      }
				  }
				}
			    }
			}
		      if (ConnectionOpen)	// Only interested in
				// completely transfered
				// messages thank you
			{
			  if (strlen(msgid) < 1)	// In the odd case
			    // there is no msgid
			    // we'll have to make 
			    // one up
			    {
			      sprintf(msgid, "<IMAP4_%i-%08d@%s>",
				      (unsigned int) time(NULL), i,
				      Parameters->Get_char("host"));
			      // Of course this is inherently unstable
			      // and
			      // should not happen, but we need _some_
			      // id for the message. Oh heck, any
			      // message
			      // evil enough not to have a Message-ID
			      // set by
			      // the server deserves to haunt the user,
			      // begging him to LART the sender AND the
			      // author of the mailerdaemon.
			    }
			  nUIMsgID->Set_char(UIDS.Get(i), msgid);
			  if (!(Parent->Have(msgid)))	// Make sure
			    // this isn't 
			    // a double
			    {
			      sprintf(buf,
				      "processing message %i msgid %s",
				      i, msgid);
			      Parent->ToUI->PostMessage(CLASS_STATUS,
							NO_SUBCLASS,
							Parent->Name,
							buf);

			      MailMessage    *
				X = new MailMessage(Parent, msgid);

			      if (!msgbuffer.Data) {
				printf
				  ("!!!!!!!!!!!!!!!!!!!!!!!!! Assert failed: msgbuffer.Data\n");
			      } else {
				msgbuffer.Add((unsigned char) 0);
				if (fetchbodies)
				  X->ProcessMessage((char *)
						    msgbuffer.
						    Data);
				else
				  X->ProcessHeaders((char *)
						    msgbuffer.
						    Data);
			      }
			      if (deletemail)
				X->Flags |= MMFL_ORIGINAL;	// If 
			      // 
			      // 
			      // 
			      // we're 
			      // going 
			      // to 
			      // delete 
			      // the 
			      // mail 
			      // from 
			      // the
			      // server it's essential to turn on the
			      // preservation
			      // bits to prevent the message from
			      // getting flushed away.

			      Parent->Deliver(X);	// And now it's
			      // safely out of our
			      // hands.
			      Parent->Lock->LockRW();
			      for (j = 0;
				   j < X->Subscribers->Count(); ++j) {
				ViewListNode   *
				  VLN = X->Subscribers->Get(j);
				if (VLN)
				  Parent->ToUI->PostMessage
				    (CLASS_NEWARTICLE,
				     NO_SUBCLASS,
				     VLN->View->Name,
				     X->MsgID);
			      }
			      Parent->Lock->UnlockRW();
			      HighestHandled = i + 1;	// Save
			      // session
			      // state
			      msgbuffer.Pos = 0;
			      msgid[0] = 0;
			    }
			}
		    } else {
		      printf("error: %s\n", msg);
		    }
		  if (!ConnectionOpen)
		    i = numberofmessages;	// In case things
		  // messed up, bail
		  // out
		  msgbuffer.Pos = 0;	// Clear the buffer
		}
	      }
	  }

	delete(UIMsgID);
	UIMsgID = nUIMsgID;

	Parent->SaveMailBox();
	sprintf(buf, "%s/uids.dat", Parent->DirectoryPrefix);
	UIMsgID->Save(buf);


	if (fetchbodies && deletemail)	// Delete mail from server?
	  {
	    if (strcasecmp(Parameters->Get_char("writeprotect"), "Y"))	// Make 
	      // 
	      // 
	      // 
	      // sure 
	      // write 
	      // protect 
	      // isn't 
	      // on
	      {
		for (i = start;
		     (i < HighestHandled) && (!Parent->ToCollector->UrgentPending); ++i)	// Queue 
		  // 
		  // 
		  // 
		  // up 
		  // the 
		  // DELE 
		  // commands
		  {
		    sprintf(msg, "STORE %d +FLAGS (\\Deleted)", i + 1);
		    SendCmd(msg);
		    sprintf(msg, "EXPUNGE");
		    SendCmd(msg);
		  }
		for (i = start;
		     (i < HighestHandled) && (!Parent->ToCollector->UrgentPending); ++i)	// And 
		  // 
		  // 
		  // 
		  // verify 
		  // confirmation
		  {
		    if (CheckReply(msg, 63) < 0)	// Connection-related 
		      // 
		      // 
		      // 
		      // error
		      {
			i = HighestHandled;	// Bail out and stuff
			ConnectionOpen = 0;
			Con->Close();
		      }

		  }
	      }
	  }
      }
    }
  Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS, Parent->Name, "");
}

void
IMAP4Handler::DeleteMessage(char *msgid)
{				/*
				 * must change.  Assumes Inbox and msgid = ID 
				 * on server.  Also only deletes one message
				 * at a time 
				 */
  int
    imapid;	// Translated id number in the popbox
  char
    msg[63];

  if (strcasecmp(Parameters->Get_char("writeprotect"), "Y"))	// Make sure
    // write
    // protect
    // isn't on
    {
      if (!ConnectionOpen) {
	OpenConnection();	// Open connection on demand
	if (ConnectionOpen)
	  CheckGroup("*");
      }
      if (ConnectionOpen) {
	imapid = TransMsgID->Get_int(msgid);
	printf("imapid = %i\n", imapid);
	if (imapid) {
	  sprintf(msg, "SELECT %s", TransMsgID->Get_char(msgid));
	  SendCmd(msg);
	  sprintf(msg, "STORE %d +FLAGS (\\Deleted)", atoi(msgid));
	  SendCmd(msg);
	  sprintf(msg, "EXPUNGE");
	  SendCmd(msg);
	  sprintf(msg, "Deleting message %i", imapid);
	  Parent->ToUI->PostMessage(CLASS_STATUS, NO_SUBCLASS,
				    Parent->Name, msg);
	  printf("%s\n", msg);

	  if (CheckReply(msg, 63) < 0) {
	    Con->Close();
	    ConnectionOpen = 0;
	  }
	}
      }
    } else
      printf("[error] Write Protect\n");


}



void
IMAP4Handler::Expire(void)
{
  // Expire really doesn't make sense for IMAP4.  Perhaps we could just
  // call delete?
}

int
IMAP4Handler::PostMessage(WordList *, MailMessage *)
{
  // PostMessage doesn't make sense either, since SMTP handles the sending. 
  // 
  // 
  // 
  // Could also call append to change already
  // existing messages.
  return 0;
}



int
IMAP4Handler::CheckReply()	// for when we don't care
  // what gets sent back, only// that something is senthandles the sending.
  // f=======
{
  char
    buf[256];
  return CheckReply(buf, 256);
}

int
IMAP4Handler::CheckReply(char *buf, int len)
{
  int
    res;
  buf[0] = 0;

  while (((res = Con->Handle(buf, len)) == 0)
	 && (!Parent->ToCollector->UrgentPending));
  if (Parent->ToCollector->UrgentPending)
    return 0;
  if (res < 0)
    printf("connection error\n");
  if ((strncmp(buf, "*", 1) && strncmp(buf + 2, "OK", 2))
      || strncmp(buf + 6, "OK", 2)) {
    return 1;
  } else {
    char           *
      errorstring = new char[strlen(buf + 1) + 96];
    sprintf(errorstring, "Error with the IMAP4 server.  Msg: %s",
	    buf + 1);
    Parent->ToUI->PostMessage(CLASS_ERROR, SUB_ERROR_CRITICAL,
			      "IMAP4Handler", errorstring);
    cout << "IMAP Error: " << errorstring << endl;
    delete[](errorstring);
  }
  return 0;

}


void
IMAP4Handler::SendCmd(char *cmd)
{
  char           *
    msg = new char[255];
  sprintf(msg, "%c%04d %s\n", CmdPrefix, CmdNum, cmd);
  Con->Send(msg, 64);
  cout << msg << endl;
  delete[](msg);

  CmdNum++;

}
