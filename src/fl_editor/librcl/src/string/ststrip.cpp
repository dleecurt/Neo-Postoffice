#include <rcl/library/strings.h>

/*
$doc Strip()
	Syntax:     Strip(string);

	Inputs:     char *string;       the string to be stripped.

	Synopsis:   This routine removes leading and trailing whitespace from
				the passed string.  It will also remove any trailing '\n'.

	Returns:	Pointer to string.

	Notes:      None.
$end
*/

RCLDLL char * Strip(char *string)
{
	char *temp;

	temp = string;
	while((*temp > 0 && *temp < 127) && isspace(*temp))
		temp++;
	if(temp>string)
		strcpy(string,temp);
	temp=string+strlen(string);
	while((temp > string) && (*(temp-1) > 0 && *(temp-1) < 127) && isspace(*(temp-1)))
		temp--;
	*temp='\0';
	return (string);
}
