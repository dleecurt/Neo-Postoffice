#ifndef _ENVELOPE_H
#define _ENVELOPE_H 1

#include "stringtoken.h"

#define ENVELOPE_I_READ			0
#define ENVELOPE_I_PARENTID		1
#define ENVELOPE_MAILFROM		2
#define ENVELOPE_NAMEFROM		3
#define ENVELOPE_SUBJECT		4
#define ENVELOPE_DATE			5
#define ENVELOPE_HOSTS			6
#define ENVELOPE_NEWSGROUPS		7
#define ENVELOPE_GROUPS			7
// Neo-PostOffice: message flags (as far as the mailbox keeps them; Maildir)
#define ENVELOPE_I_FLAGS		8
#define EFLAG_FLAGGED			0x01
#define EFLAG_REPLIED			0x02
#define EFLAG_PASSED			0x04

class MailEnvelope
{
	public:
					 MailEnvelope (void);
					~MailEnvelope (void);
		
		void		 Set (int, const char *);
		void		 SetInt (int, int);
		const char	*Get (int);
		int			 GetInt (int);
				
		const char	*ENV_MAILFROM;
		const char	*ENV_NAMEFROM;
		const char	*ENV_SUBJECT;
		const char	*ENV_DATE;
		const char	*ENV_GROUPS;
		const char  *ENV_PARENTID;
		
		int			 ENV_I_READ;
		int			 ENV_I_FLAGS;
};

#endif
