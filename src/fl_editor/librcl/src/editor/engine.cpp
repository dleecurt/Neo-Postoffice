#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

//
//  EditorEngine::EditorEngine(short editwidth, bool readonly=FALSE, unsigned long flags=0,long maxch=0,long maxlin=0, char *clipname=NULL)
//
//  This constructor just sets up an editor engine.  The only required parameter
//  is editwidth, which sets how wide the editor's lines should be.  This might be 
//  characters, pixels, inches, or whatever else you want, depending on how you 
//  define the MeasureTextWidth() function.  By default, it is characters.
//

_export EditorEngine::EditorEngine(short editwidth, bool readonly, unsigned long flags,long maxch,long maxlin)
{
	CharCount = LineCount = 0;
	Lines = NULL;

	Clear();
	EditStatus = LastResult = NOERROR;
	LastFindStatus = FALSE;
	last_wrapped = false;
    Width = editwidth;
    if(Width > 32000)
		Width = 32000;  // wString objects have signed integer indeces...
    Flags = flags;
    MaxChars = maxch;
    MaxLines = maxlin;
    _ReadOnly = readonly;
    TabStops = NULL;
    HasChanged = FALSE;
    BlockStart.Row = BlockEnd.Row = BlockStart.Column = BlockEnd.Column = 0;
}


//
//  EditorEngine::~EditorEngine()
//
//  Destroys the editor engine, freeing up all associated memory.  If
//  you don't save the document before this, it's gone.
//

_export EditorEngine::~EditorEngine()
{
	long count;
    
    if(TabStops)
		delete[] TabStops;
    for (count=0; count < LineCount; count++)
		delete Lines[count];        // if any lines allocated, free them
    if(Lines)
		free(Lines);    // free the line array itself
}


wString _export EditorEngine::GetLine(long whichline) const
{
	const char *what = "";
    
    if(whichline >= 0 && whichline < LineCount)
		what = (*Lines[whichline]).Get();
	return wString(what);
}

rclError EditorEngine::ReplaceLineAt(long line, wString& newstr)
{
	if(_ReadOnly)
		return LIMITS_FULL_ERROR;
  rclError err = NOERROR;
  if(line >= 0 && line < LineCount)
    (*Lines[line]) = newstr;
  else
    err = RANGE_ERROR;
  return(err);
}

bool PosInRange(const EditPosition& pos, const EditPosition& from, const EditPosition& to)
{
	// report whether a position is in the given range. ASSUMES RANGE IS ALREADY SORTED
	bool inrange = false;
	// if between start and end rows, it's in range regardless of column
	if((pos.Row > from.Row) && (pos.Row < to.Row))
		inrange = true;
    // else if on start row, is in range if column after start column
	else if((pos.Row == from.Row)	&& (pos.Column >= from.Column))
		inrange = true;
	// else if on end row, is in range if column before end column
	else if((pos.Row == to.Row)	&& (pos.Column <= to.Column))
		inrange = true;
	return inrange;
}


