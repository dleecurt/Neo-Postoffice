// Edit multiple buffers simultaneously.
// There are undoubtedly a lot of ways you could do this, but here's
// an easy one -- just keep an editor engine attached to each buffer,
// and swap the engine and state in and out when switching buffers.
//

#include <FL/Fl_MultiEditor.H>
#include <FL/filename.H>
#include <FL/Fl_Menu.H>
#include <FL/fl_message.H>


Fl_MultiEditor::Fl_MultiEditor(int x, int y, int w, int h, const char *cap)
	: Fl_Editor(x, y, w, h, cap)
{
	SessionCount = 1;
	Sessions = CreateNode(sizeof(fledit_SessionInfo));
	if(Sessions)
	{			// editor widget already created first session, so copy it
		fledit_SessionInfo *sinf = (fledit_SessionInfo *)Sessions->UserData;
		sinf->engine = this->engine;
		sinf->marking = this->marking;
		sinf->Mark = this->Mark;
		sinf->StartLine = this->StartLine;
		sinf->SessionName = new wString;
		sinf->FileName = new wString;
		sinf->ReadOnly = readonly();
		sinf->Caption = new wString;
		*sinf->SessionName = (cap && cap[0]) ? cap : "Untitled";
		sinf->SessionNumber = SessionCount;
		CurrentSession = sinf;
	}
}

Fl_MultiEditor::~Fl_MultiEditor()
{
	while(Sessions)
	{
			// pull node off and reset list to next node
		struct GenericListNode *node = Sessions;
		Sessions = RemHead(Sessions);
			// now release everything for this node
		fledit_SessionInfo *inf = (fledit_SessionInfo *)node->UserData;
		if(inf->SessionName)
			delete inf->SessionName;
		if(inf->FileName)
			delete inf->FileName;
		if(inf->engine)
		{
			inf->engine->Clear();
			delete inf->engine;
		}
		DisposeNode(node);
	}
}

void Fl_MultiEditor::Session(fledit_SessionInfo *sinf)
{
	CurrentSession->engine = engine;
	CurrentSession->marking = marking;
	CurrentSession->Mark = Mark;
	CurrentSession->StartLine = StartLine;
	if(sinf)
	{	
		this->engine = sinf->engine;
		this->marking = sinf->marking;
		this->Mark = sinf->Mark;
		this->StartLine = sinf->StartLine;
		readonly(sinf->ReadOnly);
		CurrentSession = sinf;
		damage(FL_DAMAGE_ALL);
		// don't know why, but it won't repaint until the next event, so make one
		HandleKey(0, "", 0, FL_CTRL);
		HandleScroll();
	}
}

long Fl_MultiEditor::NewSession(const char *name)
{
	FILE *loadfile = NULL;
	if(name)
	{
		loadfile = fopen(name, "r");
		if(!loadfile)
		{
			fl_message("Error opening file.");
			return(-1);
		}
	}
	struct GenericListNode *node = CreateNode(sizeof(fledit_SessionInfo));
	fledit_SessionInfo *sinf = NULL;
	if(node)
	{		
		sinf = (fledit_SessionInfo *)node->UserData;
		sinf->engine = new Fl_EditorEngine(EditWidth);
		sinf->engine->textfont(textfont());
		sinf->engine->textsize(textsize());
		sinf->marking = 0;
		sinf->Mark.Row = sinf->Mark.Column = 0;
		sinf->StartLine = 0;
		sinf->FileName = new wString;
		sinf->Caption = new wString;
		sinf->SessionName = new wString;
		sinf->SessionNumber = ++SessionCount;
		const char *temp = name ? fl_filename_name(name) : NULL;
		if(temp)
            *sinf->SessionName = temp;
		else
		{
			char buffer[40];
			sprintf(buffer, "Untitled %ld", sinf->SessionNumber);
			*sinf->SessionName = buffer;
		}
		if(loadfile)
		{
			sinf->engine->LoadFrom(loadfile);
			fclose(loadfile);
		}
		if(this->Sessions)
			AddTail(this->Sessions, node);
		else
			Sessions = node;
		Session(sinf);
		return(sinf->SessionNumber);
	}
	return(-1);
}

struct GenericListNode *Fl_MultiEditor::FindSessionNode(long bynum)
{
	struct GenericListNode *target=NULL,  *node = Sessions;
	while(node)
	{
		struct fledit_SessionInfo *tinf = (struct fledit_SessionInfo *)node->UserData;
		if(tinf->SessionNumber == bynum)
		{
			target = node;
			break;
		}
		else
			node = node->Next;
	}
	return(target);
}

struct GenericListNode *Fl_MultiEditor::FindSessionNode(const char *byname)
{
	struct GenericListNode *target=NULL,  *node = Sessions;
	while(node)
	{
		struct fledit_SessionInfo *tinf = (struct fledit_SessionInfo *)node->UserData;
		if(*tinf->SessionName == byname)
		{
			target = node;
			break;
		}
		else
			node = node->Next;
	}
	return(target);
}

