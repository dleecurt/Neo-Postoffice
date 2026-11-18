#include <algorithm>
#include <vector>
#include "semaphore.h"
#include "helpdesk.h"
#include "encode.h"
#include "wordlist.h"
#include "smtp.h"
#include "buffer.h"
#include "formatter.h"
#include "mailbox.h"
#include "mailhandler.h"
#include "checksum.h"
#include "var.h"
#include "reg.h"
#include "substr.h"
#include "strdupp.h"
#include "chop.h"
#include "base64.h"
#include "mailstorage.h"
#include "debug.h"
#include "version.h"

#include <FL/Fl_Pixmap.H>
#include <FL/x.H>

#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

extern int 				 _USELOCALE;
extern char 			*_LANGUAGE;
extern Var 				*_CLOCALE;

extern class MailBox 	*OUTBOX;
extern class MailBox 	*MailBoxList[16];
extern char				 currentfolder[64];
extern MailBox			*currentmailbox;

extern int			 mdi_current;

extern char			 currentfrom[8][256];
extern char			 currentsubject[8][256];
extern char			 currentreplyto[8][256];
extern char			 currentfollowup[8][256];
extern char			 currentmsgid[8][256];
extern char			 currentreferences[8][2048];
extern WordList		 currentmessage[8];
extern int			 currentbodystart[8];
extern int			 currentsigstart[8];

extern int			 adr_widths[2];

extern Var			 PREFERENCES;

extern Fl_Window	*address;

void doforward (bool);

void cb_compose_header_expand (Fl_Widget *, struct Composer_Window *);
void cb_compose_header_shrink (Fl_Widget *, struct Composer_Window *);
void cb_selectaddress (Fl_Browser *, void *);
void cb_pasteaddress (Fl_Button *, void *);
void cb_sendmessage (Fl_Window *, struct Composer_Window *);
void cb_discardmessage (Fl_Window *, struct Composer_Window *);


struct Composer_Window
{
		Fl_Button 			*bt_address;
		Fl_Button			*bt_address2;
		Fl_Button			*bt_menu_expand;
		Fl_Button			*bt_resizeheader;
		
		Fl_FancyEditor		*in_message;
		
		Fl_Input			*in_to;
		Fl_Input			*in_cc;
		Fl_Input			*in_bcc;
		Fl_Input			*in_groups;
		Fl_Input			*in_subject;
		
		Fl_Choice			*ch_personality;
		Fl_Group			*gr_personality;
		
		Fl_Window			*comp;
		Fl_Group			*gr_mini_buttons;
		Fl_Group			*gr_bigbuttons;
		Fl_Group			*gr_headers;
		Fl_Group			*gr_to;
		Fl_Group			*gr_cc;
		Fl_Group			*gr_bcc;
		Fl_Group			*gr_groups;

		char				 msgid[256];
		char				*references;
		char				 outbox[256];

		// Neo-PostOffice: the message replied to or forwarded, so its
		// mailbox can flag it (Maildir R / P) when this one is sent.
		char				 origbox[64];
		char				 origid[256];
		char				 origflag;
};

// The composer windows that are open (Neo-PostOffice): the Message menu's
// items are shared by all of them, so their callbacks find the composer
// from the window the menu belongs to.
static std::vector<Composer_Window *> open_composers;


// Remembers the shown message as the original of W (flag is 'R' for a
// reply, 'P' for a forward) and puts its real Message-ID header in W->msgid
// for In-Reply-To/References. The internal id is not a Message-ID for
// every mailbox type (Maildir, SQL).
static void set_original (Composer_Window *W, char flag)
{
	snprintf (W->msgid, sizeof (W->msgid), "%s", currentmsgid[mdi_current]);
	W->origbox[0] = 0;
	snprintf (W->origid, sizeof (W->origid), "%s", currentmsgid[mdi_current]);
	W->origflag = flag;
	if (!currentmailbox) return;
	snprintf (W->origbox, sizeof (W->origbox), "%s", currentmailbox->Name);
	MailMessage *M = currentmailbox->Find (currentmsgid[mdi_current]);
	if (!M) return;
	if (M->HeaderNames->Count == 0) M->LoadHeaders();
	const char *mid = M->Header ("Message-ID");
	if (mid && *mid) snprintf (W->msgid, sizeof (W->msgid), "%s", mid);
}

