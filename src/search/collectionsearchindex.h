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
// index copy is stale without reading the body.
struct ReconcileEntry {
    QString relPath;
    QString absPath;
    qint64 fileSize = 0;
    qint64 modifiedMs = 0;
};

// The GUI-free coordinator for disk-backed global search.
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
    // back to the legacy scanner in development when this is false.
    static bool capabilityAvailable();

    bool isUsable() const { return m_usable; }
    // True from the moment a reconcile is queued until the last queued
    // reconcile has finished — never momentarily false in between.
    bool isIndexing() const { return m_indexing; }
    // True when a database operation failed: a mutation that did not land, a
    // reconcile that could not complete, or a query the engine could not
    // answer. The index is then out of step with the notes on disk in a way it
    // cannot repair by itself, and its answers are not trustworthy until
    // rebuildIndex() succeeds.
    bool isDegraded() const { return m_degraded; }

    // --- Lifecycle (GUI thread) -----------------------------------------
    // Opens (creating) the cache database for `rootPath` and readies both
    // connections. A closed or empty root tears the index down.
    void openForRoot(const QString &rootPath);

    // Tear the index down, waiting for both workers to become idle first.
    // BLOCKS THE CALLER for as long as the work in flight takes to notice the
    // cancellation. Prefer requestClose() on the GUI thread.
    void closeIndex();

    // Tear the index down without waiting. In-flight reconcile and query work
    // is cancelled, isUsable() and isIndexing() are false when this returns,
    // and the two database connections close on their own threads. A later
    // openForRoot() is ordered behind those closes, so switching roots
    // immediately afterwards is safe.
    void requestClose();

    // Delete and recreate the database for the current root, then reattach
    // both connections. This is the recovery step for isDegraded(): it throws
    // away an index that cannot be trusted and leaves an empty one, so the
    // caller must follow it with reconcile() to refill it. Returns false when
    // no root is open or the rebuild failed. BLOCKS THE CALLER.
    bool rebuildIndex();

    // Absolute path of the cache database for a notes root — exposed for tests
    // and diagnostics.
    static QString databasePathForRoot(const QString &rootPath);

    // Parse one note's file text into the indexable form: title as the kind-0
    // block, each body block's display text, the code-block verbatim flag, and
    // the front-matter tags. Static and pure so the differential oracle can
    // build the same rows without a database.
    static IndexedNote parseNote(const QString &relPath, const QString &fileText,
                                 qint64 fileSize, qint64 modifiedMs);

    // What one stat says about a note file: the whole basis of the reconcile
    // fast path.
    struct FileStamp {
        qint64 fileSize = 0;
        qint64 modifiedMs = 0;
        // A change token — a value the kernel moves on every write and that
        // userspace cannot move back. 0 means this platform offers no such
        // value, and then the tuple proves nothing and the file must be read.
        qint64 changeToken = 0;
        bool exists = false;
    };

    // Stat one note file. See the implementation for what the change token is
    // on each platform and what it is worth there; on a platform without one
    // this still returns size and modification time, with changeToken 0.
    static FileStamp stampOf(const QString &absPath);

    // Whether this platform supplies a change token that a reconcile may act
    // on. False means every reconcile reads and hashes every note, which is
    // correct but costs one read per note.
    static bool changeTokenIsTrustworthy();

    // One note's text together with the metadata that describes *that* text.
    struct NoteSnapshot {
        QString text;
        qint64 fileSize = 0;
        qint64 modifiedMs = 0;
        qint64 changeToken = 0;
        bool ok = false;
    };

    // Read a note's text and metadata as one consistent snapshot: the file is
    // stated before and after the read and re-read while the two disagree, so
    // a rewrite that lands mid-read cannot store the old text under the new
    // file's stamp. `ok` is false when the file is unreadable, or
    // still changing after `maxAttempts` tries; the caller then leaves the
    // index alone rather than recording a mixture.
    static NoteSnapshot readNoteSnapshot(const QString &absPath,
                                         int maxAttempts = 4);

    // --- Content feed (thread-safe) -------------------------------------
    // Reconcile the index against the current on-disk listing: parse new or
    // changed notes, drop missing ones, and report progress. This is the cold
    // build and the warm-startup sync.
    void reconcile(const QList<ReconcileEntry> &listing);
    // The in-app save path passes the already-available text so the worker does
    // not re-read the file.
    void replaceFromText(const QString &relPath, const QString &fileText,
                         qint64 fileSize, qint64 modifiedMs);
    // The worker reads the file itself (rename, move, create, metadata write).
    void replaceFromPath(const QString &relPath, const QString &absPath);
    void removePath(const QString &relPath);

    // --- Query (thread-safe) --------------------------------------------
    // Submit a query under a monotonically increasing generation. The reply
    // arrives on queryFinished with the same generation; the caller keeps only
    // the latest.
    void submitQuery(quint64 generation, const SearchQuery &request);

    // Abandon outstanding query work without submitting a replacement —
    // what clearing the search box needs. A running query stops at its next
    // row, anything still queued below `generation` is dropped unread, and
    // neither produces a reply. Replies emitted before the cancellation
    // reached the worker can still arrive, so the caller must also reject them
    // by generation.
    void cancelQueries(quint64 generation);

    // The current index revision of a note, for click-time staleness checks.
    //
    // BLOCKS THE CALLER. The read is short, but it is a
    // BlockingQueuedConnection onto the read worker's thread, so it also waits
    // for whatever that worker is already doing — a full-text query over a
    // large vault, for instance. Calling it from the GUI thread ties the
    // interface to query latency.
    //
    // Nothing calls this today (verified across src/, qml/ and tests/), which
    // is why it has not been made asynchronous: there is no caller whose
    // behaviour would tell us what the right non-blocking shape is. Give it
    // one and it should return a future or take a callback rather than block.
    qint64 revisionOf(const QString &relPath) const;

signals:
    void usableChanged();
    void indexingChanged();
    void degradedChanged();
    void indexingProgress(int indexed, int total);
    void queryFinished(quint64 generation, SearchResults results);
    // A single note's rows were replaced — lets live search recompute.
    void indexUpdated();

private slots:
    void onReconcileProgress(int indexed, int total);
    void onReconcileFinished(bool ok);
    void onNoteReplaced();
    void onQueryReady(quint64 generation, SearchResults results);

private:
    void setUsable(bool usable);
    void setIndexing(bool indexing);
    void setDegraded(bool degraded);
    void cancelWork();
    void forgetRoot();

    QThread *m_writeThread = nullptr;
    QThread *m_readThread = nullptr;
    SearchIndexWriteWorker *m_writeWorker = nullptr;
    SearchIndexReadWorker *m_readWorker = nullptr;

    QString m_rootPath;
    QString m_dbPath;
    bool m_usable = false;
    bool m_indexing = false;
    bool m_degraded = false;
    // Reconciles queued but not yet finished. Counted here, on the coordinator
    // thread, so the indexing flag is a fact about the queue rather than a
    // lagging echo of the worker.
    int m_pendingReconciles = 0;

    std::atomic<quint64> m_submittedGeneration{0};
};

#endif // COLLECTIONSEARCHINDEX_H
