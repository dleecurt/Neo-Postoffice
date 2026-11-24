// Redirect (Neo-PostOffice): the shown message is sent on unchanged to
// new recipients, with Resent-From, Resent-To, Resent-Date and
// Resent-Message-ID added on top (RFC 5322 3.6.6). The original's text is
// fetched by its mailbox's thread (MailBox::RedirectMessage), then queued
// in the OutBox here; the OutBox sends it to the Resent- recipients from
// the identity's server and keeps a copy in Sent, like any message.

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Menu_.H>
#include <FL/fl_ask.H>

#include <string>
#include <vector>
#include <time.h>
#include <string.h>
#include <strings.h>

#include "mailbox.h"
#include "mailhandler.h"
#include "mailstorage.h"
#include "itc.h"
#include "var.h"
#include "eaddr.h"
#include "mailheaders.h"

extern Var				 PREFERENCES;
extern class MailBox	*OUTBOX;
extern class MailBox	*MailBoxList[16];
extern MailBox			*currentmailbox;
extern int				 mdi_current;
extern char				 currentmsgid[8][256];
extern std::string		 currentsubject[8];
extern std::string		 currentto[8];
extern std::string		 currentcc[8];
char *make_msgid (const char *email);
std::string rfc5322_date (time_t t);
void update_tree_counts (void);

// The identities: short name, display name, address.
struct RedirIdentity { std::string Short, Name, Addr; };

static std::vector<RedirIdentity> identities (void)
{
	std::vector<RedirIdentity> out;
	Var *ids = PREFERENCES.Get_collection ("postoffice")->Get_collection ("identities");
	WordList *X = PREFERENCES.Get_collection ("postoffice")->Get_wordlist ("identities");
	for (int i = 0; i < X->Count; ++i)
	{
		Var *I = ids->Get_collection (X->Get (i));
		RedirIdentity r;
		r.Short = I->Get_char ("shortname");
		if (r.Short.empty ()) r.Short = X->Get (i);
		r.Name = I->Get_char ("realname");
		char *a = eaddr2addr ((char *) I->Get_char ("mailfrom"));
		r.Addr = a ? a : "";
		delete[] a;
		if (!r.Addr.empty ()) out.push_back (r);
	}
	return out;
}

struct RedirDialog
{
	Fl_Window *win;
	Fl_Choice *identity;
	Fl_Input *to;
	std::vector<RedirIdentity> ids;
	std::string msgid;
	MailBox *box;
};

static RedirDialog *dlg = NULL;

static void cb_redir_cancel (Fl_Widget *, void *)
{
	dlg->win->hide ();
}

static void cb_redir_ok (Fl_Widget *, void *)
{
	std::vector<std::string> items = mailheaders::SplitAddresses (dlg->to->value ());
	bool ok = !items.empty ();
	for (const std::string &item : items)
		if (mailheaders::AddressKey (item).find ('@') == std::string::npos) ok = false;
	if (!ok)
	{
		fl_alert ("Enter the address or addresses to redirect the message to, separated by commas.");
		return;
	}
	int k = dlg->identity->value ();
	if (k < 0 || k >= (int) dlg->ids.size ()) return;
	const RedirIdentity &I = dlg->ids[k];
	std::string from = I.Name.empty () ? I.Addr : mailheaders::EncodeWords (I.Name) + " <" + I.Addr + ">";
	// The mailbox may have gone since the dialog opened.
	bool alive = false;
	for (int b = 0; b < 16 && MailBoxList[b]; ++b) if (MailBoxList[b] == dlg->box) alive = true;
	if (alive)
		dlg->box->ToCollector->PostMessage (CLASS_REDIRECT, NO_SUBCLASS, dlg->msgid.c_str (), from.c_str (),
											mailheaders::EncodeAddresses (dlg->to->value ()).c_str ());
	dlg->win->hide ();
}

