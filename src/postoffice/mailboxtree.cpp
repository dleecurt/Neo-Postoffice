#include <pthread.h>
#include <signal.h>
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

#include <setjmp.h>

#define locs(uk,nam) ((const char *) (_USELOCALE ? _CLOCALE->Get_char(nam) : (uk)))

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

extern Fl_Toggle_Tree	*TREE;

void cb_flipfolder (Fl_Widget *, void *);
void update_tree (Fl_Scroll *);
void cb_selectfolder (Fl_Widget *, void *);
void cb_selectfolder2 (Fl_Button *, void *);
void update_folder (void);

void tree_clear_newmessages (MailBox *);
void tree_new_messages (MailBox *, const char *);

class BoxData
{
	public:
						 BoxData (MailBox *, char *);
						~BoxData (void);

		MailBox			*mailbox;
		char			*foldername;
};

// Neo-PostOffice: the mailbox and folder of the tree node under the pointer
// (for dropping messages on a folder). False if it is not over a folder.
bool tree_folder_at_pointer (MailBox *&box, const char *&folder)
{
	if (!TREE || !TREE->parent() || !Fl::event_inside (TREE->parent())) return false;
	int depth, cy;
	Fl_Toggle_Node *n = (Fl_Toggle_Node *) TREE->Fl_Toggle_Tree_Base::find (Fl::event_y(), depth, cy);
	if (!n || !n->data()) return false;
	BoxData *B = (BoxData *) n->data();
	if (!B->mailbox || !B->foldername || !B->mailbox->GetView (B->foldername)) return false;
	box = B->mailbox;
	folder = B->foldername;
	return true;
}

BoxData::BoxData (MailBox *m, char *f)
{
	mailbox = m;
	if (f) foldername = strdupp (f);
	else foldername = strdupp ("empty");

}

BoxData::~BoxData (void)
{
	delete[] (foldername);
}

//===============================================================================
// CALLBACK		cb_flipfolder
// SENDER		n/a
// SYNOPSIS		deprecated (FIXME)
//===============================================================================

void cb_flipfolder (Fl_Widget */*w*/, void *d)
{
	long idx = (long) d;
	int state = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (MailBoxList[idx]->Name)->Get_int ("open");
	dprintf ("state: postoffice/mailboxes/%s/open = %i\n", MailBoxList[idx]->Name, state);
	if (state) PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (MailBoxList[idx]->Name)->Set_int ("open", 0); 
	else PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (MailBoxList[idx]->Name)->Set_int ("open", 1); 
	//state = 1-state;
	MailBoxList[idx]->Open = (state);
	update_tree(current_boxlist);
}

//===============================================================================
// FUNCTION		update_tree
// SYNOPSIS		Creates the folderlist Fl_Toggle_Tree data from the currently
//				available mailboxes and their views.
//
// ARGUMENTS	scr		(Fl_Scroll *)	Group the tree should be a part of.
//
// RETURNS		function returns no data
//===============================================================================

