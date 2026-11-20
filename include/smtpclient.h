#ifndef _SMTPCLIENT_H
#define _SMTPCLIENT_H 1

// Neo-PostOffice's SMTP client (RFC 5321) for sending mail straight to a
// mail server: TLS (port 465) or STARTTLS (port 587) with the certificate
// checks of NetStream (system CAs, the certificate store, host name), or
// no encryption for a server on the user's own network (port 25).

#include <functional>
#include <string>
#include <vector>

struct SmtpServer
{
	std::string		 Host;
	int				 Port = 0;			// 0: 465 for tls, 587 for starttls, 25 for none
	std::string		 Security = "tls";	// tls, starttls or none
	std::string		 User;				// empty: no login
	std::string		 Password;
};

struct SmtpResult
{
	bool			 Sent = false;
	bool			 Temporary = false;	// a 4xx reply or a lost connection: worth trying again
	std::string		 Error;				// why it was not sent, for the user
};

// Sends one message (headers, empty line, body; \n or \r\n line ends) from
// mailfrom to every recipient. Nothing is sent unless the server accepts
// every recipient. status reports progress; cancelled stops it.
SmtpResult smtp_send (const SmtpServer &server, const std::string &mailfrom,
					  const std::vector<std::string> &recipients, const std::string &message,
					  std::function<void (const std::string &)> status = nullptr,
					  std::function<bool ()> cancelled = nullptr);

// The SASL mechanisms the client can use, best first.
extern const char *const smtp_auth_mechanisms[];

// For the tests: one SCRAM exchange (RFC 5802 / 7677) worked out by hand.
// Given the client nonce, the password and the server-first message,
// fills in the client-final message and the server signature expected
// in server-final ("v=..."). False with err for a bad server message.
bool smtp_scram_compute (const std::string &hash, const std::string &user, const std::string &password,
						 const std::string &cnonce, const std::string &server_first,
						 std::string &client_final, std::string &server_signature, std::string &err);

// For the tests: the message as sent after DATA (CRLF line ends, lines
// starting with "." doubled, ending with CRLF . CRLF).
std::string smtp_data_block (const std::string &message);

#endif
