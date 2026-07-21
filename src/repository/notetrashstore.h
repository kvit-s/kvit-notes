// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTETRASHSTORE_H
#define NOTETRASHSTORE_H

#include <QString>

// The <root>/.kvit/trash directory: where deleted notes and folders go
// instead of being removed. Destructive collection operations are not on the
// editor undo stack, so this directory is the only thing standing between a
// mistaken delete and a lost note.
//
// This object knows the directory and the stamped naming scheme, nothing
// else. Containment checking stays with NoteCollection, which validates the
// relative path against the canonical vault root before handing over the
// absolute path — the store never resolves a relative path itself, so it
// cannot be talked into moving a file from outside the vault.
class NoteTrashStore
{
public:
    // "" closes the store: every query then answers as if there were no
    // trash, which is what a collection with no open root reports.
    void setRootPath(const QString &rootPath);
    QString rootPath() const { return m_rootPath; }

    // How many top-level items sit in the trash directory.
    int itemCount() const;
    // Permanent removal of all of them. True when the directory is gone or
    // was never there.
    bool empty();
    // Move one already-contained absolute path into the trash under a
    // timestamped name derived from `name`, disambiguated when the same
    // second already produced that name.
    bool moveIn(const QString &absPath, const QString &name);

private:
    QString trashDirPath() const;

    QString m_rootPath;
};

#endif // NOTETRASHSTORE_H
