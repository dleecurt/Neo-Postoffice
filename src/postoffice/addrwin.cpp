//===============================================================================
// Neo-PostOffice: the Address Book window
// ----------------------------------------
// Opened from the Addr buttons (main toolbar, small toolbar, Mail Menu),
// File -> Address Book, the book buttons beside To/CC/BCC in a composer,
// and Message -> Add Sender to Address Book. It is not modal: entries are
// added, changed and deleted in it, and To / CC / BCC put the selected
// entries in the composer it was opened from (or in a new message).
//===============================================================================

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Menu_.H>
#include <FL/fl_ask.H>

#include <string>
#include <vector>
#include <string.h>

#include "helpdesk.h"
#include "addrbook.h"
#include "mailbox.h"
#include "mailheaders.h"
#include "search.h"

extern MailBox		*currentmailbox;
extern int			 mdi_current;
extern char			 currentmsgid[8][256];

bool		 composer_is_open (void *);
Fl_Input	*composer_field (void *, int);
Fl_Window	*composer_window (void *);
void		*composer_new (void);
void		 composer_show_field (void *, int);

namespace
{
	// fl_alert and fl_choice draw @ as the start of a symbol.
	std::string esc_at (const std::string &s)
	{
		std::string r;
		for (char c : s) { r += c; if (c == '@') r += '@'; }
		return r;
	}

	Fl_Window					*abwin = NULL;
	void						*ab_target = NULL;		// the composer, or NULL
	int							 ab_field = 0;			// 0 To, 1 CC, 2 BCC
	std::vector<addrbook::Entry> ab_entries;			// as listed
	std::string					 ab_editing;			// Id of the entry in the fields
	const char					*FIELDNAMES[] = { "To", "CC", "BCC" };
	int							 ab_widths[] = { 120, 180, 0 };

	void status (const std::string &s)
	{
		bx_ab_status->copy_label (esc_at (s).c_str ());
		bx_ab_status->redraw ();
	}

	void show_target (void)
	{
		std::string t;
		if (!composer_is_open (ab_target)) ab_target = NULL;
		if (ab_target)
		{
			Fl_Window *w = composer_window (ab_target);
			const char *l = w ? w->label () : NULL;
			t = std::string ("For: ") + (l && *l ? l : "the message being written")
				+ "  (double-click: " + FIELDNAMES[ab_field] + ")";
		}
		else t = "To, CC and BCC start a new message";
		bx_ab_target->copy_label (esc_at (t).c_str ());
		bx_ab_target->redraw ();
	}

	// Add for a new entry, Save for changes to the entry chosen in the list.
	void save_label (void)
	{
		bt_ab_save->label (ab_editing.empty () ? "Add" : "Save");
		bt_ab_save->tooltip (ab_editing.empty () ? "Add the fields as a new entry"
												  : "Store the changes to the entry chosen in the list");
		bt_ab_save->redraw ();
	}

	void fill_fields (const addrbook::Entry &E)
	{
		in_ab_nickname->value (E.Nickname.c_str ());
		in_ab_name->value (E.Name.c_str ());
		in_ab_email->value (E.Email.c_str ());
		ab_editing = E.Id;
		save_label ();
	}

	void update_buttons (void)
	{
		bool any = false;
		for (int i = 1; i <= br_addresses->size (); ++i)
			if (br_addresses->selected (i)) any = true;
		if (any) bt_ab_delete->activate (); else bt_ab_delete->deactivate ();
		// No entry chosen: the fields make a new entry.
		if (!any) ab_editing = "";
		save_label ();
	}

	void refresh (const std::string &select = "")
	{
		ab_entries = addrbook::All ();
		br_addresses->clear ();
		br_addresses->format_char (0);
		br_addresses->column_char ('\t');
		br_addresses->column_widths (ab_widths);
		for (size_t i = 0; i < ab_entries.size (); ++i)
		{
			const addrbook::Entry &E = ab_entries[i];
			std::string row = E.Nickname + "\t" + E.Name + "\t" + E.Email;
			br_addresses->add (row.c_str ());
			if (!select.empty () && E.Id == select)
			{
				br_addresses->select ((int) i + 1);
				br_addresses->middleline ((int) i + 1);
			}
		}
		update_buttons ();
		char buf[64];
		snprintf (buf, sizeof (buf), "Address Book (%d)", (int) ab_entries.size ());
		abwin->copy_label (buf);
	}

	std::vector<addrbook::Entry> selected (void)
	{
		std::vector<addrbook::Entry> R;
		for (int i = 1; i <= br_addresses->size () && i <= (int) ab_entries.size (); ++i)
			if (br_addresses->selected (i)) R.push_back (ab_entries[i - 1]);
		return R;
	}

	void make (void)
	{
		if (abwin) return;
		abwin = make_address_window ();
		abwin->size_range (440, 260);
		br_addresses->callback ((Fl_Callback *) cb_ab_select);
	}

