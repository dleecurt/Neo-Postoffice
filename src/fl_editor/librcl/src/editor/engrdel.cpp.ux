#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>

rclError _export EditorEngine::DeleteRegion()
{
	if(_ReadOnly)
		return NOERROR;
	last_wrapped = false;
  LastResult = NOERROR;
  if((BlockStart.Row != BlockEnd.Row) || (BlockStart.Column != BlockEnd.Column))
  {
    SortRegion();
    long crow, srcrow;
    HasChanged = TRUE;
    bool realloclines = FALSE;
    if(BlockStart.Row == BlockEnd.Row)
    {       // deleting from single line?
      Lines[BlockStart.Row]->DeleteAt((int)BlockStart.Column,(int)(BlockEnd.Column-BlockStart.Column));
      if(Cursor.Column > BlockStart.Column && Cursor.Column < BlockEnd.Column)
        Cursor.Column = BlockStart.Column;      
      else if(Cursor.Column > BlockEnd.Column)
        Cursor.Column -= (int)(BlockEnd.Column-BlockStart.Column);
    }
    else if(BlockStart.Column == 0 && BlockEnd.Column == 0)
    {
      for(short drow=(int)BlockStart.Row;drow<BlockEnd.Row;drow++)
      {
        CharCount -= Lines[drow]->Length();
        delete Lines[drow];
      }
      realloclines = TRUE;
      crow = BlockStart.Row;
      srcrow = BlockEnd.Row;
      while(srcrow < LineCount)
        Lines[crow++] = Lines[srcrow++];
      LineCount -= (BlockEnd.Row - BlockStart.Row);
    }
    else
    {
      CharCount -= (int)(Lines[BlockStart.Row]->Length()-BlockStart.Column);
      Lines[BlockStart.Row]->ChopAt((int)BlockStart.Column);
      if(BlockEnd.Column)     // if in column zero, nothing to do...
      {
        CharCount -= BlockEnd.Column;
        Lines[BlockEnd.Row]->DeleteAt(0,(int)BlockEnd.Column);
      }
      *Lines[BlockStart.Row] += *Lines[BlockEnd.Row]; // close split in block
      delete Lines[BlockEnd.Row];
      for(crow=BlockStart.Row+1;crow<BlockEnd.Row;crow++)
      {           // first delete all the lines as necessary
        CharCount -= Lines[crow]->Length();
        delete Lines[crow];
        Lines[crow] = NULL;
      }
      realloclines = TRUE;
      crow = BlockStart.Row+1;
      srcrow = BlockEnd.Row+1;
      while(srcrow < LineCount)
        Lines[crow++] = Lines[srcrow++];
      LineCount -= (BlockEnd.Row - BlockStart.Row);
    }
    if(realloclines)
    {
      if(LineCount > 0)
      {
        wString **newlines = (wString **)calloc(1,(size_t)(sizeof(wString *)*(LineCount)));
        if(newlines)
        {
          if(Lines)
          {
            memcpy(newlines,Lines,(size_t)(sizeof(wString *)*(LineCount)));
            free(Lines);
          }
          Lines = newlines;
        }
      }
      else
        Clear();
      if(Cursor.Row >= BlockStart.Row)    // need to move cursor?
        if(Cursor.Row < BlockEnd.Row || (Cursor.Row == BlockEnd.Row && Cursor.Column <= BlockEnd.Column))
        {           // cursor was inside block?
          MoveTo(BlockStart.Row,BlockStart.Column);
        }
        else
        {           // else past end of block, subtract row, columns as necessary
          Cursor.Row -= (BlockEnd.Row - BlockStart.Row - 1);
          Cursor.Column -= BlockEnd.Column;
        }
    }
    ConstrainCursor();
  }
  else
    LastResult = NO_REGION;
  if(LastResult == NOERROR)
    Command(CLEAR_MARKS);
  if(CharCount < 0)
  {
    CharCount = 0;
  }
  if(LineCount < 0)
  {
    LineCount = CharCount = 0;
  }
  if(CharCount)
  {
    CheckLineFit();
    ReformatParagraph();
  }
  else
    Clear();
  return LastResult;
}


int EditorEngine::FindSelectPos(long line, long *start, long *end)
{	
	EditPosition holdstart = BlockStart;
	EditPosition holdend = BlockEnd;
	int inselection = 0;
	*start = *end = 0;
	if(line > LineCount)
		return(0);
	if(BlockStart.Row + BlockStart.Column + BlockEnd.Row + BlockEnd.Column == 0)
		return(0);
	SortRegion();   	
	inselection = ((line >= BlockStart.Row) && (line <= BlockEnd.Row));
	if(inselection)
	{	// line inside selection area.
		*end = Lines[line]->Length();
		if(line == BlockStart.Row)
			*start = BlockStart.Column;
		if(line == BlockEnd.Row)
			*end = BlockEnd.Column;
	}
	BlockStart = holdstart;
	BlockEnd = holdend;
	return(inselection);
}

long EditorEngine::GetSelectionSize()
{	
	long size=0;
	EditPosition holdstart = BlockStart;
	EditPosition holdend = BlockEnd;
	
	if(BlockStart.Row + BlockStart.Column + BlockEnd.Row + BlockEnd.Column == 0)
		return(0);
	if((BlockStart.Row == BlockEnd.Row) && (BlockStart.Column == BlockEnd.Column))
		return(0);
	SortRegion();   	
	for(long line = BlockStart.Row; line <= BlockEnd.Row; line++)
		size += Lines[line]->Length();
	size -= BlockStart.Column;
	size -= Lines[BlockEnd.Row]->Length() - BlockEnd.Column;
	BlockStart = holdstart;
	BlockEnd = holdend;
	return(size);
}

