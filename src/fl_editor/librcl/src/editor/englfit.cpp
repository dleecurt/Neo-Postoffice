#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

short _export EditorEngine::MeasureTextWidth(const char *t)
{
    return (strlen(t));		// default is width in characters
}

short _export EditorEngine::MeasureTextHeight(const char *t)
{
    return (1);		// default is just 1 (line)
}

void _export EditorEngine::CheckLineFit()
{
	bool unwrap = FALSE;

	if(CanUnwrap(Cursor.Row))
	{
		// word at start is short enough to fit on end of previous
		long lenprev = Lines[Cursor.Row - 1]->Length();
		long currow = Cursor.Row - 1;
		long curcol = Cursor.Column;
		*Lines[Cursor.Row - 1] += *Lines[Cursor.Row];   // attach entire current line to previous
		RemoveLine();                                   // and remove current
		Cursor.Row = currow;    // move cursor out, let reformat put it right...
		Cursor.Column = curcol + lenprev;   // move to end of line plus length of word wrapped
		ReformatParagraph();
		last_wrapped = true;	// unwrap sets this true too.
	}
}

bool _export EditorEngine::CanUnwrap(long lineno)
{
	bool unwrap = FALSE;

	if(lineno && !isspace(Lines[lineno]->Get()[0]))
		if(Lines[lineno-1]->LastCharacter() != HARD_RETURN)
			{
				short count;
				short maxfit = Width - MeasureTextWidth(Lines[lineno - 1]->Get());
				// this pointer modifies only the contents of the buffer without making any
				// other engine calls, so it's safe to use it like this.
				char *buff = (char *)Lines[lineno]->Get();
                if(MeasureTextWidth(Lines[lineno]->Get()) <= maxfit)
					unwrap = TRUE;
				else
					for(count = 0; count <= Lines[lineno]->Length(); count++)
						if(isspace(buff[count]) || (buff[count] == '\0'))
							{
								char holdit = buff[count];
								buff[count] = '\0';
                                //if(MeasureTextWidth(Lines[Cursor.Row]->Copy(0, count)) <= maxfit)
								if(MeasureTextWidth(buff) <= maxfit)
									unwrap = TRUE;
								buff[count] = holdit;
								break;
							}
			}
	return(unwrap);
}

