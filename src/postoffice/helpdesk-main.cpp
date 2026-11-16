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
#include "stringtoken.h"

#include "debug.h"
#include "status.h"
#include "style.h"
#include "folderdata.h"


StorageList STORAGE;

int VERBOSE;

#include <FL/Fl_JPEG_Image.H>

#define locs(uk,nam) ((const char *) (_USELOCALE ? _CLOCALE->Get_char(nam) : (uk)))

int				  xwidths[6] = {365,175,115,0};
int				  hdrwidths[3] = {32,100,0};

class				 MailBox *MailBoxList[16];
class				 MailBox *OUTBOX;
Fl_Scroll 			*current_boxlist;
//void 				*dbhand;
char				 currentfolder[64];
MailBox 			*currentmailbox = NULL;

extern int			 mdi_current;
extern void			 mdi_init (void);
extern void			 mdi_title (const char *);
extern Fl_View		*mdi_views[8];

char				 currentfrom[8][256];
char				 currentlabel[8][256];
char				 currentsubject[8][256];
char				 currentreplyto[8][256];
char				 currentfollowup[8][256];
char				 currentmsgid[8][256];
char				 currentreferences[8][2048];
WordList			 currentmessage[8];
int					 currentbodystart[8];
int					 currentsigstart[8];
int					 unreadonly = 0;

int					 folderlist_fullsize = 0;
AsyncBuffer			 HTMLView(8192);

Var					 PREFERENCES;
WordList			 STATUS_BOXEN, STATUS_MSGS;
StringTokenList		 TOKEN;

extern Fl_Pixmap	 pixmap_folder;
extern Fl_Pixmap	 pixmap_folder_open;
extern Fl_Pixmap	 pixmap_mbox;
extern Fl_Pixmap	 pixmap_compose;
//extern Fl_Pixmap	 pixmap_mailappt;

#define SORT_THREAD 0x00
#define SORT_RECEIVE 0x01

int 				 _USELOCALE;
char 				*_LANGUAGE;
Var 				*_CLOCALE;
int					 _SORTMETHOD=0;

// Which field to sort by
int SORTFIELD = -1;


void addheader (const char *, const char *);
void cb_flipfolder (Fl_Widget *, void *);
void update_tree (Fl_Scroll *);
void cb_menu_untear (Fl_Widget *, void *);
void cb_folders_untear (Fl_Widget *, void *);
void cb_selectfolder (Fl_Widget *, void *);
void cb_selectfolder2 (Fl_Button *, void *);
void cb_selectmessage (Fl_Widget *, void *);
void cb_sendmessage (Fl_Window *, struct Composer_Window *);
void cb_discardmessage (Fl_Window *, struct Composer_Window *);
void cb_selectaddress (Fl_Browser *, void *);
void cb_pasteaddress (Fl_Button *, void *);
void cb_viewall (Fl_Menu_ *, void *);
void cb_viewunread (Fl_Menu_ *, void *);
void displaymessage (MailMessage *);
void update_folder (void);
void new_folderitem (MailMessage *);

void set_status (const char *);
void update_status (void);
void cb_exit (Fl_Window *, void *);

extern void start_wizard (void);

void extract_recipients (WordList *, char *);

void cb_folders_undrop (Fl_Button *, void *);

void tree_clear_newmessages (MailBox *);
void tree_new_messages (MailBox *, const char *);

int mysort (Fl_Toggle_Node_Base *, Fl_Toggle_Node_Base *);

Fl_Window 		*menu = (Fl_Window *) NULL;
Fl_Window 		*address = (Fl_Window *) NULL;
Fl_Toggle_Tree	*TREE = (Fl_Toggle_Tree *) NULL;
Fl_Toggle_Tree	*br_folderindex = (Fl_Toggle_Tree *) NULL;
CCache			 CCACHE;

JobList			 JOBQ;

int				  adr_widths[2] = {120,0};

//===============================================================================
// CALLBACK		cb_viewunread
// SENDER		"view" menu
// SYNOPSIS		Sets the display filter to show all messages in the view
//===============================================================================

void cb_viewall (Fl_Menu_ */*w*/, void */*b*/)
{
	unreadonly = 0;
	update_folder();
}

//===============================================================================
// CALLBACK		cb_viewunread
// SENDER		"view" menu
// SYNOPSIS		Sets the display filter to only show unread messages
//===============================================================================

void cb_viewunread (Fl_Menu_ */*w*/, void */*b*/)
{
	unreadonly = 1;
	update_folder();
}

//===============================================================================
// CALLBACK		cb_hdrfull
// SENDER		"view" menu
// SYNOPSIS		Opts for displaying all headers in the Fl_View
//===============================================================================

void cb_hdrfull (Fl_Menu_ */*w*/, void */*b*/)
{
	PREFERENCES.Set_char_tree ("postoffice/state/main/fullheaders", "1");
}

//===============================================================================
// CALLBACK		cb_hdrpart
// SENDER		"view" menu
// SYNOPSIS		Opts for only displaying relevant headers in the Fl_View
//===============================================================================

void cb_hdrpart (Fl_Menu_ */*w*/, void */*b*/)
{
	PREFERENCES.Set_char_tree ("postoffice/state/main/fullheaders", "0");
}

//===============================================================================
// CALLBACK		cb_delete_m
// SENDER		"message" menu (or [Delete] shortcut)
// SYNOPSIS		prototype stub
//===============================================================================

void cb_delete_m (Fl_Menu_*w, void *d)
{
	cb_delete ( (Fl_Button *)w, d);
}

//===============================================================================
// CALLBACK		cb_delete
// SENDER		toolbar button
// SYNOPSIS		attempts to delete the current selection from the local
// 				and possibly the remote database.
//===============================================================================

void cb_delete (Fl_Button */*w*/, void */*d*/)
{
	FolderData *D;
	Fl_Toggle_Node *S;
	
/*	Fl_Multi_Browser *xbr = (Fl_Multi_Browser *) br_folderindex; */

	//int i;
	// CLASS_DELETE

	S = br_folderindex->selected();
	if (S)
	{
		D = (FolderData *) S->data();
			currentmailbox->ToCollector->PostMessage (CLASS_DELETE, NO_SUBCLASS, D->msgid);
			currentmailbox->RemoveMessage (D->msgid);
	}
	else while ((S = br_folderindex->selection()))
	{
		D = (FolderData *) S->data();
			currentmailbox->ToCollector->PostMessage (CLASS_DELETE, NO_SUBCLASS, D->msgid);
			currentmailbox->RemoveMessage (D->msgid);
	}
	//currentmailbox->SaveMailBox();
	update_folder();
}

