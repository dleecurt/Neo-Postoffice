#include <FL/Fl_Box.H>
#include <FL/Fl.H>
#include <Fl/fl_draw.H>

class Fl_ProgressBox : public Fl_Box
{
protected:
	double mMin;
	double mMax;
	double mPresent;
	double mStep;
	bool mShowPct;
	Fl_Color mTextColor;
	FL_EXPORT void draw();
public:
	FL_EXPORT Fl_ProgressBox(int x, int y, int w, int h, const char *lbl);
	void range(double min, double max, double step = 1)  { mMin = min; mMax = max; mStep = step; };
	void step(double step)		{ mPresent += step; redraw(); };
	double min()	{ return mMin; }
	double max()	{ return mMax; }
	double position()	{ return mPresent; }
	double step()		{ return mStep; }
	void position(double pos) 	{ mPresent = pos; redraw(); }
	void showtext(bool st)	{ mShowPct = st; }
	bool showtext()		{ return mShowPct; }
	void textcolor(Fl_Color col)	{ mTextColor = col; }
	Fl_Color textcolor() 	{ return mTextColor; }
};


