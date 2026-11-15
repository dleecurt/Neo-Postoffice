#ifndef FL_EDITOR_H
#define FL_EDITOR_H 1

#include <Fl/Fl_Scrollbar.H>
#include <FL/Fl.H>
#include <Fl/fl_draw.H>
#include <FL/Fl_Window.H>
#include <Fl/Fl_Tile.H>
#include <Fl/Fl_Box.H>

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
public:
	Fl_CursorBox(int nx, int ny, int nw, int nh, const char *nc=""): Fl_Box(nx, ny, nw, nh, nc) {  AllowResize = false; };
	void hide()		{ set_flag(INVISIBLE); };	// DON'T redraw the parent!
	void show()	   { clear_flag(INVISIBLE); redraw(); };	// DON'T redraw the parent!
	void resize(int newx, int newy, int neww, int newh)	{ if(AllowResize) Fl_Box::resize(newx, newy, neww, newh); }
	void setsize(int nx, int ny, int nw, int nh)	{ AllowResize = true; resize(nx, ny, nw, nh); AllowResize = false;  redraw(); };
};

class Fl_EditorEngine : public EditorEngine
{
	friend class Fl_Editor;
protected:
	uchar textfont_;
	uchar textsize_;
	short MeasureTextWidth(const char *t);
	short MeasureTextHeight(const char *t);
public:
	Fl_EditorEngine(short p, uchar tf=8, uchar ts=12) : EditorEngine(p) { textfont(tf); textsize(ts); };
	Fl_Font textfont() const { return(Fl_Font)textfont_; }
	void textfont(uchar s) { textfont_ = s; }
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

struct  fleditor_FindOptions
{
	wString SearchFor;
	wString ReplaceWith;
	bool MatchCase;
	bool MatchWord;
	bool SearchCursor;	// only one or the other of these two will be true.
	bool SearchDoc;
	bool PreserveCase;
	bool PromptReplace;
};

struct fleditor_DrawState
{
	struct fleditor_DrawState *next;	// to implement a simple stack
	EditPosition EffectivePos;				// where to begin this drawing state
	EditPosition EffectiveUntil;	         // where to end this drawing state
	Fl_Color fg;
	Fl_Color bg;
	Fl_Color sel;
	uchar font;
	uchar size;
	long attrib;		// for things like bold, italic, underline, etc.  Not used yet
	fleditor_DrawState(Fl_Color nfg = (Fl_Color)-1, Fl_Color nbg = (Fl_Color)-1, Fl_Color nsel = (Fl_Color)-1, uchar nf = 0, uchar ns = 0, long atr = 0):
			fg(nfg), bg(nbg), sel(nsel), font(nf), size(ns), attrib(atr) { next = NULL; };
	fleditor_DrawState& operator=(const fleditor_DrawState& ds)  
		{	fg = ds.fg;  bg = ds.bg; sel = ds.sel; font = ds.font; size = ds.size; attrib = ds.attrib; EffectivePos = ds.EffectivePos;  return *this; };
};

// LEFTMARGIN = pixels between outer edge of widget and text on either side
// TOPMARGIN = pixels between outer edge of widget and text on top and bottom.
// SCROLLERWIDTH is the width of the scroll bar.
// PANELHEIGHT is the height of the indicator panel beneath the editor.

#define LEFTMARGIN 5
#define TOPMARGIN 2
#define SCROLLERWIDTH 16
#define PANELHEIGHT 21

#define DAMAGE_EDITOR	0x10
#define DAMAGE_LINE	0x20
#define DAMAGE_CHAR	0x40
#define DAMAGE_PARTIAL 0x60
#define DAMAGE_CURSOR	0x70

class Fl_Editor : public Fl_Group
{
	void setfont() const;
protected:
	Fl_EditorEngine *engine;
	Fl_Scrollbar *scroller;
	Fl_Box *editbox;
	Fl_Tile *lights;
	Fl_Tile *POSlight;
	Fl_Tile *CAPlight;
	Fl_Tile *INSlight;
	Fl_Tile *NUMlight;  
	int _scrollspeed;
	int EditWidth;
	fleditor_FindOptions findopts;
	uchar textfont_;
	uchar textsize_;
	uchar textcolor_;
	char PositionString[30];
	short VisibleLines;	// how many lines presently visible in the widget?
	long StartLine;		// first line that is visible in widget
	int overstrike;		// boolean flag for overstrike (else insert) mode
	int marking;		// boolean flag for marking text
	EditPosition Mark;	// start of marked block (marks from there to present cursor position)
	EditPosition DrawFrom;	
	bool selectlines;	// flag for selecting line-at-a-time after triple-click
	bool Readonly;
	bool TimerEnable;	// scroll timer
	fleditor_DrawState *DrawState;
	int lastcursorsize;	
	Fl_CursorBox *Cursor;
protected:
	virtual FL_EXPORT Fl_Window *MakeFindDialog();
	virtual FL_EXPORT Fl_Window *MakeReplaceDialog();
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
	FL_EXPORT bool Find();
	FL_EXPORT bool Replace();
	FL_EXPORT bool FindNext();
	FL_EXPORT void ShowPanel(bool show);
	FL_EXPORT void Cut();
	FL_EXPORT void resize(int x, int y, int w, int h);
	static FL_EXPORT void Fl_Editor::draw_clip(void *v, int X, int Y, int W, int H);
	FL_EXPORT Fl_Font textfont() const { return (Fl_Font) textfont_; };
	virtual FL_EXPORT void textfont(uchar s);
	FL_EXPORT uchar textsize() const { return textsize_; };
	virtual FL_EXPORT void textsize(uchar s);
	// everything below this is inline.
	void FindCursor(short screenX, short screenY)	{ if(FindRow(screenY) < 0) engine->Command(MOVE_BOF); else FindColumn(screenX); }
	void Copy() { CopySelection(true);  damage(FL_DAMAGE_ALL); };
	void Paste() { Fl::paste(*this);     damage(FL_DAMAGE_ALL); };
	Fl_Color textcolor() const { return(Fl_Color)textcolor_; }
	void textcolor(uchar n) { textcolor_ = n; }
	void LoadFrom(FILE *file) { engine->LoadFrom(file); redraw(); }
	void SaveTo(FILE *file) { engine->SaveTo(file); }
	void LoadFrom(const char *buffer) { engine->LoadFrom(buffer); redraw(); }
	void SaveTo(char *buffer) { engine->SaveTo(buffer); }
	long CharacterCount() { return engine->GetCharacterCount(); }
	void Clear() { engine->Clear(); redraw(); }
	bool changed() { return engine->Changed(); }
	bool readonly() { return Readonly; }
	void readonly(bool state) { Readonly = state; }
	bool Wrap() { return !(engine->GetFlags() & EF_NOWRAP); }
	void Wrap(bool wrapit) { engine->SetFlags(wrapit ? engine->GetFlags() & ~EF_NOWRAP : engine->GetFlags() | EF_NOWRAP); }
	void scrollspeed(int linespersec)	{ _scrollspeed = linespersec; };
	int scrollspeed()	{ return _scrollspeed; }	
	bool TimerEnabled()	{ return TimerEnable; }	
	Fl_EditorEngine *Engine()		{ return engine; };
	void SyncDisplay();	
};


#endif //FL_EDIT_H
