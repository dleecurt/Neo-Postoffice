#include "iconbrowser.h"
#include "filereq.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include "wordlist.h"
#include "strdupp.h"

static unsigned char *image_folder[] = {
(unsigned char *)"16 17 9 1",
(unsigned char *)"g c #808080",
(unsigned char *)"b c #ffa858",
(unsigned char *)"e c #c0c0c0",
(unsigned char *)"# c #000000",
(unsigned char *)"c c #ffdca8",
(unsigned char *)". c None",
(unsigned char *)"a c #585858",
(unsigned char *)"f c #a0a0a4",
(unsigned char *)"d c #ffffff",
(unsigned char *)"................",
(unsigned char *)"..#a#...........",
(unsigned char *)".#abc##.........",
(unsigned char *)".#daabc#####....",
(unsigned char *)".#ddeaabcccb#...",
(unsigned char *)".#dedeeabccca...",
(unsigned char *)".#edeeeeaaaab#..",
(unsigned char *)".#deeeeeeefe#ba.",
(unsigned char *)".#eeeeeeefef#ba.",
(unsigned char *)".#eeeeeefeff#ba.",
(unsigned char *)".#eeeeefefff#ba.",
(unsigned char *)".##geefeffff#ba.",
(unsigned char *)"...##gefffff#ba.",
(unsigned char *)".....##fffff#ba.",
(unsigned char *)".......##fff#b##",
(unsigned char *)".........##f#b##",
(unsigned char *)"...........####."
};
static Fl_Pixmap pixmap_kfolder(image_folder);

static unsigned char *image_document[] = {
(unsigned char *)"16 17 41 1",
(unsigned char *)" \tc None",
(unsigned char *)".\tc #000000",
(unsigned char *)"+\tc #C3C3C3",
(unsigned char *)"@\tc #303030",
(unsigned char *)"#\tc #C0C0FF",
(unsigned char *)"$\tc #FF0000",
(unsigned char *)"%\tc #008080",
(unsigned char *)"&\tc #800000",
(unsigned char *)"*\tc #C0C000",
(unsigned char *)"=\tc #0000C0",
(unsigned char *)"-\tc #808080",
(unsigned char *)";\tc #C0FFC0",
(unsigned char *)">\tc #00FFFF",
(unsigned char *)",\tc #FFFFFF",
(unsigned char *)"\'\tc #A0A0A0",
(unsigned char *)")\tc #00FF00",
(unsigned char *)"!\tc #FFFF00",
(unsigned char *)"~\tc #FFC0FF",
(unsigned char *)"{\tc #404000",
(unsigned char *)"]\tc #00C000",
(unsigned char *)"^\tc #000080",
(unsigned char *)"/\tc #FFFFC0",
(unsigned char *)"(\tc #400000",
(unsigned char *)"_\tc #C05800",
(unsigned char *)":\tc #800080",
(unsigned char *)"<\tc #008000",
(unsigned char *)"[\tc #00C0C0",
(unsigned char *)"}\tc #DCDCDC",
(unsigned char *)"|\tc #FF8000",
(unsigned char *)"1\tc #FFC0C0",
(unsigned char *)"2\tc #585858",
(unsigned char *)"3\tc #C00000",
(unsigned char *)"4\tc #FFDCA8",
(unsigned char *)"5\tc #808000",
(unsigned char *)"6\tc #FFA858",
(unsigned char *)"7\tc #004000",
(unsigned char *)"8\tc #C000C0",
(unsigned char *)"9\tc #0000FF",
(unsigned char *)"0\tc #FF00FF",
(unsigned char *)"a\tc #C0FFFF",
(unsigned char *)"b\tc #004040",
(unsigned char *)"                ",
(unsigned char *)"      ..        ",
(unsigned char *)"      ./...     ",
(unsigned char *)"     ./,/,/...  ",
(unsigned char *)"     .,,,,,/,/..",
(unsigned char *)"    ./,,,,/,/,/.",
(unsigned char *)"    ./,,,/,////.",
(unsigned char *)"   ./,/,/,/,///.",
(unsigned char *)"   .,/,/,/,/,/*.",
(unsigned char *)"  .,/,/,/,/,//. ",
(unsigned char *)" .//,///,/////. ",
(unsigned char *)".////,/,/,/,//. ",
(unsigned char *)".*//////,////*. ",
(unsigned char *)" ..*/////////.  ",
(unsigned char *)"   ...*/////*.  ",
(unsigned char *)"      ...*/*.   ",
(unsigned char *)"         ...    "
};
static Fl_Pixmap pixmap_document(image_document);

Fl_Window	*FREQ = NULL;
int		  	 FREQ_OPEN = 0;
int			 FREQ_UPDATE;
char		 FREQ_FILENAME[512];
WordList	 Garbage;

