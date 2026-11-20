#ifndef _CERTSTORE_H
#define _CERTSTORE_H 1

// Neo-PostOffice's own store of trusted CA certificates, used by every TLS
// connection (POP3, IMAP, SMTP) on top of the system's CAs. Each
// certificate is one PEM file in ~/.postoffice/certs named after its
// SHA-256 fingerprint. Certificates get in only by importing a file.

#include <string>
#include <vector>

typedef struct x509_store_st X509_STORE;

namespace certstore
{
	struct Info
	{
		std::string	 File;			// path in the store
		std::string	 Name;			// common name, else organisation, else the subject
		std::string	 Subject;		// one line
		std::string	 Issuer;		// one line
		std::string	 NotBefore;		// YYYY-MM-DD
		std::string	 NotAfter;		// YYYY-MM-DD
		std::string	 Fingerprint;	// SHA-256, AA:BB:...
		bool		 IsCA = false;		// basicConstraints CA:TRUE
		bool		 SelfSigned = false;
		bool		 Expired = false;
		bool		 NotYetValid = false;
	};

	// ~/.postoffice/certs (created, mode 700, when needed).
	std::string		 Dir (void);

	// The certificates in the store, sorted by name.
	std::vector<Info> List (void);

	// Adds the certificates in a PEM (one or more) or DER file. Returns
	// false with err set when the file holds no certificate or a
	// certificate cannot be stored. added lists what was stored, already
	// those that were in the store, and skipped those that cannot make any
	// server trusted (neither a CA nor self-signed).
	bool			 Import (const std::string &path, std::vector<Info> &added,
							 std::vector<Info> &already, std::vector<Info> &skipped,
							 std::string &err);

	// Removes one certificate (by its File).
	bool			 Remove (const std::string &file, std::string &err);

	// Adds the store's certificates to an OpenSSL store; returns how many.
	int				 AddTo (X509_STORE *store);
}

#endif
