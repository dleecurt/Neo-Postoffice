//===============================================================================
// MaildirHandler (Neo-PostOffice); the shared part is in localmailhandler.cpp
// -------------------------------
// A mailbox that reads a Maildir (https://cr.yp.to/proto/maildir.html),
// for example ~/Maildir filled by getmail or a local mail server, with
// Maildir++ subfolders (.Work, .Lists.2025, as Dovecot and Courier use).
//
// - The top-level Maildir is the folder "Inbox"; each subfolder becomes a
//   folder ("Lists.2025" is shown as "Lists/2025") as soon as it is seen.
// - A message is known by its folder and the unique part of its file name
//   (before ":2,"), so it stays the same message when another program
//   renames it to change its flags.
// - Changes go both ways: opening a message gives it the S (seen) flag and
//   moves it from new/ to cur/; deleting it here removes the file; a message
//   read or deleted by another program is read or gone here after the next
//   check. Messages with the T (trashed) flag are not shown.
// - Flags F (flagged), R (replied) and P (passed/forwarded) are kept in the
//   file name too, and messages can be moved between folders (the file
//   moves). A folder that disappears from disk is removed from the list.
//
// GNU LGPL like PostOffice itself.
//===============================================================================

#include "mailbox.h"
#include "mailheaders.h"
#include "var.h"
#include "status.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "debug.h"

extern JobList JOBQ;

static bool is_dir (const std::string &path)
{
	struct stat st;
	return stat (path.c_str(), &st) == 0 && S_ISDIR (st.st_mode);
}

static bool is_maildir (const std::string &path)
{
	return is_dir (path + "/cur") && is_dir (path + "/new");
}

// The unique part of a Maildir file name and its flags ("2,FS" -> "FS").
static void split_filename (const std::string &file, std::string &uniq, std::string &flags)
{
	size_t colon = file.find (':');
	uniq = file.substr (0, colon);
	flags.clear();
	if (colon != std::string::npos && file.compare (colon + 1, 2, "2,") == 0)
		flags = file.substr (colon + 3);
}

MaildirHandler::MaildirHandler (MailBox *_Parent, class Var *_Parameters)
{
	Init (_Parent, _Parameters);
}

MaildirHandler::~MaildirHandler (void)
{
}

std::string MaildirHandler::MissingText (void)
{
	return "\"" + Root() + "\" is not a Maildir (it needs cur and new folders).";
}

// Folder names: no "." (the Maildir++ separator).
std::string MaildirHandler::CheckFolderName (const std::string &name)
{
	if (name.find ('.') != std::string::npos) return "Maildir folder names cannot contain a dot.";
	return "";
}

bool MaildirHandler::IsFixedFolder (const char *name)
{
	return strcmp (name, "Inbox") == 0;
}

std::string MaildirHandler::Root (void)
{
	std::string path = Parameters->Get_char ("path");
	if (path.empty()) path = "~/Maildir";
	if (path[0] == '~' && (path.size() == 1 || path[1] == '/') && getenv ("HOME"))
		path = std::string (getenv ("HOME")) + path.substr (1);
	while (path.size() > 1 && path.back() == '/') path.pop_back();
	return path;
}

// "Lists.2025" -> "Lists/2025"; the top level is "Inbox".
static std::string folder_name (const std::string &dir)
{
	if (dir.empty()) return "Inbox";
	std::string name = dir.substr (1);
	std::replace (name.begin(), name.end(), '.', '/');
	return name;
}

// The inverse: "Inbox" -> "", "Lists/2025" -> ".Lists.2025".
static std::string folder_dir (const std::string &name)
{
	if (name == "Inbox") return "";
	std::string dir = "." + name;
	std::replace (dir.begin(), dir.end(), '/', '.');
	return dir;
}

std::string MaildirHandler::MsgID (const std::string &dir, const std::string &uniq)
{
	return "<" + (dir.empty() ? std::string ("INBOX") : dir) + "/" + uniq + "@maildir>";
}

// Lists all messages of the Maildir and its subfolders into Files.
bool MaildirHandler::Scan (void)
{
	std::string root = Root();
	Files.clear();
	Dirs.clear();
	Folders.clear();
	if (!is_maildir (root)) return false;

	std::vector<std::string> dirs;
	dirs.push_back ("");
	if (DIR *d = opendir (root.c_str()))
	{
		while (struct dirent *e = readdir (d))
		{
			std::string n = e->d_name;
			if (n.size() > 1 && n[0] == '.' && n != ".." && is_maildir (root + "/" + n))
				dirs.push_back (n);
		}
		closedir (d);
	}
	std::sort (dirs.begin() + 1, dirs.end());
	Dirs = dirs;
	Folders.clear();
	for (const std::string &dir : dirs) Folders.push_back (folder_name (dir));

	for (const std::string &dir : dirs)
	{
		for (const char *sub : { "new", "cur" })
		{
			std::string path = root + (dir.empty() ? "" : "/" + dir) + "/" + sub;
			DIR *d = opendir (path.c_str());
			if (!d) continue;
			while (struct dirent *e = readdir (d))
			{
				if (e->d_name[0] == '.') continue;
				Entry f;
				f.Folder = folder_name (dir);
				f.Dir = dir;
				f.Sub = sub;
				f.File = e->d_name;
				std::string uniq;
				split_filename (f.File, uniq, f.Flags);
				if (f.Flags.find ('T') != std::string::npos) continue;	// trashed
				Files[MsgID (dir, uniq)] = f;
			}
			closedir (d);
		}
	}
	return true;
}

