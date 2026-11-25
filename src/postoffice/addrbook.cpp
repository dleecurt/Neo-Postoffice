//===============================================================================
// Neo-PostOffice: the address book (entries, matching, completion)
// -----------------------------------------------------------------
// See addrbook.h. The window is in addrwin.cpp.
//===============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>

#include <FL/Fl.H>
#include <FL/Fl_Menu_Window.H>
#include <FL/fl_draw.H>

#include "addrbook.h"
#include "var.h"
#include "wordlist.h"
#include "encode.h"
#include "mailheaders.h"
#include "search.h"

extern Var PREFERENCES;

namespace
{
	std::string enc (const std::string &s)
	{
		char *e = encode_mysql ((char *) s.c_str ());
		std::string r = e;
		delete[] e;
		return r;
	}

	std::string dec (const char *s)
	{
		if (!s || !*s) return "";
		char *d = decode_mysql ((char *) s);
		std::string r = d;
		delete[] d;
		return r;
	}

	// Tabs and line breaks have no place in a name or an address (the list
	// shows its columns split at tabs).
	std::string clean (const std::string &s)
	{
		std::string r;
		for (char c : s) r += ((unsigned char) c < ' ') ? ' ' : c;
		size_t a = r.find_first_not_of (' ');
		if (a == std::string::npos) return "";
		size_t b = r.find_last_not_of (' ');
		return r.substr (a, b - a + 1);
	}

	Var *contacts (void)
	{
		Var *P = PREFERENCES.Get_collection ("postoffice");
		return P->Get_collection ("contacts");
	}

	// The old address book (nickname = address) becomes entries.
	void migrate (void)
	{
		Var *P = PREFERENCES.Get_collection ("postoffice");
		if (!P->Exists ("addressbook")) return;
		WordList *W = P->Get_wordlist ("addressbook");
		std::vector<std::pair<std::string, std::string>> old;
		for (int i = 0; W && i < W->Count; ++i)
			old.push_back ({ W->Get (i), P->Get_collection ("addressbook")->Get_char (W->Get (i)) });
		P->Delete ("addressbook");
		for (auto &o : old)
		{
			addrbook::Entry E;
			mailheaders::SplitAddress (o.second, E.Name, E.Email);
			if (E.Email.empty ()) continue;
			E.Nickname = o.first;
			addrbook::Save (E);
		}
	}

	std::string lower_ascii (std::string s)
	{
		for (char &c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		return s;
	}

	bool starts (const std::string &s, const std::string &p)
	{
		return s.compare (0, p.size (), p) == 0;
	}
}

namespace addrbook
{
	std::vector<Entry> All (void)
	{
		migrate ();
		std::vector<Entry> R;
		Var *C = contacts ();
		WordList *W = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("contacts");
		for (int i = 0; W && i < W->Count; ++i)
		{
			Var *V = C->Get_collection (W->Get (i));
			Entry E;
			E.Id = W->Get (i);
			E.Nickname = dec (V->Get_char ("nickname"));
			E.Name = dec (V->Get_char ("name"));
			E.Email = dec (V->Get_char ("email"));
			if (!E.Email.empty ()) R.push_back (E);
		}
		std::sort (R.begin (), R.end (), [](const Entry &a, const Entry &b)
		{
			std::string x = search::Fold (a.Nickname), y = search::Fold (b.Nickname);
			if (x != y) return x < y;
			x = search::Fold (a.Name); y = search::Fold (b.Name);
			if (x != y) return x < y;
			return lower_ascii (a.Email) < lower_ascii (b.Email);
		});
		return R;
	}

	std::string Save (const Entry &E)
	{
		Var *C = contacts ();
		std::string id = E.Id;
		if (id.empty ())
		{
			WordList *W = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("contacts");
			int max = 0;
			for (int i = 0; W && i < W->Count; ++i) max = std::max (max, atoi (W->Get (i)));
			char buf[16];
			snprintf (buf, sizeof (buf), "%03d", max + 1);
			id = buf;
		}
		Var *V = C->Get_collection (id.c_str ());
		V->Set_char ("nickname", enc (clean (E.Nickname)).c_str ());
		V->Set_char ("name", enc (clean (E.Name)).c_str ());
		V->Set_char ("email", enc (clean (E.Email)).c_str ());
		return id;
	}

	void Remove (const std::string &id)
	{
		if (!id.empty ()) contacts ()->Delete (id.c_str ());
	}

	Entry ByEmail (const std::string &email)
	{
		std::string k = lower_ascii (clean (email));
		for (const Entry &E : All ())
			if (lower_ascii (E.Email) == k) return E;
		return Entry ();
	}

