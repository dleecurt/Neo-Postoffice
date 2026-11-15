# PostOffice 0.7.3

PostOffice is an X11 mail and Usenet client for Unix, written in C++ with the
FLTK toolkit. This repository preserves the 0.7.3 source release (June 2000).

PostOffice was written by **Pim van Riezen**, Mad Science Labs, Rotterdam, The
Netherlands, with sponsorship from Vuurwerk Internet, Haarlem. This is an
archival import, not the original project home. The upstream FTP site
(ftp.vuurwerk.nl) no longer exists.

## Features

- POP3, IMAP4, SMTP and NNTP (Usenet) support, with a background outbox
- Mail storage in mbox files, a GDBM message store, or a MySQL database
- MDI interface with a folder tree, message index, composer, preferences
  panel and first-run setup wizard
- Threaded back end (POSIX threads) for fetching mail in the background

## Contents

| Path | What it is |
|---|---|
| `src/postoffice` | The PostOffice client |
| `src/libfltk` | FLTK 1.0.4 (bundled) |
| `src/libgdbm` | GNU DBM 1.8.0 (bundled) |
| `src/fl_editor` | Fl_Editor text editor widget 0.3, by Robert Kesterson |
| `src/toggletree` | Fl_Toggle_Tree widget 0.2.4 by Maarten de Boer and James Dean Palmer, with PostOffice patches |
| `include` | Shared headers, including the FLTK headers |
| `config`, `pkg` | Build system: Perl configure script and package descriptions |

## Building

The original build supports IRIX 6.2+, Solaris and Linux of the period:

    make            # runs config/softlinks, config/configure, then builds every package
    make clean
    make bindist

The build needs Perl, a C++ compiler, X11, libjpeg and the MySQL client
library. **It will not build unmodified on a current Linux system.** For
example, some sources include the pre-standard `<iostream.h>` header, and the
bundled FLTK 1.0.4 is very old. The `make update` target fetched snapshots
from the defunct upstream FTP server and no longer works.

## Changes from the original tarball

The source code is unchanged except for the following cleanup for publication:

- Removed the `CVS/` metadata directories, editor backup files (`*~`) and
  `xv` thumbnail caches (`.xvpics/`)
- Removed the generated `src/postoffice/version.h`, which `makeversion`
  recreates during the build
- In `helpdesk.fld` / `helpdesk.cpp`, replaced design-time placeholder text in
  the About dialog (the original developer's workstation hostname and login)
  with `(kernel)` and `(build info)`. The program replaces these labels with
  real build information at run time, so its behavior is unchanged.
- Added this README, `LICENSE` and `.gitignore`

## License

PostOffice is licensed under the **GNU Lesser General Public License**. The
source headers name the license without a version number. `LICENSE` contains
LGPL 2.1, the first version to use the "Lesser" name.

The bundled components keep their own licenses:

- **FLTK 1.0.4**: GNU LGPL with FLTK exceptions. See `src/libfltk/COPYING`.
- **GNU DBM 1.8.0**: GNU GPL version 2. See `src/libgdbm/gdbm-1.8.0/COPYING`.
  Binaries that statically link it are covered by the GPL.
- **Fl_Toggle_Tree**: GNU Library General Public License. See
  `src/toggletree/README`.
- **Fl_Editor**: this copy contains no license statement. See
  `src/fl_editor/README.TXT` for the author's contact details.

Copyright (C) 1999-2000 Pim van Riezen / Mad Science Labs. Original copyright
notices are kept in all source files.
