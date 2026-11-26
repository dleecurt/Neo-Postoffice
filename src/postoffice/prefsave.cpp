//===============================================================================
// Neo-PostOffice: saving the settings
// ------------------------------------
// The settings used to be written only by File -> Quit, so a crash lost every
// change made since the program started. Now, every two seconds, the settings
// are written out in memory and compared with what was last saved; when they
// differ, ~/.postoffice/resource.rc is replaced. That catches every change
// (Config, the address book, folders added by a check, the window layout)
// without each place having to ask for a save.
//
// The file is written to resource.rc.tmp, flushed to disk and renamed over
// resource.rc, so a crash while saving leaves the old or the new file, never
// half of one.
//===============================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include <string>

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/fl_ask.H>

#include "var.h"

extern Var	 PREFERENCES;
Fl_Window	*main_window = NULL;		// set in main(), for its geometry

namespace
{
	const double	INTERVAL = 2.0;
	std::string		saved;				// the settings as last written
	bool			warned = false;		// a failed save was reported
	bool			running = false;

	std::string path (const char *suffix = "")
	{
		const char *home = getenv ("HOME");
		return std::string (home ? home : ".") + "/.postoffice/resource.rc" + suffix;
	}

	// The settings as they would be written to the file.
	std::string snapshot (void)
	{
		char *buf = NULL;
		size_t len = 0;
		FILE *F = open_memstream (&buf, &len);
		if (!F) return saved;
		PREFERENCES.Save (F, 0);
		fclose (F);
		std::string r (buf ? buf : "", len);
		free (buf);
		return r;
	}

	bool write_file (const std::string &text, std::string &err)
	{
		std::string tmp = path (".tmp"), real = path ();
		int fd = open (tmp.c_str (), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (fd < 0) { err = strerror (errno); return false; }
		fchmod (fd, 0600);		// the settings hold passwords
		size_t done = 0;
		while (done < text.size ())
		{
			ssize_t n = write (fd, text.data () + done, text.size () - done);
			if (n < 0)
			{
				if (errno == EINTR) continue;
				err = strerror (errno);
				close (fd);
				unlink (tmp.c_str ());
				return false;
			}
			done += (size_t) n;
		}
		if (fsync (fd) != 0 || close (fd) != 0)
		{
			err = strerror (errno);
			unlink (tmp.c_str ());
			return false;
		}
		if (rename (tmp.c_str (), real.c_str ()) != 0)
		{
			err = strerror (errno);
			unlink (tmp.c_str ());
			return false;
		}
		return true;
	}

	// The main window's place and size, kept in the settings.
	void record_geometry (void)
	{
		if (!main_window || !main_window->shown () || !main_window->visible ()) return;
		char buf[64];
		snprintf (buf, sizeof (buf), "%i,%i,%i,%i", main_window->x (), main_window->y (),
				  main_window->w (), main_window->h ());
		const char *old = PREFERENCES.Get_char_tree ("postoffice/state/main/geometry");
		if (!old || strcmp (old, buf) != 0)
			PREFERENCES.Set_char_tree ("postoffice/state/main/geometry", buf);
	}

	void tick (void *)
	{
		record_geometry ();
		std::string now = snapshot ();
		if (now != saved)
		{
			std::string err;
			if (write_file (now, err))
			{
				saved = now;
				warned = false;
			}
			else if (!warned)
			{
				warned = true;
				fl_alert ("The settings could not be saved to\n%s:\n%s\n\nThey will be saved as soon as that works again.",
						  path ().c_str (), err.c_str ());
			}
		}
		Fl::repeat_timeout (INTERVAL, tick);
	}
}

// Saves the settings now, if they changed (File -> Quit, and at start-up
// after old settings were converted). Returns false if writing failed.
bool prefs_save_now (void)
{
	record_geometry ();
	std::string now = snapshot ();
	if (now == saved) return true;
	std::string err;
	if (!write_file (now, err))
	{
		fprintf (stderr, "postoffice: cannot save the settings to %s: %s\n", path ().c_str (), err.c_str ());
		return false;
	}
	saved = now;
	return true;
}

// Starts saving changes automatically. The settings as loaded count as saved.
void prefs_autosave_start (void)
{
	if (running) return;
	running = true;
	saved = snapshot ();
	Fl::add_timeout (INTERVAL, tick);
}

// Stops the automatic saves (at the end of the program, before the
// mailboxes are taken down).
void prefs_autosave_stop (void)
{
	if (!running) return;
	running = false;
	Fl::remove_timeout (tick);
}
