# Neo-PostOffice

Neo-PostOffice is a modernized PostOffice, the X11 mail and Usenet client
written in C++ by **Pim van Riezen** (Mad Science Labs, Rotterdam) in
1999–2000. It is based on the PostOffice 0.7.3 source release, which is
preserved unchanged in
[dleecurt/postoffice](https://github.com/dleecurt/postoffice).

The modernization builds against the current FLTK 1.4 toolkit with CMake,
compiles without warnings, and fixes bugs found along the way. The user
interface and behavior are intentionally kept close to the original.

## Features

- POP3, IMAP4 and NNTP (Usenet) support, SMTP sending with an outbox queue
- Local mbox files as mailboxes
- SQL mailboxes: mail read from a table in PostgreSQL or MySQL/MariaDB,
  filled by the included `po-sqldeliver` tool
- Mail storage in GDBM files or, per mailbox, in PostgreSQL
- Filter rules that sort incoming mail into folders
- Inline display of JPEG attachments, clickable links
- Multiple message views, composer with quoting and templates

## Building

On Debian 13 (trixie):

    sudo apt install build-essential cmake libfltk1.4-dev libgdbm-dev \
                     libx11-dev libxext-dev libpq-dev
    cmake -S . -B build
    cmake --build build -j$(nproc)
    ./build/postoffice

Install with `sudo cmake --install build` (default prefix `/usr/local`).

Requirements: a C++17 compiler, CMake 3.16+, FLTK 1.4 (with its image
library) and GDBM; libpq for PostgreSQL. FLTK is found through `fltk-config` or, if that is not
installed, FLTK's CMake package. (Debian 13's FLTK CMake package is
broken, which is why `fltk-config` comes first.) To use a self-built
FLTK, put its `bin` directory first in `PATH`, or pass
`-DFLTK_CONFIG=/path/to/fltk/bin/fltk-config`.

### Build options

| Option | Default | Meaning |
|---|---|---|
| `WITH_POSTGRESQL` | `AUTO` | PostgreSQL support (needs `libpq-dev`). `AUTO` uses it when libpq is found |
| `WITH_MYSQL` | `OFF` | MySQL/MariaDB support for SQL mailboxes (needs `libmariadb-dev`) |
| `PO_WARNINGS` | `ON` | Compile with `-Wall -Wextra` |

### Debug build with sanitizers

    cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
          -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
          -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build build-asan -j$(nproc)
    ASAN_OPTIONS=detect_leaks=0 ./build-asan/postoffice

Leak detection is off because the original code does not free everything
at exit.

## Using it

On first start a wizard asks for your name and e-mail address and,
optionally, a POP3 and an NNTP server. Settings live in
`~/.postoffice/resource.rc`. Remove or rename `~/.postoffice` to start
over.

Clicking a link runs the command in the preference
`postoffice/gui/action/http`, with `$` replaced by the URL. The default
is `xdg-open $`. The URL is passed to the shell as a separate argument,
so links in messages cannot inject commands.

## Storing mail in PostgreSQL

Each mailbox can keep its downloaded messages in PostgreSQL instead of
GDBM files. In Config → Mailboxes, set *Store mail in* to PostgreSQL and
enter a libpq connection string, for example
`host=localhost dbname=postoffice user=me`. Leave the password out and
put it in `~/.pgpass`. The tables (`po_messages`, `po_parts`) are created
on first use; `docs/postgresql-storage.sql` shows them. Messages from
several mailboxes and users can share one database.

## SQL mailboxes

The *Post Office SQL table* protocol reads mail from a database table
instead of a mail server. Some other program puts the mail there; the
original PostOffice expected a mail server feeding a shared MySQL
database. Neo-PostOffice supports this with PostgreSQL or MySQL/MariaDB.

1. Create the tables with `docs/sql-mailbox-postgresql.sql` or
   `docs/sql-mailbox-mysql.sql`.
2. Deliver mail into them with `po-sqldeliver`, which reads one message
   on standard input:

       po-sqldeliver pgsql "host=dbhost dbname=postoffice"  < message
       po-sqldeliver mysql dbhost myuser postoffice         < message

   Options: `-t TABLE` (default `messages`), `-f FOLDER` (default
   `Inbox`), `-H` to also fill the `headers` table. Exit status is 0 on
   success, 75 on a temporary error (the mail server should retry), 64
   for bad usage and 65 for unusable input. For example, from a
   `~/.forward` or a procmail rule:

       |/usr/local/bin/po-sqldeliver pgsql "dbname=postoffice"

3. In Config → Mailboxes, add a mailbox with the protocol *Post Office
   SQL table*, choose PostgreSQL or MySQL next to *Server*, and fill in
   the server, user, database and table. Name its folders as in the
   `folder` column (`Inbox` by default).

