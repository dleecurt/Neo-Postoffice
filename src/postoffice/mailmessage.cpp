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

#include <string>
#include <vector>
#include "mailheaders.h"
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
#include <strings.h>
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

// The stored content type of a part: its type, plus "; x-neo-cte=..."
// when it is base64 or quoted-printable (Neo-PostOffice).
// And "; x-neo-mp=..." when it was inside multiparts (0.9.4): the chain of
// multiparts around it, for rebuilding a copy with the same nesting.
static std::string part_type (const std::string &type, const std::string &encoding, const std::string &chain = "")
{
	std::string out = type.empty() ? "text/plain" : type;
	while (!out.empty() && (out.back() == ' ' || out.back() == ';' || out.back() == '\r')) out.pop_back();
	if (strcasestr (encoding.c_str(), "base64")) out += "; x-neo-cte=base64";
	else if (strcasestr (encoding.c_str(), "quoted-printable")) out += "; x-neo-cte=quoted-printable";
	if (!chain.empty()) out += "; x-neo-mp=\"" + chain + "\"";
	return out;
}

// "mixed" from "multipart/mixed; boundary=...", for the chain.
static std::string mp_link (int number, const std::string &ctype)
{
	std::string sub;
	for (size_t i = 10; i < ctype.size() && (isalnum ((unsigned char) ctype[i]) || ctype[i] == '-' || ctype[i] == '.'); ++i)
		sub += (char) tolower ((unsigned char) ctype[i]);
	if (sub.empty()) sub = "mixed";
	return std::to_string (number) + ":" + sub;
}

void MailPart::Set (const char *contenttype, const char *filename, const char *description, unsigned long contentsize, const char *contents)
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

MailMessage::MailMessage (MailBox *M, const char *msgid) : BTreeNode (msgid, M->Root)
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
	CaptureRaw = NULL;
	AtFront = false;
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
	// (PostOffice 0.7 had a store-on-destroy here that could never run. It
	// is not needed: ProcessBody stores every message as it is parsed, and
	// saving the mailbox syncs the store.)
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

	sprintf (fnam, "%s/%02x", Parent->DirectoryPrefix, BChecksum & 0xff);
	if (stat (fnam, &mystat) != 0)
	{
		mkdir (fnam, 0700);
	}
	
	safe_msgid = MakeSafe (MsgID);
	if (part>=0) sprintf (fnam, "%s/%02x/%s.%02i", Parent->DirectoryPrefix, BChecksum & 0xff, safe_msgid, part);
	else sprintf (fnam, "%s/%02x/%s.hd", Parent->DirectoryPrefix, BChecksum & 0xff, safe_msgid);

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
	
	if (CaptureRaw) CaptureRaw->assign (Message);
	
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

char *MailMessage::Header (const char *name)
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
			if (HeaderValues->Count == 0) continue;	// nothing to continue (malformed)
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
	// Neo-PostOffice: names and subjects are shown decoded (RFC 2047) as UTF-8.
	std::string from_decoded = mailheaders::Decode (Header ("From"));
	std::vector<char> from_copy (from_decoded.begin(), from_decoded.end());
	from_copy.push_back (0);
	tmp = eaddr2name (from_copy.data());
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
	Envelope->Set (ENVELOPE_SUBJECT, mailheaders::Decode (Header ("Subject")).c_str());
	SaveHeaders();
}

void MailMessage::ProcessBody (char *body)
{
	int	  didmultipart = 0;
	char *contenttype;

    Parent->GrimReaper();

	Count = 0;
	
	contenttype = Header ("Content-Type"); // Get the Content-Type header
	std::string ctype = contenttype;
	size_t ct0 = ctype.find_first_not_of (" \t");
	if (ct0 != std::string::npos) ctype.erase (0, ct0);

	// Any multipart type (mixed, alternative, related, signed, report...)
	// is split at its boundary lines (Neo-PostOffice; 0.7 handled mixed and
	// alternative only, and found the boundary anywhere in the text).
	if (strncasecmp (ctype.c_str(), "multipart/", 10) == 0)
	{
		std::string boundary = mailheaders::Param (ctype, "boundary");
		MimeCount = 0;
		if (!boundary.empty()) didmultipart = SplitMultipart (boundary.c_str(), body, 0, mp_link (++MimeCount, ctype));
	}

	// A single-part message that is not plain text, or is encoded, is one
	// part with its own type and encoding.
	std::string cte = Header ("Content-Transfer-Encoding");
	bool encoded = strcasestr (cte.c_str(), "base64") || strcasestr (cte.c_str(), "quoted-printable");
	if (!didmultipart && !ctype.empty() && (encoded || strncasecmp (ctype.c_str(), "text/plain", 10) != 0))
	{
		std::string filename = mailheaders::Param (Header ("Content-Disposition"), "filename");
		if (filename.empty()) filename = mailheaders::Param (ctype, "name");
		AddPart (part_type (ctype, cte).c_str(), "", filename.c_str(), body);
		didmultipart = 1;
	}
	// Plain text keeps its declared charset for the message view.
	const char *plaintype = ctype.empty() ? "text/plain" : ctype.c_str();

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
								AddPart (plaintype,"","", (char *) Bd.Data);
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
					AddPart (plaintype,"","", (char *) Bd.Data);
				}
			}
		}
		else // really a plain message
		{
			AddPart (plaintype,"","", body);
		}
	}

	Flags |= (MMFL_HAVEBODY+MMFL_INRAM); 
	
	StoreFile();
}

