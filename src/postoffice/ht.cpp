#include <initializer_list>
#include "ht.h"
#include "reg.h"
#include "strdupp.h"
#include "style.h"
#include "debug.h"
#include "substr.h"
#include <string.h>
#include <stdlib.h>
#include <spawn.h>
#include <sys/wait.h>
#include <string>

extern char **environ;

#include <FL/fl_draw.H>
#include <FL/Fl.H>

static void scrollbar_callback (Fl_Widget *, void *);
static void flview_drawclip (void *, int, int, int, int);

HTElement::HTElement (HTElement *p)
{

#ifdef ANAL_ASSERTIONS
	if (p == NULL)
	{
		dprintf ("created htelement has no parent\n");
	}
	else
	{
		HTELement *X = p;
		while (X)
		{
			X = X->Parent;
		}
	}
#endif

	// Initialization

	Properties = NULL;


	BMark = -1; // int
	EMark = -1; // int
	Parent = p; // HTElement *

	X = Y = W = H = 0;
	LF = false;
	Next = Prev = NULL;
	First = Last = NULL;
	HaveSelection = false;
	CleanMeUp = false;

	// Link node to parent
	
	if (Parent) Parent->AddElement (this);
}

HTElement::~HTElement (void)
{
	HTElement *E, *NE;

	if (Properties) delete (Properties);
	Properties = NULL;

	E = First;
	First = NULL;

	while (E)
	{
		NE = E->Next;
		delete (E);
		E = NE;
	}

	First = Last = NULL;
}

void HTElement::Layout (int /*wh*/)
{
}

void HTElement::Draw (int /*offx*/, int /*offy*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/, bool /*partial*/, bool */*selected*/)
{
}

void HTElement::Selection (WordList */*wl*/, bool */*s*/)
{
	//return 
}

void HTElement::HandlePush (void)
{
}

void HTElement::SetBMark (int x, int y)
{
	HTElement *E = First;
	
	if (E)
	{
		while (E)
		{
			if (((E->X <= x) && (((E->X+E->W) >= x)||(E->LF))) &&
			    ((E->Y <= y) && ((E->Y+E->H) >= y)))
			{
				E->SetBMark (x - E->X, y - E->Y);
				return;
			}
			E = E->Next;
		}
	}
	else
	{
		if ((x<W) && (y<H)) BMark = x;
		else if ((y<H) && (LF)) BMark = W;
	}
}

void HTElement::SetEMark (int x, int y)
{
	HTElement *E = First;
	
	if (E)
	{
		while (E)
		{
			if (((E->X <= x) && (((E->X+E->W) >= x)||(E->LF))) &&
			    ((E->Y <= y) && ((E->Y+E->H) >= y)))
			{
				E->SetEMark (x - E->X, y - E->Y);
				return;
			}
			E = E->Next;
		}
	}
	else
	{
		if ((x<W) && (y<H)) EMark = x;
		else if ((y<H)&&(LF)) EMark = W;
	}
}

void HTElement::UnBMark (void)
{
	BMark = -1;
}

void HTElement::UnEMark (void)
{
	EMark = -1;
}

HTElement *HTElement::Inside (int x, int y)
{
	HTElement *E = First;

	if (E)
	{
		while (E)
		{
			if (((E->X <= x) && (((E->X+E->W) >= x)||(E->LF))) &&
			    ((E->Y <= y) && ((E->Y+E->H) >= y)))
			{
				return E->Inside (x - E->X, y - E->Y);
			}
			E = E->Next;
		}
	}
	else
	{
		if ((((x<W))||LF) && (y<H)) return this;
	}
	return NULL;
}

int HTElement::GetIProperty (const char *nm)
{
	if (Properties)
	{
		if (Properties->Exists ((char *) nm)) return Properties->Get_int ((char *) nm);
	}
	if (Parent)
	{
		return Parent->GetIProperty (nm);
	}
	return 0;
}

