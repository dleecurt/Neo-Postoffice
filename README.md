# Neo-PostOffice

Neo-PostOffice is a modernized PostOffice, the X11 mail client (it also
read Usenet news; that support has been removed) written in C++ by **Pim van Riezen** (Mad Science Labs, Rotterdam) in
1999–2000. It is based on the PostOffice 0.7.3 source release, which is
preserved unchanged in
[dleecurt/postoffice](https://github.com/dleecurt/postoffice).

The modernization builds against the current FLTK 1.4 toolkit with CMake,
compiles without warnings, and fixes bugs found along the way. The user
interface and behavior are intentionally kept close to the original.

Neo-PostOffice runs on Linux and the BSDs. The code for other systems
(IRIX, Solaris, SunOS, Windows) has been removed.

## Features

- POP3 over TLS or STARTTLS, IMAP4 (with libetpan: push with IDLE,
  flags, moves and folders on the server; see below), or a fetch command (getmail, fetchmail, mbsync)
  run before each check of a local mailbox; outgoing mail is queued in the
  outbox and handed to the system's MTA, to msmtp or to the built-in SMTP
  client
- Local mbox files, Maildir (with Maildir++ folders) and MH folders
  (nmh) as mailboxes; read state, flags, moves and deletions are shared
  with other programs
- SQL mailboxes (experimental, on hold): mail read from a shared
  PostgreSQL database over SSL
- Mail storage in GDBM files or, per mailbox, in PostgreSQL
- Filter rules that sort incoming mail into folders
- Inline display of JPEG attachments, clickable links; saving and
  opening received attachments, sending attachments
- Multiple message views, composer with quoting, lines wrapped at 72
  characters and sent as format=flowed
- Redirect (resend a message unchanged), printing through lpr (CUPS or
  the BSD print spooler), Refilter

## Building

On Debian 13 (trixie):

    sudo apt install build-essential cmake pkg-config libfltk1.4-dev libgdbm-dev \
                     libx11-dev libxext-dev libpq-dev libssl-dev libetpan-dev
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
library) and GDBM; libpq for PostgreSQL; libetpan 1.9 or newer (found
with pkg-config; BSD ports: `mail/libetpan`) for IMAP. FLTK is found through `fltk-config` or, if that is not
installed, FLTK's CMake package. (Debian 13's FLTK CMake package is
broken, which is why `fltk-config` comes first.) To use a self-built
FLTK, put its `bin` directory first in `PATH`, or pass
`-DFLTK_CONFIG=/path/to/fltk/bin/fltk-config`.

### Build options

| Option | Default | Meaning |
|---|---|---|
| `WITH_POSTGRESQL` | `AUTO` | PostgreSQL support: SQL mailboxes, PostgreSQL storage and `po-sqldeliver` (needs `libpq-dev`). `AUTO` uses it when libpq is found |
| `WITH_LIBETPAN` | `AUTO` | IMAP4 support (needs `libetpan-dev`). Without it, IMAP4 mailboxes keep their settings and show a note |
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
optionally, a POP3 server. Settings live in
`~/.postoffice/resource.rc`. Remove or rename `~/.postoffice` to start
over.

## POP3 mailboxes

Config → Mailboxes → *POP3 Account* collects mail from a POP3 server:

- **Encryption**: *TLS* (port 995, the default for new mailboxes),
  or *STARTTLS* (port 110, then encrypted; if the server cannot, nothing
  is sent and an error says so). There is no unencrypted choice: the
  password would be sent in the clear. *Port* overrides the usual port.
  Mailboxes set up before this option existed, or set to *None* in older
  versions, use STARTTLS.
- **Certificates** are always checked against the system's certificate
  authorities, Neo-PostOffice's own store (see *Certificates* below) and
  the host name. *CA file* adds a PEM file with a CA for this mailbox
  only. There is no way to accept an unchecked certificate.
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

## Certificates

Every TLS connection (POP3, IMAP, and SMTP once the new client is in)
trusts the system's certificate authorities plus Neo-PostOffice's own
store, kept in `~/.postoffice/certs` (one PEM file per certificate,
named after its SHA-256 fingerprint). Use it for a local or company CA:

- **Config → Certificates → Add...** takes a PEM file (one or more
  certificates) or a DER file. Each certificate is listed with its name,
  expiry date and type; selecting one shows its subject, issuer,
  validity and SHA-256 fingerprint. Compare the fingerprint with the
  one your CA's administrator gives you before relying on it.
