//===============================================================================
// MHHandler (Neo-PostOffice)
// --------------------------
// A mailbox that reads MH folders as nmh keeps them:
//
// - The mail directory comes from the nmh profile ($MH or ~/.mh_profile,
//   entry "Path:", relative to the home directory), unless the mailbox
//   settings give one. Every directory in it is a folder ("inbox",
//   "lists/2025"); a message is a file named by its number.
// - Read state is the profile's Unseen-Sequence ("unseen" if there is
//   none); flagged, replied and passed are the sequences "flagged",
//   "replied" and "forwarded". They are kept in each folder's
//   .mh_sequences (or the profile's "mh-sequences" name), which is changed
//   under an fcntl lock, keeping all other sequences as they are.
// - Deleting works like nmh's rmm: the profile's rmmproc is run if there is
//   one, otherwise the file is renamed to ",NUMBER"; the message leaves all
//   sequences.
// - Moving gives the message the next free number in the target folder.
// - A message is known by its file's inode and modification time, so it
//   stays the same message when a folder is renumbered (folder -pack) or its
//   sequences change.
//
// The shared part (checks, reading, flags) is in localmailhandler.cpp.
// GNU LGPL like PostOffice itself.
//===============================================================================

#include "mailbox.h"
#include "var.h"
#include "status.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "debug.h"

static bool is_dir (const std::string &path)
{
	struct stat st;
	return stat (path.c_str(), &st) == 0 && S_ISDIR (st.st_mode);
}

static bool is_number (const char *s)
{
	if (!*s || *s == '0') return false;
	for (; *s; ++s) if (!isdigit ((unsigned char) *s)) return false;
	return true;
}

static std::string trim (const std::string &s)
{
	size_t a = s.find_first_not_of (" \t\r\n"), b = s.find_last_not_of (" \t\r\n");
	return a == std::string::npos ? "" : s.substr (a, b - a + 1);
}

//-------------------------------------------------------------------------------
// The nmh profile
//-------------------------------------------------------------------------------

MHHandler::Profile MHHandler::ReadProfile (void)
{
	Profile p;
	p.Path = "Mail";
	p.Inbox = "inbox";
	p.SeqFile = ".mh_sequences";
	const char *home = getenv ("HOME") ? getenv ("HOME") : "";

	std::string file = getenv ("MH") ? getenv ("MH") : std::string (home) + "/.mh_profile";
	if (FILE *F = fopen (file.c_str(), "r"))
	{
		char line[4096];
		std::string key, value;
		auto take = [&]()
		{
			if (key.empty()) return;
			std::string v = trim (value);
			if (strcasecmp (key.c_str(), "Path") == 0 && !v.empty()) p.Path = v;
			else if (strcasecmp (key.c_str(), "Inbox") == 0 && !v.empty()) p.Inbox = v;
			else if (strcasecmp (key.c_str(), "rmmproc") == 0) p.Rmmproc = v;
			else if (strcasecmp (key.c_str(), "mh-sequences") == 0 && !v.empty()) p.SeqFile = v;
			else if (strcasecmp (key.c_str(), "Unseen-Sequence") == 0)
			{
				for (char &c : v) if (c == ',') c = ' ';
				size_t pos = 0;
				while ((pos = v.find_first_not_of (' ', pos)) != std::string::npos)
				{
					size_t end = v.find (' ', pos);
					p.Unseen.push_back (v.substr (pos, end - pos));
					pos = end;
				}
			}
		};
		while (fgets (line, sizeof (line), F))
		{
			if ((line[0] == ' ' || line[0] == '\t') && !key.empty()) { value += line; continue; }
			take();
			char *colon = strchr (line, ':');
			if (!colon) { key.clear(); continue; }
			key = trim (std::string (line, colon - line));
			value = colon + 1;
		}
		take();
		fclose (F);
	}
	if (p.Unseen.empty()) p.Unseen.push_back ("unseen");
	if (p.Path[0] != '/') p.Path = std::string (home) + "/" + p.Path;
	while (p.Path.size() > 1 && p.Path.back() == '/') p.Path.pop_back();
	return p;
}

