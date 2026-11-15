#include "wordlist.h"
#include "helpdesk.h"

#include <string.h>

Fl_Button 			*mdi_buttons[8];
Fl_View 			*mdi_views[8];
int					 mdi_number = 0;
int					 mdi_current = 0;

extern char			 currentfrom[8][256];
extern char			 currentsubject[8][256];
extern char			 currentreplyto[8][256];
extern char			 currentfollowup[8][256];
extern char			 currentmsgid[8][256];
extern char			 currentlabel[8][256];
extern char			 currentreferences[8][2048];
extern WordList		 currentmessage[8];
extern int			 currentbodystart[8];
extern int			 currentsigstart[8];

void mdi_init (void)
{
	for (int i=0; i<8; ++i)
	{
		mdi_views[i] = NULL;
		currentfrom[i][0] = 0;
		currentsubject[i][0] = 0;
		currentreplyto[i][0] = 0;
		currentfollowup[i][0] = 0;
		currentmsgid[i][0] = 0;
		currentreferences[i][0] = 0;
		currentbodystart[i] = 0;
		currentsigstart[i] = 0;
		sprintf (currentlabel[i], "Message %i", i+1);
	}
	
	mdi_number = 1;
	mdi_current = 0;
	mdi_buttons[0] = bt_mdi_0;
	mdi_buttons[1] = bt_mdi_1;
	mdi_buttons[2] = bt_mdi_2;
	mdi_buttons[3] = bt_mdi_3;
	mdi_buttons[4] = bt_mdi_4;
	mdi_buttons[5] = bt_mdi_5;
	mdi_buttons[6] = bt_mdi_6;
	mdi_buttons[7] = bt_mdi_7;
	mdi_views[0] = br_message;
	mdi_buttons[0]->box (FL_DOWN_BOX);
	mdi_buttons[0]->show();
	bt_mdi_close->deactivate();
	bt_mdi_0->label (currentlabel[0]);
}

void mdi_new_window (void)
{
	if (mdi_number)
	{
		if (mdi_views[mdi_current])
		{
			if (mdi_number < 8)
			{
				mdi_buttons[mdi_current]->box(FL_UP_BOX);
				mdi_buttons[mdi_current]->redraw();
				Fl::flush();
				mdi_views[mdi_current]->hide_();

				
				gr_view_wrapper->begin();
				{
					sprintf (currentlabel[mdi_number], "Message %i", mdi_number);
					mdi_views[mdi_number] = new Fl_View (gr_view_wrapper->x(), gr_view_wrapper->y(),
														 gr_view_wrapper->w(), gr_view_wrapper->h());
					mdi_buttons[mdi_number]->box(FL_DOWN_BOX);
					mdi_buttons[mdi_number]->show();
					mdi_buttons[mdi_number]->label (currentlabel[mdi_number]);
				}
				gr_view_wrapper->end();
				gr_view_wrapper->redraw();
				mdi_current = mdi_number;
				++mdi_number;
			}
			if (mdi_number > 7) bt_mdi_new->deactivate();
			if (mdi_number > 1) bt_mdi_close->activate();
		}
	}
}

void mdi_move (int oldid, int newid)
{
	char tmp[256];
	
	strcpy (currentfrom[newid],currentfrom[oldid]);
	strcpy (currentsubject[newid],currentsubject[oldid]);
	strcpy (currentreplyto[newid],currentreplyto[oldid]);
	strcpy (currentfollowup[newid],currentfollowup[oldid]);
	strcpy (currentmsgid[newid],currentmsgid[oldid]);
	strcpy (currentreferences[newid],currentreferences[oldid]);
	mdi_views[newid] = mdi_views[oldid];
	currentmessage[newid].Clear();
	
	strcpy (tmp, currentlabel[oldid]);
	strcpy (currentlabel[oldid], currentlabel[newid]);
	strcpy (currentlabel[newid], tmp);
	
	for (int i=0; i<currentmessage[oldid].Count; ++i)
	{
		currentmessage[newid].Add (currentmessage[oldid].Get(i));
	}
	currentmessage[oldid].Clear();
}

void mdi_close_window (int winid)
{
	if (winid > mdi_number) return;
	if (winid > 8) return;
	if (winid < 0) return;
	if (mdi_number < 2) return;
	
	mdi_views[winid]->hide_();
	gr_view_wrapper->remove (mdi_views[winid]);
	delete (mdi_views[winid]);
	for (int i = winid; (i+1)<mdi_number; ++i)
	{
		mdi_move (i+1, i);
	}
	mdi_buttons[mdi_number-1]->hide();
	--mdi_number;
	if (mdi_current >= mdi_number)
	{
		mdi_current = 0;
		mdi_buttons[mdi_current]->box (FL_DOWN_BOX);
		mdi_buttons[mdi_current]->redraw();
	}
	mdi_views[mdi_current]->show_();
	gr_view_wrapper->redraw();

	if (mdi_number < 2) bt_mdi_close->deactivate();
	if (mdi_number < 7) bt_mdi_new->activate();
}

void mdi_set_window (int winid)
{
	if ((winid >= mdi_number) || (winid>8) || (winid<0) || (winid == mdi_current)) return;
	mdi_buttons[mdi_current]->box (FL_UP_BOX);
	mdi_buttons[mdi_current]->value (0);
	mdi_buttons[mdi_current]->redraw();
	mdi_buttons[winid]->box (FL_DOWN_BOX);
	mdi_buttons[winid]->value(0);
	mdi_buttons[winid]->redraw();
	gr_message->redraw();
	//Fl::flush();
	mdi_views[winid]->show_();
	mdi_views[mdi_current]->hide_();
	//gr_view_wrapper->redraw();
	mdi_current = winid;
}

void cb_mdi_window (Fl_Button *b, long id)
{
	mdi_set_window (id);
}

void cb_mdi_new (Fl_Button *b, void *id)
{
	mdi_new_window();
}

void cb_mdi_close (Fl_Button *b, void *v)
{
	mdi_close_window (mdi_current);
}

void mdi_title (const char *title)
{
	if (mdi_current > mdi_number) return;
	strncpy (currentlabel[mdi_current],title,255);
	mdi_buttons[mdi_current]->label (currentlabel[mdi_current]);
	mdi_buttons[mdi_current]->redraw();
}
