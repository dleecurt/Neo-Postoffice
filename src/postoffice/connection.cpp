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

#include <sys/types.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include "socketclasses.h"
#include <time.h>
#include <string.h>
//#include "config.h"
//#include "memlog.h"

#ifdef USE_SSL
  #include "ssl.h"
#endif

#define MAXFD 255

#ifdef FREEBSD
  #define O_SYNC O_FSYNC
#endif

#ifdef IRIX
  typedef int socklen_t;
#endif

#define _MAX_TIMEOUT 9999

extern int      errno;

// This is a kludge, but those darn headers are hard to find in Scott's world

extern          "C" int socket (int, int, int);

// Not that Sun does any better :-)

#ifdef SUNOS
#define bzero(s, n) memset ((s), 0, (n))
extern          "C" int connect (int, struct sockaddr *, int);
extern          "C" int accept (int, caddr_t, int *);
extern          "C" int shutdown (int, int);
#endif

#include "debug.h"

void switch_blocking (int);
void switch_nonblocking (int);
char *xstrstr (char *, char *, int);

void switch_blocking (int Socket)
{
	int oldres;
	int newres;
	
	oldres = fcntl (Socket, F_GETFL, 0);
	newres = oldres & (~O_NONBLOCK);
	fcntl (Socket, F_SETFL, newres);
}

void switch_nonblocking (int Socket)
{
	int oldres;
	int newres;

	oldres = fcntl (Socket, F_GETFL, 0);
	newres = oldres | (O_NONBLOCK);
	fcntl (Socket, F_SETFL, newres);
}


//===============================================================================
// CONSTRUCTOR	Connection
// SYNOPSIS		Initializes memory and variables. Signs in connection to the
//				global SocketList.
//
// ARGUMENTS	flood		(int)		obsoleted.
//===============================================================================

Connection::Connection (int flood)
{
	theBuffer = new AsyncBuffer (16384);
	Connected = ST_DISCONNECTED;
	InternalBuffer[0] = 0;
	BufferPos = 0;
	SockID = 0;
	Direction = C_DIR_OUT;
	strcpy (Owner, "root");
	_Input = 1;
	Type = 0;
	Misc[0] = 0;
	blocksize = 512;
	waittime = 500;
	Sent = 0;
	if (flood) idle_out = 0;
	else idle_out = 1;
#ifdef USE_SSL
	SSL = NULL;
#endif
	Redirect = NULL;
}

//===============================================================================
// DESTRUCTOR	Connection
// SYNOPSIS		Frees up memory. Signs off connection from the global SocketList.
//===============================================================================

Connection::~Connection (void)
{
	if (Connected != ST_DISCONNECTED)
		Close ();
	Connected = ST_DISCONNECTED;
	delete (theBuffer);
}

#ifdef USE_SSL

void Connection::StartSSL (void)
{
	SS = SSL_new();
	SSL_set_fd (SS, Socket);
	SSL_use_RSAPrivateKey(SS,"key.pem");
	SSL_use_certificate(SS,"dummy.pem");
	SSL_accept (SS);
}

#endif

//===============================================================================
// METHOD		Open
// SYNOPSIS		Initiates a tcp connection
//
// ARGUMENTS	host		(char *)	Resolveable domain name of the host
//				port		(u_short)	The tcp port to connect to.
//
// RETURNS		A result value. <0 indicates an error.
//===============================================================================

