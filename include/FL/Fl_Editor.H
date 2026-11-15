#ifndef FL_EDITOR_H
#define FL_EDITOR_H 1

#include <FL/Fl_Scrollbar.H>
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Box.H>
#if (FL_MAJOR_VERSION > 1)
#include <FL/Fl_Font.H>
#endif

#include <rcl/library/editor.hpp>

#ifdef WIN32
#include <Fl/win32.H>
#else
#include <Fl/x.H>
#endif

class Fl_CursorBox : public Fl_Box
{
protected:
	bool AllowResize;
	bool AlwaysHide;
public:
	Fl_CursorBox(int nx, int ny, int nw, int nh, const char *nc=""): Fl_Box(nx, ny, nw, nh, nc) {  AllowResize = AlwaysHide = false; };
#if (FL_MAJOR_VERSION > 1)
	void hide()		{ set_flag(FL_INVISIBLE); };	// DON'T redraw the parent!
	void show()	   { if(!AlwaysHide) {clear_flag(FL_INVISIBLE); redraw(); } };	// DON'T redraw the parent!
#else
	void hide()		{ set_flag(INVISIBLE); };	// DON'T redraw the parent!
	void show()	   { if(!AlwaysHide) { clear_flag(INVISIBLE); redraw(); } };	// DON'T redraw the parent!
#endif
	void resize(int newx, int newy, int neww, int newh)	{ if(AllowResize) Fl_Box::resize(newx, newy, neww, newh); }
	void setsize(int nx, int ny, int nw, int nh)	{ AllowResize = true; resize(nx, ny, nw, nh); AllowResize = false;  redraw(); };
	void hideall(bool hideit)	{ AlwaysHide = hideit; if(hideit && visible()) hide(); else if(!hideit && !visible()) show(); };
	bool hideall()	{ return AlwaysHide; };
};


class Fl_EditorEngine : public EditorEngine
{
	friend class Fl_Editor;
protected:
#if (FL_MAJOR_VERSION > 1)
	Fl_Font textfont_;
	int textsize_;
#else
	uchar textfont_;
	uchar textsize_;
#endif        
	FL_EXPORT short MeasureTextWidth(const char *t);
	FL_EXPORT short MeasureTextHeight(const char *t);
public:
#if (FL_MAJOR_VERSION > 1)
	Fl_EditorEngine(short p, Fl_Font tf=0, int ts=12) : EditorEngine(p) { textfont(tf); textsize(ts); };
        void textfont(Fl_Font s) { textfont_ = s; }
#else        
	Fl_EditorEngine(short p, uchar tf=8, uchar ts=12) : EditorEngine(p) { textfont(tf); textsize(ts); };
        void textfont(uchar s) { textfont_ = s; }
#endif        
	Fl_Font textfont() const { return(Fl_Font)textfont_; }
	uchar textsize() const { return textsize_; }
	void textsize(uchar s) { textsize_ = s; }
};

enum CursorStyles
{
	CURSOR_NORMAL,
	CURSOR_OVERSTRIKE,
	CURSOR_HIDE,
	CURSOR_UNHIDE
};

struct fleditor_DrawState
{
	struct fleditor_DrawState *next;	// to implement a simple stack
	EditPosition EffectivePos;				// where to begin this drawing state
	EditPosition EffectiveUntil;	         // where to end this drawing state
	Fl_Color fg;
	Fl_Color bg;
	Fl_Color sel;
#if (FL_MAJOR_VERSION > 1)
	Fl_Font font;
	int size;
	fleditor_DrawState(Fl_Color nfg = (Fl_Color)-1, Fl_Color nbg = (Fl_Color)-1, Fl_Color nsel = (Fl_Color)-1, Fl_Font nf = 0, int ns = 0, long atr = 0):
			fg(nfg), bg(nbg), sel(nsel), font(nf), size(ns), attrib(atr) { next = NULL; };
#else
	uchar font;
	uchar size;
	fleditor_DrawState(Fl_Color nfg = (Fl_Color)-1, Fl_Color nbg = (Fl_Color)-1, Fl_Color nsel = (Fl_Color)-1, uchar nf = 0, uchar ns = 0, long atr = 0):
			fg(nfg), bg(nbg), sel(nsel), font(nf), size(ns), attrib(atr) { next = NULL; };
#endif        
	long attrib;		// for things like bold, italic, underline, etc.  Not used yet
	fleditor_DrawState& operator=(const fleditor_DrawState& ds)  
		{	fg = ds.fg;  bg = ds.bg; sel = ds.sel; font = ds.font; size = ds.size; attrib = ds.attrib; EffectivePos = ds.EffectivePos;  return *this; };
};

// LEFTMARGIN = pixels between outer edge of widget and text on either side
// TOPMARGIN = pixels between outer edge of widget and text on top and bottom.
// SCROLLERWIDTH is the width of the scroll bar.
// PANELHEIGHT is the height of the indicator panel beneath the editor.

#define LEFTMARGIN 5
#define TOPMARGIN 2
#define SCROLLERWIDTH 17
#define PANELHEIGHT 21

#define DAMAGE_EDITOR	0x10
#define DAMAGE_LINE	0x20
#define DAMAGE_CHAR	0x40
#define DAMAGE_PARTIAL 0x60
#define DAMAGE_CURSOR	0x70

