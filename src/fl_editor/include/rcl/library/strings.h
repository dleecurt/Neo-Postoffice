/***************************************************************************/
/*                                                              STRINGS.H                                                                  */
/***************************************************************************/
/*      This file lists the prototypes for the functions in the character          */
/*      string manipulation library.                                                                               */
/***************************************************************************/

#ifndef STRINGS_H

#define STRINGS_H 1

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <rcl/library/vartypes.h>



typedef enum { RCLJ_LEFT, RCLJ_RIGHT, RCLJ_CENTER, RCLJ_FULL } JUSTIFY_POSITION;

RCLDLL char * RepeatCharacter(char character, long repeat_value, char *buffer);
RCLDLL char * Strip(char *string);
RCLDLL char * Justify(const char *string, long size, JUSTIFY_POSITION position, char *buffer);
RCLDLL char * Extract(const char *string, long position, long length, char *buffer);
RCLDLL char * ExtractWord(const char *string, char *buffer);
RCLDLL char * FindCurrentWord(const char *string, char *buffer);
RCLDLL char * CommaDelimitNumber(const char *buffer, char *destination);
RCLDLL short  SplitLine(char *buffer,short limit);
RCLDLL long  WordCount(const char *string);
RCLDLL const char * Soundex(const char *string);
RCLDLL void StripCommas(char *number);
RCLDLL char ** ParseCommandLine(char *line, int *argc);
RCLDLL void  ReleaseCommandLine(char **argv);

#ifndef strlwr
inline void strlwr(char *string)
{
    while(string && *string)
    {
        if(isupper(*string))
            *string = tolower(*string);
        string++;
    }
}
#endif
#ifndef strupr
inline void strupr(char *string)
{
    while(string && *string)
    {
        if(islower(*string))
            *string = toupper(*string);
        string++;
    }
}
#endif

#endif


