#ifndef _PODB_H
#define _PODB_H 1
//===============================================================================
// Neo-PostOffice database layer
// -----------------------------
// A small driver-independent interface to SQL databases, used by the
// PostgreSQL mail storage and the SQL mailbox.
//
// Statements use numbered placeholders, $1, $2, ..., with every driver.
// Values are always passed as parameters, never pasted into the SQL text.
//
// A Connection may be shared between threads: every call locks the
// connection's mutex. Hold Connection::Mutex yourself (it is recursive)
// to keep a transaction or a sequence of statements together.
//===============================================================================

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace podb
{
	// A statement parameter: text, binary data (bytea / blob) or NULL.
	struct Param
	{
		std::string	Value;
		bool		IsNull = false;
		bool		IsBinary = false;

		Param (const char *s) : Value (s ? s : ""), IsNull (s == NULL) {}
		Param (const std::string &s) : Value (s) {}
		Param (long v) : Value (std::to_string (v)) {}
		Param (int v) : Value (std::to_string (v)) {}
		Param (unsigned int v) : Value (std::to_string (v)) {}
		Param (unsigned long v) : Value (std::to_string (v)) {}

		static Param Null (void) { Param p (""); p.IsNull = true; return p; }
		static Param Bytes (const char *data, size_t len)
		{
			Param p (std::string (data ? data : "", data ? len : 0));
			p.IsBinary = true;
			return p;
		}
	};

	typedef std::vector<Param> Params;

	// The rows returned by a query. Binary columns (bytea / blob) are
	// returned as their raw bytes.
	class Result
	{
	public:
		int					 Rows (void) const { return (int) Data.size(); }
		int					 Cols (void) const { return Columns; }
		bool				 IsNull (int r, int c) const { return Nulls[r][c]; }
		const std::string	&Get (int r, int c) const { return Data[r][c]; }
		long				 GetLong (int r, int c) const;

		void				 Clear (void) { Data.clear(); Nulls.clear(); Columns = 0; Affected = 0; }

		std::vector<std::vector<std::string>>	Data;
		std::vector<std::vector<bool>>			Nulls;
		int										Columns = 0;
		long									Affected = 0;	// rows changed by INSERT/UPDATE/DELETE
	};

	class Connection
	{
	public:
		virtual				~Connection (void) {}

		// Runs one statement. Returns false on error (see Error()).
		virtual bool		 Exec (const std::string &sql, const Params &params = Params(),
								   Result *out = NULL) = 0;

		virtual bool		 Begin (void)	 { return Exec ("BEGIN"); }
		virtual bool		 Commit (void)	 { return Exec ("COMMIT"); }
		virtual bool		 Rollback (void) { return Exec ("ROLLBACK"); }

		// True while the connection to the server is usable.
		virtual bool		 Connected (void) = 0;

		virtual const char	*Driver (void) const = 0;
		std::string			 Error (void) { std::lock_guard<std::recursive_mutex> g (Mutex); return LastError; }

		std::recursive_mutex Mutex;

	protected:
		std::string			 LastError;
	};

	// Opens a PostgreSQL connection. conninfo is a libpq connection string
	// such as "host=db dbname=postoffice user=me"; an empty string uses the
	// libpq defaults (PGHOST, PGDATABASE, ~/.pgpass, ...). Returns NULL and
	// sets error if the build has no PostgreSQL support or connecting fails.
	std::unique_ptr<Connection>	ConnectPostgreSQL (const std::string &conninfo, std::string &error);

	bool	HavePostgreSQL (void);

	// Returns s as valid UTF-8. Text that is not valid UTF-8 (old mail in
	// ISO-8859-1, say) is converted from ISO-8859-1.
	std::string	Utf8 (const char *s);
}

#endif
