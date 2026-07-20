// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef RECOVERYJOURNALSTORE_H
#define RECOVERYJOURNALSTORE_H

#include <QString>
#include <QStringList>

// The <root>/.kvit/recovery directory: one journal file per note that had
// unsaved edits when the session ended. DocumentManager writes and removes
// them as the open note goes dirty and clean; a journal still present when a
// vault is opened is evidence of a crash.
//
// A journal is a full file image, front-matter included, so restoring one is
// a plain write of its bytes over the note. The file name IS the note's
// relative path, percent-encoded, which keeps the directory flat.
//
// This object owns the directory and the pending list. Deciding what a
// restore means for the rest of the collection — recreating a folder the
// note used to live in, reindexing, reporting failure — stays with
// NoteCollection.
class RecoveryJournalStore
{
public:
    // "" closes the store and empties the pending list.
    void setRootPath(const QString &rootPath);

    // Absolute path of one note's journal, creating the directory. "" when
    // no root is set or `relPath` is empty.
    QString journalPathFor(const QString &relPath) const;

    // Re-read the directory: the relative paths of every journal present.
    // Called when a vault is opened, so what it finds is crash evidence.
    void reload();
    // The notes still awaiting a restore-or-discard decision.
    QStringList pending() const { return m_pending; }
    bool isPending(const QString &relPath) const;
    // Read one journal's full file image. `ok` distinguishes an unreadable
    // journal from an empty one.
    QString readJournal(const QString &relPath, bool *ok) const;
    // Delete the journal and drop it from the pending list.
    void resolve(const QString &relPath);
    void clear();

private:
    QString m_rootPath;
    QStringList m_pending; // relPaths with startup journals
};

#endif // RECOVERYJOURNALSTORE_H
