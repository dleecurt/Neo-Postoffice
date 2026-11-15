#ifndef _WORDLIST_H
#define _WORDLIST_H 1

class Word
{
	public:
			 				 Word (void);
			 				~Word (void);
			 
		//Word 				*Next, *Prev;
		
		char 				*Contents;
		class WordList  	*WContents;
};

class WordList
{
	public:
						 WordList (void);
						 WordList (WordList *);
						~WordList (void);
		void			 Clear (void);
		void			 Make (char *);
		void			 MakeDB (char *);
		void			 MakeGeneric (char *, char, char Escape = '\\');
		void			 MakeGenericQuoted (char *, char, char Escape = '\\');
		void			 Add (char *);
		void			 Add_ (char *);
		void			 AddWordList (WordList *);
		void			 AddWordList_ (WordList *);
		void			 AddInt (int);
		void			 Trim (int);
		int				 Find (char *);
		char			*Get (int);
		WordList		*GetWordList (int);
		int				 IsWordList (int);
		int				 GetInt (int);
		void			 Delete (int);
		char			*Gather (int, char *, int);
		void			 Set (int, char *);
		void			 SetInt (int, int);
		
		int				 Count;

	private:

		Word			**Array;
		int				 ArraySize;
};

void reclaim_wordlist (void *);

#endif