	std::string Format (const Entry &E)
	{
		if (E.Name.empty ()) return E.Email;
		bool special = false;
		for (char c : E.Name)
			if (strchr ("()<>[]:;@\\,.\"", c)) special = true;
		if (!special) return E.Name + " <" + E.Email + ">";
		std::string q = "\"";
		for (char c : E.Name)
		{
			if (c == '"' || c == '\\') q += '\\';
			q += c;
		}
		return q + "\" <" + E.Email + ">";
	}

	std::vector<Entry> Match (const std::string &text, size_t max)
	{
		std::string t = search::Fold (clean (text));
		std::vector<Entry> first, rest;
		if (t.empty ()) return first;
		for (const Entry &E : All ())
		{
			if (starts (search::Fold (E.Nickname), t)) { first.push_back (E); continue; }
			bool hit = starts (lower_ascii (E.Email), t);
			std::string n = search::Fold (E.Name);
			for (size_t p = 0; !hit && p < n.size (); )
			{
				if (starts (n.substr (p), t)) hit = true;
				size_t sp = n.find_first_of (" .-'", p);
				if (sp == std::string::npos) break;
				p = sp + 1;
			}
			if (hit) rest.push_back (E);
		}
		first.insert (first.end (), rest.begin (), rest.end ());
		if (first.size () > max) first.resize (max);
		return first;
	}

	std::string SuggestNickname (const std::string &name, const std::string &email)
	{
		std::string n = clean (name);
		std::string r;
		if (!n.empty ())
		{
			size_t sp = n.find_first_of (" ,");
			r = n.substr (0, sp);
		}
		if (r.empty ()) r = clean (email).substr (0, clean (email).find ('@'));
		return search::Fold (r);
	}
}

//-------------------------------------------------------------------------------
// The completion list
//-------------------------------------------------------------------------------

namespace
{
	const int ROWH = 18;

	class PopupList : public Fl_Widget
	{
		public:
			Fl_Address_Input	*owner = NULL;
			std::vector<std::string> rows;
			int					 sel = 0;

			PopupList (int x, int y, int w, int h) : Fl_Widget (x, y, w, h) {}

			void draw () override
			{
				fl_draw_box (FL_BORDER_BOX, x (), y (), w (), h (), FL_BACKGROUND2_COLOR);
				fl_font (FL_HELVETICA, 12);
				for (int i = 0; i < (int) rows.size (); ++i)
				{
					int ry = y () + 1 + i * ROWH;
					if (i == sel)
					{
						fl_color (FL_SELECTION_COLOR);
						fl_rectf (x () + 1, ry, w () - 2, ROWH);
						fl_color (fl_contrast (FL_FOREGROUND_COLOR, FL_SELECTION_COLOR));
					}
					else fl_color (FL_FOREGROUND_COLOR);
					fl_push_clip (x () + 1, ry, w () - 2, ROWH);
					fl_draw (rows[i].c_str (), x () + 5, ry, w () - 10, ROWH, FL_ALIGN_LEFT, NULL, 0);
					fl_pop_clip ();
				}
			}

			int row_at (int ey) { return (ey - y () - 1) / ROWH; }

			int handle (int e) override
			{
				switch (e)
				{
					case FL_ENTER: case FL_MOVE:
					{
						int r = row_at (Fl::event_y ());
						if (r >= 0 && r < (int) rows.size () && r != sel) { sel = r; redraw (); }
						return 1;
					}
					case FL_PUSH:
					{
						int r = row_at (Fl::event_y ());
						if (owner && r >= 0 && r < (int) rows.size ()) owner->Accept (r);
						return 1;
					}
				}
				return Fl_Widget::handle (e);
			}
	};

