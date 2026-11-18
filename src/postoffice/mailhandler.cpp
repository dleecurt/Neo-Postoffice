//===============================================================================
// MailHandler (Neo-PostOffice)
// ----------------------------
// The defaults of the mailbox protocol interface (see mailhandler.h), and
// the handler for mailboxes whose protocol is not available.
// GNU LGPL like PostOffice itself.
//===============================================================================

#include "mailbox.h"
#include "var.h"

MailHandler::~MailHandler (void)
{
}

void MailHandler::OpenConnection (void)
{
}

void MailHandler::CloseConnection (void)
{
}

void MailHandler::GetBody (char * /*msgid*/)
{
}

void MailHandler::CheckGroup (const char * /*group*/)
{
}

void MailHandler::DeleteMessage (char * /*msgid*/)
{
}

void MailHandler::Expire (void)
{
}

int MailHandler::PostMessage (WordList * /*groups*/, MailMessage * /*m*/)
{
	return 0;
}

UnavailableHandler::UnavailableHandler (MailBox *_Parent, Var *_Parameters, const char *what)
	: What (what), Told (false)
{
	Parent = _Parent;
	Parameters = new Var (_Parameters);
	ConnectionOpen = 0;
}

UnavailableHandler::~UnavailableHandler (void)
{
	delete Parameters;
}

void UnavailableHandler::CheckGroup (const char * /*group*/)
{
	if (Told) return;
	Told = true;
	std::string note = std::string (Parent->Name) + " is an " + What + " mailbox. " + What +
					   " is not available in this version of Neo-PostOffice; its settings are kept for when it is."
					   " Meanwhile mbsync or OfflineIMAP can keep a Maildir in step with the server"
					   " (see the README).";
	Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, Parent->Name, note.c_str());
}
