#include <FL/Fl_Input_.H>
#include <algorithm>
#include <vector>
#include <set>
#include "semaphore.h"
#include "helpdesk.h"
#include "encode.h"
#include "wordlist.h"
#include <random>
#include <string>
#include <ctype.h>
#include <unistd.h>
#include "wordlist.h"
#include "buffer.h"
#include "formatter.h"
#include "mailbox.h"
#include "mailhandler.h"
#include "checksum.h"
#include "var.h"
#include "reg.h"
#include "substr.h"
#include "strdupp.h"
#include <FL/fl_ask.H>
#include "eaddr.h"
#include "chop.h"
#include "base64.h"
#include "mailstorage.h"
#include "debug.h"
#include "version.h"
#include "mailheaders.h"

#include <FL/Fl_Pixmap.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Box.H>
#include <sys/stat.h>
#include <fcntl.h>
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
void update_folder (void);
void update_tree_counts (void);

extern int			 mdi_current;

extern std::string	 currentfrom[8];
extern std::string	 currentsubject[8];
extern std::string	 currentreplyto[8];
extern std::string	 currentto[8];
extern std::string	 currentcc[8];
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

		char				 msgid[256];
		char				*references;

		// Neo-PostOffice: the message replied to or forwarded, so its
		// mailbox can flag it (Maildir R / P) when this one is sent.
		char				 origbox[64];
		char				 origid[256];
		char				 origflag;

		// Neo-PostOffice: the OutBox draft this window edits; it is removed
		// once the corrected message is queued.
		char				 draftid[256];

		// Neo-PostOffice: the files attached (their contents, read when
		// attached), and the row under Subject that lists them.
		struct Attachment { std::string Name, Type, Data; };
		std::vector<Attachment> atts;
		Fl_Group			*gr_atts;
		Fl_Hold_Browser		*br_atts;
		Fl_Button			*bt_att_remove;
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

// Attachments (Neo-PostOffice; defined below).
static void composer_layout (Composer_Window *W, bool full);
static void cb_compose_attach (Fl_Widget *, void *);
static void cb_compose_att_remove (Fl_Widget *, void *);
static void composer_file_drop (Fl_Widget *, const char *);
static void attach_parts_of (Composer_Window *, MailMessage *, int);

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
	W->draftid[0]		= 0;
	W->references		= NULL;		// set by each caller
	W->bt_address 		= compose_bt_address;
	W->bt_address2		= compose_bt_address2;
	W->bt_menu_expand	= bt_compose_menu_expand;
	W->bt_resizeheader	= compose_bt_resizeheader;

	W->in_message 		= compose_in_message;
	W->in_to	  		= compose_in_to;
	W->in_cc			= compose_in_cc;
	W->in_bcc			= compose_in_bcc;
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
	
	W->in_message->WantTabs (false);

	// The attachments row (Neo-PostOffice): under Subject, shown by
	// composer_layout only when something is attached.
	{
		int ax = W->in_subject->x (), aw = W->in_subject->w (), ay = W->in_subject->y () + 25;
		W->gr_headers->begin ();
		W->gr_atts = new Fl_Group (ax, ay, aw, 48);
		W->br_atts = new Fl_Hold_Browser (ax, ay, aw - 76, 48, "Attachments");
		W->br_atts->labelsize (12);
		W->br_atts->textsize (12);
		W->br_atts->align (FL_ALIGN_LEFT);
		W->br_atts->format_char (0);		// names are shown as they are
		W->bt_att_remove = new Fl_Button (ax + aw - 72, ay, 72, 20, "Remove");
		W->bt_att_remove->labelsize (12);
		W->bt_att_remove->tooltip ("Remove the selected attachment");
		W->bt_att_remove->callback ((Fl_Callback *) cb_compose_att_remove, (void *) W);
		W->gr_atts->resizable (W->br_atts);
		W->gr_atts->end ();
		W->gr_headers->end ();
		W->gr_atts->hide ();
	}
	compose_bt_attach->callback ((Fl_Callback *) cb_compose_attach, (void *) W);
	compose_bt_attach_mini->callback ((Fl_Callback *) cb_compose_attach, (void *) W);
	Fl_Editor::file_drop_hook = composer_file_drop;
	
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
	
	return W;
}

// Edit menu and Cut/Copy/Paste buttons (Neo-PostOffice): they work on the
// header field you are typing in, otherwise on the message text.
static Composer_Window *composer_of (Fl_Widget *w)
{
	Fl_Window *win = w ? w->top_window () : NULL;
	for (Composer_Window *W : open_composers)
		if (W->comp == win) return W;
	return NULL;
}

static Fl_Input_ *composer_field (Composer_Window *W)
{
	Fl_Input_ *in = Fl::focus () ? dynamic_cast<Fl_Input_ *> (Fl::focus ()) : NULL;
	return (in && in->top_window () == W->comp) ? in : NULL;
}

enum { EDIT_CUT, EDIT_COPY, EDIT_PASTE, EDIT_ALL };

