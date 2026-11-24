#include <FL/Fl_Input_.H>
#include <pthread.h>
#include <signal.h>
#include "mailheaders.h"
#include "semaphore.h"
#include "helpdesk.h"
#include "encode.h"
#include "wordlist.h"
#include "wordlist.h"
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
#include <FL/Fl_Menu_Item.H>
#include <algorithm>
#include <string>
#include <vector>
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

// The shown message's From, Subject, Reply-To (or From), To and CC, with
// encoded words decoded (Neo-PostOffice), for Reply, Reply All and Forward.
std::string			 currentfrom[8];
char				 currentlabel[8][256];
std::string			 currentsubject[8];
std::string			 currentreplyto[8];
std::string			 currentto[8];
std::string			 currentcc[8];
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
void clear_folder_index (void);
void update_tree_counts (void);

// Breaks lines longer than width at spaces, so that a long error message
// fits in its dialog (Neo-PostOffice).
std::string wrap_text (const char *text, size_t width)
{
	std::string out;
	size_t col = 0, last_space = std::string::npos;
	for (const char *p = text; *p; ++p)
	{
		out += *p;
		if (*p == '\n') { col = 0; last_space = std::string::npos; continue; }
		if (*p == ' ') last_space = out.size() - 1;
		if (++col > width && last_space != std::string::npos)
		{
			out[last_space] = '\n';
			col = out.size() - 1 - last_space;
			last_space = std::string::npos;
		}
	}
	// For a dialog: FLTK reads "@" in a label as the start of a symbol, so
	// "test@example.com" was cut off at the "@". "@@" shows one "@".
	std::string escaped;
	for (char c : out)
	{
		escaped += c;
		if (c == '@') escaped += '@';
	}
	return escaped;
}
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

//===============================================================================
// Neo-PostOffice: moving and flagging messages (Maildir mailboxes)
//===============================================================================

bool tree_folder_at_pointer (MailBox *&box, const char *&folder);

// The internal ids of the selected messages (one, or a multiple selection).
static std::vector<std::string> selected_msgids (void)
{
	std::vector<std::string> ids;
	Fl_Toggle_Node *S = br_folderindex->selected();
	if (S) ids.push_back (((FolderData *) S->data())->msgid);
	else while ((S = br_folderindex->selection()))
		ids.push_back (((FolderData *) S->data())->msgid);
	return ids;
}

// Moves or copies messages of the current mailbox into a folder of a mailbox
// that can store them. Within one mailbox a move is the handler's own (a
// rename, an IMAP MOVE); everything else goes
// through the source mailbox's thread, which copies the original message
// into the target and, for a move, then deletes it like Delete does.
static void transfer_messages (const std::vector<std::string> &ids, MailBox *box, const char *folder, bool copy)
{
	if (ids.empty() || !currentmailbox || !box) return;
	if (!box->Handler || !box->Handler->CanMove())
	{
		fl_alert ("Messages can only be moved or copied into Maildir, MH and IMAP mailboxes.");
		return;
	}
	if (box == currentmailbox && strcmp (folder, currentfolder) == 0) return;

	if (box == currentmailbox && !copy)
	{
		for (const std::string &id : ids)
			currentmailbox->ToCollector->PostMessage (CLASS_MOVE, NO_SUBCLASS, id.c_str(), folder);
		// One check after the moves shows the result.
		currentmailbox->ToCollector->PostMessage (CLASS_CHECKGROUP, NO_SUBCLASS, "*");
		return;
	}
	for (size_t i = 0; i < ids.size(); ++i)
	{
		WordList *W = new WordList;
		W->Add (ids[i].c_str());
		W->Add (box->Name);
		W->Add (folder);
		W->Add (copy ? "copy" : "move");
		W->Add (i + 1 == ids.size() ? "1" : "0");
		currentmailbox->ToCollector->PostMessage (CLASS_TRANSFER, NO_SUBCLASS, W);
	}
}

