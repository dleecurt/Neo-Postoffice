//===============================================================================
// Neo-PostOffice database layer: MySQL/MariaDB driver
//
// Replaces the original dbdriver_* functions of mysql.cpp, which used the
// long-removed mysql_connect() and built queries by pasting values into SQL.
//===============================================================================

#include "podb.h"

#ifdef PO_HAVE_MYSQL

#include <mysql.h>
#include <stdlib.h>
#include <string.h>

namespace podb
{

class MyConnection : public Connection
{
public:
	explicit MyConnection (MYSQL *m) : My (m) {}
	~MyConnection (void) override { if (My) mysql_close (My); }

	const char *Driver (void) const override { return "mysql"; }

	bool Connected (void) override
	{
		std::lock_guard<std::recursive_mutex> g (Mutex);
		return My && mysql_ping (My) == 0;
	}

	bool Begin (void) override { return Exec ("START TRANSACTION"); }

	bool Exec (const std::string &sql, const Params &params = Params(), Result *out = NULL) override
	{
		std::lock_guard<std::recursive_mutex> g (Mutex);

		std::string query;
		if (!Bind (sql, params, query)) return false;

		if (mysql_real_query (My, query.data(), (unsigned long) query.size()))
		{
			LastError = mysql_error (My);
			return false;
		}
		LastError.clear();

		MYSQL_RES *res = mysql_store_result (My);
		if (out)
		{
			out->Clear();
			out->Affected = (long) mysql_affected_rows (My);
		}
		if (!res)
		{
			if (mysql_field_count (My) != 0)
			{
				LastError = mysql_error (My);
				return false;
			}
			return true;
		}
		if (out)
		{
			out->Columns = (int) mysql_num_fields (res);
			MYSQL_ROW row;
			while ((row = mysql_fetch_row (res)))
			{
				unsigned long *len = mysql_fetch_lengths (res);
				std::vector<std::string> data (out->Columns);
				std::vector<bool> nulls (out->Columns, false);
				for (int c = 0; c < out->Columns; ++c)
				{
					if (row[c]) data[c].assign (row[c], len[c]);
					else nulls[c] = true;
				}
				out->Data.push_back (std::move (data));
				out->Nulls.push_back (std::move (nulls));
			}
		}
		mysql_free_result (res);
		return true;
	}

private:
	// Replaces $1, $2, ... with escaped literals. Quoted strings and
	// identifiers in the SQL text are copied unchanged.
	bool Bind (const std::string &sql, const Params &params, std::string &query)
	{
		static const char hex[] = "0123456789ABCDEF";
		char quote = 0;

		for (size_t i = 0; i < sql.size(); ++i)
		{
			char c = sql[i];
			if (quote)
			{
				query += c;
				if (c == '\\' && i + 1 < sql.size()) query += sql[++i];
				else if (c == quote) quote = 0;
				continue;
			}
			if (c == '\'' || c == '"' || c == '`')
			{
				quote = c;
				query += c;
				continue;
			}
			if (c != '$' || i + 1 >= sql.size() || sql[i+1] < '1' || sql[i+1] > '9')
			{
				query += c;
				continue;
			}

			size_t n = 0;
			while (i + 1 < sql.size() && sql[i+1] >= '0' && sql[i+1] <= '9')
				n = n * 10 + (size_t) (sql[++i] - '0');
			if (n < 1 || n > params.size())
			{
				LastError = "placeholder $" + std::to_string (n) + " has no parameter";
				return false;
			}

			const Param &p = params[n - 1];
			if (p.IsNull) query += "NULL";
			else if (p.IsBinary)
			{
				query += "X'";
				for (unsigned char b : p.Value) { query += hex[b >> 4]; query += hex[b & 15]; }
				query += '\'';
			}
			else
			{
				std::string buf (p.Value.size() * 2 + 1, '\0');
				unsigned long l = mysql_real_escape_string (My, &buf[0], p.Value.data(),
															(unsigned long) p.Value.size());
				query += '\'';
				query.append (buf.data(), l);
				query += '\'';
			}
		}
		return true;
	}

	MYSQL *My;
};

bool HaveMySQL (void) { return true; }

std::unique_ptr<Connection> ConnectMySQL (const std::string &host, int port,
										  const std::string &user, const std::string &password,
										  const std::string &database, std::string &error)
{
	MYSQL *m = mysql_init (NULL);
	if (!m)
	{
		error = "out of memory";
		return NULL;
	}
	mysql_options (m, MYSQL_SET_CHARSET_NAME, "utf8mb4");
	if (!mysql_real_connect (m, host.c_str(), user.c_str(), password.c_str(),
							 database.empty() ? NULL : database.c_str(),
							 (unsigned int) (port > 0 ? port : 3306), NULL, 0))
	{
		error = mysql_error (m);
		mysql_close (m);
		return NULL;
	}
	return std::unique_ptr<Connection> (new MyConnection (m));
}

}

#endif