std::string MHHandler::Root (void)
{
	std::string path = Parameters->Get_char ("path");
	if (path.empty()) return ReadProfile().Path;
	if (path[0] == '~' && (path.size() == 1 || path[1] == '/') && getenv ("HOME"))
		path = std::string (getenv ("HOME")) + path.substr (1);
	while (path.size() > 1 && path.back() == '/') path.pop_back();
	return path;
}

MHHandler::MHHandler (MailBox *_Parent, class Var *_Parameters)
{
	Init (_Parent, _Parameters);
}

MHHandler::~MHHandler (void)
{
}

std::string MHHandler::MissingText (void)
{
	return "The MH mail directory \"" + Root() + "\" does not exist (see Path: in ~/.mh_profile).";
}

// Folder names: no part may start with . , or # (those are MH's own
// files). A name made of digits is allowed (it fails if a message has that
// number).
std::string MHHandler::CheckFolderName (const std::string &name)
{
	size_t pos = 0;
	while (pos <= name.size())
	{
		size_t end = name.find ('/', pos);
		std::string part = name.substr (pos, end == std::string::npos ? std::string::npos : end - pos);
		if (part.empty() || part[0] == '.' || part[0] == ',' || part[0] == '#')
			return "MH folder names cannot start with . , or #";
		if (end == std::string::npos) break;
		pos = end + 1;
	}
	return "";
}

bool MHHandler::IsFixedFolder (const char *name)
{
	return ReadProfile().Inbox == name;
}

//-------------------------------------------------------------------------------
// Sequences (.mh_sequences)
//-------------------------------------------------------------------------------

typedef std::map<std::string, std::set<int>> Sequences;

// "unseen: 1-3 7" lines, with continuation lines; the order of the names
// is kept so that the file is written back as it was.
static void parse_sequences (const std::string &text, Sequences &seqs, std::vector<std::string> &order)
{
	std::string name, value;
	auto take = [&]()
	{
		if (name.empty()) return;
		if (!seqs.count (name)) order.push_back (name);
		std::set<int> &set = seqs[name];
		for (char &c : value) if (c == ',' || c == '\t' || c == '\n' || c == '\r') c = ' ';
		size_t pos = 0;
		while ((pos = value.find_first_not_of (' ', pos)) != std::string::npos)
		{
			size_t end = value.find (' ', pos);
			std::string tok = value.substr (pos, end - pos);
			pos = end;
			int a = 0, b = 0;
			if (sscanf (tok.c_str(), "%d-%d", &a, &b) == 2)
				for (int n = a; n <= b && n - a < 1000000; ++n) set.insert (n);
			else if (sscanf (tok.c_str(), "%d", &a) == 1) set.insert (a);
		}
	};
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t nl = text.find ('\n', pos);
		std::string line = text.substr (pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = nl == std::string::npos ? text.size() : nl + 1;
		if ((line[0] == ' ' || line[0] == '\t') && !name.empty()) { value += " " + line; continue; }
		take();
		size_t colon = line.find (':');
		if (colon == std::string::npos) { name.clear(); continue; }
		name = trim (line.substr (0, colon));
		value = line.substr (colon + 1);
	}
	take();
}

static std::string format_sequences (const Sequences &seqs, const std::vector<std::string> &order)
{
	std::string out;
	for (const std::string &name : order)
	{
		const std::set<int> &set = seqs.at (name);
		if (set.empty()) continue;			// nmh leaves out empty sequences
		out += name + ":";
		for (auto it = set.begin(); it != set.end(); )
		{
			int a = *it, b = a;
			while (++it != set.end() && *it == b + 1) b = *it;
			out += " " + std::to_string (a);
			if (b > a) out += "-" + std::to_string (b);
		}
		out += "\n";
	}
	return out;
}

static std::string read_whole (int fd)
{
	std::string text;
	char buf[8192];
	ssize_t n;
	lseek (fd, 0, SEEK_SET);
	while ((n = read (fd, buf, sizeof (buf))) > 0) text.append (buf, n);
	return text;
}