//===============================================================================
// CALLBACK		cb_refilter
// SENDER		n/a
// SYNOPSIS		stub (FIXME)
//===============================================================================

void cb_refilter (Fl_Menu_ */*w*/, void */*d*/)
{
//	FolderData *D;
//	MailMessage *M;3
//	int i;
}

//===============================================================================
// CALLBACK		cb_select_all
// SENDER		n/a
// SYNOPSIS		stub (FIXME)
//===============================================================================

void cb_select_all (Fl_Menu_ */*w*/, void */*d*/)
{
	/*
	int i;
	Fl_Multi_Browser *xbr = (Fl_Multi_Browser *) br_folderindex;
	
	for (i=0;i<br_folderindex->size();++i)
	{
		xbr->select (i+1);
	}
	br_folderindex->redraw(); */
}

//===============================================================================
// CALLBACK		cb_menu_tearoff
// SENDER		toolbar handle
// SYNOPSIS		moves the toolbar into its own window
//===============================================================================

void cb_menu_tearoff (Fl_Button */*b*/, void */*d*/)
{
	gr_buttonbar->hide();
	tile_all->resize (tile_all->x(),20,tile_all->w(),tile_all->h()+20);
	if (!menu) menu = make_menuwindow();
	menu->show();
	menu->callback ((Fl_Callback *) cb_menu_untear);
}

//===============================================================================
// CALLBACK		cb_menu_untear
// SENDER		toolbar window closebox
// SYNOPSIS		moves the toolbar back into the main window
//===============================================================================
void cb_menu_untear (Fl_Widget */*w*/, void */*d*/)
{
	tile_all->resize (tile_all->x(),40,tile_all->w(), tile_all->h()-20);
	gr_buttonbar->show();
	gr_buttonbar->redraw();
	gr_buttons->redraw();
	tile_lists->redraw();
	menu->hide();
}

int owidth = 0;
int oheight = 0;
int oldx = 0;
Fl_Window *wfolders = (Fl_Window *) NULL;

//===============================================================================
// CALLBACK		cb_folders_tearoff
// SENDER		folderlist toolbar handle
// SYNOPSIS		moves the folderlist into its own window
//===============================================================================

void cb_folders_tearoff (Fl_Button */*b*/, void */*d*/)
{
	owidth = gr_folderlist->w();
	oheight = gr_folderlist->h();
	gr_folderlist->hide();
	if (!folderlist_fullsize)
	{
		oldx = gr_folderindex->x();
		gr_folderindex->position(0,gr_folderindex->y());
		gr_folderindex->size(gr_folderindex->w()+oldx,gr_folderindex->h());
		gr_folderlist->size (0,oheight);
		tile_lists->redraw();
	}
	else
	{
		oldx = tile_all->x();
		((Fl_Widget *)tile_all)->position(0,tile_all->y());
		tile_all->size (tile_all->w()+oldx, tile_all->h());
		gr_folderlist->size (0,oheight);
		tile_lists->redraw();
	}
	wfolders->show();
	wfolders->callback ((Fl_Callback *) cb_folders_untear);
	current_boxlist = br_winfolderlist;
	update_tree (current_boxlist);
}

//===============================================================================
// CALLBACK		cb_folders_tearoff
// SENDER		folderlist window close box
// SYNOPSIS		moves the folderlist back into the main window
//===============================================================================

void cb_folders_untear (Fl_Widget */*w*/, void */*d*/)
{
	if (!folderlist_fullsize)
	{
		gr_folderlist->size (owidth,gr_folderindex->h());
		gr_folderindex->position(oldx,gr_folderindex->y());
		gr_folderindex->size(gr_folderindex->w()-oldx,gr_folderindex->h());
	}
	else
	{
		gr_folderlist->size (owidth,tile_all->h());
		((Fl_Widget *)tile_all)->position (oldx, tile_all->y());
		tile_all->size (tile_all->w()-oldx, tile_all->h());
	}
	gr_folderlist->show();
	tile_lists->redraw();
	tile_all->redraw();
	wfolders->hide();
	current_boxlist = br_folderlist;
	update_tree (current_boxlist);
}

int lwidth = 0;
int lwidth2 = 0;

void cb_updatewidth (Fl_Tile */*t*/, void */*d*/)
{
}

void ti_widthcheck (void */*x*/)
{
	xwidths[0] = bt_from->x() - bt_thread->x();
	xwidths[1] = bt_date->x() - bt_from->x();
	int delta = xwidths[0] - lwidth;
	if ((xwidths[0] != lwidth)||(xwidths[1]!=lwidth2))
	{
		if (br_folderindex)
		{
			br_folderindex_->redraw();
			dprintf ("delta = %i w=%i\n", delta, br_folderindex->w());
			br_folderindex->size (gr_folderindex->w(), br_folderindex->h());
		}
		if (TREE)
		{
			TREE->size (gr_folderlist->w(), TREE->h());
			TREE->redraw();
		}
		lwidth = xwidths[0];
		lwidth2 = xwidths[1];
		Fl::add_timeout(0.02,ti_widthcheck);
	}
	else
	{
		Fl::add_timeout(0.5,ti_widthcheck);
	}
}

ITCPipe *PIPE;

int __ARGC;
char **__ARGV;

