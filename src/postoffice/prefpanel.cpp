#include "flowed.h"
#include <time.h>
#include "semaphore.h"
#include "helpdesk.h"
#include "var.h"
#include "mailbox.h"
#include "encode.h"
#include "prefjournal.h"
#include "maccheck.h"
#include "style.h"
#include <FL/Fl_Pixmap.H>
#include <FL/Fl_Multi_Browser.H>
#include "podb.h"
#include <FL/fl_ask.H>
#include "certstore.h"


#define FL_SCREEN_BOLD_ITALIC FL_SCREEN_BOLD
#define FL_SCREEN_ITALIC FL_SCREEN

extern Stylist _STYLES;

extern ITCPipe		 *PIPE;
extern Fl_Scroll	 *current_boxlist;

extern Fl_Pixmap	  pixmap_folder;
extern Fl_Pixmap	  pixmap_folder_open;
extern Fl_Pixmap	  pixmap_mbox;
extern Fl_Pixmap	  pixmap_identity;
extern ITCPipe		 *PIPE;

extern const char *single_file_request (void);

static unsigned char *image_radio_off[] = {
(unsigned char *)"16 16 17 1",
(unsigned char *)" \tc None",
(unsigned char *)".\tc #626062",
(unsigned char *)"+\tc #0C0C0C",
(unsigned char *)"@\tc #050505",
(unsigned char *)"#\tc #1B1B1B",
(unsigned char *)"$\tc #3F3E3F",
(unsigned char *)"%\tc #9D9E9D",
(unsigned char *)"&\tc #B8B9B8",
(unsigned char *)"*\tc #AAABAA",
(unsigned char *)"=\tc #545454",
(unsigned char *)"-\tc #CFCFCF",
(unsigned char *)";\tc #DDDDDD",
(unsigned char *)">\tc #888888",
(unsigned char *)",\tc #8F908F",
(unsigned char *)"\'\tc #302F30",
(unsigned char *)")\tc #777977",
(unsigned char *)"!\tc #474547",
(unsigned char *)"                ",
(unsigned char *)"                ",
(unsigned char *)"      .+@#      ",
(unsigned char *)"    $#%&&*=@    ",
(unsigned char *)"   $=&-;;;->@   ",
(unsigned char *)"   .,-;;;;-*$   ",
(unsigned char *)"  .>-;;;---*>\'  ",
(unsigned char *)"  \');;;---*%>@  ",
(unsigned char *)"  \');;--&*%%>@  ",
(unsigned char *)"  .);;-&*%,,)$  ",
(unsigned char *)"   .,-&*%,,>\'   ",
(unsigned char *)"   !$,%%,,))@   ",
(unsigned char *)"    $\'...)!@    ",
(unsigned char *)"      )+@\'      ",
(unsigned char *)"                ",
(unsigned char *)"                "
};
static Fl_Pixmap pixmap_radio_off(image_radio_off);

static unsigned char *image_radio_on[] = {
(unsigned char *)"16 16 17 1",
(unsigned char *)" \tc None",
(unsigned char *)".\tc #9C9D9C",
(unsigned char *)"+\tc #525052",
(unsigned char *)"@\tc #626162",
(unsigned char *)"#\tc #A4A5A4",
(unsigned char *)"$\tc #393839",
(unsigned char *)"%\tc #101010",
(unsigned char *)"&\tc #737173",
(unsigned char *)"*\tc #4A484A",
(unsigned char *)"=\tc #8B8D8B",
(unsigned char *)"-\tc #202020",
(unsigned char *)";\tc #B4B6B4",
(unsigned char *)">\tc #080808",
(unsigned char *)",\tc #C5C6C5",
(unsigned char *)"\'\tc #EEEEEE",
(unsigned char *)")\tc #FFFFFF",
(unsigned char *)"!\tc #D5D6D5",
(unsigned char *)"                ",
(unsigned char *)"                ",
(unsigned char *)"      .++@      ",
(unsigned char *)"    #@$%%%&*    ",
(unsigned char *)"   #$%+&&=@+*   ",
(unsigned char *)"   *-&*---.;%   ",
(unsigned char *)"  #$+*>>>>%=.=  ",
(unsigned char *)"  *-&->>>>>.,>  ",
(unsigned char *)"  *-&->>>>>.\'>  ",
(unsigned char *)"  *-=->>>>>#)-  ",
(unsigned char *)"   +@=%>>>&)=   ",
(unsigned char *)"   *@;&==.\')>   ",
(unsigned char *)"    +-#!\')&>    ",
(unsigned char *)"      &%>$      ",
(unsigned char *)"                ",
(unsigned char *)"                "
};
static Fl_Pixmap pixmap_radio_on(image_radio_on);

extern Var PREFERENCES;
Var LPREFERENCES;
extern MailBox *MailBoxList[16];
extern MailBox *OUTBOX;

extern void update_tree (Fl_Scroll *);

Var *PREFS_CURRENTBOX = (Var *) NULL;
Var *PREFS_CURRENTIDENT = (Var *) NULL;
Var *PREFS_FCURRENTBOX = (Var *) NULL;

int PREFS_MBOX_CHANGE;
PrefJournal JOURNAL;

//extern Fl_Pixmap pixmap_radio_on;
//extern Fl_Pixmap pixmap_radio_off;

// The items of the protocol list (Config -> Mailboxes), in menu order.
enum { PROTO_SQL = 0, PROTO_POP3, PROTO_MBOX, PROTO_IMAP4, PROTO_MAILDIR, PROTO_MH };

//===============================================================================
// FUNCTION		func_view_protocol
// SYNOPSIS		Updates the display area for protocol-specific entries in the
//				"MailBoxes" tab.
//
// ARGUMENTS	_MaxSize	(int)		The desired maximum size of the buffer.
//										This is not an absolute number as much as
//										a guideline.
//===============================================================================

#include <string>
extern MailBox		*currentmailbox;
extern char			 currentfolder[64];

void show_delivery (void);
void certs_refresh (void);

// POP3 and IMAP share the server fields (host, port, encryption, login,
// CA file); only the labels and default ports differ.
// Push on or off. Handlers keep a copy of their settings, so the
// mailboxes are made again at Apply for this to take effect at once.
void cb_prefs_idle (Fl_Check_Button *b, void *)
{
	if (!PREFS_CURRENTBOX) return;
	PREFS_CURRENTBOX->Set_char ("idle", (char *) (b->value () ? "Y" : "N"));
	PREFS_MBOX_CHANGE = 1;
}

static void server_fields_for (bool imap)
{
	lb_prefs_protocol_pop3_host->label (imap ? "IMAP Host" : "POP3 Host");
	in_prefs_protocol_pop3_port->tooltip (imap ? "Empty: 993 for TLS, 143 otherwise."
											   : "Empty: 995 for TLS, 110 otherwise.");
	Fl_Menu_Item *m = (Fl_Menu_Item *) ch_prefs_protocol_pop3_security->menu ();
	m[0].label (imap ? "TLS (port 993)" : "TLS (port 995)");
	m[1].label (imap ? "STARTTLS (port 143)" : "STARTTLS (port 110)");
	gr_prefs_protocol_pop3->redraw ();
}

static void load_server_fields (bool imap)
{
	in_prefs_protocol_pop3_host->value (PREFS_CURRENTBOX->Get_char ("host"));
	in_prefs_protocol_pop3_user->value (PREFS_CURRENTBOX->Get_char ("user"));
	in_prefs_protocol_pop3_pass->value (PREFS_CURRENTBOX->Get_char ("pass"));
	in_prefs_protocol_pop3_port->value (PREFS_CURRENTBOX->Get_int ("port") > 0 ? PREFS_CURRENTBOX->Get_char ("port") : "");
	in_prefs_protocol_pop3_passcmd->value (PREFS_CURRENTBOX->Get_char ("passcmd"));
	in_prefs_protocol_pop3_cafile->value (PREFS_CURRENTBOX->Get_char ("cafile"));
	// Anything but TLS is STARTTLS: there is no unencrypted login (also not
	// for "none" or the empty setting of mailboxes from PostOffice 0.7).
	const char *sec = PREFS_CURRENTBOX->Get_char ("security");
	ch_prefs_protocol_pop3_security->value (strcmp (sec, "tls") == 0 ? 0 : 1);
	(void) imap;
}