// Reads a folder's sequences (without changing them).
static void load_sequences (const std::string &file, Sequences &seqs)
{
	std::vector<std::string> order;
	int fd = open (file.c_str(), O_RDONLY);
	if (fd < 0) return;
	struct flock fl = {};
	fl.l_type = F_RDLCK;
	fl.l_whence = SEEK_SET;
	fcntl (fd, F_SETLKW, &fl);
	parse_sequences (read_whole (fd), seqs, order);
	close (fd);
}

// Changes a folder's sequences under an fcntl lock (as nmh does):
// change (seqs) is called with the sequences, and the file is written back
// if it returns true. The other sequences are kept as they were.
template <class F>
static bool change_sequences (const std::string &file, F change, std::string &err)
{
	int fd = open (file.c_str(), O_RDWR | O_CREAT, 0600);
	if (fd < 0)
	{
		err = "cannot open " + file + ": " + strerror (errno);
		return false;
	}
	struct flock fl = {};
	fl.l_type = F_WRLCK;
	fl.l_whence = SEEK_SET;
	if (fcntl (fd, F_SETLKW, &fl) != 0)
	{
		err = "cannot lock " + file + ": " + strerror (errno);
		close (fd);
		return false;
	}
	Sequences seqs;
	std::vector<std::string> order;
	parse_sequences (read_whole (fd), seqs, order);
	bool ok = true;
	if (change (seqs))
	{
		for (auto &kv : seqs)
			if (std::find (order.begin(), order.end(), kv.first) == order.end()) order.push_back (kv.first);
		std::string out = format_sequences (seqs, order);
		if (ftruncate (fd, 0) != 0 || lseek (fd, 0, SEEK_SET) != 0 ||
			write (fd, out.data(), out.size()) != (ssize_t) out.size() || fsync (fd) != 0)
		{
			err = "cannot write " + file + ": " + strerror (errno);
			ok = false;
		}
	}
	close (fd);		// releases the lock
	return ok;
}

//-------------------------------------------------------------------------------
// Scanning
//-------------------------------------------------------------------------------

// All folders below the mail directory, by relative name.
static void find_folders (const std::string &root, const std::string &rel, std::vector<std::string> &out)
{
	std::string path = rel.empty() ? root : root + "/" + rel;
	DIR *d = opendir (path.c_str());
	if (!d) return;
	std::vector<std::string> subs;
	while (struct dirent *e = readdir (d))
	{
		const char *n = e->d_name;
		// Only directories count: one named by a number (a year, say) is a
		// folder too, since messages are always plain files.
		if (n[0] == '.' || n[0] == ',' || n[0] == '#') continue;
		std::string r = rel.empty() ? n : rel + "/" + n;
		if (is_dir (root + "/" + r)) subs.push_back (r);
	}
	closedir (d);
	std::sort (subs.begin(), subs.end());
	for (const std::string &r : subs)
	{
		out.push_back (r);
		find_folders (root, r, out);
	}
}

bool MHHandler::Scan (void)
{
	Profile p = ReadProfile();
	std::string root = Root();
	Files.clear();
	Folders.clear();
	if (!is_dir (root)) return false;

	find_folders (root, "", Folders);
	for (const std::string &folder : Folders)
	{
		std::string dir = root + "/" + folder;
		Sequences seqs;
		load_sequences (dir + "/" + p.SeqFile, seqs);
		const std::set<int> &unseen = seqs[p.Unseen[0]];
		const std::set<int> &flagged = seqs["flagged"], &replied = seqs["replied"], &passed = seqs["forwarded"];

		DIR *d = opendir (dir.c_str());
		if (!d) continue;
		while (struct dirent *e = readdir (d))
		{
			if (!is_number (e->d_name)) continue;
			struct stat st;
			std::string path = dir + "/" + e->d_name;
			if (stat (path.c_str(), &st) != 0 || !S_ISREG (st.st_mode)) continue;
			int n = atoi (e->d_name);

			Entry f;
			f.Folder = folder;
			f.Dir = folder;
			f.File = e->d_name;
			bool seen = true;
			for (const std::string &u : p.Unseen) if (seqs[u].count (n)) seen = false;
			(void) unseen;
			if (flagged.count (n)) f.Flags += 'F';
			if (passed.count (n)) f.Flags += 'P';
			if (replied.count (n)) f.Flags += 'R';
			if (seen) f.Flags += 'S';

			char id[512];
			snprintf (id, sizeof (id), "<%s/%lu.%ld@mh>", folder.c_str(), (unsigned long) st.st_ino,
					  (long) st.st_mtime);
			Files[id] = f;
		}
		closedir (d);
	}
	return true;
}

