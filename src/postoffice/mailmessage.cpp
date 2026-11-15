//===============================================================================
// POSTOFFICE - X11 Office Communication Handler
// ---------------------------------------------
// All sourcecode, unless differently annotated, written by Pim van Riezen
// Sourcecode and resulting binaries are covered by the GNU Lesser General Public
// License (GNU LGPL). For more information and the literal text of the license
// agreement, check the Free Software Foundation's website on http://www.fsf.org/
// 
// Copyright (C) 1999 Mad Sciene Labs - Rotterdam, The Netherlands
//===============================================================================

#include "mailbox.h"
#include "substr.h"
#include "eaddr.h"
#include "strdupp.h"
#include "chop.h"
#include "checksum.h"
#include "buffer.h"
#include "reg.h"
#include "buffer.h"
#include "btree.h"

#include <string.h>
#if defined(SOLARIS) || defined(IRIX)
  #include <strings.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#ifdef USE_GDBM
  #include <gdbm.h>
  #include "gdbmtools.h"
#endif

#include "debug.h"

MailPart::MailPart (void)
{
	ContentType = NULL;
	FileName = NULL;
	Description = NULL;
	ContentSize = 0;
	Contents = NULL;
}

MailPart::~MailPart (void)
{
	if (ContentType ) delete[] (ContentType);
	if (FileName) delete[] (FileName);
	if (Description) delete[] (Description);
	if (Contents) delete[] (Contents);
}

void MailPart::Set (char *contenttype, char *filename, char *description, unsigned long contentsize, char *contents)
{
	if (ContentType ) delete[] (ContentType);
	if (FileName) delete[] (FileName);
	if (Description) delete[] (Description);
	if (Contents) delete[] (Contents);

	ContentType = strdupp (contenttype);
	FileName = strdupp (filename);
	Description = strdupp (description);
	Contents = new char[contentsize+2];
	memcpy (Contents, contents, contentsize+1);
	Contents[contentsize] = 0;
	//printf ("mailpart: set contents\n%s\n", Contents);
	ContentSize = contentsize;
}

MailMessage::MailMessage (MailBox *M, char *msgid) : BTreeNode (msgid, M->Root)
{
	Subscribers = new ViewList (this);
	HeaderNames = new WordList;
	HeaderValues = new WordList;
	if (msgid) MsgID = BKey;
    else MsgID = NULL;
	//cMsgID = checksum (msgid);
	Flags = 0;
	Parent = M;
	Count = 0;
	Envelope = new MailEnvelope;
	First = Last = NULL;
	ReceiveDate = time (NULL);
    Parent->Count++;
    Parent->GrimReaper();
}

// Think straight for a bit here. Should the message bare responsibility for its contents or should this be handled by the
// mailbox it is a part of?
// The mailbox, hypothetically speaking, has a better way to know how to handle the in RAM / on disk cache, which means
// the only kind of flush we should do is make sure that message carrying MMFL_ORIGINAL do have a copy on disk before
// the message gets flushed.

MailMessage::~MailMessage (void)
{
	if ( (Flags & MMFL_ORIGINAL) && (Flags & MMFL_HAVEBODY) && (Flags & MMFL_INRAM) && (! Flags & MMFL_ONDISK) )
	{
		StoreFile();
	}
	delete (HeaderNames);
	delete (HeaderValues);
	delete (Subscribers);
	delete (Envelope);
	//if (MsgID) delete (MsgID);
	MailPart *X, *NX;
	X = First;
	while (X)
	{
		NX = X->Next;
		delete (X);
		X = NX;
	}
}


void MailMessage::RemoveFiles (void)
{
    Parent->DB->Delete (MsgID);
}

void MailMessage::StoreFile (void)
{
// 	FILE *X;
// 	int i;

	if ((! (Flags & MMFL_ONDISK)) && (Flags & MMFL_HAVEBODY))
	{
		Parent->DB->Store (this);
		Parent->DB->StoreIndex();
        Flags = Flags | MMFL_ONDISK;
	}
}

void MailMessage::SaveHeaders (void)
{
//	FILE *X;
//	char *fnam;
//	int i;

	if (HeaderNames->Count)
	{
		Parent->DB->Store (this);
	}
}

void MailMessage::LoadHeaders (void)
{
// 	FILE *X;
// //	char *fnam;
// 	char buf[512];
// 	char *headername;
// 	char *headervalue;
// 	char *boundary;

	if (HeaderNames->Count) return;

	dprintf ("mailmessage(%s)::loadheaders()\n", MsgID);

	if (!Parent->DB->Retrieve (this))
	{
		if (Flags & MMFL_ONDISK) Flags -= MMFL_ONDISK;
		if (Flags & MMFL_HAVEBODY) Flags -= MMFL_HAVEBODY;
	}
}