void func_view_protocol (void)
{
	int val = ch_prefs_protocol->value();

	if (PREFS_CURRENTBOX)
	{
		if (strlen (PREFS_CURRENTBOX->Get_char ("interval")) == 0) PREFS_CURRENTBOX->Set_char ("interval","0");
		in_prefs_interval->value (PREFS_CURRENTBOX->Get_char ("interval"));
	}
	
	gr_prefs_protocol_sql->hide();
	gr_prefs_protocol_pop3->hide();
	gr_prefs_protocol_mbox->hide();
	gr_prefs_protocol_imap4->hide();
	gr_prefs_protocol_maildir->hide();
	gr_prefs_protocol_mh->hide();

	switch (val)
	{
		case PROTO_SQL:
			gr_prefs_protocol_sql->show();
			gr_prefs_protocol_pop3->hide();
			gr_prefs_protocol_mbox->hide();
					gr_prefs_protocol_imap4->hide();
			if (PREFS_CURRENTBOX)
			{
				PREFS_CURRENTBOX->Set_char ("protocol","sql");
				if (!*PREFS_CURRENTBOX->Get_char ("table")) PREFS_CURRENTBOX->Set_char ("table", "po_mail");
				in_prefs_protocol_sql_host->value   (PREFS_CURRENTBOX->Get_char ("host"));
				in_prefs_protocol_sql_user->value   (PREFS_CURRENTBOX->Get_char ("user"));
				in_prefs_protocol_sql_cafile->value (PREFS_CURRENTBOX->Get_char ("cafile"));
				in_prefs_protocol_sql_db->value     (PREFS_CURRENTBOX->Get_char ("database"));
				in_prefs_protocol_sql_tab->value    (PREFS_CURRENTBOX->Get_char ("table"));
			}
			break;

		case PROTO_POP3:
			gr_prefs_protocol_sql->hide();
			gr_prefs_protocol_pop3->show();
			gr_prefs_protocol_mbox->hide();
					gr_prefs_protocol_imap4->hide();
			if (PREFS_CURRENTBOX)
			{
				PREFS_CURRENTBOX->Set_char ("protocol","pop3");
				load_server_fields (false);
			}
			server_fields_for (false);
			break;

		case PROTO_MBOX:
			gr_prefs_protocol_sql->hide();
			gr_prefs_protocol_pop3->hide();
					gr_prefs_protocol_mbox->show();
			gr_prefs_protocol_imap4->hide();
			if (PREFS_CURRENTBOX)
			{
				PREFS_CURRENTBOX->Set_char ("protocol","mbox");
				WordList *X = PREFS_CURRENTBOX->Get_wordlist ("files");
				while (br_prefs_mboxfiles->size()) br_prefs_mboxfiles->remove(1);
				for (int bla=0; bla<X->Count;++bla)
				{
					br_prefs_mboxfiles->add (X->Get(bla), &pixmap_mbox, NULL);
				}
				bt_prefs_mboxfile_erase->deactivate();
				in_prefs_mboxfile_name->value ("");
				in_prefs_mboxfile_name->deactivate();
			}
			break;
			
		case PROTO_IMAP4:
			gr_prefs_protocol_sql->hide();
			gr_prefs_protocol_pop3->hide();
			gr_prefs_protocol_mbox->hide();
		#ifdef PO_HAVE_LIBETPAN
			gr_prefs_protocol_pop3->show();
			if (PREFS_CURRENTBOX)
			{
				PREFS_CURRENTBOX->Set_char ("protocol","imap4");
				load_server_fields (true);
			}
			server_fields_for (true);
#else
			// Built without libetpan: the menu item is hidden, and an
			// existing IMAP4 mailbox shows a note and keeps its settings.
			gr_prefs_protocol_imap4->show();
#endif
			break;

		case PROTO_MAILDIR:
			gr_prefs_protocol_maildir->show();
			if (PREFS_CURRENTBOX)
			{
				PREFS_CURRENTBOX->Set_char ("protocol","maildir");
				if (!*PREFS_CURRENTBOX->Get_char ("path")) PREFS_CURRENTBOX->Set_char ("path", "~/Maildir");
				in_prefs_protocol_maildir_path->value (PREFS_CURRENTBOX->Get_char ("path"));
			}
			break;

		case PROTO_MH:
			gr_prefs_protocol_mh->show();
			if (PREFS_CURRENTBOX)
			{
				PREFS_CURRENTBOX->Set_char ("protocol","mh");
				in_prefs_protocol_mh_path->value (PREFS_CURRENTBOX->Get_char ("path"));
			}
			break;

		default:
			break;
	}
	if (PREFS_CURRENTBOX)
	{
		//int val;
		ch_prefs_collection->value (PREFS_CURRENTBOX->Get_int ("collection"));
		// Mailboxes kept in local files can have a command that fetches
		// their mail; the collection method only applies to servers.
		gr_prefs_idle_row->hide ();
		if (val == PROTO_MBOX || val == PROTO_MAILDIR || val == PROTO_MH)
		{
			in_prefs_fetchcmd->value (PREFS_CURRENTBOX->Get_char ("fetchcmd"));
			gr_prefs_collection_row->hide ();
			gr_prefs_fetchcmd->show ();
		}
#ifdef PO_HAVE_LIBETPAN
		else if (val == PROTO_IMAP4)
		{
			// IMAP: bodies always come when opened; instead, push (IDLE),
			// on unless set to "N".
			gr_prefs_fetchcmd->hide ();
			gr_prefs_collection_row->hide ();
			ck_prefs_idle->value (strcasecmp (PREFS_CURRENTBOX->Get_char ("idle"), "N") != 0);
			gr_prefs_idle_row->show ();
		}
#endif
		else
		{
			gr_prefs_fetchcmd->hide ();
			gr_prefs_collection_row->show ();
		}
		// Settings that only apply to some protocols are greyed out for
		// the others: the collection method and deleting from the server
		// after some days to POP3.
		if (val == PROTO_POP3) ch_prefs_collection->activate (); else ch_prefs_collection->deactivate ();
		if (val == PROTO_POP3)
		{
			in_prefs_expire_onserver_received->activate ();
			in_prefs_expire_onserver_notreceived->activate ();
		}
		else
		{
			in_prefs_expire_onserver_received->deactivate ();
			in_prefs_expire_onserver_notreceived->deactivate ();
		}
		in_prefs_expire_onserver_received->value (PREFS_CURRENTBOX->Get_char ("expire_onserver_received"));
		in_prefs_expire_onserver_notreceived->value (PREFS_CURRENTBOX->Get_char ("expire_onserver_notreceived"));
		in_prefs_expire_local_only->value (PREFS_CURRENTBOX->Get_char ("expire_local_only"));
		in_prefs_expire_local->value (PREFS_CURRENTBOX->Get_char ("expire_local"));
		ch_prefs_storage->value (strcasecmp (PREFS_CURRENTBOX->Get_char ("storage"), "pgsql") == 0 ? 1 : 0);
		in_prefs_storage_conninfo->value (PREFS_CURRENTBOX->Get_char ("pgsql_conninfo"));
		if (ch_prefs_storage->value() == 1) in_prefs_storage_conninfo->activate();
		else in_prefs_storage_conninfo->deactivate();
	}
}

/*
void cb_prefs_trash_server (Fl_Button *b, void *d)
{
	PREFS_CURRENTBOX->Set_int ("archtrash", 1);
	pixmap_radio_off.label (rd_prefs_trash_local);
	pixmap_radio_on.label (rd_prefs_trash_server);
	rd_prefs_trash_local->redraw();
	rd_prefs_trash_server->redraw();
}

void cb_prefs_trash_local (Fl_Button *b, void *d)
{
	PREFS_CURRENTBOX->Set_int ("archtrash", 0);
	pixmap_radio_on.label (rd_prefs_trash_local);
	pixmap_radio_off.label (rd_prefs_trash_server);
	rd_prefs_trash_local->redraw();
	rd_prefs_trash_server->redraw();
}
*/
// The most messages of a folder shown in the list (0: all). Unset: 1024, as
// in PostOffice 0.7.
static void fill_sentbox (void);

int list_limit (Var *prefs)
{
	Var *gui = prefs->Get_collection ("postoffice")->Get_collection ("gui");
	if (!gui->Exists ("list_limit")) return 1024;
	int n = gui->Get_int ("list_limit");
	return n < 0 ? 0 : n;
}

void cb_prefs_listlimit (Fl_Int_Input *i, void *)
{
	long n = atol (i->value ());
	if (n < 0) n = 0;
	if (n > 100000000) n = 100000000;
	LPREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->Set_int ("list_limit", (int) n);
}

void cb_prefs_wrap (Fl_Int_Input *i, void *)
{
	int n = atoi (i->value ());
	if (n < 60) n = 60;
	if (n > 100) n = 100;
	LPREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->Set_int ("wrap", n);
}

void cb_configure (Fl_Button */*b*/, void */*d*/)
{
	WordList		*W;
	int			  i;

	PREFS_MBOX_CHANGE = 0;
	JOURNAL.Erase();
	
	if (!(win_prefs))
	{
		make_prefs_window();
		// Grey out database features this build does not have.
		if (!podb::HavePostgreSQL()) menu_ch_prefs_protocol[PROTO_SQL].deactivate();
		if (!podb::HavePostgreSQL()) menu_ch_prefs_storage[1].deactivate();
		// IMAP4: offered, or shown greyed out with the reason.
		menu_ch_prefs_protocol[PROTO_IMAP4].show();
#ifndef PO_HAVE_LIBETPAN
		menu_ch_prefs_protocol[PROTO_IMAP4].label ("IMAP4 Account (built without libetpan)");
		menu_ch_prefs_protocol[PROTO_IMAP4].deactivate();
#endif
	}
	certs_refresh ();
	
	// Display Mailboxes
	
	while (br_prefs_mailboxes->size()) br_prefs_mailboxes->remove (1);
	while (br_prefs_mailfolders_mailboxes->size()) br_prefs_mailfolders_mailboxes->remove (1);
	//br_prefs_mailboxes->clear();
	LPREFERENCES.CopyFrom (&PREFERENCES);
	W = LPREFERENCES.Get_collection ("postoffice")->Get_wordlist ("mailboxes");
	for (i=0; i<W->Count; ++i)
	{
		if (strcasecmp (W->Get(i), "OutBox") == 0)
		{
			W->Delete (i);
			--i;
		}
		else
		{
			br_prefs_mailboxes->add (W->Get(i), &pixmap_folder, NULL);
			br_prefs_mailfolders_mailboxes->add (W->Get(i), &pixmap_folder, NULL);
		}
	}

	br_prefs_style_element->clear();
	
	WordList *XX = PREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->
							   Get_wordlist ("style");

	for (int cnt=0;cnt < XX->Count; ++cnt)
		if (strlen (XX->Get(cnt)) > 2)
			br_prefs_style_element->add (XX->Get(cnt), NULL);
									  
	// Display Identities
	
	//br_prefs_identities->clear();
	while (br_prefs_identities->size()) br_prefs_identities->remove(1);
	W = LPREFERENCES.Get_collection ("postoffice")->Get_wordlist ("identities");
	for (i=0;i<W->Count;++i)
	{
		br_prefs_identities->add (LPREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (W->Get(i))->Get_char ("shortname"), &pixmap_identity, NULL );
	}
	show_delivery ();
	{
		char n[16];
		snprintf (n, sizeof (n), "%d", list_limit (&LPREFERENCES));
		in_prefs_listlimit->value (n);
		snprintf (n, sizeof (n), "%d", flowed::WrapWidth (&LPREFERENCES));
		in_prefs_wrap->value (n);
	}
	// The settings were copied anew: nothing from before is selected.
	PREFS_CURRENTIDENT = (Var *) NULL;
	ed_prefs_edittexts->Clear();
	gr_identities_set->deactivate();
	gr_mailboxes_set->deactivate();
	gr_prefs_filters->deactivate();

	br_prefs_mailfolders_folders->deactivate();
	br_prefs_mailfolders_subfolders->deactivate();
	gr_prefs_mailfolders_folderset->deactivate();
	gr_prefs_mailfolders_subset->deactivate();
	bt_prefs_mailfolders_folderadd->deactivate();
	bt_prefs_mailfolders_newsub->deactivate();
	
	tb_prefs->value(gr_prefs_mailboxes);
	cb_prefs_tab (tb_prefs, NULL);
	win_prefs->show();
}

