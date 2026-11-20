//
//	This file implements the file I/O routines for the editor engine.
//	These are simple ASCII I/O routines, just for getting text out of
//	a file, or putting it into one.
//

#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError EditorEngine::LoadFrom(FILE *infile)
{
	LastResult = NOERROR;
	bool reallychanged = FALSE; 	// if empty, don't mark as changed
	bool aslast = FALSE;
	long row = Cursor.Row;
	long col = Cursor.Column;
        wString left;            // left and right split of current line
        wString right;   // (if any) for inserting into
        wString s;
	if(CharCount)
		{
			reallychanged = TRUE;
			left = *Lines[Cursor.Row];
		}
	else
		aslast = TRUE;
	if((Cursor.Row >= LineCount-1) || (!CharCount))	// inserting at EOF
		aslast = TRUE;
	left.ChopAt((int)Cursor.Column,&right);	// split current line, insert text
	InsertLine(right, Cursor.Row + 1);		// between two pieces...
	while(!feof(infile))
		{
			short ThisCount = 0;
			s = "";
//			if(row == Cursor.Row)			// first row?  Add left side of split
//				s = left;
			while(!feof(infile))
				{
					signed char sc = fgetc(infile);
					char c = sc;
					if((sc == EOF) && feof(infile))
						break;
	// Linux uses the soft returns as hard returns
					else if(c == SOFT_RETURN)
						c = HARD_RETURN;
					else if(c == '\t')  // TAB
						{
							c = ' ';
							s += "   ";	// add 3 spaces, get fourth one below
							ThisCount += 3;
						}
					ThisCount++;
					s += c;
					if(c == HARD_RETURN || (s.Length() > Width && c == ' '))
						{
							break;		// end of line, so add it.
						}
				}
//			if((row == Cursor.Row) && CharCount) // if first row, copy back to same line
//				{
//					*Lines[Cursor.Row] = s;
//					++row;						// bump to next row...
//				}
//			else
				InsertLine(s,++row,aslast);		// otherwise insert new line
			if(LastResult == NOMEMORY || LastResult == LIMITS_FULL_ERROR)
				{
					//ShowError(NOMEMORY);
					break;
				}
			if(LastResult != NOERROR)
				break;
		}
	ReformatParagraph(TRUE,TRUE);
	MoveTo(row,col);
	HasChanged = reallychanged; 	// if was empty, don't mark as changed
	return LastResult;
}

rclError EditorEngine::SaveTo(FILE *outfile)
{
	long row;
	char softbuffer[10];
	LastResult = NOERROR;
	if(CharCount)		// if nothing in it, don't write anything...
		{
			sprintf(softbuffer,"%c",LINE_FEED);
            wString softreturn(softbuffer);
            wString s;
			LastResult = NOERROR;
			for(row = 0; row < LineCount; row++)
				{
					s = *Lines[row];
					if((s.LastCharacter() != HARD_RETURN) && (s.LastCharacter() != SOFT_RETURN))
						{
							if(s.LastCharacter() == ' ')		// if last character is a space,
								s.DeleteAt(s.Length()-1,1); 	// replace with soft return, else 
							s += softreturn;					// just add it.
						}
					fputs(s.Get(),outfile);
				}
			softbuffer[0] = softbuffer[1] = '\0';
			fwrite(softbuffer,1,2,outfile);
		}
	return(LastResult);
}


rclError EditorEngine::LoadFrom(const char *buffer)
{
	LastResult = NOERROR;
	bool reallychanged = FALSE; 	// if empty, don't mark as changed
	bool aslast = FALSE;
	bool endofbuffer = FALSE;
	long row = Cursor.Row;
	long buffpos = 0;
        wString left;            // left and right split of current line
        wString right;   // (if any) for inserting into
        wString s;
	if(CharCount)
		{
			reallychanged = TRUE;
			left = *Lines[Cursor.Row];
		}
	else
		aslast = TRUE;
	if((Cursor.Row >= LineCount-1) || (!CharCount))	// inserting at EOF
		aslast = TRUE;
	left.ChopAt((int)Cursor.Column,&right);	// split current line, insert text
	while(!endofbuffer)
		{
			short ThisCount = 0;
			s = "";
			if(row == Cursor.Row)			// first row?  Add left side of split
				s = left;
			while(!endofbuffer)
				{
					char c = buffer[buffpos++];
					if(!c)
						{
							endofbuffer = TRUE;
							break;
						}
					if(c == SOFT_RETURN)
						c = ' ';
					s += c;
					ThisCount++;
					if(c == HARD_RETURN)
						{
							break;		// end of line, so add it.
						}
				}
			if((row == Cursor.Row) && CharCount) // if first row, copy back to same line
			{
				*Lines[Cursor.Row] = s;
				row++;
			}
			else
				InsertLine(s,++row,aslast);		// otherwise insert new line
			if(endofbuffer && right.Length())	// if last line, include other side of
				InsertLine(right,++row,aslast);	// original line split
		}
	ReformatParagraph(FALSE,TRUE);
	HasChanged = reallychanged;
	return LastResult;
}

rclError EditorEngine::SaveTo(char *buffer)
{
	long row;
	char softbuffer[10];
	LastResult = NOERROR;

	// Neo-PostOffice: an empty editor gives an empty string, not an
	// untouched buffer (the caller then read past its end).
	if(buffer) buffer[0] = '\0';
	if(CharCount && buffer)
		{
			sprintf(softbuffer,"%c",SOFT_RETURN);
                        wString softreturn(softbuffer);
                        wString s;
			LastResult = NOERROR;
			for(row = 0; row < LineCount; row++)
				{
					s = *Lines[row];
					if((s.LastCharacter() != HARD_RETURN) && (s.LastCharacter() != SOFT_RETURN))
						{
							if(s.LastCharacter() == ' ')		// if last character is a space,
								s.DeleteAt(s.Length()-1,1); 	// replace with soft return, else
							s += softreturn;					// just add it.
						}
					strcat(buffer,s.Get());
					buffer += s.Length();	// so strcat is quicker
				}
		}
	return(LastResult);
}