const char *HTElement::GetSProperty (const char *nm)
{
	if (Properties)
	{
		if (Properties->Exists ((char *) nm)) return Properties->Get_char ((char *) nm);
	}
	if (Parent)
	{
		return Parent->GetSProperty (nm);
	}
	return (const char *) "";
}

void HTElement::SetIProperty (const char *nm, int vl)
{
	if (!Properties) Properties = new Var;
	Properties->Set_int ((char *) nm, vl);
}

void HTElement::SetSProperty (const char *nm, const char *vl)
{
	if (!Properties) Properties = new Var;
	Properties->Set_char ((char *) nm, (char *) vl);
}

void HTElement::AddElement (HTElement *E)
{
	if (First)
	{
		E->Next = NULL;
		E->Prev = Last;
		Last->Next = E;
		Last = E;
	}
	else
	{
		E->Next = E->Prev = NULL;
		First = Last = E;
	}
}

HTGroup::HTGroup (HTElement *p) : HTElement (p)
{
}

HTGroup::~HTGroup (void)
{
/*	HTElement *E, *NE;
	if (Properties) delete (Properties);
	Properties = NULL;
	E = First;
	First = NULL;
	while (E)
	{
		NE = E->Next;
		delete (E);
		E = NE;
	}
	First = Last = NULL; */
}

void HTGroup::Selection (WordList *wl, bool *sel)
{
	HTElement *E = First;
	
	while (E)
	{
		E->Selection (wl, sel);
		E = E->Next;
	}
	//if ((*sel) && (LF)) wl->Add ("\n");
}

void HTGroup::Layout (int HintWidth)
{
	int			 x,y,lm,tm;
	int			 h;
	int			 inrow;
	HTElement	*C;

	lm = tm = 0;
	x = y = h = 0;
	W = H = 0;
	inrow = 0;

	if (Properties)
	{
		if (Properties->Exists ("leftmargin")) x = lm = GetIProperty ("leftmargin");
		if (Properties->Exists ("topmargin")) y = tm = GetIProperty ("topmargin");
	}

	C = First;

	while (C)
	{
		C->Layout (HintWidth);

		// element exceeds margin or contains newline? -> start a new line
		
		if ( ((inrow) && ( (x + C->W) > HintWidth)) || ((C->Prev) && (C->Prev->LF)) )
		{
			const char *align = GetSProperty ("align"); // align the previous row
			if (strcasecmp (align, "centre") == 0) // centre
			{
				int c = (x + HintWidth) / 2;
				HTElement *CC = C;
				for (int i=0;i<inrow;++i)
				{
					if (CC)
					{
						CC = CC->Prev;
						if (CC)
						{
							CC->X = c - CC->W;
							c -= CC->W;
						}
					}
				}
			}
			else if (strcasecmp (align, "right") == 0) // right
			{
				int c = HintWidth;
				HTElement *CC = C;
				for (int i=0;i<inrow;++i)
				{
					if (CC)
					{
						CC = CC->Prev;
						if (CC)
						{
							CC->X = c - CC->W;
							c -= CC->W;
						}
					}
				}
			}
			const char *valign = GetSProperty ("valign");
			if (strcasecmp (valign, "top") == 0)
			{
			}
			else if (strcasecmp (valign, "centre") == 0)
			{
				//int c = h;
				HTElement *CC = C;
				for (int i=0;i<inrow;++i)
				{
					if (CC) CC = CC->Prev;
					if (CC) CC->Y = y + ((h - CC->H) / 2);
				}
			}
			else // bottom
			{
				//int c = h;
				HTElement *CC = C;
				for (int i=0;i<inrow;++i)
				{
					if (CC)
					{
						CC = CC->Prev;
						if (CC) CC->Y = y + (h - CC->H);
					}
				}
			}
			
			x = lm;
			y += h;
			inrow = 0;
			h = 0;
		}
		if (C->H > h) h = C->H;
		C->X = x;
		C->Y = y;
		x += C->W;
		if (x > W) W = x;
		++inrow;

		C = C->Next;
	}
	y += h;
	H = y;
}

