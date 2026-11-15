#ifndef _FORMATTER_H
#define _FORMATTER_H 1

#include "wordlist.h"

class Paragraph
{
	public:
					  Paragraph (int, int);
					~Paragraph (void);
					
		Paragraph		*Next, *Prev;
		
		WordList		*Text;
		int			  QuoteLevel;
		int			  Indent;
		int			  IndentOnce;
};

class TextMessage
{
	public:
					  TextMessage (WordList *);
					~TextMessage (void);
					
		WordList		*Get (int);
	
	private:
		Paragraph		*First, *Last;
};

#endif
