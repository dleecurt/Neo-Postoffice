#ifndef _MAILMESSAGE_H
#define _MAILMESSAGE_H 1

#include "wordlist.h"
#include "envelope.h"
#include "viewlist.h"
#include "mailbox.h"
#include "btree.h"

//==========================================================================================
// MailMessage
//
// I'd dare say the most essential class of the bunch. This class represents a mailmessage as part of
// a mailbox. It has an envelope with one or more headers. It has one or more body-parts, or none in
// the case of a message that doesn't have a downloaded body yet.
//
// An MMFL_ORIGINAL flag keeps track of MailMessages that don't have an equivalent copy anymore on
// the server side. These should not get lost in garbage collection runs.
//
// The MMFL_HAVEBODY flag indicates whether the body of the message has been downloaded yet.
//
// MailParts may be flushed to disk at will (it is recommended to do this after a reasonable, but not
// too long, interval. 
//==========================================================================================

#define ENVELOPE_I_READ			0
#define ENVELOPE_I_PARENTID		1
#define ENVELOPE_MAILFROM		2
#define ENVELOPE_NAMEFROM		3
#define ENVELOPE_SUBJECT		4
#define ENVELOPE_DATE			5
#define ENVELOPE_HOSTS			6
#define ENVELOPE_NEWSGROUPS		7
#define ENVELOPE_GROUPS			7

#define MMFL_ORIGINAL		 0x01
#define MMFL_HAVEBODY		 0x02
#define MMFL_INRAM			 0x04
#define MMFL_ONDISK			 0x08

class MailPart
{
	public:
						 MailPart (void);
						~MailPart (void);
		
		MailPart		*Next, *Prev;
		
		void			 Set (const char *, const char *, const char *, unsigned long, const char *);
		
		char			*ContentType;
		char			*FileName;
		char			*Description;
		unsigned int	 ContentSize;
		char			*Contents;
};

class MailMessage : public BTreeNode
{
	public:
						 MailMessage (MailBox *, char *);
						~MailMessage (void);
		
		//MailMessage		*Next, *Prev;
		
		ViewList		*Subscribers;
		WordList		*HeaderNames;
		WordList		*HeaderValues;
		MailEnvelope	*Envelope;
		
		void			 StoreFile (void);
		void			 Flush (void);
		void			 LoadBody (void);
		void			 SaveHeaders (void);
		void			 LoadHeaders (void);
		void			 RemoveFiles (void);
		
		FILE			*OpenFile (int, char *);
	
		char			*Header (const char *);
			
		void			 ProcessMessage (char *);
		void			 ProcessHeaders (char *);
		void			 ProcessBody (char *);
		
		MailPart		*GetPart (int);
		void			 AddPart (const char *, const char *, const char *, const char *); // content-type, content-description, filename
		void			 AddPart (const char *);
																											
		char			*MsgID;
		//unsigned long	 cMsgID;
		
		MailBox			*Parent;
		
		short			 Count;
		unsigned short	 Flags;
		time_t			 ReceiveDate;
		
	private:
		MailPart		*First, *Last;
		char			*MakeSafe (char *);
};

#endif
