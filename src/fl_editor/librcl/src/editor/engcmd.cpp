#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::Command(EditorCommandTypes command, long argument)
{
  LastResult = NOERROR;
  switch(command)
  {
    default:            // string commands are handled below
      break;
    case INSERT_CHARACTER:
		last_wrapped = false;
      LastResult = InsertCharacter((char)argument);
      break;
    case DELETE_CHARACTER:
		last_wrapped = false;
      LastResult = DeleteCharacter();
      break;
    case BACKSPACE_CHARACTER:
		last_wrapped = false;
      LastResult = BackSpaceCharacter();
      break;
    case REPLACE_CHARACTER:
      LastResult = ReplaceCharacter((char)argument);
      break;
    case MOVE_LEFT:
    case MOVE_RIGHT:
    case MOVE_DOWN:
    case MOVE_UP:
    case MOVE_SOL:
    case MOVE_EOL:
    case MOVE_TOF:
    case MOVE_BOF:
    case MOVE_LEFT_WORD:
    case MOVE_RIGHT_WORD:
    case MOVE_ABSOLUTE_ROW:
    case MOVE_ABSOLUTE_COLUMN:
      LastResult = MoveCursor(command,argument);
      break;
    case CLEAR_ALL:
		last_wrapped = false;
      Clear();
      break;
    case MARK_BEGIN:        // begin marking block, set start and end to current position
      BlockStart.Row = BlockEnd.Row = Cursor.Row;
      BlockStart.Column = BlockEnd.Column = Cursor.Column;
      break;
    case MARK_END:
      BlockEnd.Row = Cursor.Row;
      BlockEnd.Column = Cursor.Column;
      break;
    case CLEAR_MARKS:
      BlockStart.Row = 0;
      BlockStart.Column = 0;
      BlockEnd.Row = 0;
      BlockEnd.Column = 0;
      break;
	  case DELETE_REGION:
		  last_wrapped = false;
		  DeleteRegion();
		  break;
    case CUT_REGION:
		last_wrapped = false;
      CopyRegion(TRUE);
      break;
    case COPY_REGION:
      CopyRegion(FALSE);
      break;
    case PASTE_REGION:
		last_wrapped = false;
      PasteRegion(FALSE);
      break;
    case REPLACE_REGION:
		last_wrapped = false;
      PasteRegion(TRUE);
      break;
	case SET_IGNORE_CASE:
	  if(argument)
		Flags |= EF_IGNORECASE;
	  else
		Flags &= (~EF_IGNORECASE);
      break;
	  case SET_SEARCHWHOLE:
		if(argument)
		  Flags |= EF_SEARCHWHOLE;
		else
		  Flags &= (~EF_SEARCHWHOLE);
		break;
    case INDENT:
      if(TabStops && TabStops[CurrentIndent+1])
        CurrentIndent++;
      else
        LastResult = RANGE_ERROR;
      HasChanged = TRUE;
      break;
    case OUTDENT:
      if(CurrentIndent > 0)
        CurrentIndent--;
      else
        LastResult = RANGE_ERROR;
      HasChanged = TRUE;
      break;
    case PARAGRAPH:
      HasChanged = TRUE;
      break;
    case REFORMAT:
      LastResult = ReformatDocument((int)argument);
      break;
    case APPEND_CLIPBOARD:
		last_wrapped = false;
      LastResult = AppendClip();
      break;
  }
  return LastResult;
}

rclError _export EditorEngine::Command(EditorCommandTypes command, const char *argument)
{
  LastResult = NOERROR;
  switch(command)
  {
    default:            // numeric commands are handled above
      break;
    case SET_REPLACEMENT_TEXT:
      ReplacementText = argument;
      break;
    case FIND:
      FindPhrase(argument,FALSE);
      break;
    case FIND_REPLACE:
		last_wrapped = false;
      FindPhrase(argument,TRUE);
      break;
    case INSERT_STRING:
    case INSERT_STRING_MOVE:
		last_wrapped = false;
      if(!argument[0] || _ReadOnly)
        break;  // read only or string is empty?  Nothing to do...
      if(LineCount)
      {
        Lines[Cursor.Row]->InsertAt((int)Cursor.Column,argument);
        CharCount += strlen(argument);
        ReformatParagraph();
      }
      else
        InsertLine(wString(argument));
      if(command == INSERT_STRING_MOVE)
        MoveCursor(MOVE_RIGHT, strlen(argument));
      break;
  }
  return LastResult;
}