void HTGroup::Draw (int off_x, int off_y, int x, int y, int w, int h, bool partial, bool *selected)
{
	int inside_x, inside_y;
	if ( (!Parent) || (Properties && ((Properties->Exists ("bgcolor")))) )
	{
		int col;
		if (Properties && (Properties->Exists ("bgcolor"))) col = GetIProperty ("bgcolor");
		else col = FL_WHITE;
		if (!partial)
		{
			fl_color (col);
			fl_rectf (off_x+x, off_y+y, w, h);
		}
	}
	//printf ("htgroup::draw ((%i,%i),(%i,%i),(%i,%i))\n", off_x, off_y, x, y, w, h);
	HTElement *C = First;
	while (C)
	{
		//printf ("  element (%i,%i),(%i,%i)\n", C->X, C->Y, C->W, C->H);
		inside_x = inside_y = 0;
		if ( (C->X >=x) && (C->X <= (x+w)) ) inside_x = 1; // left side is inside
		if ( (C->Y >=y) && (C->Y <= (y+h)) ) inside_y = 1; // top is inside
		if ( ((C->X+C->W) >= x) && ((C->X+C->W) <= (x+w)) ) inside_x = 1; // right side is inside
		if ( ((C->Y+C->H) >= y) && ((C->Y+C->H) <= (y+h)) ) inside_y = 1; // bottom is inside
		if ( (!inside_x) && (C->X <= x) && ((C->X+W) >= (x+w)) ) inside_x = 1; // element envelopes view horizontally
		if ( (!inside_y) && (C->Y <= y) && ((C->Y+H) >= (y+h)) ) inside_y = 1; // element envelopes view vertically

		//printf ("  inside_x=%i inside_y=%i\n", inside_x, inside_y);

		if ((inside_x) && (inside_y))
		{
			int deltax = C->X - x;
			int deltay = C->Y - y;
			C->Draw(off_x + C->X, off_y + C->Y, 0, 0, w - deltax, h - deltay, partial, selected);
		}

		C = C->Next;
	}	
}

HTHeader::HTHeader (HTElement *p, char *nam, char *val) : HTElement (p)
{
	First = Last = NULL;
	Value = new HTGroup (this);
	Name = new HTWord (this, nam);
	Name->SetIProperty ("font", _STYLES.font ("messageheader:name"));
	Name->SetIProperty ("fontsize", _STYLES.size ("messageheader:name"));
	Name->SetSProperty ("valign","top");
	Value->SetIProperty ("font", _STYLES.font ("messageheader:value"));
	Value->SetIProperty ("fontsize", _STYLES.size ("messageheader:value"));
	Value->SetSProperty ("valign","top");
	
	Name->X = 0;
	Name->Y = 0;
	Name->W = 128;
	Value->X = 128;
	Value->Y = 0;		// was 128: clicks on the value never found it
	
	WordList W;
	W.MakeGeneric (val, ' ');
	for (int i=0;i < W.Count; ++i)
	{
		char *tmp = new char[strlen(W.Get(i))+2];
		strcpy (tmp, W.Get(i));
		strcat (tmp, " ");
		HTWord *WW = new HTWord (Value, tmp);
		delete[] tmp;
		if (i==W.Count) WW->LF = true;
	}
	Properties = NULL;
	First = Name;
	Last = Value;
	Name->Prev = NULL;
	Name->Next = Value;
	Value->Prev = Name;
	Value->Next = NULL;
}

HTHeader::~HTHeader (void)
{
	//delete Value;
	//delete Name;
}

void HTHeader::Selection (WordList *wl, bool *sel)
{
	Name->Selection (wl, sel);
	if (*sel) wl->Add (": ");
	Value->Selection (wl, sel);
	if (*sel) wl->Add ("\n");
}

void HTHeader::Layout (int WidthHint)
{
	Name->Layout (128);
	Value->Layout (WidthHint - 128);
	W = WidthHint;
	H = Name->H;
	if (Value->H > H) H = Value->H;
}