int main (int argc, char *argv[])
{
	__ARGC = argc;
	__ARGV = argv;
	
	char buf[512];
	Fl_Toggle_Tree *o;
	Fl_Window *W, *spl;
	int i;//,j;
	PIPE = new ITCPipe;
	MailView *Vw;
	MailMessage *Ms;
	struct stat mystat;

	char *vrb = getenv ("VERBOSE");
	if (vrb) VERBOSE = atoi (vrb);

	signal (SIGPIPE, SIG_IGN);
	//Fl::visual(FL_RGB);
	
	spl = make_splash_screen();

    int flw = Fl::w();
    int flh = Fl::h();
    if (((double)flw/(double)flh) >= 2.0) flw = flw/2;

	spl->position ((flw-spl->w())/2,(flh-spl->h())/2);

	sprintf (buf, "%s/.postoffice", getenv ("HOME"));
	if (stat (buf, &mystat)!=0)
	{
		mkdir (buf, 0700);
		start_wizard();
	}
	else
	{
		sprintf (buf, "%s/.postoffice/resource.rc", getenv ("HOME"));
		if (stat (buf, &mystat)!=0)
		{
			start_wizard();
		}
	}

	spl->show();
	sl_splash->value (10);
	sl_splash->label ("Loading preferences");
	Fl::wait(0.01); Fl::flush(); Fl::wait(0.01);

	sprintf (buf, "%s/.postoffice/resource.rc", getenv ("HOME"));
	PREFERENCES.Load (buf);
	_USELOCALE = atoi (PREFERENCES.Get_char_tree ("postoffice/international/uselocale"));
	_LANGUAGE = PREFERENCES.Get_char_tree ("postoffice/international/language");
	_CLOCALE = PREFERENCES.Get_collection ("locale")->Get_collection (_LANGUAGE)->Get_collection ("postoffice");
	initialize_styles();
	
	sl_splash->value(20);
	sl_splash->label (locs("Loading mailboxes","loading_mailboxes"));
	Fl::wait(0.01);  Fl::flush(); Fl::wait(0.01);
	
	W = make_window();
	
	mdi_init();
	
	Fl::add_timeout(0.1,ti_widthcheck);

	
	set_status (locs("Ready for action...","ready_for_action")); //bt_status->redraw();

   	//br_message->column_widths (hdrwidths);
	
	Var *VBoxes = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes");
	WordList *MBoxes = VBoxes->GatherNames();

	int cnt=0;
	
	for (i=0;i<MBoxes->Count;++i)
	{
		if (strcasecmp (MBoxes->Get (i), "outbox"))
		{
			sl_splash->value(30+((60*i)/MBoxes->Count));
			sprintf (buf, locs("Opening mailbox: %s","opening_mailbox"), MBoxes->Get(i));
			sl_splash->label (buf);
			Fl::wait(0.01);  Fl::flush(); Fl::wait(0.01);
			dprintf ("[startup] Creating Mailbox: %s\n", MBoxes->Get(i));
			
			struct InitArgument *_mb_init = new InitArgument;
			
			_mb_init->Pipe = PIPE;
			_mb_init->Name = MBoxes->Get(i);
			
			InitThread *thr = new InitThread;
			
			thr->Result = NULL;
			thr->Run ((void *) _mb_init);
			
			while (thr->Started)
			{
				Fl::wait(0.01);
			}
			if (thr->Result)
				MailBoxList[cnt++] = thr->Result;
			
			delete (thr);
			delete (_mb_init);
		}
	}
	MailBoxList[cnt++] = OUTBOX = new MailBox (PIPE);
	MailBoxList[cnt] = (MailBox *) NULL;
	delete (MBoxes);

	sl_splash->value(100);
	sl_splash->label (locs("Starting Program","starting_program"));
	Fl::wait(0.01);  Fl::flush(); Fl::wait(0.01);


	current_boxlist = br_folderlist;
	update_tree (current_boxlist);
	
	if (!wfolders) wfolders = make_folderlist_window();

	win_splash->hide();
	
	if (strcasecmp (PREFERENCES.Get_char_tree ("postoffice/state/main/toolbar"), "small") == 0)
	{
		cb_menu_shrink ((Fl_Button *) NULL, NULL);
	}

	if (strcasecmp (PREFERENCES.Get_char_tree ("postoffice/state/main/mailboxtile"), "full") == 0)
	{
		cb_folders_dropdown ((Fl_Button *) NULL, NULL);
	}
	else
	{
		cb_folders_dropdown ((Fl_Button *) NULL, NULL);
		cb_folders_undrop ((Fl_Button *) NULL, NULL);
	}
	W->callback ( (Fl_Callback *) cb_exit, NULL);

	WordList *Gm = PREFERENCES.Get_collection ("postoffice")->Get_collection ("state")->Get_collection ("main")->Get_wordlist ("geometry");
	if (Gm->Count == 4)
	{
		W->resize (Gm->GetInt(0), Gm->GetInt(1), Gm->GetInt(2), Gm->GetInt(3));
	}

	br_folderindex_->begin();
	br_folderindex = new Fl_Toggle_Tree (br_folderindex_->x()+2,br_folderindex_->y()+2,br_folderindex_->w()-22,12);
	br_folderindex->selection_color (246 /*147*/);
	br_folderindex->selection_label_color (FL_BLACK);
	br_folderindex->edit_on_reselect (0);
	br_folderindex->alternate_color(br_folderindex->color());
	br_folderindex->trim_color (br_folderindex->color());
	br_folderindex->draw_lines (1);
	br_folderindex->callback ((Fl_Callback *) cb_selectmessage);
	br_folderindex_->end();
	br_folderindex_->resizable (br_folderindex);
	br_folderindex_->type (Fl_Scroll::VERTICAL);
	
	W->show (argc, argv);

	o = br_folderindex;
    	o->column_widths (xwidths);

	sprintf (currentfolder, "Inbox");
	
	ITCMessage *M = NULL;
	MailMessage *MS = NULL;
	char *errorstring = NULL;

	double wait_time = 0.7;

	int redraw_backlog = 0;

	while (1)
	{
		Fl::wait(wait_time);
		if ((PIPE->Queue() > 2) && (wait_time > 0.02)) wait_time = 0.6 * wait_time;
		if ((PIPE->Queue() < 1) && (wait_time < 0.7)) wait_time = 1.2 * wait_time;
		if (wait_time < 0.02) wait_time = 0.02;
		if ((M = PIPE->GetNextMessage()))
		{
			CCacheNode *CC;
			
			switch (M->Class)
			{
				case CLASS_GOTBODY :
					//currentmailbox->Lock->LockR();
					MS = currentmailbox->Find (M->Message->Get(0));
					//currentmailbox->Lock->UnlockR();
					if (MS) displaymessage (MS);
					break;

				case CLASS_CHECKEDGROUP :
					//update_folder();
					break;

				case CLASS_NEWARTICLE:
					if (currentmailbox)
					{
						Ms = currentmailbox->Find (M->Message->Get(1));
						if (!Ms)
						{
							for (int k=0; k<16; ++k)
							{
								if (MailBoxList[k])
								{
									Ms = MailBoxList[k]->Find (M->Message->Get(1));
									if (Ms) k=16;
								}
								else k=16;
							}
							if (!Ms)
							{
								Ms = OUTBOX->Find (M->Message->Get(1));
							}
						}
						if (Ms)
						{
							tree_new_messages (Ms->Parent, M->Message->Get(0));
							if ((currentmailbox == Ms->Parent) && 
							    (strcasecmp (currentfolder, M->Message->Get(0)) == 0))
							{
								Vw = currentmailbox->GetView (currentfolder);
								if (Vw)
								{
									++redraw_backlog;
									redraw_backlog = redraw_backlog & 0x07;
									new_folderitem (Ms);
									if ((PIPE->Queue() <3) || (!redraw_backlog))
									{
										br_folderindex->update_height();
										br_folderindex->redraw();
									}
								}
							}
						}
					}
					//Fl::wait(0.02);
					break;

				case CLASS_REMARTICLE:
					if (currentmailbox)
					{
						dprintf ("remarticle event: %s,%s\n", M->Message->Get(0), M->Message->Get(1));
						if (strcasecmp (currentfolder, M->Message->Get(0)) == 0)
						{
							dprintf ("currentfoldermatch\n");
							if ((CC = CCACHE.FindMsgid (M->Message->Get(1))))
							{
								dprintf ("ccmatch\n");
								if (CC->node)
								{
									dprintf ("hasnode %p\n", (void *) CC->node);
									br_folderindex->remove (CC->node);
									//br_folderindex->update_height ();
									//br_folderindex_->redraw();
									CC->node = NULL;
									br_folderindex->update_height();
									br_folderindex->redraw();
								}
							}
						}
					}
					break;

				case CLASS_NEWFOLDER:
					// A mailbox (the SQL mailbox) created folders by itself or
					// lost messages: rebuild the tree for the folders and counts.
					update_tree (current_boxlist);
					break;

				case CLASS_GONE:
					// A message was deleted on the server (by another client).
					for (int k=0; k<16 && MailBoxList[k]; ++k)
					{
						MailBox *B = MailBoxList[k];
						if (strcmp (B->Name, M->Message->Get(0)) != 0) continue;
						if (B->Find (M->Message->Get(1)))
						{
							B->RemoveMessage (M->Message->Get(1));
							if (B == currentmailbox) update_folder();
						}
						break;
					}
					break;

				case CLASS_ERROR :
					errorstring = new char [strlen (M->Message->Get(0)) + strlen (M->Message->Get(1)) + 64];
					sprintf (errorstring, "Error in [%s]\n%s", M->Message->Get(0), M->Message->Get(1));
					if (M->Subclass == SUB_ERROR_CRITICAL) fl_alert ("%s", errorstring);
					eprintf ("\n*** ERROR ***\n%s\n", errorstring);

					delete[] errorstring;
					break;

				case CLASS_STATUS:
					update_status();
					break;
			}
			delete (M);
		}
	}
}

