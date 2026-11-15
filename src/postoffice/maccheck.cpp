#include "maccheck.h"
#include "icons/check_on.xpm"
#include "icons/check_off.xpm"

void Fl_MacCheck_Button::draw()
{
	if (value()) on_pix.draw (x(),y()+((h()-16)/2));
	else off_pix.draw (x(),y()+((h()-16)/2));
	draw_label (x()+24,y(),w()-24,h(), (Fl_Align) (FL_ALIGN_LEFT | FL_ALIGN_INSIDE));
}

int Fl_MacCheck_Button::handle(int event)
{
	if (event == FL_RELEASE) redraw();
	return Fl_Button::handle (event);
}

Fl_MacCheck_Button::Fl_MacCheck_Button (int x, int y, int w, int h, const char *l) : Fl_Button(x,y,w,h,l), on_pix (check_on_xpm), off_pix (check_off_xpm) {
	type(FL_TOGGLE_BUTTON);
	box(FL_NO_BOX);
}
