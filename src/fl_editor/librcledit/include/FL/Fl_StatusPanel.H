#include <FL/Fl_Box.H>
#include <FL/Fl.H>
#include <FL/Fl_Tile.H>
#include <FL/fl_draw.H>
#include <FL/Fl_ProgressBox.H>

// This is a status panel, mainly for the editor widget 
// Has a general status text (used by the editor to
// show row/column position), several keyboard status indicators,
// and a progress bar that can be shown or hidden as necessary.

class Fl_StatusPanel : public Fl_Group
{
protected:
	Fl_ProgressBox *PBar;
	Fl_Tile *StatusMsg;
	Fl_Tile *CAPlight;
	Fl_Tile *INSlight;
	Fl_Tile *NUMlight;  
public:	
	FL_EXPORT Fl_StatusPanel(int nx, int ny, int nw, int nh, const char *lbl="");
	FL_EXPORT ~Fl_StatusPanel();
	FL_EXPORT void updatestatus();
	FL_EXPORT int handle(int event);
	Fl_ProgressBox *progressbar()	{ return PBar; }
	void ins(bool on) { INSlight->label(on ? "INS" : ""); INSlight->redraw(); }
	void cap(bool on) { CAPlight->label(on ? "CAP" : ""); CAPlight->redraw(); }
	void num(bool on) { NUMlight->label(on ? "NUM" : ""); NUMlight->redraw(); }
	void status(const char *msg)	{ StatusMsg->label(msg); StatusMsg->redraw(); }
	void showprogress(bool on) { if(on) PBar->show(); else PBar->hide(); }
};