time_t lastclick = 0;

//===============================================================================
// CALLBACK		cb_checkmail_m
// SENDER		menubar or shortcut
// SYNOPSIS		prototype stub
//===============================================================================

void cb_check_mail_m (Fl_Menu_ *m, void *blah)
{
	cb_check_mail ((Fl_Button *)m, blah);
}

//===============================================================================
// CALLBACK		cb_checkmail
// SENDER		toolbar
// SYNOPSIS		instructs the current mailbox' collector to check for new
//				messages, possibly within the context of the selected folder.
//===============================================================================

void cb_check_mail (Fl_Button */*m*/, void */*blah*/)
{
	if (currentmailbox) 
	{
		currentmailbox->ToCollector->PostMessage (CLASS_CHECKGROUP, NO_SUBCLASS, currentfolder);
		tree_clear_newmessages (currentmailbox);
	}
	//if (currentmailbox) currentmailbox->Handler->CheckGroup ("inbox");
}

//===============================================================================
// CALLBACK		cb_expire
// SENDER		menubar or shortcut
// SYNOPSIS		attempts to expire messages in the current mailbox
//===============================================================================

void cb_expire (Fl_Menu_ */*m*/, void */*blah*/)
{
	if (currentmailbox)
	{
		currentmailbox->ExpireLocal();
		currentmailbox->ToCollector->PostMessage (CLASS_EXPIRE, NO_SUBCLASS, "*");
	}
}

//===============================================================================
// CALLBACK		cb_thr_open
// SENDER		menubar
// SYNOPSIS		opens all leafs in the toggletree that makes up the folder index
//===============================================================================

void cb_thr_open (Fl_Menu_ */*b*/, void */*v*/)
{
	Fl_Toggle_Node_Base *x;
	x = br_folderindex->traverse_start();
	while (x)
	{
		if (((Fl_Toggle_Node *)x)->can_open()) br_folderindex->open ((Fl_Toggle_Node *) x);
		x = br_folderindex->traverse_forward();
	}
	br_folderindex->update_height();
	br_folderindex_->redraw();
}

//===============================================================================
// CALLBACK		cb_thr_close
// SENDER		menubar
// SYNOPSIS		closes all leafs in the toggletree that makes up the folder index
//===============================================================================

void cb_thr_close (Fl_Menu_ */*b*/, void */*v*/)
{
	Fl_Toggle_Node_Base *x;
	x = br_folderindex->traverse_start();
	while (x)
	{
		if (((Fl_Toggle_Node *)x)->can_open()) br_folderindex->close ((Fl_Toggle_Node *) x);
		x = br_folderindex->traverse_forward();
	}
	br_folderindex->update_height();
	br_folderindex_->redraw();
}


//===============================================================================
// CALLBACK		cb_selectmessage
// SENDER		index view
// SYNOPSIS		attempts to display the message that was clicked in the index
//				view. If it's not yet completely available, the mailbox' handler
//				is instructed to go fetch it.
//===============================================================================

void cb_selectmessage (Fl_Widget */*w*/, void */*v*/)
{
	if (!br_folderindex->selected()) return;
	
	char			*msgid =  ((FolderData *) br_folderindex->selected()->data())->msgid;
	
	
	sld_progress->value (5);
	//set_status (locs("Downloading message...","downloading_message")); Fl::wait(0.005);

	if (currentmailbox)
	{
		if (strlen (currentfolder))
		{
			MailMessage *M = currentmailbox->Find (msgid);
			if (M)
			{
				if (M->Flags & MMFL_HAVEBODY)
				{
					//printf ("body cached\n");
					if (! (M->Flags & MMFL_INRAM))
						M->LoadBody();

					displaymessage (M);
				}
				else
				{
					currentmailbox->ToCollector->PostMessage (CLASS_GETBODY, NO_SUBCLASS, M->MsgID);
					//printf ("posted message for \"%s\"\n", M->MsgID);
				}
			}  else eprintf ("fail: message \"%s\" not found\n", msgid);

		} else eprintf ("fail: no current folder\n");
	} else eprintf ("fail: no current mailbox\n");
}

