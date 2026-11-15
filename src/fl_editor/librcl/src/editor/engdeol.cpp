#include <rcl/library/vartypes.h>
#include <rcl/library/editor.hpp>
#include <rcl/library/strings.hpp>


rclError _export EditorEngine::DeleteToEOL()
{
	if(LineCount && !_ReadOnly)
		{
			if(Lines[Cursor.Row]->LastCharacter() == HARD_RETURN)
				Lines[Cursor.Row]->DeleteAt((int)Cursor.Column,(int)(Lines[Cursor.Row]->Length() - Cursor.Column - 1));
			else
				Lines[Cursor.Row]->ChopAt((int)Cursor.Column);
			HasChanged = TRUE;
			LastResult = ReformatParagraph();
		}
	return LastResult;
}
