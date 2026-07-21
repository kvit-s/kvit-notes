// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTEINDEXFILE_H
#define NOTEINDEXFILE_H

#include <QByteArray>
#include <QHash>
#include <QString>

#include "noteentry.h"

// <root>/.kvit/index.json: the performance sidecar holding what the last scan
// learned about every note, so a warm start can show the collection without
// reading and parsing each file again.
//
// It is a cache and nothing else. Deleting it costs one slow start; the notes
// themselves are the truth. A sidecar that fails to parse, or that carries an
// older format version, is discarded rather than repaired.
//
// This object owns the format and the file. Deciding WHEN to write it stays
// with NoteCollection, along with the dirty flag: a save that fails has to
// leave the collection still owing a write, and that outliving-the-write
// bookkeeping belongs with the object whose state is unsaved.
class NoteIndexFile
{
public:
    // "" closes the file: path() is then empty and load() returns nothing.
    void setRootPath(const QString &rootPath);
    QString path() const;

    // Read the sidecar. `ok` is false for absent, unreadable, malformed or
    // outdated — the caller distinguishes "nothing cached" from "cache
    // present but unusable" by testing whether the file exists.
    QHash<QString, NoteEntry> load(bool *ok) const;

    // Write `notes` to path(). False only when the write was attempted and
    // failed, so the caller keeps the change pending and retries; an empty
    // collection with no file yet writes nothing and reports success.
    bool save(const QHash<QString, NoteEntry> &notes) const;

    // Serialize and write, split apart and static so the asynchronous save
    // can run them on a pool thread against a snapshot rather than against
    // live collection state.
    static QByteArray buildBytes(const QHash<QString, NoteEntry> &notes);
    static bool writeBytes(const QString &path, const QByteArray &bytes);

private:
    QString m_rootPath;
};

#endif // NOTEINDEXFILE_H