// Messages dragged from the message list and dropped on a folder; with
// Ctrl held they are copied instead of moved.
static void drop_messages (void)
{
	Fl_Toggle_Node *dragged = br_folderindex->drag_node();
	MailBox *box;
	const char *folder;
	if (!dragged || !dragged->data() || !tree_folder_at_pointer (box, folder)) return;

	// The whole selection if the dragged message is part of it, else just it.
	std::string dragged_id = ((FolderData *) dragged->data())->msgid;
	std::vector<std::string> ids = selected_msgids();
	if (std::find (ids.begin(), ids.end(), dragged_id) == ids.end())
		ids.assign (1, dragged_id);
	transfer_messages (ids, box, folder, Fl::event_state (FL_CTRL) != 0);
}

// A popup of the folders of all mailboxes that can store messages (Maildir,
// MH, IMAP), one submenu each.
// Returns false if nothing was chosen.
static bool choose_target_folder (const char *title, MailBox *&box, std::string &folder)
{
	struct Target { MailBox *box; std::string folder; };
	std::vector<Target> targets;
	std::vector<std::string> labels;
	std::vector<Fl_Menu_Item> items;

	auto add_item = [&](const char *text, int flags, long data)
	{
		Fl_Menu_Item it;
		memset (&it, 0, sizeof (it));
		it.text = text;
		it.flags = flags;
		it.user_data_ = (void *) data;
		it.labelsize_ = 12;
		items.push_back (it);
	};

	// Collect first (the menu keeps pointers to the label strings).
	std::vector<std::pair<MailBox *, std::vector<std::string>>> boxes;
	for (int k = 0; k < 16 && MailBoxList[k]; ++k)
	{
		MailBox *B = MailBoxList[k];
		if (!B->Handler || !B->Handler->CanMove()) continue;
		std::vector<std::string> names;
		B->VLock->LockR();
		for (MailView *V = B->VFirst; V; V = V->Next)
			if (!(B == currentmailbox && strcmp (V->Name, currentfolder) == 0)) names.push_back (V->Name);
		B->VLock->UnlockR();
		if (!names.empty()) boxes.push_back ({ B, names });
	}
	if (boxes.empty())
	{
		fl_alert ("There is no Maildir, MH or IMAP mailbox to move or copy messages to.");
		return false;
	}
	for (auto &b : boxes)
		for (auto &n : b.second) targets.push_back ({ b.first, n });

	size_t t = 0;
	for (auto &b : boxes)
	{
		add_item (b.first->Name, FL_SUBMENU, -1);
		for (auto &n : b.second)
		{
			add_item (n.c_str(), 0, (long) t);
			++t;
		}
		add_item (NULL, 0, 0);		// end of submenu
	}
	add_item (NULL, 0, 0);			// end of menu

	const Fl_Menu_Item *m = items[0].popup (Fl::event_x(), Fl::event_y(), title);
	if (!m || (long) m->user_data_ < 0) return false;
	box = targets[(long) m->user_data_].box;
	folder = targets[(long) m->user_data_].folder;
	return true;
}

static void move_or_copy_menu (bool copy)
{
	if (!currentmailbox) return;
	std::vector<std::string> ids = selected_msgids();
	if (ids.empty()) return;
	MailBox *box;
	std::string folder;
	if (choose_target_folder (copy ? "Copy to folder" : "Move to folder", box, folder))
		transfer_messages (ids, box, folder.c_str(), copy);
}

void cb_move_m (Fl_Menu_ */*w*/, void */*d*/)
{
	move_or_copy_menu (false);
}

void cb_copy_m (Fl_Menu_ */*w*/, void */*d*/)
{
	move_or_copy_menu (true);
}

// Mark the selected messages as not read (the Maildir S flag goes; other
// mailboxes keep it in their index).
void cb_markunread_m (Fl_Menu_ */*w*/, void */*d*/)
{
	if (!currentmailbox) return;
	std::vector<std::string> ids = selected_msgids();
	bool flags = currentmailbox->Handler && currentmailbox->Handler->KeepsFlags();
	for (const std::string &id : ids)
	{
		if (MailMessage *M = currentmailbox->Find ((char *) id.c_str()))
			M->Envelope->SetInt (ENVELOPE_I_READ, 0);
		if (flags) currentmailbox->ToCollector->PostMessage (CLASS_SETFLAG, NO_SUBCLASS, id.c_str(), "-S");
	}
	update_folder();
}