void HTHeader::Draw (int off_x, int off_y, int /*x*/, int /*y*/, int w, int h, bool partial, bool *selected)
{
	Name->Draw (off_x, off_y, 0, 0, 128, h, partial, selected);
	Value->Draw (off_x + 128, off_y, 0, 0, w - 128, h, partial, selected);
}

HTWord::HTWord (HTElement *p, const char *txt) : HTElement (p)
{
	First = Last = NULL;
	Prev = Next = NULL;
	Properties = NULL;
	if (txt)
	{
		text = strdupp (txt);
	}
	else text = strdupp ((char *) "");
}

HTWord::~HTWord (void)
{
	delete[] text;
}

void HTWord::Layout (int /*WidthHint*/)
{
	int sz = GetIProperty ("fontsize");	
	int fn = GetIProperty ("font");
	fl_font (fn, sz ? sz : 12);
	W = 0;
	fl_measure (text, W, H, 0); // 0: no @symbols in message text
	H += fl_descent();
}

void HTWord::Selection (WordList *wl, bool *selected)
{
	if ((*selected) && (BMark<0) && (EMark<0))
	{
		wl->Add (text);
		if (LF) wl->Add ("\n");
		return;
	}
	if ((!(*selected)) && (BMark<0) && (EMark<0)) return;

	int sz = GetIProperty ("fontsize");
	int fn = GetIProperty ("font");
	fl_font (fn, sz ? sz : 12);

	int XMark = (BMark<0) ? EMark : BMark;

	int left,right;

	if (*selected)
	{
		if (XMark >= 0)
		{
			right = StringPos (XMark);
			left = 0;
			wl->Add_ (substr (text, left, right-left));
		}
	}
	else
	{
		if ((BMark>=0) && (EMark>=0))
		{
			left = (BMark < EMark) ? BMark : EMark;
			right = (BMark > EMark) ? BMark : EMark;
			
			left = StringPos (left);
			right = StringPos (right);
			
			wl->Add_ (substr (text, left, right-left));
			*selected = false;
			return;
		}
		else
		{
			if (XMark >= 0)
			{
				right = strlen (text);
				left = StringPos (XMark);
				wl->Add_ (substr (text, left, right-left));
			}
		}
	}
	
	if (BMark >= 0) *selected = !(*selected);
	if (EMark >= 0) *selected = !(*selected);
   
}

int HTWord::StringPos (int val)
{
	int crsr = 0;
	int w;
	int h=0;
	int curval = 0;
	int oldval = 0;
	char str[2];
	str[1] = 0;
	
	while ((text[crsr]) && (curval < val))
	{
		oldval = curval;
		str[0] = text[crsr];
		w = 0;
		fl_measure (str, w, h, 0);
		curval = oldval + w;
		++crsr;
	}
	return crsr;
}

int HTWord::Snap (int val)
{
	int crsr = 0;
	int w;
	int h=0;
	int curval = 0;
	int oldval = 0;
	char str[2];
	str[1] = 0;

#ifdef ANAL_ASSERTS
	if (text == NULL)
	{
		eprintf ("assert failed: ht.cpp/htword::snap/01");
		return 0;
	}
#endif

	while ((text[crsr]) && (curval < val))
	{
		oldval = curval;
		str[0] = text[crsr];
		w = 0;
		fl_measure (str, w, h, 0);
		curval = oldval + w;
		++crsr;
	}
	return curval;
}

