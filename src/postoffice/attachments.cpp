//===============================================================================
// Received attachments (Neo-PostOffice)
// -------------------------------------
// Saving and opening the attachments of the message in the view. An
// attachment line in the message view (Fl_View::add_attachment) pops up
// Save as... / Open...; Message -> Save all attachments... saves every one
// into a folder.
//
// - Save as: the app's file requester, starting in the folder used last
//   (the first time ~/Downloads, else the home folder). The name comes from
//   the message, made safe: no folders, no leading dots, no control
//   characters. An existing file is replaced only after asking.
// - Open: always asks first, with a clear warning (a stronger one for
//   programs, scripts and other files that can run code). The file is
//   written to a private folder (~/.postoffice/tmp, 0700, file 0600, so
//   never executable) and opened with the command in the preference
//   postoffice/gui/action/attachment ("$" for the file; default
//   "xdg-open $"), the file name passed as a separate argument as for
//   links. The folder is emptied when the program starts and quits.
//===============================================================================

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Menu_.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_draw.H>
#include <string>
#include <algorithm>
#include <vector>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include "mailbox.h"
#include "mailmessage.h"
#include "mailheaders.h"
#include "base64.h"
#include "var.h"
#include "ht.h"

extern Var			 PREFERENCES;
extern MailBox		*currentmailbox;
extern char			 currentmsgid[8][256];
extern int			 mdi_current;
const char			*file_request (const char *title, const char *dir, const char *name, bool folder);
void				 run_with_argument (const char *tmpl, const char *arg);

//-------------------------------------------------------------------------------
// Which parts are attachments, their names and contents
//-------------------------------------------------------------------------------

// The part's type without parameters, lower case.
static std::string bare_type (const MailPart *P)
{
	std::string t = P && P->ContentType ? P->ContentType : "";
	size_t semi = t.find (';');
	if (semi != std::string::npos) t.erase (semi);
	while (!t.empty() && (t.back() == ' ' || t.back() == '\t')) t.pop_back();
	for (char &c : t) c = tolower ((unsigned char) c);
	return t;
}

// An attachment: any part that is not plain text, and plain text that has
// a file name (shown as text, but also a file).
bool part_is_attachment (const MailPart *P)
{
	if (!P) return false;
	return bare_type (P) != "text/plain" || (P->FileName && *P->FileName);
}

// A file name that is safe to create: the last path element, no control
// characters, not starting with a dot or a dash, not too long. Without a
// name: "attachment-N" with an extension for common types.
std::string attachment_file_name (const MailPart *P, int index)
{
	std::string in = P && P->FileName ? P->FileName : "";
	size_t slash = in.find_last_of ("/\\");
	if (slash != std::string::npos) in.erase (0, slash + 1);
	std::string out;
	for (unsigned char c : in) out += (c < 0x20 || c == 0x7f) ? '_' : (char) c;
	while (!out.empty() && (out[0] == '.' || out[0] == '-' || out[0] == ' ')) out.erase (0, 1);
	while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
	if (out.size() > 200)
	{
		size_t dot = out.rfind ('.');
		std::string ext = (dot != std::string::npos && out.size() - dot <= 10) ? out.substr (dot) : "";
		out = out.substr (0, 200 - ext.size()) + ext;
	}
	if (out.empty())
	{
		static const struct { const char *type, *ext; } known[] = {
			{ "text/plain", ".txt" }, { "text/html", ".html" }, { "application/pdf", ".pdf" },
			{ "image/jpeg", ".jpg" }, { "image/png", ".png" }, { "image/gif", ".gif" },
			{ "application/zip", ".zip" }, { "message/rfc822", ".eml" }, { "text/calendar", ".ics" },
		};
		std::string ext;
		for (auto &k : known) if (bare_type (P) == k.type) ext = k.ext;
		out = "attachment-" + std::to_string (index + 1) + ext;
	}
	return out;
}

// The part's bytes, with its transfer encoding (or uuencoding) undone.
bool attachment_bytes (const MailPart *P, std::string &out)
{
	out.clear ();
	if (!P || !P->Contents) return false;
	std::string type = P->ContentType ? P->ContentType : "";
	if (bare_type (P) == "text/uuencoded")
	{
		// "begin 644 name" line, then the data.
		const char *b = strstr (P->Contents, "begin ");
		const char *data = b ? strchr (b, '\n') : NULL;
		if (!data) return false;
		++data;
		unsigned long size = 0;
		unsigned long left = P->ContentSize - (unsigned long) (data - P->Contents);
		char *bytes = (char *) UUdecode ((unsigned char *) data, left, &size);
		if (!bytes) return false;
		out.assign (bytes, size);
		delete[] bytes;
		return true;
	}
	out = mailheaders::DecodeBody (std::string (P->Contents, P->ContentSize), mailheaders::Param (type, "x-neo-cte"));
	return true;
}