int Connection::Open (char *host, u_short port)
{
	struct hostent *MyHostEnt;
	int             result = 0;
	int             pram = 1;

	strncpy (Address, host, 15);
	Address[15] = 0;
	
	InternalBuffer[0] = 0;
	BufferPos = 0;
	Socket = socket (PF_INET, SOCK_STREAM, 0);
	if (Socket > 0)
	{
		fcntl (Socket, F_SETFL, O_NONBLOCK);

		pram = 1;
		setsockopt (Socket, SOL_SOCKET, SO_KEEPALIVE, (char *) &pram, sizeof (int));

		Remote.sin_family = AF_INET;
		Remote.sin_port = htons (port);

		MyHostEnt = gethostbyname (host);
		if (MyHostEnt)
		{
			memcpy (&BindAddr, MyHostEnt->h_addr, sizeof (struct in_addr));

			Remote.sin_addr = BindAddr;

			result = connect (Socket, (const sockaddr *) &Remote, sizeof (Remote));
			ConnectionTime = time (NULL);
		}
		else
			result = -1;

		if (result == 0)
			Connected = ST_CONNECTED;
		else
		{
			if ((errno == EINPROGRESS) || (errno == 29))
			{
				Connected = ST_CONNECTING;
				result = 0;
			}
			else
			{
				Close ();
			}

		}
	}
	else
	{
		Close ();
		Connected = ST_DISCONNECTED;
	}
	return result;
}

//===============================================================================
// METHOD		Accept
// SYNOPSIS		Binds the connection to an accepted socket (typically invoked
// 				from the Listener class).
//
// ARGUMENTS	mySocket	(int)		The fildes of the socket to bind to
//
// RETURNS		method returns no data
//===============================================================================

void  Connection::Accept (int mySocket)
{
	socklen_t       blah;
	int             pri, sec, tert, quad;
	u_long          myAddress;
	fd_set          myfd;
	struct timeval  timeout;
	int				Escape;

	//fcntl (mySocket, F_SETFL, 0);

	timeout.tv_sec = (time_t) 0;
	timeout.tv_usec = 0;
	FD_ZERO (&myfd);
	FD_SET (mySocket, &myfd);
	Socket = 0;
	Sent = 0;
	Escape = 0;

	while ((Socket<=0) && (!Escape))
	{

		blah = sizeof (Remote);
		bzero (&Remote, blah);
		if (select (MAXFD, NULL, NULL, &myfd, &timeout) > 0)
		{
			if ( (errno != EAGAIN) && (errno != EINTR) ) Connected = ST_DISCONNECTED;
		}
		FD_ZERO (&myfd);
		FD_SET (mySocket, &myfd);

		Socket = accept (mySocket, (struct sockaddr *) &Remote, &blah);
		if ((Socket > 0) && (Socket < MAXFD))
		{
			fcntl (Socket, F_SETFL, O_NONBLOCK);
			int             res = 1;

			res = fcntl (Socket, F_GETFL, 0);
			res &= ~O_SYNC;
			fcntl (Socket, F_SETFL, res);
			myAddress = Remote.sin_addr.s_addr;
#ifdef LINUX
			quad = ((myAddress & 0xff000000) >> 24);
			tert = ((myAddress & 0x00ff0000) >> 16);
			sec = ((myAddress & 0x0000ff00) >> 8);
			pri = ((myAddress & 0x000000ff));
#else
			pri = ((myAddress & 0xff000000) >> 24);
			sec = ((myAddress & 0x00ff0000) >> 16);
			tert = ((myAddress & 0x0000ff00) >> 8);
			quad = ((myAddress & 0x000000ff));
#endif
			sprintf (Address, "%i.%i.%i.%i", pri, sec, tert, quad);
			Connected = ST_CONNECTED;
			Direction = C_DIR_IN;
			ConnectionTime = time (NULL);
		}
		else
		{
			Connected = ST_DISCONNECTED;
			Escape = 1;
			sleep (1);
		}
	}
}