void HTWord::Draw (int off_x, int off_y, int x, int y, [[maybe_unused]] int w, [[maybe_unused]] int h, bool partial, bool *selected)
{
	int sz = GetIProperty ("fontsize");
	int fn = GetIProperty ("font");
	fl_font (fn, sz ? sz : 12);
 
	if (BMark >= 0) BMark = Snap (BMark);
	if (EMark >= 0) EMark = Snap (EMark);

	int XMark = (BMark>=0) ? BMark : EMark;

#ifdef ANAL_ASSERTS
	if (selected == NULL)
	{
		eprintf ("assert failed: ht.cpp/htword::draw/01");
		return 0;
	}
#endif
   
	if (*selected)
	{
		fl_color ((Fl_Color) 246);
		if (XMark>=0)
		{
			fl_color ((Fl_Color) 246);
			fl_rectf (off_x+x, off_y+y, XMark, H);
			fl_color (FL_WHITE);
			fl_rectf (off_x+x+XMark, off_y+y, W-XMark, H);
			CleanMeUp = true;
		}
		else if (!(partial && HaveSelection))
		{
			fl_color ((Fl_Color) 246);
			fl_rectf (off_x+x, off_y+y, W, H);
			CleanMeUp = false;
		}
		if (XMark < 0) HaveSelection = true;
		
	}
	else
	{
		if ((BMark>=0) && (EMark>=0))
		{
			int left = (BMark < EMark) ? BMark : EMark;
			int right = (BMark > EMark) ? BMark : EMark;
			
			fl_color (FL_WHITE);
			fl_rectf (off_x+x, off_y+y, left, H);
			fl_rectf (off_x+x+right, off_y+y, W-right, H);
			fl_color ((Fl_Color) 246);
			fl_rectf (off_x + x + left, off_y+y, (right-left), H);
			CleanMeUp = true;
		}
		else
		{
			if (XMark>=0)
			{
				fl_color (FL_WHITE);
				fl_rectf (off_x + x, off_y + y, XMark, H);
				fl_color ((Fl_Color) 246);
				fl_rectf (off_x + x + XMark, off_y+y, W-XMark, H);
				CleanMeUp = true;
			}
			else // no marks
			{
				if (HaveSelection || CleanMeUp)
				{
					fl_color (FL_WHITE);
					fl_rectf (off_x+x, off_y+y, W, H);
					CleanMeUp = false;
				}
				else
				{
					if (partial && !selected) return;
				}
			}
		}
		HaveSelection = false;
	}

	if (BMark >= 0) *selected = !(*selected);
	if (EMark >= 0) *selected = !(*selected);
	
	fl_color (GetIProperty ("fontcolor"));
	fl_draw (text, off_x+x, off_y+y, W, H, FL_ALIGN_LEFT, NULL, 0); // 0: no @symbols
}

// Runs the user's URL action (a shell command template in which '$' stands
// for the URL) without letting the URL be interpreted by the shell.
//
// The URL comes from message text, so it is never pasted into the command.
// Each '$' is replaced by a reference to the shell's positional parameter
// $1, quoted to match the quoting at that point in the template (so old
// templates such as  netscape -remote 'openURL($)'  keep working), and the
// URL itself is passed to /bin/sh as a separate argument.
static void run_url_action (const char *tmpl, const char *url)
{
	std::string cmd;
	bool in_single = false, in_double = false;

	for (const char *p = tmpl; *p; ++p)
	{
		if (*p == '$')
		{
			if (in_single) cmd += "'\"$1\"'";
			else if (in_double) cmd += "$1";
			else cmd += "\"$1\"";
			continue;
		}
		if (*p == '\\' && !in_single && p[1])
		{
			cmd += *p++;
			cmd += *p;
			continue;
		}
		if (*p == '\'' && !in_double) in_single = !in_single;
		else if (*p == '"' && !in_single) in_double = !in_double;
		cmd += *p;
	}
	cmd += " &";

	dprintf ("[ht] url action: %s (url %s)\n", cmd.c_str(), url);

	char *const argv[] = {
		(char *) "sh", (char *) "-c", (char *) cmd.c_str(),
		(char *) "sh", (char *) url, NULL
	};
	pid_t pid;
	if (posix_spawn (&pid, "/bin/sh", NULL, NULL, argv, environ) == 0)
		waitpid (pid, NULL, 0); // returns quickly: the command runs in the background
}

// Runs a command template with '$' standing for one argument, safely as
// above (used for opening attachments).
void run_with_argument (const char *tmpl, const char *arg)
{
	run_url_action (tmpl, arg);
}

