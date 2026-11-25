// Printing (Neo-PostOffice): the shown message's headers and text, with a
// list of its attachments, as a plain UTF-8 text file handed to a print
// command. The command is set in Config -> Network -> Print command
// (postoffice/gui/action/print), default "lpr $": lpr works with CUPS and
// with the BSD print spooler (lpd), and both copy the file into the spool,
// so it is removed once the command has finished. A command without $
// gets the text on standard input. Long lines are wrapped at 80
// characters, as a printer without a text filter would not wrap them.

#include <FL/Fl.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Menu_.H>
#include <FL/fl_ask.H>

#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "mailbox.h"
#include "var.h"
#include "wordlist.h"
#include "mailheaders.h"
#include "flowed.h"

extern Var				 PREFERENCES;
extern char			   **environ;
extern MailBox			*currentmailbox;
extern int				 mdi_current;
extern char				 currentmsgid[8][256];
extern WordList			 currentmessage[8];
bool part_is_attachment (const MailPart *P);
std::string attachment_file_name (const MailPart *P, int index);

static const size_t PRINT_WIDTH = 80;

// Wraps one line at PRINT_WIDTH characters, at spaces where it can.
static void wrap_line (const std::string &line, std::string &out)
{
	std::string rest = line;
	while (flowed::Length (rest) > PRINT_WIDTH)
	{
		// Byte position of the character after PRINT_WIDTH characters.
		size_t n = 0, pos = 0;
		while (pos < rest.size () && n < PRINT_WIDTH)
		{
			++pos;
			while (pos < rest.size () && ((unsigned char) rest[pos] & 0xC0) == 0x80) ++pos;
			++n;
		}
		size_t brk = rest.rfind (' ', pos);
		if (brk == std::string::npos || brk == 0) brk = pos;	// one long word
		out += rest.substr (0, brk) + "\n";
		rest = rest.substr (brk);
		while (!rest.empty () && rest[0] == ' ') rest.erase (0, 1);
	}
	out += rest + "\n";
}

static std::string print_text (MailMessage *M)
{
	std::string out;
	static const char *shown[] = { "From", "To", "Cc", "Date", "Subject" };
	for (const char *h : shown)
	{
		const char *v = M->Header ((char *) h);
		if (!v || !*v) continue;
		bool addr = strcasecmp (h, "From") == 0 || strcasecmp (h, "To") == 0 || strcasecmp (h, "Cc") == 0;
		wrap_line (std::string (h) + ": " + (addr ? mailheaders::DecodeAddresses (v) : mailheaders::Decode (v)), out);
	}
	std::vector<std::string> files;
	for (int i = 0; i < M->Count; ++i)
		if (part_is_attachment (M->GetPart (i))) files.push_back (attachment_file_name (M->GetPart (i), i));
	if (!files.empty ())
	{
		std::string list;
		for (const std::string &f : files) list += (list.empty () ? "" : ", ") + f;
		wrap_line ("Attachments: " + list, out);
	}
	out += "\n";
	for (int i = 0; i < currentmessage[mdi_current].Count; ++i)
	{
		std::string line = currentmessage[mdi_current].Get (i);
		while (!line.empty () && (line.back () == '\r' || line.back () == ' ')) line.pop_back ();
		for (char &c : line) if (c == '\t') c = ' ';
		wrap_line (line, out);
	}
	return out;
}

// The command template with $ standing for the file, quoted as for opening
// attachments; without $ the file is given on standard input.
static std::string shell_command (const char *tmpl)
{
	std::string cmd;
	bool in_single = false, in_double = false, used = false;
	for (const char *p = tmpl; *p; ++p)
	{
		if (*p == '$')
		{
			used = true;
			if (in_single) cmd += "'\"$1\"'";
			else if (in_double) cmd += "$1";
			else cmd += "\"$1\"";
			continue;
		}
		if (*p == '\\' && !in_single && p[1]) { cmd += *p++; cmd += *p; continue; }
		if (*p == '\'' && !in_double) in_single = !in_single;
		else if (*p == '"' && !in_single) in_double = !in_double;
		cmd += *p;
	}
	if (!used) cmd += " < \"$1\"";
	return cmd;
}

