//===============================================================================
// POSTOFFICE - X11 Office Communication Handler
// ---------------------------------------------
// All sourcecode, unless differently annotated, written by Pim van Riezen
// Sourcecode and resulting binaries are covered by the GNU Lesser General Public
// License (GNU LGPL). For more information and the literal text of the license
// agreement, check the Free Software Foundation's website on http://www.fsf.org/
// 
// Copyright (C) 1999 Mad Sciene Labs - Rotterdam, The Netherlands
//===============================================================================

#include <string>
#include "formatter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

int quotelevel (char *);
int indentlevel (char *);
int textboundary (char *);
int isheader (char *);
int isrepetitive (char *, char *);
int issystem (char *);

TextMessage::TextMessage (WordList *W)
{
	First = NULL;
	Last = NULL;
	WordList *WW = new WordList;

	Paragraph *P = new Paragraph (0,0);
	P->Prev = P->Next = NULL;
	int		  i, j;
	int		  ind,quo;
	int		  numsamelevel = 0;
	char		*tmp;
	char		*nxt;
	char		*nxx;
	int		  flaglf;
	int		  oldlen=0;
	int		  curlen=0;
	
	for (i=0;i<W->Count;++i)
	{
		quo = quotelevel ( W->Get(i));
		ind = indentlevel (W->Get(i));
		tmp = W->Get(i) + textboundary (W->Get(i));
		if ((i+1)<W->Count) nxt = W->Get(i+1) + textboundary (W->Get(i+1));
		else nxt = (char *) "";
		if ((i+2)<W->Count) nxx = W->Get(i+2) + textboundary (W->Get(i+2));
		else nxx = (char *) "";
		curlen = strlen (tmp);
		if (Last)
		{
			if ((curlen < 5 && Last->Text->Count > 0))
			{
				if (Last)
				{
					P->Next = NULL;
					P->Prev = Last;
					Last->Next = P;
					Last = P;
				}
				else
				{
					P->Next = P->Prev = NULL;
					Last = First = P;
				}
				P = new Paragraph (quo, ind);
			}
		}
		//printf ("[pre-quot] (i=%i q=%i) %s\n", ind, quo, W->Get(i));
		
		if ( quo  != P->QuoteLevel )
		{
			if (P->Text->Count)
			{
				if (Last)
				{
					P->Next = NULL;
					P->Prev = Last;
					Last->Next = P;
					Last = P;
				}
				else
				{
					P->Next = P->Prev = NULL;
					Last = First = P;
				}
				P = new Paragraph (quo, ind);
				numsamelevel = 0;
			}
			else
			{
				P->QuoteLevel = quo;
				P->Indent = ind;
				numsamelevel = 0;
			}
		}
		if ((ind != P->Indent) && (P->Text->Count==0)) P->Indent = ind;
		if (ind < P->Indent) P->IndentOnce = 1;
		else if (ind > P->Indent)
		{
			if (Last)
			{
				P->Next = NULL;
				P->Prev = Last;
				Last->Next = P;
				Last = P;
			}
			else
			{
				P->Next = P->Prev = NULL;
				Last = First = P;
			}
			P = new Paragraph (quo, ind);
		}
		else P->IndentOnce = 0;
		flaglf = 0;
		flaglf |= isheader (tmp);
		if ((!flaglf) && (i)) flaglf |= isrepetitive (tmp, W->Get(i-1) + textboundary (W->Get(i-1)));
		if ((!flaglf) && ((tmp[0] == '#')||(tmp[0]=='[')||(tmp[0]=='<'))) flaglf=1;
		if ((!flaglf) && (strlen (tmp) < 50) && (strlen (nxt) < 50) &&
						 (strlen (nxx) > 1)) flaglf = 1;
		if ((!flaglf) && (strlen (tmp) < 60) && (strlen (nxt) < 60) && 
						 ((strlen (nxx) < 60)&&(strlen (nxx) > 1)) ) flaglf = 1;
		if (!flaglf) flaglf |= issystem (tmp);
		if (!flaglf && strstr (tmp, "   ")) flaglf=1;
		if (flaglf)
		{
			if (P->Text->Count)
			{
				if (Last)
				{
					P->Next = NULL;
					P->Prev = Last;
					Last->Next = P;
					Last = P;
				}
				else
				{
					P->Next = P->Prev = NULL;
					Last = First = P;
				}
				P = new Paragraph (quo, ind);
			}
		}
		WW->MakeGeneric (W->Get(i) + textboundary (W->Get(i)), ' ', 0);
		for (j=0;j<WW->Count;++j)
		{
			P->Text->Add (WW->Get(j));
		}
		if ((!flaglf) && (strlen(tmp)) && (tmp[strlen(tmp)-1] == ':')) flaglf=1;
		if (flaglf)
		{
			if (P->Text->Count)
			{
				if (Last)
				{
					P->Next = NULL;
					P->Prev = Last;
					Last->Next = P;
					Last = P;
				}
				else
				{
					P->Next = P->Prev = NULL;
					Last = First = P;
				}
				P = new Paragraph (quo, ind);
			}
		}
		if ((WW->Count < 1) && ((!numsamelevel)||(!quo)))
		{
			if (Last)
			{
				P->Next = NULL;
				P->Prev = Last;
				Last->Next = P;
				Last = P;
			}
			else
			{
				P->Next = P->Prev = NULL;
				Last = First = P;
			}
			P = new Paragraph (quo, ind);
				//P->Text->Add ("");
				P->Next = NULL;
				P->Prev = NULL;
				Last->Next = P;
				Last = P;
				P = new Paragraph (quo, ind);
		}
		if ((curlen < 30 && oldlen < 30)||(curlen<2))
		{
			if (Last)
			{
				P->Next = NULL;
				P->Prev = Last;
				Last->Next = P;
				Last = P;
			}
			else
			{
				P->Next = P->Prev = NULL;
				Last = First = P;
			}
			P = new Paragraph (quo, ind);
		}
		++numsamelevel;
		WW->Clear();
		oldlen = curlen;
	}
}

