#include <pthread.h>
#include <signal.h>
#include "mailheaders.h"
#include <string>
#include "semaphore.h"
#include "helpdesk.h"
#include "encode.h"
#include "wordlist.h"
#include "smtp.h"
#include "buffer.h"
#include "formatter.h"
#include "mailbox.h"
#include "checksum.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <FL/Fl_Pixmap.H>
#include <FL/Fl_Multi_Browser.H>
#include <FL/x.H>
#include <FL/fl_ask.H>
#include <string.h>
#include "var.h"
#include "reg.h"
#include "substr.h"
#include "strdupp.h"
#include "chop.h"
#include "ccache.h"
#include "buffer.h"
#include "ht.h"
#include "base64.h"
#include <FL/Fl_Button.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl.H>
#include <FL/Fl_Toggle_Tree.H>
#include <FL/Fl_Pixmap.H>
#include <signal.h>
#include "mailstorage.h"
#include "initexitthread.h"

#include "debug.h"
#include "status.h"
#include "style.h"
#include "folderdata.h"

#include <setjmp.h>

#define locs(uk,nam) ((const char *) (_USELOCALE ? _CLOCALE->Get_char(nam) : (uk)))

#define SORT_THREAD 0x00
#define SORT_RECEIVE 0x01

#include "icons/envelope_ghost.xpm"
#include "icons/envelope_open.xpm"
#include "icons/envelope_closed.xpm"
#include "icons/flagged.xpm"

Fl_Pixmap pix_envelope_ghost (envelope_ghost_xpm);
Fl_Pixmap pix_envelope_open (envelope_open_xpm);
Fl_Pixmap pix_envelope_closed (envelope_closed_xpm);
Fl_Pixmap pix_flagged (flagged_xpm);

int mysort (Fl_Toggle_Node_Base *, Fl_Toggle_Node_Base *);

extern time_t			 lastclick;
extern int				 hdrwidths[3];
extern class MailBox	*MailBoxList[16];
extern class MailBox	*OUTBOX;
extern Fl_Scroll		*current_boxlist;
extern char				 currentfolder[64];
extern MailBox			*currentmailbox;
extern int				 folderlist_fullsize;
extern Var				 PREFERENCES;
extern Fl_Pixmap		 pixmap_folder;
extern Fl_Pixmap		 pixmap_folder_open;
extern Fl_Pixmap		 pixmap_mbox;
extern Fl_Pixmap		 pixmap_compose;

extern int				 _USELOCALE;
extern char				*_LANGUAGE;
extern Var				*_CLOCALE;
extern int				 _SORTMETHOD;
extern int				  SORTFIELD;

extern Fl_Toggle_Tree	*br_folderindex;

extern int				 unreadonly;

extern CCache			 CCACHE;

void update_folder (void);
void new_folderitem (MailMessage *);

FolderData::FolderData (MailMessage *M)
{
	const char *subj;
	msgid = strdupp (M->MsgID);
	chk_msgid = checksum (msgid);
	
	subj = M->Envelope->Get (ENVELOPE_SUBJECT);
	if (strncasecmp (subj, "Re: ", 4) == 0)
	{
		subj += 4;
	}
	chk_subject = checksum (subj);
}

FolderData::~FolderData (void)
{
	delete[] (msgid);
}

void cb_sort_thread (Fl_Button */*b*/, void */*v*/)
{
	if (_SORTMETHOD == SORT_THREAD) _SORTMETHOD = 1;
	else _SORTMETHOD = SORT_THREAD;
	update_folder();
}

//===============================================================================
// FUNCTION		new_folderitem
// SYNOPSIS		Adds a reference to a new message to the index view.
//
// ARGUMENTS	M		(MailMessage *)	The message to be processed
//
// RETURNS		Function returns no data.
//===============================================================================

