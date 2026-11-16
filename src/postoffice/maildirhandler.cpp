//===============================================================================
// MaildirHandler (Neo-PostOffice)
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
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
	Parameters = new Var (_Parameters);
	Parent = _Parent;
	FoldersChanged = false;
	ReportedMissing = false;
}

MaildirHandler::~MaildirHandler (void)
{
}

void MaildirHandler::OpenConnection (void)
{
}

void MaildirHandler::CloseConnection (void)
{
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

bool MaildirHandler::MarkSeen (Entry &f)
{
	return ChangeFlags (f, "S", "");
}

// Copies the F, R and P flags of the file to the message's envelope.
// Returns true if they changed.
bool MaildirHandler::ApplyFlags (MailMessage *M, const Entry &f)
{
	int flags = 0;
	if (f.Flags.find ('F') != std::string::npos) flags |= EFLAG_FLAGGED;
	if (f.Flags.find ('R') != std::string::npos) flags |= EFLAG_REPLIED;
	if (f.Flags.find ('P') != std::string::npos) flags |= EFLAG_PASSED;
	if (M->Envelope->GetInt (ENVELOPE_I_FLAGS) == flags) return false;
	M->Envelope->SetInt (ENVELOPE_I_FLAGS, flags);
	return true;
}

void MaildirHandler::CheckGroup (const char */*groupname*/)
{
	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Stat (jid, "Reading Maildir...");
	JOBQ.Progress (jid, 10);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	if (!Scan())
	{
		if (!ReportedMissing)
		{
			std::string msg = "\"" + Root() + "\" is not a Maildir (it needs cur and new folders).";
			Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "maildir", msg.c_str());
			ReportedMissing = true;
		}
		JOBQ.EndJob (jid);
		return;
	}
	ReportedMissing = false;

	// 1. Folders (empty ones too), before any message is delivered into them.
	FoldersChanged = false;
	for (const std::string &dir : Dirs)
		if (Parent->EnsureFolder (folder_name (dir))) FoldersChanged = true;

	//    Folders this mailbox made whose Maildir folder is gone from disk.
	//    The window removes them (after the messages in them, below).
	std::vector<std::string> gone_folders;
	Parent->VLock->LockR();
	for (MailView *V = Parent->VFirst; V; V = V->Next)
		if (Parent->IsAutoFolder (V->Name) &&
			std::find (Dirs.begin(), Dirs.end(), folder_dir (V->Name)) == Dirs.end())
			gone_folders.push_back (V->Name);
	Parent->VLock->UnlockR();

	// 2. Messages that are gone (deleted or moved by another program). The
	//    window removes them, as it does for its own Delete button.
	std::vector<std::string> gone;
	Parent->Lock->LockR();
	for (MailMessage *M = (MailMessage *) Parent->Root->BNext; M && M != Parent->Root;
		 M = (MailMessage *) M->BNext)
		if (!Files.count (M->MsgID)) gone.push_back (M->MsgID);
	Parent->Lock->UnlockR();
	for (const std::string &id : gone)
		Parent->ToUI->PostMessage (CLASS_GONE, NO_SUBCLASS, Parent->Name, id.c_str());
	for (const std::string &name : gone_folders)
		Parent->ToUI->PostMessage (CLASS_FOLDERGONE, NO_SUBCLASS, Parent->Name, name.c_str());
	if (!gone_folders.empty()) FoldersChanged = true;

	// 3. New messages, and the read state and flags of known ones.
	int added = 0, i = 0, changed = 0;
	for (auto &kv : Files)
	{
		const std::string &msgid = kv.first;
		Entry &f = kv.second;
		bool seen = f.Flags.find ('S') != std::string::npos;
		++i;

		if (MailMessage *M = Parent->Find ((char *) msgid.c_str()))
		{
			// The Maildir decides: opening a message here sets its S flag at
			// once, so a difference means another program changed it.
			if (M->Envelope->GetInt (ENVELOPE_I_READ) != (seen ? 1 : 0))
			{
				M->Envelope->SetInt (ENVELOPE_I_READ, seen ? 1 : 0);
				++changed;
			}
			if (ApplyFlags (M, f)) ++changed;
			continue;
		}

		if ((added & 63) == 0)
		{
			std::string st = "Reading message " + std::to_string (i) + " of " + std::to_string (Files.size());
			JOBQ.Progress (jid, 20 + (int) ((70L * i) / (long) Files.size()));
			JOBQ.Stat (jid, st.c_str());
			Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");
		}

		// Read the header only.
		std::string head;
		struct stat st;
		std::string path = Path (f);
		if (FILE *F = fopen (path.c_str(), "rb"))
		{
			char buf[8192];
			size_t n;
			while (head.size() < 262144 && (n = fread (buf, 1, sizeof (buf), F)) > 0)
			{
				head.append (buf, n);
				if (head.find ("\n\n") != std::string::npos || head.find ("\r\n\r\n") != std::string::npos) break;
			}
			fclose (F);
		}
		else continue;	// renamed or removed meanwhile; picked up next time
		time_t delivered = stat (path.c_str(), &st) == 0 ? st.st_mtime : time (NULL);

		std::vector<mailheaders::Header> h = mailheaders::Parse (head);
		std::string from_name, from_addr;
		mailheaders::SplitAddress (mailheaders::Decode (mailheaders::Get (h, "From")), from_name, from_addr);
		time_t sent = mailheaders::ParseDate (mailheaders::Get (h, "Date"));
		if (!sent) sent = delivered;
		char date[64];
		struct tm tmv;
		localtime_r (&sent, &tmv);
		strftime (date, sizeof (date), "%a, %d %b %Y %H:%M:%S %z", &tmv);

		MailMessage *M = new MailMessage (Parent, msgid.c_str());
		M->Envelope->Set (ENVELOPE_NAMEFROM, (from_name.empty() ? from_addr : from_name).c_str());
		M->Envelope->Set (ENVELOPE_MAILFROM, from_addr.c_str());
		M->Envelope->Set (ENVELOPE_SUBJECT,  mailheaders::Decode (mailheaders::Get (h, "Subject")).c_str());
		M->Envelope->Set (ENVELOPE_DATE,     date);
		M->Envelope->Set (ENVELOPE_GROUPS,   folder_name (f.Dir).c_str());
		M->Envelope->SetInt (ENVELOPE_I_READ, seen ? 1 : 0);
		ApplyFlags (M, f);
		M->ReceiveDate = delivered;
		Parent->Deliver (M);
		++added;

		Parent->Lock->LockRW();
		for (int j = 0; j < M->Subscribers->Count(); ++j)
		{
			ViewListNode *VLN = M->Subscribers->Get (j);
			if (VLN) Parent->ToUI->PostMessage (CLASS_NEWARTICLE, NO_SUBCLASS, VLN->View->Name, M->MsgID);
		}
		Parent->Lock->UnlockRW();
	}

	if (added || !gone.empty()) Parent->SaveMailBox();
	// Redraw the folder tree (new folders, message counts) after the window
	// has handled the removals posted above.
	if (FoldersChanged || added || changed || !gone.empty())
		Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	JOBQ.EndJob (jid);
}