void  Connection::AcceptUnix (int mySocket)
{
	socklen_t       blah;
//	int             pri, sec, tert, quad;
//	u_long          myAddress;
	fd_set          myfd;
	//struct timeval  timeout;

//	fcntl (mySocket, F_SETFL, 0);

//	timeout.tv_sec = (time_t) 0;
//	timeout.tv_usec = 0;
	FD_ZERO (&myfd);
	FD_SET (mySocket, &myfd);
	Socket = 0;
	Sent = 0;

	while (Socket<=0)
	{

		blah = sizeof (Local);
		Socket = accept (mySocket, (sockaddr *) &Local, &blah);
		if ((Socket > 0) && (Socket < MAXFD))
		{
			fcntl (Socket, F_SETFL, O_NONBLOCK);
			int             res = 1;

			res = fcntl (Socket, F_GETFL, 0);
			res &= ~O_SYNC;
			fcntl (Socket, F_SETFL, res);
			sprintf (Address, "127.0.0.1");
			Connected = ST_CONNECTED;
			Direction = C_DIR_IN;
			ConnectionTime = time (NULL);
		}
		else
		{
			Connected = ST_DISCONNECTED;
			_usleep (90000);
		}
	}
}

//===============================================================================
// METHOD		Accept2
// SYNOPSIS		Accepts a connection from a listening socket and binds to it.
//
// ARGUMENTS	mySocket	(int)		The fildes of the listening socket
//
// RETURNS		method returns no data
//===============================================================================

void Connection::Accept2 (int mySocket)
{
	//int             blah;
	int             pri, sec, tert, quad;
	u_long          myAddress;

	Sent = 0;

	//blah = sizeof (Remote);
	Socket = mySocket;
	if ((Socket > 0) && (Socket < MAXFD))
	{
		fcntl (Socket, F_SETFL, O_NONBLOCK);
		int             res = 1;

		res = fcntl (Socket, F_GETFL, 0);
		res &= ~O_SYNC;
		fcntl (Socket, F_SETFL, res);
		myAddress = Remote.sin_addr.s_addr;
		pri = ((myAddress & 0xff000000) >> 24);
		sec = ((myAddress & 0x00ff0000) >> 16);
		tert = ((myAddress & 0x0000ff00) >> 8);
		quad = ((myAddress & 0x000000ff));
		sprintf (Address, "%i.%i.%i.%i", pri, sec, tert, quad);
		Connected = ST_CONNECTED;
		Direction = C_DIR_IN;
		ConnectionTime = time (NULL);
	}
	else
	{
		Connected = ST_DISCONNECTED;
	}
}

//===============================================================================
// METHOD		Close
// SYNOPSIS		Close the tcp connection.
//
// RETURNS		method returns no data

void Connection::Close (void)
{
	if (Connected != ST_DISCONNECTED)
	{
		if (Redirect)
		{
			Connected = ST_DISCONNECTED;
			// Redirect is never set to an object; nothing to free.
			Redirect = NULL;
		}
		else
		{
			shutdown (Socket, 2);
			close (Socket);
			Connected = ST_DISCONNECTED;
			InternalBuffer[0] = 0;
			BufferPos = 0;
		}
		theBuffer->Pos = 0;
	}
}

//===============================================================================
// METHOD		Read
// SYNOPSIS		Attempts to read data from the socket into the buffer.
//
// ARGUMENTS	Buffer		(char *)	Storage space
//				size 		(int)		Size of the buffer
//
// RETURNS		The amount of data fetched. Negative for error.
//===============================================================================

