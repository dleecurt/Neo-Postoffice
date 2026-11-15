
//
//  OKay, I know this is a  pretty wimpy string class, but I was in
//  a hurry, OK?  Nothing fancy, just basic "safe" strings.
//

#include <string.h>
#include <ctype.h>
#include <rcl/library/strings.hpp>


RCLDLL void wString::Initialize(const char *init, long  size)
{
    dummy[0] = '\0';
	if(size < 1)
		if(init)
			size = strlen(init);
		else
			size = 1;
	BufferCapacity = 0;
	BufferLength = 0;
	buffer = (char *)calloc(1,size+4);
	if(buffer)
		{
			BufferCapacity = size+3;
			if(init)
				strcpy(buffer,init);
			else
				buffer[0] = '\0';
			BufferLength = strlen(buffer);
		}
}

RCLDLL wString::wString(long  repeat_value, char character)
{
	if(repeat_value < 0)
		repeat_value = 0;
	buffer = (char *)calloc(1,repeat_value+4);
	if(buffer)
		{
			BufferCapacity = repeat_value+3;
			memset(buffer,character,repeat_value);
			buffer[repeat_value] = '\0';
			BufferLength = strlen(buffer);
		}
	else
		BufferCapacity = 0;
}

RCLDLL wString::wString(const char *what, long  where, long  howmuch, bool stripit)
{
	buffer = (char *)calloc(1,howmuch+4);
	if(buffer)
		{
			BufferCapacity = howmuch+3;
			memcpy(buffer,&what[where],howmuch);
			buffer[howmuch] = '\0';
			BufferLength = strlen(buffer);
			if(stripit)
				Strip();
			else
				{
					*this += wString(howmuch, ' ');
					this->ChopAt(howmuch);
				}
		}
	else
		BufferCapacity = 0;
}



RCLDLL wString::~wString()
{
	if(buffer)
		free(buffer);
	buffer = NULL;
	BufferCapacity = 0;
	BufferLength = 0;
}

RCLDLL void wString::Shrink()
{
	if(BufferCapacity > (BufferLength + 4))
		{
			char *newbuf = (char *)calloc(1,BufferLength+4);
			if(newbuf)
				{
					if(buffer)
						{
							strcpy(newbuf,buffer);
							free(buffer);
						}
					BufferCapacity = BufferLength + 3;
					buffer = newbuf;
				}
		}
}

RCLDLL void wString::Set(const char *val)
{
	long  newsize;

	if(val)
		{
			newsize = strlen(val) + 4;
			if(BufferCapacity < newsize)
				Grow(newsize - BufferCapacity + 4);
			if(buffer)
				{       // this will work whether or not the buffer got resized
						// since if it did, the '\0' will get copied by the
						// strncpy, and if it didn't, the next line will get it.
					strncpy(buffer,val,BufferCapacity);
					buffer[BufferCapacity] = '\0';
					BufferLength = strlen(buffer);
					Shrink();
				}
		}
	else
		{
			buffer[0] = '\0';
			BufferLength = 0;
			Shrink();
		}
}

RCLDLL wString& wString::operator+=(const wString& str)
{
	long  newsize = BufferLength + str.BufferLength + 4;
	if(newsize > BufferCapacity)
		Grow(newsize - BufferCapacity + 4);
	strcat(buffer,str.buffer);
	BufferLength = strlen(buffer);
	return(*this);
}

RCLDLL wString& wString::operator+=(char c)
{
	if(BufferLength > BufferCapacity - 4)
		Grow();
	buffer[BufferLength++] = c;
	buffer[BufferLength] = '\0';
	return(*this);
}

RCLDLL wString& wString::operator=(const wString& str)
{
	Set(str.buffer);
	return (*this);
}

RCLDLL wString wString::Extract(long  position,long  length,bool cutit)
{
	long  count;

	wString s("",length+1);
	if(position < BufferLength)
		{
			if(s.buffer)
				{
					for(count=0;count<length;count++)
						s.buffer[count] = this->buffer[position+count];
					s.buffer[count] = '\0';
					s.BufferLength = length;
					if(cutit)
						DeleteAt(position,length);
				}
		}
	return (s);
}

