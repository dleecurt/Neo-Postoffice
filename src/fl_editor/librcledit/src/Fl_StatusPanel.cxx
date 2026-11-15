#include <FL/Fl_StatusPanel.H>
#include <stdio.h>


FL_EXPORT Fl_StatusPanel::Fl_StatusPanel(int nx, int ny, int nw, int nh, const char *lbl)
	: Fl_Group(nx, ny, nw, nh, lbl)
{
	box(FL_ENGRAVED_BOX);
	
	int ttop = ny + Fl::box_dy(box());
	int theight = (nh - Fl::box_dh(box()));
	nx += Fl::box_dx(box());
	nw -= Fl::box_dx(box());

	StatusMsg = new Fl_Tile(nx, ttop, nw - (3*33), theight, "");
	StatusMsg->box(FL_DOWN_BOX);
	StatusMsg->labelsize(12);
	StatusMsg->align(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
	
	PBar = new Fl_ProgressBox(StatusMsg->x() + Fl::box_dx(StatusMsg->box()) , StatusMsg->y() + Fl::box_dy(StatusMsg->box()), StatusMsg->w() / 2, StatusMsg->h() - Fl::box_dh(StatusMsg->box()), "");
	PBar->labelsize(10);
	PBar->hide();
	
	StatusMsg->end();
	this->resizable(StatusMsg);

	INSlight = new Fl_Tile((nx+nw) - (3*33), ttop, 33, theight, "INS");
	INSlight->box(FL_DOWN_BOX);
	INSlight->labelsize(12);
	INSlight->align(FL_ALIGN_CENTER|FL_ALIGN_INSIDE);
	INSlight->end();

	CAPlight = new Fl_Tile((nx+nw) - (2*33), ttop, 33, theight, "");	// caps lock
	CAPlight->box(FL_DOWN_BOX);
	CAPlight->labelsize(12);
	CAPlight->align(FL_ALIGN_CENTER|FL_ALIGN_INSIDE);
	CAPlight->end();

	NUMlight = new Fl_Tile((nx+nw) - 33, ttop, 33, theight, "");	// num lock
	NUMlight->box(FL_DOWN_BOX);
	NUMlight->labelsize(12);
	NUMlight->align(FL_ALIGN_CENTER|FL_ALIGN_INSIDE);
	NUMlight->end();

	this->end();
}

FL_EXPORT Fl_StatusPanel::~Fl_StatusPanel()
{
	if ( NUMlight )
		delete NUMlight;
	if ( CAPlight )
		delete CAPlight;
	if ( INSlight )
		delete INSlight;
	if ( StatusMsg )
		delete StatusMsg;
	if(PBar)
		delete PBar;
}

FL_EXPORT void Fl_StatusPanel::updatestatus()
{
	// update the keyboard status lights
	int key = Fl::event_key(); 
	int state = Fl::event_state();
	switch ( key )
	{
		case FL_Caps_Lock:
			// if pressing the key, toggle the state for the check below...
			if(state & FL_CAPS_LOCK)
				state &= ~FL_CAPS_LOCK;
			else
				state |= FL_CAPS_LOCK;
			CAPlight->label(( state  & FL_CAPS_LOCK ) ? "CAP" : "");
			CAPlight->redraw();
			break;
		case FL_Num_Lock:
			if(state & FL_NUM_LOCK)
				state &= ~FL_NUM_LOCK;
			else
				state |= FL_NUM_LOCK;
			NUMlight->label(( state & FL_NUM_LOCK ) ? "NUM" : "");
			NUMlight->redraw();
			break;
		default:
			CAPlight->label(( state  & FL_CAPS_LOCK ) ? "CAP" : "");
			CAPlight->redraw();
			NUMlight->label(( state & FL_NUM_LOCK ) ? "NUM" : "");
			NUMlight->redraw();
			break;
	}
}

FL_EXPORT int Fl_StatusPanel::handle(int /*event*/)
{
	updatestatus();
	return(0);
}