void MailMessage::AddPart (const char *contenttype, const char *description, const char *filename, const char *body)
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

// Splits a multipart body (RFC 2046): a part starts after a line
// "--boundary" and ends before the line break in front of the next one;
// "--boundary--" closes it. Text before the first and after the last
// boundary line is ignored.
bool MailMessage::SplitMultipart (const char *boundary, const char *body, int depth, const std::string &chain)
{
	std::string delim = std::string ("--") + boundary;
	const char *p = body, *start = NULL;
	bool any = false;
	while (*p)
	{
		const char *eol = strchr (p, '\n');
		size_t len = eol ? (size_t) (eol - p) : strlen (p);
		if (len >= delim.size() && strncmp (p, delim.c_str(), delim.size()) == 0)
		{
			const char *q = p + delim.size(), *lend = p + len;
			bool close = lend - q >= 2 && q[0] == '-' && q[1] == '-';
			if (close) q += 2;
			while (q < lend && (*q == ' ' || *q == '\t' || *q == '\r')) ++q;
			if (q == lend)		// a boundary line
			{
				if (start)
				{
					const char *end = p;
					if (end > start && end[-1] == '\n') --end;
					if (end > start && end[-1] == '\r') --end;
					std::string part (start, end);
					AddPart (part.c_str(), depth, chain);
					any = true;
				}
				if (close) return any;
				start = eol ? eol + 1 : lend;
			}
		}
		if (!eol) break;
		p = eol + 1;
	}
	if (start && *start)		// no closing line: the last part runs to the end
	{
		AddPart (start, depth, chain);
		any = true;
	}
	return any;
}

// Adds one part of a multipart message: its headers, then its body.
// Nested multiparts are split in turn.
void MailMessage::AddPart (const char *part, int depth, const std::string &chain)
{
	// Where the headers end: a part that starts with an empty line has none.
	const char *body;
	std::string head;
	if (part[0] == '\n') body = part + 1;
	else if (part[0] == '\r' && part[1] == '\n') body = part + 2;
	else
	{
		const char *e1 = strstr (part, "\n\n"), *e2 = strstr (part, "\n\r\n");
		const char *e = e1 && (!e2 || e1 < e2) ? e1 : e2;
		if (e)
		{
			head.assign (part, e + 1);
			body = e + (e == e1 ? 2 : 3);
		}
		else if (strchr (part, ':') && strchr (part, ':') < (strchr (part, '\n') ? strchr (part, '\n') : part + strlen (part)))
		{
			head = part;			// only headers
			body = part + strlen (part);
		}
		else body = part;			// no headers at all
	}
	std::vector<mailheaders::Header> h = mailheaders::Parse (head + "\n");
	std::string ctype = mailheaders::Get (h, "Content-Type");
	std::string cte = mailheaders::Get (h, "Content-Transfer-Encoding");
	std::string desc = mailheaders::Decode (mailheaders::Get (h, "Content-Description"));
	std::string filename = mailheaders::Param (mailheaders::Get (h, "Content-Disposition"), "filename");
	if (filename.empty()) filename = mailheaders::Param (ctype, "name");
	if (ctype.empty()) ctype = "text/plain";		// RFC 2045 default

	if (strncasecmp (ctype.c_str(), "multipart/", 10) == 0 && depth < 8)
	{
		std::string boundary = mailheaders::Param (ctype, "boundary");
		if (!boundary.empty() && SplitMultipart (boundary.c_str(), body, depth + 1,
												  (chain.empty() ? "" : chain + ",") + mp_link (++MimeCount, ctype))) return;
	}
	AddPart (part_type (ctype, cte, chain).c_str(), desc.c_str(), filename.c_str(), body);
}
