//
// Implementation of text editor class for FLTK.
//

#include <FL/Fl_FancyMultiEditor.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Group.H>
#include <FL/fl_draw.H>

// **************************************************
//				Private structure types
// **************************************************

struct fleditor_FindDialogPtrs
{
	// all these pointers are to "read" the dialog when  it closes, rather than use a bunch of callbacks.
	Fl_Input *SearchFor;
	Fl_Input *ReplaceWith;
	Fl_Check_Button *MatchCase;
	Fl_Check_Button *MatchWord;
	Fl_Round_Button *SearchCursor;
	Fl_Round_Button *SearchDoc;
	Fl_Check_Button *PreserveCase;
	Fl_Check_Button *PromptReplace;
};

struct fleditor_FindDialogData
{
	fleditor_FindOptions Options;
	fleditor_FindDialogPtrs Pointers;
	bool Cancelled;		// the OK button will set this to false, otherwise it's true
};

static void cb_EditScroller(Fl_Scrollbar* o, void *udata) 
{
	Fl_Editor *ed = (Fl_Editor *)udata;
	if ( ed )
		ed->HandleScroll();
}


static void cb_FindOptionsOK(Fl_Return_Button* o, void *udata) 
{
	// reads dialog options for either a search or a replace dialog
	fleditor_FindDialogData *dlgdata = (fleditor_FindDialogData *)o->window()->user_data();
	if ( dlgdata )
	{
		if ( dlgdata->Pointers.SearchFor )
			dlgdata->Options.SearchFor = dlgdata->Pointers.SearchFor->value();
		if ( dlgdata->Pointers.ReplaceWith )
			dlgdata->Options.ReplaceWith = dlgdata->Pointers.ReplaceWith->value();
		if ( dlgdata->Pointers.MatchCase )
			dlgdata->Options.MatchCase = dlgdata->Pointers.MatchCase->value() ? true : false;
		if ( dlgdata->Pointers.MatchWord )
			dlgdata->Options.MatchWord = dlgdata->Pointers.MatchWord->value() ? true : false;
		if ( dlgdata->Pointers.SearchCursor )
			dlgdata->Options.SearchCursor = dlgdata->Pointers.SearchCursor->value() ? true : false;
		if ( dlgdata->Pointers.SearchDoc )
			dlgdata->Options.SearchDoc = dlgdata->Pointers.SearchDoc->value() ? true : false;
		if ( dlgdata->Pointers.PreserveCase )
			dlgdata->Options.PreserveCase = dlgdata->Pointers.PreserveCase->value() ? true : false;
		if ( dlgdata->Pointers.PromptReplace )
			dlgdata->Options.PromptReplace = dlgdata->Pointers.PromptReplace->value() ? true : false;
		dlgdata->Cancelled = false;		// let program know to process this data
		o->window()->hide();
	}
}

// **************************************************
//						Widget functions
// **************************************************

FL_EXPORT Fl_FancyMultiEditor::Fl_FancyMultiEditor(int x, int y, int w, int h, const char *cap)
	: Fl_MultiEditor(x, y, w, h-PANELHEIGHT, cap)
{
	// set default find options
	findopts.SearchFor = "";
	findopts.ReplaceWith = "";
	findopts.MatchCase = false;
	findopts.MatchWord = false;
	findopts.SearchCursor = true;
	findopts.SearchDoc = false;
	findopts.PreserveCase = false;
	findopts.PromptReplace = true;
	// add the status panel
	short paneltop = (y+h) - PANELHEIGHT;
	lights = new Fl_StatusPanel(x, paneltop, w, PANELHEIGHT);
}

FL_EXPORT Fl_FancyMultiEditor::~Fl_FancyMultiEditor()
{
	if ( lights )
		delete lights;
}


FL_EXPORT int Fl_FancyMultiEditor::handle(int event)
{
	long curstart = StartLine;
	int handled = Fl_Editor::handle(event);

	if(!handled && (event == FL_KEYBOARD))
	{
		switch ( Fl::event_key() )
		{
			case FL_Caps_Lock:
			case FL_Num_Lock:
		   		handled = lights->handle(event);
				break;
		}
	}
	lights->ins(!overstrike);
	if ( handled )
	{
		// update position indicator
		snprintf(PositionString, sizeof(PositionString), "R %ld C %ld", engine->CursorRow() + 1, engine->CursorColumn() + 1);
		lights->status(PositionString);
	}
	return(handled);
}

