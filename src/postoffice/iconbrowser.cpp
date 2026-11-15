#include "iconbrowser.h"
#include <stdio.h>
#include <string.h>

#include <FL/Fl_Window.H>

WidgetList::WidgetList (Fl_Scroll *_Scroll)
{
	Scroll = _Scroll;
	First = Last = NULL;
	Count = 0;
}

WidgetList::~WidgetList (void)
{
	WidgetNode *X, *NX;
	
	X = First;
	while (X)
	{
		NX = X->Next;
		Scroll->remove (X->Group);
		delete[] X->Text;
		delete (X->Group);
		delete (X);
		X = NX;
	}
	First = Last = NULL;
}

void WidgetList::Add (Fl_Group *Group, Fl_Button *Icon, Fl_Button *Label, void *Userdata, char *Text)
{
	WidgetNode *X = new WidgetNode;
	
	X->Group = Group;
	X->Icon = Icon;
	X->Label = Label;
	X->Userdata = Userdata;
	X->Text = Text;
	X->Next = NULL;
	if (Last)
	{
		X->Prev = Last;
		Last->Next = X;
		Last = X;
	}
	else
	{
		X->Prev = NULL;
		First = Last = X;
	}
	++Count;
}

WidgetNode *WidgetList::Get (int idx)
{
	int i = 0;
	WidgetNode *N = First;
	for (i=0; (((i+1) < idx) && (N)); ++i) N=N->Next;
	return N;
}

int WidgetList::Find (Fl_Button *b)
{
	int i = 0;
	WidgetNode *N = First;
	while (N)
	{
		++i;
		if ((N->Icon == b)||(N->Label == b)) return (i);
		N = N->Next;
	}
	return (0);
}

void WidgetList::Remove (int idx)
{
	WidgetNode *N = Get (idx);
	if (N)
	{
		Scroll->remove (N->Group);
		if (N->Prev)
		{
			if (N->Next)
			{
				N->Prev->Next = N->Next;
				N->Next->Prev = N->Prev;
			}
			else
			{
				N->Prev->Next = NULL;
				Last = N->Prev;
			}
		}
		else
		{
			if (N->Next)
			{
				N->Next->Prev = NULL;
				First = N->Next;
			}
			else
			{
				First = Last = NULL;
			}
		}
		delete (N->Group);
		delete[] N->Text;
		delete (N);
		--Count;
	}
}

int WidgetList::Size (void)
{
	return (Count);
}

Fl_Iconbrowser::Fl_Iconbrowser (int x, int y, int w, int h, const char *label) : Fl_Group (x, y, w, h, label), Scroll (x,y,w,h)
{
	Scroll.type (Fl_Scroll::VERTICAL_ALWAYS);
	Scroll.box (FL_DOWN_BOX);
	Scroll.color (FL_WHITE);
	Scroll.selection_color (147);
	Scroll.end ();
	box (FL_NO_BOX);
	Contents = new WidgetList (&Scroll);
	Fl_Group::end();
}

Fl_Iconbrowser::~Fl_Iconbrowser (void)
{
	delete (Contents);
	//delete (Scroll);
}

void Fl_Iconbrowser::draw (void)
{
	this->window()->make_current();
	Fl_Group::draw();
	//Scroll.redraw();
}

int Fl_Iconbrowser::handle (int event)
{
	return Fl_Group::handle (event);
}

void Fl_Iconbrowser::show (void)
{
	Fl_Group::show();
	//Scroll.show();
}

void Fl_Iconbrowser::hide (void)
{
	Fl_Group::hide();
	//Scroll.hide();
}

void Fl_Iconbrowser::activate (void)
{
	Fl_Group::activate();
	//Scroll.activate();
}

void Fl_Iconbrowser::deactivate (void)
{
	Fl_Group::deactivate();
	//Scroll.deactivate();
}

#define MARGIN 8

void Fl_Iconbrowser::add (const char *_label, Fl_Pixmap *_icon, void *_userdata)
{
	int ypos = y() + 2 + (18 * Contents->Size()) - Scroll.yposition();
	char *Text = new char[strlen(_label)+1];
	strcpy (Text, _label);
	Scroll.begin();
	Fl_Group *Group = new Fl_Group (x(), ypos, w() + MARGIN, 16);
	Group->box (FL_FLAT_BOX);
	Group->color (FL_WHITE);
	Group->begin();

	Fl_Button *Icon = new Fl_Button (x() + MARGIN, ypos, 16, 16);
	Icon->box (FL_NO_BOX);
	Icon->down_box (FL_NO_BOX);
	Icon->callback ((Fl_Callback *) cb_click, (void *) this);
	_icon->label (Icon);
	
	Fl_Button *Label = new Fl_Button (x()+16 + MARGIN, ypos, w()-16, 16, Text);
	Label->box (FL_NO_BOX);
	Label->callback ((Fl_Callback *) cb_click, (void *) this);
	Label->labelfont (labelfont());
	Label->labelsize (labelsize());
	Label->labelcolor (labelcolor());
	Label->align (align());
	Label->type (FL_TOGGLE_BUTTON);
	Label->when (FL_WHEN_CHANGED);
	Label->selection_color (selection_color());
	Label->down_box (FL_NO_BOX);
	
	Group->end ();
	Scroll.end();
	
	Contents->Add (Group, Icon, Label, _userdata, Text);
	Scroll.redraw();
}

