#include <pthread.h>
#include <signal.h>
#include "semaphore.h"
#include "helpdesk.h"
#include "encode.h"
#include "wordlist.h"
#include "wordlist.h"
#include "buffer.h"
#include "formatter.h"
#include "mailbox.h"
#include <strings.h>
#include <algorithm>
#include <string>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Check_Browser.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Box.H>
#include <vector>
#include "itc.h"
#include "mailhandler.h"
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
void clear_folder_index (void);

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

//===============================================================================
// Neo-PostOffice: managing Maildir folders (right-click menu and File menu)
//===============================================================================

extern MailBox *currentmailbox;
extern char currentfolder[64];

static bool can_manage_folders (MailBox *box)
{
	if (box && box->Handler && box->Handler->CanManageFolders()) return true;
	fl_alert ("The folders of this mailbox are filters; change them in Config, Mailfolders.");
	return false;
}

// Folder names: "/" separates subfolders. Not allowed: "." (the Maildir++
// separator), characters the settings file cannot store, and the Inbox name.
static bool check_folder_name (MailBox *box, std::string &name)
{
	size_t a = name.find_first_not_of (" \t"), b = name.find_last_not_of (" \t");
	name = a == std::string::npos ? "" : name.substr (a, b - a + 1);
	const char *why = NULL;
	if (name.empty()) why = "The name is empty.";
	else if (name.size() > 63) why = "The name is too long (at most 63 bytes).";
	else if (name.find_first_of ("={}\",") != std::string::npos)
		why = "Folder names cannot contain = { } \" or ,";
	else if (name.front() == '/' || name.back() == '/' || name.find ("//") != std::string::npos)
		why = "Use / only between folder names, as in Lists/2025.";
	else if (box->GetView (name.c_str())) why = "There is already a folder with that name.";
	else for (char c : name) if ((unsigned char) c < ' ') why = "The name contains a control character.";
	std::string format_rule;
	if (!why && box->Handler && !(format_rule = box->Handler->CheckFolderName (name)).empty())
		why = format_rule.c_str();
	if (why) fl_alert ("%s", why);
	return !why;
}

static void post_folder_op (MailBox *box, const char *op, const char *a, const char *b)
{
	WordList *W = new WordList;
	W->Add (op);
	W->Add (a);
	W->Add (b);
	box->ToCollector->PostMessage (CLASS_FOLDEROP, NO_SUBCLASS, W);
}

void folder_new (MailBox *box, const char *parent)
{
	if (!can_manage_folders (box)) return;
	std::string def = (parent && *parent && !box->Handler->IsFixedFolder (parent)) ? std::string (parent) + "/" : "";
	const char *in = fl_input ("Name of the new folder (use / for a subfolder, e.g. Lists/2025):", def.c_str());
	if (!in) return;
	std::string name = in;
	if (!check_folder_name (box, name)) return;
	post_folder_op (box, "create", name.c_str(), "");
}

void close_search_results (MailBox *box);

void folder_rename (MailBox *box, const char *folder)
{
	if (folder && *folder && box->GetView (folder) && box->GetView (folder)->IsSearch)
	{
		fl_message ("The search results cannot be renamed; search again for other results.");
		return;
	}
	if (!can_manage_folders (box)) return;
	if (!folder || !*folder || !box->GetView (folder) || box->Handler->IsFixedFolder (folder))
	{
		fl_alert ("Select a folder other than the inbox to rename.");
		return;
	}
	std::string old = folder;
	const char *in = fl_input ("Rename the folder \"%s\" (and its subfolders) to:", old.c_str(), old.c_str());
	if (!in || old == in) return;
	std::string name = in;
	if (!check_folder_name (box, name)) return;
	post_folder_op (box, "rename", old.c_str(), name.c_str());
}

