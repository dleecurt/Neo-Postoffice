#include <rcl/library/strings.h>

static char * far FillJustifyString(const char *string, long size,char *buffer);

/*
$doc Justify()
	Syntax: 	string = Justify(string,size,position,buffer);

	Inputs:     char *string;               character string to be positioned.
				short size;                   size of field to put it into.
				JUSTIFY_POSITION position;	where to put it.
				char *buffer;				buffer to put it in.

	Synopsis:	Sets the string into the buffer at the desired position,
				(LEFT, CENTER, or RIGHT), padding the remainder with spaces.

	Returns:	Returns a pointer to the buffer.

	Notes:		None.
$end
*/

RCLDLL char *	Justify(const char *string, long size,JUSTIFY_POSITION position, char *buffer)
{
	long start=0;

	if(position == RCLJ_FULL)
		FillJustifyString(string,size,buffer);
	else
		if(strlen(string)>(size_t)size)
			{
				strncpy(buffer,string,size);
				buffer[size] = '\0';
			}
		else
			{
				if(position == RCLJ_RIGHT)
					start=size-strlen(string);
				else if (position == RCLJ_CENTER)
					start=(int)(size-strlen(string))/2;
				if(start<0 || start>size)
					start=0;
				memset(buffer,' ',start);
				buffer[start] = '\0';
				strcat(buffer,string);
				RepeatCharacter(' ',size-start-strlen(string),&buffer[strlen(buffer)]);
			}
	return (buffer);
}

RCLDLL long WordCount(const char *string)
{
	short count = 0;
	bool InWord = FALSE;
	long words = 0;

	while(string[count])
		{
			if(isspace(string[count]))
				{
					if(InWord)
						{
							words++;
							InWord = FALSE;
						}
				}
			else
				InWord = TRUE;
			count++;
		}
	return(words);
}

char * FillJustifyString(const char *string, long size,char *buffer)
{
	long spaces,perword, count, remains;
	long words;
	const char *fromstring;
	char *tobuff;

	spaces = size - strlen(string) - 1;
	if(spaces > 0)
		{
			words = WordCount(string) - 1;	/* find number of spaces between words */
			perword = (int)(spaces/words);	/* not the word count. */
			remains = (int)(spaces%words);
			fromstring = string;
			tobuff = buffer;
			while(*fromstring)
				if(isspace(*fromstring))
					{
						*tobuff++ = *fromstring++;			/* copy existing space */
						for(count=0;count<perword;count++) /* insert spaces */
							*tobuff++ = ' ';
						if(remains)	/* odd line size, pad one extra */
							{
								*tobuff++ = ' ';
								remains--;
							}
						while(isspace(*fromstring)) 	/* skip to next word */
							fromstring++;
					}
				else
					while(!isspace(*fromstring))
						*tobuff++ = *fromstring++;
		}
	else
		strcpy(buffer,string);
	return(buffer);
}
