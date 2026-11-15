#ifndef _FL_MACCHECK_H
#define _FL_MACCHECK_H 1

#include <FL/Fl_Button.H>
#include <FL/Fl_Pixmap.H>

class Fl_MacCheck_Button : public Fl_Button
{
	protected:
		virtual FL_EXPORT void 	 draw();

	public:
		Fl_Pixmap				 on_pix, off_pix;
		virtual FL_EXPORT int	 handle(int);
		FL_EXPORT				 Fl_MacCheck_Button (int x, int y, int w, int h, const char *l = 0);
};

#endif
