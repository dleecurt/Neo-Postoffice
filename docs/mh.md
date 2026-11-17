# MH mailboxes (nmh)

An MH mailbox reads the mail folders of [nmh](https://www.nongnu.org/nmh/)
(and the older MH and its relatives, such as mmh) on this computer. nmh
and Neo-PostOffice can be used on the same folders at the same time: what
one reads, flags, moves or deletes the other sees at its next look.

    mail server --> inc / getmail / procmail --> ~/Mail/inbox --> Neo-PostOffice
                                                      ^
                                     nmh (scan, show, refile, rmm)

## Setting it up

In Config → Mailboxes, add a mailbox and choose the protocol *MH (nmh)*.
Leave *MH directory* empty to use the mail directory from the nmh profile;
or enter a directory to read that one instead.

The nmh profile is `$MH`, or `~/.mh_profile` if `MH` is not set. These
entries are used (everything else in it is left alone):

| Entry | Used for | Default |
|-------|----------|---------|
| `Path:` | The mail directory, relative to your home directory | `Mail` |
| `Inbox:` | The inbox folder, which cannot be renamed or deleted | `inbox` |
| `Unseen-Sequence:` | The sequences of unread messages (one or more names) | `unseen` |
| `mh-sequences:` | The name of each folder's sequence file | `.mh_sequences` |
| `rmmproc:` | The program that deletes messages | (none) |

The profile is read again at every check, so a change to it takes
effect without restarting.

If nmh is not set up yet, `install-mh` creates the profile and
`~/Mail`; Neo-PostOffice itself only needs the directory to exist.

## How it behaves

- **Folders.** Every directory in the mail directory is a folder,
  nested ones too: `~/Mail/lists/2025` is the folder *lists/2025*.
  Folders made with `folder -create` appear at the next check, and a
  folder removed with `rmf` disappears from the list. A directory named
  by a number (`2025`) is a folder like any other; names starting with
  `.`, `,` or `#` are skipped, as nmh uses them for its own files.
- **Messages** are the files named by a number. A message stays the same
  message when nmh renumbers the folder (`folder -pack`, `sortm`), since
  it is known by its file and not by its number.
- **Read state** is the profile's Unseen-Sequence. Opening a message
  takes it out of all the unseen sequences; *Mark as unread* puts it back
  into the first one. Mail that `inc` delivers is in the unseen sequence,
  so it shows as new here.
- **Flags.** *Flag / unflag* uses the sequence `flagged`, and replying
  and forwarding add the message to `replied` and `forwarded` (the names
  mutt uses for MH folders). In nmh they can be used as usual:
  `scan flagged`, `mark -sequence flagged -add`.
- **Sequences** are changed under an fcntl lock, the same lock nmh
  takes, and every other sequence in the file (`cur`, your own ones) is
  kept as it was. Empty sequences are left out, as nmh does.
- **Deleting** works like `rmm`: if the profile has an `rmmproc`, it is
  run in the folder with the message number (as rmm does); otherwise the
  file is renamed to `,NUMBER` and can be brought back with
  `mv ,12 12` until something removes the backups. The message leaves
  all sequences.
- **Moving and copying** a message, also from or to another mailbox,
  stores it under the next free number of the target folder, byte for
  byte as it was, with its read state and flags. If `inc` takes that
  number at the same moment, the next one is used.
- **Folder management.** A right-click in the folder tree (or *File →
  Folder*) creates, renames and deletes folders. Renaming takes the
  subfolders along. Deleting a folder moves its messages, and those of
  its subfolders, into the folder *Trash* (created when needed) and then
  removes the directories; deleting *Trash* itself deletes its
  messages.

## Notes

- Neo-PostOffice reads the folders; it does not deliver mail. Keep using
  `inc`, or let getmail, fetchmail or procmail deliver into the inbox
  (procmail writes an MH folder when the folder name in a rule ends in
  `/.`, as in `Mail/inbox/.`). The mailbox's *Fetch command* in Config →
  Mailboxes (for example `inc` or `getmail`) is run before every check.
- The ",NUMBER" backups are not removed by Neo-PostOffice. nmh does not
  remove them either; a cron job such as
  `find ~/Mail -name ',[0-9]*' -mtime +30 -delete` does.