std::string MHHandler::Path (const Entry &f)
{
	return Root() + "/" + f.Dir + "/" + f.File;
}

// Flags are sequences: S is "not in the unseen sequence(s)", F flagged,
// R replied, P forwarded.
bool MHHandler::ChangeFlags (Entry &f, const std::string &add, const std::string &remove)
{
	Profile p = ReadProfile();
	int n = atoi (f.File.c_str());
	std::string err;
	auto seq_of = [&](char c) -> std::string
	{
		switch (c)
		{
			case 'F': return "flagged";
			case 'R': return "replied";
			case 'P': return "forwarded";
			default:  return "";
		}
	};
	bool ok = change_sequences (Root() + "/" + f.Dir + "/" + p.SeqFile, [&](Sequences &seqs)
	{
		bool changed = false;
		for (char c : add)
		{
			if (c == 'S') { for (const std::string &u : p.Unseen) changed |= seqs[u].erase (n) > 0; }
			else if (!seq_of (c).empty()) changed |= seqs[seq_of (c)].insert (n).second;
		}
		for (char c : remove)
		{
			if (c == 'S') changed |= seqs[p.Unseen[0]].insert (n).second;
			else if (!seq_of (c).empty()) changed |= seqs[seq_of (c)].erase (n) > 0;
		}
		return changed;
	}, err);
	if (!ok)
	{
		errno = EIO;
		dprintf ("[mh] %s\n", err.c_str());
		return false;
	}
	std::string flags = f.Flags + add;
	for (char c : remove) flags.erase (std::remove (flags.begin(), flags.end(), c), flags.end());
	std::sort (flags.begin(), flags.end());
	flags.erase (std::unique (flags.begin(), flags.end()), flags.end());
	f.Flags = flags;
	return true;
}

//-------------------------------------------------------------------------------
// Storing, moving, deleting
//-------------------------------------------------------------------------------

int MHHandler::NextNumber (const std::string &dir)
{
	int last = 0;
	if (DIR *d = opendir (dir.c_str()))
	{
		while (struct dirent *e = readdir (d))
		{
			struct stat st;
			if (is_number (e->d_name) && stat ((dir + "/" + e->d_name).c_str(), &st) == 0 && S_ISREG (st.st_mode))
				last = std::max (last, atoi (e->d_name));
		}
		closedir (d);
	}
	return last + 1;
}

// Puts a message into a folder under the next free number (link() never
// overwrites, so a number taken meanwhile by inc is skipped), and adds it
// to the sequences its flags call for.
bool MHHandler::Store (const std::string &folder, const std::string &raw, const std::string &flags,
					   std::string &err)
{
	static std::atomic<unsigned> counter (0);
	Profile p = ReadProfile();
	std::string dir = Root() + "/" + folder;
	std::string tmp = dir + "/#neo-postoffice." + std::to_string (getpid()) + "." + std::to_string (++counter);
	if (!WriteFile (tmp, raw, err)) return false;

	int n = NextNumber (dir);
	for (int tries = 0; ; ++n, ++tries)
	{
		if (link (tmp.c_str(), (dir + "/" + std::to_string (n)).c_str()) == 0) break;
		if (errno != EEXIST || tries > 1000)
		{
			err = "cannot store message " + std::to_string (n) + " in " + dir + ": " + strerror (errno);
			unlink (tmp.c_str());
			return false;
		}
	}
	unlink (tmp.c_str());

	return change_sequences (dir + "/" + p.SeqFile, [&](Sequences &seqs)
	{
		if (flags.find ('S') == std::string::npos) seqs[p.Unseen[0]].insert (n);
		if (flags.find ('F') != std::string::npos) seqs["flagged"].insert (n);
		if (flags.find ('R') != std::string::npos) seqs["replied"].insert (n);
		if (flags.find ('P') != std::string::npos) seqs["forwarded"].insert (n);
		return true;
	}, err);
}