RCLDLL wString wString::GetWordAt(long  position) const
{
	char *temp;

	wString s("",this->BufferCapacity);
	if(s.buffer)
		{
			// rewind to start of current word
			while(!isspace(this->buffer[position-1]) && !ispunct(this->buffer[position-1]))
				if(position)
					position--;
				else
					break;
			// skip leading punctuation
			while(ispunct(this->buffer[position]))
				if(position < BufferLength)
					position++;
				else
					break;
			temp = s.buffer;
			while(this->buffer[position] && !(isspace(this->buffer[position])))
				{
					if(ispunct(this->buffer[position]))
						if(this->buffer[position] != '\'')
							break;
					*temp++ = this->buffer[position++];
				}
			*temp = '\0';
			s.BufferLength = strlen(s.buffer);
			s.Shrink();
		}
	return(s);
}

RCLDLL wString& wString::TrimLeft()
{
	char *temp = buffer;
	while((*temp >0 && *temp < 127) && isspace(*temp))
		temp++;
	if(temp > buffer)
		strcpy(buffer,temp);
	BufferLength = strlen(buffer);
	Shrink();
	return (*this);
}

RCLDLL wString& wString::TrimRight()
{
	if(BufferLength)
		{
			char *temp = &buffer[BufferLength-1];
			while((temp > buffer) && (*temp > 0 && *temp < 127) && isspace(*temp))
				temp--;
			if(temp >= buffer)
				*(temp+1) = '\0';
			Shrink();
		}
	return (*this);
}

//
//  Position comes from the JUSTIFY_POSITION enumeration in strings.h
//

RCLDLL wString wString::Justify(long  size,long  position)
{
	wString s(' ', size+1);  // copy current string
	if((size_t)BufferLength < (size_t)size)
		{
			if(s.buffer)
				{
					::Justify(buffer, size, (JUSTIFY_POSITION)position, s.buffer);
					s.BufferLength = size;
				}
		}
	else
		s = *this;
	return(s);      // return original if no justification needed
}

RCLDLL wString& wString::InsertAt(long  where, char character)
{
	if(AlmostFull())
		Grow();
	if(BufferCapacity > BufferLength)
		if(where <= (BufferLength+1))   // insert allowed if before NULL terminator
			{
				memmove(&buffer[where+1],&buffer[where],(BufferLength+1)-where+1);
				buffer[where] = character;
				BufferLength++;
			}
	return(*this);
}

RCLDLL wString& wString::InsertAt(long  where, const char *insertme)
{
	long  inslen = strlen(insertme);
	if((BufferCapacity - BufferLength) < inslen)
		Grow(inslen);
	if(BufferCapacity  >= (BufferLength + inslen))
		if(where <= (BufferLength+1))   // insert allowed if before NULL terminator
			{
				memmove(&buffer[where+inslen],&buffer[where],BufferLength-where+1);
				memcpy(&buffer[where],insertme,inslen);
				BufferLength += inslen;
			}
	return(*this);
}

RCLDLL wString& wString::DeleteAt(long  where,long  howmany)
{
	if((where >= 0) && (where < BufferLength))
		{
			if(howmany < (BufferLength-where))
				{
					strcpy(&buffer[where],&buffer[where+howmany]);
					BufferLength -= howmany;
				}
			else
				{
					buffer[where] = '\0';
					BufferLength = where;
				}
			Shrink();
		}
	return(*this);
}


RCLDLL wString& wString::ChopAt(long  limit, wString *remainder)
{
	if(BufferLength && (limit >= 0) && (limit < BufferLength))
		{
			if(remainder)
				remainder->Set(&buffer[limit]);
			buffer[limit] = '\0';
			BufferLength = limit;
			Shrink();
		}
	else if(remainder)
		remainder->Set("");
	return(*this);
}

RCLDLL wString& wString::WrapAt(long  limit, wString *remainder)
{
	if(remainder)
		remainder->Set("");
	if(BufferLength && (limit >= 0) && (limit < BufferLength))
		{
			if(isspace(buffer[limit]) || (buffer[limit] == '\0'))
				ChopAt(limit,remainder);
			else
				{
					while(limit && !isspace(buffer[limit]))
						limit--;
					if(limit)
						ChopAt(++limit,remainder);  // skip past space, clip rest of line
				}
		}
	return(*this);
}