// Finds the message's file, rescanning once if another program has renamed
// or moved it since the last check.
MaildirHandler::Entry *MaildirHandler::Locate (const char *msgid)
{
	auto it = Files.find (msgid);
	if (it != Files.end() && access (Path (it->second).c_str(), F_OK) == 0) return &it->second;
	Scan();
	it = Files.find (msgid);
	return it == Files.end() ? NULL : &it->second;
}

void MaildirHandler::GetBody (char *msgid)
{
	MailMessage *M = Parent->Find (msgid);
	if (!M)
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "maildir",
								   "Message not found in internal database");
		return;
	}
	Entry *f = Locate (msgid);
	std::string text;
	if (f)
	{
		if (FILE *F = fopen (Path (*f).c_str(), "rb"))
		{
			char buf[65536];
			size_t n;
			while ((n = fread (buf, 1, sizeof (buf), F)) > 0) text.append (buf, n);
			fclose (F);
		}
		else f = NULL;
	}
	if (!f)
	{
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "maildir",
								   "This message is no longer in the Maildir.");
		return;
	}
	// A NUL byte in the message would end it early.
	std::vector<char> buf (text.begin(), text.end());
	buf.push_back (0);
	M->ProcessMessage (buf.data());
	MarkSeen (*f);
}

void MaildirHandler::MarkRead (char *msgid)
{
	if (Entry *f = Locate (msgid)) MarkSeen (*f);
}

void MaildirHandler::SetFlag (char *msgid, char flag, bool on)
{
	Entry *f = Locate (msgid);
	if (!f) return;
	std::string letter (1, flag);
	if (!ChangeFlags (*f, on ? letter : "", on ? "" : letter))
	{
		std::string msg = "Cannot change the flags of " + Path (*f) + ": " + strerror (errno);
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "maildir", msg.c_str());
		return;
	}
	if (MailMessage *M = Parent->Find (msgid))
		if (ApplyFlags (M, *f))
			Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
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
		std::string msg = std::string ("There is no Maildir folder \"") + folder + "\".";
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "maildir", msg.c_str());
		return;
	}
	Entry n = *f;
	n.Dir = dir;
	if (rename (Path (*f).c_str(), Path (n).c_str()) != 0)
	{
		std::string msg = "Cannot move " + Path (*f) + ": " + strerror (errno);
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "maildir", msg.c_str());
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
		std::string msg = "Cannot delete " + Path (*f) + ": " + strerror (errno);
		Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, "maildir", msg.c_str());
		return;
	}
	Files.erase (msgid);
}

void MaildirHandler::Expire (void)
{
}

int MaildirHandler::PostMessage (WordList */*groups*/, MailMessage */*M*/)
{
	return 0;
}
