#ifndef SOCKETCLASSES_H
#define SOCKETCLASSES_H 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include "buffer.h"
#include "usleep.h"

#define MAX_SENDQ 6000
#define MAX_RECVQ 256
#define ERROR -1
#define ERR_EXCESS_FLOOD -2
#define ERR_MAX_SENDQ -3
#define ERR_TIMEOUT -4
#define ERR_GENERIC1 -5
#define ERR_GENERIC2 -6
#define CONNECTION_TIMEOUT 30

#define C_DIR_OUT 0
#define C_DIR_IN 1

#define C_TYPE_SERVER 0x00
#define C_TYPE_SERVICE 0x10
#define C_TYPE_PARTYLINE 0x20
#define C_TYPE_BINARY 0x40
#define ST_CONNECTED 1
#define ST_DISCONNECTED 0
#define ST_CONNECTING -1

class Connection
{
	public:
							 Connection (int);
							~Connection (void);
		int					 Open (char *, u_short);
		
		void				 Accept (int);
		void				 AcceptUnix (int);
		void				 Accept2 (int);

#ifdef USE_SSL
		void				 StartSSL (void);
#endif

		void				 Flush (void);
		void				 Close (void);
		int					 Read (char *, int);
		
		void				 AwaitInput (int);
		int					 Handle (char *, int);
		int					 Handle (char *, int, int, int, int);
		void				 Send (const char *, int);
		void				 SendF (int, const char *, ...);
		void				 SendB (int, u_char *, int);
		void				 StopInput (void);
		void				 RestartInput (void);
		
		int					 SendQ (void);
		unsigned long		 ConnID (void);
		unsigned long		 RemoteIP (void);

		long				 SockID;
		int					 Type;
		int					 Direction;
		char				 Owner[32];
		char				 Misc[32];
		time_t				 ConnectionTime;
		int					 Connected;
		int					 BufferPos;
		char				 Address[48];
		AsyncBuffer			*theBuffer;
		unsigned long		 Sent;
		int					 idle_out;
	
	private:
		
		void				*Redirect;
		int					 blocksize;
		int					 waittime;
		int					 NoWrite;
		u_short				 _Input;		 
		struct sockaddr_in	 Remote;
		struct sockaddr_in	 Local;
		int					 Socket;
		struct in_addr		 BindAddr;

#ifdef USE_SSL
		SSL					*SS;
#endif
		
		char				 InternalBuffer[8192];
};

#endif