void cb_prefs_tab (Fl_Tabs */*t*/, void */*v*/)
{
	Fl_Group *current = (Fl_Group *) tb_prefs->value();
	gr_prefs_mailboxes->color (47);
	gr_prefs_identities->color (47);
	gr_prefs_network->color(47);
	gr_prefs_language->color(47);
	gr_prefs_mailfolders->color(47);
	gr_prefs_style->color(47);
	current->color (51);
}

void cb_prefs_mboxfiles (Fl_Iconbrowser */*b*/, void */*v*/)
{
	int val;

	if ((val = br_prefs_mboxfiles->value()))
	{
		bt_prefs_mboxfile_erase->activate();
		in_prefs_mboxfile_name->activate();
		in_prefs_mboxfile_name->value (br_prefs_mboxfiles->text (val));
	}
	else
	{
		bt_prefs_mboxfile_erase->deactivate();
		in_prefs_mboxfile_name->value ("");
		in_prefs_mboxfile_name->deactivate();
	}
}

void cb_prefs_mailboxes (Fl_Iconbrowser */*b*/, void */*v*/)
{
	int val;
	char *proto;
	
	if ((val = br_prefs_mailboxes->value()))
	{
		gr_mailboxes_set->activate();
		PREFS_CURRENTBOX = LPREFERENCES.Get_collection("postoffice")->Get_collection("mailboxes")->Get_collection ((char *) br_prefs_mailboxes->text(val));
		in_prefs_mailboxname->value (br_prefs_mailboxes->text(val));
		
		proto = PREFS_CURRENTBOX->Get_char ("protocol");
		if (strcasecmp (proto, "sql") == 0 || strcasecmp (proto, "mysql") == 0)
		{
			ch_prefs_protocol->value (PROTO_SQL);
		}
		else if (strcasecmp (proto, "pop3") == 0)
		{
			ch_prefs_protocol->value (PROTO_POP3);
		}
		else if (strcasecmp (proto, "mbox") == 0)
		{
			ch_prefs_protocol->value (PROTO_MBOX);
		}
		else if (strcasecmp (proto, "imap4") == 0)
		{
			ch_prefs_protocol->value (PROTO_IMAP4);
		}
		else if (strcasecmp (proto, "maildir") == 0)
		{
			ch_prefs_protocol->value (PROTO_MAILDIR);
		}
		else if (strcasecmp (proto, "mh") == 0)
		{
			ch_prefs_protocol->value (PROTO_MH);
		}
		else
		{
			// No or unknown protocol: POP3, as PostOffice 0.7 did.
			ch_prefs_protocol->value (PROTO_POP3);
		}
		//ch_prefs_protocol->update();
		func_view_protocol();
	}
	else gr_mailboxes_set->deactivate();
}

void cb_prefs_mailfolders_mailboxes (Fl_Iconbrowser */*b*/, void */*v*/)
{
	int imailbox;
	char *mailbox;
	int i;
	
	while (br_prefs_mailfolders_folders->size()) br_prefs_mailfolders_folders->remove(1);
	while (br_prefs_mailfolders_subfolders->size()) br_prefs_mailfolders_subfolders->remove(1);
	
	imailbox = br_prefs_mailfolders_mailboxes->value();
	if (imailbox)
	{
		mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
		PREFS_FCURRENTBOX = LPREFERENCES.Get_collection ("postoffice")->Get_collection("mailboxes")->Get_collection ((char *) mailbox);
		WordList *F = PREFS_FCURRENTBOX->Get_wordlist ("folders");
		for (i=0;i<F->Count;++i)
		{
			if (!strchr (F->Get(i), ':'))
			{
				if (strcasecmp (PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (F->Get(i))->Get_char ("type"), "sub") == 0)
				{
					br_prefs_mailfolders_folders->add (F->Get(i), &pixmap_folder, NULL);
				}
				else br_prefs_mailfolders_folders->add (F->Get(i), &pixmap_mbox, NULL);
			}
		}
		br_prefs_mailfolders_folders->redraw();
		gr_prefs_mailfolders_folderset->deactivate();
		bt_prefs_mailfolders_folderadd->activate();
		bt_prefs_mailfolders_newsub->activate();
		br_prefs_mailfolders_folders->activate();
		br_prefs_mailfolders_subfolders->deactivate();
		gr_prefs_mailfolders_subset->deactivate();
		bt_prefs_mailfolders_subadd->deactivate();
		gr_prefs_filters->deactivate();
	}
}

int filwidths[2] = {125,0};

void cb_prefs_mailfolders_folders (Fl_Iconbrowser */*b*/, void */*v*/)
{
	int ifolder;
	char *folder;
	WordList W;
	int i;
	char tmp[128];
	
	while (br_prefs_mailfolders_subfolders->size()) br_prefs_mailfolders_subfolders->remove(1);
	ifolder = br_prefs_mailfolders_folders->value();
	if (ifolder)
	{
		folder = (char *) br_prefs_mailfolders_folders->text (ifolder);
		in_prefs_mailfolders_foldername->value (folder);
		if (strcasecmp (PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (folder)->Get_char ("type"), "sub") == 0)
		{
			WordList *F = PREFS_FCURRENTBOX->Get_wordlist ("folders");
			for (i=0;i<F->Count;++i)
			{
				if (strchr (F->Get(i), ':'))
				{
					W.MakeGeneric (F->Get(i), ':');
					if (strcasecmp (W.Get(0), folder) == 0) br_prefs_mailfolders_subfolders->add (W.Get(1), &pixmap_mbox, NULL);
				}
			}
			gr_prefs_mailfolders_folderset->activate();
			br_prefs_mailfolders_subfolders->activate();
			bt_prefs_mailfolders_subadd->activate();
			gr_prefs_mailfolders_subset->deactivate();
			gr_prefs_filters->deactivate();
			br_prefs_filters->clear();
		}
		else
		{
			gr_prefs_mailfolders_folderset->activate();
			br_prefs_mailfolders_subfolders->deactivate();
			bt_prefs_mailfolders_subadd->deactivate();
			gr_prefs_mailfolders_subset->deactivate();
			gr_prefs_filters->activate();
			br_prefs_filters->clear();
			br_prefs_filters->column_widths (filwidths);
			WordList *WW = PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (folder)->Get_wordlist ("filters");
			for (i=0; i+4 < WW->Count; i+=5)
			{
				const char *xxx = WW->Get(i);
				if (atoi (xxx)==7) xxx = "Envelope Folder";
				if (atoi (xxx)==2) xxx = "Envelope From";
				snprintf (tmp, sizeof (tmp), "%s\t%s", xxx, WW->Get(i+4));
				br_prefs_filters->add (tmp, NULL);
			}
			bt_prefs_filt_delete->deactivate();
		}
	}
}

void cb_prefs_br_filters (Fl_Browser */*b*/, void */*v*/)
{
	int ifolder;
	char *folder;
	char bf[32];

	ifolder = br_prefs_mailfolders_folders->value();
	if (ifolder)
	{
		folder = (char *) br_prefs_mailfolders_folders->text (ifolder);

		WordList *F = PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (folder)->Get_wordlist ("filters");
		int pos = br_prefs_filters->value();
		if (pos)
		{
			bt_prefs_filt_delete->activate();
			pos = 5*(pos-1);
			if (F->GetInt (pos) == 2) ch_prefs_filt_match->value(0);
			else if (F->GetInt (pos) == 7) ch_prefs_filt_match->value(1);
			else if (strcasecmp (F->Get (pos), "Message-ID") == 0) ch_prefs_filt_match->value(2);
			else if (strcasecmp (F->Get (pos), "Subject") == 0) ch_prefs_filt_match->value(3);
			else if (strcasecmp (F->Get (pos), "From") == 0) ch_prefs_filt_match->value(4);
			else if (strcasecmp (F->Get (pos), "To") == 0) ch_prefs_filt_match->value(5);
			else if (strcasecmp (F->Get (pos), "Newsgroups") == 0) ch_prefs_filt_match->value(6);
			else if (strcasecmp (F->Get (pos), "Sender") == 0) ch_prefs_filt_match->value(7);
			else if (strcasecmp (F->Get (pos), "Reply-To") == 0) ch_prefs_filt_match->value(8);
			else if (strcasecmp (F->Get (pos), "Received") == 0) ch_prefs_filt_match->value(9);
			else if (strcasecmp (F->Get (pos), "Cc") == 0) ch_prefs_filt_match->value(10);

			if (strcasecmp (F->Get (pos+1), "subs") == 0) ch_prefs_filt_type->value(0);
			else if (strcasecmp (F->Get (pos+1), "case") == 0) ch_prefs_filt_type->value(1);
			else if (strcasecmp (F->Get (pos+1), "glob") == 0) ch_prefs_filt_type->value(2);

			if (strcasecmp (F->Get (pos+2), "accept") == 0) ch_prefs_filt_action->value(0);
			else if (strcasecmp (F->Get (pos+2), "deny") == 0) ch_prefs_filt_action->value(1);
			else if (strcasecmp (F->Get (pos+2), "score") == 0) ch_prefs_filt_action->value(2);

			int xscor = F->GetInt (pos+3);
			if (xscor>=0) ch_prefs_filt_scoring->value (0);
			else
			{
				ch_prefs_filt_scoring->value(1);
				xscor = -xscor;
			}

			snprintf (bf, sizeof (bf), "%i", xscor);
			in_prefs_filt_scoring->value (bf);

			in_prefs_filt_string->value (F->Get (pos+4));
		}
	}
}