bool MHHandler::DeliverRaw (const std::string &raw, const char *folder, const std::string &flags,
							std::string &err)
{
	if (!is_dir (Root() + "/" + folder))
	{
		err = std::string ("there is no MH folder \"") + folder + "\"";
		return false;
	}
	return Store (folder, raw, flags, err);
}

// Takes a message out of all sequences of its folder.
static bool forget_in_sequences (const std::string &seqfile, int n, std::string &err)
{
	return change_sequences (seqfile, [&](Sequences &seqs)
	{
		bool changed = false;
		for (auto &kv : seqs) changed |= kv.second.erase (n) > 0;
		return changed;
	}, err);
}

void MHHandler::MoveMessage (char *msgid, const char *folder)
{
	Entry *f = Locate (msgid);
	if (!f || f->Dir == folder) return;
	if (!is_dir (Root() + "/" + folder))
	{
		Error (std::string ("There is no MH folder \"") + folder + "\".");
		return;
	}
	std::string raw, flags, err;
	if (!ExportRaw (msgid, raw, flags) || !Store (folder, raw, flags, err))
	{
		Error ("Cannot move the message: " + (err.empty () ? std::string ("it is gone") : err));
		return;
	}
	// The copy is stored: now the original goes (unlink, not rmm: it moved).
	Profile p = ReadProfile();
	std::string from = Path (*f);
	int n = atoi (f->File.c_str());
	std::string seqfile = Root() + "/" + f->Dir + "/" + p.SeqFile;
	if (unlink (from.c_str()) != 0 && errno != ENOENT)
		Error ("The message was copied, but " + from + " could not be removed: " + strerror (errno));
	if (!forget_in_sequences (seqfile, n, err)) Error (err);
	Files.erase (msgid);
}

// Like nmh's rmm: the profile's rmmproc, or a rename to ",NUMBER".
void MHHandler::DeleteMessage (char *msgid)
{
	if (strcasecmp (Parameters->Get_char ("writeprotect"), "Y") == 0) return;
	Entry *f = Locate (msgid);
	if (!f) return;		// already gone
	Profile p = ReadProfile();
	std::string dir = Root() + "/" + f->Dir;
	int n = atoi (f->File.c_str());

	if (!p.Rmmproc.empty())
	{
		pid_t pid = fork();
		if (pid == 0)
		{
			if (chdir (dir.c_str()) == 0)
				execl ("/bin/sh", "sh", "-c", (p.Rmmproc + " \"$@\"").c_str(), "rmmproc", f->File.c_str(), (char *) NULL);
			_exit (127);
		}
		int status = 0;
		if (pid < 0 || waitpid (pid, &status, 0) < 0 || !WIFEXITED (status) || WEXITSTATUS (status) != 0)
		{
			Error ("rmmproc (" + p.Rmmproc + ") failed for message " + f->File + " in " + dir + ".");
			return;
		}
	}
	else
	{
		std::string backup = dir + "/," + f->File;
		unlink (backup.c_str());
		if (rename ((dir + "/" + f->File).c_str(), backup.c_str()) != 0 && errno != ENOENT)
		{
			Error ("Cannot delete " + dir + "/" + f->File + ": " + strerror (errno));
			return;
		}
	}
	std::string err;
	if (!forget_in_sequences (dir + "/" + p.SeqFile, n, err)) Error (err);
	Files.erase (msgid);
}

//-------------------------------------------------------------------------------
// Folders
//-------------------------------------------------------------------------------

static int remove_entry (const char *path, const struct stat *, int, struct FTW *)
{
	return remove (path);
}

static bool make_dirs (const std::string &root, const std::string &rel)
{
	std::string path = root;
	size_t pos = 0;
	while (pos <= rel.size())
	{
		size_t end = rel.find ('/', pos);
		path += "/" + rel.substr (pos, end == std::string::npos ? std::string::npos : end - pos);
		if (!is_dir (path) && mkdir (path.c_str(), 0700) != 0) return false;
		if (end == std::string::npos) break;
		pos = end + 1;
	}
	return true;
}

