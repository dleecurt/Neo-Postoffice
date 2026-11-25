// Search (Neo-PostOffice): the Search Messages window (File -> New Query,
// the Search buttons). A search covers the folder that was open, or all
// folders of its mailbox; it runs on the mailbox's thread
// (MailBox::Search) and its results are shown in a "Search results"
// folder of that mailbox, where the messages can be opened, answered,
// moved or deleted as in their own folders. The folder is the mailbox's
// alone (no folder on the server or disk) and lasts until it is closed
// (right-click -> Close search results) or the program quits.

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_.H>
#include <FL/fl_ask.H>

#include <string>
#include <string.h>
#include <stdlib.h>

#include "helpdesk.h"
#include "mailbox.h"
#include "itc.h"
#include "search.h"

extern MailBox			*currentmailbox;
extern MailBox			*MailBoxList[16];
extern char				 currentfolder[64];
void update_folder (void);
void clear_folder_index (void);
void update_tree (Fl_Scroll *);
extern Fl_Scroll		*current_boxlist;
void tree_select_folder (MailBox *box, const char *folder);

static MailBox			*search_box = NULL;	// the mailbox searched
static std::string		 search_folder;		// its folder, "" for all
static bool				 searching = false;

static const char		*RESULTS = "Search results";

static bool box_alive (MailBox *B)
{
	for (int k = 0; k < 16 && MailBoxList[k]; ++k) if (MailBoxList[k] == B) return true;
	return false;
}

static MailView *results_view (MailBox *B)
{
	for (MailView *V = B->VFirst; V; V = V->Next) if (V->IsSearch) return V;
	return NULL;
}

static void open_search (void)
{
	if (!currentmailbox)
	{
		fl_message ("Open a folder first: the search covers that folder or all folders of its mailbox.");
		return;
	}
	if (!win_search)
	{
		make_query_window ();
		ch_search_field->value (SEARCH_SUBJECT);
	}
	if (searching && search_box != currentmailbox)
	{
		win_search->show ();
		return;
	}
	search_box = currentmailbox;
	MailView *cur = *currentfolder ? currentmailbox->GetView (currentfolder) : NULL;
	ch_search_scope->clear ();
	std::string all = std::string ("All folders of ") + currentmailbox->Name;
	std::string esc;
	for (char c : all) { if (c == '/' || c == '&' || c == '\\' || c == '_') esc += '\\'; if (c == '@') esc += '@'; esc += c; }
	if (cur && !cur->IsSearch)
	{
		search_folder = currentfolder;
		std::string one = std::string ("This folder (") + currentfolder + ")", e1;
		for (char c : one) { if (c == '/' || c == '&' || c == '\\' || c == '_') e1 += '\\'; if (c == '@') e1 += '@'; e1 += c; }
		ch_search_scope->add (e1.c_str ());
	}
	else search_folder.clear ();
	ch_search_scope->add (esc.c_str ());
	ch_search_scope->value (0);
	if (!searching) bx_search_status->copy_label ("");
	win_search->show ();
	in_search_text->take_focus ();
}

void cb_search (Fl_Button *, void *) { open_search (); }
void cb_search_m (Fl_Menu_ *, void *) { open_search (); }
void cb_search_close (Fl_Button *, void *) { if (win_search) win_search->hide (); }
void cb_search_window (Fl_Window *, void *) { if (win_search) win_search->hide (); }

void cb_search_go (Fl_Return_Button *, void *)
{
	if (searching) return;
	std::string text = in_search_text->value ();
	while (!text.empty () && text.back () == ' ') text.pop_back ();
	while (!text.empty () && text[0] == ' ') text.erase (0, 1);
	if (text.empty ())
	{
		fl_message ("Type the text to look for.");
		return;
	}
	if (!search_box || !box_alive (search_box))
	{
		bx_search_status->copy_label ("The mailbox is gone; close this window and open it again.");
		return;
	}
	// "This folder" is the first choice only when there is one.
	bool one = !search_folder.empty () && ch_search_scope->value () == 0;
	std::string folder = one ? search_folder : "";
	int field = ch_search_field->value ();
	if (field < 0) field = SEARCH_SUBJECT;
	searching = true;
	bt_search_go->deactivate ();
	bx_search_status->copy_label (("Searching " + (one ? folder : std::string ("all folders of ") + search_box->Name) + "...").c_str ());
	search_box->ToCollector->PostMessage (CLASS_SEARCH, NO_SUBCLASS, std::to_string (field).c_str (), text.c_str (), folder.c_str ());
}

// The results came (CLASS_SEARCHED): [mailbox, field, text, folder, note,
// Message-IDs...].
void search_done (WordList *W)
{
	searching = false;
	if (win_search) bt_search_go->activate ();
	if (W->Count < 5) return;
	MailBox *B = NULL;
	for (int k = 0; k < 16 && MailBoxList[k]; ++k) if (strcmp (MailBoxList[k]->Name, W->Get (0)) == 0) B = MailBoxList[k];
	if (!B) return;
	if (atoi (W->Get (1)) < 0)
	{
		if (win_search) bx_search_status->copy_label ("The search failed.");
		return;
	}
	MailView *V = results_view (B);
	if (!V)
	{
		// The name, unless a real folder has it.
		std::string name = RESULTS;
		for (int n = 2; B->GetView (name.c_str ()); ++n) name = std::string (RESULTS) + " (" + std::to_string (n) + ")";
		B->CreateMailView (name.c_str ());
		V = B->GetView (name.c_str ());
		if (!V) return;
		V->IsSearch = true;
	}
	V->UnassociateAll ();
	int found = 0;
	for (int i = 5; i < W->Count; ++i)
	{
		MailMessage *M = B->Find (W->Get (i));
		if (M) { V->Associate (M); ++found; }
	}
	std::string what = std::string ("\"") + W->Get (2) + "\" in " + (*W->Get (3) ? W->Get (3) : (std::string ("all folders of ") + B->Name));
	std::string status = (found == 0 ? std::string ("No messages found") : found == 1 ? std::string ("1 message found")
						  : std::to_string (found) + " messages found") + " for " + what + ".";
	if (*W->Get (4)) status += std::string ("\n") + W->Get (4);
	if (win_search) bx_search_status->copy_label (status.c_str ());
	tree_select_folder (B, V->Name);
}

// Right-click -> Close search results, or Folder -> Delete folder on it.
void close_search_results (MailBox *B)
{
	MailView *V = results_view (B);
	if (!V) return;
	std::string name = V->Name;
	bool shown = currentmailbox == B && name == currentfolder;
	if (shown)
	{
		currentfolder[0] = 0;
		clear_folder_index ();
	}
	B->DestroyMailView ((char *) name.c_str ());
	update_tree (current_boxlist);
}
