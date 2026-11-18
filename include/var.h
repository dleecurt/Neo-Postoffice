#ifndef VAR_H
#define VAR_H 1
#include <sys/types.h>
#include <stdio.h>
#include "wordlist.h"
#include "semaphore.h"

#define VT_INT 1
#define VT_SHORT 2
#define VT_LONG 3
#define VT_CHAR 4
#define VT_BINARY 5
#define VT_WORDLIST 6
#define VT_POINTER 7
#define VT_COLLECTION 8

#define MAXVARNAMELEN 128

class Value
{
	public:
						 Value (const char *);
						~Value (void);
		Value			*Prev, *Next;
		Value			*Parent, *HigherNode, *SameNode, *LowerNode;
		
		void			 Rename (const char *);
		void			 CopyTo (class Var *);

		void			 Set_collection (Var *);
		void			 Set_int (int);
		void			 Set_long (u_long);
		void			 Set_char (const char *);
		void			 Set_short (u_short);
		void			 Set_binary (const char *, size_t);
		void			 Set_wordlist (WordList *);
		void			 Set_pointer (void *);
		int				 Get_int (void);
		u_long			 Get_long (void);
		char			*Get_char (void);
		u_short			 Get_short (void);
		char			*Get_binary (void);
		size_t			 Get_size (void);
		WordList		*Get_wordlist (void);
		void			*Get_pointer (void);
		u_short			 Get_type (void);
		class Var		*Get_collection (void);
		
		char			 Name[MAXVARNAMELEN];
		unsigned int	 Checksum;
		
		char			 Altered;
		
	private:
		u_short			 TYPE;
		char			*cValue;
		u_short			 sValue;
		int				 iValue;
		u_long			 lValue;
		size_t			 BSize;
		WordList		*wValue;
		void			*Pointer;
		Var				*vValue;
};

class Var
{
	public:
						 Var (void)
						 {
							 int i;

							 First = NULL;
							 Last = NULL;
							 Count = 0;

							 for (i=0;i<16;++i)
							 {
								 CacheChecksum[i] = 0;
								 CacheValue[i] = NULL;
							 }
						 };
						 Var (Var *);
						~Var (void);
		void			 Set_int (const char *, int);
		void			 Set_long (const char *, u_long);
		void			 Set_char (const char *, const char *);
		void			 Set_short (const char *, u_short);
		void			 Set_binary (const char *, const char *, size_t);
		void			 Set_wordlist (const char *, WordList *);

		bool			 Exists (const char *);		
		int				 Get_int (const char *);
		u_long			 Get_long (const char *);
		char			*Get_char (const char *);
		u_short			 Get_short (const char *);
		char			*Get_binary (const char *);
		size_t			 Get_size (const char *);
		u_short			 Get_type (const char *);
		Var				*Get_collection (const char *);
		WordList		*Get_wordlist (const char *);
		
		void			 Rename (const char *, const char *);
		void			 Delete (const char *);

		char			*Get_char_tree (const char *);
		char			*Get_char_tree (const char *, const char);
		char			*Get_char_tree (int, ...);
		void			 Set_char_tree (const char *, const char *);
		void			 Set_char_tree (const char *, int, const char *, ...);
		char			*Get_char_backmatch (const char *, const char *);
		WordList		*Get_wordlist_backmatch (const char *, const char *);

		WordList		*GatherNames (void);
		WordList		*GatherValues (void);
		
		void			 Save (const char *);
		void			 Save (FILE *, int);
		void			 Load (const char *);
		void			 Load (FILE *);
		void			 CopyTo (Var *);
		void			 CopyFrom (Var *);

		int				 Count;
		Value			*First, *Last;
		Value			*Find (const char *);
		void			 Clear (void);
	
	private:
		Value			*_Find (const char *);
		void			 Rebuild (void);
		Value			*_Find (unsigned long, const char *);

		unsigned int	 CacheChecksum[16];
		Value			*CacheValue[16];
		
		void			 Add (Value *);
		void			 Relink (Value *);
		
		ASemaphore		 Lock;
};



void reclaim_var (void *);

#endif