//===============================================================================
// FUNCTION		addheader
// SYNOPSIS		Adds a header line to the currently viewed message
//
// ARGUMENTS	headername	(char *)	The Name of the header
//				headervalue (char *)	The Value of the header
//
// RETURNS		Function returns no data.
//===============================================================================

void addheader (const char *headername, const char *headervalue)
{
	mdi_views[mdi_current]->add_header ((char *) headername, (char *) headervalue);
}

//===============================================================================
// FUNCTION		displaymessage
// SYNOPSIS		Puts a fully loaded MailMessage into the Fl_View.
//
// ARGUMENTS	M		(MailMessage *)	The message to be processed
//
// RETURNS		Function returns no data.
//===============================================================================

void displaymessage (MailMessage *M)
{
	char			 buf[514];
	char			*body;
	char			*wrappoint = NULL;
	//WordList		*dbr;
	char			*nval;
	char			*xval;
	int				 i;
	char			*ln, *xln, *oln;
	int				 insignature = 0;
	unsigned long	 totalsize = 0;
	
	bool			 ms_outlook_shite = false;
	
	MailPart		*P;

	// clear status information for the current mdi window

	currentfrom[mdi_current][0] = 0;
	currentsubject[mdi_current][0] = 0;
	currentreplyto[mdi_current][0] = 0;
	currentmsgid[mdi_current][0] = 0;
	currentmessage[mdi_current].Clear();
	currentreferences[mdi_current][0] = 0;
	currentfollowup[mdi_current][0] = 0;

	// set message-id for current mdi window

	strncpy (currentmsgid[mdi_current], M->MsgID, 255);
	currentmsgid[mdi_current][255] = 0;

	// clear the textview

	mdi_views[mdi_current]->clear();
	mdi_views[mdi_current]->add_textline (" ");

	// verify that the headers are loaded

	if (M->HeaderNames->Count == 0) M->LoadHeaders();
	
	// load the message body
	
	if (! (M->Flags & MMFL_INRAM)) M->LoadBody();
	
	// mark the message as read
	
	M->Envelope->SetInt (ENVELOPE_I_READ, 1);

	// set the title for the mdi window

	const char *t_winfrom = M->Envelope->Get (ENVELOPE_NAMEFROM);
	if (strlen (t_winfrom)<1) t_winfrom = M->Envelope->Get (ENVELOPE_MAILFROM);
	mdi_title (t_winfrom);

	// display the headers

	for (i=0;i<M->HeaderNames->Count;++i)
	{
		nval = M->HeaderNames->Get (i);
		xval = M->HeaderValues->Get (i);

		if (strcasecmp (nval, "From") == 0)
		{
			addheader (locs("From","From"), xval);
			strncpy (currentfrom[mdi_current], xval, 255);
			currentfrom[mdi_current][255] = 0;
		}
		else if (strcasecmp (nval, "To") == 0)
		{
			addheader (locs("To","To"), xval);
		}
		else if (strcasecmp (nval, "References") == 0)
		{
			strncpy (currentreferences[mdi_current], xval, 1023);
			currentreferences[mdi_current][1023] = 0;
			if (atoi (PREFERENCES.Get_char_tree ("postoffice/state/main/fullheaders"))) addheader (nval, xval);
		}
		else if (strcasecmp (nval, "Cc") == 0)
		{
			addheader (locs("Cc","Cc"), xval);
		}
		else if (strcasecmp (nval, "Date") == 0)
		{
			addheader (locs("Date","Date"), xval);
		}
		else if (strcasecmp (nval, "Subject") == 0)
		{
			addheader (locs("Subject","Subject"), xval);
			strncpy (currentsubject[mdi_current], xval, 255);
			currentsubject[mdi_current][255] = 0;
		}
		else if (strcasecmp (nval, "Newsgroups") == 0)
		{
			addheader (locs("Newsgroups","Newsgroups"), xval);
			if (strlen (currentfollowup[mdi_current]) == 0)
			{
				strncpy (currentfollowup[mdi_current], xval, 255);
				currentfollowup[mdi_current][255] = 0;
			}
		}
		else if (strcasecmp (nval, "Organization") == 0)
		{
			addheader (locs("Organization","Organization"), xval);
		}
		else if (strcasecmp (nval, "Reply-To") == 0)
		{
			addheader (locs("Reply-To","Reply-To"), xval);
			strncpy (currentreplyto[mdi_current], xval, 255);
			currentreplyto[mdi_current][255] = 0;
		}
		else if (strcasecmp (nval, "Followup-To") == 0)
		{
			addheader (locs("Followup-To","Followup-To"), xval);
			strncpy (currentfollowup[mdi_current], xval, 255);
			currentfollowup[mdi_current][255] = 0;
		}
		else if (strcasecmp (nval, "BCC") == 0)
		{
			addheader (locs("BCC","BCC"), xval);
		}
		else if (atoi (PREFERENCES.Get_char_tree ("postoffice/state/main/fullheaders")))
		{
			addheader (nval, xval);
		}
		if (strcasecmp (nval, "X-MimeOLE") == 0) // Outlook - assume everything is fucked up
		{
			ms_outlook_shite = true;
		}
	}

	// add an empty line

	mdi_views[mdi_current]->add_textline (" ");

	// set the default reply options

	if ((strlen (currentreplyto[mdi_current]) == 0) && (strlen (currentfollowup[mdi_current]) == 0))
		strcpy (currentreplyto[mdi_current], currentfrom[mdi_current]);

	// find the relevant style info

	Style *sbody = (Style *) _STYLES.Root->Find ("messagebody:text");
	Style *squoted = (Style *) _STYLES.Root->Find ("messagebody:quoted-text");
	Style *ssig = (Style *) _STYLES.Root->Find ("messagebody:signature");
	Style *sascii = (Style *) _STYLES.Root->Find ("messagebody:ascii");
	
	bool in_ascii = false;

	// display the message body

	for (int ik=0;ik<M->Count;++ik)
	{
		P = M->GetPart (ik);
		
		if (P)
		{
			totalsize += P->ContentSize;
			WordList CTH;

			// Analyze this part's content type

			dprintf ("contenttype: %s\n", P->ContentType);
			CTH.MakeGenericQuoted (P->ContentType, ';');
			
			if (CTH.Count > 1)
			{
				WordList Wtmp;
				
				for (int idx=0; idx<CTH.Count;++idx)
				{
					char *tmpnam;
					Wtmp.MakeGenericQuoted (CTH.Get(idx), '=');
					tmpnam = Wtmp.Get(0);
					while (*tmpnam==' ') ++tmpnam;
					if ((strcasecmp (tmpnam, "format") == 0) && (wild_match ("*flowed*", Wtmp.Get(1))))
					{
						dprintf ("stupid outlook format=flowed crap found\n");
						ms_outlook_shite = true;
					}
				}
			}
			
			if (strcasecmp (CTH.Get(0), "text/plain") == 0)
			{
				body = P->Contents;
				dprintf ("currentmessage.Clear()\n");
				currentmessage[mdi_current].Clear();
				dprintf ("currentmessage.MakeGeneric()\n");
				currentmessage[mdi_current].MakeGeneric (body, '\n', 0);
				dprintf ("for (i,0...%i)\n", currentmessage[mdi_current].Count);
				for (i=0;i<currentmessage[mdi_current].Count;++i)
				{
					xln = currentmessage[mdi_current].Get(i);
					if (ms_outlook_shite)
					{
						oln = chopwincrap (xln);
						ln = oln;
					}
					else
					{
						oln = strdupp (xln);
						ln = oln;
					}
					while (ln)
					{
						if (strlen (ln) < 2) in_ascii = false;
						int wrapped = 0;
						if (strlen (ln) > 100)
						{
							dprintf ("wrap\n");
							wrapped = 1;
							wrappoint = strchr (ln+60, ' ');
							if (!wrappoint) wrappoint = ln+79;
							*wrappoint = 0;
						}
						if ((!insignature) && (ln[0] != '>'))
						{
							dprintf ("sigcheck\n");
							if (strcmp (ln, "-- ") == 0) insignature=1;
							else if (!strstr (body, "-- ")) // only look for alternate sig.seperators if there is no compliant one
							{
								if (strcmp (ln, "---") == 0) insignature=1;
								else if (strcmp (ln, "--") == 0) insignature=1;
								else if (strstr (ln, "----------")) insignature=1;
								else if (strstr (ln, "_____")) insignature=1;
								else if (strstr (ln, "_ _ _")) insignature=1;
								else if (strstr (ln, "====")) insignature=1;
								else if (strstr (ln, "-=-=")) insignature=1;
								if ((insignature) && ((currentmessage[mdi_current].Count - i) > 18)) insignature = 0;
							}
						}
						if (!insignature) currentsigstart[mdi_current] = i+1;
						if (!insignature)
						{
							if ( ( (strlen(ln)>4) && (strstr (ln+4, "    ")) ) || 
							    strchr (ln, '{') || strchr (ln,'}') || 
								(ln[0] == '#') ||
								strchr (ln, '\t') || strchr (ln, '|'))
							{
								in_ascii = true;
							}
							
							if (strncmp (ln, ">From", 5) == 0) ++ln;
							
							if (ln[0] == '>')
							{
								mdi_views[mdi_current]->add_textline (ln,squoted->color,squoted->font,squoted->size);
							}
							else
							{
								if (!in_ascii) mdi_views[mdi_current]->add_textline (ln,sbody->color,sbody->font,sbody->size);
								else mdi_views[mdi_current]->add_textline (ln,sascii->color,sascii->font,sascii->size);
							}
						}
						else mdi_views[mdi_current]->add_textline (ln,ssig->color,ssig->font,ssig->size);
						if (wrapped)
						{
							ln = wrappoint+1;
							*wrappoint = ' ';
						}
						else ln = (char *) NULL;
					}
					if (oln) delete[] (oln);
				}
			}
			else
			{
				bool uuencoded = false;
				const char *mimetype = CTH.Get(0);
				if (strcasecmp (mimetype, "text/uuencoded") == 0) // remap by file extension
				{
					uuencoded = true;
					if (P->FileName)
					{
						if (wild_match ("*.jpg", P->FileName) || wild_match ("*.jpeg", P->FileName))
						{
							mimetype = "image/jpeg";
						}
						else if (wild_match ("*.gif", P->FileName)) mimetype = "image/gif";
						else if (wild_match ("*.txt", P->FileName)) mimetype = "text/plain";
					}
				}
				if (strcasecmp (mimetype, "image/jpeg") == 0)
				{
					unsigned long size = 0;
					unsigned char *jpeg_data = NULL;

					int jid = JOBQ.NewJob ("Decode");
					JOBQ.Stat (jid, "Decoding attachments");
					update_status(); Fl::wait (0.05);
					if (uuencoded)
					{
						unsigned char *uu = (unsigned char *) strstr (P->Contents, "begin");
						if (uu)
						{
							uu = (unsigned char *) strstr ((const char *) uu, "\n");
							if (uu)
							{
								jpeg_data = (unsigned char *) UUdecode ((unsigned char *) uu+1, P->ContentSize, &size);
							}
						}
					}
					else jpeg_data = (unsigned char *) Base64 ((unsigned char *) P->Contents, P->ContentSize, &size);
					// Decode in memory with FLTK's JPEG support (no temporary file).
					if (jpeg_data && size > 0 && size < 0x7fffffffUL)
					{
						Fl_JPEG_Image img (NULL, jpeg_data, (int) size);
						int iw = img.data_w(), ih = img.data_h(), id = img.d(), ild = img.ld();
						if (img.fail() || iw <= 0 || ih <= 0 || !img.count() || (id != 1 && id != 3))
						{
							mdi_views[mdi_current]->add_textline ((char *) "[Mangled image]");
						}
						else
						{
							// Fl_View wants packed 8-bit RGB and takes ownership of it.
							const unsigned char *src = (const unsigned char *) img.data()[0];
							unsigned char *bitmap = new unsigned char[3 * (size_t) iw * ih + 1];
							if (ild == 0) ild = iw * id;
							for (int y = 0; y < ih; ++y)
							{
								const unsigned char *in = src + (size_t) y * ild;
								unsigned char *out = bitmap + (size_t) y * iw * 3;
								for (int x = 0; x < iw; ++x)
								{
									if (id == 3) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; }
									else { out[0] = out[1] = out[2] = in[0]; }
									in += id; out += 3;
								}
								if (!(y & 0x0f))
								{
									JOBQ.Progress (jid, (100 * y) / ih);
									update_status(); Fl::wait (0.01);
								}
							}
							mdi_views[mdi_current]->add_bitmap (bitmap, iw, ih);
						}
					}
					if (jpeg_data) delete[] (char *) jpeg_data;
					JOBQ.EndJob (jid);
					update_status();
				}
				else
				{
					sprintf (buf, "%i: \"%s\" %s (%i bytes): %s\n", i, P->FileName, P->ContentType, (int) P->ContentSize, P->Description);
					mdi_views[mdi_current]->add_header ((char *) locs("Attachment","attachment"), buf);
				}
			}
		}
	}

	mdi_views[mdi_current]->add_textline (" "); // Make a bit of bottom margin
	//br_message->add_textline (" ");
	
	mdi_views[mdi_current]->redraw();
	//set_status (locs("Done...","status_done"));
	if (totalsize > 8192) M->Flush();
}

