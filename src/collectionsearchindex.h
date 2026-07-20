// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef COLLECTIONSEARCHINDEX_H
#define COLLECTIONSEARCHINDEX_H

#include <QList>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

#include "searchindexdb.h"

class QThread;
class SearchIndexWriteWorker;
class SearchIndexReadWorker;

// One note as the reconcile pass sees it on disk: enough to decide whether the
// index copy is stale without reading the body (search.md §6.2).
struct ReconcileEntry {
    QString relPath;
    QString absPath;
    qint64 fileSize = 0;
    qint64 modifiedMs = 0;
};

// The GUI-free coordinator for disk-backed global search (search.md §11).
//
// It owns two SQLite connections on two worker threads: a write connection that
// reconciles, replaces, and removes notes, and a read connection that answers
// queries. Callers on the GUI thread post work through the public methods and
// receive results through queued signals; no SqlQuery, connection name, or row
// id ever crosses the boundary. The database lives under the cache directory,
// keyed by the notes root, and is rebuilt from Markdown whenever it is missing
// or invalid, so deleting it never loses user data.
class CollectionSearchIndex : public QObject
{
    Q_OBJECT

public:
    explicit CollectionSearchIndex(QObject *parent = nullptr);
    ~CollectionSearchIndex() override;

    // Whether packaged SQLite has FTS5 with the trigram tokenizer. Callers fall
    // back to the legacy scanner in development when this is false (search.md
    // §6.3).
    static bool capabilityAvailable();

    bool isUsable() const { return m_usable; }
    bool isIndexing() const { return m_indexing; }

    // --- Lifecycle (GUI thread) -----------------------------------------
    // Opens (creating) the cache database for `rootPath` and readies both
    // connections. A closed or empty root tears the index down.
    void openForRoot(const QString &rootPath);
    void closeIndex();

    // Absolute path of the cache database for a notes root — exposed for tests
    // and diagnostics (search.md §1).
    static QString databasePathForRoot(const QString &rootPath);

    // Parse one note's file text into the indexable form: title as the kind-0
    // block, each body block's display text, the code-block verbatim flag, and
    // the front-matter tags (search.md §5). Static and pure so the differential
    // oracle can build the same rows without a database.
    static IndexedNote parseNote(const QString &relPath, const QString &fileText,
                                 qint64 fileSize, qint64 modifiedMs);

    // --- Content feed (thread-safe) -------------------------------------
    // Reconcile the index against the current on-disk listing: parse new or
    // changed notes, drop missing ones, and report progress. This is the cold
    // build and the warm-startup sync (search.md §6.1/§6.2).
    void reconcile(const QList<ReconcileEntry> &listing);
    // The in-app save path passes the already-available text so the worker does
    // not re-read the file (search.md §6.2).
    void replaceFromText(const QString &relPath, const QString &fileText,
                         qint64 fileSize, qint64 modifiedMs);
    // The worker reads the file itself (rename, move, create, metadata write).
    void replaceFromPath(const QString &relPath, const QString &absPath);
    void removePath(const QString &relPath);

    // --- Query (thread-safe) --------------------------------------------
    // Submit a query under a monotonically increasing generation. The reply
    // arrives on queryFinished with the same generation; the caller keeps only
    // the latest (search.md §7).
    void submitQuery(quint64 generation, const SearchQuery &request);

    // The current index revision of a note, for click-time staleness checks
    // (search.md §9). Runs a short synchronous read on a dedicated connection.
    qint64 revisionOf(const QString &relPath) const;

signals:
    void usableChanged();
    void indexingChanged();
    void indexingProgress(int indexed, int total);
    void queryFinished(quint64 generation, SearchResults results);
    // A single note's rows were replaced — lets live search recompute.
    void indexUpdated();

private slots:
    void onReconcileProgress(int indexed, int total);
    void onReconcileFinished();
    void onNoteReplaced();
    void onQueryReady(quint64 generation, SearchResults results);

private:
    void setUsable(bool usable);
    void setIndexing(bool indexing);

    QThread *m_writeThread = nullptr;
    QThread *m_readThread = nullptr;
    SearchIndexWriteWorker *m_writeWorker = nullptr;
    SearchIndexReadWorker *m_readWorker = nullptr;

    QString m_rootPath;
    QString m_dbPath;
    bool m_usable = false;
    bool m_indexing = false;

    std::atomic<quint64> m_submittedGeneration{0};
};

#endif // COLLECTIONSEARCHINDEX_H