void new_folderitem (MailMessage *M)
{
	Fl_Pixmap		*fpm = &pix_envelope_closed;
	char			 buf[512];
	Fl_Color		 read = (Fl_Color) _STYLES.color ("index:score0");
	CCacheNode		*C;
	Fl_Toggle_Node	*nn;
	int				 score=1;
	MailView		*MV = currentmailbox->GetView (currentfolder);
	
	// Score the message according to the rules set in the MailView
	
	if (MV) score = MV->ProcessScore (M);
	if (score<=0) // Negative score, killfiled
	{
		dprintf ("[reject] %s(%i)\n", M->MsgID, score);
		return;
	}
	
	// Deduct the sender. Prefer the Real Name, use email if
	// unavailable
	
	const char *from = M->Envelope->Get (ENVELOPE_NAMEFROM);
	if (strlen (from) < 2) from = M->Envelope->Get (ENVELOPE_MAILFROM);

	// Check whether the message has been read. If so, adjust style.

	if (M->Envelope->GetInt (ENVELOPE_I_READ))
	{
		fpm = &pix_envelope_open;
		read = (Fl_Color) _STYLES.color ("index:read_message");
	}
	
	// Show ghosted envelope for messages that have not been downloaded
	
	if (! (M->Flags & MMFL_HAVEBODY)) fpm = &pix_envelope_ghost;

	// Flagged messages (Maildir F flag) show a flag instead
	
	if (M->Envelope->GetInt (ENVELOPE_I_FLAGS) & EFLAG_FLAGGED) fpm = &pix_flagged;
	
	// Mark recent messages
	
	if (((time (NULL) - M->ReceiveDate)<1800) && (!M->Envelope->GetInt (ENVELOPE_I_READ))) read = (Fl_Color) _STYLES.color ("index:new_message");

	// adjust color for higher scores

	if (score==2) read = (Fl_Color) _STYLES.color ("index:score2");
	else if (score==3) read = (Fl_Color) _STYLES.color ("index:score3");
	else if (score==4) read = (Fl_Color) _STYLES.color ("index:score4");
	else if (score>4) read = (Fl_Color) _STYLES.color ("index.score5");

	// build up line for indexview

	// Neo-PostOffice: decoded (RFC 2047) and bounded; tabs would split columns.
	std::string subject = mailheaders::Decode (M->Envelope->Get (ENVELOPE_SUBJECT));
	std::string sender = mailheaders::Decode (from);
	for (char &c : subject) if (c == '\t') c = ' ';
	for (char &c : sender) if (c == '\t') c = ' ';
	snprintf (buf, sizeof (buf), "%.300s\t%.120s\t%.60s", subject.c_str(), sender.c_str(), M->Envelope->Get (ENVELOPE_DATE));

	// do threading if that's what the user wants

	if (_SORTMETHOD == SORT_THREAD)
	{
		// message has a recorded parent message?
	
		if (strlen (M->Envelope->Get (ENVELOPE_I_PARENTID)) > 2)
		{
			// scan the cache for the parent message
		
			if ((C = CCACHE.FindMsgid ((char *) M->Envelope->Get (ENVELOPE_I_PARENTID))))
			{
				// add as a child node
			
				br_folderindex->current (C->node);
				((Fl_Toggle_Node *)C->node)->can_open (1);
				br_folderindex->add_sub (buf, 0, fpm, (void *) new FolderData (M));

				nn = (Fl_Toggle_Node *) br_folderindex->current();
				nn->color (read);
				
				CCACHE.Add (M, (Fl_Toggle_Node_Base *) br_folderindex->current());
				return;
			}
		}

		// scan for messages with the same subject line

		const char * tmp = M->Envelope->Get (ENVELOPE_SUBJECT);
		if ((tmp[2]==':') && (C = CCACHE.FindSubject ((char *) M->Envelope->Get (ENVELOPE_SUBJECT))))
		{
			br_folderindex->current (C->node);
			((Fl_Toggle_Node *)C->node)->can_open (1);
			br_folderindex->add_sub (buf, 0, fpm, (void *) new FolderData (M));
			nn = (Fl_Toggle_Node *) br_folderindex->current();
			nn->color (read);
		}
		else // add to the end of the tree
		{
			br_folderindex->traverse_start();
			br_folderindex->add_next (buf, 0, fpm, (void *) new FolderData (M));
			nn = (Fl_Toggle_Node *) br_folderindex->current();
			//nn->color (read);
		}
	}
	else // add to the end of the tree
	{
		br_folderindex->traverse_start();
		br_folderindex->add_next (buf, 0, fpm, (void *) new FolderData (M));
		nn = (Fl_Toggle_Node *) br_folderindex->current();
		//nn->color (read);
	}
	CCACHE.Add (M, (Fl_Toggle_Node_Base *) br_folderindex->current());
}

