#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::FindPhrase(const char *searchword, bool replace, bool replaceall)
{
	long oldrow=0, oldcol=0;
	long row=0;
	char *temp=NULL;
	bool found;
	LastResult = NOERROR;
	wString tryline;
	wString sword(searchword);

	LastFindStatus = FALSE;
	if(Flags & EF_IGNORECASE)
		sword.ToLower();
	const char *word = sword.Get();
 	if(replace && replaceall)
		{
			oldrow = Cursor.Row;
			oldcol = Cursor.Column;
		}
	for(row = Cursor.Row;row < LineCount; row++)
		{
			found = FALSE;
			if(Flags & EF_IGNORECASE)
			{
				// if ignoring case, copy the line, make it and the search string lowercase, and search
				// then move the pointer over onto the real line for the rest of the routine to use.
				
				if(row == Cursor.Row)
					tryline = Lines[row]->Copy(Cursor.Column, Lines[row]->Length());
				else
					tryline = *Lines[row];
				tryline.ToLower();
				temp = strstr((char *)tryline.Get(), word);
				if(Flags & EF_SEARCHWHOLE)
					while(temp)
					{
						// if not a whole word, skip and keep looking in rest of line.
						wString wholeword = tryline.GetWordAt(temp - tryline.Get());
						if(wholeword == word)
							break;
						else
							temp = strstr(temp + 1, word);
					}
				if(temp)
				{
					found = TRUE;	
					// now adjust back to actual line buffer for remainder of routine.
					temp = (char *)(Lines[row]->Get() + (temp - tryline.Get()));
					if(row == Cursor.Row)
						temp += Cursor.Column;
				}
			}
			else
			{
				// save time by searching directly in lines if not ignoring case
				if(row == Cursor.Row)
					{
						if(Cursor.Column < Lines[row]->Length() - 1)
							{
								temp = strstr(((char *)Lines[row]->Get())+Cursor.Column,word);
								if(temp)
									found = TRUE;
							}
					}
				else
					{
						temp = strstr(((char *)Lines[row]->Get()),word);
						if(temp)
							found = TRUE;
					}
			}
			LastFindStatus = found;
			if(LastFindStatus)
				{
					Cursor.Row = row;
					Cursor.Column = temp - (char *)Lines[row]->Get();
					short count;
					if(replace)
						{
							HasChanged = TRUE;
                                                        wString s(*Lines[row]);
                                                        wString t("");
							t = s.GetWordAt((int)Cursor.Column);
							s.DeleteAt((int)Cursor.Column,t.Length());
							s.InsertAt((int)Cursor.Column, ReplacementText.Get());
						}
					for(count=0;count<(int)strlen(word);count++)
						MoveCursor(MOVE_RIGHT,1);
					if(!replaceall)
						break;
				}
		}
	if(replace && replaceall)
		{
			Cursor.Row = oldrow;
			Cursor.Column = oldcol;
		}
	return LastResult;
}

