//===============================================================================
// sendpipe (Neo-PostOffice)
// -------------------------
// Delivers an outgoing message by handing it to a program: the system's
// MTA (/usr/sbin/sendmail, as Postfix, Exim, OpenSMTPD and msmtp-mta
// provide it) or msmtp. Both take the same arguments:
//
//     PROGRAM -i -f SENDER -- RECIPIENT...
//
// with the message on standard input. -f also makes msmtp use the account
// whose "from" is the sender (or its default account). The message is
// kept in memory (memfd) where the system has it.
//
// It also runs a mailbox's fetch command (getmail, fetchmail) through
// /bin/sh before the mailbox is checked. Either program is stopped after
// five minutes.
//
// GNU LGPL like PostOffice itself.
//===============================================================================

#include "sendpipe.h"
#include "wordlist.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

static const int RUN_TIMEOUT = 300;	// seconds

// The message as the program reads it: headers, an empty line, the body,
// with plain \n line ends (sendmail and msmtp turn them into CRLF).
static std::string build_message (WordList *HdrNames, WordList *HdrValues, const char *body)
{
	std::string out;
	for (int i = 0; i < HdrNames->Count; ++i)
	{
		out += HdrNames->Get (i);
		out += ": ";
		out += HdrValues->Get (i);
		out += "\n";
	}
	out += "\n";
	for (const char *p = body ? body : ""; *p; ++p)
		if (!(p[0] == '\r' && p[1] == '\n')) out += *p;
	if (out.back() != '\n') out += "\n";
	return out;
}

static void close_from (int lowfd)
{
#ifdef SYS_close_range
	if (syscall (SYS_close_range, lowfd, ~0U, 0) == 0) return;
#endif
	long max = sysconf (_SC_OPEN_MAX);
	if (max < 0 || max > 65536) max = 65536;
	for (int fd = lowfd; fd < max; ++fd) close (fd);
}

// Where the message is kept for the program to read: memory (memfd) on
// Linux, FreeBSD and NetBSD; on the other BSDs a private file in
// ~/.postoffice that is removed as soon as it is open.
static int open_message_store (void)
{
#ifdef MFD_CLOEXEC
	int fd = memfd_create ("neo-postoffice-message", MFD_CLOEXEC);
	if (fd >= 0 || errno != ENOSYS) return fd;
#endif
	std::string tmpl = std::string (getenv ("HOME") ? getenv ("HOME") : "/tmp") + "/.postoffice/.send-XXXXXX";
	int fd2 = mkstemp (&tmpl[0]);
	if (fd2 < 0) return -1;
	unlink (tmpl.c_str());
	fcntl (fd2, F_SETFD, FD_CLOEXEC);
	return fd2;
}

// Trims the program's output to its last few lines, for the error message.
static std::string short_output (const std::string &text)
{
	std::string t = text;
	while (!t.empty() && (t.back() == '\n' || t.back() == ' ')) t.pop_back();
	if (t.size() > 600) t = "..." + t.substr (t.size() - 600);
	return t;
}

