//===============================================================================
// NetStream (Neo-PostOffice): see netstream.h.
// GNU LGPL like PostOffice itself.
//===============================================================================

#include "netstream.h"

#include <algorithm>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

static const size_t MAX_LINE = 64 * 1024 * 1024;	// a line longer than this is an error

NetStream::NetStream (void) : Timeout (60), fd (-1), ssl (NULL), ctx (NULL)
{
}

NetStream::~NetStream (void)
{
	Close ();
}

void NetStream::Close (void)
{
	if (ssl)
	{
		SSL_free (ssl);
		ssl = NULL;
	}
	if (ctx)
	{
		SSL_CTX_free (ctx);
		ctx = NULL;
	}
	if (fd >= 0)
	{
		close (fd);
		fd = -1;
	}
	in.clear ();
}

bool NetStream::Fail (const std::string &why)
{
	error = why;
	Close ();
	return false;
}

// Waits until the socket can be read (or written), the time is up or the
// caller cancels.
bool NetStream::Wait (bool for_write)
{
	time_t start = time (NULL);
	for (;;)
	{
		if (Cancelled && Cancelled ()) return Fail ("cancelled");
		struct pollfd p = { fd, (short) (for_write ? POLLOUT : POLLIN), 0 };
		int r = poll (&p, 1, 500);
		if (r > 0) return true;
		if (r < 0 && errno != EINTR) return Fail (std::string ("connection error: ") + strerror (errno));
		if (time (NULL) - start >= Timeout)
			return Fail ("the server did not answer within " + std::to_string (Timeout) + " seconds");
	}
}

