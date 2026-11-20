//===============================================================================
// certstore (Neo-PostOffice)
// --------------------------
// The CA certificates trusted by Neo-PostOffice's own TLS connections on top
// of the system's: one PEM file per certificate in ~/.postoffice/certs,
// named after its SHA-256 fingerprint. GNU LGPL like PostOffice itself.
//===============================================================================

#include "certstore.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <memory>

namespace
{
	struct X509Free { void operator() (X509 *x) const { X509_free (x); } };
	typedef std::unique_ptr<X509, X509Free> X509Ptr;

	std::string name_line (X509_NAME *n)
	{
		if (!n) return "";
		BIO *b = BIO_new (BIO_s_mem ());
		X509_NAME_print_ex (b, n, 0, (XN_FLAG_ONELINE & ~ASN1_STRFLGS_ESC_MSB) | ASN1_STRFLGS_UTF8_CONVERT);
		char *data = NULL;
		long len = BIO_get_mem_data (b, &data);
		std::string s (data ? data : "", len > 0 ? (size_t) len : 0);
		BIO_free (b);
		return s;
	}

	std::string name_entry (X509_NAME *n, int nid)
	{
		int i = X509_NAME_get_index_by_NID (n, nid, -1);
		if (i < 0) return "";
		ASN1_STRING *a = X509_NAME_ENTRY_get_data (X509_NAME_get_entry (n, i));
		unsigned char *u = NULL;
		int len = ASN1_STRING_to_UTF8 (&u, a);
		if (len < 0) return "";
		std::string s ((char *) u, len);
		OPENSSL_free (u);
		return s;
	}

	std::string date_of (const ASN1_TIME *t)
	{
		struct tm tm;
		if (!t || ASN1_TIME_to_tm (t, &tm) != 1) return "?";
		char buf[16];
		strftime (buf, sizeof (buf), "%Y-%m-%d", &tm);
		return buf;
	}

	std::string fingerprint (X509 *x, bool colons)
	{
		unsigned char md[EVP_MAX_MD_SIZE];
		unsigned int n = 0;
		X509_digest (x, EVP_sha256 (), md, &n);
		std::string s;
		char hex[4];
		for (unsigned int i = 0; i < n; ++i)
		{
			snprintf (hex, sizeof (hex), colons ? "%02X" : "%02x", md[i]);
			if (colons && i) s += ':';
			s += hex;
		}
		return s;
	}

	certstore::Info describe (X509 *x, const std::string &file)
	{
		certstore::Info I;
		I.File = file;
		X509_NAME *subject = X509_get_subject_name (x);
		I.Subject = name_line (subject);
		I.Issuer = name_line (X509_get_issuer_name (x));
		I.Name = name_entry (subject, NID_commonName);
		if (I.Name.empty ()) I.Name = name_entry (subject, NID_organizationName);
		if (I.Name.empty ()) I.Name = I.Subject;
		I.NotBefore = date_of (X509_get0_notBefore (x));
		I.NotAfter = date_of (X509_get0_notAfter (x));
		I.Fingerprint = fingerprint (x, true);
		I.IsCA = X509_check_ca (x) > 0;
		I.SelfSigned = (X509_get_extension_flags (x) & EXFLAG_SS) != 0;
		I.Expired = X509_cmp_current_time (X509_get0_notAfter (x)) < 0;
		I.NotYetValid = X509_cmp_current_time (X509_get0_notBefore (x)) > 0;
		return I;
	}

	// All certificates in a file: PEM (any number) or one DER.
	std::vector<X509Ptr> read_certs (const std::string &path, std::string &err)
	{
		std::vector<X509Ptr> out;
		FILE *f = fopen (path.c_str (), "rb");
		if (!f) { err = std::string ("cannot open it: ") + strerror (errno); return out; }
		ERR_clear_error ();
		while (X509 *x = PEM_read_X509 (f, NULL, NULL, NULL)) out.emplace_back (x);
		if (out.empty ())
		{
			rewind (f);
			ERR_clear_error ();
			if (X509 *x = d2i_X509_fp (f, NULL)) out.emplace_back (x);
		}
		fclose (f);
		ERR_clear_error ();
		if (out.empty ()) err = "it holds no certificate (PEM or DER)";
		return out;
	}

