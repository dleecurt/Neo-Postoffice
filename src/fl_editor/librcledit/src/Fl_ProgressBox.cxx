#include <FL/Fl_ProgressBox.H>
#include <stdio.h>

FL_EXPORT Fl_ProgressBox::Fl_ProgressBox(int x, int y, int w, int h, const char *lbl)
	: Fl_Box(x,y,w,h,lbl)
{
	mMin = mPresent = 0; 
	mMax = 100; 
	mShowPct = true; 
	box(FL_DOWN_BOX);
	selection_color(FL_BLUE);
	color(FL_WHITE);
	textcolor(FL_RED);
}					   
					   
FL_EXPORT void Fl_ProgressBox::draw()
{
	int bdx, bdy;
	double pct;
	if(damage() & FL_DAMAGE_ALL)
		draw_box();
	bdx = Fl::box_dx(box());
	bdy = Fl::box_dy(box());
	fl_color(selection_color());
	if(mPresent > mMax)
		mPresent = mMax;
	if(mPresent < mMin)
		mPresent = mMin;
	pct = (mPresent - mMin) / mMax;
	fl_rectf(x() + bdx, y() + bdy, (int)(((double)w() - 2*bdx) * pct), h() - 2*bdy);
	if(mShowPct)
	{
		char buffer[30];
		sprintf(buffer, "%d%%", (int) (pct * 100));
		fl_color(textcolor());
		fl_font(this->labelfont(), this->labelsize());
		fl_draw(buffer, x() + (w() - fl_width(buffer))/2, y() + fl_height() + (((h() - 2*bdy) - fl_height())/2));
	}
}

