//===============================================================================
// Neo-PostOffice database layer: PostgreSQL driver (libpq)
//===============================================================================

#include "podb.h"

#ifdef PO_HAVE_POSTGRESQL

#include <libpq-fe.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

namespace podb
{

static const Oid BYTEAOID = 17;

static void quiet_notices (void *, const char *message)
{
	// NOTICEs such as "relation already exists, skipping" are not errors.
	dprintf ("[pgsql] %s", message);
}

class PgConnection : public Connection
{
public:
	explicit PgConnection (PGconn *c) : Conn (c) {}
	~PgConnection (void) override { if (Conn) PQfinish (Conn); }

	const char *Driver (void) const override { return "postgresql"; }

	bool Connected (void) override
	{
		std::lock_guard<std::recursive_mutex> g (Mutex);
		return Conn && PQstatus (Conn) == CONNECTION_OK;
	}

	bool Exec (const std::string &sql, const Params &params = Params(), Result *out = NULL) override
	{
		std::lock_guard<std::recursive_mutex> g (Mutex);

		// Retry once after re-establishing a lost connection, but only
		// outside a transaction: a reset discards the transaction.
		bool in_transaction = PQtransactionStatus (Conn) != PQTRANS_IDLE;
		if (Run (sql, params, out)) return true;
		if (PQstatus (Conn) == CONNECTION_BAD && !in_transaction)
		{
			eprintf ("[pgsql] connection lost (%s), reconnecting\n", LastError.c_str());
			PQreset (Conn);
			if (PQstatus (Conn) == CONNECTION_OK)
			{
				PQsetClientEncoding (Conn, "UTF8");
				return Run (sql, params, out);
			}
		}
		return false;
	}

private:
	bool Run (const std::string &sql, const Params &params, Result *out)
	{
		std::vector<const char *> values (params.size());
		std::vector<int> lengths (params.size()), formats (params.size());

		for (size_t i = 0; i < params.size(); ++i)
		{
			values[i]  = params[i].IsNull ? NULL : params[i].Value.data();
			lengths[i] = (int) params[i].Value.size();
			formats[i] = params[i].IsBinary ? 1 : 0;
		}

		PGresult *res = PQexecParams (Conn, sql.c_str(), (int) params.size(), NULL,
									  values.data(), lengths.data(), formats.data(), 0);
		ExecStatusType st = PQresultStatus (res);

		if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK)
		{
			// Keep the first line; the rest is query context for a terminal.
			LastError = PQerrorMessage (Conn);
			size_t nl = LastError.find ('\n');
			if (nl != std::string::npos) LastError.erase (nl);
			while (!LastError.empty() && LastError.back() == ' ') LastError.pop_back();
			PQclear (res);
			return false;
		}
		LastError.clear();

		if (out)
		{
			out->Clear();
			out->Affected = atol (PQcmdTuples (res));
			out->Columns = PQnfields (res);
			int rows = PQntuples (res);
			out->Data.resize (rows);
			out->Nulls.resize (rows);
			for (int r = 0; r < rows; ++r)
			{
				out->Data[r].resize (out->Columns);
				out->Nulls[r].resize (out->Columns);
				for (int c = 0; c < out->Columns; ++c)
				{
					if (PQgetisnull (res, r, c)) { out->Nulls[r][c] = true; continue; }
					const char *v = PQgetvalue (res, r, c);
					if (PQftype (res, c) == BYTEAOID)
					{
						size_t len = 0;
						unsigned char *raw = PQunescapeBytea ((const unsigned char *) v, &len);
						if (raw)
						{
							out->Data[r][c].assign ((const char *) raw, len);
							PQfreemem (raw);
						}
					}
					else out->Data[r][c].assign (v, PQgetlength (res, r, c));
				}
			}
		}
		PQclear (res);
		return true;
	}

	PGconn *Conn;
};

bool HavePostgreSQL (void) { return true; }

std::unique_ptr<Connection> ConnectPostgreSQL (const std::string &conninfo, std::string &error)
{
	PGconn *conn = PQconnectdb (conninfo.c_str());
	if (!conn)
	{
		error = "out of memory";
		return NULL;
	}
	if (PQstatus (conn) != CONNECTION_OK)
	{
		error = PQerrorMessage (conn);
		while (!error.empty() && (error.back() == '\n' || error.back() == ' ')) error.pop_back();
		PQfinish (conn);
		return NULL;
	}
	PQsetNoticeProcessor (conn, quiet_notices, NULL);
	PQsetClientEncoding (conn, "UTF8");
	return std::unique_ptr<Connection> (new PgConnection (conn));
}

}

#endif