- Only CA certificates (and a server's own self-signed certificate) are
  taken. A server certificate issued by a CA is refused: add the CA
  instead.
- **Remove** takes a certificate out. Changes apply from the next
  connection, without Save.
- A server whose certificate does not check out is always refused; the
  error says when adding a CA would help. There is no "trust this
  server anyway" button.

## IMAP4

A new IMAP4 client is being built on [libetpan](https://github.com/dinhvh/libetpan),
the library Claws Mail uses, in steps (see [docs/imap.md](docs/imap.md)).
All **six steps** are done: mail is read and pushed; read state, flags,
deletes and moves go to the server; folders are created, renamed,
deleted and subscribed to here.

What works now: an *IMAP4 Account* mailbox (Config → Mailboxes) connects
over TLS (port 993) or STARTTLS (port 143), never without encryption
(older *None* settings use STARTTLS); certificates are checked as for POP3
(system CAs, the certificate store and an optional CA file, and the host
name), with no way to
turn that off. It logs in with the password or the first line printed
by the password command, and shows the folders you subscribe to on the
server (and always the inbox) as its folders.

Reading: each check brings every folder up to date. The first check of a
folder fetches the headers of all its messages, newest first, so recent
mail shows up while older mail is still coming in; later checks fetch
only new messages, remove those deleted on the server, and skip folders
that have not changed. A message's text is fetched when you open it
(with a progress count for large ones), even in the middle of a check,
and kept here. **Stop** ends a check at once; the next check carries on
where it stopped. What is known of each folder is kept in `imap.dat` in
the mailbox's directory.

Read state and flags: opening a message marks it read on the server;
**Mark as unread**, **Flag / unflag** and sending a reply or forward
(answered, `$Forwarded`) change it there too, at once. When the server
cannot be reached the changes wait in `imap.dat` and are sent at the
start of the next check; until then they win over the server's. Changes
made elsewhere (webmail, a phone) show up at the next check: on servers
with CONDSTORE (Dovecot, Cyrus, Gmail, Fastmail and others) only what
changed is read, otherwise the flags of every message in the folder.

Deleting and moving: **Delete** moves a message to the server's Trash
folder (the one the server marks as Trash, else one called Trash,
Deleted Items and the like, else a new "Trash"); Delete in the Trash
removes it for good. **Move to folder** (or dragging onto a folder)
within the account moves it on the server; to or from another mailbox
it is copied and the original removed for good (not put in the Trash).
**Copy to folder** stores a copy on the server. Like flag changes, these
are sent at once and wait for the next check when the server cannot be
reached. Messages put into an IMAP mailbox from elsewhere wait in its
`spool` directory, safely on disk, until the server has them; one the
server refuses is kept there as `*.failed`.

Push: new mail in the Inbox (and flag changes and removals there) shows
at once, without waiting for the interval, on servers that offer IDLE.
The connection stays open on the Inbox while nothing else is going on;
other folders are brought up to date at the interval checks. Config →
Mailboxes → *New mail: Push new mail at once* turns it off. If the
connection is lost (the computer slept, the network changed, the server
restarted), it is made again quietly, after 10 seconds, then 30, 60, 120
and every 5 minutes; changes made meanwhile are sent when it is back.
You are told once if the server is still out of reach after 10 minutes.
With push on, checks on the interval that cannot connect are quiet in
the same way; a check you start says what is wrong at once.

Large folders: on servers with QRESYNC (Dovecot, Cyrus, mox, Fastmail)
a changed folder's removed messages come with its flag changes; on
others the messages of a folder are listed in full only when its count
shows that some were removed.

Folders: right-click an IMAP mailbox or folder in the folder tree, or
use the *Folder* menu. **New folder...** creates a folder on the server
and subscribes to it (`/` makes a subfolder, as in `Lists/2025`).
**Rename folder...** renames it and its subfolders, and their
subscriptions move along; their messages are fetched again under the new
name. **Delete folder...** says how many messages the folder and its
subfolders hold and, once confirmed, deletes them on the server for
good: they do not go to the Trash. The Inbox cannot be renamed or
deleted. **Subscriptions...** lists every folder on the server with a
tick box: ticked folders are shown here and checked; unticking one
removes it and its messages here only, not on the server.

The message list shows the newest 1024 messages of a folder, for every
mailbox type; Config → Style → *Message list* changes that (0 shows them
all, which makes a large folder slower to open).

A copy of the mail you send can go to an IMAP account's Sent folder:
Config → Identities → *Save sent mail to* (off by default, as Gmail and
some other providers keep sent mail themselves). The copy keeps Bcc as a
record of who got the message, and is stored at that mailbox's next
check (which sending starts); if the server cannot be reached, it waits
in the spool.

An IMAP4 mailbox from PostOffice 0.7, which has no encryption setting,
uses STARTTLS on its old port rather than sending the password in the
clear.

mbsync (isync) or OfflineIMAP can also keep a local Maildir in step
with an IMAP server in both directions (for working offline, say): folders, read
state, flags, deletions and moves. Set up a *Maildir* mailbox on that
directory with `mbsync -a` (or `offlineimap -o -u quiet`) as its *Fetch
command*: every check then brings new mail in and sends your changes
back.

## Replying and forwarding

- **Reply** (Alt+R) answers the sender: the Reply-To address if the
  message has one, otherwise From.
- **Reply All** (toolbar, Message menu, the message's right-click menu,
  Alt+Shift+R) puts the sender in To and everyone else the message went
  to (its To and CC) in CC.
- Your own addresses (those of all your identities) are left out, and
  each address appears only once. Replying to a message you sent goes to
  its recipients instead of to yourself.
- The identity is chosen from the address the message was sent to: mail
  to one of your identities is answered from that identity; otherwise
  the default identity is used.
- **Forward** leaves To empty for you to fill in, and keeps the
  original's attachments.
- **Redirect** (Message menu, Redir button) sends the message on
  unchanged, as it was received, to the addresses you give: the
  recipients see the original sender, with Resent-From, Resent-To and
  Resent-Date lines naming you. It is sent from the identity the message
  was addressed to (you can pick another), and a copy goes to Sent. Some
  mail servers only send messages whose From line is your own address
  and refuse a redirect; the message then goes to OutBox → Drafts with
  an explanation, and Forward is the way to pass it on.
- Names and subjects written in other character sets (`=?UTF-8?B?...?=`
  in the raw message) are shown decoded in replies, forwards and the
  quoted header lines, and encoded again when sent.

## Line length and format=flowed

The composer wraps what you type at 72 characters, on the screen as well
as in the message sent (a wider window leaves space on the right). The
width can be changed in **Settings → Style → Composer → Wrap lines at**
(60 to 100); it applies to composers opened afterwards.

Messages are sent as `format=flowed` (RFC 3676): each line of a wrapped
paragraph ends with a space. Mail programs that understand it
(Thunderbird, Apple Mail, mutt, most phone apps) join the lines again and
fit the paragraph to their window; others show ordinary 72-character
lines. Line breaks you type yourself stay as they are, and lines starting
with a space or "From " are protected as the standard describes, so
indented text and ASCII art in the signature come through unchanged.

Replies quote the original re-wrapped at the same width with `> ` on
every line; a word longer than a line, such as a link, is kept whole on
a line of its own. **Edit Draft** joins a draft's paragraphs again so
they re-wrap as you edit.

## Printing

**File → Print Message** (Ctrl+P) or the **Print** button prints the
shown message: its From, To, Cc, Date and Subject, a list of its
attachments by name, and its text, as plain UTF-8 text with lines wrapped
at 80 characters. The text goes to the print command set in **Config →
Network → Print command**, `lpr $` by default, where `$` stands for the
file (without `$` the text is given on standard input). `lpr` is there
with CUPS and with the BSD print spooler (lpd); use `lpr -P name $` for
another printer than the default. If the command fails, what it said is
shown.

## Window layout

The striped handle at the left of the toolbar, and the one at the left of
the Mailboxes bar, move the toolbar or the folder list into a window of
its own. Close that window to put it back, or use **View → Toolbar in
Own Window** and **View → Folder List in Own Window**, which are ticked
while they are separate. Search and Addr are greyed out until they are
finished (0.9.2 and 0.9.3).

## Filters and Refilter

A local mailbox's folders are filters (Config → Mailfolders): each
incoming message goes to the first folder whose rules accept it, else to
the mailbox's default folder. Saving filter changes in Config sorts all
of the mailbox's messages again. **Message → Refilter** does the same for
the shown mailbox without changing anything in Config.

## Cut, Copy and Paste

- **Copy** and **Cut** put text on the clipboard, so **Paste** (or
  Ctrl+V) in another program pastes it; **Paste** takes the clipboard.
  Selecting text with the mouse still sets the X selection, which the
  middle mouse button pastes, as in other X programs.
- Shortcuts are Ctrl+X, Ctrl+C, Ctrl+V and Ctrl+A (Select All). The
  original Alt+X, Alt+C, Alt+V and Alt+A also work.
- **Main window, Edit menu:** Copy copies the text you selected in the
  message view (or in a text field); it is greyed out when nothing is
  selected. The message's right-click menu has Copy too. Cut and Paste
  work in a text field you are typing in. Select All selects all
  messages in the list.
- **Composer:** the Edit menu and the Cut, Copy and Paste buttons work on
  the header field you are typing in (To, CC, BCC, Subject), otherwise
  on the message text. Select All selects the whole message text.

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
- **Built-in SMTP client (server per identity)**: Neo-PostOffice sends
  the message itself to the mail server of the identity whose *Email
  From* address is the sender (Config → Identities → *Outgoing mail
  server*). See below.

For the first two, *Program* is the program to run (another path, or
`~/bin/...`, can be given). It is run as
`PROGRAM -i -f SENDER -- RECIPIENT...` with the message on standard
input; Bcc recipients are passed as arguments and the Bcc header is
removed. If the program fails, the message stays in the outbox, its
error (for example msmtp's) is shown once, and it is tried again at the
next round. Settings made before this option existed keep using the
built-in client until changed.

### The built-in SMTP client

Each identity has its own server: *Server*, *Port* (empty: 465 for TLS,
587 for STARTTLS, 25 without encryption) and *Encryption*:

- **TLS (port 465)**, the default: encrypted from the first byte
  (RFC 8314).
- **STARTTLS (port 587)**: the connection is upgraded before anything
  else is sent; a server that does not offer STARTTLS is refused, and so
  is one that sends data along with its STARTTLS answer.
- **None (port 25)**: only for a server on your own network, and never
  with a login.

*Username* and *Password* (or *Password cmd*, a command whose first line
of output is the password, such as `pass show mail/smtp`) log in to the
server; leave the user name empty for a server that takes your mail
without a login. The client uses the best method the server offers:
**SCRAM-SHA-256** (or SCRAM-SHA-1), where the password itself is never
sent and the server must prove that it knows it too, otherwise
**PLAIN** or **LOGIN** inside the encrypted connection. A SCRAM server
that asks for fewer than 4096 rounds or cannot prove itself is refused.
OAuth2 (needed by Microsoft 365, and by Gmail without an app password)
is not supported.

Certificates are checked as for POP3 and IMAP: the system's CAs, the
certificate store (Config → Certificates) and the host name, with no way
to skip the check. The client uses `EHLO` with this machine's full
domain name (or its address), honours the server's `SIZE` limit, sends
8-bit messages with `BODY=8BITMIME` and non-ASCII addresses with
`SMTPUTF8` (and refuses to send them to a server without those), and
escapes lines starting with a dot. The message is sent only if the
server accepts every recipient; otherwise nothing is sent and the
refused addresses are listed with the server's answer.

How the client was tested (a scripted server and a real Postfix with
Cyrus SASL), notes for common providers and a list of error messages
with what to do about them are in [docs/smtp.md](docs/smtp.md).

### When a message cannot be sent

A message is sent as soon as you press Send, and the OutBox (folders
*Queue*, *Sent* and *Drafts*) is also emptied every 15 seconds and at
**File → Check Mail**, which always tries every queued message at once.
What happens when sending fails depends on why:

| Why | What happens |
|---|---|
| Passing: the server cannot be reached, or answers "try later" (4xx) | Stays in Queue; tried again after 1, 5, 15 and 30 minutes, then every hour. After three days it moves to Drafts |
| The message: a refused recipient or sender, too large, 8-bit text or non-ASCII addresses the server does not take | Moves to OutBox → Drafts |
| The settings or server: login refused, certificate, no encryption offered, no server or identity set, the password command failed, or the server refuses for policy (5.7.x: relaying denied, a login needed, a sender the login may not use) | Stays in Queue, held until you fix it and use Check Mail |
| You pressed **Stop** | The connection is cut at once; the message stays in Queue, held until Check Mail |

Each reason is shown once per message. **Message → Edit Draft** opens a
message from Drafts in the composer, with its recipients, subject,
identity and text (the signature is added again when sent); sending it
queues the corrected message and removes the draft. Retry times are
kept in `~/.postoffice/OutBox.box/retry.dat`, so they survive a restart.

Clicking a link runs the command in the preference
`postoffice/gui/action/http`, with `$` replaced by the URL. The default
is `xdg-open $`. The URL is passed to the shell as a separate argument,
so links in messages cannot inject commands.

## Attachments

Each attachment of the message shown has an *Attachment* line (in the
link colour) with its number, name, type and size. Pictures (JPEG) and
named text files are shown in the message, and get a line after it.
Clicking a line offers:

- **Save as...**: the file requester, starting in the folder you saved
  to last (the first time `~/Downloads`, or your home folder), with the
  attachment's name filled in. The name is made safe: folder parts
  (`../`), leading dots and control characters are removed, so a file
  can only go where you choose. An existing file is replaced only after
  you confirm.
- **Open...**: always asks first, with a warning that attachments can be
  harmful. Programs, scripts, `.desktop` files, macro documents and
  similar files get a stronger warning. *Cancel* is the default (Return
  and Escape cancel); opening must be clicked. The file is written to a
  private folder, `~/.postoffice/tmp` (readable only by you, never
  executable), and opened with the command in the preference
  `postoffice/gui/action/attachment` (`$` stands for the file; the
  default is `xdg-open $`), with the file passed as a separate argument
  as for links. The folder is emptied when Neo-PostOffice starts and
  quits.

**Message → Save all attachments...** saves every attachment of the
message into a folder you choose; an existing file is kept and the new
one is saved as `name (2).ext`.

### Sending attachments

In the composer, **Attach** (also the small paperclip button) picks a
file with the file requester, starting in the folder you attached from
last. Files can also be dragged from a file manager onto the message
text. Attached files are listed under the headers with their size and
type; select one and press **Remove** to take it off. When the
attachments together pass 10 MB you are asked first, since many mail
servers refuse large messages and attachments grow by about a third
when sent.

A message with attachments is sent as `multipart/mixed`: the text
first, then each file in base64 with its name (non-ASCII names are
encoded as in RFC 2231). The copy kept in Sent has the attachments too.
**Forward** keeps the original's attachments (remove any you do not
want), and **Message → Edit Draft** brings back a draft's attachments.

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
renamed and deleted with a right-click in the folder tree or the
*Folder* menu. See [docs/maildir.md](docs/maildir.md).

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
- Edit → Select All (Alt+A, or Ctrl+A in the main window when no text
  field has the focus) selects every message in the list, also those in
  closed threads, for Move, Copy, Delete or Mark. It was an empty stub.
- Ctrl-click in the message list adds to the selection (it cleared it:
  the Ctrl bit was passed where 1 was expected). Clearing an empty list
  no longer crashes.
- Encoded words in headers (`=?UTF-8?B?...?=`) are decoded in the
  message list, the message view and the window tabs; 8-bit headers
  without a character set are read as ISO-8859-1.
- Security review (Stage 1): no unencrypted POP3/IMAP login (the *None*
  choice is gone; old settings use STARTTLS, which fails safely if the
  server cannot); header values sent can no longer carry a line break
  that starts another header (an encoded CR LF in a subject, or a pasted
  one); a lone CR in the text is a line end also for sendmail and msmtp;
  mailbox index and mbox state files are created readable by you only
  (0600). The IMAP *LOGINDISABLED* message says that password logins
  are off on the server instead of asking for TLS.
- Sent mail says it is UTF-8 (`MIME-Version`, `Content-Type: text/plain;
  charset=UTF-8`, `7bit`/`8bit`), and a subject, organization or display
  name with non-ASCII text is sent as encoded words. Before, other mail
  programs had to guess the character set of text such as an ASCII-art
  signature with box-drawing characters.
- The text editor (message composer and signature) works on whole UTF-8
  characters: the cursor, Delete, Backspace and line wrapping split
  them before, which could leave broken bytes in the text.
- Config → Identities: the signature editor is taller (about six lines),
  is greyed out with the other fields until an identity is chosen, and
  keeps a change to the signature alone (it was saved only when another
  field changed). The unfinished *Primary Box* and *Message Header*
  controls from 0.7.3 were removed. Deleting the selected identity no
  longer leaves its fields editable (they wrote into the deleted
  entry). The Config window is a little taller so the outgoing server's
  notes fit with larger fonts.
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
- The old IMAP4 client was removed (it could not work); a new one is
  being built on libetpan (step 1: connecting, logging in and folders;
  step 2: reading mail; step 3: read state and flags both ways; step 4:
  delete to the Trash, moves and copies, sent mail to the Sent folder;
  step 5: push with IDLE, quiet reconnecting, QRESYNC), and IMAP4
  mailboxes keep their settings.
- Move to folder, Copy to folder and dragging messages work for IMAP
  folders too (they were for Maildir and MH).
- The flagged, replied and forwarded flags of messages are kept in the
  mailbox index, so they survive a restart for every mailbox type.
- How many messages of a folder the list shows (1024 in PostOffice 0.7)
  is a setting: Config → Style → *Message list*, 0 for all.
- Folder filters made for a mailbox's folders now escape `*` and `?` in
  the folder name, so a folder called `Lists*` no longer takes the
  messages of `Lists2`.
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
- A new built-in SMTP client (TLS, STARTTLS, login with SCRAM-SHA-256,
  PLAIN or LOGIN, SIZE, 8BITMIME, SMTPUTF8,
  dot-escaping, every recipient checked) with a server per identity; the
  old one could cut a message short at a line holding a single dot and
  spoke only plain SMTP on port 25. Sending a message with an empty body
  no longer reads past the end of a buffer. Tested against Postfix 3.11
  with Cyrus SASL (every login method, TLS and STARTTLS, refusals).
- Sending: temporary failures are retried at growing intervals for three
  days, refused messages go to OutBox → Drafts (Message → Edit Draft),
  Stop cuts a send off, and Check Mail also sends the queue. A message
  queued less than two minutes before a crash is no longer lost. Error
  dialogs show e-mail addresses whole (FLTK read the "@" as a symbol).
- A certificate store for local CAs (Config → Certificates), trusted by
  every TLS connection on top of the system's CAs. The file requester
  takes a full path typed as the file name.
- Keyboard navigation in the message list (after clicking it): Up/Down,
  Page Up/Down and Home/End move the selection and keep it in view; Right
  opens a thread and Left closes it (on a reply, Left goes to the message
  it answers). The message is shown once the keys pause. The mailbox
  list keeps its selection when it is rebuilt more than once.

## Versions

- **0.8.x**: one release per finished stage of work, each tagged in git
  (`v0.8.1`, `v0.8.2`, ...).
  - 0.8.1: security fixes (no unencrypted POP3/IMAP login, no header
    injection, private mailbox files).
  - 0.8.2: saving and opening received attachments.
  - 0.8.3: sending attachments.
  - 0.8.4: Reply All; replies and forwards show decoded names and
    subjects; Forward leaves To empty.
  - 0.8.5: Cut, Copy and Paste with the clipboard; accented letters at
    the end of words in headers fixed.
  - 0.8.6: the composer wraps lines at 72 characters (adjustable) and
    sends format=flowed; the unused Template menu is removed.
- **0.9.x**: the beta. 0.9.0 has all the features planned for 0.8.x
  (attachments, Reply All, clipboard, line wrapping). The 0.9.x releases
  finish or remove the controls the original author left without code
  (Redirect, Refilter, Print, Search, the address book), fix the known
  issues below and add packaging.
  - 0.9.0: feature-complete beta.
  - 0.9.1: Redirect, printing, Refilter; the separate toolbar window
    works and View menu items put separated windows back; the unused
    MySQL directory setting is removed.
- **1.0.0**: after 0.9.x has been in daily use for a while without
  serious bugs. From then on, 1.0.1 and so on for fixes, 1.1.0 for new
  features.

## Reporting bugs

Info → Build Configuration → **Bug Report** opens a new issue on
[GitHub](https://github.com/dleecurt/Neo-Postoffice/issues) in your web
browser, with the version details filled in (it uses the browser command
set for links; a GitHub account is needed to send it). The old address,
bugs@tarball.net, no longer exists.

## Usenet (NNTP) removed

Reading and posting Usenet news is gone: the NNTP mailbox type, its Config
page, the wizard's news server page, the composer's Newsgroups field and
the group list window. A news mailbox in settings from PostOffice 0.7 is
removed at the next start, with a note saying where its saved articles
remain (they are not deleted). A message in the outbox with no mail
recipient goes to OutBox → Drafts with a note, and the composer no longer
sends a message without To, CC or BCC. A *Newsgroups* header on received
mail is still shown, and filters can still match it.

## Known issues and ideas

Found while modernizing and left for later:

- Settings (including folders added or removed by a check) are saved
  when you quit through File → Quit; if the program is killed instead,
  they are as at the last save, and the next check puts the folders
  right again.
- The built-in SMTP client and IMAP do not support OAuth2 logins.
- HTML-only messages show only an attachment line (which can be saved
  or opened in a browser); there is no HTML to text view.
- SQL mailbox: messages cannot be moved between folders from the client.
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