#define BADFILECHARS "!'`\"$&*?#%()[]\\/|{};:="

char *MailMessage::MakeSafe (char *_src)
{
	char *dst, *_dst;
	char *src = _src;
	
	_dst = new char[strlen (src)+2];
	dst = _dst;

	while (*src)
	{
		if (strchr (BADFILECHARS, *src)) *dst = '_';
		else if ((*src != '<') && (*src != '>'))
		{
			*dst = *src;
		}
		if ((*src == '<') || (*src == '>')) ++src;
		else
		{
			++src; ++dst;
		}
	}
	*dst = 0;
	return _dst;
}

FILE *MailMessage::OpenFile (int part, char *mode)
{
	FILE *result;

	char *fnam;
	char *safe_msgid;
	struct stat mystat;

	fnam = new char[strlen (Parent->DirectoryPrefix) + strlen (MsgID) + 32];

	sprintf (fnam, "%s/%02x", Parent->DirectoryPrefix, (unsigned int) BKey & 0xff);
	if (stat (fnam, &mystat) != 0)
	{
		mkdir (fnam, 0700);
	}
	
	safe_msgid = MakeSafe (MsgID);
	if (part>=0) sprintf (fnam, "%s/%02x/%s.%02i", Parent->DirectoryPrefix, (unsigned int) BKey & 0xff, safe_msgid, part);
	else sprintf (fnam, "%s/%02x/%s.hd", Parent->DirectoryPrefix, (unsigned int) BKey & 0xff, safe_msgid);

	result = fopen (fnam, mode);
	delete[] safe_msgid;
	delete[] fnam;
	return result;
}

// If there is body data in memory, save it to disk as needed and remove it from RAM

void MailMessage::Flush (void)
{
	MailPart *P, *NP;
	
	if ( (Flags & MMFL_HAVEBODY) && (Flags & MMFL_INRAM) )
	{
		if (! (Flags & MMFL_ONDISK) )
		{
			StoreFile();
            Flags = Flags | MMFL_ONDISK;
		}
		if (Flags & MMFL_ONDISK)
		{
			P = First;
			while (P)
			{
				NP = P->Next;
				Parent->MemoryUsage -= P->ContentSize;
				delete (P);
				P = NP;
			}
			First = Last = NULL;
			Flags = Flags - MMFL_INRAM;
			Flags = Flags | (MMFL_ONDISK + MMFL_HAVEBODY);
            HeaderNames->Clear();
            HeaderValues->Clear();
		}
	}
}

// If we have a body, but not in RAM, load the copy from disk

void MailMessage::LoadBody (void)
{
	if ( ! (Flags & MMFL_INRAM) )
	{
		if (!Parent->DB->Retrieve (this))
	    {
		   if (Flags & MMFL_ONDISK) Flags -= MMFL_ONDISK;
		   if (Flags & MMFL_HAVEBODY) Flags -= MMFL_HAVEBODY;
	    }
		else
		{
			Flags |= MMFL_INRAM;
		}
	}
}

void MailMessage::ProcessMessage (char *Message)
{
	char *headers;
	char *body;
	
	Count = 0;
	
	body = strstr (Message, "\n\n");
	if (!body)
	{
		ProcessHeaders (Message);
	}
	else
	{
		++body;
		if (body)
		{
			headers = substr (Message, body);
			++body;
			ProcessHeaders (headers);
			delete[] (headers);
			ProcessBody (body);
		}
	}
}

inline char *MailMessage::Header (char *name)
{
	int x = HeaderNames->Find (name);
	if (x>=0) return (HeaderValues->Get (x));
	
	return ((char *) "");
}

