#include <rcl/library/strings.h>

/*
$doc Extract()
	Syntax: 	substring = Extract(string,position,length,buffer);

	Inputs:     char *string;           string to extract from.
				long position;           the position to start at.
				long length; 			how many characters to extract.
				char *buffer;			where to put it.

	Synopsis:   Creates a string consisting of the portion of "string"
				beginning at "position", and continuing for "length"
				characters.

	Returns:	Pointer to buffer.

	Notes:		None.
$end
*/

RCLDLL char * Extract(const char *string, long position, long length, char *s)
{
	long count;

	s[0] = '\0';
	if((signed)strlen(string) < position)
		return(s);
	for(count=0;count<length;count++)
		if(*(string+position+count))
			s[count]=*(string+position+count);
		else
			break;
	s[count]='\0';
	return (s);
}