// Empties the message list (and frees its row data and cache).
void clear_folder_index (void)
{
	Fl_Toggle_Node_Base *x;
	if (br_folderindex->first())
	{
		x = br_folderindex->traverse_start();
		while (x)
		{
			if (br_folderindex->current()->data()) delete ((FolderData *) br_folderindex->current()->data());
			br_folderindex->current()->data(NULL);
			x = br_folderindex->traverse_forward();
		}

		// Fl_ToggleTree doesn't clean up its mess properly

		while (br_folderindex->first())
			br_folderindex->clear();

		// Clear cache

		CCACHE.Clear();
		
		// redraw everything
		
		br_folderindex->update_height();
		br_folderindex_->scroll_to (0,0); // FLTK 1.0 Fl_Scroll::position() scrolled; in 1.4 it moves the widget
		br_folderindex_->redraw();
	}
}

//===============================================================================
// FUNCTION		update_folder
// SYNOPSIS		(Re-) draws the currently selected folder in the indexview pane
//
// RETURNS		Function returns no data
//===============================================================================

void update_folder (void)
{
	int i;
	int flip = 0;
	
	MailMessage *M;
    if (currentmailbox == NULL) return;
	MailView *V = currentmailbox->GetView (currentfolder);
	if (V == NULL) return;
	dprintf ("V = %p\n", (void *) V);

	clear_folder_index();

	dprintf ("count = %i\n", V->Count());

	for (i=(V->Count()<1024) ? 0 : V->Count() - 1024;i<V->Count();++i)
	{
		// don't flood X with updates
	
		flip = (flip + 1) & 0xff;
		if (!flip) Fl::wait (0.05);
		
		// Get the current message in the view
		
		M = V->GetMessage (i);
		if (M)
		{
			if (unreadonly)
			{
				dprintf ("unreadonly\n");
				if (! (M->Envelope->GetInt (ENVELOPE_I_READ))) new_folderitem (M);
			}
			else new_folderitem (M);
			
			V->DoneWithMessage(); // unlock view
			
			if (!(i & 0x0f)) // update the view now and then
			{
				br_folderindex->update_height();
				//Fl::wait (0.001);
			}
		}
	}

	br_folderindex->update_height();
	if (SORTFIELD>=0) br_folderindex->sort_tree (mysort);
	br_folderindex->redraw();
	
	//set_status (locs("Done...","status_done"));
}

int mysort (Fl_Toggle_Node_Base *A, Fl_Toggle_Node_Base *B)
{
	const char *a = ((Fl_Toggle_Node *)A)->label();
	const char *b = ((Fl_Toggle_Node *)B)->label();
	
	const char *tmp;
	int i;
	
	for (i=0;i<SORTFIELD;++i)
	{
		if ((tmp = strchr (a, '\t'))) a = tmp+1;
		if ((tmp = strchr (b, '\t'))) b = tmp+1;
	}
	return strcmp (a,b);
}

void cb_sortby_subject (Fl_Button */*b*/, void */*v*/)
{
	SORTFIELD = 0;
	br_folderindex->sort_tree (mysort);
	br_folderindex->redraw();
}

void cb_sortby_from (Fl_Button */*b*/, void */*v*/)
{
	SORTFIELD = 1;
	br_folderindex->sort_tree (mysort);
	br_folderindex->redraw();
}

void cb_sortby_date (Fl_Button */*b*/, void */*v*/)
{
	SORTFIELD = -1;
	update_folder();
}