void folder_delete (MailBox *box, const char *folder)
{
	if (folder && *folder && box->GetView (folder) && box->GetView (folder)->IsSearch)
	{
		close_search_results (box);
		return;
	}
	if (!can_manage_folders (box)) return;
	if (!folder || !*folder || !box->GetView (folder) || box->Handler->IsFixedFolder (folder))
	{
		fl_alert ("Select a folder other than the inbox to delete.");
		return;
	}
	std::string name = folder;
	// Messages in the folder and its subfolders.
	int count = 0;
	box->VLock->LockR();
	for (MailView *V = box->VFirst; V; V = V->Next)
		if (name == V->Name || strncmp (V->Name, (name + "/").c_str(), name.size() + 1) == 0)
			count += V->Count();
	box->VLock->UnlockR();
	// IMAP: gone from the server with the folder, not kept in a Trash.
	bool final = box->Handler->FolderDeleteIsFinal();
	bool trash = final || name == "Trash" || name.compare (0, 6, "Trash/") == 0;
	char what[128];
	if (count == 0) snprintf (what, sizeof (what), "They contain no messages.");
	else if (count == 1) snprintf (what, sizeof (what), "The message in them will be %s.",
								   trash ? "deleted permanently" : "moved to the Trash");
	else snprintf (what, sizeof (what), "The %d messages in them will be %s.", count,
				   trash ? "deleted permanently" : "moved to the Trash");
	int r = fl_choice ("Delete the folder \"%s\" and its subfolders%s?\n%s",
					   "Cancel", "Delete", NULL, name.c_str(), final ? " on the server" : "", what);
	if (r != 1) return;
	post_folder_op (box, "delete", name.c_str(), "");
}

// Subscriptions (IMAP): the mailbox lists the server's folders and answers
// with CLASS_FOLDERLIST, which opens the dialog (folder_subscriptions_show).
void folder_subscriptions (MailBox *box)
{
	if (!box || !box->Handler || !box->Handler->CanSubscribe())
	{
		fl_alert ("Subscriptions are for IMAP mailboxes: select an IMAP mailbox or one of its folders.");
		return;
	}
	post_folder_op (box, "list", "", "");
}

struct SubsDialog
{
	std::string box;
	std::vector<std::string> names, servers;
	std::vector<bool> was;
};

static void subs_dialog_open (void *data)
{
	SubsDialog *D = (SubsDialog *) data;
	MailBox *box = NULL;
	for (int k = 0; k < 16 && MailBoxList[k]; ++k)
		if (D->box == MailBoxList[k]->Name) box = MailBoxList[k];
	if (!box) { delete D; return; }		// made anew meanwhile (Config)

	std::string title = "Subscriptions - " + D->box;
	Fl_Double_Window win (440, 400, title.c_str());
	Fl_Box note (10, 8, 420, 52, "Ticked folders are shown here and checked for mail. Unticking a "
				 "folder removes its messages here only: they stay on the server.");
	note.align (FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP | FL_ALIGN_TOP);
	note.labelsize (12);
	Fl_Check_Browser list (10, 62, 420, 290);
	list.textsize (12);
	list.when (FL_WHEN_NEVER);
	int answer = 0;
	Fl_Return_Button ok (240, 362, 90, 26, "OK");
	ok.labelsize (12);
	ok.callback ([](Fl_Widget *, void *v) { *(int *) v = 1; }, &answer);
	Fl_Button cancel (340, 362, 90, 26, "Cancel");
	cancel.labelsize (12);
	cancel.callback ([](Fl_Widget *, void *v) { *(int *) v = 2; }, &answer);
	win.callback ([](Fl_Widget *, void *v) { *(int *) v = 2; }, &answer);
	win.end();
	for (size_t i = 0; i < D->names.size(); ++i) list.add (D->names[i].c_str(), D->was[i] ? 1 : 0);
	if (D->names.empty()) list.add ("(the server has no other folders than the Inbox)");
	win.set_modal();
	win.show();
	while (win.shown() && !answer) Fl::wait();
	win.hide();

	if (answer == 1)
	{
		std::string changes;
		for (size_t i = 0; i < D->names.size(); ++i)
		{
			bool now = list.checked ((int) i + 1);
			if (now != D->was[i]) changes += std::string (now ? "+" : "-") + D->servers[i] + "\n";
		}
		if (!changes.empty()) post_folder_op (box, "subscriptions", changes.c_str(), "");
	}
	delete D;
}