static void open_redirect (void)
{
	if (!currentmailbox || !currentmsgid[mdi_current][0])
	{
		fl_message ("Select the message to redirect first.");
		return;
	}
	std::vector<RedirIdentity> ids = identities ();
	if (ids.empty ())
	{
		fl_alert ("Set up an identity with an e-mail address first (Config -> Identities).");
		return;
	}
	if (!dlg)
	{
		dlg = new RedirDialog;
		dlg->win = new Fl_Window (480, 150, "Redirect Message");
		Fl_Box *info = new Fl_Box (10, 8, 460, 34);
		info->labelsize (12);
		info->align (FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
		info->copy_label ("The message is sent on unchanged, as it was received; the recipients see "
						  "the original sender and a Resent-From line with yours.");
		dlg->identity = new Fl_Choice (90, 48, 250, 22, "Identity");
		dlg->identity->labelsize (12);
		dlg->identity->textsize (12);
		dlg->to = new Fl_Input (90, 78, 380, 22, "To");
		dlg->to->labelsize (12);
		dlg->to->textsize (12);
		dlg->to->tooltip ("One or more addresses, separated by commas");
		Fl_Button *cancel = new Fl_Button (270, 116, 95, 24, "Cancel");
		cancel->labelsize (12);
		cancel->callback (cb_redir_cancel);
		Fl_Return_Button *ok = new Fl_Return_Button (375, 116, 95, 24, "Redirect");
		ok->labelsize (12);
		ok->callback (cb_redir_ok);
		dlg->win->end ();
		dlg->win->set_modal ();
	}
	dlg->ids = ids;
	dlg->identity->clear ();
	int pick = 0;
	// The identity the message was sent to, as for a reply.
	std::vector<std::string> rcpt = mailheaders::SplitAddresses (currentto[mdi_current]);
	std::vector<std::string> cc = mailheaders::SplitAddresses (currentcc[mdi_current]);
	rcpt.insert (rcpt.end (), cc.begin (), cc.end ());
	bool found = false;
	for (size_t i = 0; i < ids.size (); ++i)
	{
		std::string label = ids[i].Short + " <" + ids[i].Addr + ">";
		// '/', '&', '_' and '@' are special in menu labels.
		std::string esc;
		for (char c : label)
		{
			if (c == '@') { esc += "@@"; continue; }		// '@' starts a symbol in labels
			if (c == '/' || c == '&' || c == '\\' || c == '_') esc += '\\';
			esc += c;
		}
		dlg->identity->add (esc.c_str ());
		for (const std::string &r : rcpt)
			if (!found && strcasecmp (mailheaders::AddressKey (r).c_str (), ids[i].Addr.c_str ()) == 0)
			{
				pick = (int) i;
				found = true;
			}
	}
	dlg->identity->value (pick);
	dlg->to->value ("");
	dlg->msgid = currentmsgid[mdi_current];
	dlg->box = currentmailbox;
	std::string title = "Redirect: " + currentsubject[mdi_current];
	dlg->win->copy_label (title.c_str ());
	dlg->win->show ();
	dlg->to->take_focus ();
}

void cb_redirect_m (Fl_Menu_ *, void *) { open_redirect (); }
void cb_redirect (Fl_Button *, void *) { open_redirect (); }

// The original's text arrived: queue it in the OutBox. Its header is
// kept (unfolded, as the OutBox stores headers one per line) with the
// Resent- block on top, which tells the OutBox to send it to the Resent-
// recipients; the body is sent exactly as it came.
void redirect_queue (const char *raw, const char *from, const char *to)
{
	std::string text (raw);
	size_t end = text.find ("\n\n");
	size_t crend = text.find ("\r\n\r\n");
	size_t bodypos;
	if (crend != std::string::npos && (end == std::string::npos || crend < end)) bodypos = crend + 4;
	else if (end != std::string::npos) bodypos = end + 2;
	else bodypos = text.size ();
	std::vector<mailheaders::Header> hdrs = mailheaders::Parse (text.substr (0, bodypos));

	char *addr = eaddr2addr ((char *) from);
	char *mid = make_msgid (addr ? addr : from);
	delete[] addr;
	MailMessage *M = new MailMessage (OUTBOX, mid);
	std::string date = rfc5322_date (time (NULL));

	M->HeaderNames->Add ("Resent-From");		M->HeaderValues->Add (from);
	M->HeaderNames->Add ("Resent-To");			M->HeaderValues->Add (to);
	M->HeaderNames->Add ("Resent-Date");		M->HeaderValues->Add (date.c_str ());
	M->HeaderNames->Add ("Resent-Message-ID");	M->HeaderValues->Add (mid);
	std::string subject;
	for (const mailheaders::Header &h : hdrs)
	{
		// Return-Path is the last server's record of the envelope sender;
		// the next one writes its own.
		if (strcasecmp (h.Name.c_str (), "Return-Path") == 0) continue;
		if (strcasecmp (h.Name.c_str (), "Subject") == 0) subject = mailheaders::Decode (h.Value);
		M->HeaderNames->Add (h.Name.c_str ());
		M->HeaderValues->Add (h.Value.c_str ());
	}

	{
		std::string name, a;
		mailheaders::SplitAddress (from, name, a);
		name = mailheaders::Decode (name);
		M->Envelope->Set (ENVELOPE_NAMEFROM, (char *) (name.empty () ? a : name).c_str ());
	}
	addr = eaddr2addr ((char *) from);
	M->Envelope->Set (ENVELOPE_MAILFROM, addr ? addr : (char *) from);
	delete[] addr;
	M->Envelope->Set (ENVELOPE_GROUPS, "Queue");
	M->Envelope->Set (ENVELOPE_SUBJECT, (char *) ("Redirected: " + subject).c_str ());
	M->Envelope->Set (ENVELOPE_DATE, (char *) date.c_str ());
	M->Flags = MMFL_ORIGINAL | MMFL_HAVEBODY | MMFL_INRAM;
	M->AddPart ("text/plain", "", "", text.c_str () + bodypos);

	OUTBOX->Deliver (M);
	M->SaveHeaders ();
	M->StoreFile ();
	OUTBOX->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, OUTBOX->VFirst->Name, M->MsgID);
	OUTBOX->ToCollector->PostMessage (CLASS_CHECKGROUP, NO_SUBCLASS, "");
	update_tree_counts ();
	delete[] mid;
}

// True for a message queued by Redirect (its header starts with the
// Resent- block).
bool is_redirect (MailMessage *M)
{
	return M && M->HeaderNames->Count && strcasecmp (M->HeaderNames->Get (0), "Resent-From") == 0;
}