Composer_Window *create_composer_window (void)
{
	Fl_Window *comp = make_compose_window();
	comp->size_range (comp->w(),comp->h(),comp->w(),0);
	WordList *X = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("identities");
	int i;
	for (i=0;i<X->Count;++i)
	{
		compose_ch_personality->add (PREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (X->Get(i))->Get_char ("shortname"));
	}
	compose_ch_personality->value (0);

	comp->show();
	//win_main->redraw();
	
	Composer_Window *W 	= new Composer_Window;
	W->origbox[0]		= 0;
	W->origid[0]		= 0;
	W->origflag			= 0;
	W->bt_address 		= compose_bt_address;
	W->bt_address2		= compose_bt_address2;
	W->bt_menu_expand	= bt_compose_menu_expand;
	W->bt_resizeheader	= compose_bt_resizeheader;

	W->in_message 		= compose_in_message;
	W->in_to	  		= compose_in_to;
	W->in_cc			= compose_in_cc;
	W->in_bcc			= compose_in_bcc;
	W->in_groups		= compose_in_groups;
	W->in_subject		= compose_in_subject;
	W->ch_personality	= compose_ch_personality;
	W->gr_personality	= compose_gr_personality;

	W->comp		  		= comp;
	W->gr_mini_buttons	= gr_compose_mini_buttons;
	W->gr_bigbuttons	= gr_compose_bigbuttons;
	W->gr_headers		= gr_compose_headers;
	W->gr_to			= gr_compose_to;
	W->gr_cc			= gr_compose_cc;
	W->gr_bcc			= gr_compose_bcc;
	W->gr_groups		= gr_compose_groups;
	
	W->in_message->WantTabs (false);
	
	compose_bt_address->callback ((Fl_Callback *) cb_address, (void *) compose_in_to);
	compose_bt_address2->callback ((Fl_Callback *) cb_address, (void *) compose_in_cc);
	compose_bt_address4->callback ((Fl_Callback *) cb_address, (void *) compose_in_bcc);
	compose_bt_send->callback ((Fl_Callback *) cb_sendmessage, (void *) W);
	open_composers.push_back (W);
	compose_mbt_send->callback ((Fl_Callback *) cb_sendmessage, (void *) W);
	comp->callback ((Fl_Callback *) cb_discardmessage, (void *) W);

	bt_compose_menu_shrink->callback ((Fl_Callback *) cb_compose_menu_shrink, (void *) W);
	bt_compose_menu_expand->callback ((Fl_Callback *) cb_compose_menu_expand, (void *) W);
	compose_bt_resizeheader->callback ((Fl_Callback *) cb_compose_header_expand, (void *) W);
	if (strcasecmp (PREFERENCES.Get_char_tree ("postoffice/state/composer/toolbar"), "small") == 0)
		cb_compose_menu_shrink (bt_compose_menu_shrink, W);
	if (strcasecmp (PREFERENCES.Get_char_tree ("postoffice/state/composer/headers"), "full") == 0)
		cb_compose_header_expand (compose_bt_resizeheader, W);
	if (currentmailbox) strcpy (W->outbox, currentmailbox->Name);
	else W->outbox[0] = 0;
	
	return W;
}

void cb_compose (Fl_Menu_ */*m*/, void */*d*/)
{
	Composer_Window *W = create_composer_window();
	
	W->msgid[0]			= 0;
	W->references		= strdupp ("");
}

void cb_bugreport (Fl_Button */*b*/, void */*v*/)
{
	WordList WT;
	AsyncBuffer *Msg = new AsyncBuffer (4096);

	win_buildinfo->hide();

	Composer_Window *W = create_composer_window();

	Msg->AddF ("Product:        Post Office\n");
	Msg->AddF ("Version:        %s\n", _RELEASE);
	Msg->AddF ("Build:          %s\n", _BUILD);
	Msg->AddF ("Architecture:   %s\n", _ARCH);
	WT.MakeGeneric ((char *) _UNAME, '#');
	Msg->AddF ("Kernel:         %s\n", WT.Get(0));
	Msg->AddF ("Build Info:     %s\n", _BUILDINFO);
	Msg->AddF ("\n");
	Msg->AddF ("Please Fill in this information\n\n");
	Msg->AddF ("### A short description of the problem...\n");
	Msg->AddF ("\n <your info here>\n\n");
	Msg->AddF ("### A way, if any, to reliably reproduce the problem\n");
	Msg->AddF ("\n <...>\n\n");
	Msg->AddF ("### What do you think is the cause?\n");
	Msg->AddF ("\n <Solar flares,...>\n\n");
	Msg->AddF ("### Got a patch?\n");
	Msg->AddF ("\n <No I'm lazy>\n\n");
	Msg->Add  ((unsigned char) 0);
	W->in_to->value ("Pest Control <bugs@tarball.net>");
	W->in_subject->value ("[Post Office] Bug Report");
	W->in_message->LoadFrom ((char *) Msg->Data);
	delete (Msg);
}