int Connection::Read (char *Buffer, int size)
{
	//int             test;
	fd_set          myfd;
	struct timeval  timeout;
	//char            Input = 0;
	int             Delta = 0;
	int             SourcePos = 0;
	//int             NewPos;
	int             DestPos = 0;
//	int             sel;

	int             result = 0;

	timeout.tv_sec = (time_t) 0;
#ifndef FREEBSD
	timeout.tv_usec = 250;
#else
	timeout.tv_usec = 25000;
#endif

	if (!Redirect) // If redirected we can only write
	{
		if (Connected == ST_CONNECTED)
		{
			FD_ZERO (&myfd);
			FD_SET (Socket, &myfd);

			if (BufferPos < 0)
			{
				BufferPos = 0;
			}

			if (_Input > 0)
			{
				switch_blocking (Socket);
				if ((select (MAXFD, &myfd, NULL, NULL, &timeout) > 0) && (BufferPos < 3070))
				{
					switch_nonblocking (Socket);
#ifdef USE_SSL
					if (SS) Delta = SSL_read (SS, InternalBuffer + BufferPos, 256);
					else Delta = recv (Socket, InternalBuffer + BufferPos, 256, 0);
#else
					Delta = recv (Socket, InternalBuffer + BufferPos, 256, 0);
#endif
					if (Delta>0)
					{
						InternalBuffer[BufferPos+Delta] = 0;
					}
					if (Delta > 0)
					{
						BufferPos += Delta;
						InternalBuffer[BufferPos] = 0;
					}
					if ((Delta < 0) && (errno != EWOULDBLOCK))
					{
						Close ();
						Connected = ST_DISCONNECTED;
						//printf ("receive error\n");
						return ERR_GENERIC1;
					}
				}
				else
				{
					switch_nonblocking (Socket);
				}

				if (Type & C_TYPE_BINARY)
				{
					int             ReturnLength;

					ReturnLength = BufferPos;
					if (ReturnLength > size)
						ReturnLength = size;
					memcpy (Buffer, InternalBuffer, (size_t) ReturnLength);
					SourcePos = ReturnLength;
					//NewPos = SourcePos;
					DestPos = 0;
					while (SourcePos < BufferPos)
					{
						InternalBuffer[DestPos++] = InternalBuffer[SourcePos++];
					}
					BufferPos = DestPos;
					InternalBuffer[BufferPos] = 0;
					result = ReturnLength;
				}
				else if (xstrstr (InternalBuffer, "\n", BufferPos))
				{
					int				had10,had13;
					SourcePos = 0;

					while ((InternalBuffer[SourcePos] != 10) && (InternalBuffer[SourcePos] != 13) && ((DestPos + 1) < size))
					{
						if (SourcePos < BufferPos)
							Buffer[DestPos++] = InternalBuffer[SourcePos++];
						else
							Buffer[DestPos++] = '.';
					}
					
					had10 = had13 = 0;
					
					if (InternalBuffer[SourcePos]==10)
					{
						had10=1; ++SourcePos;
						if ((DestPos+1)<size) Buffer[DestPos++] = 10;
					}
					if (InternalBuffer[SourcePos]==13)
					{
						had13=1; ++SourcePos;
						if ((DestPos+1)<size) Buffer[DestPos++] = 13;
					}
					if ((InternalBuffer[SourcePos]==10)&&(!had10))
					{
						had10=1; ++SourcePos;
						if ((DestPos+1)<size) Buffer[DestPos++] = 10;
					}

					Buffer[DestPos] = 0;

					DestPos = 0;
					while (SourcePos < BufferPos)
					{
						InternalBuffer[DestPos++] = InternalBuffer[SourcePos++];
					}
					BufferPos = DestPos;
					InternalBuffer[BufferPos] = 0;
					result = 1;
				}
			}
			if (BufferPos > 4092)
			{
				result = ERR_EXCESS_FLOOD;
				//printf ("excess flood\n");
			}

		}
		else
		{
			result = -1;
		}
	}
	return result;
}

//===============================================================================
// METHOD		Send
// SYNOPSIS		Places a string in the sendqueue and attempts to empty the queue.
//
// ARGUMENTS	TheLine		(char *)	The string to send
//				Priority	(int)		Buffer priority (for priority signalling,
//										currently unimplemented.
//
// RETURNS		method returns no data.
//===============================================================================