const char *single_file_request (void);
void freq_update (void);

const char *single_file_request (void)
{
	char buf[256];

	FREQ_UPDATE = 0;
	FREQ_OPEN = 1;
	FREQ_FILENAME[0] = 0;
	if (!FREQ) FREQ = make_filereq();
	FREQ->show();

	buf[0] = 0;
	if (!getcwd (buf, 255)) buf[0] = 0;
	if (!buf[0])
	{
		strcpy (buf, "/");
	}
	
	in_freq_dir->value (buf);
	freq_update();

	while (FREQ_OPEN)
	{
		Fl::wait (0.2);
		if (FREQ_UPDATE) freq_update();
	}
	Garbage.Clear();
	return (FREQ_FILENAME);
}

void freq_update (void)
{
	char 			*wd = (char *) in_freq_dir->value();
	DIR  			*D  = opendir (wd);
	struct dirent	*DE;
	struct stat		 stbuff;
	char			 bf[512];

	FREQ_UPDATE = 0;

	printf ("D(%s)=%p\n", wd, (void *) D);

	in_freq_dir->redraw();

	printf ("Done\n");

	if (D)
	{
		while (br_freq->size()) br_freq->remove(1); // clean up iconbrowser

		while ((DE = readdir (D))) // walk through the directory
		{
			snprintf (bf, sizeof (bf), "%s/%s", wd,DE->d_name);
			if (stat (bf, &stbuff) == 0)
			{
				char *xn = strdupp (DE->d_name);
				Garbage.Add_ (xn);
				if (S_ISDIR (stbuff.st_mode))
				{
					if ((strcmp (xn, ".")) && (strcmp (xn, "..")))
						br_freq->add (xn, &pixmap_kfolder, NULL);
				}
				else
				{
					if (DE->d_name[0] != '.') br_freq->add (xn, &pixmap_document, NULL);
				}
			}
		}
	}
	if (D) closedir (D);
	br_freq->redraw();
}

void cb_freq_home (Fl_Button */*b*/, void */*v*/)
{
	in_freq_dir->value (getenv ("HOME"));
	freq_update();
}

void cb_freq_desktop (Fl_Button */*b*/, void */*v*/)
{
	char *hm = getenv ("HOME");
	if (hm)
	{
		char *dst = new char[strlen(hm)+strlen("/Desktop")+1];
		sprintf (dst, "%s/Desktop", hm);
		in_freq_dir->value (dst);
		delete[] dst;
		freq_update();
	}
}

void cb_freq_reload (Fl_Button */*b*/, void */*v*/)
{
	freq_update();
}

void cb_freq_back (Fl_Button */*b*/, void */*v*/)
{
}

void cb_freq_up (Fl_Button */*b*/, void */*v*/)
{
	WordList TR;
	char     buf[256];
	TR.MakeGeneric ((char *) in_freq_dir->value(), '/');
	if (TR.Count)
	{
		TR.Delete (TR.Count-1);
		buf[0] = 0;
		for (int i=0;i<TR.Count;++i)
		{
			strcat (buf, TR.Get(i));
			if ((i+1) < TR.Count) strcat (buf, "/");
		}
		in_freq_dir->value (buf);
		freq_update();
	}
}

void cb_freq_list (Fl_Button */*b*/, void */*v*/)
{
}

void cb_freq_cd (Fl_Input */*b*/, void */*v*/)
{
	freq_update();
}

void cb_freq_select (Fl_Iconbrowser */*br*/, void */*v*/)
{
	char bf[512];
	struct stat xstat;
	int val;

	if ((val = br_freq->value()))
	{
		snprintf (bf, sizeof (bf), "%s/%s", in_freq_dir->value(), br_freq->text (val));
		if (stat (bf, &xstat) == 0)
		{
			if (S_ISDIR (xstat.st_mode))
			{
				in_freq_dir->value(bf);
				FREQ_UPDATE = 1;
			}
			else
			{
				in_freq_filename->value (br_freq->text (val));
			}
		}	
	}
	else in_freq_filename->value ("");
}

void cb_freq_ok (Fl_Button */*b*/, void */*v*/)
{
	if (strlen (in_freq_filename->value()))
	{
		snprintf (FREQ_FILENAME, sizeof (FREQ_FILENAME), "%s/%s", in_freq_dir->value(), in_freq_filename->value());
	}
	else FREQ_FILENAME[0] = 0;
	FREQ->hide();
	FREQ_OPEN = 0;
}

void cb_freq_cancel (Fl_Button */*b*/, void */*v*/)
{
	FREQ_FILENAME[0] = 0;
	FREQ->hide();
	FREQ_OPEN = 0;
}

void cb_freq_filename (Fl_Input */*b*/, void */*v*/)
{
}
