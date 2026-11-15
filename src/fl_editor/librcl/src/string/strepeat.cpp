#include <rcl/library/vartypes.h>
#include <rcl/library/strings.h>


/*
$doc RepeatCharacter()
	Syntax:     string = RepeatCharacter(character,howmany,destination);

	Inputs:     char character;         character to be repeated.
				long howmany;            how many times to repeat.
				char *destination;      buffer to hold the result.

	Synopsis:   This creates a string consisting of a repeat run of
				"howmany" of "character".  The result is stored in
				"destination".

	Returns:    Pointer to the created character string.

	Notes:      Memory corruption will occur if the buffer is too small
				to hold the desired number of characters.
$end
*/

RCLDLL char *  RepeatCharacter(char character, long repeat_value, char *destination)
{
	destination[0] = '\0';
	if((repeat_value>0) && (repeat_value < 255))
		{
			memset(destination,character,repeat_value);
			destination[repeat_value] = '\0';
		}
	return (destination);
}