RCLDLL wString& wString::Grow(long  howmuch)
{
	char *newbuf = (char *)calloc(1, BufferCapacity + howmuch + 1);
	if(newbuf)
		{
			if(buffer)
				{
					strcpy(newbuf,buffer);
					free(buffer);
				}
			else
				newbuf[0] = '\0';
			BufferCapacity += howmuch;
			buffer = newbuf;
		}
	return(*this);
}

RCLDLL void wString::Replace(char orig, char replace, long  start, long  count)
{
	long  position, replaced=0;

	if(!count)                  // number of replacements to allow
		count = BufferLength;
	if(count && BufferLength && orig)       // don't change if no buffer, replacement count,
		for(position=start; position < BufferLength; position++)    // or original character
			if(buffer[position] == orig)
				{
					buffer[position] = replace;
					if(++replaced >= count)
						break;
				}
}

RCLDLL wString operator+(const char *str1, const wString& str2)
{
	wString s(str1);
	s+= str2;
	return (s);
}

RCLDLL wString operator+(const wString& str1, const char *str2)
{
	long  len = strlen(str2);
	wString s(str1.Get(), str1.Length() + len + 4);
	strcat(s.buffer,str2);
	s.BufferLength += len;
	return (s);
}

RCLDLL wString operator-(const wString& str1, const char *str2)
{
	char *temp;

	wString s(str1);
	if(s.buffer)
		{
			do
				{
					temp = strstr(s.buffer,str2);           // find substring
					if(temp)                                // knock it out
						strcpy(temp,&temp[strlen(str2)]);
				}
			while(temp);
			s.BufferLength = strlen(s.buffer);
			s.Shrink();
		}
	return(s);
}

RCLDLL wString& StripCR(wString& str)
{
	for(long  index=0; index < str.Length(); index++)
		if(str[index] == HARD_RETURN || str[index] == SOFT_RETURN)
			str[index] = ' ';
	return str;
}

RCLDLL wString& FormatNumber(double number, long  wide, wString& dest, bool wholenum)
{
	char *amount = new char[256];
	char *buffer = new char[256];

	if(buffer && amount)
		{
			double num = Round(number, 2);
                        if(wholenum)
                                sprintf(buffer, "%0.0f", num);
                        else
                                sprintf(buffer, "%0.2f", num);
			CommaDelimitNumber(buffer, amount);
			buffer[0] = '\0';
			if(wide > 0 && wide < 250-(long )strlen(amount))
				Justify(amount, wide, RCLJ_RIGHT, buffer);
			else
				strcpy(buffer, amount);
			dest = buffer;      // copy string to destination
		}
	if(buffer)
		delete[] buffer;
	if(amount)
		delete[] amount;
	return dest;
}

RCLDLL wString& FormatNumber(long number, long  wide, wString& dest)
{
	char *amount = new char[256];
	char *buffer = new char[256];
	long num = number;

	if(buffer && amount)
		{
			sprintf(buffer, "%ld", num);
			CommaDelimitNumber(buffer, amount);
			buffer[0] = '\0';
			if(wide > 0 && wide < 250-(long )strlen(amount))
				Justify(amount, wide, RCLJ_RIGHT, buffer);
			else
				strcpy(buffer, amount);
			dest = buffer;
		}
	if(buffer)
		delete[] buffer;
	if(amount)
		delete[] amount;
	return dest;
}



RCLDLL void Pack(wString& string)
{
	long  count = 0;

	while(count < string.Length())
		if(ispunct(string[count]) || isspace(string[count]))
			string.DeleteAt(count, 1);
		else
			count++;
}

RCLDLL wString operator+(const wString& str, long num)
{
	wString numstring;

	FormatNumber(num, 15, numstring);
	numstring.Strip();
	wString temp(str);
	temp += numstring;
	return temp;
}

RCLDLL wString operator+(const wString& str, double num)
{
	wString numstring;

	FormatNumber(num, 20, numstring);
	numstring.Strip();
	wString temp;
	temp += numstring;
	return temp;
}