void folder_subscriptions_show (WordList *);
void attachments_init (void);
bool part_is_attachment (const MailPart *);
std::string attachment_file_name (const MailPart *, int);
bool attachment_bytes (const MailPart *, std::string &);
void cb_delete (Fl_Button *, void *);
void cb_mark (Fl_Button *, void *);

// Right-click in the message list: the message commands for the selection
// (the clicked message becomes the selection if it was not part of it).
static void message_context_menu (Fl_Toggle_Node *n)
{
	if (!n || !n->data() || !currentmailbox) return;
	std::string id = ((FolderData *) n->data())->msgid;
	std::vector<std::string> ids = selected_msgids();
	if (std::find (ids.begin(), ids.end(), id) == ids.end())
	{
		br_folderindex->select_only (n);
	}
	bool maildir = currentmailbox->Handler && currentmailbox->Handler->KeepsFlags();
	Fl_Menu_Item items[] = {
		{ "Move to folder...", 0, 0, 0, 0, 0, 0, 12, 0 },
		{ "Copy to folder...", 0, 0, 0, FL_MENU_DIVIDER, 0, 0, 12, 0 },
		{ "Flag / unflag",     0, 0, 0, maildir ? 0 : FL_MENU_INACTIVE, 0, 0, 12, 0 },
		{ "Mark as unread",    0, 0, 0, FL_MENU_DIVIDER, 0, 0, 12, 0 },
		{ "Delete",            0, 0, 0, 0, 0, 0, 12, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 }
	};
	const Fl_Menu_Item *m = items[0].popup (Fl::event_x(), Fl::event_y(), NULL);
	if (m == &items[0]) move_or_copy_menu (false);
	else if (m == &items[1]) move_or_copy_menu (true);
	else if (m == &items[2]) cb_mark (NULL, NULL);
	else if (m == &items[3]) cb_markunread_m (NULL, NULL);
	else if (m == &items[4]) cb_delete (NULL, NULL);
}

// Flag / unflag the selected messages (Maildir F flag, IMAP \Flagged).
void cb_mark (Fl_Button */*w*/, void */*d*/)
{
	if (!currentmailbox) return;
	std::vector<std::string> ids = selected_msgids();
	if (ids.empty()) return;
	if (!currentmailbox->Handler || !currentmailbox->Handler->KeepsFlags())
	{
		fl_alert ("This mailbox does not keep flags (Maildir, MH and IMAP mailboxes do).");
		return;
	}
	// Flag them all, unless the first one is flagged: then unflag them all.
	MailMessage *first = currentmailbox->Find ((char *) ids[0].c_str());
	bool on = !(first && (first->Envelope->GetInt (ENVELOPE_I_FLAGS) & EFLAG_FLAGGED));
	for (const std::string &id : ids)
	{
		if (MailMessage *M = currentmailbox->Find ((char *) id.c_str()))
		{
			int f = M->Envelope->GetInt (ENVELOPE_I_FLAGS);
			M->Envelope->SetInt (ENVELOPE_I_FLAGS, on ? (f | EFLAG_FLAGGED) : (f & ~EFLAG_FLAGGED));
		}
		currentmailbox->ToCollector->PostMessage (CLASS_SETFLAG, NO_SUBCLASS, id.c_str(), on ? "+F" : "-F");
	}
	update_folder();
}

void cb_mark_m (Fl_Menu_ *w, void *d)
{
	cb_mark ((Fl_Button *) w, d);
}

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
	update_tree_counts();
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
// SENDER		Edit -> Select All (Alt+A), Ctrl+A in the main window
// SYNOPSIS		Selects every message in the message list, also those in
//				closed threads, for Move, Delete or Mark (Neo-PostOffice;
//				an empty stub in 0.7.3).
//===============================================================================

void cb_select_all (Fl_Menu_ */*w*/, void */*d*/)
{
	if (!br_folderindex || !br_folderindex->traverse_start()) return;
	br_folderindex->select_all();
}

// Ctrl+A in the main window selects all messages too, unless a text field
// has the focus (where it keeps its own meaning).
// Edit menu (Neo-PostOffice). Cut and Paste work on the text field you are
// typing in; Copy also copies the text selected in the message view.
// Copy and Cut go to the clipboard, Paste takes the clipboard.