// Opens a web address with the command set for links (Config; default
// xdg-open), in the background.
void open_url (const char *url)
{
	const char *cmd = PREFERENCES.Get_char_tree ("postoffice/gui/action/http");
	if (!cmd || strlen (cmd) < 1) cmd = "xdg-open $";
	run_url_action (cmd, url);
}

void HTWord::HandlePush (void)
{
	const char 	*action_config;
	const char 	*action = GetSProperty ("action");
	char 		*subtext;
	char		*t;
	int			 ln;

	// An attachment line: the main window offers Save / Open.
	int part = GetIProperty ("attachment");
	if (part > 0)
	{
		if (ht_attachment_hook) ht_attachment_hook (part - 1);
		return;
	}

	if (action)
	{
		if (strlen (action))
		{
			// The link starts at its scheme (https://, http://, ftp://).
			const char *start = NULL;
			for (const char *scheme : { "https://", "http://", "ftp://" })
			{
				const char *f = strstr (text, scheme);
				if (f && (!start || f < start)) start = f;
			}
			if (start)
			{
				action_config = PREFERENCES.Get_char_tree ("postoffice/gui/action/http");
				if (strlen (action_config) < 1) action_config = "xdg-open $";

				subtext = strdupp (start);
				t = subtext;

				// Punctuation after a link is not part of it.
				ln = strlen (t);
				while ( (ln>1) && strchr (". ,;:!?)>\"'", t[ln-1]) ) t[--ln] = 0;

				run_url_action (action_config, t);

				delete[] subtext;
			}
		}
	}
}

HTBitmap::HTBitmap (HTElement *p, unsigned char *bmp, int _w, int _h) : HTElement (p)
{
	First = Last = NULL;
	Properties = NULL;
	Bitmap = bmp;
	Width = _w;
	Height = _h;
}

HTBitmap::~HTBitmap (void)
{
	delete[] Bitmap;
}

void HTBitmap::Selection (WordList *wl, bool *selected)
{
	if (*selected) wl->Add ("\n[Image]\n");
}

void HTBitmap::Layout (int /*WidthHint*/)
{
	W = Width;
	H = Height;
}

void HTBitmap::Draw (int off_x, int off_y, int x, int y, int /*w*/, int /*h*/, bool partial, bool */*selected*/)
{
	if (partial) return;
	fl_draw_image (Bitmap, off_x+x, off_y+y, Width, Height);
}

Fl_View::Fl_View (int xx, int yy, int ww, int hh, const char *l) : Fl_Group (xx,yy,ww,hh,l), scrollbar_(xx + ww-18, yy+2, 16, hh-4)
{
	Pane = new HTGroup;
	Pane->LF = true;
	Pane->SetIProperty ("topmargin", 16);
	Pane->SetIProperty ("leftmargin", 16);
	scrollbar_.value(0,hh,0,1);
	scrollbar_.step(8.0);
	scrollbar_.show();
	scrollbar_.callback(scrollbar_callback);
	changed_ =1;
	oldval_ = 0;
	dragged_ = false;
	BSel = ESel = NULL;
	color (FL_WHITE);
}

Fl_View::~Fl_View (void)
{
#ifdef ANAL_ASSERTS
	if (Pane == NULL)
	{
		eprintf ("assert failed: ht.cpp/fl_view::~fl_view/01\n");
		return;
	}
#endif
	delete (Pane);
}