void cb_prefs_delfilter (Fl_Button */*b*/, void */*v*/)
{
	int ifolder;
	char *folder;
	int i;
	char tmp[128];

	ifolder = br_prefs_mailfolders_folders->value();
	if (ifolder)
	{
		folder = (char *) br_prefs_mailfolders_folders->text (ifolder);

		WordList *F = PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (folder)->Get_wordlist ("filters");
		int pos = br_prefs_filters->value();
		if (pos)
		{
			bt_prefs_filt_delete->deactivate();
			pos = 5*(pos-1);
			for (i=0;i<5;++i) F->Delete (pos);

			br_prefs_filters->clear();

			WordList *WW = PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (folder)->Get_wordlist ("filters");
			for (i=0; i+4 < WW->Count; i+=5)
			{
				const char *xxx = WW->Get(i);
				if (atoi (xxx)==7) xxx = "Envelope Folder";
				if (atoi (xxx)==2) xxx = "Envelope From";
				snprintf (tmp, sizeof (tmp), "%s\t%s", xxx, WW->Get(i+4));
				br_prefs_filters->add (tmp, NULL);
			}
			bt_prefs_filt_delete->deactivate();
		}
		int imailbox = br_prefs_mailfolders_mailboxes->value();
		if (imailbox)
		{
			char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
			JOURNAL.Add (_FOLDER|_RECONFIG, folder, mailbox); // FIXME
			PREFS_MBOX_CHANGE=1;
		}
	}
}

void cb_prefs_addfilter (Fl_Button */*b*/, void */*v*/)
{
	int ifolder;
	char *folder;

	ifolder = (int) br_prefs_mailfolders_folders->value();
	if (ifolder)
	{
		folder = (char *) br_prefs_mailfolders_folders->text (ifolder);

		WordList *F = PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (folder)->Get_wordlist ("filters");
		char tmp[128];
		int i;

		switch (ch_prefs_filt_match->value())
		{
			case 0: F->Add ("7"); break;
			case 1: F->Add ("2"); break;
			case 2: F->Add ("Message-ID"); break;
			case 3: F->Add ("Subject"); break;
			case 4: F->Add ("From"); break;
			case 5: F->Add ("To"); break;
			case 6: F->Add ("Newsgroups"); break;
			case 7: F->Add ("Sender"); break;
			case 8: F->Add ("Reply-To"); break;
			case 9: F->Add ("Received"); break;
			case 10: F->Add ("Cc"); break;
			default: F->Add ("Subject"); break;
		}
		switch (ch_prefs_filt_type->value())
		{
			case 0: F->Add ("subs"); break;
			case 1: F->Add ("case"); break;
			case 2: F->Add ("glob"); break;
		}
		switch (ch_prefs_filt_action->value())
		{
			case 0: F->Add ("accept"); break;
			case 1: F->Add ("deny"); break;
			case 2: F->Add ("score"); break;
		}
		int xscor = atoi (in_prefs_filt_scoring->value());
		if (ch_prefs_filt_scoring->value() == 1) xscor = -xscor;
		F->AddInt (xscor);
		F->Add ((char *) in_prefs_filt_string->value());

		br_prefs_filters->clear();

		WordList *WW = PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (folder)->Get_wordlist ("filters");
		for (i=0; i+4 < WW->Count; i+=5)
		{
			const char *xxx = WW->Get(i);
			if (atoi (xxx)==7) xxx = "Envelope Folder";
			if (atoi (xxx)==2) xxx = "Envelope From";
			snprintf (tmp, sizeof (tmp), "%s\t%s", xxx, WW->Get(i+4));
			br_prefs_filters->add (tmp, NULL);
		}
		bt_prefs_filt_delete->deactivate();

		int imailbox = br_prefs_mailfolders_mailboxes->value();
		if (imailbox)
		{
			char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
			JOURNAL.Add (_FOLDER|_RECONFIG, folder, mailbox); // FIXME
			PREFS_MBOX_CHANGE=1;
		}
	}
}

void cb_prefs_mailfolders_newsub (Fl_Button */*b*/, void */*v*/)
{
	if (PREFS_FCURRENTBOX)
	{
		if (PREFS_FCURRENTBOX->Get_wordlist ("folders")->Find ("Untitled") < 0)
		{
			PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection ("Untitled")->Set_char ("type","sub");
			br_prefs_mailfolders_folders->add ("Untitled", &pixmap_folder, NULL);
			br_prefs_mailfolders_folders->value (br_prefs_mailfolders_folders->size());
			br_prefs_mailfolders_folders->redraw();
			in_prefs_mailfolders_foldername->value ("Untitled");
			gr_prefs_mailfolders_folderset->activate();
			br_prefs_mailfolders_subfolders->activate();
			bt_prefs_mailfolders_subadd->activate();
			int imailbox = br_prefs_mailfolders_mailboxes->value();
			if (imailbox)
			{
				char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
				JOURNAL.Add (_MAILBOX|_RECONFIG, mailbox); // FIXME
				PREFS_MBOX_CHANGE=1;
			}
		}
	}
}

void cb_prefs_mailfolders_sub_add (Fl_Button */*b*/, void */*v*/)
{
	char buf[256];
	int ifolder;
	char *folder;

	if (PREFS_FCURRENTBOX)
	{
		ifolder = br_prefs_mailfolders_folders->value();
		if (ifolder)
		{
			folder = (char *) br_prefs_mailfolders_folders->text (ifolder);
			snprintf (buf, sizeof (buf), "%s:Untitled", folder);
			if (PREFS_FCURRENTBOX->Get_wordlist ("folders")->Find (buf) < 0)
			{
				PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection (buf);
				
				br_prefs_mailfolders_subfolders->add ("Untitled", &pixmap_mbox, NULL);
				br_prefs_mailfolders_subfolders->value (br_prefs_mailfolders_subfolders->size());
				br_prefs_mailfolders_subfolders->redraw();
				in_prefs_mailfolders_subname->value ("Untitled");
				bt_prefs_mailfolders_subadd->activate();
				gr_prefs_mailfolders_subset->activate();
				int imailbox = br_prefs_mailfolders_mailboxes->value();
				if (imailbox)
				{
					char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
					JOURNAL.Add (_FOLDER|_ADD, buf, mailbox); // FIXME
					PREFS_MBOX_CHANGE=1;
				}
				//JOURNAL.Add (_FOLDER|_ADD, buf, in_prefs_mailboxname->value());
			}
			
		}
	}
}

void cb_prefs_mailfolders_sub_del (Fl_Button */*b*/, void */*v*/)
{
	char buf[256];
	int ifolder;
	char *folder;
	int isub;
	char *sub;
	if (PREFS_FCURRENTBOX)
	{
		ifolder = br_prefs_mailfolders_folders->value();
		if (ifolder)
		{
			folder = (char *) br_prefs_mailfolders_folders->text (ifolder);
			isub = br_prefs_mailfolders_subfolders->value();
			if (isub)
			{
				sub = (char *) br_prefs_mailfolders_subfolders->text (isub);
				br_prefs_mailfolders_subfolders->remove (isub);
				br_prefs_mailfolders_subfolders->redraw();
				snprintf (buf, sizeof (buf), "%s:%s", folder, sub);
				PREFS_FCURRENTBOX->Get_collection ("folders")->Delete (buf);
				gr_prefs_mailfolders_subset->deactivate();
				int imailbox = br_prefs_mailfolders_mailboxes->value();
				if (imailbox)
				{
					char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
					JOURNAL.Add (_MAILBOX|_DELETE, buf, mailbox);
					PREFS_MBOX_CHANGE=1;
				}
				//JOURNAL.Add (_FOLDER|_ADD, buf, in_prefs_mailboxname->value());
			}
			//gr_prefs_mailfolders_folderset->deactivate();
		}
	}
}

void cb_prefs_mailfolders_subname (Fl_Input *in, void */*v*/)
{
//	char *newval;
	int ifolder;
	char *folder;
	int isub;
	char *sub;
	char *val = (char *) in->value();
	char buf[256], buf2[256], buf3[512];
	
	if (strlen (val) > 1)
	{
		ifolder = br_prefs_mailfolders_folders->value();
		if (ifolder)
		{
			folder = (char *) br_prefs_mailfolders_folders->text (ifolder);
			isub = (int) br_prefs_mailfolders_subfolders->value();
			if (isub)
			{
				sub = (char *) br_prefs_mailfolders_subfolders->text (isub);
				snprintf (buf, sizeof (buf), "%s:%s", folder, sub);
				snprintf (buf2, sizeof (buf2), "%s:%s", folder, val);
				PREFS_FCURRENTBOX->Get_collection ("folders")->Rename (buf, buf2);
				br_prefs_mailfolders_subfolders->text (isub, val);
				snprintf (buf3, sizeof (buf3), "%s|%s", buf, buf2);
				int imailbox = br_prefs_mailfolders_mailboxes->value();
				if (imailbox)
				{
					char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
					JOURNAL.Add (_FOLDER|_RENAME, buf3, mailbox); // FIXME
					PREFS_MBOX_CHANGE=1;
				}
			}	
		}
	}
}

void cb_prefs_mailfolders_sub (Fl_Iconbrowser *b, void */*v*/)
{
	in_prefs_mailfolders_subname->value (b->text (b->value()));
			gr_prefs_mailfolders_subset->activate();
			gr_prefs_filters->activate();
}