void MailMessage::ProcessHeaders (char *Hdr)
{
	if (!Hdr) return;
	int i;
	WordList X;
	char *lin;
	char *bnd;
	char *name;
	char *tmp;
	
	HeaderNames->Clear();
	HeaderValues->Clear();
	
	X.MakeGeneric (Hdr, '\n', 0);
	for (i=0; i<X.Count; ++i)
	{
		lin = X.Get (i);
		if ((lin[0] == '\t') || (lin[0] == ' '))
		{
			while ((*lin == '\t')||(*lin == ' ')) ++lin;
			bnd = HeaderValues->Get (HeaderValues->Count -1);
			tmp = new char [strlen (lin) + strlen (bnd) + 3];
			strcpy (tmp, bnd);
			strcat (tmp, " ");
			strcat (tmp, lin);
			HeaderValues->Set (HeaderValues->Count -1, tmp);
			delete[] (tmp);
		}
		else if ((bnd = strchr (lin, ':')))
		{
			name = substr (lin, bnd);
			while ( (*bnd == ':') || (*bnd == ' ') || (*bnd == '\t') ) ++bnd;
			HeaderNames->Add_ (name);
			HeaderValues->Add (bnd);
			//delete[] (name);
		}
	}

	if (strlen (Header ("Newsgroups"))) Envelope->Set (ENVELOPE_NEWSGROUPS, Header ("Newsgroups"));
	Envelope->Set (ENVELOPE_DATE, Header ("Date"));
	tmp = eaddr2name (Header ("From"));
	Envelope->Set (ENVELOPE_NAMEFROM, tmp);
	delete[] (tmp);
	
	tmp = Header ("In-Reply-To");
	if (strlen (tmp))
	{
		X.MakeGeneric (tmp, ';');
		Envelope->Set (ENVELOPE_I_PARENTID, X.Get(0));
	}
	else
	{
		tmp = Header ("References");
		X.MakeGeneric (tmp, ' ');
		if (X.Count)
			Envelope->Set (ENVELOPE_I_PARENTID, X.Get (X.Count-1));
	}

	tmp = eaddr2addr (Header ("From"));
	if (tmp)
	{
		if (strlen (tmp) > 0) Envelope->Set (ENVELOPE_MAILFROM, tmp);
		delete[] (tmp);
	}
	Envelope->Set (ENVELOPE_SUBJECT, Header ("Subject"));
	SaveHeaders();
}

void MailMessage::ProcessBody (char *body)
{
	int	  didmultipart = 0;
	char *contenttype;
	char *boundary;
	char *mimetype;

    Parent->GrimReaper();

	Count = 0;
	
	contenttype = Header ("Content-Type"); // Get the Content-Type header
	if (*contenttype)
	{
		dprintf ("<--- Content-Type: %s --->\n", contenttype);
		WordList CT;
		CT.MakeGenericQuoted (contenttype, ';'); // Seperate arguments
		mimetype = strdupp (CT.Get(0));
		chopwhitespace (mimetype);
		if ( (strcasecmp (mimetype, "multipart/alternative") == 0) || // a.k.a. OutLook HTML Cannon
			 (strcasecmp (mimetype, "multipart/mixed") == 0) )
		{
			dprintf ("Korben Dallas MULTIPART\n");
			for (int i = 1; i<CT.Count;++i)
			{
				WordList Sub;
				char *subname;
				char *subval;
				char *tmp;
				
				Sub.MakeGenericQuoted (CT.Get(i), '='); // Split parameters
				subname = Sub.Get (0);
				chopwhitespace (subname);
				dprintf ("checking subname: '%s'\n", subname);
				if ((strcasecmp (subname, "boundary") == 0) && Sub.Count > 1) // boundary parameter?
				{
					subval = strchr (CT.Get(i),'=') + 1;
					while ( (*subval == ' ') || (*subval == '\t') ) ++subval; // find value
					chopwhitespace (subval);
					if (subval[0] == '\"') // value between double quotes
					{
						++subval;
						tmp = strchr (subval, '\"'); // find second quote
						if (tmp) *tmp = 0;
					}
					else if (subval[0] == '\'') // value between single quotes
					{
						++subval;
						tmp = strchr (subval, '\''); // find second quote
						if (tmp) *tmp = 0;
					}
					boundary = strdupp (subval); // value unquoted
					dprintf ("boundary=%s\n", boundary);
					if (strlen (boundary) > 3)
					{
						char *partstart;
						char *partend;
						char *part;
						
						didmultipart = 1;
						partstart = strstr (body, boundary); // find first part
						while (partstart)
						{
							partstart += strlen (boundary);
							while (*partstart < 32) ++partstart;
							partend = strstr (partstart, boundary); // find end of part
							if (partend)
							{
								part = substr (partstart, partend);
								AddPart (part);
								delete[] (part);
							}
							partstart = partend;
						}
					}
					delete[] (boundary);
				}
			}
		}
		delete[] (mimetype);
	}
	if (!didmultipart) // This wasn't a multipart message I guess, well, add it all as text/plain then
	{
		int doingplain=1;
		// first let's scan for uuencoded crap-o-la
		if (strstr (body, "begin ") || strstr (body, "BEGIN ")) // possibly uuencoded, this message needs more investigation.
		{
			dprintf ("[uu] body has 'begin'\n");
			AsyncBuffer Bd (4096);
			WordList WL;
			char *crsr = body;
			char *ln;
			char *eol;

			while ((eol = strchr (crsr, '\n')))
			{
				ln = substr (crsr, eol+1);
				choplf (ln);
				if (doingplain)
				{
					if (wild_match ("begin * *.*", ln))
					{
						dprintf ("[uu] found valid uu-start\n");
						WL.MakeGeneric (ln, ' '); // count spaces and get uuenc parameters
						if (WL.Count == 3) // fits our description, not every sentence that starts with "begin" is a binary ;)
						{
							Bd.Add ("\n");
							Bd.Add ((unsigned char) 0);
							if (Bd.Pos > 16) // filter out noise
								AddPart ((char *) "text/plain","","", (char *) Bd.Data);
							Bd.Pos = 0;
							doingplain = 0;
						}
					}
					Bd.Add (ln);
					Bd.Add ("\n");
				}
				else
				{
					if (strlen (ln))
					{
						Bd.Add (ln);
						Bd.Add ("\n");
					}
					else
					{
						Bd.Add ("\n");
						Bd.Add ((unsigned char) 0);
						AddPart ((char *) "text/uuencoded","",(char *) WL.Get(2), (char *) Bd.Data);
						Bd.Pos = 0;
						
						doingplain = 1;
					}
				}
				crsr = eol+1;
			}
			if (Bd.Pos > 16)
			{
				Bd.Add ("\n");
				Bd.Add ((unsigned char) 0);
				if (!doingplain)
				{
					AddPart ((char *) "text/uuencoded","",(char *) WL.Get(2), (char *) Bd.Data);
				}
				else
				{
					AddPart ((char *) "text/plain","","", (char *) Bd.Data);
				}
			}
		}
		else // really a plain message
		{
			AddPart ("text/plain","","", body);
		}
	}

	Flags |= (MMFL_HAVEBODY+MMFL_INRAM); 
	
	StoreFile();
}

