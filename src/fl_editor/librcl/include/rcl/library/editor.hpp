//
//

#ifndef EDITCLASS_H

#define EDITCLASS_H 1

#include <rcl/library/vartypes.h>
#include <rcl/library/strings.hpp>
#include <stdio.h>

//
//  Here are the commands that the editor engine understands.  Most
//  of these take no arguments at all, some take a single character,
//  some take a pointer to a string, etc.

enum EditorCommandTypes
{
  INSERT_CHARACTER,       // insert argument character at cursor
  DELETE_CHARACTER,       // delete character under cursor
  BACKSPACE_CHARACTER,    // backspace one character
  MOVE_LEFT,              // cursor movement
  MOVE_RIGHT,
  MOVE_UP,
  MOVE_DOWN,
  MOVE_SOL,               // move to start of line
  MOVE_EOL,               // move to end of line
  MOVE_TOF,               // move to start of document
  MOVE_BOF,               // move to bottom of document
  MOVE_LEFT_WORD,         // move by words
  MOVE_RIGHT_WORD,
  MOVE_ABSOLUTE_ROW,      // move to absolute line, column
  MOVE_ABSOLUTE_COLUMN,
  CLEAR_ALL,              // wipe out buffer
  MARK_BEGIN,             // mark beginning of region
  CLEAR_MARKS,            // remove region marks
  MARK_END,               // mark end of region
  DELETE_REGION,		  // delete region (NOT to clipboard)
  CUT_REGION,             // cut marked region out of document (to clipboard)
  COPY_REGION,            // copy region into document
  PASTE_REGION,           // paste clipboard into document at cursor
  REPLACE_REGION,         // replace marked region with clipboard
  SET_IGNORE_CASE,        // sets ignore case flag true/false
  SET_REPLACEMENT_TEXT,   // set text to use in search/replace
  FIND,                   // search/replace operations
  FIND_REPLACE,           //
  INDENT,                 // indent to next tab stop
  OUTDENT,                // outdent one position
  PARAGRAPH,              // start new paragraph
  REFORMAT,               // reformat text to different width
  APPEND_CLIPBOARD,       // append clipboard contents to end of document
  INSERT_STRING,          // insert a string of characters at cursor
  REPLACE_CHARACTER,      // overstrike character under cursor
  INSERT_STRING_MOVE,      // insert string of characters, move cursor to end of it...
  SET_SEARCHWHOLE	 		// search whole words only
};

struct EditPosition
{
  long Row;
  long Column;
};

//
//  This is the low-level editor engine.  It doesn't concern itself with
//  what a character looks like, where you are on the screen, etc.  It just knows
//  about the document in the edit buffer.  This CAN be used without
//  a display -- just feed it the proper commands and it can be driven to
//  do whatever you would do with the keyboard.
//


RCLDLL void SortPositions(EditPosition& first, EditPosition& last);


