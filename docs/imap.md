# The IMAP4 client

The IMAP4 client of PostOffice 0.7 was removed: it could not download a
message, misread server replies, mixed up message numbers and UIDs, and
had no TLS. The new one is built on [libetpan](https://github.com/dinhvh/libetpan)
(BSD licence, used by Claws Mail), which parses the protocol, and on the
`NetStream` connection the POP3 client uses. It is built in steps; each
step is tested and committed on its own.

## Decisions

- **libetpan, not our own parser.** It is optional at build time
  (`WITH_LIBETPAN`, default `AUTO`); without it, IMAP4 mailboxes keep
  their settings and show a note.
- **One TLS implementation.** libetpan reads and writes through a
  `NetStream` (a libetpan stream driver in `imap4handler.cpp`), so TLS,
  STARTTLS, the certificate checks (system CAs, the certificate store in
  `~/.postoffice/certs` and an optional CA file,
  host name, TLS 1.2 or newer, no way to turn them off), timeouts and
  cancelling are the same as for POP3.
- **Storage:** headers are fetched for the message list, a body when the
  message is opened; both are kept in the mailbox's local store. Mail
  stays on the server.
- **New mail:** IDLE when the server has it, else checking on the
  mailbox's interval.
- **Folders:** the folders subscribed to on the server, and always the
  inbox.

## Settings

The same names as POP3: `host`, `port` (empty: 993 for TLS, 143
otherwise), `security` (`tls`, `starttls`, `none`; empty, as in
mailboxes from PostOffice 0.7, means `starttls`), `user`, `pass`,
`passcmd` (the first line it prints is the password), `cafile`,
`interval`. Config → Mailboxes shows the POP3 server fields, labelled
for IMAP.

## Folder names

INBOX is shown as *Inbox*. The personal namespace prefix (`INBOX.` on
Courier and Cyrus, from `NAMESPACE`) is left out, names are decoded from
modified UTF-7 to UTF-8, and the server's hierarchy delimiter is shown
as `/` (a `/` inside a name on a server whose delimiter is `.` is shown
as `.`). Folders marked `\Noselect` or `\NonExistent` are not shown.

## Steps

1. **Done: build and connect.** CMake option; connecting over TLS,
   STARTTLS or plain; refusing a PREAUTH greeting before STARTTLS, data
   sent along with the STARTTLS reply (injection), and logins the server
   marks `LOGINDISABLED`; capabilities read again after TLS and after
   login; `LOGIN` with the password or password command; `NAMESPACE`;
   `LSUB` → folders, and folders no longer subscribed are removed. The
   connection is closed after each check.
2. **Done: reading mail.** See *Reading* below.
3. **Done: flags both ways.** See *Flags* below. (`\Deleted` belongs to
   step 4.)
4. **Done: changes on the server.** See *Deleting, moving and storing*
   below.
5. **Done: push and speed.** See *Push* below.
6. **Folder management:** `CREATE`, `RENAME`, `DELETE`, `SUBSCRIBE`.

## Reading

Each check lists the folders, then goes through them in order:

1. `EXAMINE` (read-only). If `UIDVALIDITY`, `UIDNEXT` and the message
   count are what they were at the last complete check of the folder,
   nothing changed and the folder is done.
2. A changed `UIDVALIDITY` means the server renumbered the folder: every
   message of it here is removed and the folder is read again.
3. `UID SEARCH ALL` lists the UIDs on the server. Known UIDs that are not
   there any more are removed here (`CLASS_GONE`).
4. New UIDs are fetched in batches of 200 with `UID FETCH set (UID FLAGS
   INTERNALDATE BODY.PEEK[HEADER.FIELDS (...)])`: only the header fields
   the list and threading need, so a first check of a large folder stays
   small. Messages newer than everything known are added at the end of
   the folder (oldest first); the rest (the whole folder at its first
   check) are fetched newest first and added at the start. The folder
   (and the saved index) stays in UID order either way, and recent mail
   shows up first. `\Seen`, `\Answered`, `\Flagged` and `$Forwarded` give
   the read state and flags; `INTERNALDATE` the date for a message
   without a `Date` header.
5. The mailbox and `imap.dat` are saved after every
   batch, so a crash or **Stop** loses at most one batch; the next check
   carries on where it stopped.

A message's text is fetched with `UID FETCH uid (BODY.PEEK[])` when it is
opened: `PEEK`, so it stays unread on the server. The whole header
replaces the few fields fetched for the list, and the text is kept in
the mailbox's store. Messages opened during a check are fetched between
two batches. The session stays open after a message is opened, so
reading several messages logs in once; it is closed after a few idle
minutes and before each check. libetpan's progress callback shows how
much of a large message has come in.

Messages are filed under made-up IDs, `<imap.HASH.UIDVALIDITY.UID@imap>`
(HASH: 64-bit FNV-1a of the server's folder name), so the same message in
two folders, or a message without a `Message-ID`, is no problem.

`imap.dat` (mode 600, written to a new file and renamed) has a line
`F HASH UIDVALIDITY UIDNEXT MESSAGES name` per folder, then `M modseq`
(its `HIGHESTMODSEQ`, with CONDSTORE) and `U 1:49,51:499` (the UIDs known
in it); at the end, `P <message-id> +S-F` for each message with flag
changes not yet sent. A damaged `U` line makes the
folder be read again; a lost file only costs fetching the headers again
(nothing kept here is removed for it).

The window removes messages after the mailbox was saved, so a crash
could bring removed messages back, and an unchanged folder is not looked
at again. After each check, messages of folders that were checked
completely, and of folders no longer subscribed, are compared with
`imap.dat` and removed if they are not known there.

## Flags

`\Seen`, `\Flagged`, `\Answered` and `$Forwarded` are kept in step with
the server, as the read state and the flagged, replied and forwarded
flags here.

**Here to the server.** The handler says it keeps flags, so the window
sends it what Maildir and MH get: opening a message (its text fetched
or already here) means `\Seen`; *Mark as unread*, *Flag / unflag* and a
reply or forward sent set or clear the others. Each change is put on the
message here and in a queue (`P` lines in `imap.dat`), then sent at once:
the folder is opened with `SELECT` and the changes go out as one
`UID STORE uids +FLAGS.SILENT (...)` or `-FLAGS.SILENT` per flag and
direction. If the server cannot be reached, the queue waits (without an
error dialog) and is sent first thing at the next check. A flag with a
change still queued keeps the value set here when the server's flags are
read. A folder the server opens read-only gets one message saying so,
and its changes stay here only; a refused `STORE` (a server that does not
take `$Forwarded`, say) is reported and not tried again.

**The server to here.** Each check, before looking for new messages:

- With CONDSTORE, folders are opened with `EXAMINE folder (CONDSTORE)`,
  which gives `HIGHESTMODSEQ`. If that and `UIDNEXT` and the count are
  what they were, the folder is done (one command). If only the modseq
  changed, `UID FETCH 1:* (UID FLAGS) (CHANGEDSINCE n)` reads just the
  messages changed since the last check.
- Without CONDSTORE, `UID FETCH 1:* (UID FLAGS)` reads the flags of every
  message of every folder that has messages, at each check.

The flagged, replied and forwarded flags are saved in the mailbox index
(bits 8 to 10 of its flags field), so they survive a restart without
being read from the server again.

## Deleting, moving and storing

Decided with the project's owner: Delete moves to the server's Trash
(Delete in the Trash is final); sent mail goes to an IMAP Sent folder
when the identity says so (*Save sent mail to*, off by default); drafts
stay local (OutBox → Drafts).

**The Trash and Sent folders** are found once per session: a subscribed
folder with the special-use flag (RFC 6154, as in the LSUB reply); else
`LIST "" "*"` and its flags; else a usual name (Trash, Deleted Items,
Deleted Messages, Deleted, `[Gmail]/Trash`; Sent, Sent Items, Sent
Messages, Sent Mail, `[Gmail]/Sent Mail`), with or without the personal
namespace prefix; else the folder is made (`CREATE prefix+name`). One
that is not subscribed is subscribed, so it shows here.

**Deletes and moves** are queued in `imap.dat` (`O T|X|M <message-id>
[target]`) and sent at once, or first thing at the next check:

- T (Delete): to the Trash; in the Trash, X instead.
- M (Move within the account): to the target folder.
- X (the original of a message moved to another mailbox, once that
  mailbox has it; the window calls `RemoveAfterTransfer`, which other
  mailbox types take as a delete): removed for good.

A move is `UID MOVE` (RFC 6851); without MOVE, `UID COPY`, then
`UID STORE +FLAGS.SILENT (\Deleted)` and an expunge. Removing for good is
`\Deleted` and the expunge. The expunge is `UID EXPUNGE` of just those
messages when the server has UIDPLUS; without it, `EXPUNGE`, which also
removes messages marked deleted by another program. The messages leave
their folder here once the server has done it; a moved one shows up in
its new folder at the next check (the window starts one after a move),
fetched like any new message. A change the server refuses is reported,
and its messages come back here at the next check.

**Storing** (a copy or move from another mailbox, a copy of sent mail)
comes from another mailbox's thread, which cannot use this mailbox's
connection. `DeliverRaw` writes the message to `spool/` in the mailbox
directory (a private file, synced and renamed into place, with a first
line naming the folder and flags), so the other mailbox can delete its
original at once; this mailbox's thread sends the spool with `APPEND`
(flags `\Seen`, `\Flagged`, `\Answered`, `$Forwarded`; CRLF line ends; an
mbox `From ` line in front is left out) at each check and after each
change made here. The window starts a check of the target after a copy;
the outbox does after a sent copy. A message the server refuses is
renamed `*.failed` and reported.

## Push

Decided with the project's owner: IDLE on the Inbox only (other folders
at the interval); a per-mailbox setting, on by default (`idle`, "N" for
off); a lost connection is made again quietly, and the user is told once
if it is still down after 10 minutes.

The mailbox thread polls its queue every 15 ms; while it has nothing to
do it now calls the handler's `Idle()` about three times a second, and
does not close an idle connection the handler keeps open on purpose
(`KeepsConnection()`: push on, and the server offers IDLE, or a
reconnect is pending). For IMAP, `Idle()`:

- starts push by itself once after the program starts (as a reconnect
  that has not happened yet), so the Inbox is pushed even with no timer;
- with an open session not in IDLE (after a check, a message opened, a
  flag changed): `EXAMINE` the Inbox and `IDLE`;
- in IDLE: looks for data without waiting, on the socket (`poll`) and in
  the buffers that hide it from the socket (libetpan's read buffer and
  OpenSSL's decrypted bytes, `NetStream::Pending`). Data: `DONE`, the
  Inbox is brought up to date as in a check (flags, removals, new
  messages), and `IDLE` again. After 10 minutes without data: `DONE` and
  `IDLE` again (servers end IDLE after 30 minutes, routers drop quiet
  connections, and a dead connection shows up here);
- any other work ends IDLE first (`Session()` sends `DONE`). A check
  closes the connection and makes a new one, as for every mailbox; push
  starts again after it. **Stop** closes it and push waits for the next
  check;
- a connection lost while pushing (an error on `DONE`, a dropped socket)
  is made again after 10 s, 30 s, 60 s, 120 s and then every 5 minutes,
  quietly; once back, the queued changes and the spool go out and the
  Inbox is brought up to date. After 10 minutes down, one message says
  since when and why. With push on, a timer check that cannot connect
  joins this instead of showing an error; a check the user starts shows
  its error at once.

**Faster checks of changed folders.** With QRESYNC (enabled after
logging in), the flag read of a changed folder is `UID FETCH 1:* (UID
FLAGS) (CHANGEDSINCE n VANISHED)`, which also names the messages removed
since. New messages: `UID SEARCH UID next:*` (the UIDs above the last one
known; the server answers `n:*` with at least its highest UID, which is
left out). If the known UIDs, less the removed ones, plus the new ones
make the folder's count, that is the list; otherwise (no QRESYNC and
something removed, or the first check) all UIDs are listed as before.

## Not planned yet

- `AUTHENTICATE` (SASL) and OAuth2 (`XOAUTH2`/`OAUTHBEARER`). libetpan
  can do both, but Neo-PostOffice uses `LOGIN` only, so providers that
  require OAuth2 (Microsoft 365, Gmail without an app password) do not
  work.

## Testing

Step 1 was tested against a scripted server (TLS and plain + STARTTLS,
with switches for the refusal cases above).

Step 2 was tested against [mox](https://github.com/mjl-/mox) 0.0.15
(`mox localserve`, a full IMAP4rev2 server with its own certificate,
imported into the certificate store), with libetpan 1.10.1 and 1.9.4,
under AddressSanitizer and UndefinedBehaviorSanitizer (no reports):

| Case | Result |
|---|---|
| First check: Inbox of 1209 messages, nested folder, `Résumé & Co` (modified UTF-7), empty special folders | All 1215 arrive; folders shown, two old ones removed |
| Order | Strictly UID order in the folder and the saved index, also after a restart |
| Flags | `\Seen`, `\Flagged`, `\Answered`, `$Forwarded` from the server |
| Encoded subject, no `Date` header, dots and 8-bit text, 3 MB attachment | Shown right; the date from `INTERNALDATE` |
| Second check after 2 new and 12 deleted messages | Only the 2 fetched (appended), the 12 removed; unchanged folders cost one `EXAMINE` |
| Opening a message | One `UID FETCH ... BODY.PEEK[]`; opening it again uses the copy here |
| Throttled link (100 KB/s): opening a message during the first check | Shown between two batches |
| Stop during the first check, then Check | Stops at a batch; the next check fetches the rest, order intact |
| Folder deleted and created again (new `UIDVALIDITY`) | Old messages removed, new ones fetched |
| Folder unsubscribed | It and its messages removed |
| App killed right after messages were removed | Removed again at the next check |
| `imap.dat` damaged, or deleted | Rebuilt; nothing duplicated or lost |
| Copy of an IMAP message to a Maildir | The original text, complete |

Step 3, against mox with CONDSTORE, and through a relay that hid CONDSTORE
from its capabilities:

| Case | Result |
|---|---|
| Flags changed by another client (`\Seen`, `\Flagged` both ways, `\Answered`) | Shown here after the next check |
| Second check with nothing changed | One `EXAMINE (CONDSTORE)` per folder |
| A flag changed in one folder | `UID FETCH 1:* (UID FLAGS) (CHANGEDSINCE n)` there only |
| Mark as unread, Flag, opening an unread message | `UID STORE` at once; the server shows the change |
| Reply and forward sent | `\Answered` and `$Forwarded` set on the original |
| Server stopped, two changes made, server started, Check | Changes shown here at once, queued, sent at the start of the check |
| Server without CONDSTORE | Plain `EXAMINE` and a full flag read per folder; changes arrive |
| Restart without a check | Flags kept (from the index) |

Step 4, against mox (MOVE, UIDPLUS, special-use flags), and through a
relay that hid MOVE and CONDSTORE; with libetpan 1.10.1, and Delete again
with 1.9.4:

| Case | Result |
|---|---|
| Delete in Inbox | `UID MOVE` to Trash (found by its `\Trash` flag); shown in Trash after the check |
| Delete in Trash | `\Deleted` + `UID EXPUNGE` of that UID; gone from the server |
| Drag to another folder of the account | `UID MOVE`; shown in the new folder after the check |
| Delete with the server stopped, server started, Check | Queued, no error dialog; moved to Trash at the start of the check |
| Copy from a Maildir to the IMAP Archive folder | `APPEND` with its flags; spool emptied; the copy shows in Archive |
| Move from IMAP to a Maildir | Stored in the Maildir with its flags; the IMAP original removed for good, not put in Trash |
| Server without MOVE: move, and Delete | `UID COPY` + `\Deleted` + `UID EXPUNGE` |
| Message sent with *Save sent mail to* set | `APPEND Sent (\Seen)` with the headers as sent |
| Message sent with the IMAP server down | Copy kept in the spool; stored at the next check |

Step 5, against mox (IDLE, CONDSTORE, QRESYNC), with libetpan 1.10.1:

| Case | Result |
|---|---|
| Program started, no check pressed (interval 0) | Connected by itself, `ENABLE QRESYNC`, Inbox brought up to date, `IDLE` |
| A message delivered to the Inbox by another client | Shown about a second later: `CHANGEDSINCE ... VANISHED`, `UID SEARCH UID n:*`, the header fetched, `IDLE` again |
| A flag set and a message expunged by another client | Both shown at once; the removal from VANISHED, no full UID listing |
| A message opened during IDLE | `DONE`, the body fetched, `\Seen` stored, `IDLE` again |
| Server stopped during IDLE | No dialog; back by itself 25 seconds after the server returned, with the mail that came meanwhile |
| Program started with the server down, left for 10 minutes | Quiet at the start; one message at 10 minutes; back by itself when the server returned, no second message |
| 10 minutes in IDLE with no news | `DONE` and `IDLE` again |

Not tested yet: Dovecot, Cyrus, Gmail and other providers. Reports are
welcome.
