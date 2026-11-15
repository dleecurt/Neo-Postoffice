#ifndef _ICONBROWSER_H
#define _ICONBROWSER_H 1

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Pixmap.H>

struct WidgetNode
{
	WidgetNode	*Next, *Prev;
	Fl_Group		*Group;
	Fl_Button		*Icon, *Label;
	void			*Userdata;
	char			*Text;
};

class WidgetList
{
	public:
					  WidgetList (Fl_Scroll *);
					~WidgetList (void);
		void			  Add (Fl_Group *, Fl_Button *, Fl_Button *, void *, char *);
		WidgetNode	*Get (int);
		int			  Find (Fl_Button *);
		void			  Remove (int);
		int			  Size (void);
		
	private:
		Fl_Scroll		*Scroll;
		WidgetNode	*First, *Last;
		int			  Count;
};

class Fl_Iconbrowser : public Fl_Group
{
	protected:
		Fl_Scroll			  Scroll;
		WidgetList			 *Contents;
		int					  Selected;
		int					  OldSelected;
		int					  handle (int);
		
	public:
		FL_EXPORT			  Fl_Iconbrowser (int x, int y, int w, int h, const char *label = 0);
		virtual FL_EXPORT		~Fl_Iconbrowser (void);
		
		FL_EXPORT void		  draw (void);
		static FL_EXPORT void	  cb_click (Fl_Button *, void *);
		void					  add (char *, Fl_Pixmap *, void *);
		int					  size (void);
		void					  show (void);
		void					  hide (void);
		void					  remove (int);
		const char			*text (int);
		void					  text (int, const char *);
		void					  textsize (int);
		int					  value (void);
		void					  value (int);
		void					  activate (void);
		void					  deactivate (void);
};

#endif