	// Puts the entries in a field of the target composer (a new message if
	// it has none or it was closed).
	void address (const std::vector<addrbook::Entry> &E, int field)
	{
		if (E.empty ())
		{
			status ("Select one or more entries first.");
			return;
		}
		if (!composer_is_open (ab_target)) ab_target = composer_new ();
		Fl_Input *I = composer_field (ab_target, field);
		if (!I) return;
		std::string v = I->value ();
		size_t end = v.find_last_not_of (" ,");
		v = (end == std::string::npos) ? "" : v.substr (0, end + 1);
		std::vector<std::string> have = mailheaders::SplitAddresses (v);
		int added = 0;
		for (auto &e : E)
		{
			bool dup = false;
			for (auto &h : have)
				if (mailheaders::AddressKey (h) == mailheaders::AddressKey (e.Email)) dup = true;
			if (dup) continue;
			if (!v.empty ()) v += ", ";
			v += addrbook::Format (e);
			++added;
		}
		I->value (v.c_str ());
		I->set_changed ();
		composer_show_field (ab_target, field);
		if (Fl_Window *w = composer_window (ab_target)) w->show ();
		abwin->show ();
		show_target ();
		char buf[96];
		if (added) snprintf (buf, sizeof (buf), "Added %d address%s to %s.", added, added == 1 ? "" : "es", FIELDNAMES[field]);
		else snprintf (buf, sizeof (buf), "Already in %s.", FIELDNAMES[field]);
		status (buf);
	}

	// The sender of a message: name and address.
	bool sender_of (MailBox *B, const char *id, std::string &name, std::string &email)
	{
		if (!B || !id || !*id) return false;
		MailMessage *M = B->Find ((char *) id);
		if (!M) return false;
		if (M->HeaderNames->Count == 0) M->LoadHeaders ();
		const char *from = M->Header ("From");
		if (!from || !*from) return false;
		std::vector<std::string> L = mailheaders::SplitAddresses (mailheaders::DecodeAddresses (from));
		if (L.empty ()) return false;
		mailheaders::SplitAddress (L[0], name, email);
		// A quoted name comes back with its quotes.
		if (name.size () >= 2 && name.front () == '"' && name.back () == '"')
		{
			std::string n;
			for (size_t i = 1; i + 1 < name.size (); ++i)
			{
				if (name[i] == '\\' && i + 2 < name.size ()) ++i;
				n += name[i];
			}
			name = n;
		}
		return !email.empty ();
	}
}

// Opens the address book for a composer (NULL: none) and its field.
void addressbook_open (void *composer, int field)
{
	make ();
	ab_target = composer;
	ab_field = (field >= 0 && field <= 2) ? field : 0;
	std::string keep;
	for (auto &E : selected ()) { keep = E.Id; break; }
	refresh (keep);
	show_target ();
	status ("");
	abwin->show ();
}

// Opens the address book with a sender filled in: their entry when they
// are in it, otherwise a new entry to check and save.
void addressbook_add_sender (const std::string &name, const std::string &email)
{
	make ();
	if (!composer_is_open (ab_target)) ab_target = NULL;
	addrbook::Entry E = addrbook::ByEmail (email);
	refresh (E.Id);
	show_target ();
	abwin->show ();
	if (!E.Id.empty ())
	{
		fill_fields (E);
		update_buttons ();
		status ("This address is already in the address book.");
		return;
	}
	br_addresses->deselect ();
	update_buttons ();
	E.Name = name;
	E.Email = email;
	E.Nickname = addrbook::SuggestNickname (name, email);
	fill_fields (E);
	ab_editing = "";
	in_ab_nickname->take_focus ();
	status ("Check the new entry and press Save.");
}

// Right-click menu in the message list: the sender of that message.
void addressbook_add_sender_of (const char *msgid)
{
	std::string name, email;
	if (!sender_of (currentmailbox, msgid, name, email))
	{
		fl_alert ("This message has no sender address.");
		return;
	}
	addressbook_add_sender (name, email);
}

//-------------------------------------------------------------------------------
// Callbacks
//-------------------------------------------------------------------------------

void cb_addressbook (Fl_Button */*b*/, void */*v*/)
{
	addressbook_open (NULL, 0);
}

void cb_addressbook_m (Fl_Menu_ */*m*/, void */*v*/)
{
	addressbook_open (NULL, 0);
}

void cb_add_sender_m (Fl_Menu_ */*m*/, void */*v*/)
{
	if (!currentmailbox || !currentmsgid[mdi_current][0])
	{
		fl_message ("Select a message first.");
		return;
	}
	addressbook_add_sender_of (currentmsgid[mdi_current]);
}

void cb_ab_window (Fl_Double_Window *w, void */*v*/)
{
	w->hide ();
}