void cb_folders_dropdown (Fl_Button */*b*/, void */*v*/)
{
	int mb_x, mb_y, mb_w, mb_h;
	[[maybe_unused]] int ml_x, ml_y, ml_w, ml_h;
	[[maybe_unused]] int ms_x, ms_y, ms_w, ms_h;

	PREFERENCES.Set_char_tree ("postoffice/state/main/mailboxtile","full");

	ms_x = gr_message->x();
	ms_y = gr_message->y();
	ms_w = gr_message->w();
	ms_h = gr_message->h();
	
	ml_x = gr_folderindex->x();
	ml_y = gr_folderindex->y();
	ml_h = gr_folderindex->h();
	ml_w = gr_folderindex->w();
	
	mb_x = gr_folderlist->x();
	mb_y = gr_folderlist->y();
	mb_w = gr_folderlist->w();
	mb_h = gr_folderlist->h();
	
	tile_lists->remove (gr_folderlist);
	tile_lists->remove (gr_folderindex);
	tile_all->remove (gr_message);
	tile_all->remove (tile_lists);
	gr_tiles->remove (tile_all);
	
	gr_message->resizable (mdi_views[mdi_current]);
	gr_message->resize (ml_x, ms_y, ml_w, ms_h);
	gr_folderlist->resize (mb_x, mb_y, mb_w, mb_h + ms_h);
	gr_folderindex->resize (ml_x,ml_y,ml_w,ml_h);
	
	// tile_all == verticaal
	// tile_lists = horizontaal
	
	delete (tile_lists);
	delete (tile_all);
	gr_tiles->begin();
		tile_lists = new Fl_Tile (mb_x, mb_y, ms_w, ms_h + ml_h);
		tile_lists->begin();
			tile_all = new Fl_Tile (ml_x, ml_y, ml_w, ml_h+ms_h);
		tile_lists->end();
	gr_tiles->end();
	
	tile_lists->add (gr_folderlist);
	tile_all->add (gr_message);
	tile_all->add (gr_folderindex);
		
	//tile_lists->resize (mb_x, mb_y, ms_w, ms_h + ml_h);
	//tile_all->resize (ml_x, ml_y, ml_w, ml_h+ms_h);
	
	
	gr_tiles->redraw();
	bt_dropfolders->label ("@-22<");
	bt_dropfolders->callback ((Fl_Callback *) cb_folders_undrop, (void *) NULL);
	folderlist_fullsize = 1;
};