FL_EXPORT void Fl_FancyMultiEditor::ShowPanel(bool showit)
{
	if ( showit )
	{
		if ( !lights->visible() )
		{
			lights->show();
			resize(x(), y(), w(), h() - lights->h());
		}
	}
	else
	{
		if ( lights->visible() )
		{
			lights->hide();
			resize(x(), y(), w(), h() + lights->h());
		}
	}
}


FL_EXPORT Fl_Window *Fl_FancyMultiEditor::MakeFindDialog()
{
	fleditor_FindDialogData *dlgdata = new fleditor_FindDialogData;
	Fl_Window* w = (Fl_Window *)0;
	if ( dlgdata )
	{
		dlgdata->Options  = findopts;
		dlgdata->Cancelled = true;
		memset(&dlgdata->Pointers, 0, sizeof(dlgdata->Pointers));

		{ Fl_Window* o = new Fl_Window(324, 132);
			w = o;
			{ Fl_Input* o = dlgdata->Pointers.SearchFor =  new Fl_Input(72, 6, 222, 25, "Search For");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->text_size(12);
#else
				o->labelsize(12);
				o->textsize(12);
#endif
				o->value(dlgdata->Options.SearchFor.Get());
			}
			{ Fl_Check_Button* o = dlgdata->Pointers.MatchCase = new Fl_Check_Button(7, 34, 103, 25, "Match &Case");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->glyph_box(FL_DIAMOND_DOWN_BOX);
#else
				o->labelsize(12);
				o->down_box(FL_DIAMOND_DOWN_BOX);
#endif
				o->value(dlgdata->Options.MatchCase);
			}
			{ Fl_Check_Button* o = dlgdata->Pointers.MatchWord = new Fl_Check_Button(7, 55, 163, 25, "Match Whole &Word Only");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->glyph_box(FL_DIAMOND_DOWN_BOX);
#else
				o->labelsize(12);
				o->down_box(FL_DIAMOND_DOWN_BOX);
#endif
				o->value(dlgdata->Options.MatchWord);
			}
			{ Fl_Return_Button* o = new Fl_Return_Button(232, 102, 73, 25, "Find");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
#else
				o->labelsize(12);
#endif
				o->callback((Fl_Callback *)cb_FindOptionsOK);
			}
			{ Fl_Group* o = new Fl_Group(177, 45, 134, 51, "Search");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->box(FL_EMBOSSED_BOX);
				o->clear_flag(FL_ALIGN_MASK);
				o->set_flag(FL_ALIGN_TOP | FL_ALIGN_LEFT);
#else
				o->labelsize(12);
				o->box(FL_EMBOSSED_FRAME);
				o->align(FL_ALIGN_TOP_LEFT);
#endif
				{ Fl_Round_Button* o = dlgdata->Pointers.SearchCursor = new Fl_Round_Button(179, 48, 92, 25, "From c&ursor");
					o->type(102);
#if (FL_MAJOR_VERSION > 1)
					o->label_size(12);
					o->glyph_box(FL_ROUND_DOWN_BOX);
					o->clear_flag(FL_ALIGN_MASK);
					o->set_flag(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
#else
					o->labelsize(12);
					o->down_box(FL_ROUND_DOWN_BOX);
					o->align(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
#endif
					if ( dlgdata->Options.SearchCursor )
						o->setonly();
				}
				{ Fl_Round_Button* o = dlgdata->Pointers.SearchDoc = new Fl_Round_Button(179, 69, 115, 25, "Entire &document");
					o->type(102);
#if (FL_MAJOR_VERSION > 1)
					o->label_size(12);
					o->glyph_box(FL_ROUND_DOWN_BOX);
					o->clear_flag(FL_ALIGN_MASK);
					o->set_flag(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
#else
					o->labelsize(12);
					o->down_box(FL_ROUND_DOWN_BOX);
					o->align(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
#endif
					if ( dlgdata->Options.SearchDoc )
						o->setonly();
				}
				o->end();
			}
			o->user_data(dlgdata);
			o->set_modal();
			o->end();
		}
	}
	return(w);
}

FL_EXPORT Fl_Window *Fl_FancyMultiEditor::MakeReplaceDialog()
{
	fleditor_FindDialogData *dlgdata = new fleditor_FindDialogData;
	Fl_Window* w = (Fl_Window *)0;
	if ( dlgdata )
	{
		dlgdata->Options  = findopts;
		dlgdata->Cancelled = true;
		memset(&dlgdata->Pointers, 0, sizeof(dlgdata->Pointers));

		{ Fl_Window* o = new Fl_Window(321, 180);
			w = o;
			{ Fl_Input* o = dlgdata->Pointers.SearchFor = new Fl_Input(80, 6, 230, 25, "Search For  ");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->text_size(12);
#else
				o->labelsize(12);
				o->textsize(12);
#endif
				o->value(dlgdata->Options.SearchFor.Get());
			}
			{ Fl_Input* o = dlgdata->Pointers.ReplaceWith = new Fl_Input(80, 31, 229, 25, "Replace with");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->text_size(12);
#else
				o->labelsize(12);
				o->textsize(12);
#endif
				o->value(dlgdata->Options.ReplaceWith.Get());
			}
			{ Fl_Check_Button* o = dlgdata->Pointers.MatchCase = new Fl_Check_Button(7, 61, 99, 25, "Match &Case");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->glyph_box(FL_DIAMOND_DOWN_BOX);
#else
				o->labelsize(12);
				o->down_box(FL_DIAMOND_DOWN_BOX);
#endif
				o->value(dlgdata->Options.MatchCase);
			}
			{ Fl_Check_Button* o = dlgdata->Pointers.PreserveCase = new Fl_Check_Button(7, 82, 112, 25, "&Preserve Case");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->glyph_box(FL_DIAMOND_DOWN_BOX);
#else
				o->labelsize(12);
				o->down_box(FL_DIAMOND_DOWN_BOX);
#endif
				o->value(dlgdata->Options.PreserveCase);
			}
			{ Fl_Check_Button* o = dlgdata->Pointers.MatchWord = new Fl_Check_Button(7, 103, 163, 25, "Match Whole &Word Only");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->glyph_box(FL_DIAMOND_DOWN_BOX);
#else
				o->labelsize(12);
				o->down_box(FL_DIAMOND_DOWN_BOX);
#endif
				o->value(dlgdata->Options.MatchWord);
			}
			{ Fl_Check_Button* o = dlgdata->Pointers.PromptReplace = new Fl_Check_Button(7, 124, 132, 25, "Prompt on &replace");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->glyph_box(FL_DIAMOND_DOWN_BOX);
#else
				o->labelsize(12);
				o->down_box(FL_DIAMOND_DOWN_BOX);
#endif
				o->value(dlgdata->Options.PromptReplace);
			}
			{ Fl_Return_Button* o = new Fl_Return_Button(231, 147, 73, 25, "OK");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
#else
				o->labelsize(12);
#endif
				o->callback((Fl_Callback *)cb_FindOptionsOK);
			}
			{ Fl_Group* o = new Fl_Group(174, 78, 134, 51, "Search");
#if (FL_MAJOR_VERSION > 1)
				o->label_size(12);
				o->box(FL_EMBOSSED_BOX);
				o->clear_flag(FL_ALIGN_MASK);
				o->set_flag(FL_ALIGN_TOP | FL_ALIGN_LEFT);
#else
				o->labelsize(12);
				o->box(FL_EMBOSSED_FRAME);
				o->align(FL_ALIGN_TOP_LEFT);
#endif
				{ Fl_Round_Button* o = dlgdata->Pointers.SearchCursor = new Fl_Round_Button(176, 81, 92, 25, "From c&ursor");
					o->type(102);
#if (FL_MAJOR_VERSION > 1)
					o->label_size(12);
					o->glyph_box(FL_ROUND_DOWN_BOX);
					o->clear_flag(FL_ALIGN_MASK);
					o->set_flag(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
#else
					o->labelsize(12);
					o->down_box(FL_ROUND_DOWN_BOX);
					o->align(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
#endif
					if ( dlgdata->Options.SearchCursor )
						o->setonly();
				}
				{ Fl_Round_Button* o = dlgdata->Pointers.SearchDoc = new Fl_Round_Button(176, 102, 115, 25, "Entire &document");
					o->type(102);
#if (FL_MAJOR_VERSION > 1)
					o->label_size(12);
					o->glyph_box(FL_ROUND_DOWN_BOX);
					o->clear_flag(FL_ALIGN_MASK);
					o->set_flag(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
#else
					o->labelsize(12);
					o->down_box(FL_ROUND_DOWN_BOX);
					o->align(FL_ALIGN_RIGHT|FL_ALIGN_INSIDE);
#endif
					if ( dlgdata->Options.SearchDoc )
						o->setonly();
				}
				o->end();
			}
			o->user_data(dlgdata);
			o->set_modal();
			o->end();
		}
	}
	return w;
}

FL_EXPORT bool Fl_FancyMultiEditor::FindNext()
{
	bool foundone = false;
	damage(DAMAGE_PARTIAL);			// redraw line after search
	marking = selectlines = false;		// turn off marking for search
	if ( findopts.SearchFor.Length() )
	{
		engine->Command(MOVE_RIGHT, 1);
		engine->Command(SET_IGNORE_CASE, !findopts.MatchCase);
		engine->Command(SET_SEARCHWHOLE, findopts.MatchWord);
		engine->Command(FIND, findopts.SearchFor.Get());
		foundone = engine->LastFind();
		if ( foundone )
		{
			engine->Command(CLEAR_MARKS);
			engine->Command(MARK_BEGIN);
			Mark.Row = engine->CursorRow();
			Mark.Column  = engine->CursorColumn();
			marking = true;
			// engine positions to end of text, so move selection back to start of it.
			engine->Command(MOVE_LEFT, findopts.SearchFor.Length());
			engine->Command(MARK_END);
			this->CopySelection(FALSE);
			SyncDisplay();
		}
		else
			engine->Command(MOVE_LEFT, 1);	// if nothing found, put cursor back where it was.
	}
	// update position indicator
	snprintf(PositionString, sizeof(PositionString), "R %ld C %ld", engine->CursorRow() + 1, engine->CursorColumn() + 1);
	lights->status(PositionString);
	return(foundone);
}

FL_EXPORT bool Fl_FancyMultiEditor::Find()
{
	bool foundone = false;
	Fl_Window *win = MakeFindDialog();
	if ( win )
	{
		fleditor_FindDialogData *dlgdata = (fleditor_FindDialogData *)win->user_data();
		win->show();        
		while ( win->visible() )
			Fl::wait(0.5);
		if ( dlgdata )
		{
			if ( !dlgdata->Cancelled )
			{
				findopts = dlgdata->Options;
				if ( findopts.SearchDoc )  // if search from start, be able to restore position after failed search
				{
					long row = engine->CursorRow();
					long col = engine->CursorColumn();
					engine->Command(MOVE_TOF);
					foundone = FindNext();
					if ( !foundone )
						engine->MoveTo(row, col);
					SyncDisplay();
				}
				else
					foundone = FindNext();
			}
			delete dlgdata;
		}
		delete win;
	}
	return(foundone);
}

FL_EXPORT bool Fl_FancyMultiEditor::Replace()
{
	bool foundone = false;
	Fl_Window *win = MakeReplaceDialog();
	if ( win )
	{
		fleditor_FindDialogData *dlgdata = (fleditor_FindDialogData *)win->user_data();
		win->show();        
		while ( win->visible() )
			Fl::wait(0.5);
		if ( dlgdata )
		{
			if ( !dlgdata->Cancelled )
			{
				findopts = dlgdata->Options;
				long row = engine->CursorRow();
				long col = engine->CursorColumn();
				if ( findopts.SearchDoc )  // if search from start, be able to restore position after failed search
				{
					engine->Command(MOVE_TOF);
					foundone = FindNext();
					if ( !foundone )
						engine->MoveTo(row, col);
					SyncDisplay();
				}
				else
					foundone = FindNext();
				if(!Readonly)
					while ( foundone )
					{
						// replace each one, asking the user first if desired
						bool doreplace = false;
						if ( findopts.PromptReplace )
						{
							SyncDisplay();
							if ( fl_choice("Replace this occurrence?", fl_no, fl_yes, NULL) == 1 )
								doreplace = true;
						}
						else
							doreplace = true;
						if ( doreplace )
						{
							engine->Command(DELETE_REGION);
							engine->Command(INSERT_STRING_MOVE, findopts.ReplaceWith.Get());
						}
						foundone = FindNext();
					}
				SyncDisplay();
			}
			delete dlgdata;
		}
		delete win;
	}
	// update position indicator
	snprintf(PositionString, sizeof(PositionString), "R %ld C %ld", engine->CursorRow() + 1, engine->CursorColumn() + 1);
	lights->status(PositionString);
	return(foundone);
}

FL_EXPORT void Fl_FancyMultiEditor::Session(fledit_SessionInfo *sinf)
{
	Fl_MultiEditor::Session(sinf);
	snprintf(PositionString, sizeof(PositionString), "R %ld C %ld", engine->CursorRow() + 1, engine->CursorColumn() + 1);
	lights->status(PositionString);
}

