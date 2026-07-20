// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTEENTRY_H
#define NOTEENTRY_H

#include <QDateTime>
#include <QString>
#include <QStringList>

#include "notefrontmatter.h"

// The collection's domain model: one indexed note and one indexed folder.
// These live outside NoteCollection so that the layers built on top of the
// note repository — the wiki-link index, the search feed, the view models —
// depend on the model rather than on the object that owns the repository.
// NoteCollection re-exports both under their original nested names, so
// `NoteCollection::NoteEntry` continues to name this type.

struct NoteEntry {
    QString relPath;  // "Ideas/Reading list.md", '/'-separated
    QString folder;   // "Ideas"; "" at the root
    QString title;    // file name without ".md"
    QDateTime created;
    QDateTime modified;
    qint64 fileSize = -1;
    int wordCount = 0;
    QString snippet;  // body display text, markers stripped
    NoteFrontMatter::Metadata meta; // tags/pinned/favorite + foreign keys
    // Outgoing [[wiki-link]] targets, raw (heading anchors kept, aliases
    // stripped), in document order with duplicates — backlink counts come
    // from here. Extracted from the file on every
    // (re)index and persisted in the sidecar so warm startup keeps the
    // backlink graph without reading every note.
    QStringList links;
    // Note bodies and per-block display text are NOT held resident: global
    // search reads them from the SQLite index, and features that need one
    // note's text read that file on demand.
};

struct FolderEntry {
    QString relPath;  // "Ideas/Projects"
    QString name;     // "Projects"
    QString color;    // "" = default
    bool expanded = true;
};

#endif // NOTEENTRY_H