static void composer_edit (Fl_Widget *w, int what)
{
	Composer_Window *W = composer_of (w);
	if (!W) return;
	Fl_Input_ *in = composer_field (W);
	if (in)
	{
		switch (what)
		{
			case EDIT_CUT:
				if (in->insert_position () != in->mark ()) { in->copy (1); in->cut (); }
				break;
			case EDIT_COPY:
				if (in->insert_position () != in->mark ()) in->copy (1);
				break;
			case EDIT_PASTE: Fl::paste (*in, 1); break;
			case EDIT_ALL: in->insert_position (in->size (), 0); break;
		}
		return;
	}
	switch (what)
	{
		case EDIT_CUT: W->in_message->Cut (); break;
		case EDIT_COPY: W->in_message->Copy (); break;
		case EDIT_PASTE: W->in_message->Paste (); break;
		case EDIT_ALL: W->in_message->SelectAll (); break;
	}
	Fl::focus (W->in_message);
	W->in_message->redraw ();
}

void cb_compose_cut (Fl_Menu_ *m, void *) { composer_edit (m, EDIT_CUT); }
void cb_compose_copy (Fl_Menu_ *m, void *) { composer_edit (m, EDIT_COPY); }
void cb_compose_paste (Fl_Menu_ *m, void *) { composer_edit (m, EDIT_PASTE); }
void cb_compose_select_all (Fl_Menu_ *m, void *) { composer_edit (m, EDIT_ALL); }
void cb_compose_cut_b (Fl_Button *b, void *) { composer_edit (b, EDIT_CUT); }
void cb_compose_copy_b (Fl_Button *b, void *) { composer_edit (b, EDIT_COPY); }
void cb_compose_paste_b (Fl_Button *b, void *) { composer_edit (b, EDIT_PASTE); }

void cb_compose (Fl_Menu_ */*m*/, void */*d*/)
{
	Composer_Window *W = create_composer_window();
	
	W->msgid[0]			= 0;
	W->references		= strdupp ("");
}

void open_url (const char *url);

// Bug reports go to the project's GitHub issues: the browser opens a new
// issue with the version details filled in.
void cb_bugreport (Fl_Button */*b*/, void */*v*/)
{
	WordList WT;
	WT.MakeGeneric ((char *) _UNAME, '#');
	win_buildinfo->hide();

	std::string body;
	body += "### What happened\n\n\n\n";
	body += "### How to make it happen again\n\n\n\n";
	body += "### What you expected\n\n\n\n";
	body += "### Version\n\n```\n";
	body += std::string ("Version:      ") + _RELEASE + " build " + _BUILD + "\n";
	body += std::string ("Architecture: ") + _ARCH + "\n";
	body += std::string ("Kernel:       ") + (WT.Count ? WT.Get (0) : "") + "\n";
	body += std::string ("Build info:   ") + _BUILDINFO + "\n";
	body += "```\n";

	auto encode = [](const std::string &in)
	{
		static const char hex[] = "0123456789ABCDEF";
		std::string out;
		for (unsigned char c : in)
		{
			if (isalnum (c) || strchr ("-_.~", c)) out += (char) c;
			else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
		}
		return out;
	};
	std::string url = "https://github.com/dleecurt/Neo-Postoffice/issues/new?title=" +
					  encode ("Bug: ") + "&body=" + encode (body);
	open_url (url.c_str ());
	fl_message ("Your web browser opens a new bug report on GitHub, with the version\n"
				"details filled in. A GitHub account is needed to send it.\n\n"
				"If no browser opens: https://github.com/dleecurt/Neo-Postoffice/issues");
}

// The addresses (lower case) of all identities, and the identity that
// owns one of the given addresses (the first match), for replies.
static std::set<std::string> own_addresses (void)
{
	std::set<std::string> own;
	Var *ids = PREFERENCES.Get_collection ("postoffice")->Get_collection ("identities");
	WordList *X = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("identities");
	for (int i = 0; i < X->Count; ++i)
	{
		std::string k = mailheaders::AddressKey (ids->Get_collection (X->Get (i))->Get_char ("mailfrom"));
		if (!k.empty ()) own.insert (k);
	}
	return own;
}

static void choose_identity (Composer_Window *W, const std::vector<std::string> &items)
{
	Var *ids = PREFERENCES.Get_collection ("postoffice")->Get_collection ("identities");
	WordList *X = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("identities");
	for (const std::string &item : items)
	{
		std::string key = mailheaders::AddressKey (item);
		if (key.empty ()) continue;
		for (int i = 0; i < X->Count; ++i)
		{
			Var *I = ids->Get_collection (X->Get (i));
			if (mailheaders::AddressKey (I->Get_char ("mailfrom")) != key) continue;
			int k = W->ch_personality->find_index (I->Get_char ("shortname"));
			if (k >= 0) W->ch_personality->value (k);
			return;
		}
	}
}

// Joins list items into one field, leaving out own addresses and
// addresses already used (seen is updated).
static std::string join_new (const std::vector<std::string> &items, const std::set<std::string> &own,
							 std::set<std::string> &seen)
{
	std::string out;
	for (const std::string &item : items)
	{
		std::string key = mailheaders::AddressKey (item);
		if (key.empty () || own.count (key) || seen.count (key)) continue;
		seen.insert (key);
		if (!out.empty ()) out += ", ";
		out += item;
	}
	return out;
}