void cb_prefs_mailfolders_foldername (Fl_Input *in, void */*v*/)
{
//	char *newval;
	int ifolder;
	char *folder;
	char *val = (char *) in->value();
	if (strlen (val) > 1)
	{
		ifolder = (int) br_prefs_mailfolders_folders->value();
		if (ifolder)
		{
			folder = (char *) br_prefs_mailfolders_folders->text (ifolder);
			WordList *F = PREFS_FCURRENTBOX->Get_wordlist ("folders");
			if (F->Find (val) < 0)
			{
				PREFS_FCURRENTBOX->Get_collection ("folders")->Rename (folder, val);
				int imailbox = br_prefs_mailfolders_mailboxes->value();
				if (imailbox)
				{
					char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
					char *buf3 = new char [strlen(folder)+strlen(val)+2];
					sprintf (buf3, "%s|%s", folder, val);
					JOURNAL.Add (_FOLDER|_RENAME, buf3, mailbox); // FIXME
					PREFS_MBOX_CHANGE=1;
					delete[] buf3;
				}
			}
			br_prefs_mailfolders_folders->text (ifolder, val);
		}
		else
		{
			gr_prefs_mailfolders_subset->deactivate();
			gr_prefs_filters->deactivate();
		}
	}
}

void cb_prefs_mailfolder_folder_add (Fl_Button */*b*/, void */*v*/)
{
	if (PREFS_FCURRENTBOX)
	{
		if (PREFS_FCURRENTBOX->Get_wordlist ("folders")->Find ("Untitled") < 0)
		{
			PREFS_FCURRENTBOX->Get_collection ("folders")->Get_collection ("Untitled");
			br_prefs_mailfolders_folders->add ("Untitled", &pixmap_mbox, NULL);
			br_prefs_mailfolders_folders->value (br_prefs_mailfolders_folders->size());
			br_prefs_mailfolders_folders->redraw();
			in_prefs_mailfolders_foldername->value ("Untitled");
			in_prefs_mailfolders_foldername->redraw();
			gr_prefs_mailfolders_folderset->activate();
			br_prefs_mailfolders_subfolders->deactivate();
			bt_prefs_mailfolders_subadd->deactivate();
			gr_prefs_mailfolders_subset->deactivate();
			gr_prefs_filters->activate();
			br_prefs_filters->clear();
			bt_prefs_filt_delete->deactivate();
			int imailbox = br_prefs_mailfolders_mailboxes->value();
			if (imailbox)
			{
				char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
				JOURNAL.Add (_FOLDER|_ADD, "Untitled", mailbox); // FIXME
				PREFS_MBOX_CHANGE=1;
			}
		}
	}
}

void cb_prefs_mailfolder_folder_del (Fl_Button */*b*/, void */*v*/)
{
//	char *newval;
	int ifolder;
	char *folder;
	ifolder = br_prefs_mailfolders_folders->value();
	if (ifolder)
	{
		folder = (char *) br_prefs_mailfolders_folders->text (ifolder);
		WordList *F = PREFS_FCURRENTBOX->Get_wordlist ("folders");
		if (F->Find (folder) >= 0)
		{
			PREFS_FCURRENTBOX->Get_collection ("folders")->Delete (folder);
		}
		br_prefs_mailfolders_folders->remove (ifolder);
		br_prefs_mailfolders_folders->redraw();
		gr_prefs_mailfolders_folderset->deactivate();
		gr_prefs_filters->deactivate();
			int imailbox = br_prefs_mailfolders_mailboxes->value();
			if (imailbox)
			{
				char *mailbox = (char *) br_prefs_mailfolders_mailboxes->text (imailbox);
				JOURNAL.Add (_FOLDER|_DELETE, folder, mailbox); // FIXME
				PREFS_MBOX_CHANGE=1;
			}
	}
}

void cb_prefs_protocol_mh (Fl_Input */*i*/, void */*v*/)
{
	if (PREFS_CURRENTBOX)
		PREFS_CURRENTBOX->Set_char ("path", (char *) in_prefs_protocol_mh_path->value());
}

void cb_prefs_protocol_maildir (Fl_Input */*i*/, void */*v*/)
{
	if (PREFS_CURRENTBOX)
		PREFS_CURRENTBOX->Set_char ("path", (char *) in_prefs_protocol_maildir_path->value());
}

void cb_prefs_protocol_sql (Fl_Input */*i*/, void */*v*/)
{
	if (PREFS_CURRENTBOX)
	{
		PREFS_MBOX_CHANGE=1;
		PREFS_CURRENTBOX->Set_char ("host", (char *) in_prefs_protocol_sql_host->value());
		PREFS_CURRENTBOX->Set_char ("user", (char *) in_prefs_protocol_sql_user->value());
		PREFS_CURRENTBOX->Set_char ("cafile", (char *) in_prefs_protocol_sql_cafile->value());
		PREFS_CURRENTBOX->Set_char ("database", (char *) in_prefs_protocol_sql_db->value());
		PREFS_CURRENTBOX->Set_char ("table", (char *) in_prefs_protocol_sql_tab->value());
		JOURNAL.Add (_MAILBOX|_RECONFIG, br_prefs_mailboxes->text (br_prefs_mailboxes->value()));
	}
}

void cb_prefs_protocol_pop3 (Fl_Input */*i*/, void */*v*/)
{
	if (PREFS_CURRENTBOX)
	{
		PREFS_MBOX_CHANGE=1;
		PREFS_CURRENTBOX->Set_char ("host", (char *) in_prefs_protocol_pop3_host->value());
		PREFS_CURRENTBOX->Set_char ("user", (char *) in_prefs_protocol_pop3_user->value());
		PREFS_CURRENTBOX->Set_char ("pass", (char *) in_prefs_protocol_pop3_pass->value());
		PREFS_CURRENTBOX->Set_char ("port", (char *) in_prefs_protocol_pop3_port->value());
		PREFS_CURRENTBOX->Set_char ("passcmd", (char *) in_prefs_protocol_pop3_passcmd->value());
		PREFS_CURRENTBOX->Set_char ("cafile", (char *) in_prefs_protocol_pop3_cafile->value());
		JOURNAL.Add (_MAILBOX|_RECONFIG, br_prefs_mailboxes->text (br_prefs_mailboxes->value()));
	}
}

void cb_prefs_protocol_pop3_security (Fl_Choice *c, void */*v*/)
{
	static const char *values[] = { "tls", "starttls" };
	if (PREFS_CURRENTBOX && c->value() >= 0 && c->value() <= 1)
	{
		PREFS_MBOX_CHANGE=1;
		PREFS_CURRENTBOX->Set_char ("security", (char *) values[c->value()]);
		JOURNAL.Add (_MAILBOX|_RECONFIG, br_prefs_mailboxes->text (br_prefs_mailboxes->value()));
	}
}

void cb_prefs_interval (Fl_Int_Input *i, void */*v*/)
{
	if (PREFS_CURRENTBOX)
	{
		PREFS_MBOX_CHANGE=1;
		PREFS_CURRENTBOX->Set_int ("interval", atoi (i->value()));
		JOURNAL.Add (_MAILBOX|_RECONFIG, br_prefs_mailboxes->text (br_prefs_mailboxes->value()));
	}
}

void cb_prefs_mailbox_delete (Fl_Button */*b*/, void */*v*/)
{
	int val;
	char *nam;
	
	if ((val= (int) br_prefs_mailboxes->value()))
	{
		nam = (char *) br_prefs_mailboxes->text(val);
		JOURNAL.Add (_MAILBOX|_DELETE, nam);
		LPREFERENCES.Get_collection("postoffice")->Get_collection("mailboxes")->Delete(nam);
		PREFS_CURRENTBOX = NULL;
		gr_mailboxes_set->deactivate();
		br_prefs_mailboxes->remove(val);
		br_prefs_mailfolders_mailboxes->remove(val);
	}
}

void cb_prefs_mboxfile_delete (Fl_Button */*b*/, void */*v*/)
{
	int val;

	if ((val = (int) br_prefs_mboxfiles->value()))
	{
		WordList *X = PREFS_CURRENTBOX->Get_wordlist ("files");
		X->Delete (val-1);
		br_prefs_mboxfiles->remove(val);
		bt_prefs_mboxfile_erase->deactivate();
		in_prefs_mboxfile_name->value ("");
		in_prefs_mboxfile_name->deactivate();
	}
}

void cb_prefs_mboxfile_add (Fl_Button */*b*/, void */*v*/)
{
	int i;
	char *newbox = (char *) single_file_request();
	if (strlen (newbox))
	{
		for (i=1;i<=br_prefs_mboxfiles->size();++i)
		{
			if (strcmp (br_prefs_mboxfiles->text(i), newbox) == 0) return;
		}
		br_prefs_mboxfiles->add (newbox, &pixmap_mbox, NULL);
		in_prefs_mboxfile_name->value (newbox);
		WordList *X = PREFS_CURRENTBOX->Get_wordlist ("files");
		X->Add (newbox);
		bt_prefs_mboxfile_erase->activate();
		br_prefs_mboxfiles->value (br_prefs_mboxfiles->size());
	}
}

void cb_prefs_mailbox_add (Fl_Button */*b*/, void */*v*/)
{
	int i;
	
	for (i=1;i<=br_prefs_mailboxes->size();++i)
	{
		if (strcasecmp (br_prefs_mailboxes->text(i), "untitled") == 0) return;
	}
	br_prefs_mailboxes->add ("Untitled", &pixmap_folder, NULL);
	br_prefs_mailfolders_mailboxes->add ("Untitled", &pixmap_folder, NULL);
	br_prefs_mailboxes->value (br_prefs_mailboxes->size());
	in_prefs_mailboxname->value ("Untitled");
	PREFS_CURRENTBOX = LPREFERENCES.Get_collection("postoffice")->Get_collection("mailboxes")->Get_collection("Untitled");
	PREFS_CURRENTBOX->Set_char ("host","host.domain.com");
	PREFS_CURRENTBOX->Set_char ("security","tls");	// for POP3 and IMAP
	// A new mailbox starts as POP3, as in PostOffice 0.7 (without this it
	// showed as the first menu entry, the experimental SQL mailbox).
	PREFS_CURRENTBOX->Set_char ("protocol","pop3");
	cb_prefs_mailboxes(br_prefs_mailboxes, (void *) NULL);
}

