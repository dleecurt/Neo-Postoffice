#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::AppendClip()
{
  long row, col;
  
  if(_ReadOnly)
	  LastResult = LIMITS_FULL_ERROR;
  else
  {
	  row = Cursor.Row;
	  col = Cursor.Column;
	  Command(MOVE_BOF,1);
	  Command(PASTE_REGION,1);
	  HasChanged = TRUE;
	  MoveTo(row,col);
  }
  return LastResult;
}

