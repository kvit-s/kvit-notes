// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef WIKILINKINDEX_H
#define WIKILINKINDEX_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

#include "noteentry.h"

// The [[wiki-link]] graph over an indexed collection: which target names
// resolve to which notes, which notes refer to a given note, and what a
// rename would have to rewrite.
//
// Everything here is derived from state the note repository already holds —
// each entry's outgoing link targets and the set of note paths — so it is
// rebuildable and holds no truth of its own. Resolution runs off a lazily
// built lowercased-basename map that is thrown away whenever the collection's
// revision or note count moves.
//
// The index reads the repository through the accessors handed to the
// constructor rather than owning any of it, so nothing in this file can
// change a note.
class WikiLinkIndex
{
public:
    // One referring note as it stood when a rename was planned: which target
    // keys in it resolve to the note being renamed, how many links that is,
    // and the content hash and modification time that let the apply step
    // refuse to rewrite a file someone edited in the meantime.
    struct RewriteSnapshot {
        QSet<QString> keys;
        QByteArray hash;
        QDateTime modified;
        int linkCount = 0;
    };

    using RevisionProvider = std::function<int()>;
    using AbsolutePathResolver = std::function<QString(const QString &)>;
    // Reads one note's body with front-matter stripped. Bodies are not
    // resident, so the queries that need text — headings, backlink context
    // lines — read the file for the notes they actually need.
    using BodyReader = std::function<QString(const QString &)>;

    WikiLinkIndex(const QHash<QString, NoteEntry> *notes,
                  RevisionProvider revision,
                  AbsolutePathResolver absolutePath,
                  BodyReader readBody);

    // Fence-aware scan of a body for [[...]] targets.
    static QStringList extractLinks(const QString &body);
    // Rewrite the note-part of matching [[...]] occurrences in `text`
    // (outside code fences, alias and #anchor preserved byte-exactly).
    // `oldKeys` holds the lowercased ".md"-stripped note-parts to replace.
    // Returns the number of links rewritten.
    static int rewriteTargetsInText(QString *text,
                                    const QSet<QString> &oldKeys,
                                    const QString &replacement);

    // Drop the basename map. The revision check catches most staleness on its
    // own; this is for the changes that move neither the revision nor the
    // note count, such as a note renamed within the same folder.
    void invalidate() const { m_indexRevision = -1; }

    // Obsidian-compatible resolution: a target matches a note by path
    // suffix — bare "note" matches any **/note.md, "folder/note" requires
    // that suffix; matching is case-insensitive and the ".md" extension is
    // implied. `resolve` returns the note's relPath only for exactly one
    // suffix match, so ambiguous and missing targets both come back "".
    QString resolve(const QString &target) const;
    // The same resolution, keeping the distinction the caller needs when an
    // ambiguous target must not be mistaken for a missing one and
    // auto-created: {status: unique|ambiguous|missing, relPath, candidates}.
    QVariantMap resolution(const QString &target) const;

    // Referrer relPath -> the lowercased note-part keys in it that resolve to
    // `relPath` right now.
    QHash<QString, QSet<QString>> collectReferrers(const QString &relPath) const;
    // The same referrers with their content hashes and link counts, read from
    // disk. Taken BEFORE a rename or move and applied after.
    QHash<QString, RewriteSnapshot> snapshotNoteReferrers(
        const QString &relPath) const;
    // Referrers of anything under a folder, for a folder rename.
    QHash<QString, RewriteSnapshot> snapshotFolderReferrers(
        const QString &oldPrefix) const;

    // Referring notes for the backlinks panel: [{relPath, title, count,
    // contexts}], sorted by relPath. `contexts` are the referring note's raw
    // body lines containing a link that resolves to `relPath`.
    QVariantList backlinksTo(const QString &relPath) const;
    // Heading texts of one indexed note, in document order — what the
    // [[target# completion offers.
    QStringList headingsFor(const QString &relPath) const;

private:
    void ensureIndex() const;

    const QHash<QString, NoteEntry> *m_notes;
    RevisionProvider m_revision;
    AbsolutePathResolver m_absolutePath;
    BodyReader m_readBody;

    // Lazy resolution cache: lowercased basename -> relPaths. Mutable
    // because resolving a target is a const query.
    mutable QHash<QString, QStringList> m_basenames;
    mutable int m_indexRevision = -1;
    mutable int m_indexNoteCount = -1;
};

#endif // WIKILINKINDEX_H
