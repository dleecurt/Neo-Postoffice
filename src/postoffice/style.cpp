#include "style.h"
#include "debug.h"

#define FL_SCREEN_BOLD_ITALIC FL_SCREEN_BOLD
#define FL_SCREEN_ITALIC FL_SCREEN

Style::Style (const char *key, Style *r) : BTreeNode (key, r)
{
	Var *S = PREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->
						 Get_collection ("style")->Get_collection (key);
	
	font = (Fl_Font) S->Get_int ("font");
	color = (Fl_Color) S->Get_int ("color");
	size = S->Get_int ("size");
}

Style::~Style (void)
{
}

Stylist::Stylist (void)
{
	//Root = new Style ("/", NULL);
}

void Stylist::Initialize (void)
{
	if (Root) delete (Root);
	Root = new Style ("/", NULL);
	iprintf ("[style] (re)initializing Stylist\n");
	
	WordList *X = PREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->
							  Get_wordlist ("style");
	
	for (int i=0; i<X->Count;++i)
	{
		new Style (X->Get(i), Root);
	}
}

Stylist::~Stylist (void)
{
	delete (Root);
	Root = NULL;
}

Fl_Font Stylist::font (const char *key)
{
	Style *S = (Style *) Root->Find (key);
	if (S) return S->font;
	return FL_COURIER;
}

Fl_Color Stylist::color (const char *key)
{
	Style *S = (Style *) Root->Find (key);
	if (S) return S->color;
	return FL_BLACK;
}

int Stylist::size (const char *key)
{
	Style *S = (Style *) Root->Find (key);
	if (S) return S->size;
	return 12;
}

Stylist _STYLES;

void initialize_styles (void)
{
	WordList *X = PREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->
							  Get_wordlist ("style");
	
	if (!X->Count)
	{
		create_style ("messageheader:name", FL_TIMES_BOLD, FL_BLACK, 14);
		create_style ("messageheader:value", FL_TIMES, FL_BLACK, 14);
		create_style ("messagebody:text", FL_HELVETICA, FL_BLACK, 12);
		create_style ("messagebody:quoted-text", FL_HELVETICA_ITALIC, (Fl_Color) 97, 12);
		create_style ("messagebody:ascii", FL_COURIER, FL_BLACK, 12);
		create_style ("messagebody:signature", FL_COURIER, FL_GRAY, 12);
		create_style ("messagebody:attachment", FL_COURIER, FL_BLUE, 12);
		create_style ("messagebody:hyperlink", FL_COURIER_BOLD, FL_BLUE, 12);
		create_style ("index:new_message", FL_HELVETICA, (Fl_Color) 12, 12);
		create_style ("index:read_message", FL_HELVETICA, FL_GRAY, 12);
		create_style ("index:score0", FL_HELVETICA, FL_BLACK, 12);
		create_style ("index:score1", FL_HELVETICA, (Fl_Color) FL_BLACK, 12);
		create_style ("index:score2", FL_HELVETICA, (Fl_Color) 72, 12);
		create_style ("index:score3", FL_HELVETICA, (Fl_Color) 73, 12);
		create_style ("index:score4", FL_HELVETICA, (Fl_Color) 74, 12);
		create_style ("index:score5", FL_HELVETICA, (Fl_Color) 74, 12);
		create_style ("folderlist:folder", FL_HELVETICA, FL_BLACK, 10);
		create_style ("folderlist:element", FL_HELVETICA, FL_BLACK, 10);
		create_style ("editor:text", FL_COURIER, FL_BLACK, 12);
		create_style ("application:buttons", FL_HELVETICA, FL_BLACK, 12);
		create_style ("application:menubar", FL_HELVETICA, FL_BLACK, 12);
		create_style ("application:filenames", FL_HELVETICA, FL_BLACK, 12);
	}
	_STYLES.Initialize();
}

void create_style (const char *key, Fl_Font font, Fl_Color color, int size)
{
	Var *o = PREFERENCES.Get_collection ("postoffice")->Get_collection ("gui")->
						 Get_collection ("style")->Get_collection (key);
	
	o->Set_int ("font", (int) font);
	o->Set_int ("color", (int) color);
	o->Set_int ("size", (int) size);
}

void set_style (Fl_Widget *w, const char *key)
{
	Style *S = (Style *) _STYLES.Root->Find (key);
	w->labelcolor (S->color);
	w->labelfont (S->font);
	w->labelsize (S->size);
}

