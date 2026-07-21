// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef SEARCHINDEXFEED_H
#define SEARCHINDEXFEED_H

#include <QList>
#include <QString>

#include <functional>

class CollectionSearchIndex;
struct ReconcileEntry;

// The one-way channel from the note repository to the global-search database.
//
// The search index is rebuildable: everything in it can be reconstructed from
// the Markdown files, so nothing here is a source of truth and a failure to
// feed it costs a stale search result rather than a lost note. That is why
// this is a separate layer from the repository, and why the whole channel is
// optional — with no index attached, the collection spawns no worker threads
// and opens no database, which is what the tests and tools that only want
// collection metadata rely on.
//
// The feed tracks which root the index is currently open for. Every write is
// gated on that root still matching the collection's, so work queued for a
// vault the user has left cannot land in the database of the one they opened
// next.
class SearchIndexFeed
{
public:
    // The collection supplies the listing lazily: reconcile is the only
    // operation that needs it, and building it walks every note.
    using ListingProvider = std::function<QList<ReconcileEntry>()>;
    using AbsolutePathResolver = std::function<QString(const QString &)>;

    SearchIndexFeed(ListingProvider listing, AbsolutePathResolver absolutePath);
    ~SearchIndexFeed();

    // Attaching a different index (or none) forgets which root was open, so
    // the next sync reopens rather than assuming.
    void setIndex(CollectionSearchIndex *index);
    CollectionSearchIndex *index() const { return m_index; }
    bool hasIndex() const { return m_index != nullptr; }

    // Open (or reopen) the index for `rootPath` and reconcile it against the
    // repository listing: the cold build and the warm-startup sync. An empty
    // root closes the index instead. Called at each scan and refresh settle
    // point.
    void syncTo(const QString &rootPath);
    // Open for `rootPath` without reconciling. The asynchronous open takes
    // this path: it has no complete listing yet, so it readies the database
    // and lets the reconcile that catches up to on-disk changes wait for the
    // scan to settle.
    void openFor(const QString &rootPath);
    // Tear the index down without reconciling, for a vault being closed.
    void close();

    // Reparse or drop one note. Both no-op unless the index is open for
    // `rootPath`, so a queued write cannot reach another vault's database.
    void reindexNote(const QString &rootPath, const QString &relPath);
    void dropNote(const QString &rootPath, const QString &relPath);
    // Reindex a note whose text the caller already holds, so the worker skips
    // a redundant disk read. Writes are queued first-in-first-out, so two
    // rapid saves cannot let the older parse win.
    void reindexNoteFromText(const QString &rootPath,
                             const QString &relPath,
                             const QString &fileText,
                             qint64 fileSize,
                             qint64 modifiedMs);

private:
    bool servesRoot(const QString &rootPath) const;

    CollectionSearchIndex *m_index = nullptr;
    QString m_openRoot; // the root the index is currently open for
    ListingProvider m_listing;
    AbsolutePathResolver m_absolutePath;
};

#endif // SEARCHINDEXFEED_H
