# Plan for a new IMAP client

The IMAP4 client of PostOffice 0.7 was removed: it could not download a
message, misread server replies, mixed up message numbers and UIDs, and
had no TLS. This note lists what is ready for its replacement and where
it fits. Nothing here is built yet.

## Ready

- **`NetStream`** (`include/netstream.h`, `src/postoffice/netstream.cpp`),
  used by the POP3 client: TCP over IPv4/IPv6, TLS from the start (993)
  or after `STARTTLS` (143), certificate and host-name checks against the
  system's CAs plus an optional CA file, `ReadLine` for lines of any
  length, `ReadExact (n)` for IMAP literals (`{1234}\r\n` followed by
  1234 bytes), timeouts and a `Cancelled` callback.
- **Password command**: `run_password_command()` in `sendpipe.h`, as POP3
  uses it (setting `passcmd`).
- **Settings** an IMAP mailbox keeps from before: `protocol = "imap4"`,
  `host`, `user`, `pass`. The POP3 names for the new ones should be
  reused: `port`, `security` (`tls`, `starttls`, `none`), `cafile`,
  `passcmd`. Settings → Mailboxes has a panel `gr_prefs_protocol_imap4`
  (now a note) and a hidden *IMAP4 Account* item at index 4 of the
  protocol menu: unhide it and put the fields back.
- **Placeholder**: `mailbox.cpp` creates an `UnavailableHandler` for
  `imap4` mailboxes; replace it with the new handler there.

## Where it fits

An IMAP mailbox behaves like a Maildir that lives on a server, so the new
handler should offer what `MaildirHandler` offers (see `mailhandler.h`):

| Mailbox operation | IMAP |
|---|---|
| `CheckGroup` (check) | `LIST` for folders → `EnsureFolder`; per folder `SELECT`/`STATUS`, `UID FETCH n:* (FLAGS ENVELOPE BODY.PEEK[HEADER])` for new messages, flags for known ones; `CLASS_GONE` for vanished UIDs |
| `GetBody` | `UID FETCH uid BODY.PEEK[]`, then `MarkSeen` |
| `MarkRead`, `SetFlag` (`KeepsFlags`) | `UID STORE uid +FLAGS (\Seen)`, `\Flagged`, `\Answered`, `$Forwarded` |
| `DeleteMessage` | `\Deleted` + `UID EXPUNGE` (or move to the Trash folder) |
| `MoveMessage` (`CanMove`) | `UID MOVE` (RFC 6851), else `UID COPY` + delete |
| `ExportRaw` / `DeliverRaw` | `UID FETCH BODY.PEEK[]` / `APPEND folder (flags)` |
| `FolderOp` (`CanManageFolders`) | `CREATE`, `RENAME`, `DELETE` |
| `CheckFolderName`, `IsFixedFolder` | hierarchy delimiter from `LIST`; `INBOX` is fixed |

Messages should be known by folder, `UIDVALIDITY` and UID (as Maildir
messages are known by their file), kept in a file like POP3's
`uidl.dat`; a changed `UIDVALIDITY` means the folder is read again.

## Steps

1. A small IMAP protocol layer on `NetStream`: tagged commands, untagged
   responses, literals, quoted strings and parenthesized lists;
   `CAPABILITY`, `LOGIN` (or `AUTHENTICATE PLAIN`), `STARTTLS`, `LOGOUT`.
   Testable without the GUI against a local server (Dovecot), as POP3
   was tested against a scripted one.
2. Reading: folders, headers, bodies, read state.
3. Writing: flags, delete, move, copy, append, folder management.
4. Later: `IDLE` for new mail without polling, `CONDSTORE`/`QRESYNC`
   for fast flag updates on large folders.