void Fl_Iconbrowser::remove (int idx)
{
//	int i;
	WidgetNode *N = Contents->Get (idx+1);
	
	while (N)
	{
		N->Group->position (N->Group->x(), N->Group->y()-18);
		N = N->Next;
	}
	Contents->Remove (idx);
	if ((18 * Contents->Size()) < Scroll.yposition())
	{
		if ((18 * Contents->Size()) < h()) Scroll.position (Scroll.xposition(), 0);
		else Scroll.position (Scroll.xposition(), ((18 * Contents->Size())-h()));
	}
	Scroll.redraw();
}

void Fl_Iconbrowser::cb_click (Fl_Button *b, void *vd)
{
	Fl_Iconbrowser *self = (Fl_Iconbrowser *) vd;
	printf ("cb_click %p\n", (void *) self);
	self->Selected = self->Contents->Find (b);
	WidgetNode *N = self->Contents->Get (self->Selected);
	if (N)
	{
		if (self->OldSelected > 0)
		{
			WidgetNode *O = self->Contents->Get (self->OldSelected);
			if (O)
			{
				O->Group->color (FL_WHITE);
				O->Group->box (FL_FLAT_BOX);
				//O->Label->value (0);
				O->Label->labelcolor (self->labelcolor());
				O->Group->redraw();
			}
		}
		self->OldSelected = self->Selected;
		N->Group->color (self->selection_color());
		N->Group->box (FL_FLAT_BOX);
		N->Label->value (0);
		N->Label->labelcolor (FL_BLACK);
		N->Group->redraw();
		self->do_callback (self, N->Userdata);
	}
	else
	{
		self->Selected = 0;
		if (self->OldSelected > 0)
		{
			WidgetNode *O = self->Contents->Get (self->OldSelected);
			if (O)
			{
				O->Group->box (FL_FLAT_BOX);
				O->Group->color (FL_WHITE);
				O->Label->labelcolor (self->labelcolor());
				//O->Label->value (0);
				O->Group->redraw();
			}
		}
		self->OldSelected = 0;
	}
}

void Fl_Iconbrowser::textsize (int /*i*/)
{
	
}

const char *Fl_Iconbrowser::text (int idx)
{
	WidgetNode *N = Contents->Get (idx);
	if (N) return (N->Label->label());
	return ((const char *) "");
}

void Fl_Iconbrowser::text (int idx, const char *lbl)
{
	WidgetNode *N = Contents->Get (idx);
	if (N)
	{
		delete[] N->Text;
		N->Text = new char[strlen (lbl)+1];
		strcpy (N->Text, lbl);
		N->Label->label (N->Text);
		N->Group->redraw();
	}
}

int Fl_Iconbrowser::value (void)
{
	return (Selected);
}

void Fl_Iconbrowser::value (int val)
{
	int pos =18*val;
	WidgetNode *N;
	if (Selected > 0)
	{
		N = Contents->Get (Selected);
		if (N)
		{
			N->Group->color (FL_WHITE);
			N->Group->box (FL_FLAT_BOX);
			N->Label->value (0);
			N->Label->labelcolor (labelcolor());
			N->Group->redraw();
		}
	}
	OldSelected = 0;
	Selected = val;
	if (Selected > 0)
	{
		N = Contents->Get (Selected);
		if (N)
		{
			N->Group->color (selection_color());
			N->Group->box (FL_FLAT_BOX);
			N->Label->value (0);
			N->Label->labelcolor (FL_WHITE);
			N->Group->redraw();
			if (Scroll.yposition() > pos) Scroll.position (Scroll.xposition(), pos);
			else
			{
				if ((Scroll.yposition() + h()) < pos) Scroll.position (Scroll.xposition(), pos - h());
			}
			Scroll.redraw();
			OldSelected = val;
		}
	}
}

int Fl_Iconbrowser::size (void)
{
	return Contents->Size();
}