void cb_reply_m (Fl_Menu_ *w, void *d)
{
	cb_reply ((Fl_Button *) w, d);
}

void cb_reply (Fl_Button */*w*/, void */*d*/)
{
	if (!currentmsgid[mdi_current][0]) return;
	int i;
	char buf[264]; // "Fwd: " + 255-character subject
	WordList *quoted_lines;
	AsyncBuffer *quoted_body;

	// Create the composer window
	
	Composer_Window *W = create_composer_window();

	set_original (W, 'R');
	W->references = strdupp (currentreferences[mdi_current]);

	// Quote the original message
	
	quoted_lines = new WordList;
	
	for (i=currentbodystart[mdi_current];i<currentsigstart[mdi_current];++i)
	{
		quoted_lines->Add (currentmessage[mdi_current].Get(i));
	}
	
	TextMessage *Tx = new TextMessage (quoted_lines);
	delete (quoted_lines);
	quoted_lines = Tx->Get (1);
	
	quoted_body = new AsyncBuffer (4096);
	
	quoted_body->AddF ("%s tapped some keys and produced:\n\n", currentfrom[mdi_current]);
	
	for (i=0;i<quoted_lines->Count;++i)
	{
		quoted_body->AddF ("%s\n", quoted_lines->Get(i));
	}
	quoted_body->Add ((unsigned char) 0);
	
	delete (quoted_lines);

	// Add a "Re:" if it isn't already there in the subject
	
	if ( (tolower(currentsubject[mdi_current][0]) == 'r') &&
		 (tolower(currentsubject[mdi_current][1]) == 'e') &&
		 (currentsubject[mdi_current][2] == ':') )
	{
		 strcpy (buf, currentsubject[mdi_current]);
	}
	else snprintf (buf, sizeof (buf), "Re: %s", currentsubject[mdi_current]);
	
	W->in_subject->value (buf);
	W->in_to->value (currentreplyto[mdi_current]);
	W->in_groups->value (currentfollowup[mdi_current]);
	
	W->in_message->LoadFrom ((char *) quoted_body->Data);
	delete (quoted_body);
}

void cb_forward_m (Fl_Menu_ */*w*/, void */*d*/)
{
	doforward (false);
}

void cb_forward (Fl_Button */*w*/, void */*d*/)
{
	doforward (false);
}

void cb_forwardfull_m (Fl_Menu_ */*w*/, void */*d*/)
{
	doforward (true);
}

void doforward (bool fullhdr)
{
	if (!currentmsgid[mdi_current][0]) return;
	int i;
	char buf[264]; // "Fwd: " + 255-character subject
	AsyncBuffer *quoted_body;

	Composer_Window *W = create_composer_window();

	set_original (W, 'P');
	W->references = strdupp (currentreferences[mdi_current]);

	// Quote the original message
	
	quoted_body = new AsyncBuffer (4096);
	quoted_body->AddF ("### --------------------------------------------------------------------\n");
	quoted_body->AddF ("### Forwarded Message\n### %s\n", currentmsgid[mdi_current]);
	quoted_body->AddF ("### --------------------------------------------------------------------\n\n");

	if (currentmailbox)
	{
		MailMessage *M = currentmailbox->Find (currentmsgid[mdi_current]);
		if (M->HeaderNames->Count == 0) M->LoadHeaders();
		for (i=0;i<M->HeaderNames->Count;++i)
		{
			char *tt = M->HeaderNames->Get (i);
			if ( fullhdr ||
				 (strncasecmp (tt, "From", 4) == 0) ||
				 (strncasecmp (tt, "To", 2) == 0) ||
				 (strncasecmp (tt, "Date", 4) == 0) ||
				 (strncasecmp (tt, "Subject", 7) == 0) ||
				 (strncasecmp (tt, "Organization", 12) == 0) )
			{
				quoted_body->AddF ("%s: %s\n", M->HeaderNames->Get(i), M->HeaderValues->Get(i));
			}
		}
		quoted_body->AddF ("\n");
	}
	
	for (i=currentbodystart[mdi_current];i<currentmessage[mdi_current].Count;++i)
	{
		quoted_body->AddF ("%s\n", currentmessage[mdi_current].Get(i));
	}

	quoted_body->AddF ("\n### End forwarded message\n");
	quoted_body->Add ((unsigned char) 0);
	
	// Add a "Re:" if it isn't already there in the subject
	
	snprintf (buf, sizeof (buf), "Fwd: %s", currentsubject[mdi_current]);
	
	W->in_subject->value (buf);
	W->in_to->value (currentreplyto[mdi_current]);
	W->in_groups->value (currentfollowup[mdi_current]);
	
	W->in_message->LoadFrom ((char *) quoted_body->Data);
	delete (quoted_body);
}

