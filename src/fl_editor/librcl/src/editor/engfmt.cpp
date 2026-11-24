#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::ReformatParagraph(bool fromfirst,bool all)
{
	long row;
	LastResult = NOERROR;
        wString s;

	last_wrapped = false;
	if(fromfirst)
		row = 0;
	else
		row = Cursor.Row;
	long realcount = CharCount;     // see below
	if(LineCount)
		for(; row < LineCount; row++)
			{
				if(MeasureTextWidth(Lines[row]->Get()) > Width)
					{
						char *temp = (char *)Lines[row]->Get();
						short word = strlen(temp)-1;
                        if(MeasureTextWidth(temp) > Width)
                        {
                            // while line doesn't fit, skip backwards until it does
                            char hold=1;
                            while(temp[word] && hold)  
                            {
                                hold = temp[word];
                                temp[word] = '\0';
                                if(MeasureTextWidth(temp) < Width)
                                {
                                    temp[word] = hold;
                                    hold = 0;
                                }
                                else
                                    temp[word] = hold;
                                word--;
                            }
                        }
						while(isspace(temp[word]))  // if breaking on a space, adjust to next word
							word++;
						// never inside a UTF-8 character (Neo-PostOffice)
						while (word > 0 && ((unsigned char) temp[word] & 0xC0) == 0x80)
							word--;
						// A quoted line is not broken inside its quote marks
						// ("> > "): a word too long for the line (a link) stays
						// on the quoted line (Neo-PostOffice).
						if(temp[0] == '>')
						{
							short pre = 0;
							while(temp[pre] == '>' || temp[pre] == ' ')
								pre++;
							// where WrapAt will break: after the space before word
							short brk = word;
							if(temp[brk] != ' ')
							{
								while(brk && temp[brk] != ' ')
									brk--;
								if(brk)
									brk++;
							}
							if(brk <= pre)
								continue;
						}
						if(Flags & EF_NOWRAP)
							Lines[row]->ChopAt(word, &s);	// split line
						else
							Lines[row]->WrapAt(word,&s);    // break at width or next word (no exact break on space...)
						if(InsertLine(s,row + 1) != NOERROR)
							break;
						if(row == Cursor.Row)   // keep cursor position where it is, even if string splits
							if(Cursor.Column >= Lines[row]->Length()) {
								if(LastResult == NOERROR)
									{
										Cursor.Column -= Lines[row]->Length();
										Cursor.Row++;
									}
								else
									ConstrainCursor();
							}
						last_wrapped = true;
					}
				else if(Lines[row]->LastCharacter() == HARD_RETURN)
					{
						// if doing all, skip this line and go on, else stop here
						if(!all)
							break;
					}
				else if(MeasureTextWidth(Lines[row]->Get()) == Width)
					continue;       // if just right, skip it and go on
				else if(row < (LineCount - 1))
					{               // lines too short?  Join as many as necessary
						if(CanUnwrap(row+1))  // check if next line should unwrap
						{
							// if line can unwrap, keep unwrapping until max length is in the line
							while((row < (LineCount - 1)) && (MeasureTextWidth(Lines[row]->Get()) < Width))
							{
								if(Lines[row]->LastCharacter() == HARD_RETURN)
									break;
								if(row + 1 == Cursor.Row)
									{       // about to move cursor?
										Cursor.Row = row;
										Cursor.Column += Lines[row]->Length();
									}
								*Lines[row] += *Lines[row+1];       // add next line
								long temprow = Cursor.Row;
								long tempcol = Cursor.Column;
								Cursor.Row = row + 1;
								if(RemoveLine() != NOERROR)
									break;
								if(MoveTo(temprow,tempcol) == NOERROR)
									if(Cursor.Row > row)
										Cursor.Row--;
							}
							row--;      // make next pass re-examine this line
						}
					}
			}
	CharCount = realcount;      // splitting lines and then re-inserting them
								// gets them counted twice, so just set the count
								// back like it was when we started
	if(!LineCount || !CharCount)
		Clear();
	return LastResult;
}

rclError _export EditorEngine::ReformatDocument(short wide)
{
	Width = wide;
	LastResult = ReformatParagraph(TRUE,TRUE);
	return LastResult;
}
