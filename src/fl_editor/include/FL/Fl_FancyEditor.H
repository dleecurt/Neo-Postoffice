#ifndef FL_FANCY_EDITOR_H
#define FL_FANCY_EDITOR_H 1

#include <FL/Fl_Editor.H>
#include <FL/Fl_StatusPanel.H>

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

class Fl_FancyEditor : public Fl_Editor
{
protected:
	Fl_StatusPanel *lights;
	fleditor_FindOptions findopts;
	char PositionString[30];
protected:
	virtual FL_EXPORT Fl_Window *MakeFindDialog();
	virtual FL_EXPORT Fl_Window *MakeReplaceDialog();
public:
	FL_EXPORT Fl_FancyEditor(int x, int y, int w, int h, const char *cap="") ;
	virtual FL_EXPORT ~Fl_FancyEditor();
	FL_EXPORT int handle(int event);
	FL_EXPORT bool Find();
	FL_EXPORT bool Replace();
	FL_EXPORT bool FindNext();
	FL_EXPORT void ShowPanel(bool show);
	// everything below this is inline.
	bool ShowPanel() 	{ return lights->visible(); };
};

#endif //FL_FANCY_EDITOR_H
