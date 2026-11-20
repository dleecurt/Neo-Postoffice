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

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

namespace
{
	// One reply: the code, the enhanced status code if given (RFC 3463),
	// and the text of every line.
	struct Reply
	{
		int					 Code = 0;
		std::vector<std::string> Lines;

		bool Positive (void) const { return Code >= 200 && Code < 400; }
		// RFC 3463: 5.7.x is about security or policy (no login, relaying
		// refused, a sender the login may not use): the settings, not the
		// message.
		bool Policy (void) const
		{
			return !Lines.empty () && Lines[0].size () >= 4 && Lines[0][1] == '.' && Lines[0][2] == '7' &&
				   Lines[0][3] == '.' && (Lines[0][0] == '5' || Lines[0][0] == '4');
		}
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

	std::string b64_encode (const std::string &in)
	{
		std::string out (4 * ((in.size () + 2) / 3) + 1, '\0');
		int n = EVP_EncodeBlock ((unsigned char *) &out[0], (const unsigned char *) in.data (), (int) in.size ());
		out.resize (n > 0 ? n : 0);
		return out;
	}

	bool b64_decode (const std::string &in, std::string &out)
	{
		std::string clean;
		for (char c : in) if (!isspace ((unsigned char) c)) clean += c;
		if (clean.size () % 4) return false;
		out.assign (3 * clean.size () / 4 + 1, '\0');
		int n = EVP_DecodeBlock ((unsigned char *) &out[0], (const unsigned char *) clean.data (), (int) clean.size ());
		if (n < 0) return false;
		size_t pad = 0;
		if (!clean.empty () && clean.back () == '=') ++pad;
		if (clean.size () > 1 && clean[clean.size () - 2] == '=') ++pad;
		out.resize (n - pad);
		return true;
	}

