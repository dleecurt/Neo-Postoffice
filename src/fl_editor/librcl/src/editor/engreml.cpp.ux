#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::RemoveLine(long line)
{
  if(LineCount && !_ReadOnly)
  {
    long row = Cursor.Row;
    long col = Cursor.Column;
    if(MoveCursor(MOVE_ABSOLUTE_ROW,line) == NOERROR)
    {
      LastResult = RemoveLine();
      if(row > Cursor.Row)
        row--;
      MoveCursor(MOVE_ABSOLUTE_ROW,row);
      MoveCursor(MOVE_ABSOLUTE_COLUMN,col);
      ConstrainCursor();
    }
  }
  return LastResult;
}

rclError _export EditorEngine::RemoveLine()
{
	if(_ReadOnly)
		return NOERROR;
  LastResult = NOERROR;
  if(LineCount > 1)
  {
    HasChanged = TRUE;
    long count;
    wString *s = Lines[Cursor.Row];          // remember string but don't delete till realloc OK
    wString **newlines = (wString **)calloc(1,(size_t)(sizeof(wString *)*(LineCount-1)));
    if(newlines)
    {
      if(Lines)       // there better be some lines if it got to here!
      {
        for(count=Cursor.Row;count<LineCount-1;count++)
          Lines[count] = Lines[count+1];
        memcpy(newlines,Lines,(size_t)(sizeof(wString *)*(LineCount-1)));
        free(Lines);
      }
      LineCount--;
      Lines = newlines;
      if(s)                   // alloc was OK, so we can delete this now.
        delete s;           // (if alloc fails, this string needs to remain in place...)
    }
    else
      LastResult = NOMEMORY;
    if(!(Flags & EF_FREEFORM))
      ConstrainCursor();
  }
  else
  {
    MoveCursor(MOVE_ABSOLUTE_COLUMN, 0);
    DeleteToEOL();
  }
  return LastResult;
}

