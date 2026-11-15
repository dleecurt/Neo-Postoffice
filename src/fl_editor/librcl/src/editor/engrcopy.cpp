#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>


//
//  EditorEngine::CopyRegion(bool cut)
//
//  Copies the currently marked region  into the clipboard.
//  Now this is handled by descendant classes.  This used to use a
//  file-based clipboard, but it was yanked since it's not so useful anymore.
//

rclError _export EditorEngine::CopyRegion(bool cut)
{
	LastResult = NOERROR;
	if(LastResult == NOERROR || LastResult == NO_REGION)
		Command(CLEAR_MARKS);
	return LastResult;
}

rclError _export EditorEngine::CopyRegionTo(char *copybuffer)
{
	long buffpos = 0;
	memset(copybuffer, 0, GetSelectionSize()+1);          
	EditPosition Mark = BlockStart;
	EditPosition endit = Cursor;
	SortPositions(Mark, endit);
	if ( (Mark.Row != endit.Row) || (Mark.Column != endit.Column) )
	{
		wString current = GetLine(Mark.Row);
		if ( endit.Row == Mark.Row )   // if single line in selection
		{
			strncpy(copybuffer, &(current.Get()[Mark.Column]), endit.Column - Mark.Column);
		}
		else
		{
			strcpy(copybuffer, &(current.Get()[Mark.Column]));
			buffpos += strlen(copybuffer);
			for ( long line=Mark.Row+1; line < endit.Row; line++ )
			{
				current = GetLine(line);
				strcpy(copybuffer + buffpos, current.Get());
				buffpos += current.Length();
			}
			current = GetLine(endit.Row);
			strncpy(copybuffer + buffpos, current.Get(), endit.Column);	
		}
	}
	return (NOERROR);
}