static Composer_Window *composer_of (Fl_Widget *w)
{
	Fl_Window *win = w ? w->window() : NULL;
	for (Composer_Window *W : open_composers)
		if (W->comp == win) return W;
	return NULL;
}

static void forget_composer (Composer_Window *W)
{
	open_composers.erase (std::remove (open_composers.begin(), open_composers.end(), W), open_composers.end());
}

void cb_compose_menu_send (Fl_Menu_ *m, void * /*v*/)
{
	if (Composer_Window *W = composer_of (m)) cb_sendmessage (W->comp, W);
}

void cb_compose_menu_discard (Fl_Menu_ *m, void * /*v*/)
{
	if (Composer_Window *W = composer_of (m)) cb_discardmessage (W->comp, W);
}

void cb_discardmessage (Fl_Window */*b*/, Composer_Window *W)
{
	forget_composer (W);
	W->comp->hide();
	delete[] (W->references);
	delete (W);
}

char *make_msgid (void);
char *make_msgid (void)
{
	unsigned long x,y;
	const char *hostname = getenv ("HOSTNAME");
	if (!hostname) hostname = "localhost";
	if (strlen (hostname) < 2) hostname = "localhost";
	char *result = new char[32+strlen (hostname)];
	x = time (NULL);
	y = rand() & 0xffffff;
	sprintf (result, "<PstOfc.%08x.%06x@%s>", (unsigned int) x, (unsigned int) y, hostname);
	return result;
}

