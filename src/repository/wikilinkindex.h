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
// rebuildable and holds no truth of its own. Resolution runs off a
// lowercased-basename map built on first use and then maintained as note
// paths are added and removed.
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

    using AbsolutePathResolver = std::function<QString(const QString &)>;
    // Where a note that no longer exists at a target's path lives now, or ""
    // when nothing does. Consulted only for targets that name no real note,
    // so a redirect can never shadow a file — see linkredirects.h. The
    // argument is a normalized target, as normalizeTarget() produces.
    using RedirectLookup = std::function<QString(const QString &)>;
    // Reads one note's body with front-matter stripped. Bodies are not
    // resident, so the queries that need text — headings, backlink context
    // lines — read the file for the notes they actually need.
    using BodyReader = std::function<QString(const QString &)>;

    WikiLinkIndex(const QHash<QString, NoteEntry> *notes,
                  AbsolutePathResolver absolutePath,
                  BodyReader readBody);

    // Set the redirect table this index consults for targets that name no
    // note. Unset by default, which is resolution as it was before renames
    // recorded redirects.
    void setRedirectLookup(RedirectLookup lookup)
    { m_redirects = std::move(lookup); }

    // Fence-aware scan of a body for [[...]] targets.
    static QStringList extractLinks(const QString &body);

    // The comparable form of a [[target]]'s note-part: trimmed, anchor
    // dropped, ".md" dropped, leading slashes dropped, lowercased. Public
    // because the redirect table has to key on exactly the same form the
    // resolver compares against.
    static QString normalizeTarget(const QString &target);
    // Obsidian's suffix rule for one candidate: whether `relPath` is what a
    // normalized target names — the whole path, or a trailing run of its
    // segments.
    static bool pathMatchesTarget(const QString &relPath,
                                  const QString &normalizedTarget);
    // Rewrite the note-part of matching [[...]] occurrences in `text`
    // (outside code fences, alias and #anchor preserved byte-exactly).
    // `oldKeys` holds the lowercased ".md"-stripped note-parts to replace.
    // Returns the number of links rewritten.
    static int rewriteTargetsInText(QString *text,
                                    const QSet<QString> &oldKeys,
                                    const QString &replacement);

    // Drop the basename map, for wholesale changes: a root closing or
    // opening, a rescan, a bulk path rewrite. Everything narrower should use
    // the two below.
    void invalidate() const { m_indexValid = false; }

    // One note's path entering or leaving the collection.
    //
    // The map is keyed on note paths and nothing else, so a note's body or
    // metadata changing cannot affect it — but every applied index update
    // used to invalidate the whole thing, and the staleness check was keyed
    // on the collection revision, which moves on every edit. Between them,
    // the first link resolution, backlinks query or heading lookup after any
    // save rebuilt a map over every note in the vault. Adding and removing
    // the one path that actually changed keeps it warm.
    void noteAdded(const QString &relPath) const;
    void noteRemoved(const QString &relPath) const;

    // Obsidian-compatible resolution: a target matches a note by path
    // suffix — bare "note" matches any **/note.md, "folder/note" requires
    // that suffix; matching is case-insensitive and the ".md" extension is
    // implied. `resolve` returns the note's relPath only for exactly one
    // suffix match, so ambiguous and missing targets both come back "".
    // A target naming no note is looked up in the redirect table, so a link
    // to a note that was renamed keeps resolving; `followRedirects` false
    // asks the narrower question of what the vault's files alone say, which
    // is what the rewrite pass needs in order to tell a link it must rewrite
    // from one that already resolves.
    QString resolve(const QString &target, bool followRedirects = true) const;
    // The same resolution, keeping the distinction the caller needs when an
    // ambiguous target must not be mistaken for a missing one and
    // auto-created: {status: unique|ambiguous|missing, relPath, candidates},
    // plus `redirected` when a redirect answered rather than a file.
    QVariantMap resolution(const QString &target,
                           bool followRedirects = true) const;

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
    // The map key for a note path: its basename, ".md" dropped, lowercased.
    static QString basenameKey(const QString &relPath);

    const QHash<QString, NoteEntry> *m_notes;
    RedirectLookup m_redirects;
    AbsolutePathResolver m_absolutePath;
    BodyReader m_readBody;

    // Lazy resolution cache: lowercased basename -> relPaths. Mutable
    // because resolving a target is a const query. Built on first use and
    // then maintained in place; the note count is a cheap self-check that
    // catches a path change that reached m_notes without telling this index.
    mutable QHash<QString, QStringList> m_basenames;
    mutable bool m_indexValid = false;
    mutable int m_indexNoteCount = -1;
};

#endif // WIKILINKINDEX_H
