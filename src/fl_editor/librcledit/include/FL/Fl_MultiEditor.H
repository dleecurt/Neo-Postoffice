
#ifndef FL_MULTI_EDITOR_H
#define FL_MULTI_EDITOR_H 1

#include <rcl/library/lists.h>
#include <FL/Fl_Editor.H>

struct fledit_SessionInfo
{
	Fl_EditorEngine *engine;
	long StartLine;		// starting display line
    int marking;        // boolean flag for marking text
	EditPosition Mark;	// start of marked block (marks from there to present cursor position)
	long SessionNumber;
	bool ReadOnly;		
	wString *SessionName;	// name of session.
	wString *FileName;
	wString *Caption;	
};
	
class Fl_MultiEditor : public Fl_Editor
{
protected:
	long SessionCount;
	struct GenericListNode *Sessions;
	fledit_SessionInfo *CurrentSession;
public:
	fledit_SessionInfo *FindSession(long bynum);
	fledit_SessionInfo *FindSession(const char *byname);
	struct GenericListNode *FindSessionNode(long bynum);
	struct GenericListNode *FindSessionNode(const char *byname);
	FL_EXPORT void Session(fledit_SessionInfo *sinf);
public:
    FL_EXPORT Fl_MultiEditor(int x, int y, int w, int h, const char *cap="");
	FL_EXPORT virtual ~Fl_MultiEditor();
	FL_EXPORT long NewSession(const char *filename=NULL);
	FL_EXPORT void RenameSession(const char *oldname, const char *newname);
	FL_EXPORT void RenameSession(long sessionno, const char *newname);
	FL_EXPORT void SaveCurrent(const char *filename=NULL);
	FL_EXPORT void CloseCurrent();
	long Session() const { return(CurrentSession->SessionNumber); }
	void Session(long bynumber)	{ Session(FindSession(bynumber)); };
	void Session(const char *byname) 		{ Session(FindSession(byname)); };
	FL_EXPORT void NextSession();
	FL_EXPORT void PreviousSession();
	void FirstSession()		{ Session((fledit_SessionInfo*)Sessions->UserData); }
	const char *CurrentFileName()	{ return(CurrentSession->FileName->Get()); }
	const char *SessionName()		{ return(CurrentSession->SessionName->Get()); };
	void SessionName(const char *newname)	{ RenameSession(Session(), newname); };
	long GetSessionCount()		{ return(SessionCount); };
	long ActiveSessions()		{ return(CountList(Sessions)); };
	//  next two are overriden so that the font carries over to all open sessions
	FL_EXPORT void textfont(uchar s);
	FL_EXPORT void textsize(uchar s);
	Fl_Font textfont()		{ return Fl_Editor::textfont(); };
	uchar textsize()		{ return Fl_Editor::textsize(); };
};

                           
#endif //FL_MULTI_EDIT_H