static void print_message (void)
{
	if (!currentmailbox || !currentmsgid[mdi_current][0])
	{
		fl_message ("Select the message to print first.");
		return;
	}
	MailMessage *M = currentmailbox->Find (currentmsgid[mdi_current]);
	if (!M) { fl_alert ("The message is no longer in this mailbox."); return; }

	const char *tmpl = PREFERENCES.Get_char_tree ("postoffice/gui/action/print");
	if (!tmpl || !*tmpl) tmpl = "lpr $";

	// The text in a private folder of its own, as for opened attachments.
	const char *home = getenv ("HOME");
	std::string root = std::string (home ? home : ".") + "/.postoffice/tmp";
	mkdir ((std::string (home ? home : ".") + "/.postoffice").c_str (), 0700);
	mkdir (root.c_str (), 0700);
	std::string dtmpl = root + "/open-XXXXXX";
	std::vector<char> buf (dtmpl.begin (), dtmpl.end ());
	buf.push_back (0);
	if (!mkdtemp (buf.data ())) { fl_alert ("Cannot make a folder in %s: %s.", root.c_str (), strerror (errno)); return; }
	std::string dir = buf.data (), path = dir + "/message.txt";
	std::string text = print_text (M);
	int fd = open (path.c_str (), O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0 || write (fd, text.data (), text.size ()) != (ssize_t) text.size ())
	{
		fl_alert ("Cannot write %s: %s.", path.c_str (), strerror (errno));
		if (fd >= 0) close (fd);
		unlink (path.c_str ());
		rmdir (dir.c_str ());
		return;
	}
	close (fd);

	// Run it, keeping what it says on standard error for the message.
	std::string cmd = shell_command (tmpl) + " 2>&1";
	int pfd[2];
	if (pipe (pfd) != 0) { unlink (path.c_str ()); rmdir (dir.c_str ()); return; }
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, pfd[1], 1);
	posix_spawn_file_actions_addclose (&fa, pfd[0]);
	posix_spawn_file_actions_addclose (&fa, pfd[1]);
	posix_spawn_file_actions_addopen (&fa, 0, "/dev/null", O_RDONLY, 0);
	char *const argv[] = { (char *) "sh", (char *) "-c", (char *) cmd.c_str (), (char *) "sh", (char *) path.c_str (), NULL };
	pid_t pid;
	int rc = posix_spawn (&pid, "/bin/sh", &fa, NULL, argv, environ);
	posix_spawn_file_actions_destroy (&fa);
	close (pfd[1]);
	std::string said;
	int status = -1;
	bool finished = false;
	if (rc == 0)
	{
		// lpr only hands the file to the spooler, which takes a moment; the
		// window keeps redrawing meanwhile. Gives up waiting after a minute.
		fcntl (pfd[0], F_SETFL, O_NONBLOCK);
		for (int waited = 0; waited < 600; ++waited)
		{
			char b[512];
			ssize_t n;
			while ((n = read (pfd[0], b, sizeof (b))) > 0) said.append (b, n);
			if (waitpid (pid, &status, WNOHANG) == pid) { finished = true; break; }
			Fl::wait (0.1);
		}
		char b[512];
		ssize_t n;
		while ((n = read (pfd[0], b, sizeof (b))) > 0) said.append (b, n);
	}
	close (pfd[0]);
	if (rc == 0 && !finished)
	{
		// Still running: its file stays until Neo-PostOffice quits.
		fl_message ("The print command \"%s\" is still running after a minute; "
					"the message may still print.", tmpl);
		return;
	}
	unlink (path.c_str ());
	rmdir (dir.c_str ());

	while (!said.empty () && (said.back () == '\n' || said.back () == ' ')) said.pop_back ();
	if (said.size () > 600) said = said.substr (0, 600) + "...";
	if (rc != 0)
		fl_alert ("Cannot run the print command: %s.", strerror (rc));
	else if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
		fl_alert ("Printing failed.\n\nThe print command \"%s\" said:\n%s\n\n"
				  "Check that a printer is set up (for CUPS, lpstat -d shows\n"
				  "the default printer), or set another command in\n"
				  "Config -> Network -> Print command.",
				  tmpl, said.empty () ? "(nothing)" : said.c_str ());
}

void cb_print (Fl_Button *, void *) { print_message (); }
void cb_print_m (Fl_Menu_ *, void *) { print_message (); }
