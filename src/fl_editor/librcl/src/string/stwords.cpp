
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef WIN32
#include <conio.h>
#endif
#include <rcl/library/strings.h>

/*
$doc ExtractWord()
	Syntax:         word = ExtractWord(string,buffer);

	Inputs:         char *string;           the string you want tokenized.
				char *buffer;           where to put the result.

	Synopsis:   This routine will build a string containing only the portion
				of the specified string up to the first white-space character.

	Returns:        Pointer to the extracted word (buffer).

	Notes:          None.
$end
*/

RCLDLL char * ExtractWord(const char *string, char *buffer)
{
	char *temp;

	buffer[0] = '\0';
	temp = buffer;
	while(isspace(*string))         /* skip spaces till next word */
		string++;
	while(!(isspace(*string)))      /* copy till next space */
		*temp++ = *string++;
	*temp = '\0';
	return(buffer);
}

/*
$doc FindCurrentWord()
	Syntax:         word = FindCurrentWord(string, buffer);

	Inputs:         char *string;           points to a character in a string.
				char *buffer;           where to put the word.

	Synopsis:   This routine is used when you want to find out what word you
				are in the middle of.  So if you have a string that contains
				"My dog has fleas." and you pass a pointer to the 'e', you
				will be returned "fleas."  This routine will search backward
				for the first space and then extract the first word from there.

	Returns:        Pointer to the current word (buffer).

	Notes:          None.
$end
*/

RCLDLL char * FindCurrentWord(const char *string, char *buffer)
{
	while(!(isspace(*(string-1))))
		string--;
	return(ExtractWord(string,buffer));
}

/*
$doc SplitLine()
	Syntax:         splitat = SplitLine(buffer,limit);

	Inputs:         char *buffer;           line to split
				short limit;                      maximum length to allow

	Synopsis:       This routine will split a line of text, allowing it
				to be no more than limit characters long.  It will
				not break the string in the middle of a word.  The
				split happens by inserting a '\0' in the middle of
				the string at the point where the split should occur.
				That is, after a call as specified above, buffer would
				contain the first part of the split, and &buffer[limit+1]
				would contain the rest of the string.

	Returns:        Point at which the line was split, or zero if the
				line was short enough not to need splitting.

	Notes:          None.
$end
*/

RCLDLL short SplitLine(char *buffer,short limit)
{
	short count;

	if((short)strlen(buffer) > limit)              // split line for table
		{
			for(count=limit;count>0;count--)
				if(isspace(buffer[count]))
					break;
			if(count == 0)
				count = limit;
			memmove(&buffer[count+1],&buffer[count],strlen(&buffer[count])+1);
			buffer[count] = '\0';
		}
	else
		count = 0;              // indicates no split was necessary
	return(count);
}

RCLDLL char ** ParseCommandLine(char *line, int *argc)
{
 	char buffer[256];
	int words = 1;

	if((!line) || (!line[0]))
    	return(NULL);

    char *temp = strchr(line, ' ') + 1;
    while(temp)
    	{
        	words++;
    		temp = strchr(temp, ' ');
            if(temp)
            	temp++;
        }

	char **argv = (char **)malloc(sizeof(char *) * (words + 1));
	*argc = 0;

    ExtractWord(line, buffer);
    while(buffer[0])
    	{
			argv[*argc] = (char *)malloc(strlen(buffer)+1);
            strcpy(argv[*argc], buffer);
            line += strlen(buffer);
            while(*line == ' ')
            	line++;
            (*argc)++;
            ExtractWord(line, buffer);
        }
    return(argv);
}

RCLDLL void ReleaseCommandLine(char **argv)
{
	int count=0;
    if(argv)
    	{
	    	while(argv[count])
    			free(argv[count++]);
	    	free(argv);
        }
}
