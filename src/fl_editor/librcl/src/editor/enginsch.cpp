#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::ReplaceCharacter(char character)
{
	last_wrapped = false;
	if(_ReadOnly)
	{
		LastResult = LIMITS_FULL_ERROR;
	}
	else
	{
		LastResult = NOERROR;
		bool insert = FALSE;
		if(LineCount)
			{
				if(character == HARD_RETURN)
					insert = TRUE;
				else if(Lines[Cursor.Row]->Get()[Cursor.Column] == HARD_RETURN)
					insert = TRUE;
				else if(Lines[Cursor.Row]->Get()[Cursor.Column] == '\0')
					insert = TRUE;
				else if(Lines[Cursor.Row]->Length() < Cursor.Column)
					insert = TRUE;
				else
					{
						Lines[Cursor.Row]->DeleteAt((int)Cursor.Column,1);
						Lines[Cursor.Row]->InsertAt((int)Cursor.Column, character);
						MoveCursor(MOVE_RIGHT,1);
					}
			}
		else
			insert = TRUE;
		if(insert)
			LastResult = InsertCharacter(character);
		HasChanged = TRUE;
	}
	return LastResult;
}

rclError _export EditorEngine::InsertCharacter(char character)
{
	last_wrapped = false;
	if(_ReadOnly)
		LastResult = LIMITS_FULL_ERROR;
	else
	{
		LastResult = NOERROR;
		if(CharCount && (Flags & EF_BYTELIMITS))
			if(CharCount >= MaxChars)
				LastResult = LIMITS_FULL_ERROR;
		if(LastResult == NOERROR)
			{
				HasChanged = TRUE;
				if(!CharCount)
					LastResult = InsertLine();
				if(Flags & EF_FREEFORM)
					{
						if(Cursor.Column > Lines[Cursor.Row]->Length())
							{
															wString s(' ', (int)(Cursor.Column - Lines[Cursor.Row]->Length()));
								if(Lines[Cursor.Row]->LastCharacter() == HARD_RETURN)
									Lines[Cursor.Row]->InsertAt(Lines[Cursor.Row]->Length()-1,s);
								else
									*Lines[Cursor.Row] += s;
							}
					}
				long lastlen = Lines[Cursor.Row]->Length();
				Lines[Cursor.Row]->InsertAt((int)Cursor.Column,character);
				if(lastlen < Lines[Cursor.Row]->Length())
					CharCount++;
				if(character == HARD_RETURN)        // is user hit return, split line at cursor
					{
											wString s;
						Lines[Cursor.Row]->ChopAt((int)Cursor.Column+1,&s);
						LastResult = InsertLine(s,Cursor.Row+1);
						if(LastResult == NOERROR)
							{
								Cursor.Row++;
								Cursor.Column = 0;
								LastResult = ReformatParagraph();
							}
					}
				else
					{
						MoveCursor(MOVE_RIGHT,1);
						if(isspace(character) && Cursor.Row)
							CheckLineFit();
						short lwide = MeasureTextWidth(Lines[Cursor.Row]->Get());
						if(lwide > Width)
							{
								LastResult = ReformatParagraph();
							}
					}
			}
	}
	return LastResult;
}