void Connection::Send (char *TheLine, int Priority)
{
	//int             test;
	int             len;

	//printf (")>> %s\n", TheLine);

	if (waittime>_MAX_TIMEOUT) waittime=_MAX_TIMEOUT;

	if (Connected != ST_DISCONNECTED)
	{
		{
			if (strlen (TheLine))
			{
				theBuffer->Add (TheLine);
				if (Priority>0)
				{
					if (Connected != ST_CONNECTING)
					{
#ifdef USE_SSL
						if (SS) len = SSL_write (SS, theBuffer->Data, theBuffer->Pos);
						else len = send (Socket, theBuffer->Data, theBuffer->Pos, 0);
#else					
						len = send (Socket, theBuffer->Data, theBuffer->Pos, 0);
#endif
						if (len < 0)
						{
							if ((errno != EAGAIN)&&(errno != EWOULDBLOCK)&&(errno != EINTR))
							{
//								int errnum = errno;
								Close ();
								Connected = ST_DISCONNECTED;
							}
							else
							{
#ifndef FREEBSD
								_usleep (waittime);
#else
								_usleep (10*waittime);
#endif
								blocksize = (int) (blocksize / 1.2);
								waittime = waittime + 100;
							}
						}
						else
						{
							theBuffer->Shift (len);
							Sent += len;
							if (theBuffer->Pos > 0)
							{
#ifndef FREEBSD
								_usleep (waittime);
#else
								_usleep (10*waittime);
#endif
							}
						}
					}
				}
			}
		}
	}
}

//===============================================================================
// METHOD		SendF
// SYNOPSIS		printf-formatted implementation of Send method.
//
// ARGUMENTS	Priority	(int)		Unimplemented
//				theLine		(int)		Format string
// 				...						Extra arguments
//
// RETURNS		method returns no data.
//===============================================================================

void Connection::SendF (int Priority, char *theLine,...)
{
	char            OutString[16384];
	va_list         argptr;

	va_start (argptr, theLine);
	vsprintf (OutString, theLine, argptr);
	va_end (argptr);

	Send (OutString, Priority);
}

//===============================================================================
// METHOD		SendB
// SYNOPSIS		Binary data implementation of Send method.
//
// ARGUMENTS	Priority	(int)		Unimplemented
//				theLine		(char *)	Pointer to the binary data
//				theSize		(int)		Size of the data
//
// RETURNS		Method returns no data.
//===============================================================================

void Connection::SendB (int Priority, u_char *theLine, int theSize)
{
	int             len, xblocksize;

	if (blocksize < 32)
		blocksize = 32;

	if (waittime > _MAX_TIMEOUT) waittime=_MAX_TIMEOUT;

	if (Connected != ST_DISCONNECTED)
	{
		{
			theBuffer->Add (theLine, theSize);
			if (theBuffer->Pos > 0)
			{
				if (theBuffer->Pos < blocksize)
					xblocksize = theBuffer->Pos;
				else
					xblocksize = blocksize;

				if (blocksize < 512) _usleep (waittime);
				if (xblocksize<1) xblocksize=1;

#ifdef USE_SSL
				if (SS) len = SSL_write (SS, theBuffer->Data, xblocksize);
				else len = send (Socket, theBuffer->Data, xblocksize, 0);
#else
				len = send (Socket, theBuffer->Data, xblocksize, 0);
#endif

				if (len < 0)
				{
					if ((errno != EAGAIN)&&(errno != EWOULDBLOCK)&&(errno != EINTR))
					{
//						int errnum = errno;
						Close ();
						Connected = ST_DISCONNECTED;
					}
					else
					{
#ifndef FREEBSD
						_usleep (waittime);
#else
						_usleep (10*waittime);
#endif
						if (blocksize > 64)
							blocksize = (int) (blocksize / 1.2);
						waittime *= 2;
					}
				}
				else if (len == 0)
				{
				}
				else
				{
					theBuffer->Shift (len);
					Sent += len;
					if (blocksize < 4096)
						blocksize = (int) (1.2 * (double) blocksize);
					if (theBuffer->Pos > 0)
					{
#ifndef FREEBSD
						_usleep (waittime);
#else
						_usleep (10*waittime);
#endif
					}
				}
			}
		}
	}
}