std::string MaildirHandler::Path (const Entry &f)
{
	return Root() + (f.Dir.empty() ? "" : "/" + f.Dir) + "/" + f.Sub + "/" + f.File;
}

// Changes a message's flags by renaming its file: new/uniq -> cur/uniq:2,FLAGS
// or cur/uniq:2,OLD -> cur/uniq:2,NEW (flags in ASCII order, as the format
// wants). Files with flags always live in cur/.
bool MaildirHandler::ChangeFlags (Entry &f, const std::string &add, const std::string &remove)
{
	std::string uniq, flags;
	split_filename (f.File, uniq, flags);
	flags += add;
	for (char c : remove) flags.erase (std::remove (flags.begin(), flags.end(), c), flags.end());
	std::sort (flags.begin(), flags.end());
	flags.erase (std::unique (flags.begin(), flags.end()), flags.end());
	if (f.Sub == "cur" && flags == f.Flags) return true;		// nothing to do

	Entry n = f;
	n.Sub = "cur";
	n.File = uniq + ":2," + flags;
	n.Flags = flags;
	if (rename (Path (f).c_str(), Path (n).c_str()) != 0)
	{
		dprintf ("[maildir] cannot rename %s: %s\n", Path (f).c_str(), strerror (errno));
		return false;
	}
	f = n;
	return true;
}

// Stores a message in a folder of this Maildir the way the format wants:
// written under tmp/ with a unique name, then renamed into new/ (no flags)
// or cur/ (with flags). Uses only the file system and the settings, so
// other mailboxes' threads may call it.
bool MaildirHandler::DeliverRaw (const std::string &raw, const char *folder, const std::string &flags_in,
								 std::string &err)
{
	static std::atomic<unsigned> counter (0);
	std::string root = Root();
	std::string dir = folder_dir (folder);
	std::string base = root + (dir.empty() ? "" : "/" + dir);
	if (!is_maildir (base))
	{
		err = std::string ("there is no Maildir folder \"") + folder + "\"";
		return false;
	}

	char host[256] = "localhost";
	gethostname (host, sizeof (host) - 1);
	std::string h;
	for (const char *p = host; *p; ++p)
	{
		if (*p == '/') h += "\\057";
		else if (*p == ':') h += "\\072";
		else h += *p;
	}
	struct timeval tv;
	gettimeofday (&tv, NULL);
	char name[512];
	snprintf (name, sizeof (name), "%ld.M%ldP%dQ%u.%s", (long) tv.tv_sec, (long) tv.tv_usec,
			  (int) getpid(), ++counter, h.c_str());

	std::string flags;
	for (char c : flags_in) if (c >= 'A' && c <= 'Z' && c != 'T') flags += c;
	std::sort (flags.begin(), flags.end());
	flags.erase (std::unique (flags.begin(), flags.end()), flags.end());

	std::string tmp = base + "/tmp/" + name;
	std::string final_path = flags.empty() ? base + "/new/" + name : base + "/cur/" + name + ":2," + flags;
	if (!WriteFile (tmp, raw, err)) return false;
	if (rename (tmp.c_str(), final_path.c_str()) != 0)
	{
		err = "cannot store " + final_path + ": " + strerror (errno);
		unlink (tmp.c_str());
		return false;
	}
	return true;
}

static int remove_entry (const char *path, const struct stat *, int, struct FTW *)
{
	return remove (path);
}

static bool make_maildir (const std::string &path)
{
	if (!is_dir (path) && mkdir (path.c_str(), 0700) != 0) return false;
	for (const char *sub : { "cur", "new", "tmp" })
	{
		std::string p = path + "/" + sub;
		if (!is_dir (p) && mkdir (p.c_str(), 0700) != 0) return false;
	}
	// Maildir++ marks subfolders with an empty "maildirfolder" file.
	if (FILE *F = fopen ((path + "/maildirfolder").c_str(), "a")) fclose (F);
	return true;
}

