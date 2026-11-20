//===============================================================================
// smtpclient (Neo-PostOffice)
// ---------------------------
// Sends mail straight to a mail server (RFC 5321, with RFC 3207 STARTTLS,
// RFC 8314 implicit TLS, RFC 1870 SIZE, RFC 6152 8BITMIME and RFC 6531
// SMTPUTF8). The connection is a NetStream, so TLS and the certificate
// checks are the POP3 and IMAP clients' own. It replaces PostOffice 0.7's
// smtp.cpp, which spoke plain SMTP on port 25 only and could cut a
// message short at a line holding a single dot.
//
// GNU LGPL like PostOffice itself.
//===============================================================================

#include "smtpclient.h"
#include "netstream.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <map>

namespace
{
	// One reply: the code, the enhanced status code if given (RFC 3463),
	// and the text of every line.
	struct Reply
	{
		int					 Code = 0;
		std::vector<std::string> Lines;

		bool Positive (void) const { return Code >= 200 && Code < 400; }
		bool Transient (void) const { return Code >= 400 && Code < 500; }
		// "550 5.1.1 User unknown"; several lines joined with " / ".
		std::string Text (void) const
		{
			std::string s = std::to_string (Code);
			for (size_t i = 0; i < Lines.size (); ++i)
			{
				s += i ? " / " : " ";
				s += Lines[i];
			}
			return s;
		}
	};

	class Session
	{
	public:
		NetStream				 Net;
		std::map<std::string, std::string> Ext;	// EHLO keywords (upper case) and their parameters
		std::string				 Fail;			// set when the connection broke

		// Reads one reply, of one or more lines.
		bool Read (Reply &r)
		{
			r = Reply ();
			for (int n = 0; ; ++n)
			{
				std::string line;
				if (!Net.ReadLine (line))
				{
					Fail = Net.Error ().empty () ? "the connection was lost" : Net.Error ();
					return false;
				}
				if (n > 1000 || line.size () < 3 || !isdigit ((unsigned char) line[0]) ||
					!isdigit ((unsigned char) line[1]) || !isdigit ((unsigned char) line[2]) ||
					(line.size () > 3 && line[3] != ' ' && line[3] != '-'))
				{
					Fail = "the server's reply could not be understood: " + line.substr (0, 80);
					return false;
				}
				int code = atoi (line.substr (0, 3).c_str ());
				if (n > 0 && code != r.Code)
				{
					Fail = "the server's reply could not be understood (mixed reply codes)";
					return false;
				}
				r.Code = code;
				r.Lines.push_back (line.size () > 4 ? line.substr (4) : "");
				if (line.size () == 3 || line[3] == ' ') return true;
			}
		}

		bool Command (const std::string &cmd, Reply &r)
		{
			if (!Net.Write (cmd + "\r\n"))
			{
				Fail = Net.Error ().empty () ? "the connection was lost" : Net.Error ();
				return false;
			}
			return Read (r);
		}

		// EHLO, recording the extensions the server offers.
		bool Ehlo (const std::string &domain, Reply &r)
		{
			Ext.clear ();
			if (!Command ("EHLO " + domain, r)) return false;
			if (r.Code != 250) return true;
			for (size_t i = 1; i < r.Lines.size (); ++i)
			{
				std::string l = r.Lines[i];
				size_t sp = l.find (' ');
				std::string key = l.substr (0, sp), param = sp == std::string::npos ? "" : l.substr (sp + 1);
				for (char &c : key) c = toupper ((unsigned char) c);
				Ext[key] = param;
			}
			return true;
		}

		bool Has (const char *ext) const { return Ext.count (ext) > 0; }
	};

	std::string size_text (unsigned long long n)
	{
		if (n < 1024) return std::to_string (n) + " bytes";
		if (n < 10ULL * 1024 * 1024) return std::to_string ((n + 1023) / 1024) + " KB";
		return std::to_string ((n + 512 * 1024) / (1024 * 1024)) + " MB";
	}

	bool ascii_only (const std::string &s)
	{
		for (unsigned char c : s) if (c >= 0x80) return false;
		return true;
	}