void cb_sendmessage (Fl_Window */*win*/, Composer_Window *W)
{
	int i;
	WordList *HdrNames;
	WordList *HdrValues;
	WordList *RCPT = new WordList;
	time_t ti;
	char *ct;
	WordList WD;
//	char emailto[256];
//	char nameto[256];
	char buf[512];
	char *crsr;
//	char *error;
	char *mid;
	char *ref;

	// Build a clean data-string out of ctime()

	ti = time (NULL);
	ct = strdupp (ctime (&ti));
	ct[3] = ct[7] = ct[10] = ct[19] = ct[24] = '/';
	WD.MakeGeneric (ct, '/');
	delete[] ct;

	// Create a new message for the OutBox

	MailMessage *M = new MailMessage (OUTBOX, mid = make_msgid());
	HdrNames = M->HeaderNames;
	HdrValues = M->HeaderValues;
	
//	int crsrpos = 0;
//	char *tmp;
	

	const char *pers_from = NULL;
	const char *pers_email = NULL;
	char *pers_replyto = NULL;
	char *pers_org = NULL;
	char *sig = NULL;

	// Get the right headers for the personality that the user selected
	
	char *pers_val = (char *) compose_ch_personality->text (compose_ch_personality->value());
	WordList *X = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("identities");
	for (i=0;i<X->Count;++i)
	{
		Var *VV = PREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (X->Get(i));
		if (strcasecmp (VV->Get_char ("shortname"), pers_val) == 0)
		{
			pers_from = VV->Get_char ("realname");
			pers_email= VV->Get_char ("mailfrom");
			pers_replyto = VV->Get_char ("replyto");
			pers_org = VV->Get_char ("organization");
			sig = decode_mysql (VV->Get_char ("signature"));
		}
	}

	if (!sig) sig = strdupp (".sigs are for the weak\n");

	// Actually I should tell the user to sod off and fix his prefs, but this'll work for now

	if (!pers_from) pers_from = "Localhost";
	if (!pers_email) pers_email = "root@localhost";
   
	char *body = new char[W->in_message->CharacterCount()+512+strlen (sig)]; // Allocate adequate space for the edit buffer
	W->in_message->SaveTo (body); // Store the edit buffer inside the body

	strcat (body, "-- \n");
	strcat (body, sig);
	delete[] sig;

	if (strlen (W->msgid) > 0) // W->msgid contains the original message's message-id if the message is a reply
	{
		HdrNames->Add ("References");
		if (strlen (W->references)) // If the original message had a References header, W->references contains it
		{
			WordList	 REF;
			char		*tmp;
			int			 len;
			
			ref = new char [strlen (W->references) + strlen (mid) + strlen (W->msgid) + 8]; // Create a new references header
			ref[0] = 0;
			
			REF.MakeGeneric (W->references, ' '); // Split the header and remove commas that some moronic
			for (i=0;i<REF.Count;++i)			  // clients mistakingly put in there.
			{
				tmp = REF.Get (i);
				len = strlen (tmp);
				if ((len>0) && (tmp[len-1]==',')) tmp[len-1] = 0; // comma-be-gone
				strcat (ref, REF.Get(i));
				strcat (ref, " ");
			}
			strcat (ref, W->msgid); // add the original message's message-id to the list of references
			HdrValues->Add (ref); // Add it as a header
			delete[] ref; // Give libc its memory back
		}
		else HdrValues->Add (W->msgid); // There was no references header, let's start one
		HdrNames->Add ("In-Reply-To"); // And let's also be friendly to mutt and pine
		HdrValues->Add (W->msgid);
		M->Envelope->Set (ENVELOPE_I_PARENTID, W->msgid); // Keep our own database neat too
	}

	// Add the Date header cleanly

	HdrNames->Add ("Date");
	char *daymonth = WD.Get(2);
	if (daymonth[0]==' ') ++daymonth;
	snprintf (buf, sizeof (buf), "%s, %s %s %s %s %s", WD.Get(0), daymonth, WD.Get(1), WD.Get(4), WD.Get(3), tzname[1]);
	HdrValues->Add (buf);
	M->Envelope->Set (ENVELOPE_DATE, buf);

	// Fix Robert's obsession with Windows EOL conventions ;)
	
	crsr = body;
	while ((crsr) && (*crsr))
	{
		if (*crsr == 13)
			*crsr = '\n';
		++crsr;
	}

	// Give the user some feedback that his message is being worked on.
	
	W->comp->deactivate();
	
	
	M->Envelope->Set (ENVELOPE_NAMEFROM, pers_from);
	M->Envelope->Set (ENVELOPE_MAILFROM, pers_email);
	M->Envelope->Set (ENVELOPE_GROUPS, "Queue");
	M->Flags = MMFL_ORIGINAL | MMFL_HAVEBODY | MMFL_INRAM;
	
	snprintf (buf, sizeof (buf), "%s <%s>", pers_from, pers_email);
	HdrNames->Add ((char *) "From");
	HdrValues->Add (buf);
	if (strlen (W->in_to->value()))
	{
		HdrNames->Add ((char *) "To");
		HdrValues->Add ((char *) W->in_to->value());
	}

	// If there are newsgroups filled in, figure out where to post it to
	
	if (strlen (W->in_groups->value()) > 0)
	{
		HdrNames->Add ((char *) "Newsgroups");
		HdrValues->Add ((char *) W->in_groups->value());
		HdrNames->Add ((char *) "X-Post-To");
		HdrValues->Add ((char *) W->outbox);
	}

	
	if (strlen (W->in_cc->value()) > 0)
	{
		HdrNames->Add ((char *) "CC");
		HdrValues->Add ((char *) W->in_cc->value());
	}

	// BCC is just added as a regular header. It should be stripped when delivering.
	
	if (strlen (W->in_bcc->value()) > 0)
	{
		HdrNames->Add ((char *) "BCC");
		HdrValues->Add ((char *) W->in_bcc->value());
	}
	
	HdrNames->Add ((char *) "Subject");
	HdrValues->Add ((char *) W->in_subject->value());
	M->Envelope->Set (ENVELOPE_SUBJECT, (char *) W->in_subject->value());
	
	if (pers_replyto && strlen (pers_replyto) > 0)
	{
		HdrNames->Add ((char *) "Reply-To");
		HdrValues->Add ((char *) pers_replyto);
	}
	
	if (pers_org && *pers_org)
	{
		HdrNames->Add ("Organization");
		HdrValues->Add ((char *) pers_org);
	}

	HdrNames->Add ("X-Mailer");
	HdrValues->Add ( __VERSIONSTRING "(by " __BUILDINFO ")" );

	// This should become a lot more niffty. Right now I just add one text/plain part.

	M->AddPart ((char *) "text/plain", "", "", (char *) body);

	// Store the message in the outbox
	
	OUTBOX->Deliver (M);

	// And save all shit.

	M->SaveHeaders();
	M->StoreFile();

	// Announce the world the birth of a new message

	OUTBOX->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, OUTBOX->VFirst->Name, M->MsgID);

	// Flag the original message as replied to / forwarded (Maildir).
	if (W->origflag && W->origbox[0])
	{
		for (int k = 0; k < 16 && MailBoxList[k]; ++k)
		{
			if (strcmp (MailBoxList[k]->Name, W->origbox) != 0) continue;
			if (MailBoxList[k]->Handler && MailBoxList[k]->Handler->KeepsFlags())
			{
				char flag[3] = { '+', W->origflag, 0 };
				MailBoxList[k]->ToCollector->PostMessage (CLASS_SETFLAG, NO_SUBCLASS, W->origid, flag);
			}
			break;
		}
	}

	// Clean up the Window and it's structure.
	
	forget_composer (W);
	W->comp->hide();
	//delete (W->comp);
	delete[] (W->references);
	delete (W);

	// Clean up our own temp stuff

	delete (RCPT);
}