void cb_ab_close (Fl_Button */*b*/, void */*v*/)
{
	abwin->hide ();
}

void cb_ab_select (Fl_Browser *b, void */*v*/)
{
	int line = b->value ();
	if (line > 0 && line <= (int) ab_entries.size () && b->selected (line))
	{
		// Fl_Browser_ calls back on the release of the second click.
		if ((Fl::event () == FL_PUSH || Fl::event () == FL_RELEASE) && Fl::event_clicks ())
		{
			address ({ ab_entries[line - 1] }, ab_field);
			Fl::event_clicks (0);
		}
		fill_fields (ab_entries[line - 1]);
		status ("");
	}
	update_buttons ();
}

void cb_ab_new (Fl_Button */*b*/, void */*v*/)
{
	br_addresses->deselect ();
	addrbook::Entry E;
	fill_fields (E);
	update_buttons ();
	in_ab_nickname->take_focus ();
	status ("Enter the new entry and press Save.");
}

void cb_ab_save (Fl_Return_Button */*b*/, void */*v*/)
{
	addrbook::Entry E;
	E.Id = ab_editing;
	E.Nickname = in_ab_nickname->value ();
	E.Name = in_ab_name->value ();
	E.Email = in_ab_email->value ();

	// "Name <addr>" pasted into the address field.
	if (E.Email.find ('<') != std::string::npos)
	{
		std::string n, a;
		mailheaders::SplitAddress (E.Email, n, a);
		E.Email = a;
		if (E.Name.empty ()) E.Name = n;
	}
	size_t a = E.Email.find_first_not_of (' '), z = E.Email.find_last_not_of (' ');
	E.Email = (a == std::string::npos) ? "" : E.Email.substr (a, z - a + 1);
	size_t at = E.Email.find ('@');
	if (E.Email.empty () || at == std::string::npos || at == 0 || at + 1 == E.Email.size ()
		|| E.Email.find_first_of (" ,;<>\"()") != std::string::npos)
	{
		fl_alert ("Enter one e-mail address (like name@@example.org) in Email.");
		in_ab_email->take_focus ();
		return;
	}
	if (E.Nickname.find_first_not_of (' ') == std::string::npos)
		E.Nickname = addrbook::SuggestNickname (E.Name, E.Email);

	std::string nick = search::Fold (E.Nickname);
	for (auto &O : addrbook::All ())
	{
		if (O.Id == E.Id) continue;
		if (search::Fold (O.Nickname) == nick)
		{
			fl_alert ("The nickname \"%s\" is already used for %s.\nChoose another one.",
					  esc_at (O.Nickname).c_str (), esc_at (O.Email).c_str ());
			in_ab_nickname->take_focus ();
			return;
		}
		if (mailheaders::AddressKey (O.Email) == mailheaders::AddressKey (E.Email))
		{
			if (fl_choice ("%s is already in the address book as \"%s\".\nSave this entry as well?",
						   "Cancel", "Save", NULL, esc_at (E.Email).c_str (), esc_at (O.Nickname).c_str ()) != 1)
				return;
			break;
		}
	}
	// The entry is done: the fields are cleared for the next one, so what
	// is typed next becomes a new entry rather than replacing this one.
	std::string id = addrbook::Save (E);
	refresh (id);
	br_addresses->deselect ();
	fill_fields (addrbook::Entry ());
	update_buttons ();
	in_ab_nickname->take_focus ();
	std::string who = E.Nickname.find_first_not_of (' ') != std::string::npos ? E.Nickname : E.Email;
	status ((E.Id.empty () ? "Added " : "Saved ") + who + ".");
}

void cb_ab_delete (Fl_Button */*b*/, void */*v*/)
{
	std::vector<addrbook::Entry> S = selected ();
	if (S.empty ()) return;
	std::string what = S.size () == 1 ? "\"" + S[0].Nickname + "\" (" + S[0].Email + ")"
									  : std::to_string (S.size ()) + " entries";
	if (fl_choice ("Delete %s from the address book?", "Cancel", "Delete", NULL, esc_at (what).c_str ()) != 1) return;
	for (auto &E : S)
	{
		addrbook::Remove (E.Id);
		if (E.Id == ab_editing) fill_fields (addrbook::Entry ());
	}
	refresh ();
	status (S.size () == 1 ? "Deleted." : "Deleted the entries.");
}

void cb_ab_add (Fl_Button */*b*/, void *v)
{
	long field = (long) v;
	std::vector<addrbook::Entry> S = selected ();
	if (S.empty ())
	{
		// An address typed in the fields but not saved can be used too.
		addrbook::Entry E;
		E.Name = in_ab_name->value ();
		E.Email = in_ab_email->value ();
		if (E.Email.find ('@') != std::string::npos && E.Email.find_first_of (" ,") == std::string::npos)
			S.push_back (E);
	}
	address (S, (int) field);
}