void MailMessage::AddPart (char *contenttype, char *description, char *filename, char *body)
{
	MailPart *M = new MailPart;
    int ln;

	M->Set (contenttype, filename, description, ln = strlen(body), body);
	if (First)
	{
		M->Next = NULL;
		M->Prev = Last;
		Last->Next = M;
		Last = M;
	}
	else
	{
		M->Prev = M->Next = NULL;
		First = Last = M;
	}
	++Count;
    Parent->MemoryUsage += ln;
}

// Extract headers form a multipart element and add it as a MailPart.

MailPart *MailMessage::GetPart (int cnt)
{
	MailPart *P = First;
	int i = 0;
	while ((P) && (i<cnt))
	{
		P = P->Next;
		++i;
	}
	return P;
}

void MailMessage::AddPart (char *part)
{
	WordList W;
	char *ln;
	char *end;
	char *body;
	int     i;
	char contenttype[256];
	char description[256];
	char filename[256];
	WordList HNames, HValues;
	
	contenttype[0] = 0;
	description[0] = 0;
	filename[0] = 0;
	
	if ((end = strchr (part, '\n')))
	{
		ln = substr (part, end);
		while (ln && (strlen (ln) > 1))
		{
			char *colonpos;
			char *hdrname;
			char *hdrvalue;
			char *crsr = ln;

			if ( ((ln[0] == ' ') || (ln[0] == '\t')) && HValues.Count)
			{
				while ( (*crsr==' ') || (*crsr=='\t') ) ++crsr;
				hdrvalue = new char [strlen (HValues.Get(HValues.Count -1)) + strlen (crsr) + 1];
				strcpy (hdrvalue, HValues.Get (HValues.Count -1));
				strcat (hdrvalue, crsr);
				HValues.Set (HValues.Count-1, hdrvalue);
				delete (hdrvalue);
			}
			else
			{
				colonpos = strchr (ln, ':');
				if (colonpos)
				{
					hdrname = substr (ln, colonpos);
					hdrvalue = colonpos+1;
					while ( (*hdrvalue==' ') || (*hdrvalue=='\t') ) ++hdrvalue;
					HNames.Add_ (hdrname);
					HValues.Add (hdrvalue);
				}
			}
			
			crsr = end+1;
			delete[] (ln);
			end = strchr (crsr, '\n');
			if (end)
			{
				ln = substr (crsr,end);
			}
			else ln = NULL;
		}
		if (ln) delete[] (ln);
		if (end)
		{
			body = end+1;
			
			i = HNames.Find ("Content-Type");
			if (i>=0)
			{
				strncpy (contenttype, HValues.Get(i), 255);
				contenttype[255] = 0;
			}
			i = HNames.Find ("Content-Description");
			if (i>=0)
			{
				strncpy (description, HValues.Get(i), 255);
				description[255] = 0;
			}
			AddPart (contenttype, description, filename, body);
		}
	}
	HNames.Clear();
	HValues.Clear();
}