	// Wipes a string that held a password.
	void wipe (std::string &s)
	{
		if (!s.empty ()) OPENSSL_cleanse (&s[0], s.size ());
		s.clear ();
	}

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

const char *const smtp_auth_mechanisms[] = { "SCRAM-SHA-256", "SCRAM-SHA-1", "PLAIN", "LOGIN", NULL };

// SCRAM (RFC 5802, RFC 7677): the client proves it knows the password
// without sending it, and the server proves it knows it too.
bool smtp_scram_compute (const std::string &hash, const std::string &user, const std::string &password,
						 const std::string &cnonce, const std::string &server_first,
						 std::string &client_final, std::string &server_signature, std::string &err)
{
	const EVP_MD *md = hash == "SHA-256" ? EVP_sha256 () : hash == "SHA-1" ? EVP_sha1 () : NULL;
	if (!md) { err = "unknown hash"; return false; }
	size_t mdlen = EVP_MD_size (md);

	// server-first: r=nonce,s=salt,i=iterations (an m= extension is fatal)
	std::string nonce, salt64;
	long iterations = 0;
	size_t pos = 0;
	while (pos <= server_first.size ())
	{
		size_t comma = server_first.find (',', pos);
		std::string attr = server_first.substr (pos, comma == std::string::npos ? std::string::npos : comma - pos);
		if (attr.size () >= 2 && attr[1] == '=')
		{
			if (attr[0] == 'r') nonce = attr.substr (2);
			else if (attr[0] == 's') salt64 = attr.substr (2);
			else if (attr[0] == 'i') iterations = strtol (attr.c_str () + 2, NULL, 10);
			else if (attr[0] == 'm') { err = "the server asked for an unknown SCRAM extension"; return false; }
		}
		if (comma == std::string::npos) break;
		pos = comma + 1;
	}
	std::string salt;
	if (nonce.size () <= cnonce.size () || nonce.compare (0, cnonce.size (), cnonce) != 0)
	{
		err = "the server's SCRAM nonce does not continue the client's";
		return false;
	}
	if (!b64_decode (salt64, salt) || salt.empty ()) { err = "the server's SCRAM salt is not valid"; return false; }
	// Fewer rounds than RFC 7677 asks for would make a stolen exchange easy
	// to attack; very many would only stall the client.
	if (iterations < 4096 || iterations > 10000000)
	{
		err = "the server asked for " + std::to_string (iterations) + " SCRAM rounds (4096 or more are needed)";
		return false;
	}

	unsigned char salted[EVP_MAX_MD_SIZE], client_key[EVP_MAX_MD_SIZE], stored_key[EVP_MAX_MD_SIZE],
				  server_key[EVP_MAX_MD_SIZE], client_sig[EVP_MAX_MD_SIZE], server_sig[EVP_MAX_MD_SIZE];
	unsigned int len = 0;
	if (PKCS5_PBKDF2_HMAC (password.data (), (int) password.size (), (const unsigned char *) salt.data (),
						   (int) salt.size (), (int) iterations, md, (int) mdlen, salted) != 1)
	{
		err = "cannot compute the SCRAM key";
		return false;
	}
	HMAC (md, salted, (int) mdlen, (const unsigned char *) "Client Key", 10, client_key, &len);
	EVP_Digest (client_key, mdlen, stored_key, &len, md, NULL);
	HMAC (md, salted, (int) mdlen, (const unsigned char *) "Server Key", 10, server_key, &len);

	std::string name;
	for (char c : user)
	{
		if (c == '=') name += "=3D";
		else if (c == ',') name += "=2C";
		else name += c;
	}
	std::string client_first_bare = "n=" + name + ",r=" + cnonce;
	std::string without_proof = "c=biws,r=" + nonce;	// biws: base64 of "n,,"
	std::string auth_message = client_first_bare + "," + server_first + "," + without_proof;
	HMAC (md, stored_key, (int) mdlen, (const unsigned char *) auth_message.data (), auth_message.size (), client_sig, &len);
	HMAC (md, server_key, (int) mdlen, (const unsigned char *) auth_message.data (), auth_message.size (), server_sig, &len);
	std::string proof (mdlen, '\0');
	for (size_t i = 0; i < mdlen; ++i) proof[i] = (char) (client_key[i] ^ client_sig[i]);
	client_final = without_proof + ",p=" + b64_encode (proof);
	server_signature = b64_encode (std::string ((char *) server_sig, mdlen));
	OPENSSL_cleanse (salted, sizeof (salted));
	OPENSSL_cleanse (client_key, sizeof (client_key));
	OPENSSL_cleanse (server_key, sizeof (server_key));
	return true;
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

static SmtpResult send_once (const SmtpServer &server, const std::string &mailfrom,
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
	auto refused = [&](const std::string &what, bool setup = false) -> SmtpResult
	{
		R.Error = what + ": " + r.Text () + ".";
		R.Temporary = r.Transient ();
		R.Setup = setup && !R.Temporary;
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
		R.Setup = true;
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

	// A password is never sent where it could be read on the way.
	if (!server.User.empty () && security == "none")
	{
		R.Error = "the identity has a login for " + host + " but no encryption, and a password is never sent"
				  " unencrypted. Choose TLS or STARTTLS.";
		R.Setup = true;
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
		R.Setup = true;
		S.Net.Close ();
		return R;
	}

	if (!S.Read (r)) return lost ("the mail server " + host + " did not greet");
	if (r.Code != 220) return refused ("the mail server " + host + " does not accept mail now", true);

	std::string domain = ehlo_domain (S.Net.Fd ());
	if (!S.Ehlo (domain, r)) return lost ("the mail server " + host + " did not answer EHLO");
	if (r.Code != 250)
	{
		// HELO only where no extension is needed: no encryption asked for.
		if (security != "none") return refused ("the mail server " + host + " refused EHLO", true);
		if (!S.Command ("HELO " + domain, r)) return lost ("the mail server " + host + " did not answer HELO");
		if (r.Code != 250) return refused ("the mail server " + host + " refused HELO", true);
	}

	if (security == "starttls")
	{
		if (!S.Has ("STARTTLS"))
		{
			R.Error = "the mail server " + host + " does not offer STARTTLS, and mail is not sent without"
					  " encryption. Choose TLS (port 465), or check the server.";
			R.Setup = true;
			Reply q;
			S.Command ("QUIT", q);
			return R;
		}
		if (!S.Command ("STARTTLS", r)) return lost ("the mail server " + host + " did not answer STARTTLS");
		if (r.Code != 220) return refused ("the mail server " + host + " refused STARTTLS", true);
		// NetStream refuses anything sent along with the reply (injection).
		if (!S.Net.StartTLS (""))
		{
			R.Error = "cannot start TLS with " + host + ": " + S.Net.Error () + ".";
			R.Setup = true;
			S.Net.Close ();
			return R;
		}
		// What was said before encryption counts for nothing (RFC 3207 4.2).
		if (!S.Ehlo (domain, r)) return lost ("the mail server " + host + " did not answer EHLO");
		if (r.Code != 250) return refused ("the mail server " + host + " refused EHLO after STARTTLS", true);
	}

	// Logging in (RFC 4954), if the identity has a user name.
	if (!server.User.empty ())
	{
		if (!S.Has ("AUTH"))
		{
			R.Error = "the mail server " + host + " offers no login (no AUTH). If it takes your mail without"
					  " one, leave the user name empty.";
			R.Setup = true;
			Reply q;
			S.Command ("QUIT", q);
			return R;
		}
		std::vector<std::string> offered;
		{
			std::string list = S.Ext["AUTH"];
			size_t p = 0;
			while (p < list.size ())
			{
				size_t e = list.find (' ', p);
				std::string m = list.substr (p, e == std::string::npos ? std::string::npos : e - p);
				for (char &c : m) c = toupper ((unsigned char) c);
				if (!m.empty ()) offered.push_back (m);
				if (e == std::string::npos) break;
				p = e + 1;
			}
		}
		std::string mech;
		for (int i = 0; smtp_auth_mechanisms[i] && mech.empty (); ++i)
			for (const std::string &m : offered)
				if (m == smtp_auth_mechanisms[i]) { mech = m; break; }
		if (mech.empty ())
		{
			R.Error = "the mail server " + host + " offers no login method Neo-PostOffice knows (it offers: " +
					  S.Ext["AUTH"] + ").";
			R.Setup = true;
			Reply q;
			S.Command ("QUIT", q);
			return R;
		}

		say ("Logging in to " + host);
		std::string password = server.Password;
		auto failed = [&](const std::string &how) -> SmtpResult
		{
			wipe (password);
			// 535: wrong name or password; 534: the method is not good enough
			// (for example an app password is needed); 4xx: try later.
			if (r.Code == 535)
				R.Error = "the mail server " + host + " refused the login for " + server.User + ": " + r.Text () +
						  ". Check the user name and password in Config \xe2\x86\x92 Identities.";
			else
				R.Error = "logging in to " + host + " with " + mech + " failed" + (how.empty () ? "" : " (" + how + ")") +
						  (r.Code ? ": " + r.Text () : "") + ".";
			R.Temporary = r.Transient ();
			R.Setup = !R.Temporary;
			Reply q;
			S.Command ("QUIT", q);
			S.Net.Close ();
			return R;
		};
		auto broken = [&]() -> SmtpResult { wipe (password); return lost ("the connection to " + host + " was lost while logging in"); };

		if (mech == "SCRAM-SHA-256" || mech == "SCRAM-SHA-1")
		{
			unsigned char rnd[24];
			if (RAND_bytes (rnd, sizeof (rnd)) != 1) { r = Reply (); return failed ("no random numbers"); }
			std::string cnonce = b64_encode (std::string ((char *) rnd, sizeof (rnd)));
			std::string name;
			for (char c : server.User) name += c == '=' ? "=3D" : c == ',' ? "=2C" : std::string (1, c);
			std::string client_first = "n,,n=" + name + ",r=" + cnonce;
			if (!S.Command ("AUTH " + mech + " " + b64_encode (client_first), r)) return broken ();
			std::string server_first, client_final, expected, err;
			if (r.Code != 334) return failed ("");
			if (r.Lines.empty () || !b64_decode (r.Lines[0], server_first))
			{
				S.Command ("*", r);
				r = Reply ();
				return failed ("the server's challenge could not be read");
			}
			if (!smtp_scram_compute (mech == "SCRAM-SHA-256" ? "SHA-256" : "SHA-1", server.User, password,
									 cnonce, server_first, client_final, expected, err))
			{
				S.Command ("*", r);			// cancel the exchange
				r = Reply ();
				return failed (err);
			}
			if (!S.Command (b64_encode (client_final), r)) return broken ();
			if (r.Code != 334) return failed ("");
			// server-final: "v=" and the server's signature, or "e=" and an error.
			std::string server_final;
			if (r.Lines.empty () || !b64_decode (r.Lines[0], server_final)) server_final.clear ();
			std::string got = server_final.compare (0, 2, "v=") == 0 ? server_final.substr (2) : "";
			if (got.size () != expected.size () || CRYPTO_memcmp (got.data (), expected.data (), got.size ()) != 0)
			{
				S.Command ("*", r);
				r = Reply ();
				return failed (server_final.compare (0, 2, "e=") == 0 ? "the server said " + server_final.substr (2)
								: "the server could not prove that it knows the password: it may not be your server");
			}
			if (!S.Command ("", r)) return broken ();
		}
		else if (mech == "PLAIN")
		{
			std::string token = std::string (1, '\0') + server.User + std::string (1, '\0') + password;
			std::string line = "AUTH PLAIN " + b64_encode (token);
			wipe (token);
			bool ok = S.Command (line, r);
			wipe (line);
			if (!ok) return broken ();
			if (r.Code == 334)			// a server that wants it after the challenge
			{
				if (!S.Command ("*", r)) return broken ();
				return failed ("");
			}
		}
		else	// LOGIN
		{
			if (!S.Command ("AUTH LOGIN", r)) return broken ();
			if (r.Code != 334) return failed ("");
			if (!S.Command (b64_encode (server.User), r)) return broken ();
			if (r.Code != 334) return failed ("");
			std::string line = b64_encode (password);
			bool ok = S.Command (line, r);
			wipe (line);
			if (!ok) return broken ();
		}
		wipe (password);
		if (r.Code != 235) return failed ("");
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
	if (r.Code == 530 && server.User.empty ())
		return refused ("the mail server " + host + " wants a login: set the user name and password in"
						" Config \xe2\x86\x92 Identities. It said", true);
	if (!r.Positive ()) return refused ("the mail server " + host + " refused the sender " + mailfrom, r.Policy ());

	std::string rejected;
	bool all_transient = true, all_policy = true;
	for (const std::string &a : recipients)
	{
		if (!S.Command ("RCPT TO:<" + a + ">", r)) return lost ("the connection to " + host + " was lost");
		if (r.Positive ()) continue;
		rejected += (rejected.empty () ? "" : "; ") + a + " (" + r.Text () + ")";
		if (!r.Transient ()) all_transient = false;
		if (!r.Policy ()) all_policy = false;
	}
	if (!rejected.empty ())
	{
		R.Error = "the mail server " + host + " refused " + rejected + ". Nothing was sent.";
		// Refused for policy (Postfix: "Client host rejected", "Relay access
		// denied"): the server wants a login, or another one.
		if (all_policy && server.User.empty () && S.Has ("AUTH"))
			R.Error += " The server probably wants a login: set the user name and password in Config \xe2\x86\x92"
					   " Identities.";
		R.Temporary = all_transient;
		R.Setup = all_policy && !all_transient;
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

SmtpResult smtp_send (const SmtpServer &server, const std::string &mailfrom,
					  const std::vector<std::string> &recipients, const std::string &message,
					  std::function<void (const std::string &)> status,
					  std::function<bool ()> cancelled)
{
	SmtpResult R = send_once (server, mailfrom, recipients, message, status, cancelled);
	// Stopped by the user: whatever broke on the way is not the reason.
	if (!R.Sent && cancelled && cancelled ())
	{
		R.Cancelled = true;
		R.Temporary = R.Setup = false;
		R.Error = "sending was stopped.";
	}
	return R;
}