class CLASSEXPORT EditorEngine
{
protected:
  wString ReplacementText;                 // string to use in replace operations
  rclError EditStatus;                // consider this a warn/fatal type thing
  rclError LastResult;                // while this is most recent action's return code
  struct EditPosition Cursor;             // current editor cursor position
  struct EditPosition BlockStart;         // start of marked block of text
  struct EditPosition BlockEnd;           // end of marked block of text
  short CurrentIndent;                      // current indent position
  short Width;                              // how wide the lines can be before wordwrap
  short *TabStops;                          // tab stop settings, if any
  unsigned long Flags;                            // various edit flags (see below)
  long  MaxChars;                         // maximum number of characters to accept
  long  MaxLines;                         // maximum number of lines to accept
  long  CharCount;                        // current number of characters in the buffer
  long  LineCount;                        // current number of lines in the buffer
  bool HasChanged;                        // TRUE if text has been changed
  bool _ReadOnly;                          // TRUE if text has been flagged as Read only
  bool LastFindStatus;				// whether most recent search found a match or not.
  wString **Lines;                         // dynamic array of pointers to string objects
  bool last_wrapped;				// flag for whether last action caused a line wrap or reformat
protected:
  // make sure region start, end are in proper order
  void SortRegion()   { SortPositions(BlockStart, BlockEnd); }
  virtual void CheckLineFit();
  virtual short MeasureTextWidth(const char *t);
  virtual short MeasureTextHeight(const char *t);
public:
  EditorEngine(short editwidth,bool readonly=FALSE, unsigned long flags=0,long maxch=0,long maxlin=0);
  virtual ~EditorEngine();
  rclError Status() const         { return EditStatus; }
  rclError LastError() const      { return LastResult; }
  wString GetLine(long whichline) const ;
  long GetLineCount() const            { return LineCount; }
  long CursorRow() const               { return Cursor.Row; }
  long CursorColumn() const            { return Cursor.Column; }
  rclError Command(EditorCommandTypes command, long argument=0);
  rclError Command(EditorCommandTypes command, const char *argument);
  rclError MoveTo(long row, long col);
  rclError SetTabs(short *tabs);
  rclError ChangeWidth(short newwidth)      { Width = newwidth; return Command(REFORMAT,(long)newwidth); };
  rclError LoadFrom(const char *buffer);
  rclError SaveTo(char *buffer);
  rclError LoadFrom(FILE *infile);
  rclError SaveTo(FILE *outfile);
  rclError InsertCharacter(char character);
  rclError ReplaceCharacter(char character);
  rclError DeleteCharacter();
  rclError BackSpaceCharacter();
  rclError DeleteToEOL();
  rclError MoveCursor(EditorCommandTypes Command, long argument);
  rclError Clear();
  void         IdentifyRegion(EditPosition& start, EditPosition& end);
  void         ConstrainCursor();
  rclError CopyRegion(bool cut);
  rclError CopyRegionTo(char *copybuffer); // DOES NOT CHECK BUFFER SIZE!
  rclError DeleteRegion();
  rclError PasteRegion(bool replace);
  rclError AppendClip();
  rclError FindPhrase(const char *word, bool replace, bool replaceall=0);
  rclError ReformatDocument(short wide);
  virtual rclError ReformatParagraph(bool fromfirst=FALSE, bool all=FALSE);   // reformat paragraph from cursor forward
  rclError RemoveLine();          // remove line from document, free its memory
  rclError RemoveLine(long targetline);       // remove line from document, free its memory
  rclError InsertLine(long atline=-1,bool AsLast=FALSE);  // insert an empty line into the document IN FRONT OF the current one, or after last one
  rclError InsertLine(const wString& str,long atline=-1,bool AsLast=FALSE);  // insert a line into the document IN FRONT OF the current one, or after last one
  unsigned long GetFlags() const        { return Flags; }
  void SetFlags(unsigned long newflags)  { Flags = newflags; }
  void FlushClipBoard();
  void CursorPosition(long& row, long& col) const;
  bool& Changed() 		            { return HasChanged; }
  long GetCharacterCount() const      { return CharCount; }
  char GetCharacterAt(long row, long col) const   { return Lines[row]->operator[]((int)col); };
  rclError ReplaceLineAt(long line, wString& newstr);
  int FindSelectPos(long line, long *start, long *end);
  long GetSelectionSize();		//return char count in selection, if any
  bool LastFind()	{ return LastFindStatus; };
  const EditPosition& GetPosition() const    { return Cursor; }
  bool ReadOnly()		{ return _ReadOnly; };
  void ReadOnly(bool state)	{ _ReadOnly = state; }
  // next line is useful for peeking into a line without making a copy of it as GetLine() does.
  const wString *Line(long whichline) const {  return (whichline >= 0 && whichline < LineCount) ? Lines[whichline] : NULL;  } ;
  bool wrapped()	{ return last_wrapped; };
  bool CanUnwrap(long lineno);  // returns TRUE if at least the first word of line lineno will fit on  line lineno-1
};

enum NewEditorFlags
{
  EF_WARNINGS     = (1<<0),   /* show warnings on limits, etc. */
  EF_FORCELIMITS  = (1<<1),   /* forcibly impose limits (slows editor) */
  EF_LINELIMITS   = (1<<2),   /* use line limits? */
  EF_BYTELIMITS   = (1<<3),   /* use byte limits? */
  EF_IGNORECASE   = (1<<4),   /* ignore case in comparisons if TRUE */
  EF_FREEFORM     = (1<<5),   /* allow freeform cursor movement */
  EF_SEARCHWHOLE = (1<<6),	// search whole words only
  EF_NOWRAP = (1<<7)					// word wrap is disabled, typing breaks line at margin and continues
};

enum EditBlockActions
{                       // functions available on a marked block
  BLOCK_NOTHING,
  BLOCK_MOVE,
  BLOCK_COPY,
  BLOCK_DELETE,
  BLOCK_APPEND,
};


inline void EditorEngine::CursorPosition(long& row, long& col) const
{
  row = Cursor.Row;
  col = Cursor.Column;
}

inline rclError EditorEngine::MoveTo(long row, long col)
{
  Command(MOVE_ABSOLUTE_ROW,row);
  Command(MOVE_ABSOLUTE_COLUMN,col);
  return(LastResult);
}


inline void EditorEngine::IdentifyRegion(EditPosition& start, EditPosition&end)
{
  start.Row = BlockStart.Row;
  start.Column = BlockStart.Column;
  end.Row = BlockEnd.Row;
  end.Column = BlockEnd.Column;
}


#endif
