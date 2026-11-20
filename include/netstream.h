#ifndef _NETSTREAM_H
#define _NETSTREAM_H 1
//===============================================================================
// NetStream (Neo-PostOffice)
// --------------------------
// A connection to a mail server for the POP3 and IMAP clients: TCP over
// IPv4 or IPv6, optionally TLS from the start or after a STARTTLS/STLS
// command, and reading by whole lines of any length, by exact byte counts,
// or as a byte stream (libetpan's IMAP parser reads through ReadSome).
//
// Certificates are always checked: against the system's CA store, plus an
// optional CA file (a local CA), and against the host name. There is no
// way to turn that off.
//
// All calls block, waiting at most Timeout seconds for the server, and give
// up early when Cancelled() returns true (checked every half second). Every
// failure closes the connection and leaves a reason in Error().
//===============================================================================

#include <functional>
#include <string>

typedef struct ssl_st SSL;
typedef struct ssl_ctx_st SSL_CTX;

class NetStream
{
public:
	NetStream (void);
	~NetStream (void);

	// Connects to host:port.
	bool			 Connect (const std::string &host, int port);
	// Starts TLS on the open connection (right after Connect for TLS on
	// its own port, or after the server agreed to STARTTLS/STLS). The
	// certificate must be valid for the host given to Connect.
	bool			 StartTLS (const std::string &cafile);
	bool			 IsOpen (void) const { return fd >= 0; }
	bool			 IsTLS (void) const { return ssl != NULL; }
	void			 Close (void);

	// A line without its CRLF (or LF). Lines may be of any length.
	bool			 ReadLine (std::string &line);
	// Exactly n bytes, appended to out.
	bool			 ReadExact (size_t n, std::string &out);
	// Sends all of data.
	bool			 Write (const std::string &data);
	// Up to n bytes (at least one), for a byte-stream user (libetpan);
	// returns -1 on failure.
	long			 ReadSome (void *buf, size_t n);
	// Bytes read from the server and not yet taken.
	size_t			 Buffered (void) const { return in.size (); }
	// Bytes that can be read without waiting: in our buffer, or decrypted
	// by OpenSSL and not yet taken (the socket shows neither as readable).
	size_t			 Pending (void) const;
	int				 Fd (void) const { return fd; }

	const std::string &Error (void) const { return error; }

	int				 Timeout;				// seconds; default 60
	std::function<bool ()> Cancelled;		// optional

private:
	int				 fd;
	SSL				*ssl;
	SSL_CTX			*ctx;
	std::string		 host;
	std::string		 in;					// read but not yet returned
	std::string		 error;

	bool			 Fail (const std::string &why);
	bool			 Wait (bool for_write);
	bool			 Fill (void);				// reads more into in
};

#endif
