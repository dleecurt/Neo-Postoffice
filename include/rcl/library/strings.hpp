
//  Definition of a string class...

#ifndef STRINGCLASS_H

#define STRINGCLASS_H 1

#include <string.h>
#include <stdlib.h>
#include <rcl/library/vartypes.h>
#include <rcl/library/strings.h>


class CLASSEXPORT wString
	{
	protected:
		char dummy[2];   // dummy string for if allocation fails...
		char *buffer;           // the buffer where the string is stored
		long BufferCapacity;     // how much it can hold without growing, one less than allocated byte count
		long BufferLength;       // length of the string currently in the buffer
		void Initialize(const char *init, long size);
	public:
		wString() 							{ Initialize("",12);  };
		wString(const wString& str)  { Initialize((const char *)str.buffer,str.BufferCapacity); } ;
		wString(const char *init) 		{ Initialize(init, (init) ? strlen(init) : 4); };
		wString(const char *init, long size)	{ Initialize(init,size); };
		wString(long howmany, char Character);
		wString(const char *init, long start, long length, bool stripit = TRUE);   // for cutting strings out of fields
		~wString();
		const char *Get() const         { if(buffer) return buffer; else return dummy; };
		long Length() const              { return BufferLength; };
		long Capacity() const            { return BufferCapacity; };
		bool AlmostFull() const         { return (BufferCapacity < (BufferLength+4)); };
		wString& Grow(long amount=10);
		wString  Extract(long position, long length, bool cutit = FALSE);
		wString  Cut(long position, long length)           { return Extract(position,length,TRUE); };
		wString  Copy(long position, long length)   { return Extract(position,length,FALSE); };
		wString& Paste(long position, wString& str)        { return InsertAt(position,str.buffer); };
		wString& Paste(long position, const char *str)    { return InsertAt(position,str); };
		wString  GetWordAt(long position) const ;
		wString  Justify(long field, long position);
		wString& TrimLeft();
		wString& TrimRight();
		wString& Strip()                 { TrimLeft(); return TrimRight(); };
		wString& InsertAt(long where, char character);
		wString& InsertAt(long where, const char *words);
		wString& DeleteAt(long where,long howmany=1);
		wString& ChopAt(long limit,wString *remainder=(wString *)NULL);
		wString& WrapAt(long limit,wString *remainder=(wString *)NULL);
		void   RecalculateLength()  { BufferLength = strlen(buffer); }
		void   Set(const char *value);
		void Shrink();                 // trim unused allocation space
		char&   LastCharacter() const       { return buffer[BufferLength-1]; };
		friend wString operator+(const wString& str1, const wString& str2)	{ return str1 + str2.Get(); }; 
		RCLDLL friend wString operator+(const char * str1, const wString& str2);
		RCLDLL friend wString operator+(const wString& str1, const char * str2);
		friend wString operator-(const wString& str1, const wString& str2)  { return (str1 - str2.Get()); };
		RCLDLL friend wString operator-(const wString& str1, const char * str2);
		wString& operator+=(const wString& str);  // concatenate
		wString& operator+=(char c);         // concatenate a character
		wString& operator-=(const wString& str)  {	(*this) = (*this) - str;  return(*this);  };   // remove from current wString
		wString& operator=(const wString& str);       // duplicate right operand in left operand
		char&   operator[](long index)  { 	return ((index < BufferLength) ? (buffer[index]) : buffer[0]); };      // array index operator
		long     operator<(const wString& str) const { return (strcmp(buffer,str.Get()) < 0); };
		long     operator>(const wString& str) const   { return (strcmp(buffer,str.Get()) > 0); };  
		long     operator==(const wString& str) const  { return (strcmp(buffer,str.Get()) == 0);  };
        wString& operator=(const char *str)	{ Set(str);  return (*this); };
        wString& operator=(const unsigned char *str)    { Set((const char *)str); return *this; };
		long     operator<(const char *str) const { return ((*this)<wString(str)); } ;
		long     operator>(const char *str) const { return ((*this)>wString(str)); };
		long     operator==(const char *str) const  { return ((*this)==wString(str)); };
		operator const char *() const       { if(buffer) return buffer; else return dummy; };
		void Replace(char orig, char replace, long start=0, long count=0);
		wString& ToUpper()	{ strupr(buffer);  return *this; };
		wString& ToLower()  { strlwr(buffer);  return *this; };
		long operator!=(const char *str) const       { return !(*this == str); };
		long operator!=(const wString& str) const     { return !(*this == str); };
		long operator<=(const char *str) const       { return !(*this > str); };
		long operator<=(const wString& str) const     { return !(*this > str); };
		long operator>=(const char *str) const       { return !(*this < str); };
		long operator>=(const wString& str) const     { return !(*this < str); };
		RCLDLL friend wString operator+(const wString& str, short num)  { return (str + (long)num); };
		RCLDLL friend wString operator+(const wString& str, long num);
		RCLDLL friend wString operator+(const wString& str, double num);
		wString& operator+=(short num)  { *this = *this + num;   return *this; };
		wString& operator+=(long num)   { *this = *this + num;   return *this; };
		wString& operator+=(double num) { *this = *this + num;  return *this; };
	};



// the inline methods.  Functional methods are in strings.cpp

RCLDLL wString& StripCR(wString& str);
RCLDLL wString& FormatNumber(double number, long wide, wString& dest, bool wholenum=FALSE);
RCLDLL void Pack(wString& string);

#endif