	// The name the client gives in EHLO: this host's full domain name if
	// it has one, else the address of the connection as an address
	// literal (RFC 5321 4.1.4), never a made-up name.
	std::string ehlo_domain (int fd)
	{
		char host[256] = "";
		if (gethostname (host, sizeof (host) - 1) == 0 && strchr (host, '.')) return host;
		if (host[0])
		{
			struct addrinfo hints, *res = NULL;
			memset (&hints, 0, sizeof (hints));
			hints.ai_flags = AI_CANONNAME;
			if (getaddrinfo (host, NULL, &hints, &res) == 0 && res)
			{
				std::string canon = res->ai_canonname ? res->ai_canonname : "";
				freeaddrinfo (res);
				if (canon.find ('.') != std::string::npos && canon.find ("localhost") != 0) return canon;
			}
		}
		struct sockaddr_storage ss;
		socklen_t len = sizeof (ss);
		char addr[INET6_ADDRSTRLEN] = "";
		if (getsockname (fd, (struct sockaddr *) &ss, &len) == 0)
		{
			if (ss.ss_family == AF_INET &&
				inet_ntop (AF_INET, &((struct sockaddr_in *) &ss)->sin_addr, addr, sizeof (addr)))
				return std::string ("[") + addr + "]";
			if (ss.ss_family == AF_INET6 &&
				inet_ntop (AF_INET6, &((struct sockaddr_in6 *) &ss)->sin6_addr, addr, sizeof (addr)))
				return std::string ("[IPv6:") + addr + "]";
		}
		return "[127.0.0.1]";
	}

	// An address is sent between < and >; one that could break the
	// command line is refused.
	bool sane_address (const std::string &a)
	{
		if (a.empty () || a.size () > 320) return false;
		for (unsigned char c : a)
			if (c < 0x21 || c == 0x7f || c == '<' || c == '>') return false;
		return a.find ('@') != std::string::npos;
	}
}

std::string smtp_data_block (const std::string &message)
{
	std::string out;
	out.reserve (message.size () + message.size () / 40 + 8);
	bool line_start = true;
	for (size_t i = 0; i < message.size (); ++i)
	{
		char c = message[i];
		if (c == '\r')
		{
			// A lone CR becomes a line end too: SMTP knows only CRLF.
			if (i + 1 < message.size () && message[i + 1] == '\n') continue;
			c = '\n';
		}
		if (line_start && c == '.') out += '.';		// dot-stuffing (RFC 5321 4.5.2)
		if (c == '\n')
		{
			out += "\r\n";
			line_start = true;
			continue;
		}
		out += c;
		line_start = false;
	}
	if (!line_start) out += "\r\n";
	out += ".\r\n";
	return out;
}

