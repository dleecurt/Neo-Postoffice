#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

void _export EditorEngine::ConstrainCursor()
{
	if (Cursor.Row > LineCount - 1)
		Cursor.Row = LineCount - 1;
	if(!(Flags & EF_FREEFORM))
		if(Cursor.Column >= (Lines[Cursor.Row]->Length()))
			{
				Cursor.Column = Lines[Cursor.Row]->Length();
				if(Lines[Cursor.Row]->LastCharacter() == HARD_RETURN)
					Cursor.Column = Lines[Cursor.Row]->Length() - 1;
				if(Cursor.Column < 0)
					Cursor.Column = 0;
			}
}


rclError _export EditorEngine::MoveCursor(EditorCommandTypes command, long argument)
{
	short count;
	LastResult = NOERROR;
	switch(command)
		{
			case MOVE_LEFT:
				if(argument == 0)
					argument = 1;
				for(count=0;count<argument;count++)
					{
						if(Cursor.Column)
							Cursor.Column--;
						else if(Cursor.Row)
							{
								Cursor.Row--;
								Cursor.Column = Lines[Cursor.Row]->Length() ? Lines[Cursor.Row]->Length() - 1 : 0;
							}
						else
							LastResult = RANGE_ERROR;
					}
				break;
			case MOVE_RIGHT:
				if(argument == 0)
					argument = 1;
				for(count=0;count<argument;count++)
					{
						if (!(CharCount && LineCount))
							LastResult = RANGE_ERROR;
						else if ((Lines[Cursor.Row]->Get()[Cursor.Column] == HARD_RETURN) && (Cursor.Row < (LineCount-1)))
							{
								Cursor.Row++;
								Cursor.Column = 0;
							}
						else if (Cursor.Column < Lines[Cursor.Row]->Length()-1)
							Cursor.Column++;
						else if (Cursor.Row < (LineCount-1))
							{
								Cursor.Row++;
								Cursor.Column = 0;
							}
						else if((Cursor.Row == (LineCount-1)) && (Cursor.Column < Lines[Cursor.Row]->Length()))
							Cursor.Column++;    // insert at very end of document OK.
						else
							LastResult = RANGE_ERROR;
					}
				break;
			case MOVE_UP:
				if(argument == 0)
					argument = 1;
				for(count=0;count<argument;count++)
					{
						if(Cursor.Row)
							{
								Cursor.Row--;
								ConstrainCursor();
							}
						else
							LastResult = RANGE_ERROR;
					}
				break;
			case MOVE_DOWN:
				if(argument == 0)
					argument = 1;
				for(count=0;count<argument;count++)
					{
						if(Cursor.Row < (LineCount-1))
							{
								Cursor.Row++;
								ConstrainCursor();
							}
						else
							LastResult = RANGE_ERROR;
					}
				break;
			case MOVE_SOL:
				Cursor.Column = 0;
				break;
			case MOVE_EOL:
				if(LineCount)       // any lines at all, even first one?
					{
						Cursor.Column = Lines[Cursor.Row]->Length();
						if(Lines[Cursor.Row]->LastCharacter() == HARD_RETURN)
							Cursor.Column--;
					}
				break;
			case MOVE_TOF:
				Cursor.Row = Cursor.Column = 0;
				break;
			case MOVE_BOF:
				if(LineCount)       // any lines at all, even first one?
					{
						Cursor.Row = LineCount - 1;
						Cursor.Column = Lines[Cursor.Row]->Length();
						if(Cursor.Column && Lines[Cursor.Row]->LastCharacter() == HARD_RETURN)
							Cursor.Column--;
					}
				break;
			case MOVE_RIGHT_WORD:
				if(LineCount)       // any lines at all, even first one?
					{
						long count,row;
						bool foundspace = FALSE;
						bool done = FALSE;
						count = Cursor.Column;      // first line check from cursor
						for(row=Cursor.Row;(row < LineCount) && !done; row++)
							{
								if(count > Lines[row]->Length())
									count = Lines[row]->Length();
								for(;count<Lines[row]->Length();count++)
									if(foundspace && !isspace(Lines[row]->Get()[count]))
										{
											Cursor.Column = count;
											Cursor.Row = row;
											done = TRUE;
											break;
										}
									else if(isspace(Lines[row]->Get()[count]))
										foundspace = TRUE;
								count = 0;      // after first line, check from left edge
							}
					}
				break;
			case MOVE_LEFT_WORD:
				if(Cursor.Row || Cursor.Column)     // if not already at TOF
					{
						MoveCursor(MOVE_LEFT,1);    // look at previous character
						if(isspace(Lines[Cursor.Row]->Get()[Cursor.Column]) || (Lines[Cursor.Row]->Get()[Cursor.Column] == '\0'))
							{                       // if it's a space, skip back to next word
								while(isspace(Lines[Cursor.Row]->Get()[Cursor.Column])  || (Lines[Cursor.Row]->Get()[Cursor.Column] == '\0'))
									if(MoveCursor(MOVE_LEFT,1) != NOERROR)
										break;  // Can't move past top of document or if error...
							}
								// then skip to start of the word
						while(!isspace(Lines[Cursor.Row]->Get()[Cursor.Column]))
							if(MoveCursor(MOVE_LEFT,1) != NOERROR)
								break;
						if(isspace(Lines[Cursor.Row]->Get()[Cursor.Column])  || (Lines[Cursor.Row]->Get()[Cursor.Column] == '\0'))
							MoveCursor(MOVE_RIGHT_WORD,1);
						LastResult = NOERROR;
					}
				break;
			case MOVE_ABSOLUTE_ROW:
				if(argument < LineCount)
					{
						Cursor.Row = (argument >= 0) ? argument : 0;
						ConstrainCursor();
					}
				else
					{
						LastResult = RANGE_ERROR;
						Cursor.Row = (LineCount) ? (LineCount - 1) : 0;
					}
				break;
			case MOVE_ABSOLUTE_COLUMN:
				if((CharCount) && (argument <= Lines[Cursor.Row]->Length()))
					{
						Cursor.Column = argument;
						ConstrainCursor();
					}
				else
					LastResult = RANGE_ERROR;
				break;
		}
	return(LastResult);
}


