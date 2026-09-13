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

class QSemaphore;
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
    // Open (creating) the cache database for `rootPath` and ready both
    // connections. An empty root tears the index down instead.
    //
    // RETURNS IMMEDIATELY. The work runs on the two search threads and the
    // outcome arrives on openFinished(), and on usableChanged() when it
    // succeeded. It used to be two blocking calls onto those threads, and that
    // is how a search thread that stopped answering took the window with it:
    // the GUI thread waited on a worker that was waiting on the other worker,
    // and nothing timed out. A stuck index now costs the search rather than
    // the session.
    //
    // Ordering is the other half of it. The close of the vault being left, the
    // open of the next one and every write in between are posted to the same
    // two worker threads, which deliver them in the order they were posted, so
    // a switch that arrives while the previous vault is still closing waits
    // behind that close on the search side — not on the caller's.
    void openForRoot(const QString &rootPath);

    // The root the index is serving or is in the middle of opening. Empty when
    // it is closed and when the last open failed, so a caller can tell "this
    // vault is already being taken care of" from "ask again".
    QString openRoot() const { return m_rootPath; }

    // Tear the index down, waiting for both workers to become idle first.
    // BLOCKS THE CALLER for as long as the work in flight takes to notice the
    // cancellation, and gives up after workerReplyTimeoutMs(). Prefer
    // requestClose() on the GUI thread.
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
    // no root is open, the rebuild failed, or a search thread did not answer
    // inside workerReplyTimeoutMs(). BLOCKS THE CALLER, boundedly.
    bool rebuildIndex();

    // How long anything on this class that blocks the calling thread waits for
    // a search thread before deciding it is not coming back. The calls that
    // still wait are the explicit ones — the rebuild, the blocking close, the
    // revision lookup and teardown — and each of them reports failure instead
    // of hanging. Nothing on the vault-switch path waits at all.
    //
    // The setter exists for the suite that wedges a worker on purpose, which
    // would otherwise spend the whole production bound on every call it makes.
    static int workerReplyTimeoutMs();
    static void setWorkerReplyTimeoutMs(int timeoutMs);

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

    // How long a file must have been quiet before its change token may be
    // recorded, in milliseconds.
    //
    // A change token is a timestamp, and every timestamp is truncated to some
    // granularity — the filesystem's, and then Qt's millisecond. A write that
    // lands inside the same granule as the write we recorded produces the same
    // token, and a reconcile comparing the two sees a file that has not
    // changed. That is not hypothetical: on ext4 under Linux 6.18 the kernel
    // moves the status-change time by 40 to 130 microseconds between two
    // consecutive writes, which Qt reports as no change at all.
    //
    // Waiting closes it. If a file's token reads `C` and the wall clock
    // already reads `C + settle` when we record it, then every later write
    // happens at a time past `C + settle`, and its token — truncated to any
    // granularity up to `settle` — is strictly greater than `C`. One second
    // covers the coarsest granularity in practice: a second is what NFS, ext3
    // and HFS+ record, and the kernel's own coarse clock is finer than that by
    // three orders of magnitude.
    static qint64 changeTokenSettleMs();

    // The change token from `stamp` if it may be recorded, or 0 if it may not.
    // Pure, so the rule above can be checked without a filesystem. `nowMs` is
    // the wall clock at the moment of recording.
    static qint64 settledChangeToken(const FileStamp &stamp, qint64 nowMs);

    // One note's text together with the metadata that describes *that* text.
    struct NoteSnapshot {
        QString text;
        qint64 fileSize = 0;
        qint64 modifiedMs = 0;
        // Already passed through settledChangeToken(), so this is a token the
        // caller may store: 0 means "do not record one", never "the file has
        // no change time".
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
    // BLOCKS THE CALLER, up to workerReplyTimeoutMs(). The read itself is
    // short, but it queues onto the read worker's thread, so it also waits for
    // whatever that worker is already doing — a full-text query over a large
    // vault, for instance. Calling it from the GUI thread ties the interface to
    // query latency. A worker that does not answer inside the bound gives 0,
    // which is also what an unknown note gives: a staleness check that could
    // not be made and a note the index has never seen are the same answer.
    //
    // Nothing calls this today (verified across src/, qml/ and tests/), which
    // is why it has not been made asynchronous: there is no caller whose
    // behaviour would tell us what the right non-blocking shape is. Give it
    // one and it should return a future or take a callback rather than block.
    qint64 revisionOf(const QString &relPath) const;

    // --- Test seam ------------------------------------------------------
    // Park both search threads inside a queued call until `gate` is released
    // once per thread. A search thread that stops answering is the one failure
    // this class has to survive and the one it cannot produce on demand, and
    // what has to hold while it is parked is that every call the GUI thread
    // makes still returns. The gate must outlive the threads: a parked worker
    // is still holding it when a teardown gives up on it.
    void parkWorkersForTesting(QSemaphore *gate);

signals:
    void usableChanged();
    // The asynchronous half of openForRoot(): `ok` is false when the database
    // could not be opened, and openRoot() is empty again by the time it
    // arrives. A result for a root that has already been left is never
    // emitted.
    void openFinished(const QString &rootPath, bool ok);
    void indexingChanged();
    void degradedChanged();
    void indexingProgress(int indexed, int total);
    void queryFinished(quint64 generation, SearchResults results);
    // A single note's rows were replaced — lets live search recompute.
    void indexUpdated();

private slots:
    void onReconcileProgress(int indexed, int total);
    void onReconcileFinished(quint64 epoch, bool ok);
    void onNoteReplaced();
    void onQueryReady(quint64 generation, SearchResults results);
    // The two halves of an open: the read connection is told to reattach once
    // the write connection reports the file is good. See openForRoot().
    void onWriteOpened(quint64 epoch, bool ok);
    void onReadOpened(quint64 epoch, bool ok);

private:
    void setUsable(bool usable);
    void setIndexing(bool indexing);
    void setDegraded(bool degraded);
    void cancelWork();
    void forgetRoot();
    // Give up on an open that could not be completed: the root is forgotten,
    // so the next sync asks for it again rather than assuming it is served.
    void failOpen();
    // True while a root is being served or taken — which is what decides
    // whether work may be queued, since work posted during an open is
    // delivered behind it rather than dropped.
    bool serving() const { return !m_rootPath.isEmpty(); }
    // Bump the epoch that stamps every unit of work, publishing it where the
    // workers can see it.
    quint64 nextEpoch();
    quint64 epoch() const { return m_rootEpoch->load(); }

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
    // Which root the work now in flight belongs to. Both database connections
    // outlive any one vault, so a reconcile or a write queued for one root
    // completes on its worker thread whenever it gets there — sometimes after
    // the reader has opened a different vault, or after a rebuild has replaced
    // the database underneath it. Every unit of work takes this number out
    // and back, and a completion whose number no longer matches is discarded
    // rather than allowed to set the degraded flag or clear the indexing one
    // for a database it never touched.
    //
    // Shared with the workers, and by a handle that outlives this object: a
    // teardown that gives up on a wedged search thread leaves that thread
    // running, and it must still be able to see that its work is obsolete.
    // The workers only ever read it.
    std::shared_ptr<std::atomic<quint64>> m_rootEpoch;

    std::atomic<quint64> m_submittedGeneration{0};
};

#endif // COLLECTIONSEARCHINDEX_H