void Fl_View::draw (void)
{
	bool selected = false;
	unsigned char dmg = damage();
	
	if (dmg & FL_DAMAGE_SELECTION)
	{
		Pane->Draw (x()+18,y()-scrollbar_.value()+2,0,scrollbar_.value(),w()-36,h()-4, true, &selected);
		return;
	}

	Fl_Boxtype b = box() ? box() : FL_DOWN_BOX;
	
	int newval = scrollbar_.value();
	//newval -= (newval % 12);
	//printf (" draw called\n");

	if (changed_)
	{
		Pane->Layout (w()-32);
		changed_ = 0;
	}
	else
	{
		if (newval != oldval_)
		{
			if (scrollbar_.visible())
			{
				draw_child (scrollbar_);
			}
			fl_scroll (x()+18,y()+2,w()-36,h()-4, 0, oldval_ - newval, flview_drawclip, this);
			oldval_ = newval;
			return ;
		}
	}
	if (Pane->H < h()) scrollbar_.hide();
	else scrollbar_.show();
	scrollbar_.value (newval,h(),0,Pane->H);
	if (scrollbar_.visible())
	{
		draw_box(b, x(),y(),w(),h(),color());
		draw_child (scrollbar_);
	}
	else
	{
		draw_box(b, x(),y(),w(),h(),color());
	}
	fl_push_clip (x()+18,y()+2,w()-36,h()-4);
	Pane->Draw (x()+18,y()-scrollbar_.value()+2,0,scrollbar_.value(),w()-36,h()-4, false, &selected);
	fl_pop_clip();
}

void Fl_View::clear (void)
{
	delete Pane;
	Pane = new HTGroup;
	BSel = ESel = NULL;		// they pointed into the old pane
	SelText.clear ();
	scrollbar_.value(0,h(),0,1);
	oldval_ = 0;
}

void Fl_View::draw_clip (int xx, int yy, int ww, int hh)
{
	bool selected = false;
	fl_push_clip (xx,yy,ww,hh);
	fl_color (FL_WHITE);
	fl_rectf (xx,yy,ww,hh);
	//printf ("y()=%i, drawing offset: %i at %i\n", y(), yy-(y()+2), yy-scrollbar_.value());
	Pane->Draw (x()+18,y()-scrollbar_.value()+2,0,scrollbar_.value(),w()-36,h()-4,false,&selected);
	fl_pop_clip();
}

void Fl_View::copy_selection () const
{
	if (!SelText.empty ())
		Fl::copy (SelText.data (), (int) SelText.size (), 1);
}

int Fl_View::handle (int event)
{
	int ox, oy;
	WordList *W;
	int cnt;
	
	switch (event)
	{
		case FL_PUSH:
			if (Fl::event_button()>1) return 0;
			SelText.clear ();
			if (BSel) BSel->UnBMark();
			BSel = NULL;
			if (ESel) ESel->UnEMark();
			ESel = NULL;
		
			if (w()-(Fl::event_x()-x()) < 18) break;
		
			ox = Fl::event_x()-x()-18;
			oy = Fl::event_y()-y()+scrollbar_.value();
		
			if (ox<0) ox = 0;
		
			BSel = Pane->Inside (ox, oy);
			if (BSel)
			{
				ESel = BSel;
				Pane->SetBMark (ox, oy);
				Pane->SetEMark (ox, oy);
				redraw();
				return 1;
			}
			redraw();
			break;
		
		case FL_DRAG:
		
			if (w()-(Fl::event_x()-x()) < 18) break;
		
			dragged_ = true;
		
			if (BSel)
			{
				if (ESel) ESel->UnEMark();
				
				ox = Fl::event_x()-x()-18;
				oy = Fl::event_y()-y()+scrollbar_.value();
				
				if (ox<0) ox = 0;
				
				ESel = Pane->Inside (ox, oy);
				if (ESel)
				{
					Pane->SetEMark (ox, oy);
					damage (FL_DAMAGE_SELECTION);
					redraw();
					return 1;
				}
			}
			break;
		
		case FL_RELEASE:
		
			if (dragged_)
			{
				dragged_ = false;
				W = new WordList;
				cnt=0;
				bool selected = false;

				Pane->Selection (W, &selected);
				for (int i=0; i<W->Count;++i)
				{
					cnt += strlen (W->Get(i));
				}
				if (cnt)
				{
					char *cbuf = new char[cnt+1];
					cbuf[0] = 0;
					for (int i=0; i<W->Count;++i) strcat (cbuf, W->Get(i));
					Fl::selection (*this, (const char *) cbuf, (int) cnt);
					SelText.assign (cbuf, cnt);
					delete[] cbuf;
				}
				delete (W);
			}
			else
			{
				ox = Fl::event_x()-x()-18;
				oy = Fl::event_y()-y()+scrollbar_.value();
				ESel = Pane->Inside (ox, oy);
				if (ESel) ESel->HandlePush ();
			}
			break;
			
		default:
			break;
	}
	return (Fl_Group::handle (event));
}

