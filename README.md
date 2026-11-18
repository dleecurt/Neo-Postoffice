# Neo-PostOffice

Neo-PostOffice is a modernized PostOffice, the X11 mail and Usenet client
written in C++ by **Pim van Riezen** (Mad Science Labs, Rotterdam) in
1999–2000. It is based on the PostOffice 0.7.3 source release, which is
preserved unchanged in
[dleecurt/postoffice](https://github.com/dleecurt/postoffice).

The modernization builds against the current FLTK 1.4 toolkit with CMake,
compiles without warnings, and fixes bugs found along the way. The user
interface and behavior are intentionally kept close to the original.

Neo-PostOffice runs on Linux and the BSDs. The code for other systems
(IRIX, Solaris, SunOS, Windows) has been removed.

## Features

- POP3 over TLS or STARTTLS, NNTP (Usenet), or a fetch command (getmail,
  fetchmail, mbsync) run before each check of a local mailbox; IMAP4 is
  being rewritten (see below); outgoing mail is queued in the
  outbox and handed to the system's MTA, to msmtp or to the built-in SMTP
  client
- Local mbox files, Maildir (with Maildir++ folders) and MH folders
  (nmh) as mailboxes; read state, flags, moves and deletions are shared
  with other programs
- SQL mailboxes (experimental, on hold): mail read from a shared
  PostgreSQL database over SSL
- Mail storage in GDBM files or, per mailbox, in PostgreSQL
- Filter rules that sort incoming mail into folders
- Inline display of JPEG attachments, clickable links
- Multiple message views, composer with quoting and templates

## Building

On Debian 13 (trixie):

    sudo apt install build-essential cmake libfltk1.4-dev libgdbm-dev \
                     libx11-dev libxext-dev libpq-dev libssl-dev
    cmake -S . -B build
    cmake --build build -j$(nproc)
    ./build/postoffice

Install with `sudo cmake --install build` (default prefix `/usr/local`).

On the BSDs, install the same dependencies from packages or ports
(FLTK must be 1.4 or newer) and build the same way. CMake looks in
`/usr/local`; on NetBSD add `-DCMAKE_PREFIX_PATH=/usr/pkg`, and on
OpenBSD also `-DCMAKE_PREFIX_PATH=/usr/X11R6` if X11 is not found. The
BSD builds have not been tested yet.

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

## POP3 mailboxes

Config → Mailboxes → *POP3 Account* collects mail from a POP3 server:

- **Encryption**: *TLS* (port 995, the default for new mailboxes),
  *STARTTLS* (port 110, then encrypted; if the server cannot, nothing is
  sent and an error says so), or *None* for a server on your own machine
  or network. *Port* overrides the usual port. Mailboxes set up before
  this option existed keep working unencrypted until it is changed.
- **Certificates** are always checked against the system's certificate
  authorities and the host name. *CA file* adds a PEM file with your own
  CA, for a server whose certificate comes from a local CA. There is no
  way to accept an unchecked certificate.
- **Password**: the *Password* field, or a *Password command* whose
  first line of output is the password, for example
  `pass show mail/pop` or `secret-tool lookup service pop3 user me`;
  then nothing secret is kept in the settings. The settings file
  (`~/.postoffice/resource.rc`) is now private to you (mode 600).
- **Collection method**: headers only (the text is fetched when you open
  a message), complete messages, or complete messages deleted from the
  server afterwards. Deleting from the server happens only after the
  messages are safely stored. Deleting a message here also deletes it on
  the server (unless the mailbox is write-protected); *File → Expire*
  removes messages left on the server after the days set below the
  collection method.

Each check is one short session (log in, list, fetch, log out), so new
mail is always seen and the server mailbox is not kept locked. Every
message is fetched once: `uidl.dat` in the mailbox directory remembers
which ones were. Errors (server down, refused login, untrusted
certificate) are shown once, not at every automatic check.

## IMAP4

The original IMAP4 client could not work with today's servers and has
been removed; a new one, with TLS, folders and flags kept in step with
the server, is planned. *IMAP4* is no longer offered for new mailboxes;
an existing IMAP4 mailbox keeps its settings for the new client and
shows a note when checked. See [docs/imap-plan.md](docs/imap-plan.md).

Until then, mbsync (isync) or OfflineIMAP can keep a local Maildir in
step with an IMAP server in both directions: folders, read state, flags,
deletions and moves. Set up a *Maildir* mailbox on that directory with
`mbsync -a` (or `offlineimap -o -u quiet`) as its *Fetch command*: every
check then brings new mail in and sends your changes back.

## Sending mail

Sent messages wait in the outbox, which is emptied every 15 seconds.
Config → Network → *Send mail with* chooses how they are delivered:

- **System MTA (sendmail)**, the default for new setups: the message is
  given to `/usr/sbin/sendmail`, which Postfix, Exim, OpenSMTPD and
  msmtp-mta all provide. The MTA does the rest (TLS, login, retries).
- **msmtp**: the message is given to `msmtp`, configured in
  `~/.msmtprc`. msmtp uses the account whose `from` is the message's
  sender, or its default account, so one account per identity just
  works.
- **Built-in SMTP client**: the original client. It speaks plain SMTP on
  port 25 without encryption or login, so it is only useful with a mail
  server on your own machine or network (`localhost`).

For the first two, *Program* is the program to run (another path, or
`~/bin/...`, can be given). It is run as
`PROGRAM -i -f SENDER -- RECIPIENT...` with the message on standard
input; Bcc recipients are passed as arguments and the Bcc header is
removed. If the program fails, the message stays in the outbox, its
error (for example msmtp's) is shown once, and it is tried again at the
next round. Settings made before this option existed keep using the
built-in client until changed.

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

## Fetching mail with getmail or fetchmail

Maildir, MH and mbox mailboxes can have a *Fetch command* (Config →
Mailboxes, in place of the collection method). It is run with
`/bin/sh -c` before every check of that mailbox: at startup, on *Check
Mail* (Alt+L) and at the mailbox's interval, and then the mailbox is
read. For example:

    getmail --rcfile work.rc
    fetchmail -s
    getmail --rcfile personal.rc && getmail --rcfile lists.rc

The command should deliver into this mailbox (getmail's destination,
fetchmail through procmail and so on). While it runs, the status bar
says so; it is stopped after five minutes. If it fails, its last lines
of output are shown, once until the error changes or a run succeeds,
and the mailbox is read anyway. Moving or copying messages into the
mailbox does not run it. Leave the field empty if mail is fetched some
other way (cron, a fetchmail daemon).

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

## MH mailboxes (nmh)

The *MH (nmh)* protocol reads the mail folders of nmh, from the `Path:`
in `~/.mh_profile` (or a directory given in the settings). Every folder,
nested ones too, appears by itself, and nmh can be used on the same
folders at the same time. Read state is the profile's Unseen-Sequence;
flagged, replied and forwarded are the sequences `flagged`, `replied` and
`forwarded`, changed under nmh's lock without touching other sequences.
Deleting works like `rmm` (the profile's `rmmproc`, or a rename to
`,NUMBER`), messages can be moved and copied in and out like with
Maildir, and folders can be managed the same way. See
[docs/mh.md](docs/mh.md).

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
| `docs/` | Maildir, MH and SQL mailbox guides, database schemas |
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
- Deleting (or moving out) a message in an mbox mailbox removes it from
  the mbox file (0.7.3 only hid it). The file is locked with fcntl and a
  `.lock` file, the rest of the file is kept in a safety copy in
  `~/.postoffice` while it is rewritten, and changes made to the file by
  other programs are detected by Message-ID. If the file is locked by
  another program the message is hidden and deleted at a later check;
  a read-only mbox can be read, and deleting there only hides.
- MH folders (nmh) as mailboxes, sharing their code with Maildir in a
  common base for mailboxes kept in local files. Folder name rules and
  the fixed inbox come from the mailbox type. Folders whose settings were
  not saved (after a crash) are made again from the message index
  instead of their messages being lost from view.
- Outgoing mail can be delivered by the system MTA (`/usr/sbin/sendmail`,
  the default for new setups) or msmtp instead of the built-in SMTP
  client, and their errors are shown (the built-in client ignores its own). The outbox
  is saved as soon as messages are sent, so a crash right after sending
  no longer sends them again.
- Only Linux and the BSDs are supported: the IRIX, Solaris/SunOS,
  old-FreeBSD and Windows code was removed, along with the `LINUX`
  build define. Connection addresses are formatted with `inet_ntop`
  instead of a byte-order guess that printed some of them backwards.
- POP3 rewritten on a new network layer (`netstream.cpp`): TLS (995) and
  STARTTLS with certificate and host name checks and an optional own CA,
  a password command, one session per operation, lines of any length,
  dot-unstuffing, stable IDs for messages without a Message-ID, and
  deletion only after messages are saved. Deletions are sent in one
  session, and messages expired as headers leave the list.
- The old IMAP4 client was removed (it could not work); IMAP4 mailboxes
  keep their settings for a new client.
- The settings file and `uidl.dat` are private (mode 600). Long error
  messages are wrapped. Changing mailbox settings no longer leaves the
  window pointing at a deleted mailbox (a crash when its next message
  arrived).
- Maildir, MH and mbox mailboxes can run a fetch command such as getmail
  or fetchmail before every check, with its errors shown.
- MIME: nested multiparts, parts without headers, boundaries of any
  length, RFC 2231 file names, and base64 or quoted-printable text in any
  character set are shown correctly (8-bit text without a character set
  is read as ISO-8859-1). `https://` and `ftp://` links are recognized.
- Copies of POP3 messages are fetched again from the server when it
  still has them; rebuilt copies keep each part's transfer encoding.
- Message texts are synced to disk with the mailbox index, and a GDBM
  store whose index lost messages in a crash repairs it on reading
  (such messages showed up empty).
- The settings store (`Var`) deleted and renamed the wrong entries and
  miscounted them; escaped separators in settings now work. Deleting a
  message updates the count beside the mailbox, and rebuilding the
  mailbox list keeps the selection and scroll position.
- The composer's Message → Send and Discard menu items work. Recipient
  lists with quoted names, comments and groups are split correctly.
  Deeply quoted replies no longer overflow a buffer.
- Settings that do not apply to a mailbox type (collection method,
  expiry on the server) are greyed out. Fixed-size `sprintf` calls were
  replaced by `snprintf`.

## Known issues and ideas

Found while modernizing and left for later:

- NNTP and the built-in SMTP client have no TLS support (send through
  the system MTA or msmtp instead).
- HTML-only messages show only an attachment line; there is no HTML to
  text view. The message list has no keyboard navigation.
- SQL mailbox: messages cannot be moved between folders from the client.
- The tools in `src/postoffice/update.cpp` and `checkmail.cpp` are not
  built; they contain the original author's database server and login.
- Copies rebuilt from the local copy (when the source no longer has the
  original) flatten nested multiparts into one `multipart/mixed`.
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