// create NAME, rename OLD NEW (subfolders move with it), delete NAME (its
// messages and its subfolders' go to the folder "Trash", then the folders
// are removed; deleting Trash itself deletes its messages).
void MHHandler::FolderOp (const char *op, const char *a, const char *b)
{
	std::string root = Root();
	std::string error;
	Profile p = ReadProfile();

	if (strcmp (op, "create") == 0)
	{
		if (is_dir (root + "/" + a)) error = std::string ("The folder \"") + a + "\" already exists.";
		else if (!make_dirs (root, a)) error = std::string ("Cannot create the folder \"") + a + "\": " + strerror (errno);
	}
	else if (strcmp (op, "rename") == 0)
	{
		std::string to = b;
		size_t slash = to.rfind ('/');
		if (p.Inbox == a) error = "The inbox cannot be renamed.";
		else if (is_dir (root + "/" + to)) error = std::string ("The folder \"") + b + "\" already exists.";
		else if (slash != std::string::npos && !make_dirs (root, to.substr (0, slash)))
			error = "Cannot create the folder " + to.substr (0, slash) + ": " + strerror (errno);
		else if (rename ((root + "/" + a).c_str(), (root + "/" + to).c_str()) != 0)
			error = std::string ("Cannot rename ") + a + " to " + b + ": " + strerror (errno);
	}
	else if (strcmp (op, "delete") == 0)
	{
		std::string del = a;
		if (p.Inbox == del) error = "The inbox cannot be deleted.";
		else
		{
			bool is_trash = del == "Trash" || del.compare (0, 6, "Trash/") == 0;
			if (!is_trash && !make_dirs (root, "Trash"))
				error = "Cannot create the Trash folder: " + std::string (strerror (errno));
			std::vector<std::string> all;
			find_folders (root, "", all);
			for (const std::string &folder : all)
			{
				if (!error.empty()) break;
				if (folder != del && folder.compare (0, del.size() + 1, del + "/") != 0) continue;
				if (is_trash) continue;
				// messages to the Trash, with their flags
				Sequences seqs;
				load_sequences (root + "/" + folder + "/" + p.SeqFile, seqs);
				DIR *d = opendir ((root + "/" + folder).c_str());
				if (!d) continue;
				std::vector<int> numbers;
				while (struct dirent *e = readdir (d))
				{
					struct stat st;
					if (is_number (e->d_name) && stat ((root + "/" + folder + "/" + e->d_name).c_str(), &st) == 0 &&
						S_ISREG (st.st_mode))
						numbers.push_back (atoi (e->d_name));
				}
				closedir (d);
				std::sort (numbers.begin(), numbers.end());
				for (int n : numbers)
				{
					std::string path = root + "/" + folder + "/" + std::to_string (n), raw, flags;
					if (FILE *F = fopen (path.c_str(), "rb"))
					{
						char buf[65536];
						size_t r;
						while ((r = fread (buf, 1, sizeof (buf), F)) > 0) raw.append (buf, r);
						fclose (F);
					}
					bool seen = true;
					for (const std::string &u : p.Unseen) if (seqs[u].count (n)) seen = false;
					if (seen) flags += 'S';
					if (seqs["flagged"].count (n)) flags += 'F';
					if (seqs["replied"].count (n)) flags += 'R';
					if (seqs["forwarded"].count (n)) flags += 'P';
					if (!Store ("Trash", raw, flags, error))
					{
						error = "Cannot move " + path + " to the Trash: " + error;
						break;
					}
				}
			}
			// then the folder with what is left (backups, sequences, subfolders)
			if (error.empty() && nftw ((root + "/" + del).c_str(), remove_entry, 16, FTW_DEPTH | FTW_PHYS) != 0)
				error = "Cannot remove the folder " + del + ": " + strerror (errno);
		}
	}

	if (!error.empty()) Error (error);
	CheckGroup (NULL);	// show the result: new, renamed and removed folders
}