//===============================================================================
// METHOD		SendQ
// SYNOPSIS		Calculates amount of data still waiting to be transmitted.
//
// RETURNS		The size of the AsyncBuffer.
//===============================================================================

int Connection::SendQ (void)
{
	if (Redirect) return 0;
	return theBuffer->Pos;
}

unsigned long Connection::ConnID (void)
{
	if (Redirect) return ( ((getpid() & 0xffff) << 12) | ((uintptr_t) Redirect & 0x0fff) );
	return ( ((getpid() & 0xffff) << 12) | (Socket & 0x0fff) );
}

unsigned long Connection::RemoteIP (void)
{
	if (Redirect) return 0xc0ff33;
	return (Remote.sin_addr.s_addr);
}

//===============================================================================
// METHOD 		Flush
// SYNOPSIS		Obsoleted.
//===============================================================================

void Connection::Flush (void)
{

}

//===============================================================================
// METHOD		StopInput
// SYNOPSIS		Indicates that there is no more inbound data to be expected.
//				Saves CPU time in the Handle method
//
// RETURNS		method returns no data.
//===============================================================================

void Connection::StopInput (void)
{
	_Input = 0;
}

void Connection::RestartInput (void)
{
	_Input = 1;
}

//===============================================================================
// METHOD		Handle
// SYNOPSIS		Takes care of filling and emptying inbound and outbound buffers.
//
// ARGUMENTS	Buffer		(char *)	Storage space
//				size		(int)		Size of the buffer
//
// RETURNS		The amount of data fetched. Negative for error.
//===============================================================================

void Connection::AwaitInput (int seconds)
{
	fd_set          myfd;
	struct timeval  timeout;

//	int             res, newres;

// 	res = fcntl (Socket, F_GETFL, 0);
// 	newres = res & (~O_NONBLOCK);
// 	fcntl (Socket, F_SETFL, newres);

	timeout.tv_sec = (time_t) seconds;
#ifndef FREEBSD
	timeout.tv_usec = 100;
#else
	timeout.tv_usec = 10000;
#endif

	FD_ZERO (&myfd);
	FD_SET (Socket, &myfd);

	select (MAXFD, &myfd, NULL, NULL, &timeout);
	
// 	fcntl (Socket, F_SETFL, res);
}

int Connection::Handle (char *Buffer, int Size)
{
	if (waittime > _MAX_TIMEOUT) waittime = _MAX_TIMEOUT;
	return Handle (Buffer, Size, 0, waittime, 0);
}