static void doreply (bool all)
{
	if (!currentmsgid[mdi_current][0]) return;
	int i;
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
	
	quoted_body->AddF ("%s tapped some keys and produced:\n\n", currentfrom[mdi_current].c_str ());
	
	for (i=0;i<quoted_lines->Count;++i)
	{
		quoted_body->AddF ("%s\n", quoted_lines->Get(i));
	}
	quoted_body->Add ((unsigned char) 0);
	
	delete (quoted_lines);

	// Add a "Re:" if it isn't already there in the subject
	
	const std::string &subj = currentsubject[mdi_current];
	if (strncasecmp (subj.c_str (), "re:", 3) == 0) W->in_subject->value (subj.c_str ());
	else W->in_subject->value (("Re: " + subj).c_str ());

	// The recipients. The sender (Reply-To, else From) goes in To; Reply
	// All puts everyone else from To and CC in CC. Own addresses are left
	// out. A reply to a message you sent goes to its recipients instead.
	std::set<std::string> own = own_addresses (), seen;
	std::vector<std::string> sender = mailheaders::SplitAddresses (currentreplyto[mdi_current]);
	std::vector<std::string> to = mailheaders::SplitAddresses (currentto[mdi_current]);
	std::vector<std::string> cc = mailheaders::SplitAddresses (currentcc[mdi_current]);
	bool from_me = !sender.empty ();
	for (const std::string &item : sender)
		if (!own.count (mailheaders::AddressKey (item))) from_me = false;

	std::string to_field, cc_field;
	if (from_me)
	{
		to_field = join_new (to, own, seen);
		if (all) cc_field = join_new (cc, own, seen);
		if (to_field.empty ()) to_field = currentreplyto[mdi_current];	// sent to yourself
	}
	else
	{
		// Own addresses are kept in To when the sender is one of them.
		std::set<std::string> none;
		to_field = join_new (sender, none, seen);
		if (all)
		{
			std::vector<std::string> rest (to);
			rest.insert (rest.end (), cc.begin (), cc.end ());
			cc_field = join_new (rest, own, seen);
		}
	}
	W->in_to->value (to_field.c_str ());
	W->in_cc->value (cc_field.c_str ());

	// The identity the message was sent to.
	std::vector<std::string> rcpt (to);
	rcpt.insert (rcpt.end (), cc.begin (), cc.end ());
	if (from_me) rcpt.insert (rcpt.begin (), sender.begin (), sender.end ());
	choose_identity (W, rcpt);
	
	W->in_message->LoadFrom ((char *) quoted_body->Data);
	delete (quoted_body);
}

void cb_reply_m (Fl_Menu_ */*w*/, void */*d*/)
{
	doreply (false);
}

void cb_reply (Fl_Button */*w*/, void */*d*/)
{
	doreply (false);
}

void cb_reply_all_m (Fl_Menu_ */*w*/, void */*d*/)
{
	doreply (true);
}

void cb_reply_all (Fl_Button */*w*/, void */*d*/)
{
	doreply (true);
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
				// Shown decoded, as in the message view.
				const char *hv = M->HeaderValues->Get (i);
				bool addr = strcasecmp (tt, "From") == 0 || strcasecmp (tt, "To") == 0 ||
							strcasecmp (tt, "Cc") == 0 || strcasecmp (tt, "Reply-To") == 0;
				std::string dv = addr ? mailheaders::DecodeAddresses (hv) : mailheaders::Decode (hv);
				quoted_body->AddF ("%s: %s\n", tt, dv.c_str ());
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
	
	// The recipient is left for you to fill in.
	W->in_subject->value (("Fwd: " + currentsubject[mdi_current]).c_str ());
	
	W->in_message->LoadFrom ((char *) quoted_body->Data);
	delete (quoted_body);

	// The original's attachments go along (each can be removed).
	if (currentmailbox)
		if (MailMessage *O = currentmailbox->Find (currentmsgid[mdi_current]))
		{
			if (!(O->Flags & MMFL_INRAM) && (O->Flags & MMFL_HAVEBODY)) O->LoadBody ();
			if (O->Flags & MMFL_INRAM) attach_parts_of (W, O, 0);
		}
}

static void forget_composer (Composer_Window *W)
{
	open_composers.erase (std::remove (open_composers.begin(), open_composers.end(), W), open_composers.end());
}

//-------------------------------------------------------------------------------
// Attachments in the composer (Neo-PostOffice)
//-------------------------------------------------------------------------------

const char *file_request (const char *title, const char *dir, const char *name, bool folder);
bool part_is_attachment (const MailPart *);
std::string attachment_file_name (const MailPart *, int);
bool attachment_bytes (const MailPart *, std::string &);

// Above this the user is asked first: many servers refuse 10-25 MB, and
// base64 makes attachments a third bigger.
static const size_t ATTACH_WARN = 10 * 1024 * 1024;

static std::string size_label (size_t n)
{
	char buf[32];
	if (n < 1024) snprintf (buf, sizeof (buf), "%zu bytes", n);
	else if (n < 1024 * 1024) snprintf (buf, sizeof (buf), "%.1f KB", n / 1024.0);
	else snprintf (buf, sizeof (buf), "%.1f MB", n / (1024.0 * 1024.0));
	return buf;
}

