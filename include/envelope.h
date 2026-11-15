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
};

#endif
