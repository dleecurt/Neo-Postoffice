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
	virtual FLE_EXPORT Fl_Window *MakeFindDialog();
	virtual FLE_EXPORT Fl_Window *MakeReplaceDialog();
public:
	FLE_EXPORT Fl_FancyEditor(int x, int y, int w, int h, const char *cap="") ;
	virtual FLE_EXPORT ~Fl_FancyEditor();
	FLE_EXPORT int handle(int event);
	FLE_EXPORT bool Find();
	FLE_EXPORT bool Replace();
	FLE_EXPORT bool FindNext();
	FLE_EXPORT void ShowPanel(bool show);
	// everything below this is inline.
	bool ShowPanel() 	{ return lights->visible(); };
};

#endif //FL_FANCY_EDITOR_H