void update_tree (Fl_Scroll *scr)
{
	int i;
	int x;
//	Fl_Button *bt;
//	Fl_ToggleNode *node;
	char buf[512];
	int first = 0;

	// Remember which mailboxes are open, so rebuilding the tree (when a
	// mailbox adds a folder by itself) does not close them.
	if (TREE)
	{
		int k = 0;
		for (Fl_Toggle_Node_Base *n = TREE->first(); n && MailBoxList[k]; n = n->next_node(), ++k)
		{
			MailBoxList[k]->Open = n->is_open();
			PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (MailBoxList[k]->Name)->Set_int ("open", n->is_open());
		}
	}

	i = scr->children()-1;
	while (i>=2)
	{
		scr->remove(scr->child(i-2));
		i--;
	}

	x = 0;

	scr->begin();

	TREE = new Fl_Toggle_Tree (2, scr->y()+2, scr->w() - 4, 10);
	TREE->textsize (10);
	TREE->selection_color (246 /*147*/);
	TREE->selection_label_color (FL_BLACK);
	TREE->edit_on_reselect (0);
	scr->resizable (TREE);
	
	//Fl_Button *b = new Fl_Button(2,scr->y()+2,3,3);
	//b->box(FL_NO_BOX);
	
	while (MailBoxList[x])
	{
		
		if (MailBoxList[x+1]) MailBoxList[x]->Open = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (MailBoxList[x]->Name)->Get_int ("open");
		{
			Fl_Toggle_Node *nod;
			
			sprintf (buf, "%s (%d)", MailBoxList[x]->Name, MailBoxList[x]->Count);
			nod = TREE->add_next (buf, 1, &pixmap_folder, NULL /*(void *) MailBoxList[x] */);
			if (MailBoxList[x]->Open) TREE->open (nod);
			else TREE->close (nod);
			
		}
		first = 1;
		{
			MailView *V = MailBoxList[x]->VFirst;
			while (V)
			{
				if (first)
				{
					first = 0;
					TREE->add_sub (V->Name, 0, &pixmap_mbox, (void *) new BoxData (MailBoxList[x], V->Name));
				}
				else TREE->add_next (V->Name, 0, &pixmap_mbox, (void *) new BoxData (MailBoxList[x], V->Name));
				V = V->Next;
			}
			TREE->traverse_up ();
		}
		++x;
	}
	TREE->callback ((Fl_Callback *) cb_selectfolder);
	TREE->update_height();
	TREE->alternate_color(TREE->color());
	TREE->trim_color (TREE->color());
	scr->end();
	scr->redraw();
	Fl::flush();
}

//===============================================================================
// CALLBACK		cb_selectfolder
// SENDER		folderlist tree
// SYNOPSIS		Makes a new folder/view the current one, redraws the indexview.
//===============================================================================

void cb_selectfolder (Fl_Widget */*b*/, void */*v*/)
{
	if (!TREE->selected()) return;
	BoxData *box = (BoxData *) TREE->selected()->data();

	TREE->redraw();
	Fl::flush();

	dprintf ("callback %p\n", (void *) box);

	if (box && currentfolder[0])
	{
		dprintf ("click: cf=<%s> fn=<%s> cmb=<%p> mb=<%p>\n", currentfolder, box->foldername, (void *) currentmailbox, (void *) box->mailbox);
		if ((strcmp (currentfolder, box->foldername) == 0) && (currentmailbox == box->mailbox))
		{
			if ((time (NULL) - lastclick) == 0)
			{
				cb_check_mail ((Fl_Button *) NULL, NULL);
				//update_folder();
			}
			lastclick = time (NULL);
			
		}
		else
		{
			snprintf (currentfolder, sizeof (currentfolder), "%s", box->foldername);
			currentmailbox = (MailBox *) box->mailbox;
			//update_tree (current_boxlist);
			update_folder();
			lastclick = time (NULL);
		}
	}
}

void cb_selectfolder2 (Fl_Button */*b*/, void */*v*/)
{
	//cb_selectfolder ((Fl_Button *)v, NULL);
}

void tree_clear_newmessages (MailBox *M)
{
	int depth = 0;
	Fl_Toggle_Node *X = (Fl_Toggle_Node *) TREE->traverse_start();
	while (X)
	{
		if (X->data() != NULL)
		{
			BoxData *box = (BoxData *) X->data();
			if (box->mailbox == M)
			{
				X->color(FL_BLACK);
			}
		}
		X = TREE->traverse_forward (0, depth);
	}
	TREE->redraw();
}

void tree_new_messages (MailBox *M, const char *foldername)
{
	int depth = 0;
	bool doredraw = false;
	Fl_Toggle_Node *X = (Fl_Toggle_Node *) TREE->traverse_start();
	while (X)
	{
		if (X->data() != NULL)
		{
			BoxData *box = (BoxData *) X->data();
			if ((box->mailbox == M) && (strcmp (foldername, box->foldername) == 0))
			{
				if (X->color() != FL_BLUE)
				{
					X->color(FL_BLUE);
					doredraw = true;
				}
			}
		}
		X = TREE->traverse_forward (0, depth);
	}
	if (doredraw) TREE->redraw();
}