static Fl_Input_ *focused_input (void)
{
	Fl_Input_ *in = Fl::focus () ? dynamic_cast<Fl_Input_ *> (Fl::focus ()) : NULL;
	if (!in || !win_main || in->top_window () != win_main) return NULL;
	return in;
}

static Fl_View *shown_view (void)
{
	if (mdi_current < 0 || mdi_current >= 8) return NULL;
	return mdi_views[mdi_current];
}

static void update_edit_menu (void)
{
	Fl_Input_ *in = focused_input ();
	bool editable = in && !in->readonly ();
	bool insel = in && in->insert_position () != in->mark ();
	Fl_View *v = shown_view ();
	bool viewsel = v && !v->selected_text ().empty ();
	if (editable && insel) mi_edit_cut->activate (); else mi_edit_cut->deactivate ();
	if (insel || viewsel) mi_edit_copy->activate (); else mi_edit_copy->deactivate ();
	if (editable) mi_edit_paste->activate (); else mi_edit_paste->deactivate ();
	if (viewsel) mi_popup_copy->activate (); else mi_popup_copy->deactivate ();
}

void cb_edit_cut_m (Fl_Menu_ */*m*/, void */*d*/)
{
	Fl_Input_ *in = focused_input ();
	if (!in || in->readonly () || in->insert_position () == in->mark ()) return;
	in->copy (1);
	in->cut ();
	in->do_callback (FL_REASON_CHANGED);
}

void cb_edit_copy_m (Fl_Menu_ */*m*/, void */*d*/)
{
	Fl_Input_ *in = focused_input ();
	if (in && in->insert_position () != in->mark ()) { in->copy (1); return; }
	Fl_View *v = shown_view ();
	if (v) v->copy_selection ();
}

void cb_edit_paste_m (Fl_Menu_ */*m*/, void */*d*/)
{
	Fl_Input_ *in = focused_input ();
	if (in && !in->readonly ()) Fl::paste (*in, 1);
}

// Every event updates the Edit menu first, so its items (and their
// shortcuts) are active exactly when they can do something.
static int edit_dispatch (int event, Fl_Window *w)
{
	if (win_main && (event == FL_PUSH || event == FL_SHORTCUT || event == FL_KEYBOARD))
		update_edit_menu ();
	return Fl::handle_ (event, w);
}