// CLASS_FOLDERLIST: [mailbox, then "1"/"0", name, server name per folder].
// Shown after the message loop has finished with the message.
void folder_subscriptions_show (WordList *W)
{
	if (!W || W->Count < 1) return;
	SubsDialog *D = new SubsDialog;
	D->box = W->Get (0);
	for (int i = 1; i + 2 < W->Count; i += 3)
	{
		D->was.push_back (strcmp (W->Get (i), "1") == 0);
		D->names.push_back (W->Get (i + 1));
		D->servers.push_back (W->Get (i + 2));
	}
	Fl::add_timeout (0.0, subs_dialog_open, D);
}

void cb_folder_subs_m (Fl_Menu_ *, void *)
{
	if (!currentmailbox) { fl_alert ("Select an IMAP mailbox or folder first."); return; }
	folder_subscriptions (currentmailbox);
}

void cb_folder_new_m (Fl_Menu_ *, void *)
{
	if (!currentmailbox) { fl_alert ("Select a mailbox or folder first."); return; }
	folder_new (currentmailbox, currentfolder);
}

void cb_folder_rename_m (Fl_Menu_ *, void *)
{
	if (!currentmailbox) { fl_alert ("Select a folder first."); return; }
	folder_rename (currentmailbox, currentfolder);
}

void cb_folder_delete_m (Fl_Menu_ *, void *)
{
	if (!currentmailbox) { fl_alert ("Select a folder first."); return; }
	folder_delete (currentmailbox, currentfolder);
}

// Right-click on a mailbox or folder in the tree.
static void tree_context_menu (Fl_Toggle_Node *n)
{
	if (!n || !n->data()) return;
	BoxData *B = (BoxData *) n->data();
	MailBox *box = B->mailbox;
	if (!box) return;
	// A search's results (Neo-PostOffice): only closing it makes sense.
	if (B->foldername && box->GetView (B->foldername) && box->GetView (B->foldername)->IsSearch)
	{
		Fl_Menu_Item sitems[] = {
			{ "Close search results", 0, 0, 0, 0, 0, 0, 12, 0 },
			{ 0, 0, 0, 0, 0, 0, 0, 0, 0 }
		};
		if (sitems[0].popup (Fl::event_x(), Fl::event_y(), box->Name)) close_search_results (box);
		return;
	}
	bool is_folder = B->foldername && box->GetView (B->foldername);
	bool fixed = !is_folder || (box->Handler && box->Handler->IsFixedFolder (B->foldername));
	bool manage = box->Handler && box->Handler->CanManageFolders();
	bool subs = box->Handler && box->Handler->CanSubscribe();

	Fl_Menu_Item items[] = {
		{ "New folder...",    0, 0, 0, manage ? 0 : FL_MENU_INACTIVE, 0, 0, 12, 0 },
		{ "Rename folder...", 0, 0, 0, (manage && !fixed) ? 0 : FL_MENU_INACTIVE, 0, 0, 12, 0 },
		{ "Delete folder...", 0, 0, 0, ((manage && !fixed) ? 0 : FL_MENU_INACTIVE) | (subs ? FL_MENU_DIVIDER : 0), 0, 0, 12, 0 },
		{ "Subscriptions...", 0, 0, 0, subs ? 0 : FL_MENU_INVISIBLE, 0, 0, 12, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 }
	};
	const Fl_Menu_Item *m = items[0].popup (Fl::event_x(), Fl::event_y(), manage ? box->Name : "Folders are set in Config");
	if (!m) return;
	std::string folder = is_folder ? B->foldername : "";
	if (m == &items[0]) folder_new (box, folder.c_str());
	else if (m == &items[1]) folder_rename (box, folder.c_str());
	else if (m == &items[2]) folder_delete (box, folder.c_str());
	else if (m == &items[3]) folder_subscriptions (box);
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

	// Remember the selected folder and the scroll position, to keep them
	// when the tree is rebuilt (Neo-PostOffice).
	std::string sel_box, sel_folder;
	int scroll_y = scr->yposition();
	if (TREE && TREE->selected() && TREE->selected()->data())
	{
		BoxData *B = (BoxData *) TREE->selected()->data();
		if (B->mailbox && B->foldername)
		{
			sel_box = B->mailbox->Name;
			sel_folder = B->foldername;
		}
	}

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

	// Back to the top first: the new tree is placed unscrolled, and the
	// scroll position is set again below.
	scr->scroll_to (0, 0);
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
			
			snprintf (buf, sizeof (buf), "%s (%d)", MailBoxList[x]->Name, MailBoxList[x]->Count);
			// Neo-PostOffice: mailbox nodes carry their mailbox (folder "") for
			// the right-click menu and so selecting one makes it current.
			nod = TREE->add_next (buf, 1, &pixmap_folder, (void *) new BoxData (MailBoxList[x], (char *) ""));
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

	// Select the same folder again (by name: the mailboxes may have been
	// made anew) and scroll back to where the tree was.
	if (!sel_box.empty())
	{
		int depth = 0;
		TREE->traverse_start ();
		for (Fl_Toggle_Node_Base *nb = TREE->traverse_start (); nb; nb = TREE->traverse_forward (0, depth))
		{
			Fl_Toggle_Node *n = (Fl_Toggle_Node *) nb;
			BoxData *B = (BoxData *) n->data();
			if (B && B->mailbox && sel_box == B->mailbox->Name && sel_folder == B->foldername)
			{
				TREE->select_only (n);
				break;
			}
		}
	}
	int max_y = TREE->h() - scr->h() + 4;
	scr->scroll_to (scr->xposition(), std::max (0, std::min (scroll_y, max_y)));
	scr->redraw();
	Fl::flush();
}