void cb_prefs_mboxfile_name (Fl_Input */*in*/, void */*v*/)
{
	int val, i, ln;
	char *vl;

	if ((ln = strlen (vl = (char *) in_prefs_mboxfile_name->value())) > 1)
	{
		for (i=1;i<=br_prefs_mboxfiles->size();++i)
		{
			if (strcmp (br_prefs_mboxfiles->text(i), vl) == 0) return;
		}
		if ((val = br_prefs_mboxfiles->value()))
		{
			WordList *X = PREFS_CURRENTBOX->Get_wordlist ("files");
			X->Set (val-1, (char *) in_prefs_mboxfile_name->value());
			br_prefs_mboxfiles->text (val, in_prefs_mboxfile_name->value());
		}
	}
}

void cb_prefs_mailboxname (Fl_Input */*in*/, void */*v*/)
{
	int val;
	int i;
	char *vl;
	int ln;
	
	if ((ln = strlen (vl = (char *) in_prefs_mailboxname->value())) > 1)
	{
		for (i=0;i<ln;++i) if (vl[i]==' ') vl[i] = '_'; // strip whitespaces
		for (i=1;i<=br_prefs_mailboxes->size();++i)
		{
			if (strcasecmp (br_prefs_mailboxes->text(i), vl) == 0) return;
		}
		if ((val = br_prefs_mailboxes->value()))
		{
			PREFS_MBOX_CHANGE=1;
			//PREFS_CURRENTBOX = PREFERENCES.Get_collection("postoffice")->Get_collection("mailboxes")->Get_collection(br_prefs_mailboxes->text(val));
			LPREFERENCES.Get_collection ("postoffice")->Get_collection("mailboxes")->Rename ((char *) br_prefs_mailboxes->text(val), (char *) in_prefs_mailboxname->value());
			char *buf3 = new char [strlen(br_prefs_mailboxes->text(val)) + strlen (in_prefs_mailboxname->value()) + 2];
			sprintf (buf3, "%s|%s", (char *) br_prefs_mailboxes->text(val), (char *) in_prefs_mailboxname->value());
			JOURNAL.Add (_MAILBOX|_RENAME, buf3);
			br_prefs_mailboxes->text (val, in_prefs_mailboxname->value());
			br_prefs_mailfolders_mailboxes->text (val, in_prefs_mailboxname->value());
		}
	}
}

void cb_prefs_mailbox_flags_b (Fl_Int_Input */*b*/, void */*v*/)
{
	cb_prefs_mailbox_flags (NULL, NULL);
}

void cb_prefs_mailbox_flags (Fl_Choice */*b*/, void */*v*/)
{
	if (PREFS_CURRENTBOX)
	{
		PREFS_MBOX_CHANGE=1;
		PREFS_CURRENTBOX->Set_int ("collection",(int) ch_prefs_collection->value());
		PREFS_CURRENTBOX->Set_int ("expire_onserver_received", atoi (in_prefs_expire_onserver_received->value()));
		PREFS_CURRENTBOX->Set_int ("expire_onserver_notreceived", atoi (in_prefs_expire_onserver_notreceived->value()));
		PREFS_CURRENTBOX->Set_int ("expire_local_only", atoi (in_prefs_expire_local_only->value()));
		PREFS_CURRENTBOX->Set_int ("expire_local", atoi (in_prefs_expire_local->value()));
	}
}

// Neo-PostOffice: where a mailbox stores its mail.
void cb_prefs_storage (Fl_Choice */*c*/, void */*v*/)
{
	if (!PREFS_CURRENTBOX) return;
	if (ch_prefs_storage->value() == 1 && !podb::HavePostgreSQL())
	{
		fl_alert ("This build of Neo-PostOffice has no PostgreSQL support.");
		ch_prefs_storage->value (0);
	}
	PREFS_MBOX_CHANGE=1;
	PREFS_CURRENTBOX->Set_char ("storage", ch_prefs_storage->value() == 1 ? "pgsql" : "gdbm");
	if (ch_prefs_storage->value() == 1) in_prefs_storage_conninfo->activate();
	else in_prefs_storage_conninfo->deactivate();
}

void cb_prefs_storage_conninfo (Fl_Input */*i*/, void */*v*/)
{
	if (!PREFS_CURRENTBOX) return;
	PREFS_MBOX_CHANGE=1;
	PREFS_CURRENTBOX->Set_char ("pgsql_conninfo", in_prefs_storage_conninfo->value());
}

void cb_prefs_fetchcmd (Fl_Input *i, void */*v*/)
{
	if (PREFS_CURRENTBOX) PREFS_CURRENTBOX->Set_char ("fetchcmd", (char *) i->value());
}

void cb_prefs_protocol (Fl_Choice */*c*/, void */*v*/)
{
	func_view_protocol();
}

void cb_prefs_window (Fl_Window */*w*/, void */*v*/)
{
	win_prefs->hide();
}

// The signature editor holds the selected identity's signature: stored
// when another identity is chosen and at Apply / Save (Neo-PostOffice;
// before, it was only stored when another field of the identity changed).
static void store_signature (void)
{
	if (!PREFS_CURRENTIDENT) return;
	char *decsignature = new char [ed_prefs_edittexts->SaveSize() + 1];
	ed_prefs_edittexts->SaveTo (decsignature);
	// Line breaks the editor made to fit its width (SOFT_RETURN, "\r")
	// are not part of the text: spaces again, none at the end.
	size_t n = strlen (decsignature);
	while (n && decsignature[n - 1] == '\r') decsignature[--n] = 0;
	for (char *c = decsignature; *c; ++c) if (*c == '\r') *c = ' ';
	char *signature = encode_mysql (decsignature);
	PREFS_CURRENTIDENT->Set_char ("signature", signature);
	delete[] decsignature;
	delete[] signature;
}

// No identity selected: nothing to edit.
static void no_identity (void)
{
	PREFS_CURRENTIDENT = (Var *) NULL;
	ed_prefs_edittexts->Clear();
	gr_identities_set->deactivate();
}

void cb_prefs_identities (Fl_Iconbrowser */*b*/, void */*v*/)
{
	store_signature ();
	WordList	*X;
	int		  val, i;
	char		*nam;
	
	if ((val = (int) br_prefs_identities->value()))
	{
		PREFS_CURRENTIDENT = (Var *) NULL;
		nam = (char *) br_prefs_identities->text (val);
		X = LPREFERENCES.Get_collection ("postoffice")->Get_wordlist("identities");
		for (i=0; i<X->Count;++i)
		{
			// printf ("comparing %s with %s\n", LPREFERENCES.Get_char_tree (4, "postoffice", "identities", X->Get(i), "shortname"), nam);
			if (strcasecmp (LPREFERENCES.Get_char_tree (4, "postoffice", "identities", X->Get(i), "shortname"), nam) == 0)
			{
				PREFS_CURRENTIDENT = LPREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (X->Get(i));
				i = X->Count;
			}
		}
		if (PREFS_CURRENTIDENT)
		{
			char *signature;
			char *decsignature;
			in_prefs_identityname->value (nam);
			gr_identities_set->activate();
			in_prefs_id_realname->value (PREFS_CURRENTIDENT->Get_char ("realname"));
			in_prefs_id_email->value (PREFS_CURRENTIDENT->Get_char ("mailfrom"));
			in_prefs_id_replyto->value (PREFS_CURRENTIDENT->Get_char ("replyto"));
			in_prefs_id_org->value (PREFS_CURRENTIDENT->Get_char ("organization"));
			in_prefs_id_smtphost->value (PREFS_CURRENTIDENT->Get_char ("smtp_host"));
			in_prefs_id_smtpport->value (PREFS_CURRENTIDENT->Get_char ("smtp_port"));
			in_prefs_id_smtpuser->value (PREFS_CURRENTIDENT->Get_char ("smtp_user"));
			in_prefs_id_smtppass->value (PREFS_CURRENTIDENT->Get_char ("smtp_pass"));
			in_prefs_id_smtppasscmd->value (PREFS_CURRENTIDENT->Get_char ("smtp_passcmd"));
			{
				const char *sec = PREFS_CURRENTIDENT->Get_char ("smtp_security");
				ch_prefs_id_smtpsec->value (strcmp (sec, "starttls") == 0 ? 1 : strcmp (sec, "none") == 0 ? 2 : 0);
			}
			fill_sentbox ();
			signature = PREFS_CURRENTIDENT->Get_char ("signature");
			decsignature = decode_mysql (signature);
			ed_prefs_edittexts->Clear();
			ed_prefs_edittexts->LoadFrom (decsignature);
			delete[] decsignature;
		}
		else no_identity ();
	}
	else no_identity ();
}

void cb_prefs_identity_add (Fl_Button */*b*/, void */*v*/)
{
	int i;
	char buf[24];
		
	for (i=1;i<=br_prefs_identities->size();++i)
	{
		if (strcasecmp (br_prefs_identities->text(i), "untitled") == 0) return;
	}
	
	snprintf (buf, sizeof (buf), "%ld", time (NULL));
	
	br_prefs_identities->add ("Untitled", &pixmap_identity, NULL);
	br_prefs_identities->value (br_prefs_identities->size());
	in_prefs_identityname->value ("Untitled");
	PREFS_CURRENTIDENT = LPREFERENCES.Get_collection("postoffice")->Get_collection("identities")->Get_collection(buf);
	PREFS_CURRENTIDENT->Set_char ("shortname","Untitled");
	cb_prefs_identities(br_prefs_identities, (void *) NULL);
}