// Alt+X, Alt+C, Alt+V and Alt+A still work in the main window, as in the
// original PostOffice (the menus show Ctrl).
static int alt_edit_shortcut (int event)
{
	if (event != FL_SHORTCUT) return 0;
	int st = Fl::event_state () & (FL_CTRL | FL_ALT | FL_SHIFT | FL_META);
	if (st != FL_ALT) return 0;
	if (!win_main || Fl::first_window () != win_main) return 0;
	switch (Fl::event_key ())
	{
		case 'x': if (mi_edit_cut->active ()) cb_edit_cut_m (NULL, NULL); return 1;
		case 'c': if (mi_edit_copy->active ()) cb_edit_copy_m (NULL, NULL); return 1;
		case 'v': if (mi_edit_paste->active ()) cb_edit_paste_m (NULL, NULL); return 1;
		case 'a':
			if (focused_input ()) return 0;
			cb_select_all (NULL, NULL);
			return 1;
	}
	return 0;
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

// Usenet (NNTP) support is gone: news mailboxes from PostOffice 0.7 are
// removed from the settings (saved at once, so this is said only once),
// and the user is told when the window is up. Their files are left.
static std::string usenet_note;

static void show_usenet_note (void *)
{
	fl_message ("%s", usenet_note.c_str ());
}

static void drop_usenet_mailboxes (void)
{
	Var *VBoxes = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes");
	WordList *names = VBoxes->GatherNames ();
	std::vector<std::string> gone, where;
	for (int i = 0; i < names->Count; ++i)
	{
		Var *B = VBoxes->Get_collection (names->Get (i));
		if (strcasecmp (B->Get_char ("protocol"), "nntp") != 0) continue;
		gone.push_back (names->Get (i));
		std::string dir = B->Get_char ("prefix");
		if (dir.empty ()) dir = std::string (getenv ("HOME") ? getenv ("HOME") : "~") + "/.postoffice/" + names->Get (i) + ".box";
		where.push_back (dir);
	}
	delete names;
	if (gone.empty ()) return;
	for (const std::string &n : gone) VBoxes->Delete ((char *) n.c_str ());
	char path[512];
	snprintf (path, sizeof (path), "%s/.postoffice/resource.rc", getenv ("HOME"));
	PREFERENCES.Save (path);

	usenet_note = "Usenet (NNTP) news is no longer supported, so ";
	if (gone.size () == 1)
		usenet_note += "the news mailbox \"" + gone[0] + "\" was removed from your settings.\n\n"
					   "The articles it had saved are still in " + where[0] + ".";
	else
	{
		usenet_note += "these news mailboxes were removed from your settings:\n";
		for (size_t i = 0; i < gone.size (); ++i) usenet_note += "  " + gone[i] + "  (articles still in " + where[i] + ")\n";
	}
	Fl::add_timeout (1.0, show_usenet_note);
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

	snprintf (buf, sizeof (buf), "%s/.postoffice", getenv ("HOME"));
	if (stat (buf, &mystat)!=0)
	{
		mkdir (buf, 0700);
		start_wizard();
	}
	else
	{
		snprintf (buf, sizeof (buf), "%s/.postoffice/resource.rc", getenv ("HOME"));
		if (stat (buf, &mystat)!=0)
		{
			start_wizard();
		}
	}

	spl->show();
	sl_splash->value (10);
	sl_splash->label ("Loading preferences");
	Fl::wait(0.01); Fl::flush(); Fl::wait(0.01);

	snprintf (buf, sizeof (buf), "%s/.postoffice/resource.rc", getenv ("HOME"));
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
	
	drop_usenet_mailboxes ();

	Var *VBoxes = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes");
	WordList *MBoxes = VBoxes->GatherNames();

	int cnt=0;
	
	for (i=0;i<MBoxes->Count;++i)
	{
		if (strcasecmp (MBoxes->Get (i), "outbox"))
		{
			sl_splash->value(30+((60*i)/MBoxes->Count));
			// The text comes from the language file: put the name in its
			// %s without using it as a format string.
			{
				std::string text = locs("Opening mailbox: %s","opening_mailbox");
				size_t at = text.find ("%s");
				if (at != std::string::npos) text.replace (at, 2, MBoxes->Get(i));
				else text += std::string (" ") + MBoxes->Get(i);
				snprintf (buf, sizeof (buf), "%s", text.c_str());
			}
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
	br_folderindex->drag_enable (1);	// drag messages onto folders
	br_folderindex->keyboard_nav (1);	// arrow keys move through the list
	br_folderindex_->end();
	br_folderindex_->resizable (br_folderindex);
	br_folderindex_->type (Fl_Scroll::VERTICAL);
	
	W->show (argc, argv);
	Fl::add_handler (alt_edit_shortcut);
	Fl::event_dispatch (edit_dispatch);
	update_edit_menu ();
	attachments_init ();

	o = br_folderindex;
    	o->column_widths (xwidths);

	snprintf (currentfolder, sizeof (currentfolder), "Inbox");
	
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
		// Neo-PostOffice: handle everything that is waiting (up to a limit, so
		// the window stays responsive), not one message per pass: a mailbox
		// that brings in thousands of messages posts one notice for each.
		bool counts_changed = false;
		for (int drained = 0; drained < 500 && (M = PIPE->GetNextMessage()); ++drained)
		{
			CCacheNode *CC;
			
			switch (M->Class)
			{
				case CLASS_GOTBODY :
					//currentmailbox->Lock->LockR();
					MS = currentmailbox ? currentmailbox->Find (M->Message->Get(0)) : NULL;
					//currentmailbox->Lock->UnlockR();
					if (MS) displaymessage (MS);
					break;

				case CLASS_CHECKEDGROUP :
					//update_folder();
					break;

				case CLASS_NEWARTICLE:
					counts_changed = true;
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
					// A mailbox (SQL, Maildir) created or removed folders, or its
					// messages changed: rebuild the tree (folders, counts) and the
					// message list if it is the one shown.
					update_tree (current_boxlist);
					if (currentmailbox && *currentfolder && strcmp (currentmailbox->Name, M->Message->Get(0)) == 0)
						update_folder();
					break;

				case CLASS_FOLDERGONE:
					// A folder disappeared at the source (a Maildir folder deleted
					// on disk): remove it and its settings.
					for (int k=0; k<16 && MailBoxList[k]; ++k)
					{
						MailBox *B = MailBoxList[k];
						if (strcmp (B->Name, M->Message->Get(0)) != 0) continue;
						if (B == currentmailbox && strcmp (currentfolder, M->Message->Get(1)) == 0)
						{
							currentfolder[0] = 0;
							clear_folder_index();
						}
						B->DestroyMailView (M->Message->Get(1));
						PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (B->Name)->Get_collection ("folders")->Delete (M->Message->Get(1));
						break;
					}
					break;

				case CLASS_FOLDERLIST:
					// The server's folders, for the Subscriptions dialog (IMAP).
					folder_subscriptions_show (M->Message);
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
							update_tree_counts();
						}
						break;
					}
					break;

				case CLASS_ERROR :
					errorstring = new char [strlen (M->Message->Get(0)) + strlen (M->Message->Get(1)) + 64];
					sprintf (errorstring, "Error in [%s]\n%s", M->Message->Get(0), M->Message->Get(1));
					if (M->Subclass == SUB_ERROR_CRITICAL) fl_alert ("%s", wrap_text (errorstring, 75).c_str());
					eprintf ("\n*** ERROR ***\n%s\n", errorstring);

					delete[] errorstring;
					break;

				case CLASS_STATUS:
					update_status();
					break;
			}
			delete (M);
		}
		if (counts_changed) update_tree_counts();
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
	// Check Mail also sends the queue now, including messages that wait
	// (after Stop or a settings problem) for a check the user starts.
	if (OUTBOX && OUTBOX != currentmailbox) OUTBOX->ToCollector->PostMessage (CLASS_CHECKGROUP, NO_SUBCLASS, "");
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
	if (br_folderindex->state() == FL_TOGGLE_DROP)
	{
		drop_messages();
		return;
	}
	if (br_folderindex->state() == FL_TOGGLE_CONTEXT)
	{
		message_context_menu (br_folderindex->context_node());
		return;
	}
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
					// Let the mailbox record it too (the Maildir's seen flag).
					currentmailbox->ToCollector->PostMessage (CLASS_MARKREAD, NO_SUBCLASS, M->MsgID);
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
	// Neo-PostOffice: shown decoded (RFC 2047 encoded words) as UTF-8.
	std::string value = mailheaders::Decode (headervalue ? headervalue : "");
	mdi_views[mdi_current]->add_header ((char *) headername, value.c_str());
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
	std::string		 text_utf8;		// a text part, decoded
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

	currentfrom[mdi_current].clear ();
	currentsubject[mdi_current].clear ();
	currentreplyto[mdi_current].clear ();
	currentto[mdi_current].clear ();
	currentcc[mdi_current].clear ();
	currentmsgid[mdi_current][0] = 0;
	currentmessage[mdi_current].Clear();
	currentreferences[mdi_current][0] = 0;

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
	mdi_title (mailheaders::Decode (t_winfrom).c_str());

	// display the headers

	for (i=0;i<M->HeaderNames->Count;++i)
	{
		nval = M->HeaderNames->Get (i);
		xval = M->HeaderValues->Get (i);

		if (strcasecmp (nval, "From") == 0)
		{
			addheader (locs("From","From"), xval);
			currentfrom[mdi_current] = mailheaders::DecodeAddresses (xval);
		}
		else if (strcasecmp (nval, "To") == 0)
		{
			addheader (locs("To","To"), xval);
			currentto[mdi_current] = mailheaders::DecodeAddresses (xval);
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
			currentcc[mdi_current] = mailheaders::DecodeAddresses (xval);
		}
		else if (strcasecmp (nval, "Date") == 0)
		{
			addheader (locs("Date","Date"), xval);
		}
		else if (strcasecmp (nval, "Subject") == 0)
		{
			addheader (locs("Subject","Subject"), xval);
			currentsubject[mdi_current] = mailheaders::Decode (xval);
		}
		else if (strcasecmp (nval, "Newsgroups") == 0)
		{
			addheader (locs("Newsgroups","Newsgroups"), xval);
		}
		else if (strcasecmp (nval, "Organization") == 0)
		{
			addheader (locs("Organization","Organization"), xval);
		}
		else if (strcasecmp (nval, "Reply-To") == 0)
		{
			addheader (locs("Reply-To","Reply-To"), xval);
			currentreplyto[mdi_current] = mailheaders::DecodeAddresses (xval);
		}
		else if (strcasecmp (nval, "Followup-To") == 0)
		{
			addheader (locs("Followup-To","Followup-To"), xval);
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

	if (currentreplyto[mdi_current].empty ())
		currentreplyto[mdi_current] = currentfrom[mdi_current];

	// find the relevant style info

	Style *sbody = (Style *) _STYLES.Root->Find ("messagebody:text");
	Style *squoted = (Style *) _STYLES.Root->Find ("messagebody:quoted-text");
	Style *ssig = (Style *) _STYLES.Root->Find ("messagebody:signature");
	Style *sascii = (Style *) _STYLES.Root->Find ("messagebody:ascii");
	
	bool in_ascii = false;

	// display the message body

	// Attachments shown inline (named text, JPEG pictures) get their own
	// clickable line after the message, to save or open them.
	std::vector<int> inline_attachments;

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
				if (P->FileName && *P->FileName) inline_attachments.push_back (ik);
				// Undo the transfer encoding and convert the charset to UTF-8
				// (Neo-PostOffice: 0.7 showed the stored text as it was, so
				// quoted-printable showed =20 and 8-bit text showed ?).
				std::string ptype = P->ContentType ? P->ContentType : "";
				text_utf8 = mailheaders::ToUTF8 (
					mailheaders::DecodeBody (std::string (P->Contents, P->ContentSize), mailheaders::Param (ptype, "x-neo-cte")),
					mailheaders::Param (ptype, "charset"));
				for (char &c : text_utf8) if (c == 0) c = ' ';
				body = (char *) text_utf8.c_str();
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
					inline_attachments.push_back (ik);
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
					// The part number, its name and type (without the stored
					// encoding parameter).
					std::string shown_type = P->ContentType ? P->ContentType : "";
					size_t cut = shown_type.find ("; x-neo-cte=");
					if (cut != std::string::npos) shown_type.erase (cut);
					// The size of the file itself, not of its encoded form.
					std::string bytes;
					size_t size = attachment_bytes (P, bytes) ? bytes.size () : (size_t) P->ContentSize;
					char sz[32];
					if (size < 1024) snprintf (sz, sizeof (sz), "%zu bytes", size);
					else if (size < 1024 * 1024) snprintf (sz, sizeof (sz), "%.1f KB", size / 1024.0);
					else snprintf (sz, sizeof (sz), "%.1f MB", size / (1024.0 * 1024.0));
					const char *desc = P->Description && *P->Description ? P->Description : NULL;
					snprintf (buf, sizeof (buf), "%i: \"%s\" %s (%s)%s%s", ik + 1, P->FileName ? P->FileName : "",
							  shown_type.c_str(), sz, desc ? " - " : "", desc ? desc : "");
					// Clicking it offers Save as... / Open... (attachments.cpp).
					mdi_views[mdi_current]->add_attachment ((char *) locs("Attachment","attachment"), buf, ik);
				}
			}
		}
	}

	for (int ik : inline_attachments)
	{
		P = M->GetPart (ik);
		std::string shown_type = P->ContentType ? P->ContentType : "";
		size_t cut = shown_type.find (';');
		if (cut != std::string::npos) shown_type.erase (cut);
		snprintf (buf, sizeof (buf), "%i: \"%s\" %s (shown above)", ik + 1,
				  attachment_file_name (P, ik).c_str(), shown_type.c_str());
		mdi_views[mdi_current]->add_attachment ((char *) locs("Attachment","attachment"), buf, ik);
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
				snprintf (str, sizeof (str), ": %i%%", tmp);
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
					snprintf (str, sizeof (str), "(%.2fKb/s)", speed/1024.0);
				}
				else
				{
					snprintf (str, sizeof (str), "(%i bytes/s)", (int) speed);
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

// Adds the addresses in a To/Cc/Bcc value to list (Neo-PostOffice: the
// original treated an apostrophe as a quote, so "Pat O'Neil <p@x>, b@y"
// became one broken address). Commas inside double quotes, <...> and
// (comments) do not separate; "Name <addr>" gives addr; groups
// ("Team: a@x, b@y;") give their members.
void extract_recipients (WordList *list, char *rcpt)
{
	std::vector<std::string> items;
	std::string cur;
	bool in_quote = false, in_angle = false;
	int comment = 0;
	for (const char *p = rcpt; p && *p; ++p)
	{
		char c = *p;
		if (in_quote)
		{
			if (c == '\\' && p[1]) { cur += c; cur += *++p; continue; }
			if (c == '"') in_quote = false;
		}
		else if (comment)
		{
			if (c == '\\' && p[1]) { ++p; continue; }
			if (c == '(') ++comment;
			else if (c == ')') --comment;
			continue;				// comments are dropped
		}
		else if (c == '"') in_quote = true;
		else if (c == '(') { ++comment; continue; }
		else if (c == '<') in_angle = true;
		else if (c == '>') in_angle = false;
		else if ((c == ',' || c == ';') && !in_angle) { items.push_back (cur); cur.clear(); continue; }
		else if (c == ':' && !in_angle) { cur.clear(); continue; }	// a group name
		cur += c;
	}
	items.push_back (cur);

	for (std::string &item : items)
	{
		std::string addr;
		size_t lt = item.rfind ('<'), gt = item.rfind ('>');
		if (lt != std::string::npos && gt != std::string::npos && gt > lt) addr = item.substr (lt + 1, gt - lt - 1);
		else
		{
			// A bare address: the word with the @ in it.
			size_t at = item.find ('@');
			if (at == std::string::npos) continue;
			size_t a = item.find_last_of (" \t\r\n", at), e = item.find_first_of (" \t\r\n", at);
			addr = item.substr (a == std::string::npos ? 0 : a + 1,
								e == std::string::npos ? std::string::npos : e - (a == std::string::npos ? 0 : a + 1));
		}
		size_t f = addr.find_first_not_of (" \t\r\n"), l = addr.find_last_not_of (" \t\r\n");
		if (f == std::string::npos) continue;
		addr = addr.substr (f, l - f + 1);
		if (addr.find ('@') == std::string::npos) continue;
		list->Add (addr.c_str());
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
		snprintf (buf, sizeof (buf), "%i,%i,%i,%i", w->x(), w->y(), w->w(), w->h());
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
	
	snprintf (tag, sizeof (tag), "Making safety backup of preferences");
	bt_exitline->label (tag);
	bt_exitline->value (10);
	bt_exitline->redraw();
	Fl::flush(); Fl::wait (0.1); Fl::flush(); Fl::wait (0.1);

	snprintf (buf, sizeof (buf), "%s/.postoffice/resource.rc.tmp", getenv ("HOME"));
	PREFERENCES.Save (buf);

	snprintf (tag, sizeof (tag), "Saving preferences");
	bt_exitline->label (tag);
	bt_exitline->value (20);
	bt_exitline->redraw();
	Fl::flush(); Fl::wait (0.1); Fl::flush(); Fl::wait (0.1);

	snprintf (buf, sizeof (buf), "%s/.postoffice/resource.rc", getenv ("HOME"));
	PREFERENCES.Save (buf);
	snprintf (buf, sizeof (buf), "%s/.postoffice/resource.rc.tmp", getenv ("HOME"));
	unlink (buf);
	
	int mbc;
	for (mbc=0; MailBoxList[mbc]; mbc++);
	
	
	while (MailBoxList[i])
	{
		ExitThread *thr = new ExitThread;
		thr->Run ((void *) MailBoxList[i]);
		
		snprintf (tag, sizeof (tag), "Saving mailbox '%s'", MailBoxList[i]->Name);
		bt_exitline->label (tag);
		bt_exitline->value (20 + ( (65*i)/mbc ));
		bt_exitline->redraw();
		Fl::flush(); 

		while (thr->Started) Fl::wait (0.02);

		snprintf (tag, sizeof (tag), "Terminating thread '%s'", MailBoxList[i]->Name);
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

	snprintf (tag, sizeof (tag), "Sending financial data to IRS");
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

