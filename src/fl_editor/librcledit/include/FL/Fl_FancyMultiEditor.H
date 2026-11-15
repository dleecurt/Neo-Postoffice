#ifndef FL_FANCY_MULTI_EDITOR_H
#define FL_FANCY_MULTI_EDITOR_H 1

#include <FL/Fl_MultiEditor.H>
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

class Fl_FancyMultiEditor : public Fl_MultiEditor
{
protected:
	Fl_StatusPanel *lights;
	fleditor_FindOptions findopts;
	char PositionString[30];
protected:
	virtual FLE_EXPORT Fl_Window *MakeFindDialog();
	virtual FLE_EXPORT Fl_Window *MakeReplaceDialog();
public:
	FLE_EXPORT Fl_FancyMultiEditor(int x, int y, int w, int h, const char *cap="") ;
	virtual FLE_EXPORT ~Fl_FancyMultiEditor();
	FLE_EXPORT void Session(fledit_SessionInfo *sinf);
	FLE_EXPORT int handle(int event);
	FLE_EXPORT bool Find();
	FLE_EXPORT bool Replace();
	FLE_EXPORT bool FindNext();
	FLE_EXPORT void ShowPanel(bool show);
	// everything below this is inline.
	bool ShowPanel() 	{ return lights->visible(); };
	// why is this necessary?  Can't seem to call it from the base class!
	long Session()		{ return Fl_MultiEditor::Session(); };
};

#endif //FL_FANCY_EDITOR_H