// The MIME type by the file's extension; application/octet-stream if unknown.
static std::string guess_type (const std::string &name)
{
	static const struct { const char *ext, *type; } known[] = {
		{ "txt", "text/plain" }, { "text", "text/plain" }, { "log", "text/plain" }, { "md", "text/plain" },
		{ "csv", "text/csv" }, { "html", "text/html" }, { "htm", "text/html" }, { "xml", "text/xml" },
		{ "ics", "text/calendar" }, { "vcf", "text/vcard" }, { "pdf", "application/pdf" },
		{ "jpg", "image/jpeg" }, { "jpeg", "image/jpeg" }, { "png", "image/png" }, { "gif", "image/gif" },
		{ "webp", "image/webp" }, { "svg", "image/svg+xml" }, { "bmp", "image/bmp" }, { "tif", "image/tiff" },
		{ "tiff", "image/tiff" }, { "zip", "application/zip" }, { "gz", "application/gzip" },
		{ "tgz", "application/gzip" }, { "bz2", "application/x-bzip2" }, { "xz", "application/x-xz" },
		{ "7z", "application/x-7z-compressed" }, { "tar", "application/x-tar" },
		{ "doc", "application/msword" }, { "xls", "application/vnd.ms-excel" },
		{ "ppt", "application/vnd.ms-powerpoint" },
		{ "docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document" },
		{ "xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
		{ "pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation" },
		{ "odt", "application/vnd.oasis.opendocument.text" },
		{ "ods", "application/vnd.oasis.opendocument.spreadsheet" },
		{ "odp", "application/vnd.oasis.opendocument.presentation" },
		{ "rtf", "application/rtf" }, { "json", "application/json" }, { "mp3", "audio/mpeg" },
		{ "ogg", "audio/ogg" }, { "wav", "audio/wav" }, { "mp4", "video/mp4" }, { "eml", "message/rfc822" },
	};
	size_t dot = name.rfind ('.');
	if (dot == std::string::npos) return "application/octet-stream";
	std::string ext = name.substr (dot + 1);
	for (char &c : ext) c = tolower ((unsigned char) c);
	for (auto &k : known) if (ext == k.ext) return k.type;
	return "application/octet-stream";
}

// Base64 in lines of 76 characters (RFC 2045).
static std::string base64_lines (const std::string &in)
{
	static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve (in.size () * 4 / 3 + in.size () / 57 * 2 + 8);
	int col = 0;
	size_t i = 0;
	auto put = [&](char c) { out += c; if (++col == 76) { out += '\n'; col = 0; } };
	for (; i + 2 < in.size (); i += 3)
	{
		unsigned v = ((unsigned char) in[i] << 16) | ((unsigned char) in[i + 1] << 8) | (unsigned char) in[i + 2];
		put (b64[v >> 18]); put (b64[(v >> 12) & 63]); put (b64[(v >> 6) & 63]); put (b64[v & 63]);
	}
	if (i + 1 == in.size ())
	{
		unsigned v = (unsigned char) in[i] << 16;
		put (b64[v >> 18]); put (b64[(v >> 12) & 63]); put ('='); put ('=');
	}
	else if (i + 2 == in.size ())
	{
		unsigned v = ((unsigned char) in[i] << 16) | ((unsigned char) in[i + 1] << 8);
		put (b64[v >> 18]); put (b64[(v >> 12) & 63]); put (b64[(v >> 6) & 63]); put ('=');
	}
	if (col) out += '\n';
	return out;
}

static size_t attached_total (Composer_Window *W)
{
	size_t n = 0;
	for (auto &a : W->atts) n += a.Data.size ();
	return n;
}

// The list under Subject, and the header area around it.
static void refresh_attachments (Composer_Window *W)
{
	W->br_atts->clear ();
	for (auto &a : W->atts)
		W->br_atts->add ((a.Name + "   (" + size_label (a.Data.size ()) + ", " + a.Type + ")").c_str ());
	char tip[96];
	snprintf (tip, sizeof (tip), "%d file%s, %s in all", (int) W->atts.size (), W->atts.size () == 1 ? "" : "s",
			  size_label (attached_total (W)).c_str ());
	W->br_atts->copy_tooltip (tip);
	composer_layout (W, W->gr_cc->visible ());
}

// Adds an attachment, asking first if the total passes ATTACH_WARN.
static bool add_attachment (Composer_Window *W, const std::string &name, const std::string &type, std::string data)
{
	size_t total = attached_total (W) + data.size ();
	if (total > ATTACH_WARN &&
		fl_choice ("With \"%s\" the attachments come to %s.\n\n"
				   "Many mail servers refuse messages over 10 to 25 MB, and attachments grow\n"
				   "by about a third when sent. Attach it anyway?",
				   "Cancel", "Attach", NULL, name.c_str (), size_label (total).c_str ()) != 1)
		return false;
	W->atts.push_back ({ name, type, std::move (data) });
	return true;
}

// Reads a file and attaches it. Says why if it cannot.
static bool attach_file (Composer_Window *W, const std::string &path)
{
	struct stat st;
	if (stat (path.c_str (), &st) != 0) { fl_alert ("Cannot attach %s: %s.", path.c_str (), strerror (errno)); return false; }
	if (!S_ISREG (st.st_mode)) { fl_alert ("Cannot attach %s: only files can be attached, not folders.", path.c_str ()); return false; }
	int fd = open (path.c_str (), O_RDONLY | O_CLOEXEC);
	if (fd < 0) { fl_alert ("Cannot attach %s: %s.", path.c_str (), strerror (errno)); return false; }
	std::string data;
	data.reserve ((size_t) st.st_size);
	char buf[65536];
	ssize_t n;
	while ((n = read (fd, buf, sizeof (buf))) > 0) data.append (buf, (size_t) n);
	int err = n < 0 ? errno : 0;
	close (fd);
	if (err) { fl_alert ("Cannot read %s: %s.", path.c_str (), strerror (err)); return false; }
	size_t slash = path.rfind ('/');
	std::string name = slash == std::string::npos ? path : path.substr (slash + 1);
	if (slash != std::string::npos)
		PREFERENCES.Set_char_tree ("postoffice/state/attachments/attachfolder", (char *) (slash ? path.substr (0, slash) : std::string ("/")).c_str ());
	return add_attachment (W, name, guess_type (name), std::move (data));
}

// The Attach button: the file requester, in the folder used last.
static void cb_compose_attach (Fl_Widget *, void *v)
{
	Composer_Window *W = (Composer_Window *) v;
	std::string dir = PREFERENCES.Get_char_tree ("postoffice/state/attachments/attachfolder");
	struct stat st;
	if (dir.empty () || stat (dir.c_str (), &st) != 0 || !S_ISDIR (st.st_mode))
		dir = getenv ("HOME") ? getenv ("HOME") : "/";
	const char *path = file_request ("Attach a file", dir.c_str (), "", false);
	if (!path || !*path) return;
	if (attach_file (W, path)) refresh_attachments (W);
}

static void cb_compose_att_remove (Fl_Widget *, void *v)
{
	Composer_Window *W = (Composer_Window *) v;
	int i = W->br_atts->value ();
	if (i < 1 || i > (int) W->atts.size ())
	{
		if (W->atts.size () != 1) { fl_alert ("Select the attachment to remove."); return; }
		i = 1;
	}
	W->atts.erase (W->atts.begin () + (i - 1));
	refresh_attachments (W);
}

// Files dropped on the message text (a text/uri-list of file:// addresses).
static void composer_file_drop (Fl_Widget *w, const char *uris)
{
	Composer_Window *W = composer_of (w);
	if (!W) return;
	bool any = false;
	std::string list = uris ? uris : "";
	size_t pos = 0;
	while (pos < list.size ())
	{
		size_t end = list.find ('\n', pos);
		if (end == std::string::npos) end = list.size ();
		std::string line = list.substr (pos, end - pos);
		pos = end + 1;
		while (!line.empty () && (line.back () == '\r' || line.back () == ' ')) line.pop_back ();
		if (line.compare (0, 7, "file://") != 0) continue;
		size_t slash = line.find ('/', 7);		// file:///path or file://host/path
		if (slash == std::string::npos) continue;
		std::string path;
		for (size_t k = slash; k < line.size (); ++k)
		{
			if (line[k] == '%' && k + 2 < line.size () && isxdigit ((unsigned char) line[k + 1]) && isxdigit ((unsigned char) line[k + 2]))
			{
				path += (char) strtol (line.substr (k + 1, 2).c_str (), NULL, 16);
				k += 2;
			}
			else path += line[k];
		}
		if (attach_file (W, path)) any = true;
	}
	if (any) refresh_attachments (W);
}

// The attachments of a message (a forwarded original, or a draft from
// part skip on), with their transfer encoding undone.
static void attach_parts_of (Composer_Window *W, MailMessage *M, int skip)
{
	if (!M) return;
	for (int i = skip; i < M->Count; ++i)
	{
		MailPart *P = M->GetPart (i);
		if (!part_is_attachment (P)) continue;
		std::string data;
		if (!attachment_bytes (P, data)) continue;
		std::string type = P->ContentType ? P->ContentType : "application/octet-stream";
		size_t semi = type.find (';');
		if (semi != std::string::npos) type.erase (semi);
		if (type == "text/uuencoded") type = guess_type (attachment_file_name (P, i));
		W->atts.push_back ({ attachment_file_name (P, i), type, std::move (data) });
	}
	refresh_attachments (W);
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

// A Message-ID (RFC 5322 3.6.4) on the sender's domain, so it is unique to
// it and does not give away (or make up) the machine's name.
// A header field's text on one line: line breaks and other control
// characters (pasted, or decoded from a received header) become spaces, so
// a value cannot start another header.
static std::string one_line (const char *v)
{
	std::string out;
	for (const char *p = v ? v : ""; *p; ++p)
	{
		unsigned char c = *p;
		out += (c < 0x20 && c != '\t') || c == 0x7f ? ' ' : (char) c;
	}
	return out;
}

char *make_msgid (const char *email);
char *make_msgid (const char *email)
{
	std::string domain;
	if (email)
	{
		const char *at = strrchr (email, '@');
		if (at)
			for (const char *c = at + 1; *c && *c != '>' && *c != ' ' && *c != '\t'; ++c) domain += *c;
	}
	if (domain.empty ())
	{
		char host[256] = "";
		if (gethostname (host, sizeof (host) - 1) == 0 && strchr (host, '.')) domain = host;
		else domain = "localhost.localdomain";
	}
	std::random_device rd;
	unsigned long long r = ((unsigned long long) rd () << 32) | rd ();
	char *result = new char[64 + domain.size ()];
	sprintf (result, "<PstOfc.%08x.%016llx@%s>", (unsigned int) time (NULL), r, domain.c_str ());
	return result;
}

// The Date header (RFC 5322 3.3): English names whatever the locale, and
// the offset from UTC as a number (the old code always wrote the summer
// time zone name, an hour off in winter).
static std::string rfc5322_date (time_t t)
{
	static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
									"Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	struct tm tm;
	localtime_r (&t, &tm);
	long off = tm.tm_gmtoff / 60;
	char sign = off < 0 ? '-' : '+';
	if (off < 0) off = -off;
	char buf[64];
	snprintf (buf, sizeof (buf), "%s, %d %s %04d %02d:%02d:%02d %c%02ld%02ld", days[tm.tm_wday], tm.tm_mday,
			  months[tm.tm_mon], tm.tm_year + 1900, tm.tm_hour, tm.tm_min, tm.tm_sec, sign, off / 60, off % 60);
	return buf;
}

void cb_sendmessage (Fl_Window */*win*/, Composer_Window *W)
{
	// Nothing to send to: say so, and keep the window open.
	auto blank = [](const char *v) { for (; v && *v; ++v) if (!isspace ((unsigned char) *v)) return false; return true; };
	if (blank (W->in_to->value()) && blank (W->in_cc->value()) && blank (W->in_bcc->value()))
	{
		fl_alert ("Enter at least one recipient (To, CC or BCC).");
		return;
	}

	int i;
	WordList *HdrNames;
	WordList *HdrValues;
	WordList *RCPT = new WordList;
	time_t ti;
//	char emailto[256];
//	char nameto[256];
	char buf[512];
	char *crsr;
//	char *error;
	char *mid;
	char *ref;

	ti = time (NULL);

	const char *pers_from = NULL;
	const char *pers_email = NULL;
	char *pers_replyto = NULL;
	char *pers_org = NULL;
	char *sig = NULL;

	// Get the right headers for the personality that the user selected
	
	// This window's identity (compose_ch_personality is the last window's).
	const char *pers_val = W->ch_personality->text (W->ch_personality->value());
	if (!pers_val) pers_val = "";
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

	// Create a new message for the OutBox

	MailMessage *M = new MailMessage (OUTBOX, mid = make_msgid (pers_email));
	HdrNames = M->HeaderNames;
	HdrValues = M->HeaderValues;
	
//	int crsrpos = 0;
//	char *tmp;
	

	if (!sig) sig = strdupp ("");

	// Actually I should tell the user to sod off and fix his prefs, but this'll work for now

	if (!pers_from) pers_from = "Localhost";
	if (!pers_email) pers_email = "root@localhost";
   
	// The edit buffer, the signature separator and the signature.
	char *body = new char[W->in_message->SaveSize()+16+strlen (sig)];
	W->in_message->SaveTo (body); // Store the edit buffer inside the body

	// The signature separator only before a signature (RFC 3676 4.3).
	bool has_sig = false;
	for (const char *c = sig; *c; ++c) if (!isspace ((unsigned char) *c)) { has_sig = true; break; }
	if (has_sig)
	{
		size_t n = strlen (body);
		if (n && body[n - 1] != '\n' && body[n - 1] != '\r') strcat (body, "\n");
		strcat (body, "-- \n");
		strcat (body, sig);
	}
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
	snprintf (buf, sizeof (buf), "%s", rfc5322_date (ti).c_str ());
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
	
	// Header values with non-ASCII text are written as RFC 2047 encoded
	// words; the addresses themselves are left alone (Neo-PostOffice).
	{
		std::string from = mailheaders::EncodeWords (one_line (pers_from)) + " <" + one_line (pers_email) + ">";
		HdrNames->Add ((char *) "From");
		HdrValues->Add ((char *) from.c_str());
	}
	if (strlen (W->in_to->value()))
	{
		HdrNames->Add ((char *) "To");
		HdrValues->Add ((char *) mailheaders::EncodeAddresses (one_line (W->in_to->value())).c_str());
	}

	
	if (strlen (W->in_cc->value()) > 0)
	{
		HdrNames->Add ((char *) "CC");
		HdrValues->Add ((char *) mailheaders::EncodeAddresses (one_line (W->in_cc->value())).c_str());
	}

	// BCC is just added as a regular header. It should be stripped when delivering.
	
	if (strlen (W->in_bcc->value()) > 0)
	{
		HdrNames->Add ((char *) "BCC");
		HdrValues->Add ((char *) mailheaders::EncodeAddresses (one_line (W->in_bcc->value())).c_str());
	}
	
	HdrNames->Add ((char *) "Subject");
	HdrValues->Add ((char *) mailheaders::EncodeWords (one_line (W->in_subject->value())).c_str());
	M->Envelope->Set (ENVELOPE_SUBJECT, (char *) W->in_subject->value());
	
	if (pers_replyto && strlen (pers_replyto) > 0)
	{
		HdrNames->Add ((char *) "Reply-To");
		HdrValues->Add ((char *) mailheaders::EncodeAddresses (one_line (pers_replyto)).c_str());
	}
	
	if (pers_org && *pers_org)
	{
		HdrNames->Add ("Organization");
		HdrValues->Add ((char *) mailheaders::EncodeWords (one_line (pers_org)).c_str());
	}

	HdrNames->Add ("X-Mailer");
	HdrValues->Add ( __VERSIONSTRING " (by " __BUILDINFO ")" );

	// The text is UTF-8; it says so, and whether it has 8-bit bytes
	// (without these headers, other programs had to guess the charset).
	HdrNames->Add ("MIME-Version");
	HdrValues->Add ("1.0");
	if (W->atts.empty ())
	{
		HdrNames->Add ("Content-Type");
		HdrValues->Add ("text/plain; charset=UTF-8");
		HdrNames->Add ("Content-Transfer-Encoding");
		HdrValues->Add (mailheaders::IsASCII (body) ? "7bit" : "8bit");
		M->AddPart ((char *) "text/plain", "", "", (char *) body);
	}
	else
	{
		// With attachments: multipart/mixed (its Content-Type, with the
		// boundary, is written when the message is sent). The text is the
		// first part, each file a base64 part after it.
		std::string text_type = std::string ("text/plain; charset=UTF-8; x-neo-cte=") +
								(mailheaders::IsASCII (body) ? "7bit" : "8bit");
		M->AddPart (text_type.c_str (), "", "", (char *) body);
		for (auto &a : W->atts)
		{
			// A whole message (message/rfc822) may not be base64 (RFC 2046
			// 5.2.1): it goes as text when it is text with lines of mail
			// length, else as a plain file.
			if (a.Type.compare (0, 8, "message/") == 0)
			{
				bool ok = a.Data.find ('\0') == std::string::npos;
				size_t line = 0;
				for (char c : a.Data) { if (c == '\n') line = 0; else if (++line > 998) { ok = false; break; } }
				if (ok)
				{
					M->AddPart ((a.Type + "; x-neo-cte=" + (mailheaders::IsASCII (a.Data) ? "7bit" : "8bit")).c_str (),
								"", a.Name.c_str (), a.Data.c_str ());
					continue;
				}
				M->AddPart ("application/octet-stream; x-neo-cte=base64", "", a.Name.c_str (), base64_lines (a.Data).c_str ());
				continue;
			}
			M->AddPart ((a.Type + "; x-neo-cte=base64").c_str (), "", a.Name.c_str (), base64_lines (a.Data).c_str ());
		}
	}

	// Store the message in the outbox
	
	OUTBOX->Deliver (M);

	// And save all shit.

	M->SaveHeaders();
	M->StoreFile();

	// Announce the world the birth of a new message

	OUTBOX->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, OUTBOX->VFirst->Name, M->MsgID);
	// Send it now (the outbox's own round is every 15 seconds); this also
	// saves the outbox's index, so a crash cannot lose the message.
	OUTBOX->ToCollector->PostMessage (CLASS_CHECKGROUP, NO_SUBCLASS, "");

	// A corrected draft replaces the draft.
	if (W->draftid[0] && OUTBOX->Find (W->draftid))
	{
		OUTBOX->ToUI->PostMessage (CLASS_REMARTICLE, NO_SUBCLASS, "Drafts", W->draftid);
		OUTBOX->RemoveMessage (W->draftid);
		OUTBOX->SaveMailBox ();
		W->draftid[0] = 0;
		// As Delete does: show the folder without it.
		if (currentmailbox == OUTBOX) update_folder ();
		update_tree_counts ();
	}

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

// Neo-PostOffice: lays the header rows out from fixed measurements (as in
// helpdesk.fld: rows 25 pixels apart, the first 5 below the top of the
// header area). The old code worked out each move from where the other
// rows were, so after a window resize it stacked every row on the To row.
// FLTK resizes a group's children from the sizes it recorded when they
// were made, so that record is renewed after the rows are moved.
static void composer_layout (Composer_Window *W, bool full)
{
	const int top = 5, inc = 25, rowh = 20;
	Fl_Group *H = W->gr_headers;
	int x = W->in_subject->x ();
	int y = H->y () + top;

	Fl_Widget *rows_full[] = { W->gr_personality, W->gr_to, W->gr_cc, W->gr_bcc, W->in_subject };
	Fl_Widget *optional[] = { W->gr_personality, W->gr_cc, W->gr_bcc };
	int n = full ? 5 : 2;

	// The attachments row under Subject, only when something is attached.
	const int atth = 48;
	bool atts = W->gr_atts && !W->atts.empty ();

	// The header area first, without FLTK moving its children (it would
	// stretch the To row into the new height), then the rows.
	int bottom = W->in_message->y () + W->in_message->h ();
	H->Fl_Widget::resize (H->x (), H->y (), H->w (), top + n * inc + (atts ? atth + 5 : 0));
	int i = 0;
	for (Fl_Widget *r : rows_full)
	{
		bool shown = full || r == W->gr_to || r == W->in_subject;
		if (!shown) continue;
		r->resize (x, y + i * inc, r->w (), rowh);
		++i;
	}
	for (Fl_Widget *r : optional)
	{
		if (full) r->show ();
		else { r->hide (); r->resize (x, y, r->w (), rowh); }
	}
	if (full) W->ch_personality->show ();
	if (W->gr_atts)
	{
		int ay = y + n * inc;
		W->gr_atts->resize (x, ay, W->in_subject->w (), atth);
		W->br_atts->resize (x, ay, W->in_subject->w () - 76, atth);
		W->bt_att_remove->resize (x + W->in_subject->w () - 72, ay, 72, 20);
		if (atts) W->gr_atts->show (); else W->gr_atts->hide ();
	}

	// Its drag handle, and the message below it.
	W->bt_resizeheader->resize (W->bt_resizeheader->x (), H->y (), W->bt_resizeheader->w (), H->h ());
	int my = H->y () + H->h ();
	W->in_message->resize (W->in_message->x (), my, W->in_message->w (), bottom - my);

	H->init_sizes ();
	if (H->parent ()) H->parent ()->init_sizes ();
	W->comp->init_sizes ();
	W->bt_resizeheader->callback ((Fl_Callback *) (full ? cb_compose_header_shrink : cb_compose_header_expand), (void *) W);
	W->comp->redraw ();
}

void cb_compose_header_expand (Fl_Widget */*x*/, Composer_Window *W)
{
	PREFERENCES.Set_char_tree ("postoffice/state/composer/headers","full");
	composer_layout (W, true);
}

void cb_compose_header_shrink (Fl_Widget */*x*/, Composer_Window *W)
{
	PREFERENCES.Set_char_tree ("postoffice/state/composer/headers","compact");
	composer_layout (W, false);
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


//===============================================================================
// CALLBACK		cb_edit_draft_m (Neo-PostOffice)
// SENDER		Message -> Edit Draft
// SYNOPSIS		opens a message from OutBox -> Drafts (one that could not be
//				sent) in a composer, to correct it and send it again. The
//				draft is removed when the new message is queued.
//===============================================================================

void cb_edit_draft_m (Fl_Menu_ */*m*/, void */*v*/)
{
	if (currentmailbox != OUTBOX || strcmp (currentfolder, "Drafts") != 0 || !currentmsgid[mdi_current][0])
	{
		fl_message ("Select a message in OutBox \xe2\x86\x92 Drafts first: messages that could not be sent"
					" are kept there.");
		return;
	}
	MailMessage *M = OUTBOX->Find (currentmsgid[mdi_current]);
	if (!M) return;
	if (!M->HeaderNames->Count) M->LoadHeaders ();
	if (!(M->Flags & MMFL_INRAM)) M->LoadBody ();

	Composer_Window *W = create_composer_window ();
	snprintf (W->draftid, sizeof (W->draftid), "%s", M->MsgID);
	W->in_to->value (mailheaders::Decode (M->Header ("To")).c_str ());
	W->in_cc->value (mailheaders::Decode (M->Header ("CC")).c_str ());
	W->in_bcc->value (mailheaders::Decode (M->Header ("BCC")).c_str ());
	W->in_subject->value (mailheaders::Decode (M->Header ("Subject")).c_str ());

	// The identity whose address sent it.
	char *from = eaddr2addr (M->Header ("From"));
	WordList *X = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("identities");
	for (int i = 0; from && i < X->Count; ++i)
	{
		Var *I = PREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (X->Get (i));
		char *addr = eaddr2addr (I->Get_char ("mailfrom"));
		bool match = addr && strcasecmp (addr, from) == 0;
		delete[] addr;
		if (match)
		{
			int k = W->ch_personality->find_index (I->Get_char ("shortname"));
			if (k >= 0) W->ch_personality->value (k);
			break;
		}
	}
	delete[] from;

	// A reply stays a reply: In-Reply-To, and References without it (the
	// send adds it again).
	snprintf (W->msgid, sizeof (W->msgid), "%s", M->Header ("In-Reply-To"));
	std::string refs = M->Header ("References");
	if (W->msgid[0])
	{
		size_t at = refs.rfind (W->msgid);
		if (at != std::string::npos) refs.erase (at);
		while (!refs.empty () && isspace ((unsigned char) refs.back ())) refs.pop_back ();
	}
	delete[] W->references;
	W->references = strdupp (refs.c_str ());

	// The text, without the signature (the send adds the identity's).
	MailPart *P = M->GetPart (0);
	std::string body = P && P->Contents ? P->Contents : "";
	size_t sig = body.rfind ("\n-- \n");
	if (sig != std::string::npos) body.erase (sig);
	else if (body.compare (0, 4, "-- \n") == 0) body.clear ();
	W->in_message->LoadFrom ((char *) body.c_str ());

	if (*M->Header ("CC") || *M->Header ("BCC"))
		cb_compose_header_expand (NULL, W);
	// Its attachments (the parts after the text).
	attach_parts_of (W, M, 1);
}