	Fl_Menu_Window	*popwin = NULL;
	PopupList		*poplist = NULL;
}

Fl_Address_Input::Fl_Address_Input (int x, int y, int w, int h, const char *l)
	: Fl_Input (x, y, w, h, l)
{
}

Fl_Address_Input::~Fl_Address_Input ()
{
	ClosePopup ();
}

void Fl_Address_Input::hide ()
{
	ClosePopup ();
	Fl_Input::hide ();
}

void Fl_Address_Input::ClosePopup (void)
{
	if (poplist && poplist->owner == this)
	{
		poplist->owner = NULL;
		popwin->hide ();
	}
	matches.clear ();
}

// The address being typed: from after the last comma (outside quotes and
// <...>) before the cursor to the next comma or the end.
bool Fl_Address_Input::Token (int &start, int &end, std::string &text)
{
	const char *v = value ();
	int pos = insert_position ();
	if (pos != mark ()) return false;
	start = 0;
	bool quote = false;
	int angle = 0;
	for (int i = 0; i < pos; ++i)
	{
		if (v[i] == '"') quote = !quote;
		else if (!quote && v[i] == '<') ++angle;
		else if (!quote && v[i] == '>' && angle) --angle;
		else if (!quote && !angle && v[i] == ',') start = i + 1;
	}
	if (quote || angle) return false;
	while (v[start] == ' ') ++start;
	end = pos;
	while (v[end] && v[end] != ',') ++end;
	// Only while typing at the end of the address.
	for (int i = pos; i < end; ++i) if (v[i] != ' ') return false;
	text.assign (v + start, pos - start);
	if (text.find_first_of ("<>\"") != std::string::npos) return false;
	return true;
}

void Fl_Address_Input::UpdatePopup (void)
{
	int s, e;
	std::string t;
	if (!Token (s, e, t) || t.size () < 2) { ClosePopup (); return; }
	matches = addrbook::Match (t, 8);
	if (matches.empty ()) { ClosePopup (); return; }
	// The list knows what to type if the text is already a whole entry.
	if (matches.size () == 1 && addrbook::Format (matches[0]) == t) { ClosePopup (); return; }

	if (!popwin)
	{
		Fl_Group *cur = Fl_Group::current ();
		Fl_Group::current (NULL);
		popwin = new Fl_Menu_Window (10, 10, 100, 100);
		popwin->set_override ();
		popwin->clear_border ();
		poplist = new PopupList (0, 0, 100, 100);
		popwin->end ();
		popwin->resizable (poplist);
		Fl_Group::current (cur);
	}
	poplist->owner = this;
	poplist->rows.clear ();
	for (auto &E : matches)
	{
		std::string row = E.Nickname.empty () ? "" : E.Nickname + "  \xe2\x80\x94  ";
		row += addrbook::Format (E);
		poplist->rows.push_back (row);
	}
	poplist->sel = 0;
	Fl_Window *top = window ();
	int wx = top ? top->x_root () : 0, wy = top ? top->y_root () : 0;
	// x()/y() are relative to the window the field is in.
	int ph = (int) matches.size () * ROWH + 2;
	popwin->resize (wx + x (), wy + y () + h (), w (), ph);
	poplist->resize (0, 0, w (), ph);
	if (!popwin->shown ()) popwin->show ();
	popwin->redraw ();
}

void Fl_Address_Input::Accept (int row)
{
	if (row < 0 || row >= (int) matches.size ()) return;
	int s, e;
	std::string t;
	addrbook::Entry E = matches[row];
	ClosePopup ();
	if (!Token (s, e, t)) return;
	std::string ins = addrbook::Format (E) + ", ";
	replace (s, e, ins.c_str (), (int) ins.size ());
	set_changed ();
	do_callback ();
	// Focus first (getting the focus selects the whole text), then the
	// cursor after the address, ready for the next one.
	if (Fl::focus () != this) take_focus ();
	insert_position (s + (int) ins.size ());
}

int Fl_Address_Input::handle (int ev)
{
	bool open = poplist && poplist->owner == this && popwin->shown ();
	switch (ev)
	{
		case FL_KEYBOARD:
		case FL_SHORTCUT:
			if (open && Fl::focus () == this)
			{
				int k = Fl::event_key ();
				if (k == FL_Down) { poplist->sel = (poplist->sel + 1) % (int) poplist->rows.size (); poplist->redraw (); return 1; }
				if (k == FL_Up) { poplist->sel = (poplist->sel + (int) poplist->rows.size () - 1) % (int) poplist->rows.size (); poplist->redraw (); return 1; }
				if (k == FL_Enter || k == FL_KP_Enter || (k == FL_Tab && !Fl::event_state (FL_SHIFT)))
				{
					Accept (poplist->sel);
					return 1;
				}
				if (k == FL_Escape) { ClosePopup (); return 1; }
			}
			if (ev == FL_KEYBOARD)
			{
				std::string before = value ();
				int r = Fl_Input::handle (ev);
				if (before != value ()) UpdatePopup ();
				else if (open && (Fl::event_key () == FL_Left || Fl::event_key () == FL_Right
								  || Fl::event_key () == FL_Home || Fl::event_key () == FL_End))
					ClosePopup ();
				return r;
			}
			break;
		case FL_UNFOCUS:
		case FL_HIDE:
		case FL_DEACTIVATE:
			ClosePopup ();
			break;
		case FL_PUSH:
			if (open) ClosePopup ();
			break;
	}
	return Fl_Input::handle (ev);
}