// Folder management (the window asks for names and confirmation):
//   create NAME          make .NAME (and missing parents for "A/B")
//   rename OLD NEW       rename .OLD and its subfolders .OLD.* to .NEW(.*)
//   delete NAME          move the messages of .NAME and .NAME.* to .Trash,
//                        then remove those folders (deleting Trash itself
//                        deletes its messages)
void MaildirHandler::FolderOp (const char *op, const char *a, const char *b)
{
	std::string root = Root();
	std::string error;
	Scan();

	if (strcmp (op, "create") == 0)
	{
		std::string dir = folder_dir (a);
		// parents first: ".Lists" for ".Lists.2025"
		for (size_t dot = dir.find ('.', 1); dot != std::string::npos; dot = dir.find ('.', dot + 1))
			if (!make_maildir (root + "/" + dir.substr (0, dot)))
				error = "Cannot create the folder " + dir.substr (1, dot - 1) + ": " + strerror (errno);
		if (error.empty() && std::find (Dirs.begin(), Dirs.end(), dir) != Dirs.end())
			error = std::string ("The folder \"") + a + "\" already exists.";
		else if (error.empty() && !make_maildir (root + "/" + dir))
			error = std::string ("Cannot create the folder \"") + a + "\": " + strerror (errno);
	}
	else if (strcmp (op, "rename") == 0)
	{
		std::string from = folder_dir (a), to = folder_dir (b);
		if (from.empty() || to.empty()) error = "The Inbox cannot be renamed.";
		else if (is_dir (root + "/" + to)) error = std::string ("The folder \"") + b + "\" already exists.";
		else
		{
			std::vector<std::string> dirs = Dirs;
			// the folder itself before its subfolders, so names stay valid
			for (const std::string &d : dirs)
			{
				if (d != from && d.compare (0, from.size() + 1, from + ".") != 0) continue;
				std::string nd = to + d.substr (from.size());
				if (rename ((root + "/" + d).c_str(), (root + "/" + nd).c_str()) != 0)
				{
					error = "Cannot rename " + d + " to " + nd + ": " + strerror (errno);
					break;
				}
			}
		}
	}
	else if (strcmp (op, "delete") == 0)
	{
		std::string del = folder_dir (a);
		if (del.empty()) error = "The Inbox cannot be deleted.";
		else
		{
			bool is_trash = del == ".Trash" || del.compare (0, 7, ".Trash.") == 0;
			std::string trash = root + "/.Trash";
			if (!is_trash && !make_maildir (trash))
				error = "Cannot create the Trash folder: " + std::string (strerror (errno));
			std::vector<std::string> dirs = Dirs;
			for (const std::string &d : dirs)
			{
				if (!error.empty()) break;
				if (d != del && d.compare (0, del.size() + 1, del + ".") != 0) continue;
				// messages to the Trash (or gone, for the Trash itself)
				for (const char *sub : { "new", "cur" })
				{
					std::string path = root + "/" + d + "/" + sub;
					DIR *dh = opendir (path.c_str());
					if (!dh) continue;
					std::vector<std::string> files;
					while (struct dirent *e = readdir (dh))
						if (e->d_name[0] != '.') files.push_back (e->d_name);
					closedir (dh);
					for (const std::string &file : files)
					{
						int r = is_trash ? unlink ((path + "/" + file).c_str())
										 : rename ((path + "/" + file).c_str(),
												   (trash + "/" + sub + "/" + file).c_str());
						if (r != 0)
						{
							error = "Cannot move " + path + "/" + file + " to the Trash: " + strerror (errno);
							break;
						}
					}
				}
				if (error.empty() && nftw ((root + "/" + d).c_str(), remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0)
					error = "Cannot remove the folder " + d + ": " + strerror (errno);
			}
		}
	}

	if (!error.empty()) Error (error);
	CheckGroup (NULL);	// show the result: new, renamed and removed folders
}

// Moves the message's file to another folder of this Maildir. The message
// gets a new id there; the window sends a check after the moves, which
// shows the result.
void MaildirHandler::MoveMessage (char *msgid, const char *folder)
{
	Entry *f = Locate (msgid);
	if (!f) return;
	std::string dir = folder_dir (folder);
	if (dir == f->Dir) return;
	if (std::find (Dirs.begin(), Dirs.end(), dir) == Dirs.end())
	{
		Error (std::string ("There is no Maildir folder \"") + folder + "\".");
		return;
	}
	Entry n = *f;
	n.Dir = dir;
	n.Folder = folder_name (dir);
	if (rename (Path (*f).c_str(), Path (n).c_str()) != 0)
	{
		Error ("Cannot move " + Path (*f) + ": " + strerror (errno));
		return;
	}
	std::string uniq, flags;
	split_filename (n.File, uniq, flags);
	Files.erase (msgid);
	Files[MsgID (dir, uniq)] = n;
}

void MaildirHandler::DeleteMessage (char *msgid)
{
	if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y") == 0) return;
	Entry *f = Locate (msgid);
	if (!f) return;		// already gone
	if (unlink (Path (*f).c_str()) != 0 && errno != ENOENT)
	{
		Error ("Cannot delete " + Path (*f) + ": " + strerror (errno));
		return;
	}
	Files.erase (msgid);
}
