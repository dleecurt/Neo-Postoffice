//===============================================================================
// LocalMailHandler (Neo-PostOffice)
// ---------------------------------
// What Maildir and MH mailboxes share: mailboxes kept as files on this
// computer, one file per message, with real folders.
//
// - A check reads the whole mailbox (whatever folder is selected): folders
//   are created as they appear on disk and removed when they disappear;
//   new messages are added, messages that are gone are removed, and the
//   read state and flags are taken from the files. The files decide: every
//   change made here is written to them at once, so a difference means
//   another program changed them.
// - Opening a message marks it seen in the mailbox; flags (flagged,
//   replied, passed) are kept there too.
//
// The formats (maildirhandler.cpp, mhhandler.cpp) list the messages with
// their flags as Maildir letters (S, F, R, P), and do the file changes.
//
// GNU LGPL like PostOffice itself.
//===============================================================================

#include "mailbox.h"
#include "mailheaders.h"
#include "var.h"
#include "status.h"

#include <errno.h>
#include <fcntl.h>
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

void LocalMailHandler::Init (MailBox *_Parent, Var *_Parameters)
{
	Parameters = new Var (_Parameters);
	Parent = _Parent;
	FoldersChanged = false;
	ReportedMissing = false;
}

void LocalMailHandler::Error (const std::string &msg)
{
	Parent->ToUI->PostMessage (CLASS_ERROR, SUB_ERROR_CRITICAL, Kind(), msg.c_str());
}

// Writes data to a new file (which must not exist), synced to disk.
bool LocalMailHandler::WriteFile (const std::string &path, const std::string &data, std::string &err)
{
	int fd = open (path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
	{
		err = "cannot create " + path + ": " + strerror (errno);
		return false;
	}
	size_t done = 0;
	while (done < data.size())
	{
		ssize_t w = write (fd, data.data() + done, data.size() - done);
		if (w < 0)
		{
			if (errno == EINTR) continue;
			err = "cannot write " + path + ": " + strerror (errno);
			close (fd);
			unlink (path.c_str());
			return false;
		}
		done += (size_t) w;
	}
	if (fsync (fd) != 0 || close (fd) != 0)
	{
		err = "cannot write " + path + ": " + strerror (errno);
		unlink (path.c_str());
		return false;
	}
	return true;
}

bool LocalMailHandler::MarkSeen (Entry &f)
{
	return ChangeFlags (f, "S", "");
}

// Copies the F, R and P flags of the file to the message's envelope.
// Returns true if they changed.
bool LocalMailHandler::ApplyFlags (MailMessage *M, const Entry &f)
{
	int flags = 0;
	if (f.Flags.find ('F') != std::string::npos) flags |= EFLAG_FLAGGED;
	if (f.Flags.find ('R') != std::string::npos) flags |= EFLAG_REPLIED;
	if (f.Flags.find ('P') != std::string::npos) flags |= EFLAG_PASSED;
	if (M->Envelope->GetInt (ENVELOPE_I_FLAGS) == flags) return false;
	M->Envelope->SetInt (ENVELOPE_I_FLAGS, flags);
	return true;
}

void LocalMailHandler::CheckGroup (const char */*groupname*/)
{
	int jid = JOBQ.NewJob (Parent->Name);
	JOBQ.Stat (jid, "Reading mail folders...");
	JOBQ.Progress (jid, 10);
	Parent->ToUI->PostMessage (CLASS_STATUS, NO_SUBCLASS, "");

	if (!Scan())
	{
		if (!ReportedMissing)
		{
			Error (MissingText());
			ReportedMissing = true;
		}
		JOBQ.EndJob (jid);
		return;
	}
	ReportedMissing = false;

	// 1. Folders (empty ones too), before any message is delivered into them.
	FoldersChanged = false;
	for (const std::string &name : Folders)
		if (Parent->EnsureFolder (name)) FoldersChanged = true;

	//    Folders this mailbox made whose folder is gone from disk. The
	//    window removes them (after the messages in them, below).
	std::vector<std::string> gone_folders;
	Parent->VLock->LockR();
	for (MailView *V = Parent->VFirst; V; V = V->Next)
		if (Parent->IsAutoFolder (V->Name) &&
			std::find (Folders.begin(), Folders.end(), std::string (V->Name)) == Folders.end())
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
			// The files decide: changes made here are written at once, so a
			// difference means another program changed them.
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
		M->Envelope->Set (ENVELOPE_GROUPS,   f.Folder.c_str());
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
LocalMailHandler::Entry *LocalMailHandler::Locate (const char *msgid)
{
	auto it = Files.find (msgid);
	if (it != Files.end() && access (Path (it->second).c_str(), F_OK) == 0) return &it->second;
	Scan();
	it = Files.find (msgid);
	return it == Files.end() ? NULL : &it->second;
}

void LocalMailHandler::GetBody (char *msgid)
{
	MailMessage *M = Parent->Find (msgid);
	if (!M)
	{
		Error ("Message not found in internal database");
		return;
	}
	std::string text, flags;
	if (!ExportRaw (msgid, text, flags))
	{
		Error ("This message is no longer in the mailbox.");
		return;
	}
	// A NUL byte in the message would end it early.
	std::vector<char> buf (text.begin(), text.end());
	buf.push_back (0);
	M->ProcessMessage (buf.data());
	if (Entry *f = Locate (msgid)) MarkSeen (*f);
}

void LocalMailHandler::MarkRead (char *msgid)
{
	if (Entry *f = Locate (msgid)) MarkSeen (*f);
}

void LocalMailHandler::SetFlag (char *msgid, char flag, bool on)
{
	Entry *f = Locate (msgid);
	if (!f) return;
	std::string letter (1, flag);
	if (!ChangeFlags (*f, on ? letter : "", on ? "" : letter))
	{
		Error ("Cannot change the flags of " + Path (*f) + ": " + strerror (errno));
		return;
	}
	if (MailMessage *M = Parent->Find (msgid))
	{
		bool changed = ApplyFlags (M, *f);
		if (flag == 'S' && M->Envelope->GetInt (ENVELOPE_I_READ) != (on ? 1 : 0))
		{
			M->Envelope->SetInt (ENVELOPE_I_READ, on ? 1 : 0);
			changed = true;
		}
		if (changed) Parent->ToUI->PostMessage (CLASS_NEWFOLDER, NO_SUBCLASS, Parent->Name, "");
	}
}

bool LocalMailHandler::ExportRaw (char *msgid, std::string &raw, std::string &flags)
{
	Entry *f = Locate (msgid);
	if (!f) return false;
	FILE *F = fopen (Path (*f).c_str(), "rb");
	if (!F) return false;
	raw.clear();
	char buf[65536];
	size_t n;
	while ((n = fread (buf, 1, sizeof (buf), F)) > 0) raw.append (buf, n);
	fclose (F);
	flags = f->Flags;
	return true;
}