class Fl_Editor : public Fl_Group
{
	FL_EXPORT void setfont() const;
protected:
	Fl_EditorEngine *engine;
	Fl_Box *editbox;
	Fl_Scrollbar *scroller;
	int _scrollspeed;
	int EditWidth;
#if (FL_MAJOR_VERSION > 1)
	Fl_Font textfont_;
	unsigned textsize_;
	uchar textcolor_;
#else
	uchar textfont_;
	uchar textsize_;
	uchar textcolor_;
#endif        
	short VisibleLines;	// how many lines presently visible in the widget?
	long StartLine;		// first line that is visible in widget
	int overstrike;		// boolean flag for overstrike (else insert) mode
	int marking;		// boolean flag for marking text
	EditPosition Mark;	// start of marked block (marks from there to present cursor position)
	EditPosition DrawFrom;	
	bool selectlines;	// flag for selecting line-at-a-time after triple-click
	bool Readonly;
	bool Browsemode;	// Modifies cursor arrow behaviour. Up/down scroll up/down, left/right are ignored
	bool TimerEnable;	// scroll timer
	bool myHideCursor;	// for hiding in focus/unfocus, prevents overriding user HideCursor() calls.
        bool keeptabs;        // whether editor uses tabs, or leaves them to FLTK.
	fleditor_DrawState *DrawState;
	Fl_CursorBox *Cursor;
protected:
	virtual FL_EXPORT int HandleKey(int keyevent, const char *keytext, int textlen, int state);
	virtual FL_EXPORT int HandleCursorKey(int key, const char *text, int len, int state);
	FL_EXPORT void ShowCursor(CursorStyles show);
	FL_EXPORT void CopySelection(bool clearmarks);
	FL_EXPORT void draw();
	virtual FL_EXPORT void DrawLine(long editorlineno, int left, int top, wString& line, fleditor_DrawState& state);
	FL_EXPORT void PushState(const fleditor_DrawState& state);
	FL_EXPORT void PopState();
public:
	FL_EXPORT Fl_Editor(int x, int y, int w, int h, const char *cap="") ;
	virtual FL_EXPORT ~Fl_Editor();
	FL_EXPORT int handle(int event);
	FL_EXPORT short CursorX();
	FL_EXPORT short CursorY();
	FL_EXPORT long FindColumn(short screenX);	
	FL_EXPORT long FindRow(short screenY);
	FL_EXPORT void HandleScroll();
	FL_EXPORT void Cut();
	FL_EXPORT void resize(int x, int y, int w, int h);
	static FL_EXPORT void Fl_Editor::draw_clip(void *v, int X, int Y, int W, int H);
#if (FL_MAJOR_VERSION > 1)
	FL_EXPORT Fl_Font textfont() const { return  (Fl_Font) textfont_; };
	virtual FL_EXPORT void textfont(Fl_Font s);
	FL_EXPORT unsigned textsize() const { return textsize_; };
#else        
	FL_EXPORT Fl_Font textfont() const { return (Fl_Font) textfont_; };
	virtual FL_EXPORT void textfont(uchar s);
	FL_EXPORT uchar textsize() const { return textsize_; };
#endif        
	virtual FL_EXPORT void textsize(uchar s);
	virtual FL_EXPORT void SyncDisplay();	
	FL_EXPORT void browse(bool setit);
	// everything below this is inline.
	void FindCursor(short screenX, short screenY)	{ if(FindRow(screenY) < 0) engine->Command(MOVE_BOF); else FindColumn(screenX); }
	void Copy() { CopySelection(true);  damage(FL_DAMAGE_ALL); };
	void Paste() { Fl::paste(*this);     damage(FL_DAMAGE_ALL); };
	Fl_Color textcolor() const { return(Fl_Color)textcolor_; }
	void textcolor(uchar n) { textcolor_ = n; }
	void LoadFrom(FILE *file) { engine->LoadFrom(file); ShowCursor(CURSOR_UNHIDE); SyncDisplay(); }
	void SaveTo(FILE *file) { engine->SaveTo(file); }
	void LoadFrom(const char *buffer) { engine->LoadFrom(buffer); ShowCursor(CURSOR_UNHIDE); SyncDisplay(); }
	void SaveTo(char *buffer) { engine->SaveTo(buffer); }
	long CharacterCount() { return engine->GetCharacterCount(); }
	void Clear() { engine->Clear(); redraw(); }
	bool changed() const { return engine->Changed(); }
	bool readonly() const { return Readonly; }
	void readonly(bool state) { Readonly = state; }
	// WARNING: Turning wrap off doesn't presently work!  
	// The draw routines for it are not in place yet!  Read the docs!
	bool Wrap() { return !(engine->GetFlags() & EF_NOWRAP); }
	void Wrap(bool wrapit) { engine->SetFlags(wrapit ? engine->GetFlags() & ~EF_NOWRAP : engine->GetFlags() | EF_NOWRAP); }
	void scrollspeed(int linespersec)	{ _scrollspeed = linespersec; };
	int scrollspeed() const	{ return _scrollspeed; }	
	bool TimerEnabled()	const { return TimerEnable; }	
	Fl_EditorEngine *Engine()  	{ return engine; };
	void HideCursor(bool hideit)	{ Cursor->hideall(hideit); };
	bool HideCursor() { return Cursor->hideall(); };
	bool browse()		{ return Browsemode; };
        bool WantTabs()   { return keeptabs; };
        void WantTabs(bool setting)     { keeptabs = setting; };
};

#if (FL_MAJOR_VERSION > 1)
#ifndef FL_TAB
        #define FL_TAB FL_Tab
#endif        
#endif


#endif //FL_EDIT_H