int Connection::Handle (char *Buffer, int Size, int TOsec, int TOusec, int fd)
{
	fd_set          myfd, myinfd, myoutfd;
	int             result = 0;
	//int             result2 = 0;
	int             i = 0;
	//int             xx = 0;
	time_t			uptime;
	int				oldfc = 0;
	int				newfc = 0;

	struct timeval  timeout;

	timeout.tv_sec = (time_t) TOsec;
#ifndef FREEBSD
	timeout.tv_usec = TOusec;
#else
	timeout.tv_usec = 100 * TOusec;
#endif

	int             blksize = 512;
	int 			selx;

	if (!Redirect)
	{
		if (idle_out)
		{
			if ( ( uptime = (time (NULL) - ConnectionTime)) > 50)
			{
				if (((Sent + theBuffer->Pos) / uptime) < 50)
				{
					Close ();
					Connected = ST_DISCONNECTED;
					eprintf ("idle timeout\n");
					return ERR_GENERIC1;
				}
			}
		}

		if ((Connected == ST_CONNECTED) || (BufferPos > 0))
		{
			FD_ZERO (&myinfd);
			FD_ZERO (&myoutfd);
			FD_SET (Socket, &myinfd);
			FD_SET (Socket, &myoutfd);
			if (fd>0)
			{
				oldfc = fcntl (fd, F_GETFL, 0);
				newfc = oldfc & (~O_NONBLOCK);
			    fcntl (fd, F_SETFL, newfc);
				FD_SET (fd, &myinfd);
			}
			result = Read (Buffer, Size);

			if (theBuffer->Pos > 128) timeout.tv_usec = 20*timeout.tv_usec;
			if (theBuffer->Pos > 0) _usleep (timeout.tv_usec);

			switch_blocking (Socket);
			
			if (result) selx = 0;
			else if (theBuffer->Pos) selx = select (MAXFD, &myinfd, &myoutfd, NULL, &timeout);
			else selx = select (MAXFD, &myinfd, NULL, NULL, &timeout);
			
			if ((result >= 0) && (selx > 0))
			{
				switch_nonblocking (Socket);
				if (fd) fcntl (fd, F_SETFL, oldfc);
				if (theBuffer->Pos)
				{
					if (theBuffer->Pos < blksize)
						blksize = theBuffer->Pos;

#ifdef USE_SSL
					if (SS) i = SS_write (SS, theBuffer->Data, blksize);
					else i = send (Socket, theBuffer->Data, blksize, 0);
#else						
					i = send (Socket, theBuffer->Data, blksize, 0);
#endif
					Sent += i;
					_usleep (waittime);
					if (i<blksize)
					{
						waittime = waittime + 100;
						_usleep (waittime);
					}
					if (i > 0)
					{
						theBuffer->Shift (i);
						NoWrite = 0;
					}
					if (i <= 0)
						if (errno != EAGAIN)
						{
							eprintf ("write error\n");
							result = ERR_GENERIC1;
						}
				}
				else
				{
					i = send (Socket, theBuffer->Data, 0, 0);
					if (i<0)
					{
						eprintf ("output sense error\n");
						result = ERR_GENERIC1;
					}
					else
					{
						if ((result == 0) && (selx>0))
						{
							result = Read (Buffer, Size);
							if ((result<=0) && errno != EINPROGRESS)
							{
								result = ERR_GENERIC1;
							}
						}
					}
				}
			}
			

			FD_ZERO (&myfd);
			FD_SET (Socket, &myfd);
			if (fd) FD_SET (fd, &myfd);
			timeout.tv_sec = 0;
			timeout.tv_usec = 0;

			switch_blocking (Socket);
			if (select (MAXFD, NULL, NULL, &myfd, &timeout) > 0)
			{
				result = ERR_GENERIC2;
			}
			switch_nonblocking (Socket);
		}
		else if (Connected == ST_DISCONNECTED)
		{
			result = ERROR;
		}
		if (Connected == ST_CONNECTING)
		{
			FD_ZERO (&myfd);
			FD_SET (Socket, &myfd);
			if (fd) FD_SET (fd, &myfd);
			if (select (MAXFD, NULL, &myfd, NULL, &timeout) > 0)
			{
				Connected = ST_CONNECTED;
				result = 0;
			}
			else
			{
				if ((time (NULL) - ConnectionTime) > 30)
				{
					result = ERR_TIMEOUT;
					Close ();
					Connected = ST_DISCONNECTED;
				}
			}
		}
	}
	return result;
}

char *xstrstr (char *haystack, char *needle, int len_haystack)
{
	int len_needle = strlen (needle);
	int success, i, j;

	char *crsr;
	
	if (len_haystack >= len_needle)
	{
		for (i=0;i<=(len_haystack - len_needle);++i)
		{
			crsr = haystack+i;
			if ((*crsr) == 0) *crsr = '\n';
			if ((*crsr) == (needle[0]))
			{
				success = 1;
				for (j=0;j<len_needle;++j)
				{
					if ((needle[j]) != (crsr[j]))
					{
						j = len_needle;
						success = 0;
					}
				}
				if (success) return (crsr);
			}
		}
	}
	return (NULL);
}
