#ifndef _FOLDERDATA_H
#define _FOLDERDATA_H 1

#include "mailbox.h"

class FolderData
{
	public:
						 FolderData (MailMessage *);
						~FolderData (void);

		char 			*msgid;
		unsigned int	 chk_msgid;
		unsigned int	 chk_subject;
};

#endif
