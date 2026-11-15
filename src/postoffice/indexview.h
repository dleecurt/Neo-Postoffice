#ifndef _FL_INDEXVIEW_H
#define _FL_INDEXVIEW_H 1

#include "var.h"
#include "wordlist.h"
#include "checksum.h"

#include "FL/Fl_Widget.H"
#include "FL/Fl_Group.H"
#include "FL/Fl_Scrollbar.H"

typedef char *IndexKey;

class Fl_IndexNode : friend class IndexView
{
	public:
						 Fl_IndexNode (Fl_IndexView *, Fl_IndexNode *, const char *, WordList *contents = NULL);
						~Fl_IndexNode (void);

		const char		*Column (int);
		int				 Height (void);

		bool			 Contains (const char *);
		Fl_IndexNode	*Find (const char *);
		void			 Add (const char *, WordList *);
		void			 Add (const char *, int, char *, ...);
		void			 Remove (const char *);
		void			 RemoveR (const char *);
		void			 SortKey (int);
		void			 Draw (int, int, int, int); (x,y,w,h);

	protected:
		bool			 Selected;
		bool			 Opened;
		int				 Count;
		char			*Key;
		unsigned int	 CKey;
		int				 Sort;
		Fl_Color		 Color;
		Fl_Font			 Font;
		Fl_Pixmap		 Icon;

		void			 Add (Fl_IndexNode *);
		void			 Unlink (Fl_IndexNode *);
		
		Fl_IndexNode	*Next, *Prev;
		Fl_IndexNode	*First, *Last;
		Fl_IndexNode	*Parent;
		Fl_IndexView	*View;
		WordList		*Contents;
};

class Fl_IndexView : public Fl_Group
{
	public:
						 Fl_IndexView (int xx, int yy, int ww, int hh, const char *l = 0);
						~Fl_IndexView (void);

		void			 resize (int, int, int, int);
		void			 draw ();
		int				 handle (int);

		void			 clear();
		void			 columns (int, char *, ...);
		int				 columnwidth (int);
		const char		*column (int);
		int				 sortkey (void);
		void			 sortkey (int);
		void			 addrow (const char *, const char *, Fl_Pixmap *, Fl_Color *, int, int, char *, ...);
		void			 remove (const char *);
		void			 rremove (const char *);
		const char		*selected (void);
		int				 selectioncount (void);
		const char		*selection (int);

	protected:
		Fl_Tile			 headers_;
		Fl_Scrollbar	 scrollbar_;
};