void cb_prefs_identityname (Fl_Input */*in*/, void */*v*/)
{
	int val;
	int i;
	char *vl;
	char *vl2;
	
	if (strlen (vl = (char *) in_prefs_identityname->value()) > 1)
	{
		for (i=1;i<=br_prefs_identities->size();++i)
		{
			if (strcasecmp (br_prefs_identities->text(i), vl) == 0) return;
		}
		if ((val = (int) br_prefs_identities->value()))
		{
			vl2 = (char *) br_prefs_identities->text (val);
			//PREFS_CURRENTBOX = PREFERENCES.Get_collection("postoffice")->Get_collection("mailboxes")->Get_collection(br_prefs_mailboxes->text(val));
			WordList *X = LPREFERENCES.Get_collection ("postoffice")->Get_wordlist("identities");
			for (i=0;i<X->Count;++i)
			{
				if (strcmp (vl2, LPREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (X->Get(i))->Get_char ("shortname")) == 0)
				{
					LPREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (X->Get(i))->Set_char ("shortname", vl); i = X->Count;
				}
			}
			LPREFERENCES.Get_collection ("postoffice")->Get_collection("identities")->Rename ((char *) br_prefs_mailboxes->text(val), (char *) in_prefs_mailboxname->value());
			br_prefs_identities->text (val, in_prefs_identityname->value());
		}
	}
}

void cb_prefs_identity_del (Fl_Button */*b*/, void */*v*/)
{
	int vl;
	char *val;
	int i;
	
	if ((vl = br_prefs_identities->value()))
	{
		val = (char *) br_prefs_identities->text (vl);
		WordList *X = LPREFERENCES.Get_collection ("postoffice")->Get_wordlist("identities");
		for (i=0;i<X->Count;++i)
		{
			if (strcmp (val, LPREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (X->Get(i))->Get_char ("shortname")) == 0)
			{
				// Not edited any more once gone.
				if (PREFS_CURRENTIDENT == LPREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Get_collection (X->Get(i)))
					no_identity ();
				LPREFERENCES.Get_collection ("postoffice")->Get_collection ("identities")->Delete (X->Get(i)); i = X->Count;
			}
		}
		br_prefs_identities->remove (vl);
	}
}

// "Save sent mail to": nowhere, or one of the IMAP mailboxes (sent_imap).
// Labels are set with replace(), so a "/" in a name is no submenu.
static std::vector<std::string> sentbox_names;

static void fill_sentbox (void)
{
	sentbox_names.assign (1, "");
	ch_prefs_id_sentbox->clear ();
	ch_prefs_id_sentbox->add ("x");
	ch_prefs_id_sentbox->replace (0, "Nowhere (only here)");
	WordList *W = LPREFERENCES.Get_collection ("postoffice")->Get_wordlist ("mailboxes");
	for (int i = 0; W && i < W->Count; ++i)
	{
		Var *box = LPREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->Get_collection (W->Get (i));
		if (strcasecmp (box->Get_char ("protocol"), "imap4") != 0) continue;
		sentbox_names.push_back (W->Get (i));
		ch_prefs_id_sentbox->add ("x");
		ch_prefs_id_sentbox->replace ((int) sentbox_names.size () - 1, sentbox_names.back ().c_str ());
	}
	const char *cur = PREFS_CURRENTIDENT ? PREFS_CURRENTIDENT->Get_char ("sent_imap") : "";
	int v = 0;
	for (size_t i = 1; i < sentbox_names.size (); ++i) if (sentbox_names[i] == cur) v = (int) i;
	// A mailbox that is gone or no longer IMAP shows as nowhere.
	ch_prefs_id_sentbox->value (v);
}

void cb_prefs_id_sentbox (Fl_Choice *c, void *)
{
	if (!PREFS_CURRENTIDENT) return;
	int v = c->value ();
	PREFS_CURRENTIDENT->Set_char ("sent_imap", (char *) (v > 0 && v < (int) sentbox_names.size () ? sentbox_names[v].c_str () : ""));
}

// The identity's outgoing mail server (Neo-PostOffice). An empty
// encryption setting means TLS.
static void identity_smtp_changed (void)
{
	static const char *security[] = { "tls", "starttls", "none" };
	if (!PREFS_CURRENTIDENT) return;
	std::string host = in_prefs_id_smtphost->value ();
	// No spaces around a pasted name.
	while (!host.empty () && isspace ((unsigned char) host.back ())) host.pop_back ();
	while (!host.empty () && isspace ((unsigned char) host[0])) host.erase (0, 1);
	PREFS_CURRENTIDENT->Set_char ("smtp_host", (char *) host.c_str ());
	PREFS_CURRENTIDENT->Set_char ("smtp_port", (char *) in_prefs_id_smtpport->value ());
	int v = ch_prefs_id_smtpsec->value ();
	PREFS_CURRENTIDENT->Set_char ("smtp_security", (char *) security[v >= 0 && v <= 2 ? v : 0]);
	PREFS_CURRENTIDENT->Set_char ("smtp_user", (char *) in_prefs_id_smtpuser->value ());
	PREFS_CURRENTIDENT->Set_char ("smtp_pass", (char *) in_prefs_id_smtppass->value ());
	PREFS_CURRENTIDENT->Set_char ("smtp_passcmd", (char *) in_prefs_id_smtppasscmd->value ());
}

void cb_prefs_id_smtphost (Fl_Input *, void *) { identity_smtp_changed (); }
void cb_prefs_id_smtpport (Fl_Int_Input *, void *) { identity_smtp_changed (); }
void cb_prefs_id_smtpsec (Fl_Choice *, void *) { identity_smtp_changed (); }
void cb_prefs_id_smtpuser (Fl_Input *, void *) { identity_smtp_changed (); }
void cb_prefs_id_smtppass (Fl_Input *, void *) { identity_smtp_changed (); }
void cb_prefs_id_smtppasscmd (Fl_Input *, void *) { identity_smtp_changed (); }

void cb_prefs_identity_fields (Fl_Input */*b*/, void */*v*/)
{
	if (PREFS_CURRENTIDENT)
	{
		PREFS_CURRENTIDENT->Set_char ("realname", (char *) in_prefs_id_realname->value());
		PREFS_CURRENTIDENT->Set_char ("mailfrom", (char *) in_prefs_id_email->value());
		PREFS_CURRENTIDENT->Set_char ("replyto", (char *) in_prefs_id_replyto->value());
		PREFS_CURRENTIDENT->Set_char ("organization", (char *) in_prefs_id_org->value());
	}
}

// Config -> Network: how mail is sent. "delivery" is smtp (or empty: the
// built-in client), sendmail or msmtp; each program has its own path.
static const char *delivery_path_key (void)
{
	const char *method = LPREFERENCES.Get_char_tree ("postoffice/network/delivery");
	if (strcasecmp (method, "sendmail") == 0) return "sendmail_path";
	if (strcasecmp (method, "msmtp") == 0) return "msmtp_path";
	return NULL;
}

void show_delivery (void)
{
	const char *method = LPREFERENCES.Get_char_tree ("postoffice/network/delivery");
	const char *key = delivery_path_key ();
	ch_prefs_delivery->value (strcasecmp (method, "sendmail") == 0 ? 1 : strcasecmp (method, "msmtp") == 0 ? 2 : 0);
	if (key)
	{
		const char *path = LPREFERENCES.Get_collection ("postoffice")->Get_collection ("network")->Get_char (key);
		in_prefs_sendprogram->value (*path ? path : (strcmp (key, "msmtp_path") == 0 ? "msmtp" : "/usr/sbin/sendmail"));
		in_prefs_sendprogram->activate ();
	}
	else
	{
		in_prefs_sendprogram->value ("");
		in_prefs_sendprogram->deactivate ();
	}
}

void cb_prefs_delivery (Fl_Choice *c, void */*v*/)
{
	static const char *methods[] = { "smtp", "sendmail", "msmtp" };
	int v = c->value ();
	if (v < 0 || v > 2) return;
	LPREFERENCES.Get_collection ("postoffice")->Get_collection ("network")->Set_char ("delivery", (char *) methods[v]);
	show_delivery ();
}

void cb_prefs_sendprogram (Fl_Input *i, void */*v*/)
{
	if (const char *key = delivery_path_key ())
		LPREFERENCES.Get_collection ("postoffice")->Get_collection ("network")->Set_char ((char *) key, (char *) i->value ());
}

void cb_prefs_save (Fl_Button *b, void *v)
{
    cb_prefs_apply (b,v);
	win_prefs->hide();
}

void cb_prefs_cancel (Fl_Button */*b*/, void */*v*/)
{
		win_prefs->hide();
}

void cb_prefs_apply (Fl_Button */*b*/, void */*v*/)
{
	store_signature ();
	char buf[256];
	int i;

    fl_cursor (FL_CURSOR_WAIT,(Fl_Color) 0, (Fl_Color) 7);
	Fl::wait(0.1);
	
	Fl_Scroll *scr = current_boxlist;
	
	snprintf (buf, sizeof (buf), "%s/.postoffice/resource.rc.bak", getenv ("HOME"));
	PREFERENCES.Save (buf);
	snprintf (buf, sizeof (buf), "%s/.postoffice/resource.rc", getenv ("HOME"));
	LPREFERENCES.Save (buf);
	
//	br_folderindex->clear();
	br_message->clear();

	// The mailboxes are deleted and made again below: remember which one is
	// shown, to point at its new self afterwards (Neo-PostOffice).
	std::string shown_box = currentmailbox ? currentmailbox->Name : "";

	if (PREFS_MBOX_CHANGE)
	{
		i = scr->children()-1;
		while (i>=2)
		{
			scr->remove(scr->child(i-2));
			i--;
		}
		
		i=0;
		while (MailBoxList[i])
		{
			MailBoxList[i]->SaveMailBox();
			MailBoxList[i]->Quit();
			delete (MailBoxList[i]);
			++i;
		}
	}
	
	PREFERENCES.Clear();
	PREFERENCES.CopyFrom (&LPREFERENCES);

	if (PREFS_MBOX_CHANGE)
	{
		Var *VBoxes = PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes");
		WordList *MBoxes = VBoxes->GatherNames();

		int bx = 0;
		
		for (i=0;i<MBoxes->Count;++i)
		{
			if (strcasecmp (MBoxes->Get(i), "outbox"))
			{
				MailBoxList[bx++] = new MailBox (PIPE, MBoxes->Get(i));
			}
		}
		MailBoxList[bx++] = OUTBOX = new MailBox (PIPE);
		MailBoxList[bx] = (MailBox *) NULL;
		delete (MBoxes);

		currentmailbox = NULL;
		for (int k = 0; MailBoxList[k]; ++k)
			if (MailBoxList[k] != OUTBOX && shown_box == MailBoxList[k]->Name) currentmailbox = MailBoxList[k];
		if (!currentmailbox) currentfolder[0] = 0;
	}
	update_tree (current_boxlist);
    fl_cursor (FL_CURSOR_DEFAULT,(Fl_Color) 0, (Fl_Color) 7);
	_STYLES.Initialize();
}

