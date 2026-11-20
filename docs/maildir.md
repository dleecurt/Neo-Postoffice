# Maildir mailboxes

A Maildir mailbox reads mail from a [Maildir](https://cr.yp.to/proto/maildir.html)
on this computer, for example `~/Maildir` filled by getmail, fetchmail
or a local mail server. Folders in the Maildir++ layout used by Dovecot
and Courier (`.Work`, `.Lists.2025`, ...) are shown as folders.

    mail server --TLS--> getmail --> ~/Maildir --> Neo-PostOffice

## Setting it up

In Config → Mailboxes, add a mailbox, choose the protocol *Maildir* and
enter the Maildir folder (default `~/Maildir`). It must contain `cur`,
`new` and `tmp`.

To create an empty Maildir:

    mkdir -p ~/Maildir/{cur,new,tmp}

## getmail

In your getmail rc file, deliver into the Maildir:

    [destination]
    type = Maildir
    path = ~/Maildir/

getmail writes each message to `tmp/` and then moves it to `new/`, so
Neo-PostOffice never sees a half-written message.

To have Neo-PostOffice run getmail itself, enter it as the mailbox's
*Fetch command* in Config → Mailboxes (for example
`getmail --rcfile work.rc`): it then runs before every check, on *Check
Mail* and at the mailbox's interval. See "Fetching mail with getmail or
fetchmail" in the README.

With fetchmail, deliver through a local mail delivery agent that writes
Maildir (for example `mda "/usr/bin/procmail -d %T"` with a procmail rule
ending in `Maildir/`).

## How it behaves

- **Folders.** The top-level Maildir is *Inbox*. Each Maildir++
  subfolder is a folder: `.Work` is *Work*, `.Lists.2025` is
  *Lists/2025*. New subfolders appear on the next check; empty ones are
  shown too, and a folder deleted on disk disappears from the list.
  Folders you make yourself in Config → Mailfolders (with your own
  filters) are left alone. The mailbox is read once when the program
  starts, and on every Check (or at the interval set in Config).
- **Moving and copying.** Drag messages from the message list onto a
  folder, or use *Message → Move to folder...* (Alt+M) or *Copy to
  folder...* (Alt+K), which pop up the folders of all Maildir mailboxes.
  Ctrl+drag copies instead of moving. Select several messages with
  Shift-click or Ctrl-click to move them together; right-click a message
  for the same commands. Messages keep their flags.
  - Within one Maildir a move is a rename of the file.
  - Into another Maildir the message is copied exactly and, for a move,
    then deleted from where it was.
  - From a POP3, IMAP, mbox or SQL mailbox the message is fetched again
    from its source, stored in the Maildir, and for a move deleted from
    the source as the Delete button does. If the source no longer has it
    (for example it was deleted from a POP3 server after downloading),
    it is rebuilt from the local copy; such messages get an
    `X-Neo-PostOffice-Rebuilt` header, as attachments may differ in
    detail from the original.
- **Flags.** *Mark* on the toolbar or *Message → Flag / unflag* (Alt+G)
  flags or unflags the selected messages (Maildir flag *F*); flagged
  messages show a red flag in the list. *Message → Mark as unread*
  (Alt+U) removes the seen flag. Sending a reply sets *R*
  (replied) on the original, sending a forward sets *P* (passed). Flags
  set by other programs are shown after the next check.
- **Reading.** Opening a message gives it the Maildir *seen* flag: a file
  in `new/` moves to `cur/` as `name:2,S`, and its other flags are kept.
- **Deleting.** Deleting a message in Neo-PostOffice deletes its file.
- **Other programs.** The Maildir is the authority. A message read,
  marked unread, flagged, moved or deleted by another program (mutt,
  Dovecot, a webmail) is shown that way after the next check.
- **Trashed messages** (flag *T*, used by IMAP servers for messages
  waiting to be expunged) are not shown.

The message list shows names and subjects decoded (`=?UTF-8?...?=`).
A copy of each opened message is kept in the mailbox's local storage, as
for the other mailbox types.

## Managing folders

Right-click a Maildir mailbox or folder in the folder tree, or use the
*Folder* menu:

- **New folder...** creates a Maildir++ folder. Use `/` for subfolders
  (`Lists/2025` becomes `.Lists.2025`; a missing parent is created too).
  Names cannot contain `.` `=` `{` `}` `"` or `,`.
- **Rename folder...** renames the folder and its subfolders.
- **Delete folder...** asks first, moves the messages of the folder and
  its subfolders to *Trash* (created if needed), then removes them.
  Deleting *Trash* itself deletes its messages permanently.

The Inbox cannot be renamed or deleted. Folders made on disk by other
programs appear on the next check, and folders removed there disappear.

## Not yet supported

- The *D* (draft) flag.
- mbox messages without a Message-ID header are not shown (as in
  PostOffice 0.7.3), and mbox files over 4 GB cannot be indexed.
