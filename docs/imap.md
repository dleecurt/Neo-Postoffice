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
3. **Flags** both ways: `\Seen`, `\Answered`, `\Flagged`, `\Deleted`,
   `$Forwarded` (`KeepsFlags`, `MarkRead`, `SetFlag`).
4. **Changes on the server:** delete (`\Deleted` + `UID EXPUNGE`, or a
   move to Trash), `UID MOVE` (RFC 6851) or `UID COPY` + delete, `APPEND`
   (copies from other mailboxes, sent mail to a Sent folder).
5. **New mail and speed:** a connection kept open with `IDLE`, falling
   back to the interval; `CONDSTORE`/`QRESYNC` for quick resyncs of large
   folders; reconnecting after a dropped connection.
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
`F HASH UIDVALIDITY UIDNEXT MESSAGES name` per folder and a line
`U 1:49,51:499` with the UIDs known in it. A damaged `U` line makes the
folder be read again; a lost file only costs fetching the headers again
(nothing kept here is removed for it).

The window removes messages after the mailbox was saved, so a crash
could bring removed messages back, and an unchanged folder is not looked
at again. After each check, messages of folders that were checked
completely, and of folders no longer subscribed, are compared with
`imap.dat` and removed if they are not known there.

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

Not tested yet: Dovecot, Cyrus, Gmail and other providers. Reports are
welcome.
