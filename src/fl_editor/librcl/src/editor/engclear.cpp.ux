#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::Clear()
{
  while(LineCount)
    delete Lines[--LineCount];
  if(Lines)
    free(Lines);
  Lines = NULL;
  Cursor.Row = Cursor.Column = 0;
  BlockStart.Row = BlockStart.Column = 0;
  BlockEnd.Row = BlockEnd.Column = 0;
  CharCount = LineCount = 0;
  HasChanged = TRUE;
  return (LastResult = NOERROR);
}

