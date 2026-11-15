#ifndef _STYLE_H
#define _STYLE_H 1

#include "btree.h"
#include "var.h"
#include <FL/Enumerations.H>
#include <FL/Fl_Widget.H>

extern Var PREFERENCES;

class Style : public BTreeNode
{
	public:
						 Style (const char *, Style *r = NULL);
						~Style (void);
		
		Fl_Font			 font;
		Fl_Color		 color;
		int				 size;
};

class Stylist
{
	public:
						 Stylist (void);
						~Stylist (void);
						
		void			 Initialize (void);
		
		Style			*Root;
		
		Fl_Font			 font (const char *);
		Fl_Color		 color (const char *);
		int				 size (const char *);
};

extern Stylist _STYLES;

void initialize_styles (void);
void create_style (const char *, Fl_Font, Fl_Color, int);
void set_style (Fl_Widget *, const char *);

#endif