// Runs a program with in as its standard input (closed here) and collects
// what it writes. The program gets its own process group, so that a
// timeout stops everything it started (sh -c "getmail ..."). label names
// it in err.
static bool run_program (std::vector<std::string> args, int in, const std::string &label, std::string &err,
						 std::string *stdout_text = NULL)
{
	// The arguments, ready before fork (the child only calls exec).
	std::vector<char *> argv;
	for (std::string &a : args) argv.push_back (&a[0]);
	argv.push_back (NULL);

	int out[2], failed[2];		// output of the program; errno if exec fails
	int so[2] = { -1, -1 };		// its standard output alone, if wanted
	if (pipe2 (out, O_CLOEXEC) != 0)
	{
		err = "cannot start " + label + ": " + strerror (errno);
		close (in);
		return false;
	}
	if (pipe2 (failed, O_CLOEXEC) != 0)
	{
		err = "cannot start " + label + ": " + strerror (errno);
		close (in); close (out[0]); close (out[1]);
		return false;
	}
	if (stdout_text && pipe2 (so, O_CLOEXEC) != 0)
	{
		err = "cannot start " + label + ": " + strerror (errno);
		close (in); close (out[0]); close (out[1]); close (failed[0]); close (failed[1]);
		return false;
	}

	pid_t pid = fork();
	if (pid == 0)
	{
		setpgid (0, 0);
		dup2 (in, 0);
		dup2 (stdout_text ? so[1] : out[1], 1);
		dup2 (out[1], 2);
		dup2 (failed[1], 3);
		fcntl (3, F_SETFD, FD_CLOEXEC);
		close_from (4);
		signal (SIGPIPE, SIG_DFL);
		execvp (argv[0], argv.data());
		int e = errno;
		if (write (3, &e, sizeof (e))) {}
		_exit (127);
	}
	int fork_errno = errno;
	close (in);
	close (out[1]);
	close (failed[1]);
	if (so[1] >= 0) close (so[1]);
	if (pid < 0)
	{
		err = "cannot start " + label + ": " + strerror (fork_errno);
		close (out[0]); close (failed[0]);
		if (so[0] >= 0) close (so[0]);
		return false;
	}
	setpgid (pid, pid);		// also here, in case the parent gets there first

	// The program's output, until it closes it or takes too long.
	std::string output;
	time_t start = time (NULL);
	bool timed_out = false;
	int open_fds = so[0] >= 0 ? 2 : 1;
	for (;;)
	{
		struct pollfd p[2] = { { out[0], POLLIN, 0 }, { so[0], POLLIN, 0 } };
		int r = poll (p, so[0] >= 0 ? 2 : 1, 1000);
		if (r < 0 && errno != EINTR) break;
		for (int k = 0; r > 0 && k < 2; ++k)
		{
			if (p[k].fd < 0 || !(p[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
			char buf[1024];
			ssize_t n = read (p[k].fd, buf, sizeof (buf));
			if (n <= 0)
			{
				close (p[k].fd);
				if (k == 0) out[0] = -1; else so[0] = -1;
				--open_fds;
				continue;
			}
			std::string &dest = k == 0 ? output : *stdout_text;
			if (dest.size() < 16384) dest.append (buf, n);
		}
		if (open_fds == 0) break;
		if (time (NULL) - start > RUN_TIMEOUT)
		{
			kill (-pid, SIGTERM);
			timed_out = true;
			break;
		}
	}
	if (out[0] >= 0) close (out[0]);
	if (so[0] >= 0) close (so[0]);

	int status = 0;
	while (waitpid (pid, &status, 0) < 0 && errno == EINTR) {}

	int exec_errno = 0;
	bool exec_failed = read (failed[0], &exec_errno, sizeof (exec_errno)) == sizeof (exec_errno);
	close (failed[0]);

	if (exec_failed)
	{
		err = "cannot run " + label + ": " + strerror (exec_errno);
		return false;
	}
	if (timed_out)
	{
		err = label + " did not finish within " + std::to_string (RUN_TIMEOUT / 60) + " minutes and was stopped";
		return false;
	}
	if (!WIFEXITED (status) || WEXITSTATUS (status) != 0)
	{
		err = label + (WIFEXITED (status) ? " failed (exit status " + std::to_string (WEXITSTATUS (status)) + ")"
										  : std::string (" was killed by signal ") + std::to_string (WTERMSIG (status)));
		std::string o = short_output (output);
		if (!o.empty()) err += ":\n" + o;
		return false;
	}
	return true;
}

bool send_message_pipe (const char *program, WordList *HdrNames, WordList *HdrValues,
						const char *mailfrom, WordList *Recipients, const char *body,
						std::string &err)
{
	std::string path = program && *program ? program : "/usr/sbin/sendmail";
	if (path.compare (0, 2, "~/") == 0 && getenv ("HOME")) path = std::string (getenv ("HOME")) + path.substr (1);
	if (!Recipients || Recipients->Count == 0)
	{
		err = "the message has no recipients";
		return false;
	}

	// The message, in memory.
	std::string message = build_message (HdrNames, HdrValues, body);
	int in = open_message_store ();
	if (in < 0)
	{
		err = std::string ("cannot hold the message: ") + strerror (errno);
		return false;
	}
	for (size_t done = 0; done < message.size(); )
	{
		ssize_t n = write (in, message.data() + done, message.size() - done);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0)
		{
			err = std::string ("cannot hold the message: ") + strerror (errno);
			close (in);
			return false;
		}
		done += n;
	}
	lseek (in, 0, SEEK_SET);

	std::vector<std::string> args = { path, "-i", "-f", mailfrom ? mailfrom : "", "--" };
	for (int i = 0; i < Recipients->Count; ++i) args.push_back (Recipients->Get (i));
	return run_program (args, in, path, err);
}

bool run_password_command (const char *command, std::string &password, std::string &err)
{
	int in = open ("/dev/null", O_RDONLY | O_CLOEXEC);
	if (in < 0)
	{
		err = std::string ("cannot open /dev/null: ") + strerror (errno);
		return false;
	}
	std::string out;
	if (!run_program ({ "/bin/sh", "-c", command }, in, "the password command", err, &out)) return false;
	// The first line, as pass(1) and secret-tool print it.
	password = out.substr (0, out.find ('\n'));
	if (!password.empty() && password.back() == '\r') password.pop_back();
	if (password.empty())
	{
		err = "the password command printed no password";
		return false;
	}
	return true;
}

bool run_fetch_command (const char *command, std::string &err)
{
	int in = open ("/dev/null", O_RDONLY | O_CLOEXEC);
	if (in < 0)
	{
		err = std::string ("cannot open /dev/null: ") + strerror (errno);
		return false;
	}
	return run_program ({ "/bin/sh", "-c", command }, in, command, err);
}