void cb_folders_undrop (Fl_Button */*b*/, void */*v*/)
{
	int mb_x, mb_y, mb_w, mb_h;
	[[maybe_unused]] int ml_x, ml_y, ml_w, ml_h;
	[[maybe_unused]] int ms_x, ms_y, ms_w, ms_h;

	PREFERENCES.Set_char_tree ("postoffice/state/main/mailboxtile","compact");

	ms_x = gr_message->x();
	ms_y = gr_message->y();
	ms_w = gr_message->w();
	ms_h = gr_message->h();
	
	ml_x = gr_folderindex->x();
	ml_y = gr_folderindex->y();
	ml_h = gr_folderindex->h();
	ml_w = gr_folderindex->w();
	
	mb_x = gr_folderlist->x();
	mb_y = gr_folderlist->y();
	mb_w = gr_folderlist->w();
	mb_h = gr_folderlist->h();
	
	tile_all->remove (gr_message);
	tile_all->remove (gr_folderindex);
	tile_lists->remove (tile_all);
	tile_lists->remove (gr_folderlist);
	gr_tiles->remove (tile_lists);
	
	tile_all->resize (mb_x, mb_y, mb_w+ms_w, mb_h);
	tile_lists->resize (mb_x, mb_y, mb_w+ms_w, ml_h);
	
	gr_folderlist->resize (mb_x, mb_y, mb_w, ml_h);
	gr_message->resize (mb_x, ms_y, ms_w+mb_w, ms_h);
	
	tile_lists->add (gr_folderlist);
	tile_lists->add (gr_folderindex);
	tile_all->add (tile_lists);
	tile_all->add (gr_message);
	
	gr_tiles->add (tile_all);
	
	gr_tiles->redraw();
	bt_dropfolders->label ("@-22>");
	bt_dropfolders->callback ((Fl_Callback *) cb_folders_dropdown, (void *) NULL);
	folderlist_fullsize = 0;
}

void cb_menu_shrink (Fl_Button */*b*/, void */*v*/)
{
	PREFERENCES.Set_char_tree ("postoffice/state/main/toolbar","small");

	gr_bigbuttons->hide();
	gr_minibuttons->show();
	bt_status->hide();
	gr_stats->hide();
	gr_stats->deactivate();
	gr_tiles->size (gr_tiles->w(), gr_tiles->h()+gr_stats->h());
	gr_tiles->redraw();
}

void cb_menu_expand (Fl_Button */*b*/, void */*v*/)
{
	PREFERENCES.Set_char_tree ("postoffice/state/main/toolbar","big");

	gr_minibuttons->hide();
	gr_bigbuttons->show();
	gr_tiles->size (gr_tiles->w(), gr_tiles->h()-gr_stats->h());
	gr_stats->position (0, gr_tiles->y()+gr_tiles->h());
	gr_stats->show();
	bt_status->show();
	gr_stats->activate();
}

char STATUS[512];

void set_status (const char *stat)
{
	strcpy (STATUS, stat);
	out_ministatus->value (STATUS);
	bt_status->label (STATUS);
	bt_status->redraw();
}