// Selects a folder in the tree and shows it (Neo-PostOffice: the search
// opens its results this way).
void tree_select_folder (MailBox *box, const char *folder)
{
	update_tree (current_boxlist);
	if (!TREE) return;
	// The mailbox open, so its folders show.
	for (Fl_Toggle_Node_Base *nb = TREE->first (); nb; nb = nb->next_node ())
	{
		BoxData *B = (BoxData *) ((Fl_Toggle_Node *) nb)->data();
		if (B && B->mailbox == box && !*B->foldername && !nb->is_open ()) TREE->open ((Fl_Toggle_Node *) nb);
	}
	int depth = 0;
	for (Fl_Toggle_Node_Base *nb = TREE->traverse_start (); nb; nb = TREE->traverse_forward (0, depth))
	{
		Fl_Toggle_Node *n = (Fl_Toggle_Node *) nb;
		BoxData *B = (BoxData *) n->data();
		if (B && B->mailbox == box && strcmp (B->foldername, folder) == 0)
		{
			TREE->select_only (n);
			break;
		}
	}
	snprintf (currentfolder, sizeof (currentfolder), "%s", folder);
	currentmailbox = box;
	update_folder ();
	TREE->redraw ();
}

// Updates the message counts next to the mailbox names without rebuilding
// the tree (Neo-PostOffice).
void update_tree_counts (void)
{
	if (!TREE) return;
	char buf[512];
	for (Fl_Toggle_Node_Base *nb = TREE->first(); nb; nb = nb->next_node())
	{
		Fl_Toggle_Node *n = (Fl_Toggle_Node *) nb;
		BoxData *B = (BoxData *) n->data();
		if (!B || !B->mailbox) continue;
		snprintf (buf, sizeof (buf), "%s (%d)", B->mailbox->Name, B->mailbox->Count);
		if (!n->label() || strcmp (n->label(), buf) != 0) n->label (buf);
	}
	TREE->redraw ();
}

//===============================================================================
// CALLBACK		cb_selectfolder
// SENDER		folderlist tree
// SYNOPSIS		Makes a new folder/view the current one, redraws the indexview.
//===============================================================================

void cb_selectfolder (Fl_Widget */*b*/, void */*v*/)
{
	if (TREE->state() == FL_TOGGLE_CONTEXT)
	{
		tree_context_menu (TREE->context_node());
		return;
	}
	if (!TREE->selected()) return;
	BoxData *box = (BoxData *) TREE->selected()->data();

	TREE->redraw();
	Fl::flush();

	dprintf ("callback %p\n", (void *) box);

	if (box && !*box->foldername)
	{
		// A mailbox itself: it becomes the current mailbox (Check works on
		// it), no folder is shown.
		currentmailbox = (MailBox *) box->mailbox;
		currentfolder[0] = 0;
		clear_folder_index();
		return;
	}
	if (box)
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