void Fl_View::resize (int xx, int yy, int ww, int hh)
{
	changed_=1;
	Fl_Group::resize (xx,yy,ww,hh);
	scrollbar_.resize (xx+ww-18,yy+2,16,hh-4);
}

void Fl_View::add_header (const char *line, const char *val)
{
	new HTHeader (Pane, (char *) line, (char *) val);
}

void (*ht_attachment_hook) (int) = NULL;

void Fl_View::add_attachment (const char *line, const char *val, int part)
{
	HTHeader *H = new HTHeader (Pane, (char *) line, (char *) val);
	H->SetIProperty ("attachment", part + 1);
	H->SetValueColor (_STYLES.color ("messagebody:hyperlink"));
}

void HTHeader::SetValueColor (int c)
{
	Value->SetIProperty ("fontcolor", c);
}

void Fl_View::add_bitmap (unsigned char *bmp, int _w, int _h)
{
	HTBitmap *B = new HTBitmap (Pane, bmp, _w, _h);
	B->LF = true;
}

void Fl_View::add_textline (const char *line, int _color, int _font, int _size)
{
	WordList W;
	HTGroup *G = new HTGroup (Pane);
	HTWord *H = NULL;
	char *tmp, *xtmp;
	changed_ = 1;
	int split=0;

	if (wild_match ("*http://*", line) || wild_match ("*https://*", line))
	{
		split=1;
	}
	else if (wild_match ("*ftp://*", line))
	{
		split=1;
	}
	else if (wild_match ("*@*.*", line))
	{
		split=1;
	}
	
	G->SetIProperty ("fontcolor", _color);
	G->SetIProperty ("font", _font);
	G->SetIProperty ("fontsize", _size);
	G->LF = true;

	if (split)
	{
		W.MakeGeneric (line, ' ');

		for (int i=0; i<W.Count;++i)
		{
			tmp = new char[strlen(W.Get(i))+2];
			strcpy (tmp, W.Get(i));
			if ((xtmp = strchr (tmp, 10))) *xtmp = 0;
			if ((xtmp = strchr (tmp, 13))) *xtmp = 0;
			strcat (tmp, " ");
			H = new HTWord (G, tmp);
			H->LF = false;
			if (strstr (tmp, "http://") || strstr (tmp, "https://") || strstr (tmp, "ftp://") || wild_match ("*@*.*",tmp))
			{
				H->SetSProperty ("action","dourl");
				iprintf ("set hyperlink property\n");
				H->SetIProperty ("fontcolor", _STYLES.color ("messagebody:hyperlink"));
				H->SetIProperty ("font", _STYLES.font ("messagebody:hyperlink"));
				H->SetIProperty ("fontsize", _STYLES.size ("messagebody:hyperlink"));
				H->LF = false;
			}
			delete[] tmp;
		}
		if (!W.Count) H = new HTWord (G, " ");
		H->LF = true;
	}
	else
	{
		H = new HTWord (G, line);
		H->LF = true;
	}
}

void Fl_View::show_()
{
	if (!visible()) {
		clear_flag(INVISIBLE);
		Fl_Widget *p = parent();
		if (p) p->redraw();
	}
}

void Fl_View::hide_()
{
	if (visible()) {
		set_flag(INVISIBLE);
		Fl_Widget *p = parent();
		if (p) p->redraw();
	}
}

static void scrollbar_callback (Fl_Widget *s, void *)
{
	((Fl_View *)(s->parent()))->redraw();
}

static void flview_drawclip (void *s, int xx, int yy, int ww, int hh)
{
	((Fl_View *) s)->draw_clip (xx,yy,ww,hh);
}