void update_status (void)
{
	STATUS[0] = 0;
	int i = 0;
	int tmp = 0;
	char str[64];
	time_t ti;
	int cnt=0;

	while (i<32 && (strlen(STATUS)<500))
	{
		if ((ti = JOBQ.Started (i)))
		{
			if (cnt++) strncat (STATUS, " | ",500);

			strncat (STATUS, "[", 500);
			strncat (STATUS, JOBQ.Owner (i),500);
			tmp = JOBQ.Progress (i);
			if (tmp)
			{
				sprintf (str, ": %i%%", tmp);
				strncat (STATUS, str, 500);
			}
			strncat (STATUS, "] ", 500);
			strncat (STATUS, JOBQ.Stat (i), 500);
			tmp = JOBQ.XFer (i);
			if (tmp)
			{
				double speed = tmp / ((time (NULL) - ti +1));
				if (speed > 1024)
				{
					sprintf (str, "(%.2fKb/s)", speed/1024.0);
				}
				else
				{
					sprintf (str, "(%i bytes/s)", (int) speed);
				}
				strncat (STATUS, str, 500);
			}
		}
		++i;
	}
	out_ministatus->value (STATUS);
	bt_status->label (STATUS);
	bt_status->redraw();
}

void extract_recipients (WordList *list, char *rcpt)
{
	WordList *tmp = new WordList;
	WordList *tmp2 = new WordList;
	char *t;
	int i,j;
	int crsr=0;
	int cstart = 0;
	int quote = 0;

	dprintf ("checking out %s\n", rcpt);


	
	while (rcpt[crsr])
	{
		if ((rcpt[crsr] == ',') && (!quote))
		{
			tmp->Add_ (t = substr(rcpt, cstart, crsr-cstart));
			dprintf ("added %s\n", t);
			cstart = crsr+1;
		}
		else if ((rcpt[crsr] =='\"')||(rcpt[crsr] =='\''))
		{
			if ((quote) && (quote==rcpt[crsr])) quote = 0;
			else
			{
				if (!quote) quote = rcpt[crsr];
			}
		}
		++crsr;
	}
	t = substr(rcpt, cstart, crsr-cstart);
	if (strlen (t)) tmp->Add_ (t);
	else delete[] t;

	if (tmp->Count)
	{
		for (i=0;i<tmp->Count;++i)
		{
			t = tmp->Get(i);
			dprintf ("considering %s\n", t);
			if (wild_match ("*<*@*>", t))
			{
				t = strchr (t, '<');
				list->Add_ (substr (t, 1, strlen(t)-2));
				dprintf ("added %s\n", list->Get (list->Count -1));
			}
			else
			{
				tmp2->MakeGeneric (tmp->Get(i), ' ');
				for (j=0;j<tmp2->Count;++j)
				{
					if (strchr (tmp2->Get(j), '@'))
					{
						list->Add (tmp2->Get(j));
						dprintf ("added %s\n", list->Get (list->Count -1));
					}
				}
			}
		}
	}
}

void cb_quit (Fl_Window *, void *);

void cb_quit_m (Fl_Menu_ */*w*/, void */*v*/)
{
	cb_exit (NULL, NULL);
}

void cb_exit (Fl_Window *w, void *v)
{
	char buf[256];
	
	if (w)
	{
		sprintf (buf, "%i,%i,%i,%i", w->x(), w->y(), w->w(), w->h());
		PREFERENCES.Set_char_tree ("postoffice/state/main/geometry", buf);
		w->hide();
	}
	cb_quit (w, v);
}


void cb_quit (Fl_Window */*b*/, void */*v*/)
{
	char buf[256];
	char tag[256];
	int i=0;

	Fl_Window *e = make_exitwindow();
    int flw = Fl::w();
    int flh = Fl::h();
    if (((double)flw/(double)flh) >= 2.0) flw = flw/2;

	e->position ((flw-e->w())/2,(flh-e->h())/2);
	e->show();
	Fl::flush(); Fl::wait (0.1); Fl::flush(); Fl::wait (0.1);
	
	sprintf (tag, "Making safety backup of preferences");
	bt_exitline->label (tag);
	bt_exitline->value (10);
	bt_exitline->redraw();
	Fl::flush(); Fl::wait (0.1); Fl::flush(); Fl::wait (0.1);

	sprintf (buf, "%s/.postoffice/resource.rc.tmp", getenv ("HOME"));
	PREFERENCES.Save (buf);

	sprintf (tag, "Saving preferences");
	bt_exitline->label (tag);
	bt_exitline->value (20);
	bt_exitline->redraw();
	Fl::flush(); Fl::wait (0.1); Fl::flush(); Fl::wait (0.1);

	sprintf (buf, "%s/.postoffice/resource.rc", getenv ("HOME"));
	PREFERENCES.Save (buf);
	sprintf (buf, "%s/.postoffice/resource.rc.tmp", getenv ("HOME"));
	unlink (buf);
	
	int mbc;
	for (mbc=0; MailBoxList[mbc]; mbc++);
	
	
	while (MailBoxList[i])
	{
		ExitThread *thr = new ExitThread;
		thr->Run ((void *) MailBoxList[i]);
		
		sprintf (tag, "Saving mailbox '%s'", MailBoxList[i]->Name);
		bt_exitline->label (tag);
		bt_exitline->value (20 + ( (65*i)/mbc ));
		bt_exitline->redraw();
		Fl::flush(); 

		while (thr->Started) Fl::wait (0.02);

		sprintf (tag, "Terminating thread '%s'", MailBoxList[i]->Name);
		bt_exitline->label (tag);
		bt_exitline->value (30 + ( (65*i)/mbc ));
		bt_exitline->redraw();
		Fl::flush(); 
		
		thr->Run ((void *) MailBoxList[i], true);
		
		while (thr->Started) Fl::wait (0.02);
		delete (thr);
		
		delete (MailBoxList[i]);
		++i;
	}

	sprintf (tag, "Sending financial data to IRS");
	bt_exitline->label (tag);
	bt_exitline->value (100);
	bt_exitline->redraw();
	Fl::flush(); Fl::wait (0.1); Fl::flush(); Fl::wait (1.0);
	e->hide();

	exit(0);
}

void cb_stop (Fl_Button */*b*/,void */*v*/)
{
	for (int i=0;i<16;++i)
	{
		if (MailBoxList[i]) MailBoxList[i]->ToCollector->PostMessage (CLASS_STOP, NO_SUBCLASS, "");
	}
}

void cb_aboot (Fl_Menu_ */*m*/, void */*v*/)
{
	if (!win_aboot) make_aboot();
	win_aboot->show();
}

void cb_close_aboot (Fl_Button */*b*/, void */*v*/)
{
	win_aboot->hide();
}

void cb_show_build (Fl_Menu_ */*m*/, void */*v*/)
{
	if (!win_buildinfo) make_buildinfo();
	win_buildinfo->show();
}

