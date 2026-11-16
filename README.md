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
- Local mbox files and Maildir (with Maildir++ folders) as mailboxes;
  Maildir read state, flags, moves and deletions are shared with other
  programs
- SQL mailboxes (experimental, on hold): mail read from a shared
  PostgreSQL database over SSL
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
| `WITH_POSTGRESQL` | `AUTO` | PostgreSQL support: SQL mailboxes, PostgreSQL storage and `po-sqldeliver` (needs `libpq-dev`). `AUTO` uses it when libpq is found |
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

## Maildir mailboxes

The *Maildir* protocol reads a Maildir on this computer, such as
`~/Maildir` filled by getmail. Maildir++ subfolders (`.Work`,
`.Lists.2025`) appear as folders by themselves. Opening a message sets
its seen flag in the Maildir, deleting it deletes the file, and changes
made by other programs show up on the next check. Messages can be
dragged onto another folder (Ctrl+drag copies) or moved or copied with
the *Message* menu or a right-click, also from other mailboxes into a
Maildir; they can be flagged with *Mark* and marked unread, and replies
and forwards set the replied/passed flags. Folders can be created,
renamed and deleted with a right-click in the folder tree or *File →
Folder*. See [docs/maildir.md](docs/maildir.md).

## SQL mailboxes

*Development of the SQL mailbox is on hold* (too little is known about
how the original was used); Maildir is the recommended way to read mail
fetched by getmail. The code below works as described but may change.


The *SQL mailbox (PostgreSQL)* protocol reads mail from a table in a
PostgreSQL database, usually on another machine, over SSL with full
certificate checking. A fetcher such as getmail collects the mail over
TLS and hands it to `po-sqldeliver`, which stores it in the table.
Several people can share the database: each logs in as their own
PostgreSQL user and row-level security shows them only their own mail.
Folders are created automatically from the database, and read state and
deletions are shared between computers.

Setup (database, roles, SSL, getmail and the client) is described in
[docs/sql-mailbox.md](docs/sql-mailbox.md); the schema is
`docs/sql-mailbox-postgresql.sql`.

## Source layout

| Path | Contents |
|---|---|
| `src/postoffice/` | The application |
| `src/postoffice/helpdesk.fld`, `filereq.fld` | User interface, edited with FLUID |
| `src/postoffice/icons/` | XPM icons and banner artwork |
| `src/tools/po-sqldeliver.cpp` | Delivery tool for SQL mailboxes |
| `docs/` | Maildir and SQL mailbox guides, database schemas |
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
- PostgreSQL support: mail storage, and the SQL mailbox rebuilt for
  PostgreSQL (multi-user with row-level security, SSL, automatic
  folders) with the `po-sqldeliver` delivery tool. MySQL support was
  removed; the original MySQL table layout is not supported.
- Maildir mailboxes with Maildir++ folders and two-way flag sync;
  moving messages by drag and drop or the Message menu; flagged,
  replied and passed flags; folders deleted on disk leave the list.
- Moving and copying messages between mailboxes into Maildir (exact
  copies, fetched again from the source; rebuilt from the local copy
  only if the source no longer has it); folder management for Maildir;
  Mark as unread; right-click menus on messages and folders. Selecting
  a mailbox itself makes it the current mailbox for Check.
- Replies use the original's Message-ID header for In-Reply-To and
  References (they used the internal id, which is not a Message-ID for
  Maildir and SQL messages).
- Ctrl-click in the message list adds to the selection (it cleared it:
  the Ctrl bit was passed where 1 was expected). Clearing an empty list
  no longer crashes.
- Encoded words in headers (`=?UTF-8?B?...?=`) are decoded in the
  message list, the message view and the window tabs; 8-bit headers
  without a character set are read as ISO-8859-1.
- The window handles up to 500 waiting notices per pass instead of one,
  so thousands of new messages arrive in seconds instead of minutes.
- The shared string table of message envelopes counted references from
  0, so freeing two messages with an identical sender, subject or date
  freed the text while one still used it; it also had no lock although
  every mailbox thread uses it.
- The settings file accepts names with spaces (folders from the
  database). Two fields on the unfinished Language tab no longer
  overwrite the selected mailbox's settings when typed in.
- Deleting a message no longer hangs the program; the message list keeps
  its scroll position.

## Known issues and ideas

Found while modernizing and left for later:

- Only `http://` links are recognized (no `https://`); POP3, IMAP4,
  NNTP and SMTP have no TLS support.
- Message text in 8-bit without a declared character set shows `?`
  instead of accented letters in the message view (headers are fine).
- After closing the Config window, Check does nothing until a folder is
  clicked. Deleting a message does not update the count next to the
  mailbox name until the next check.
- Deleting a message in an mbox mailbox only hides it (deleting from
  mbox files was never implemented); the same goes for moving one out.
- SQL mailbox: messages cannot be moved between folders from the client,
  and the collection-method and expiry settings, which do not apply to
  it, are not greyed out.
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