// Programs, scripts and other files that can run code when opened.
static bool risky (const std::string &name, const std::string &type)
{
	static const char *exts[] = {
		"exe", "com", "bat", "cmd", "scr", "pif", "msi", "msp", "cpl", "vbs", "vbe", "js", "jse",
		"wsf", "wsh", "ps1", "psm1", "hta", "lnk", "reg", "inf", "sh", "bash", "csh", "ksh", "zsh",
		"fish", "py", "pyc", "pl", "rb", "php", "tcl", "lua", "jar", "class", "apk", "app", "run",
		"bin", "out", "elf", "so", "desktop", "appimage", "deb", "rpm", "pkg", "dmg", "iso", "img",
		"vhd", "docm", "dotm", "xlsm", "xltm", "xlam", "pptm", "potm", "ppam", "command", "action",
		"workflow", "scpt", "service", "url", "webloc", "chm", "msc", "gadget", NULL
	};
	size_t dot = name.rfind ('.');
	if (dot != std::string::npos)
	{
		std::string ext = name.substr (dot + 1);
		for (char &c : ext) c = tolower ((unsigned char) c);
		for (int i = 0; exts[i]; ++i) if (ext == exts[i]) return true;
	}
	static const char *types[] = {
		"application/x-executable", "application/x-sharedlib", "application/x-sh", "application/x-shellscript",
		"application/x-msdownload", "application/x-msdos-program", "application/x-ms-installer",
		"application/x-desktop", "application/java-archive", "application/x-java-archive",
		"application/vnd.android.package-archive", "application/x-perl", "application/x-python",
		"text/x-shellscript", "text/x-python", "text/x-perl", "application/javascript", NULL
	};
	for (int i = 0; types[i]; ++i) if (type == types[i]) return true;
	return false;
}

static std::string size_text (size_t n)
{
	char buf[32];
	if (n < 1024) snprintf (buf, sizeof (buf), "%zu bytes", n);
	else if (n < 1024 * 1024) snprintf (buf, sizeof (buf), "%.1f KB", n / 1024.0);
	else snprintf (buf, sizeof (buf), "%.1f MB", n / (1024.0 * 1024.0));
	return buf;
}

//-------------------------------------------------------------------------------
// Folders
//-------------------------------------------------------------------------------

static std::string home (void)
{
	const char *h = getenv ("HOME");
	return h && *h ? h : "/";
}

static bool is_dir (const std::string &p)
{
	struct stat st;
	return stat (p.c_str(), &st) == 0 && S_ISDIR (st.st_mode);
}

// Where Save starts: the folder used last, else ~/Downloads, else home.
static std::string start_folder (void)
{
	const char *last = PREFERENCES.Get_char_tree ("postoffice/state/attachments/folder");
	if (last && *last && is_dir (last)) return last;
	if (is_dir (home() + "/Downloads")) return home() + "/Downloads";
	return home();
}

static void remember_folder (const std::string &dir)
{
	PREFERENCES.Set_char_tree ("postoffice/state/attachments/folder", (char *) dir.c_str());
}