fledit_SessionInfo *Fl_MultiEditor::FindSession(long sessno)
{
	struct GenericListNode *node = FindSessionNode(sessno);
	struct fledit_SessionInfo *sinf = NULL;
	if(node)
		sinf = (struct fledit_SessionInfo *)node->UserData;
	return(sinf);
}

fledit_SessionInfo *Fl_MultiEditor::FindSession(const char *byname)
{
	struct GenericListNode *node = FindSessionNode(byname);
	struct fledit_SessionInfo *sinf = NULL;
	if(node)
		sinf = (struct fledit_SessionInfo *)node->UserData;
	return(sinf);
}

void Fl_MultiEditor::RenameSession(const char *oldname, const char *newname)
{
	struct fledit_SessionInfo *inf = FindSession(oldname);
	if(inf)
		*inf->SessionName = newname;
}

void Fl_MultiEditor::RenameSession(long sessionno, const char *newname)
{
	struct fledit_SessionInfo *inf = FindSession(sessionno);
	if(inf)
		*inf->SessionName = newname;
}

void Fl_MultiEditor::CloseCurrent()
{
	struct GenericListNode *target = FindSessionNode(CurrentSession->SessionNumber);

	if(target)
	{
		struct fledit_SessionInfo *inf = (struct fledit_SessionInfo *)target->UserData;
		if(target->Next)
			NextSession();
		else if(target->Previous)
			PreviousSession();
		else
		{
			// no more sessions?  Just clear out this one.
			CurrentSession->engine->Clear();
			CurrentSession->marking = marking = 0;
			CurrentSession->StartLine = StartLine = 0;
			*CurrentSession->FileName = "";
			damage(FL_DAMAGE_ALL);
		}
		if(CurrentSession != (fledit_SessionInfo*)target->UserData)
		{
			if(inf->SessionName)
				delete inf->SessionName;
			if(inf->FileName)
				delete inf->FileName;
			if(inf->Caption)
				delete inf->Caption;
			if(inf->engine)
			{
				inf->engine->Clear();
				delete inf->engine;
			}
			Sessions = RemoveNode(target);
			DisposeNode(target);
		}		
	}
}

void Fl_MultiEditor::SaveCurrent(const char *fn)
{
	wString filename = fn ? fn : CurrentSession->FileName->Get();
	if(filename.Length())
	{
		FILE *out = fopen(filename.Get(), "w");
		if(out)
		{
			CurrentSession->engine->SaveTo(out);
			fclose(out);
		}
		*CurrentSession->FileName = filename;
		const char *temp = fl_filename_name(fn);
		if(!temp)
			temp = fn;
		if(temp)
            *CurrentSession->SessionName = temp;
		engine->Changed() = FALSE;
	}
}	

void Fl_MultiEditor::NextSession()
{
	struct GenericListNode *node = FindSessionNode(CurrentSession->SessionNumber);
	if(node) {
		if(node->Next)
		{
			struct fledit_SessionInfo *sinf = (struct fledit_SessionInfo *)node->Next->UserData;
			Session(sinf);
		}
		else if(Sessions)
		{
			struct fledit_SessionInfo *sinf = (struct fledit_SessionInfo *)Sessions->UserData;
			Session(sinf);
		}
	}
}

void Fl_MultiEditor::PreviousSession()
{
	struct GenericListNode *node = FindSessionNode(CurrentSession->SessionNumber);
	if(node) {
		if(node->Previous)
		{
			struct fledit_SessionInfo *sinf = (struct fledit_SessionInfo *)node->Previous->UserData;
			Session(sinf);
		}
		else 
		{
			node = FindTail(Sessions);
			if(node && (node != Sessions))
			{
				struct fledit_SessionInfo *sinf = (struct fledit_SessionInfo *)node->UserData;
				Session(sinf);
			}
		}
	}
}

#if (FL_MAJOR_VERSION > 1)
void Fl_MultiEditor::textfont(Fl_Font s)
#else
void Fl_MultiEditor::textfont(uchar s)
#endif
{
	// set font for this session
	Fl_Editor::textfont(s);
	// and for the rest of them
	struct GenericListNode *node = Sessions;
	while(node)
	{
		struct fledit_SessionInfo *sinf = (struct fledit_SessionInfo*)node->UserData;
		sinf->engine->textfont(s);
		node = node->Next;
	}
}

#if (FL_MAJOR_VERSION > 1)
void Fl_MultiEditor::textsize(unsigned s)
#else
void Fl_MultiEditor::textsize(uchar s)
#endif
{
	// set font for this session
	Fl_Editor::textsize(s);
	// and for the rest of them
	struct GenericListNode *node = Sessions;
	while(node)
	{
		struct fledit_SessionInfo *sinf = (struct fledit_SessionInfo*)node->UserData;
		sinf->engine->textsize(s);
		sinf->engine->ReformatParagraph(TRUE, TRUE);
		node = node->Next;
	}
}

