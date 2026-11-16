# The SQL mailbox

An SQL mailbox reads mail from a PostgreSQL table instead of a mail
server. A separate program fetches the mail and puts it into the table:
normally getmail (or fetchmail) over TLS, handing each message to
`po-sqldeliver`. Neo-PostOffice then reads it from the database, over an
SSL connection when the database is on another machine.

    mail server --TLS--> getmail --> po-sqldeliver --> PostgreSQL --SSL--> Neo-PostOffice

- Several people can share one database. Each logs in with their own
  PostgreSQL user and, through row-level security, sees only their own
  mail.
- Delivery uses its own login, which may add mail for anyone but cannot
  read any.
- Folders come from the database: each value of the `folder` column
  becomes a folder in Neo-PostOffice when it is first seen.
- Read state, deleting and new mail are shared between computers: a
  message read or deleted on one is read or gone on the others after
  their next check.

## 1. Database server

Create a database and load the schema as a PostgreSQL superuser (or a
user allowed to create roles):

    createdb postoffice
    psql -d postoffice -f docs/sql-mailbox-postgresql.sql

This creates the table `po_mail`, its row-level security rules and two
group roles: `po_users` (people who read mail) and `po_delivery` (the
delivery program). Create the logins:

    CREATE ROLE alice   LOGIN PASSWORD '...' IN ROLE po_users;
    CREATE ROLE bob     LOGIN PASSWORD '...' IN ROLE po_users;
    CREATE ROLE getmail LOGIN PASSWORD '...' IN ROLE po_delivery;

What each may do:

| Login | Read mail | Add mail | Mark read | Delete |
|---|---|---|---|---|
| `po_users` member | own only | no | own only | own only |
| `po_delivery` member | no | for any mail user | no | no |

Users cannot change anything else about a message (sender, subject,
folder, owner).

### SSL

Neo-PostOffice always connects to a database on another machine with
SSL and checks the server's certificate and host name
(`sslmode=verify-full`). The server needs a certificate whose name matches
the host name you connect to, signed by a CA the clients have. In
`postgresql.conf`:

    listen_addresses = '*'
    ssl = on
    ssl_cert_file = 'server.crt'
    ssl_key_file = 'server.key'
    password_encryption = 'scram-sha-256'

and in `pg_hba.conf`, allowing only encrypted connections with passwords
(adjust the network):

    hostssl   postoffice  +po_users     192.168.1.0/24  scram-sha-256
    hostssl   postoffice  +po_delivery  192.168.1.0/24  scram-sha-256
    hostnossl all         all           0.0.0.0/0       reject

Copy the CA certificate (`ca.crt`, not the key) to each client.

## 2. Delivery with getmail

`po-sqldeliver` reads one message on standard input:

    po-sqldeliver [-u USER] [-f FOLDER] [-t TABLE] [CONNINFO]

- `-u USER` the PostgreSQL user the mail is for. Leave it out to deliver
  to the login that connects (a user delivering their own mail).
- `-f FOLDER` the folder (default `Inbox`). Folder names are 1-63 bytes
  and may not contain `=` `{` `}` `"` or `,`.
- `CONNINFO` a libpq connection string. The password goes in the
  delivering account's `~/.pgpass` (mode 600), not on the command line.

For getmail (getmail6), in the account's getmail rc file:

    [destination]
    type = MDA_external
    path = /usr/local/bin/po-sqldeliver
    arguments = ("-u", "alice", "host=db.example.org dbname=postoffice user=getmail sslmode=verify-full sslrootcert=/etc/postoffice/ca.crt")

and in that account's `~/.pgpass`:

    db.example.org:5432:postoffice:getmail:the-password

Use one getmail rc file per mail account, each with its own `-u` (and
`-f` if you want mail from that account in a separate folder).

Exit status: 0 delivered, 64 bad arguments, 65 not an e-mail message,
67 `-u` is not a member of `po_users`, 75 database unavailable or other
temporary error. getmail treats any non-zero status as a failed delivery,
so the message stays on the mail server and is fetched again next time.

`po-sqldeliver` stores the message exactly as received, and also
stores the sender, recipient and subject (with encoded words such as
`=?UTF-8?B?...?=` decoded), the date and the Message-ID in their own
columns for the message list.

## 3. Neo-PostOffice

In Config → Mailboxes, add a mailbox and choose the protocol
*SQL mailbox (PostgreSQL)*:

| Field | Value |
|---|---|
| Server | the database host, `host:port`, or empty / a socket directory for a database on this machine |
| Username | your PostgreSQL user (`alice`) |
| CA file | the CA certificate, e.g. `~/.postgresql/ca.crt`; empty means `~/.postgresql/root.crt` |
| Database | `postoffice` |
| Table | `po_mail` |

Put your password in `~/.pgpass` (mode 600):

    db.example.org:5432:postoffice:alice:the-password

You don't need to create folders: they appear on the first check, which
happens when the program starts. Each gets a filter that matches its
folder name exactly; you can change the filters in Config → Mailfolders.

With a server name the connection always uses SSL with full certificate
checking. Without one (or with a socket directory such as
`/var/run/postgresql`) it connects locally without SSL.

## Moving mail between folders

Neo-PostOffice does not move SQL mailbox messages yet. To file a message,
change its folder in the database:

    UPDATE po_mail SET folder = 'Archive' WHERE id = 42;

(only an administrator can do this; users have no right to change the
folder column).
