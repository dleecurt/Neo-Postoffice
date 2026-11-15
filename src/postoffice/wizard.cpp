#include <time.h>
#include "semaphore.h"
#include "helpdesk.h"
#include "var.h"
#include "mailbox.h"

extern Var PREFERENCES;

int WIZARDRUNNING;
int CURRENTGROUP;

void show_field (Fl_Group *);

void start_wizard (void)
{
	Fl_Window *w = make_wizard();
	w->position ((Fl::w()-w->w())/2,(Fl::h()-w->h())/2);
	w->show();

	WIZARDRUNNING = 1;
	CURRENTGROUP = 1;
	show_field (gr_wiz01);

	while (WIZARDRUNNING) Fl::wait (1.0);
	
	if (strlen (PREFERENCES.Get_char_tree ("postoffice/mailboxes/Personal/host")))
	{
		PREFERENCES.Set_char_tree ("postoffice/mailboxes/Personal/protocol","pop3");
		PREFERENCES.Set_char_tree ("postoffice/mailboxes/Personal/folders/Inbox/filters","7,subs,accept,0,Inbox");
		if (atoi (PREFERENCES.Get_char_tree ("tmp/listfolder")))
			PREFERENCES.Set_char_tree("postoffice/mailboxes/Personal/folders/Lists/filters","7,subs,accept,0,Lists,Sender,subs,accept,0,owner-");
		if (atoi (PREFERENCES.Get_char_tree ("tmp/bouncefolder")))
			PREFERENCES.Set_char_tree("postoffice/mailboxes/Personal/folders/Bounces/filters","7,subs,accept,0,Lists,Sender,subs,accept,0,mailer-daemon");
		if (atoi (PREFERENCES.Get_char_tree ("tmp/autocheck")))
			PREFERENCES.Set_char_tree ("postoffice/mailboxes/Personal/interval","300");
	}
	else
	{
		PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->
					Delete ("Personal");
	}
	if (strlen (PREFERENCES.Get_char_tree ("postoffice/mailboxes/Usenet/host")))
	{
		PREFERENCES.Set_char_tree ("postoffice/mailboxes/Usenet/protocol","nntp");
		PREFERENCES.Set_char_tree ("postoffice/mailboxes/Usenet/folders/alt.religion.kibology/filters","7,subs,accept,0,alt.religion.kibology");
	}
	else
	{
		PREFERENCES.Get_collection ("postoffice")->Get_collection ("mailboxes")->
					Delete ("Usenet");
	}
	PREFERENCES.Set_char_tree ("postoffice/network/smtp","smtp");
	PREFERENCES.Set_char_tree ("postoffice/identities/001/shortname","Default Identity");
}

void cb_wiz_data (Fl_Input *i, void *data)
{
	char *str = (char *) data;
	PREFERENCES.Set_char_tree (str, (char *) i->value());
}

void cb_wiz_check (Fl_MacCheck_Button *b, void *data)
{
	char tmp[16];
	char *str = (char *) data;
	sprintf (tmp, "%i", b->value());
	PREFERENCES.Set_char_tree (str, tmp);
}

void show_field (Fl_Group *gr)
{
	gr_wiz01->hide();
	gr_wiz02->hide();
	gr_wiz03->hide();
	gr_wiz04->hide();
	gr_wiz05->hide();
	gr_wiz06->hide();
	gr->show();
}

void cb_wiz_next (Fl_Button *b, void *d)
{
	if (CURRENTGROUP < 6)
	{
		++CURRENTGROUP;
		switch (CURRENTGROUP)
		{
			case 1: show_field (gr_wiz01); break;
			case 2: show_field (gr_wiz02); break;
			case 3: show_field (gr_wiz03); break;
			case 4: show_field (gr_wiz04); break;
			case 5: show_field (gr_wiz05); break;
			case 6: show_field (gr_wiz06); break;
		}
		if (CURRENTGROUP==2) bt_wiz_prev->activate();
		if (CURRENTGROUP==6)
		{
			bt_wiz_next->deactivate();
			bt_wiz_finish->activate();
		}
	}
	
}

void cb_wiz_prev (Fl_Button *b, void *d)
{
	if (CURRENTGROUP > 1)
	{
		--CURRENTGROUP;
		switch (CURRENTGROUP)
		{
			case 1: show_field (gr_wiz01); break;
			case 2: show_field (gr_wiz02); break;
			case 3: show_field (gr_wiz03); break;
			case 4: show_field (gr_wiz04); break;
			case 5: show_field (gr_wiz05); break;
			case 6: show_field (gr_wiz06); break;
		}
		if (CURRENTGROUP==5)
		{
			bt_wiz_next->activate();
			bt_wiz_finish->deactivate();
		}
		if (CURRENTGROUP==1) bt_wiz_prev->deactivate();
	}
	
}

void cb_wiz_finish (Fl_Button *b, void *d)
{
	WIZARDRUNNING = 0;
	win_wizard->hide();
}
