/*****************************************************************************/
/*                                                                           */
/*                    Header File for object modue DATETIME                  */
/*                                                                           */
/*****************************************************************************/

#ifndef DATETIME_H

#define DATETIME_H 1


#include <rcl/library/vartypes.h>
#include <rcl/library/strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

//
//      Determination of leap year:  if year is greater than 1581 (when
//      the Gregorian calendar was adopted by the catholic church and
//      many countries (1752 for most of the rest of the world), then if the
//      year is divisible by four or four hundred, but not by one hundred.
//      (For example, 1900 was not a leap year, 1904 was, and 2000 will be.
//

#define NULL_TDATE           "  /  /  "

/*
	Also note that the day numbers are 1-relative, while the others
	are zero-relative...
*/

typedef struct date_struct { short day; short month; short year; } TDATE;
typedef struct time_struct { short minutes; short hours; } T_TIME;
typedef enum month_numbers { JAN, FEB, MAR, APR, MAY, JUN, JUL, AUG, SEP, OCT, NOV, DEC } MONTHS;
typedef enum day_names { SUNDAY=0, MONDAY, TUESDAY, WEDNESDAY, THURSDAY, FRIDAY, SATURDAY } DAYS;

	RCLDLL extern short _DT_days_per_month[];
	RCLDLL extern short _DT_days_till_month[];
	RCLDLL extern char * _DT_month_names[];
	RCLDLL extern char * _DT_day_names[];
	#define NUM_TOKENS 10
	RCLDLL extern char * _DT_DateNameTokens[NUM_TOKENS][2];

#define ClearDate(date)     ((TDATE)(date)).year=((TDATE)(date)).month=((TDATE)(date)).day=0
#define ClearTime(time)     ((T_TIME)(time)).hours=((T_TIME)(time)).minutes=0;

#ifndef __cplusplus
short DaysInMonth(short month);
#endif

RCLDLL short DaysTillMonth(short month);
RCLDLL const char * DayName(short day);
RCLDLL const char * MonthName(short month);
RCLDLL void  GetDate(TDATE *date);
RCLDLL void  GetTime(T_TIME *time);
RCLDLL char * DateToString(const TDATE *date);
RCLDLL char * DateToString4(const TDATE *date);
RCLDLL char * DateToVerbose(const TDATE *date);
RCLDLL char * TimeToString(const T_TIME *time);
RCLDLL char * TimeToVerbose(const T_TIME *time);
RCLDLL bool  StringToTime(const char *string, T_TIME *time);
RCLDLL short  ValidDate(TDATE *date);
RCLDLL short  ValidTime(const T_TIME *time);
RCLDLL short  ValidBigTime(const T_TIME *time);
RCLDLL void   AddDays(TDATE *date, short days);
RCLDLL short  AddTime(T_TIME *time1, T_TIME *time2);
RCLDLL short  CompareDates(const TDATE *date1, const TDATE *date2);
RCLDLL short  CompareTimes(const T_TIME *time1, const T_TIME *time2);
RCLDLL short  DayOfWeek(const TDATE *date);
RCLDLL bool  DateIsBetween(const TDATE *start,const TDATE *target,const TDATE *end);
RCLDLL bool  ParseDate(TDATE *date, const char *string);
RCLDLL bool  DateRangesOverlap(const TDATE *start1, const TDATE *end1, const TDATE *start2, const TDATE *end2);
RCLDLL bool  ParseDateBased(TDATE *date, const char *string, const TDATE *basedate);
RCLDLL void  InitDateTimeData();

inline short IsLeapYear(short yr)
{
    int leaper = 0;
    if(yr > 1581)
        {
            if((yr % 100) == 0) // divisible by 100?
                {
                    if((yr % 400) == 0) // also divisible by 400?
                        leaper = 1;
                }
            else if((yr % 4) == 0)
                leaper = 1;
        }
    return(leaper);
}

inline short DaysInMonth(short month)
{
	InitDateTimeData();
	return(_DT_days_per_month[month]);
}

inline short DaysInMonth(TDATE& date)
{
	InitDateTimeData();
	short days = DaysInMonth(date.month);
	if(date.month == FEB && IsLeapYear(date.year))
		days++;
	return(days);
}

inline long DateToLong(const TDATE *thedate)
{
	long ldate = 0L;

	InitDateTimeData();
	if(thedate && thedate->year)
		{
			long year = (long)thedate->year;
			ldate = (year * 365)  + ((year/4) - (year/100)) + (_DT_days_till_month[thedate->month])  + (thedate->day);
		}
	return (ldate);
}

#endif