void cb_compose_menu_shrink (Fl_Button */*b*/, void *v)
{
	Composer_Window *W = (Composer_Window *) v;

	W->gr_bigbuttons->hide();
	W->gr_mini_buttons->show();
	W->in_message->ShowPanel (0);
	PREFERENCES.Set_char_tree ("postoffice/state/composer/toolbar","small");
}

void cb_compose_menu_expand (Fl_Button */*b*/, void *v)
{
	Composer_Window *W = (Composer_Window *) v;
	
	W->gr_mini_buttons->hide();
	W->gr_bigbuttons->show();
	W->in_message->ShowPanel (1);
	PREFERENCES.Set_char_tree ("postoffice/state/composer/toolbar","big");
}

void cb_compose_header_expand (Fl_Widget */*x*/, Composer_Window *W)
{
	int base_x, base_y, base_inc, top_inc;
	
	PREFERENCES.Set_char_tree ("postoffice/state/composer/headers","full");

	base_x = W->in_to->x();
	base_y = W->in_to->y();
	base_inc = W->in_subject->y() - base_y;
	top_inc = W->in_to->y() - W->gr_headers->y();
	
	W->gr_personality->show();
	W->ch_personality->show();
	W->gr_cc->show();
	W->gr_bcc->show();
	W->gr_groups->show();
	
	W->gr_headers->size (W->gr_headers->w(), top_inc + 6 * base_inc);
	W->gr_personality->resize (base_x, base_y, W->gr_personality->w(), 20);
	W->gr_to->resize (base_x, base_y + 1 * base_inc, W->gr_to->w(), 20);
	W->gr_cc->resize (base_x, base_y + 2 * base_inc, W->gr_cc->w(), 20);
	W->gr_bcc->resize (base_x, base_y + 3 * base_inc, W->gr_cc->w(), 20);
	W->gr_groups->resize (base_x, base_y + 4 * base_inc, W->gr_groups->w(), 20);
	W->in_subject->resize (base_x, base_y + 5 * base_inc, W->in_subject->w(), 20);
	W->in_message->hide();
	W->in_message->resize (W->in_message->x(), W->in_message->y() + 4 * base_inc, W->in_message->w(), W->in_message->h() - 4 * base_inc);	
	W->in_message->show();
	
	W->bt_resizeheader->callback ((Fl_Callback *) cb_compose_header_shrink, (void *) W);
	W->gr_headers->redraw();
	W->in_message->redraw();
}

