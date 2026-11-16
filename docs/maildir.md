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

With fetchmail, deliver through a local mail delivery agent that writes
Maildir (for example `mda "/usr/bin/procmail -d %T"` with a procmail rule
ending in `Maildir/`).

## How it behaves

- **Folders.** The top-level Maildir is *Inbox*. Each Maildir++
  subfolder is a folder: `.Work` is *Work*, `.Lists.2025` is
  *Lists/2025*. New subfolders appear on the next check; empty ones are
  shown too. The mailbox is read once when the program starts, and on
  every Check (or at the interval set in Config).
- **Reading.** Opening a message gives it the Maildir *seen* flag: a file
  in `new/` moves to `cur/` as `name:2,S`, and other flags (for example
  *F*, flagged) are kept.
- **Deleting.** Deleting a message in Neo-PostOffice deletes its file.
- **Other programs.** The Maildir is the authority. A message read,
  marked unread or deleted by another program (mutt, Dovecot, a webmail)
  is shown that way after the next check.
- **Trashed messages** (flag *T*, used by IMAP servers for messages
  waiting to be expunged) are not shown.

The message list shows names and subjects decoded (`=?UTF-8?...?=`).
A copy of each opened message is kept in the mailbox's local storage, as
for the other mailbox types.

## Not yet supported

- Moving messages between folders, and creating or deleting Maildir
  folders, from Neo-PostOffice. A folder deleted on disk stays in the
  folder list (empty) until you remove it in Config → Mailfolders.
- Setting other flags (flagged, replied).
