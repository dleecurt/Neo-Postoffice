#ifndef _SENDPIPE_H
#define _SENDPIPE_H 1

#include <string>

class WordList;

// Delivers a message through a sendmail-compatible program (the system MTA
// or msmtp): PROGRAM -i -f MAILFROM -- RECIPIENTS..., the message on its
// standard input. Returns false with a reason in err if it failed.
bool send_message_pipe (const char *program, WordList *HdrNames, WordList *HdrValues,
						const char *mailfrom, WordList *Recipients, const char *body,
						std::string &err);

// Runs a mailbox's fetch command (getmail, fetchmail...) with /bin/sh -c,
// stopping it after five minutes. Returns false with the reason and the
// last lines of its output in err if it failed.
bool run_fetch_command (const char *command, std::string &err);

// Runs a password command (pass show ..., secret-tool lookup ...) and
// returns the first line it prints. Its error output goes into err.
bool run_password_command (const char *command, std::string &password, std::string &err);

#endif