bool NetStream::Connect (const std::string &_host, int port)
{
	Close ();
	error.clear ();
	host = _host;

	struct addrinfo hints = {}, *res = NULL;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	int g = getaddrinfo (host.c_str(), std::to_string (port).c_str(), &hints, &res);
	if (g != 0) return Fail ("cannot find the server " + host + ": " + gai_strerror (g));

	std::string last = "no address";
	for (struct addrinfo *a = res; a; a = a->ai_next)
	{
		fd = socket (a->ai_family, a->ai_socktype | SOCK_CLOEXEC, a->ai_protocol);
		if (fd < 0) { last = strerror (errno); continue; }
		fcntl (fd, F_SETFL, fcntl (fd, F_GETFL, 0) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
		int one = 1;
		setsockopt (fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof (one));
#endif
		if (connect (fd, a->ai_addr, a->ai_addrlen) == 0) break;
		if (errno == EINPROGRESS)
		{
			int saved_timeout = Timeout;
			Timeout = Timeout > 30 ? 30 : Timeout;	// per address
			bool ready = Wait (true);
			Timeout = saved_timeout;
			if (!ready)
			{
				if (error == "cancelled") { freeaddrinfo (res); return false; }
				last = error;
				continue;			// Wait closed fd
			}
			int soerr = 0;
			socklen_t len = sizeof (soerr);
			getsockopt (fd, SOL_SOCKET, SO_ERROR, &soerr, &len);
			if (soerr == 0) break;
			last = strerror (soerr);
		}
		else last = strerror (errno);
		close (fd);
		fd = -1;
	}
	freeaddrinfo (res);
	if (fd < 0) return Fail ("cannot connect to " + host + " port " + std::to_string (port) + ": " + last);
	error.clear ();
	return true;
}

static std::string ssl_errors (void)
{
	std::string out;
	unsigned long e;
	while ((e = ERR_get_error ()) != 0)
	{
		char buf[256];
		ERR_error_string_n (e, buf, sizeof (buf));
		if (!out.empty()) out += "; ";
		out += buf;
	}
	return out.empty() ? "unknown TLS error" : out;
}

bool NetStream::StartTLS (const std::string &cafile)
{
	if (fd < 0) return Fail ("not connected");
	// Anything the server sent before the handshake would be unprotected
	// text pretending to come after it (a STARTTLS injection).
	if (!in.empty()) return Fail ("the server sent data before TLS was started");
	ERR_clear_error ();

	ctx = SSL_CTX_new (TLS_client_method ());
	if (!ctx) return Fail ("cannot set up TLS: " + ssl_errors ());
	SSL_CTX_set_min_proto_version (ctx, TLS1_2_VERSION);
	SSL_CTX_set_verify (ctx, SSL_VERIFY_PEER, NULL);
	SSL_CTX_set_mode (ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
	if (SSL_CTX_set_default_verify_paths (ctx) != 1)
		return Fail ("cannot load the system's certificate authorities: " + ssl_errors ());
	if (!cafile.empty() && SSL_CTX_load_verify_locations (ctx, cafile.c_str(), NULL) != 1)
		return Fail ("cannot load the CA file " + cafile + ": " + ssl_errors ());

	ssl = SSL_new (ctx);
	if (!ssl) return Fail ("cannot set up TLS: " + ssl_errors ());
	SSL_set_fd (ssl, fd);

	// The certificate must name this host (or this address).
	unsigned char addr[16];
	bool is_ip = inet_pton (AF_INET, host.c_str(), addr) == 1 || inet_pton (AF_INET6, host.c_str(), addr) == 1;
	if (is_ip)
		X509_VERIFY_PARAM_set1_ip_asc (SSL_get0_param (ssl), host.c_str());
	else
	{
		SSL_set_tlsext_host_name (ssl, host.c_str());
		SSL_set_hostflags (ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
		SSL_set1_host (ssl, host.c_str());
	}

	for (;;)
	{
		int r = SSL_connect (ssl);
		if (r == 1) break;
		int e = SSL_get_error (ssl, r);
		if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
		{
			if (!Wait (e == SSL_ERROR_WANT_WRITE)) return false;
			continue;
		}
		long v = SSL_get_verify_result (ssl);
		if (v != X509_V_OK)
			return Fail ("the certificate of " + host + " is not trusted: " + X509_verify_cert_error_string (v));
		if (e == SSL_ERROR_SYSCALL && ERR_peek_error () == 0)
			return Fail ("the server closed the connection during the TLS handshake");
		return Fail ("TLS handshake with " + host + " failed: " + ssl_errors ());
	}
	return true;
}

bool NetStream::Fill (void)
{
	char buf[16384];
	for (;;)
	{
		if (fd < 0) return Fail (error.empty () ? "not connected" : error);
		ssize_t n;
		if (ssl)
		{
			ERR_clear_error ();
			int r = SSL_read (ssl, buf, sizeof (buf));
			if (r > 0) { in.append (buf, r); return true; }
			int e = SSL_get_error (ssl, r);
			if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
			{
				if (!Wait (e == SSL_ERROR_WANT_WRITE)) return false;
				continue;
			}
			if (e == SSL_ERROR_ZERO_RETURN || (e == SSL_ERROR_SYSCALL && ERR_peek_error () == 0))
				return Fail ("the server closed the connection");
			return Fail ("TLS error: " + ssl_errors ());
		}
		n = recv (fd, buf, sizeof (buf), 0);
		if (n > 0) { in.append (buf, n); return true; }
		if (n == 0) return Fail ("the server closed the connection");
		if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
		{
			if (!Wait (false)) return false;
			continue;
		}
		return Fail (std::string ("connection error: ") + strerror (errno));
	}
}

bool NetStream::ReadLine (std::string &line)
{
	size_t searched = 0;
	for (;;)
	{
		size_t nl = in.find ('\n', searched);
		if (nl != std::string::npos)
		{
			size_t end = (nl > 0 && in[nl - 1] == '\r') ? nl - 1 : nl;
			line.assign (in, 0, end);
			in.erase (0, nl + 1);
			return true;
		}
		searched = in.size ();
		if (in.size () > MAX_LINE) return Fail ("the server sent a line longer than 64 MB");
		if (!Fill ()) return false;
	}
}

bool NetStream::ReadExact (size_t n, std::string &out)
{
	while (in.size () < n)
		if (!Fill ()) return false;
	out.append (in, 0, n);
	in.erase (0, n);
	return true;
}

long NetStream::ReadSome (void *buf, size_t n)
{
	if (n == 0) return 0;
	if (in.empty () && !Fill ()) return -1;
	size_t k = std::min (n, in.size ());
	memcpy (buf, in.data (), k);
	in.erase (0, k);
	return (long) k;
}

bool NetStream::Write (const std::string &data)
{
	size_t done = 0;
	while (done < data.size ())
	{
		if (fd < 0) return Fail (error.empty () ? "not connected" : error);
		if (ssl)
		{
			ERR_clear_error ();
			int r = SSL_write (ssl, data.data () + done, (int) std::min (data.size () - done, (size_t) 65536));
			if (r > 0) { done += r; continue; }
			int e = SSL_get_error (ssl, r);
			if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE)
			{
				if (!Wait (e == SSL_ERROR_WANT_WRITE)) return false;
				continue;
			}
			return Fail ("TLS error while sending: " + ssl_errors ());
		}
#ifdef MSG_NOSIGNAL
		ssize_t n = send (fd, data.data () + done, data.size () - done, MSG_NOSIGNAL);
#else
		ssize_t n = send (fd, data.data () + done, data.size () - done, 0);
#endif
		if (n > 0) { done += n; continue; }
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		{
			if (!Wait (true)) return false;
			continue;
		}
		return Fail (std::string ("connection error while sending: ") + strerror (errno));
	}
	return true;
}