TextMessage::~TextMessage (void)
{
	Paragraph *P, *NP;
	
	P = First;
	
	while (P)
	{
		NP = P->Next;
		delete (P);
		P = NP;
	}
	First = NULL;
	Last = NULL;
}

WordList *TextMessage::Get (int quoted)
{
	std::string buf;		// (was char[1024]: deep quoting overflowed it)
	Paragraph *P, *NP;
	int quo, ind;
	int total;
	int ln;
	int i,j;
	int lastwasempty = 0;
	int inc;
	
	WordList *result = new WordList;
	
	dprintf ("textmessage::get()\n");
	
	P = First;
	while (P)
	{
		quo = P->QuoteLevel;
		ind = P->Indent;
		
		
		j = 0;
		while (j<P->Text->Count)
		{
			buf.clear();
			total = 0;
		
			for (i=0;i<quo+quoted;++i)
			{
				buf += "> ";
				total += 2;
			}
			if (P->Prev)
			{
				if ((P->Prev->Indent == P->Indent)&&(P->IndentOnce==1)) ind = 0;
			}
			else ind = P->Indent;
			//printf ("[quote-paragraph] indent=%i once=%i\n", ind, P->IndentOnce);
			if (P->Text->Get(j)[0] != '*')
			{
				for (i=0;i<ind;++i)
				{
					buf += ' ';
					++total;
				}
			}
			if (P->IndentOnce == 1)
			{
				ind = 0;
			}
			inc = 1;
			while ( (j<P->Text->Count) && (total + (ln = strlen (P->Text->Get(j))) < 75) )
			{
				//printf ("[quote] %s\n", buf);
				buf += P->Text->Get(j); total += ln;
				if ( (j+1) < P->Text->Count)
				{
					++total;
					buf += ' ';
				}
				++j;
				inc = 0;
			}
			j += inc;
			result->Add (buf.c_str());
			if (j < P->Text->Count && strlen (P->Text->Get(j)) > 74)
			{
				buf = "(...) ";
				buf.append (P->Text->Get(j++), 70);
				result->Add (buf.c_str());
			}
		}
		
		NP = P->Next;
		
		if (P->Text->Count == 0)
		{
			if (!lastwasempty)
			{
				buf.clear();
				for (i=0;i<quo+quoted;++i) buf += "> ";
				result->Add (buf.c_str());
				lastwasempty=1;
			}
		}
		else
		{
			lastwasempty = 0;
			if (NP)
			{
				if (NP->QuoteLevel > quo)
				{
					buf.clear();
					for (i=0;i<quo+quoted;++i) buf += "> ";
					result->Add (buf.c_str());
				}
				else if (NP->QuoteLevel < quo)
				{
					buf.clear();
					for (i=0;i<NP->QuoteLevel+quoted;++i) buf += "> ";
					result->Add (buf.c_str());
					lastwasempty=1;
				}
			}
		}
		P = NP;
	}
	return (result);
}

Paragraph::Paragraph (int _Quot, int _Ind)
{
	Text = new WordList;
	QuoteLevel = _Quot;
	Indent = _Ind;
	IndentOnce = 0;
}

Paragraph::~Paragraph (void)
{
	if (Text) delete (Text);
}

// Determine the level of quotes on a textline

int quotelevel (char *text)
{
	int result = 0;
	int crsr = 0;
	
	while ( (text[crsr] == ' ') || (text[crsr] == '>') )
	{
		if (text[crsr] == '>') ++result;
		++crsr;
	}
	return (result);
}

// Determine if the text is indented

int indentlevel (char *text)
{
//	int result = 0;
	int crsr = 0;
	int textstart = 0;
	
	while ( (text[crsr] == ' ') || (text[crsr] == '>') || (text[crsr] == '*') )
	{
		if ((text[crsr] == '>') && ((text[crsr+1] == ' ') || (text[crsr+1] == '*')) ) textstart = crsr+2;
		else if (text[crsr] == '>') textstart = crsr+1;
		++crsr;
	}
	return (crsr - textstart);
}

// Figure out where on the line the actual text starts

int textboundary (char *text)
{
	int result = 0;
	
	while ( (text[result] == ' ') || (text[result] == '>') ) ++result;
	
	return (result);
}

const char *headersafe = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";

// See if a line is syntactically a header

int isheader (char *text)
{
	int crsr=0;
	while ((text[crsr])&&(text[crsr]!=':')) if (!(strchr (headersafe, text[crsr++]) )) return 0;
	if (text[crsr]) return 1;
	return 0;
}

int isrepetitive (char *t1, char *t2)
{
	if ((strlen (t1) >2) && (strlen (t2) > 2))
	{
		if ( (t1[0] == t2[0]) && (t1[1] == t2[1]) ) return 1;
	}
	return 0;
}

int issystem (char *text)
{
	if (strlen (text) <3) return 0;
	if ((text[0]==text[1])&&(text[1]==text[2])) return 1;
	return 0;
}
