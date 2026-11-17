
#ifndef VARTYPES_H          /* include only once per compile */
#define VARTYPES_H 1

// compatibility with code written for ancient compilers...
#define far
#define _export 

#define RCLDLL  
#define CLASSEXPORT

// if your compiler doesn't support a bool type, uncomment this...
//typedef char bool;


// These are for the editor, but occasionally seem useful elsewhere
#define SOFT_RETURN 0x0D
#define HARD_RETURN 0x0A
#define CARRIAGE_RETURN  0x0D
#define LINE_FEED 0x0A


#define REVERSE_WORD(x) ((((x) & 0x00FF) << 8) | (((x) & 0xFF00) >> 8))


#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

#ifdef _Windows

#ifdef __DLL__
#define CLASSEXPORT _export
#define CLASSIMPORT _import
#else
#define CLASSEXPORT _import
#define CLASSIMPORT _import
#endif  // ___DLL__

#else
#define CLASSEXPORT
#define CLASSIMPORT
#endif  // _Windows



#ifdef _WINDOWS
// ms compiler macro for exporting classes
#undef CLASSEXPORT
#ifdef AFX_EXT_CLASS
#define CLASSEXPORT AFX_EXT_CLASS
#else
#define CLASSEXPORT __declspec(dllexport)
#endif
#undef RCLDLL
#if defined(RCL_EXPORTS)
#define RCLDLL  __declspec(dllexport)
#else
#define RCLDLL  
#endif
#endif

#ifndef __LINE__
#define __LINE__       (0L)
#endif

#ifndef __FILE__
#define __FILE__    NULL
#endif

extern double PowersOfTen[];

enum ErrorSeverity
{
  ERROR_OKAY,
  ERROR_INFO,
  ERROR_WARN,
  ERROR_FAIL,
};


#undef NOERROR

enum rclError {
  NOERROR,        /* everything OK */	
  NOMEMORY,           /* out of memory */	
  USERCANCEL,         /* user cancelled operation */
  GOTBREAKKEY,        /* user hit a break key */
  ABORTED,            /* operation aborted */
  FILEERROR,          /* a file error occurred */
  BUFFER_OVERFLOW,    /* operation overflowed internal buffers */	
  RELOAD,             /* internally used to force data reload */
  SECURITYVIOLATION,  /* unauthorized operation attempted */
  NOCONFIG,           /* can't read config file records */
  CLIPBOARD_EMPTY,    /* nothing in clipboard to operate on */
  NO_REGION,          /* region not marked to operate on */
  RANGE_ERROR,        /* bounds check exceeded */
  LIMITS_FULL_ERROR,  /* preset limits exceeded */
  LICENSE_OK,         /* license is OK for this module */
  NOT_LICENSED,       /* no license for this module */
  LICENSE_EXPIRED,    /* past license date range */
  FORM_COMPLETE,      /* user completed input form */
  WRONG_TYPE,         /* data is not the expected type */
  VMERROR,            /* VM file error */
  INVALID_PARAMETER,  /* bad parameter given for object construction */
  UNKNOWN_USER,			/* can't identify user name */
};


/* Macro to get a random integer within a specified range */

#define getrandom( min, max ) ((rand() % (int)(((max)+1) - (min))) + (min))
#ifndef __cplusplus
#define DIVIDE(num, by)         (((by) == 0) ? 0 : ((num) / (by)))
#endif

#ifdef __cplusplus
#include <math.h>
#ifndef modf
extern "C" double modf(double, double *);
#endif
inline double Round(double num, short places)
{
  double intpart, fracpart;
  
  fracpart = modf(num * PowersOfTen[places], &intpart);
  if(fracpart >= 0.5)
    intpart++;
  return (intpart / PowersOfTen[places]);
}
#endif


inline bool IsZero(double num)
{	
  return((num > -0.005) && (num < 0.005));	
}

inline double DIVIDE(double num, double by)
{	
  return (IsZero(by) ? 0 : num / by);	
}

#endif

