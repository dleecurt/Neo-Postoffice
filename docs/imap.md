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
2. **Reading mail.** Per folder `SELECT`, `UIDVALIDITY` and UIDs kept in
   a file in the mailbox directory (a changed `UIDVALIDITY` means the
   folder is read again); `UID FETCH n:* (UID FLAGS RFC822.SIZE
   BODY.PEEK[HEADER])` for new messages; `BODY.PEEK[]` when a message is
   opened; messages gone from the server removed (`CLASS_GONE`).
3. **Flags** both ways: `\Seen`, `\Answered`, `\Flagged`, `\Deleted`,
   `$Forwarded` (`KeepsFlags`, `MarkRead`, `SetFlag`).
4. **Changes on the server:** delete (`\Deleted` + `UID EXPUNGE`, or a
   move to Trash), `UID MOVE` (RFC 6851) or `UID COPY` + delete, `APPEND`
   (copies from other mailboxes, sent mail to a Sent folder).
5. **New mail and speed:** a connection kept open with `IDLE`, falling
   back to the interval; `CONDSTORE`/`QRESYNC` for quick resyncs of large
   folders; reconnecting after a dropped connection.
6. **Folder management:** `CREATE`, `RENAME`, `DELETE`, `SUBSCRIBE`.

## Not planned yet

- `AUTHENTICATE` (SASL) and OAuth2 (`XOAUTH2`/`OAUTHBEARER`). libetpan
  can do both, but Neo-PostOffice uses `LOGIN` only, so providers that
  require OAuth2 (Microsoft 365, Gmail without an app password) do not
  work.

## Testing

The client has been tested against a scripted server (TLS and
plain + STARTTLS, with switches for the refusal cases above), not yet
against Dovecot or a provider.