struct prefstyle
{
	char resource[64];
	int  font;
	int  size;
	int	 color;
	bool bold;
	bool italics;
};

struct prefstyle PREFSTYLE;

Fl_Font I2F[4] = {FL_COURIER,FL_HELVETICA,FL_TIMES,FL_SCREEN};

void prefs_style_render (void);

void prefs_style_render (void)
{
	bx_prefs_style_elementname->label (PREFSTYLE.resource);

	Fl_Font _font = (Fl_Font)( I2F[PREFSTYLE.font] + (Fl_Font)(( PREFSTYLE.bold ? 1 : 0 ) + 
					((PREFSTYLE.font<3) ? ( PREFSTYLE.italics ? 2 : 0) : 0) ));
	

	bx_prefs_style_example->labelfont (_font);

	Fl_Color _color = (Fl_Color) PREFSTYLE.color;
	bx_prefs_style_example->labelcolor (_color);
	
	bx_prefs_style_curcol->color (_color);
	bx_prefs_style_curcol->redraw ();

	if (PREFSTYLE.size < 8) PREFSTYLE.size = 8;
	if (PREFSTYLE.size > 18) PREFSTYLE.size = 18;

	bx_prefs_style_example->labelsize (PREFSTYLE.size);
	bx_prefs_style_example->redraw();
	
	ch_prefs_style_typeface->value (PREFSTYLE.font);
	ch_prefs_style_typeface->redraw();
	
	int v;
	
	switch (PREFSTYLE.size)
	{
		case 10: v = 0; break;
		case 11: v = 1; break;
		case 12: v = 2; break;
		case 13: v = 3; break;
		case 14: v = 4; break;
		case 16: v = 5; break;
		case 18: v = 6; break;
		default: v = 2; break;
	}
	
	ch_prefs_style_size->value (v);
	ch_prefs_style_size->redraw();
	
	if (PREFSTYLE.bold) ck_prefs_style_bold->value(1);
	else ck_prefs_style_bold->value(0);
	ck_prefs_style_bold->redraw();
	
	if (PREFSTYLE.italics) ck_prefs_style_italic->value(1);
	else ck_prefs_style_italic->value(0);
	ck_prefs_style_italic->redraw();
	
	bx_prefs_style_curcol->color ((Fl_Color) PREFSTYLE.color);
}

void cb_prefs_style_element (Fl_Browser *b, void */*d*/)
{
	strcpy (PREFSTYLE.resource, b->text (b->value()));
	Var *v = LPREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->
						  Get_collection ("style")->Get_collection (PREFSTYLE.resource);

	int fnt = v->Get_int ("font");
	int fo = fnt - (fnt & 3);
	PREFSTYLE.bold = (fnt & 1);
	PREFSTYLE.italics = (fnt & 2);
	
	if (fo == FL_COURIER) PREFSTYLE.font = 0;
	else if (fo == FL_HELVETICA) PREFSTYLE.font = 1;
	else if (fo == FL_TIMES) PREFSTYLE.font = 2;
	else if (fo == FL_SCREEN) PREFSTYLE.font = 3;
	
	PREFSTYLE.size = v->Get_int ("size");
	PREFSTYLE.color = v->Get_int ("color");

	prefs_style_render();
}

void cb_prefs_style_typeface (Fl_Choice *c, void */*d*/)
{
	PREFSTYLE.font = c->value();
	prefs_style_render();
}

int _PSIZES[9] = {10,11,12,13,14,15,16,18,20};

void cb_prefs_style_size (Fl_Choice *c, void */*d*/)
{
	PREFSTYLE.size = _PSIZES[c->value()];
	prefs_style_render();
}

void cb_prefs_style_bold (Fl_MacCheck_Button *c, void */*d*/)
{
	PREFSTYLE.bold = c->value() ? true : false;
	prefs_style_render();
}

void cb_prefs_style_italic (Fl_MacCheck_Button *c, void */*d*/)
{
	PREFSTYLE.italics = c->value() ? true : false;
	prefs_style_render();
}

void cb_prefs_style_color (Fl_Button *b, void */*d*/)
{
	Fl_Color col = b->color();
	bx_prefs_style_curcol->color (col);
	PREFSTYLE.color = (int) col;
	prefs_style_render();
}

Fl_Font __fonts[4] = {FL_COURIER, FL_HELVETICA, FL_TIMES, FL_COURIER};

void cb_prefs_style_update (Fl_Button */*b*/, void */*d*/)
{
	if (strlen (PREFSTYLE.resource))
	{
		Var *w = LPREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->
							 Get_collection ("style")->Get_collection (PREFSTYLE.resource);
	
		w->Set_int ("color", PREFSTYLE.color);
		w->Set_int ("size", PREFSTYLE.size);
		
		int fn = PREFSTYLE.font;
		if (fn>3) fn=3;
		if (fn<0) fn=0;
		
		w->Set_int ("font", __fonts[fn] + (PREFSTYLE.bold ? 1 : 0) + (PREFSTYLE.italics ? 2 : 0));
	}
}


//-------------------------------------------------------------------------------
// Config -> Certificates (Neo-PostOffice): the CA certificate store. Changes
// take effect at once (the next connection), not with OK.
//-------------------------------------------------------------------------------

static std::vector<certstore::Info> cert_list;
std::string wrap_text (const char *text, size_t width);	// helpdesk-main.cpp

static std::string cert_kind (const certstore::Info &I)
{
	if (I.IsCA) return I.SelfSigned ? "Root CA" : "Intermediate CA";
	return "Self-signed server";
}

// Browser lines are taken as format text; "@." ends the formatting codes.
static std::string browser_text (const std::string &s)
{
	std::string out;
	for (char c : s) out += (c == '\t') ? ' ' : c;
	return out;
}

void certs_refresh (void)
{
	static const int widths[] = { 300, 140, 150, 0 };
	cert_list = certstore::List ();
	br_prefs_certs->column_widths (widths);
	br_prefs_certs->column_char ('\t');
	br_prefs_certs->clear ();
	br_prefs_certs->add ("@bName\t@bExpires\t@bType");
	for (const certstore::Info &I : cert_list)
	{
		std::string line = "@." + browser_text (I.Name) + "\t@." + I.NotAfter +
						   (I.Expired ? " (expired)" : "") + "\t@." + cert_kind (I);
		br_prefs_certs->add (line.c_str ());
	}
	bt_prefs_certremove->deactivate ();
	bx_prefs_certinfo->copy_label (cert_list.empty () ? "The store is empty: only the system's CAs are trusted."
													  : "Select a certificate to see its details.");
	br_prefs_certs->redraw ();
}

void cb_prefs_certs_select (Fl_Browser *b, void */*v*/)
{
	int i = b->value () - 2;		// line 1 is the heading
	if (i < 0 || i >= (int) cert_list.size ())
	{
		if (b->value () == 1) b->deselect ();
		bt_prefs_certremove->deactivate ();
		return;
	}
	const certstore::Info &I = cert_list[i];
	std::string valid = I.NotBefore + " to " + I.NotAfter;
	if (I.Expired) valid += "  (EXPIRED: no longer trusted)";
	else if (I.NotYetValid) valid += "  (not valid yet)";
	std::string text = "Subject:  " + I.Subject + "\nIssuer:  " + I.Issuer + "\nValid:  " + valid +
					   "\nType:  " + cert_kind (I) + "\nSHA-256:  " + I.Fingerprint;
	bx_prefs_certinfo->copy_label (text.c_str ());
	bt_prefs_certremove->activate ();
}

void cb_prefs_certadd (Fl_Button */*b*/, void */*v*/)
{
	const char *path = single_file_request ();
	if (!path || !*path) return;
	std::vector<certstore::Info> added, already, skipped;
	std::string err;
	bool ok = certstore::Import (path, added, already, skipped, err);
	certs_refresh ();

	std::string msg;
	for (const certstore::Info &I : added)
	{
		msg += "Added: " + I.Name + " (" + cert_kind (I) + ", SHA-256 " + I.Fingerprint.substr (0, 23) + "...)";
		if (I.Expired) msg += "\n    It has expired (" + I.NotAfter + "), so it does not make any server trusted.";
		msg += "\n";
	}
	for (const certstore::Info &I : already)
		msg += "Already in the store: " + I.Name + "\n";
	for (const certstore::Info &I : skipped)
		msg += "Not added: " + I.Name + " is a server certificate issued by " + I.Issuer +
			   ", not a CA. Add the CA that issued it instead.\n";
	if (!ok) msg += "Cannot add " + std::string (path) + ": " + err + "\n";
	if (!msg.empty ()) fl_message ("%s", wrap_text (msg.c_str (), 75).c_str ());
}

void cb_prefs_certremove (Fl_Button */*b*/, void */*v*/)
{
	int i = br_prefs_certs->value () - 2;
	if (i < 0 || i >= (int) cert_list.size ()) return;
	certstore::Info I = cert_list[i];
	if (fl_choice ("Remove %s from the certificate store?\nServers whose certificates were issued by it"
				   " will be refused from the next connection.", "Cancel", "Remove", NULL, I.Name.c_str ()) != 1)
		return;
	std::string err;
	if (!certstore::Remove (I.File, err)) fl_alert ("Cannot remove %s: %s", I.Name.c_str (), err.c_str ());
	certs_refresh ();
}
