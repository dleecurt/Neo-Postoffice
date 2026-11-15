#include <rcl/library/strings.h>

/*
	This routine takes a number string in the form "123456789.12"
	and puts it into destination in the form "123,456,789.12", returning
	a pointer to that destination.
*/

RCLDLL char * CommaDelimitNumber(const char *inbuffer, char *Destination)
{
	const char *inb = inbuffer;
	while(!isdigit(*inb))            /* copy spaces, sign, etc. */
		*Destination++ = *inb++;

	const char *dot = strchr(inb, '.');
	short len = strlen(inb);
	short dotpos = dot ? dot - inb : len;
	short comflag = 4 - (dotpos % 3) ;
	
	while(*inb)
	{
		if(*inb == '.')
		{
			strcpy(Destination, inb);
			Destination += strlen(inb);
			break;
		}
		*Destination++ = *inb++;
		if(!(comflag++ % 3) && *inb && (*inb != '.'))
			*Destination++ = ',';
	}
	*Destination = '\0' ;
	return(Destination);
}

RCLDLL void StripCommas(char *number)
{
	char *temp = strchr(number, ',');
	while(temp)
	{
		strcpy(temp-1, temp);
		temp = strchr(number, ',');
	}
}