	bool ends_with (const std::string &s, const char *suffix)
	{
		size_t n = strlen (suffix);
		return s.size () >= n && s.compare (s.size () - n, n, suffix) == 0;
	}
}

std::string certstore::Dir (void)
{
	const char *home = getenv ("HOME");
	std::string base = std::string (home ? home : ".") + "/.postoffice";
	std::string dir = base + "/certs";
	mkdir (base.c_str (), 0700);
	mkdir (dir.c_str (), 0700);
	return dir;
}

std::vector<certstore::Info> certstore::List (void)
{
	std::vector<Info> out;
	std::string dir = Dir ();
	DIR *D = opendir (dir.c_str ());
	if (!D) return out;
	while (struct dirent *e = readdir (D))
	{
		std::string name = e->d_name;
		if (!ends_with (name, ".pem")) continue;
		std::string path = dir + "/" + name, err;
		std::vector<X509Ptr> certs = read_certs (path, err);
		if (!certs.empty ()) out.push_back (describe (certs[0].get (), path));
	}
	closedir (D);
	std::sort (out.begin (), out.end (), [](const Info &a, const Info &b)
	{
		int c = strcasecmp (a.Name.c_str (), b.Name.c_str ());
		return c ? c < 0 : a.Fingerprint < b.Fingerprint;
	});
	return out;
}

bool certstore::Import (const std::string &path, std::vector<Info> &added,
						std::vector<Info> &already, std::vector<Info> &skipped,
						std::string &err)
{
	std::vector<X509Ptr> certs = read_certs (path, err);
	if (certs.empty ()) return false;
	std::string dir = Dir ();
	for (X509Ptr &x : certs)
	{
		std::string file = dir + "/" + fingerprint (x.get (), false) + ".pem";
		// A certificate signed by someone else that is not a CA (a server's
		// own certificate from a CA) is no starting point for a check.
		if (X509_check_ca (x.get ()) <= 0 && !(X509_get_extension_flags (x.get ()) & EXFLAG_SS))
		{
			skipped.push_back (describe (x.get (), ""));
			continue;
		}
		struct stat st;
		if (stat (file.c_str (), &st) == 0) { already.push_back (describe (x.get (), file)); continue; }

		// Written to a temporary name, then renamed: never half a file.
		std::string tmp = file + ".tmp";
		int fd = open (tmp.c_str (), O_WRONLY | O_CREAT | O_TRUNC, 0644);
		FILE *f = fd >= 0 ? fdopen (fd, "w") : NULL;
		if (!f)
		{
			if (fd >= 0) close (fd);
			err = "cannot write to " + dir + ": " + strerror (errno);
			return false;
		}
		bool ok = PEM_write_X509 (f, x.get ()) == 1;
		ok = (fflush (f) == 0) && ok;
		ok = (fsync (fileno (f)) == 0) && ok;
		ok = (fclose (f) == 0) && ok;
		if (!ok || rename (tmp.c_str (), file.c_str ()) != 0)
		{
			unlink (tmp.c_str ());
			err = "cannot write to " + dir + ": " + strerror (errno);
			return false;
		}
		added.push_back (describe (x.get (), file));
	}
	return true;
}

bool certstore::Remove (const std::string &file, std::string &err)
{
	// Only files in the store.
	std::string dir = Dir () + "/";
	if (file.compare (0, dir.size (), dir) != 0 || file.find ('/', dir.size ()) != std::string::npos)
	{
		err = "not a file in the certificate store";
		return false;
	}
	if (unlink (file.c_str ()) != 0)
	{
		err = std::string ("cannot remove it: ") + strerror (errno);
		return false;
	}
	return true;
}

int certstore::AddTo (X509_STORE *store)
{
	int n = 0;
	for (const Info &I : List ())
	{
		std::string err;
		for (X509Ptr &x : read_certs (I.File, err))
		{
			// A certificate that is already there (also in the system's
			// store) is not an error.
			if (X509_STORE_add_cert (store, x.get ()) == 1) ++n;
			ERR_clear_error ();
		}
	}
	return n;
}
