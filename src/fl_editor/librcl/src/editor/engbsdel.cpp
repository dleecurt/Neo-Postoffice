#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::BackSpaceCharacter()
{
	if(_ReadOnly)
		return LIMITS_FULL_ERROR;
	last_wrapped = false;
  LastResult = NOERROR;
  if(LineCount && (Cursor.Row || Cursor.Column))  // if empty or at TOF, do nothing
  {
    HasChanged = TRUE;
    if((Cursor.Row > 0) && (Lines[Cursor.Row]->Length() == 0))
    {       // if just emptied this line, toss it.
      bool moveup = TRUE;
      if(Cursor.Row == LineCount - 1)
        moveup = FALSE;
      RemoveLine();
      if(moveup)
        Cursor.Row--;
      MoveCursor(MOVE_EOL,1);
      if(Lines[Cursor.Row]->LastCharacter() == HARD_RETURN)
        ;       // if backspaced "under" a hard return, stay put and delete it
      else        // otherwise, move left one character before deleting
        MoveCursor(MOVE_LEFT,1);
    }
    else
      MoveCursor(MOVE_LEFT,1);
    if(LastResult == NOERROR)
		DeleteCharacter();
    // don't need to decrement character count, since DeleteCharacter() does that
  }
  else
    LastResult = RANGE_ERROR;
  return LastResult;
}

rclError _export EditorEngine::DeleteCharacter()
{
	if(_ReadOnly)
		return LIMITS_FULL_ERROR;
	last_wrapped = false;
  long oldcol = Cursor.Column;
  long oldrow = Cursor.Row;
  LastResult = NOERROR;
  if (CharCount)
  {
    HasChanged = TRUE;
    if (Cursor.Row < LineCount - 1)
      if ((Lines[Cursor.Row]->Get()[Cursor.Column] == HARD_RETURN) ||
          (Cursor.Column == Lines[Cursor.Row]->Length()))
      {               // delete pressed at EOL, join lines, delete character
        *Lines[Cursor.Row] += *Lines[Cursor.Row + 1];
        if(MoveTo(Cursor.Row+1,0) == NOERROR)
          RemoveLine();
        MoveTo(oldrow, oldcol);
      }
    Lines[Cursor.Row]->DeleteAt((int)Cursor.Column);
    CharCount--;
    // the rest of a UTF-8 character goes too (Neo-PostOffice)
    while (Cursor.Column < Lines[Cursor.Row]->Length() &&
           ((unsigned char) Lines[Cursor.Row]->Get()[Cursor.Column] & 0xC0) == 0x80)
    {
      Lines[Cursor.Row]->DeleteAt((int)Cursor.Column);
      CharCount--;
    }
    CheckLineFit();
    LastResult = ReformatParagraph();
  }
  else
    LastResult = RANGE_ERROR;
  if(!CharCount || !LineCount)
    Clear();
  return LastResult;
}