// Writes the bytes to path (mode as for any new file of yours).
static bool write_file (const std::string &path, const std::string &bytes, std::string &err, mode_t mode = 0666)
{
	int fd = open (path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
	if (fd < 0) { err = strerror (errno); return false; }
	size_t done = 0;
	while (done < bytes.size())
	{
		ssize_t w = write (fd, bytes.data() + done, bytes.size() - done);
		if (w < 0) { if (errno == EINTR) continue; err = strerror (errno); close (fd); return false; }
		done += (size_t) w;
	}
	if (close (fd) != 0) { err = strerror (errno); return false; }
	return true;
}

//-------------------------------------------------------------------------------
// The private folder for opened attachments
//-------------------------------------------------------------------------------

static std::string open_root (void)
{
	return home() + "/.postoffice/tmp";
}

// Removes the opened attachments (each in its own open-XXXXXX folder).
void attachments_cleanup (void)
{
	std::string root = open_root ();
	DIR *D = opendir (root.c_str());
	if (!D) return;
	while (struct dirent *e = readdir (D))
	{
		if (strncmp (e->d_name, "open-", 5) != 0) continue;
		std::string dir = root + "/" + e->d_name;
		if (DIR *F = opendir (dir.c_str()))
		{
			while (struct dirent *f = readdir (F))
				if (strcmp (f->d_name, ".") && strcmp (f->d_name, ".."))
					unlink ((dir + "/" + f->d_name).c_str());
			closedir (F);
		}
		rmdir (dir.c_str());
	}
	closedir (D);
}

//-------------------------------------------------------------------------------
// The message in the view
//-------------------------------------------------------------------------------

static MailMessage *shown_message (void)
{
	if (!currentmailbox || !currentmsgid[mdi_current][0]) return NULL;
	MailMessage *M = currentmailbox->Find (currentmsgid[mdi_current]);
	if (!M) return NULL;
	if (!(M->Flags & MMFL_HAVEBODY)) return NULL;
	if (!(M->Flags & MMFL_INRAM)) M->LoadBody ();
	return M;
}

//-------------------------------------------------------------------------------
// Save, Open, Save all
//-------------------------------------------------------------------------------

static void save_attachment (MailMessage *M, int index)
{
	MailPart *P = M->GetPart (index);
	std::string bytes;
	if (!attachment_bytes (P, bytes)) { fl_alert ("This attachment cannot be read."); return; }
	std::string name = attachment_file_name (P, index);
	const char *chosen = file_request ("Save attachment", start_folder().c_str(), name.c_str(), false);
	if (!chosen || !*chosen) return;
	std::string path = chosen;
	struct stat st;
	if (stat (path.c_str(), &st) == 0)
	{
		if (S_ISDIR (st.st_mode)) { fl_alert ("%s is a folder.", path.c_str()); return; }
		if (fl_choice ("The file %s already exists.\nReplace it?", "Cancel", "Replace", NULL, path.c_str()) != 1) return;
	}
	std::string err;
	if (!write_file (path, bytes, err)) { fl_alert ("Cannot save %s: %s.", path.c_str(), err.c_str()); return; }
	size_t slash = path.rfind ('/');
	if (slash != std::string::npos) remember_folder (slash ? path.substr (0, slash) : "/");
}

// The warning before opening. Cancel is the default (Return, Escape and
// closing the window all cancel); Open must be clicked.
static bool confirm_open (const std::string &title, const std::string &text, const char *open_label)
{
	fl_font (FL_HELVETICA, 13);
	int tw = 0, th = 0;
	fl_measure (text.c_str(), tw, th, 0);
	int w = std::max (420, tw + 90), h = th + 80;
	Fl_Double_Window win (w, h, title.c_str());
	Fl_Box icon (12, 14, 44, 44, "!");
	icon.box (FL_THIN_UP_BOX);
	icon.labelfont (FL_TIMES_BOLD);
	icon.labelsize (34);
	icon.labelcolor (FL_RED);
	Fl_Box msg (70, 12, w - 80, th + 6, NULL);
	msg.copy_label (text.c_str());
	msg.labelsize (13);
	msg.align (FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
	int answer = 0;
	Fl_Button open (w - 250, h - 38, 120, 26, open_label);
	open.labelsize (13);
	open.callback ([](Fl_Widget *, void *v) { *(int *) v = 1; }, &answer);
	Fl_Return_Button cancel (w - 120, h - 38, 108, 26, "Cancel");
	cancel.labelsize (13);
	cancel.callback ([](Fl_Widget *, void *v) { *(int *) v = 2; }, &answer);
	win.callback ([](Fl_Widget *, void *v) { *(int *) v = 2; }, &answer);
	win.end();
	win.set_modal();
	win.show();
	cancel.take_focus();
	while (win.shown() && !answer) Fl::wait();
	win.hide();
	return answer == 1;
}

static void open_attachment (MailMessage *M, int index)
{
	MailPart *P = M->GetPart (index);
	std::string bytes;
	if (!attachment_bytes (P, bytes)) { fl_alert ("This attachment cannot be read."); return; }
	std::string name = attachment_file_name (P, index);
	std::string type = bare_type (P);
	const char *cmd = PREFERENCES.Get_char_tree ("postoffice/gui/action/attachment");
	if (!cmd || !*cmd) cmd = "xdg-open $";

	// Always a clear warning, and confirmation; Cancel is the default.
	std::string what = "\"" + name + "\" (" + (type.empty() ? "unknown type" : type) + ", " + size_text (bytes.size()) + ")";
	std::string shown_cmd = cmd;
	for (size_t at; (at = shown_cmd.find ('$')) != std::string::npos; ) shown_cmd.replace (at, 1, "FILE");
	bool ok;
	if (risky (name, type))
		ok = confirm_open ("Warning: program or script",
			"WARNING: " + what + " is a program, script or similar file that can run code.\n\n"
			"Opening it can harm your computer and your files. Attachments like this are a common\n"
			"way to spread viruses, even in mail that seems to come from someone you know.\n\n"
			"Open it only if you were expecting exactly this file from someone you trust.\n\n"
			"It will be opened with: " + shown_cmd, "Open anyway");
	else
		ok = confirm_open ("Open attachment",
			"Open the attachment " + what + "?\n\n"
			"Attachments can contain harmful content. Open attachments only if you\n"
			"expected them, from people you trust.\n\n"
			"It will be opened with: " + shown_cmd, "Open");
	if (!ok) return;

	std::string root = open_root ();
	mkdir ((home() + "/.postoffice").c_str(), 0700);
	mkdir (root.c_str(), 0700);
	chmod (root.c_str(), 0700);
	std::string tmpl = root + "/open-XXXXXX";
	std::vector<char> buf (tmpl.begin(), tmpl.end());
	buf.push_back (0);
	if (!mkdtemp (buf.data())) { fl_alert ("Cannot make a folder in %s: %s.", root.c_str(), strerror (errno)); return; }
	std::string path = std::string (buf.data()) + "/" + name;
	std::string err;
	// Read-only for you, and never executable.
	if (!write_file (path, bytes, err, 0600)) { fl_alert ("Cannot write %s: %s.", path.c_str(), err.c_str()); return; }
	run_with_argument (cmd, path.c_str());
}

static void save_all_attachments (MailMessage *M)
{
	std::vector<int> parts;
	for (int i = 0; i < M->Count; ++i) if (part_is_attachment (M->GetPart (i))) parts.push_back (i);
	if (parts.empty()) { fl_message ("This message has no attachments."); return; }
	const char *chosen = file_request ("Save all attachments into this folder", start_folder().c_str(), "", true);
	if (!chosen || !*chosen) return;
	std::string dir = chosen;
	while (dir.size() > 1 && dir.back() == '/') dir.pop_back();
	if (!is_dir (dir)) { fl_alert ("%s is not a folder.", dir.c_str()); return; }

	int saved = 0;
	std::string failed;
	for (int i : parts)
	{
		MailPart *P = M->GetPart (i);
		std::string bytes, err;
		std::string name = attachment_file_name (P, i);
		if (!attachment_bytes (P, bytes)) { failed += "\n" + name + ": cannot be read"; continue; }
		// An existing file is kept: the new one gets " (2)", " (3)"...
		std::string base = name, ext;
		size_t dot = name.rfind ('.');
		if (dot != std::string::npos && dot > 0) { base = name.substr (0, dot); ext = name.substr (dot); }
		std::string path = dir + "/" + name;
		struct stat st;
		for (int n = 2; stat (path.c_str(), &st) == 0 && n < 1000; ++n)
			path = dir + "/" + base + " (" + std::to_string (n) + ")" + ext;
		if (write_file (path, bytes, err)) ++saved;
		else failed += "\n" + name + ": " + err;
	}
	remember_folder (dir);
	if (failed.empty()) fl_message ("Saved %d attachment%s in %s.", saved, saved == 1 ? "" : "s", dir.c_str());
	else fl_alert ("Saved %d attachment%s in %s. Not saved:%s", saved, saved == 1 ? "" : "s", dir.c_str(), failed.c_str());
}

//-------------------------------------------------------------------------------
// Entry points
//-------------------------------------------------------------------------------

// A click on an attachment line: Save as... / Open...
static void attachment_clicked (int index)
{
	MailMessage *M = shown_message ();
	if (!M || index < 0 || index >= M->Count)
	{
		fl_alert ("The message is no longer available here (select it again).");
		return;
	}
	Fl_Menu_Item items[] = {
		{ "Save as...", 0, 0, 0, 0, 0, 0, 12, 0 },
		{ "Open...", 0, 0, 0, 0, 0, 0, 12, 0 },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0 }
	};
	const Fl_Menu_Item *m = items->popup (Fl::event_x(), Fl::event_y(), attachment_file_name (M->GetPart (index), index).c_str());
	if (m == &items[0]) save_attachment (M, index);
	else if (m == &items[1]) open_attachment (M, index);
}

// Message -> Save all attachments...
void cb_save_attachments_m (Fl_Menu_ *, void *)
{
	MailMessage *M = shown_message ();
	if (!M) { fl_alert ("Select a message first."); return; }
	save_all_attachments (M);
}

void attachments_init (void)
{
	ht_attachment_hook = attachment_clicked;
	attachments_cleanup ();
	atexit (attachments_cleanup);
}