void cb_compose_header_shrink (Fl_Widget */*x*/, Composer_Window *W)
{
	int base_x, base_y, base_inc, top_inc;

	PREFERENCES.Set_char_tree ("postoffice/state/composer/headers","compact");

	base_x = W->gr_personality->x();
	base_y = W->gr_personality->y();
	base_inc = W->in_to->y() - base_y;
	top_inc = W->gr_personality->y() - W->gr_headers->y();
	
	W->gr_personality->hide();
	W->gr_cc->position (base_x, base_y);
	W->gr_cc->hide();
	W->gr_bcc->hide();
	W->gr_groups->position (base_x, base_y);
	W->gr_groups->hide();

	W->gr_to->position (base_x, base_y);
	W->in_subject->Fl_Widget::position (base_x, base_y + 1 * base_inc);

	W->gr_headers->size (W->gr_headers->w(), top_inc + 2 * base_inc);
	W->bt_resizeheader->callback ((Fl_Callback *) cb_compose_header_expand, (void *) W);
	W->in_message->hide();
	W->in_message->resize (W->in_message->x(), W->in_message->y() - 4 * base_inc, W->in_message->w(), W->in_message->h() + 4 * base_inc);	
	W->in_message->show();
	W->gr_headers->redraw();
	W->in_message->redraw();
}

void cb_address (Fl_Button */*b*/, void *d)
{
	int i;
	char buf[256];
	WordList *W = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("addressbook");
	
	if (!address)
	{
		address = make_address_window();
	}
	br_addresses->clear();
	br_addresses->column_widths (adr_widths);
	for (i=0;i<W->Count;++i)
	{
		snprintf (buf, sizeof (buf), "%s\t%s", W->Get(i), PREFERENCES.Get_char_tree (3, "postoffice","addressbook",W->Get(i)));
		br_addresses->add (buf, NULL);
	}
	br_addresses->callback ((Fl_Callback *) cb_selectaddress, (void *) d);
	bt_address_execute->callback ((Fl_Callback *) cb_pasteaddress, (void *) d);
	bt_address_delete->deactivate();
	address->show();
}

void cb_address_new (Fl_Button */*b*/, void */*d*/)
{
	char buf[256];

	PREFERENCES.Get_collection ("postoffice")->Get_collection ("addressbook")
		->Set_char (in_address_nickname->value(), in_address_email->value());


	WordList *W = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("addressbook");

	br_addresses->clear();
	br_addresses->column_widths (adr_widths);
	for (int i=0;i<W->Count;++i)
	{
		snprintf (buf, sizeof (buf), "%s\t%s", W->Get(i), PREFERENCES.Get_char_tree (3, "postoffice","addressbook",W->Get(i)));
		br_addresses->add (buf, NULL);
	}
	bt_address_delete->deactivate();
}

void cb_address_delete (Fl_Button */*b*/, void */*d*/)
{
	if (br_addresses->value()<0) return;

	char buf[256];
	WordList L;
	char *tmp = (char *) br_addresses->text (br_addresses->value());
	
	L.MakeGeneric (tmp, '\t');
	PREFERENCES.Get_collection("postoffice")->Get_collection("addressbook")->Delete(L.Get(0));

	WordList *W = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("addressbook");

	br_addresses->clear();
	br_addresses->column_widths (adr_widths);
	for (int i=0;i<W->Count;++i)
	{
		snprintf (buf, sizeof (buf), "%s\t%s", W->Get(i), PREFERENCES.Get_char_tree (3, "postoffice","addressbook",W->Get(i)));
		br_addresses->add (buf, NULL);
	}
	bt_address_delete->deactivate();
}

void cb_selectaddress (Fl_Browser *b, void */*d*/)
{
	if (b->value() < 0)
	{
		bt_address_delete->deactivate();
		return;
	}
	
	bt_address_delete->activate();
	
	char *selected = (char *) b->text (b->value());
	WordList *W = new WordList;
	W->MakeGeneric (selected, '\t');
	in_address_nickname->value (W->Get(0));
	in_address_email->value (W->Get(1));
}

void cb_pasteaddress (Fl_Button */*b*/, void *d)
{
	Fl_Input *I = (Fl_Input *) d;
	dprintf ("[addressbook] callback window: %p\n", (void *) d);
	if (I) I->value (in_address_email->value());
	address->hide();
}

