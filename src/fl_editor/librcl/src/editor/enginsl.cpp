#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>


rclError _export EditorEngine::InsertLine(long atline,bool aslast)
{
	if(_ReadOnly)
		LastResult = LIMITS_FULL_ERROR;
	else
	{
		LastResult = NOMEMORY;      // assume the worst...
		wString s;
		if(s.Capacity())
		  LastResult = InsertLine(s,atline,aslast);
	}
  return LastResult;
}

rclError _export EditorEngine::InsertLine(const wString& str, long atline, bool aslast)
{
	if(_ReadOnly)
		LastResult = LIMITS_FULL_ERROR;
	else
	{
		if(LineCount && (Flags & EF_LINELIMITS))    // if would be past limit, just return error
		  if(LineCount + 1 >= MaxLines)
			return(LastResult = LIMITS_FULL_ERROR);
		if(CharCount && (Flags & EF_BYTELIMITS))
		  if(CharCount + str.Length() >= MaxChars)    // if would be past limit, just return error
			LastResult = LIMITS_FULL_ERROR;
		LastResult = NOMEMORY;      // assume the worst...
		HasChanged = TRUE;
		wString *s = new wString;
		if(s) {
		  if(s->Capacity())       // make sure allocation of buffer went OK
		  {
			(*s) = str;
			wString **newlines = (wString **)calloc(1,(size_t)(sizeof(wString *)*(LineCount+1)));
			if(newlines)
			{
			  long count;
			  if(Lines)
			  {
				memcpy(newlines,Lines,(size_t)(sizeof(wString *)*(LineCount)));
				free(Lines);
			  }
			  Lines = newlines;
			  if(aslast || !CharCount || (atline >= LineCount))
				Lines[LineCount] = s;   // add as last line
			  else
			  {                       // else shift lines down, insert
				if(atline == -1)
				  atline = Cursor.Row;
				for(count=LineCount;count>atline;count--)   // this is right since LineCount hasn't
				  Lines[count] = Lines[count-1];          // been incremented yet...
				Lines[atline] = s;
			  }
			  LineCount++;                // don't forget to count the line
			  CharCount += s->Length();
			  LastResult = NOERROR;
			}
		  }
		  else
			delete s;
		}
	}
  return LastResult;
}

