#ifndef _HT_H
#define _HT_H 1

#define FL_DAMAGE_SELECTION 0x10

#include "var.h"
#include "wordlist.h"
#include "FL/Fl_Widget.H"
#include "FL/Fl_Group.H"
#include "FL/Fl_Scrollbar.H"

class HTElement
{
	public:

						 HTElement (HTElement *p = 0);
		virtual			~HTElement (void);

		virtual void	 Layout (int);
		virtual void	 Draw (int, int, int, int, int, int, bool, bool *);
		virtual void	 Selection (WordList *, bool *);
		virtual void	 HandlePush (void);
		
		void			 SetBMark (int, int);
		void			 SetEMark (int, int);
		void			 UnBMark (void);
		void			 UnEMark (void);
		

		int				 X, Y, W, H;
		bool			 LF;

		HTElement		*Inside (int, int);
		
		int				 GetIProperty (const char *);
		const char		*GetSProperty (const char *);
		void			 SetIProperty (const char *, int);
		void			 SetSProperty (const char *, const char *);
		HTElement		*Next, *Prev;
		Var				*Properties;

//	protected:
		void			 AddElement (HTElement *);

		void			*pad, *pad2, *pad3;
		HTElement		*First, *Last;
		HTElement		*Parent;
		
		int				 BMark;
		int				 EMark;
		bool			 HaveSelection;
		bool			 CleanMeUp;
};

class HTGroup : public HTElement
{
	public:
						 HTGroup (HTElement *p = 0);
						~HTGroup (void);

		void			 Layout (int);
		void			 Draw (int, int, int, int, int, int, bool, bool *);
		void			 Selection (WordList *, bool *);
};

class HTHeader : public HTElement
{
	public:
						 HTHeader (HTElement *, char *, char *);
						~HTHeader (void);

		void			 Layout (int);
		void			 Draw (int, int, int, int, int, int, bool, bool *);
		void			 Selection (WordList *, bool *);

	private:
		HTGroup			*Value;
		HTElement		*Name;
};

class HTWord : public HTElement
{
	public:
						 HTWord (HTElement *p, char *txt);
						~HTWord (void);

		void			 Layout (int);
		void			 Draw (int, int, int, int, int, int, bool, bool *);
		void			 Selection (WordList *, bool *);
		void			 HandlePush (void);
		char			*text;

	private:
		int				 Snap (int);
		int				 StringPos (int);
};

class HTBitmap : public HTElement
{
	public:
						 HTBitmap (HTElement *p, unsigned char *, int, int);
						~HTBitmap (void);

		void			 Layout (int);
		void			 Draw (int, int, int, int, int, int, bool, bool *);
		void			 Selection (WordList *, bool *);

	private:
		unsigned char	*Bitmap;
		int				 Width, Height;
};

class Fl_View : public Fl_Group
{
	Fl_Scrollbar	 scrollbar_;
	int				 changed_;
	int				 oldval_;
	bool			 dragged_;

	public:
					 Fl_View (int xx, int yy, int ww, int hh, const char *l = 0);
					~Fl_View (void);

	void			 resize (int, int, int, int);				
	void			 draw();
	void			 draw_clip (int, int, int, int);
	void			 clear();
	void			 show_();
	void			 hide_();
	int				 handle (int);
	void			 add_textline (char *, int _color=FL_BLACK, int _font=FL_COURIER, int _size=12);
	void			 add_bitmap (unsigned char *, int, int);
	void			 add_header (const char *, const char *);
	HTGroup			*Pane;
	
	HTElement		*BSel, *ESel;
};


#endif
