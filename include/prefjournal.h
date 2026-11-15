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

#ifndef _PREFJOURNAL_H
#define _PREFJOURNAL_H

#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>

#define _MAILBOX 	0x10
#define _FOLDER  	0x20

#define _ADD	 	0x01
#define _DELETE	 	0x02
#define _RENAME	 	0x03
#define _RECONFIG	0x04

class PrefJournalEntry
{
	public:
							 PrefJournalEntry (unsigned char, const char *, const char *_Second=NULL);
							~PrefJournalEntry (void);

		PrefJournalEntry	*Next, *Prev;
		unsigned char		 Type;
		char				*Argument;
		char				*Second;
};

class PrefJournal
{
	public:
							 PrefJournal (void);
							~PrefJournal (void);

		void				 Add (unsigned char, const char *, const char *_second=NULL);
		void				 Rewind (void);
		PrefJournalEntry	*Replay (void);
		void				 Erase (void);

	protected:
		PrefJournalEntry 	*First, *Last;
		PrefJournalEntry 	*Cursor;
};

#endif