SmtpResult smtp_send (const SmtpServer &server, const std::string &mailfrom,
					  const std::vector<std::string> &recipients, const std::string &message,
					  std::function<void (const std::string &)> status,
					  std::function<bool ()> cancelled)
{
	SmtpResult R;
	Session S;
	Reply r;
	auto say = [&](const std::string &s) { if (status) status (s); };
	// A broken connection may be a passing problem: try again later.
	auto lost = [&](const std::string &what) -> SmtpResult
	{
		R.Error = what + ": " + S.Fail + ".";
		R.Temporary = true;
		S.Net.Close ();
		return R;
	};
	auto refused = [&](const std::string &what) -> SmtpResult
	{
		R.Error = what + ": " + r.Text () + ".";
		R.Temporary = r.Transient ();
		Reply q;
		S.Command ("QUIT", q);
		S.Net.Close ();
		return R;
	};

	std::string security = server.Security;
	if (security != "starttls" && security != "none") security = "tls";
	int port = server.Port > 0 ? server.Port : security == "tls" ? 465 : security == "starttls" ? 587 : 25;
	const std::string &host = server.Host;

	if (host.empty ())
	{
		R.Error = "no mail server is set.";
		return R;
	}
	if (recipients.empty ())
	{
		R.Error = "the message has no recipients.";
		return R;
	}
	if (!sane_address (mailfrom))
	{
		R.Error = "the sender address \"" + mailfrom + "\" is not a valid address.";
		return R;
	}
	for (const std::string &a : recipients)
		if (!sane_address (a))
		{
			R.Error = "the recipient address \"" + a + "\" is not a valid address.";
			return R;
		}

	S.Net.Cancelled = cancelled;
	S.Net.Timeout = 300;				// RFC 5321 4.5.3.2: five minutes for replies

	say ("Connecting to " + host);
	if (!S.Net.Connect (host, port))
	{
		R.Error = "cannot reach the mail server " + host + ": " + S.Net.Error () + ".";
		R.Temporary = true;			// the server or the network may be back later
		S.Net.Close ();
		return R;
	}
	if (security == "tls" && !S.Net.StartTLS (""))
	{
		// A certificate that does not check out stays that way.
		R.Error = "cannot start TLS with the mail server " + host + ": " + S.Net.Error () + ".";
		S.Net.Close ();
		return R;
	}

	if (!S.Read (r)) return lost ("the mail server " + host + " did not greet");
	if (r.Code != 220) return refused ("the mail server " + host + " does not accept mail now");

	std::string domain = ehlo_domain (S.Net.Fd ());
	if (!S.Ehlo (domain, r)) return lost ("the mail server " + host + " did not answer EHLO");
	if (r.Code != 250)
	{
		// HELO only where no extension is needed: no encryption asked for.
		if (security != "none") return refused ("the mail server " + host + " refused EHLO");
		if (!S.Command ("HELO " + domain, r)) return lost ("the mail server " + host + " did not answer HELO");
		if (r.Code != 250) return refused ("the mail server " + host + " refused HELO");
	}

	if (security == "starttls")
	{
		if (!S.Has ("STARTTLS"))
		{
			R.Error = "the mail server " + host + " does not offer STARTTLS, and mail is not sent without"
					  " encryption. Choose TLS (port 465), or check the server.";
			Reply q;
			S.Command ("QUIT", q);
			return R;
		}
		if (!S.Command ("STARTTLS", r)) return lost ("the mail server " + host + " did not answer STARTTLS");
		if (r.Code != 220) return refused ("the mail server " + host + " refused STARTTLS");
		// NetStream refuses anything sent along with the reply (injection).
		if (!S.Net.StartTLS (""))
		{
			R.Error = "cannot start TLS with " + host + ": " + S.Net.Error () + ".";
			S.Net.Close ();
			return R;
		}
		// What was said before encryption counts for nothing (RFC 3207 4.2).
		if (!S.Ehlo (domain, r)) return lost ("the mail server " + host + " did not answer EHLO");
		if (r.Code != 250) return refused ("the mail server " + host + " refused EHLO after STARTTLS");
	}

	// What the message needs from the server.
	bool eight_bit = !ascii_only (message);
	bool utf8_addresses = !ascii_only (mailfrom);
	for (const std::string &a : recipients) if (!ascii_only (a)) utf8_addresses = true;
	if (utf8_addresses && !S.Has ("SMTPUTF8"))
	{
		R.Error = "an address has non-ASCII characters and the mail server " + host +
				  " does not accept those (no SMTPUTF8).";
		Reply q;
		S.Command ("QUIT", q);
		return R;
	}
	if (eight_bit && !S.Has ("8BITMIME") && !S.Has ("SMTPUTF8"))
	{
		R.Error = "the message has 8-bit text and the mail server " + host + " accepts only 7-bit mail"
				  " (no 8BITMIME).";
		Reply q;
		S.Command ("QUIT", q);
		return R;
	}
	std::string data = smtp_data_block (message);
	size_t size = data.size () - 3;		// without the closing "."
	if (S.Has ("SIZE"))
	{
		unsigned long long max = strtoull (S.Ext["SIZE"].c_str (), NULL, 10);
		if (max > 0 && size > max)
		{
			R.Error = "the message is " + size_text (size) + " and the mail server " + host +
					  " takes at most " + size_text (max) + ".";
			Reply q;
			S.Command ("QUIT", q);
			return R;
		}
	}

	// The envelope. Every recipient must be accepted, or nothing is sent:
	// a message to half its recipients is worse than none.
	std::string from = "MAIL FROM:<" + mailfrom + ">";
	if (S.Has ("SIZE")) from += " SIZE=" + std::to_string (size);
	if (eight_bit && S.Has ("8BITMIME")) from += " BODY=8BITMIME";
	if (utf8_addresses) from += " SMTPUTF8";
	say ("Sending from " + mailfrom);
	if (!S.Command (from, r)) return lost ("the connection to " + host + " was lost");
	if (!r.Positive ()) return refused ("the mail server " + host + " refused the sender " + mailfrom);

	std::string rejected;
	bool all_transient = true;
	for (const std::string &a : recipients)
	{
		if (!S.Command ("RCPT TO:<" + a + ">", r)) return lost ("the connection to " + host + " was lost");
		if (r.Positive ()) continue;
		rejected += (rejected.empty () ? "" : "; ") + a + " (" + r.Text () + ")";
		if (!r.Transient ()) all_transient = false;
	}
	if (!rejected.empty ())
	{
		R.Error = "the mail server " + host + " refused " + rejected + ". Nothing was sent.";
		R.Temporary = all_transient;
		Reply q;
		S.Command ("RSET", q);
		S.Command ("QUIT", q);
		return R;
	}

	if (!S.Command ("DATA", r)) return lost ("the connection to " + host + " was lost");
	if (r.Code != 354) return refused ("the mail server " + host + " refused the message");

	say ("Sending the message (" + size_text (size) + ")");
	if (!S.Net.Write (data)) return lost ("the connection to " + host + " was lost while sending");
	S.Net.Timeout = 600;				// RFC 5321 4.5.3.2: ten minutes after the final dot
	if (!S.Read (r))
	{
		// It may or may not have been taken; better a second copy than none.
		return lost ("the mail server " + host + " did not confirm the message");
	}
	if (r.Code != 250) return refused ("the mail server " + host + " did not take the message");

	R.Sent = true;
	S.Net.Timeout = 30;
	Reply q;
	S.Command ("QUIT", q);
	S.Net.Close ();
	return R;
}