Passwords: PostgreSQL reads `~/.pgpass` (the password field is greyed
out), MySQL reads the `[client]` section of `~/.my.cnf` when the field is
left empty. The server can be a host name or, for PostgreSQL, a socket
directory.

The table layout is reconstructed from the queries in PostOffice 0.7.3;
the original schema was never published. Message bodies are stored in
the original encoding: every byte except letters, digits, space and
`.,:;/!?` is written as `@` followed by two hex digits.

## Source layout

| Path | Contents |
|---|---|
| `src/postoffice/` | The application |
| `src/postoffice/helpdesk.fld`, `filereq.fld` | User interface, edited with FLUID |
| `src/postoffice/icons/` | XPM icons and banner artwork |
| `src/tools/po-sqldeliver.cpp` | Delivery tool for SQL mailboxes |
| `docs/` | Database schemas |
| `include/` | Application headers |
| `src/toggletree/` | Fl_Toggle_Tree widget (Maarten de Boer, James Dean Palmer), patched |
| `src/fl_editor/` | Fl_Editor text editor widget and librcl (Robert Kesterson) |

## Editing the user interface

`helpdesk.cpp/.h` and `filereq.cpp/.h` are generated by FLUID from the
`.fld` files and are committed so that building does not require FLUID.
After changing the interface, regenerate them from `src/postoffice`
(FLUID is in the Debian package `fluid`):

    cd src/postoffice
    fluid -c -o helpdesk.cpp -h helpdesk.h helpdesk.fld
    fluid -c -o filereq.cpp -h filereq.h filereq.fld

Run FLUID from that directory so it finds the images under `icons/`.

Two conventions in the `.fld` files:

- Windows are `type Single` (unbuffered), as in the original. FLUID 1.4
  treats windows without a type as double-buffered.
- The localization macro takes the widget explicitly: `xloc(o, "name")`.

## Changes from PostOffice 0.7.3

See the git history for details. In summary:

- Ported from the bundled FLTK 1.0.4 to the system FLTK 1.4; UI code
  regenerated with FLUID 1.4.
- CMake build replaces the Perl configure script and per-host
  Makefiles. The bundled GNU DBM 1.8.0 was replaced by the system
  library.
- 64-bit fixes (pointers stored in `int`), C++17 fixes.
- Security: links in messages could run shell commands when clicked.
- The builder's user and host name are no longer recorded in the build
  and sent in the `X-Mailer` header of every message.
- Memory and logic bugs found by compiler warnings and by
  AddressSanitizer/UBSan, among them a use-after-free when a send failed,
  reading before buffers, non-virtual destructors on polymorphic trees,
  `new[]`/`delete` mismatches, missing return values, and
  File → Configure doing nothing.
- JPEG attachments are decoded with FLTK's image support instead of
  libjpeg through a temporary file.
- PostgreSQL support: mail storage and SQL mailboxes, through a small
  database layer shared with MySQL, and the `po-sqldeliver` tool.
- Deleting a message no longer hangs the program; the message list keeps
  its scroll position.

## Known issues and ideas

Found while modernizing and left for later:

- Only `http://` links are recognized (no `https://`); POP3, IMAP4,
  NNTP and SMTP have no TLS support.
- Messages with 8-bit headers or text and no declared character set
  show `?` instead of accented letters in the message view (seen with a
  Latin-1 test message).
- The tools in `src/postoffice/update.cpp` and `checkmail.cpp` are not
  built; they contain the original author's database server and login.
- The composer's Message → Discard menu item has no action; closing the
  window through the window manager discards the message.
- The MIME parser ignores boundaries of three characters or fewer and
  leaves the `--` of each boundary line at the end of the previous part.
- `WordList::MakeGeneric()` tests `*tmp-1` instead of `*(tmp-1)`, so
  escaped separators in preference values have never worked. It is left
  as is because fixing it would change how existing settings are read.
- The IMAP4 handler prints debug output to standard output.
- `~MailMessage()` contains a store-on-destroy that never ran in the
  original (see the comment there); it stays disabled until reviewed.

## License

PostOffice is licensed under the **GNU Lesser General Public License**
(see `LICENSE`, LGPL 2.1). The bundled components keep their own
licenses:

- **Fl_Toggle_Tree**: GNU Library General Public License (see
  `src/toggletree/README`).
- **Fl_Editor / librcl**: this copy contains no license statement. See
  `src/fl_editor/README.TXT` for the author's contact details.

FLTK and GDBM are no longer bundled. Neo-PostOffice links against the
system FLTK (LGPL with exceptions) and GDBM (GPL). Distributed binaries
that link GDBM are therefore covered by the GPL.

Copyright (C) 1999–2000 Pim van Riezen / Mad Science Labs. Original
copyright notices are kept in all source files.
