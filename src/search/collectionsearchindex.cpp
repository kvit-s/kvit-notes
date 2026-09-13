// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "collectionsearchindex.h"
#include "blockkinddef.h"
#include "blockkinds.h"

#include "block.h"
#include "documentserializer.h"
#include "notefrontmatter.h"
#include "perflog.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QStandardPaths>
#include <QThread>

#include <limits>
#include <memory>
#include <utility>

// ======================================================================
// Worker objects. Each owns a thread-affine SearchIndexDb connection and runs
// on its own QThread; the coordinator posts work through queued invocations.
// ======================================================================

// What a worker needs to know about the root the coordinator is on now.
//
// Every unit of work is stamped with the epoch it was queued for, and the
// coordinator's current epoch is published where the workers can read it. Work
// that no longer matches is not merely ignored when it comes back — an open of
// a vault the user has already left is not started at all, which is what keeps
// a run of fast switches from making each one wait for the last one's database
// to be verified.
//
// Held by shared handle because a search thread that stopped answering is left
// running rather than taken down with the window, and it has to be able to
// read this after the coordinator is gone.
using RootEpoch = std::shared_ptr<const std::atomic<quint64>>;

// The write side: reconcile, per-note replace, and remove, all serialized on
// one thread with one write connection.
class SearchIndexWriteWorker : public QObject
{
    Q_OBJECT
public:
    explicit SearchIndexWriteWorker(RootEpoch rootEpoch)
        : m_db(QStringLiteral("write"))
        , m_rootEpoch(std::move(rootEpoch))
    {
    }

    Q_INVOKABLE void openDb(const QString &dbPath, quint64 epoch)
    {
        if (m_rootEpoch->load() != epoch) {
            // The vault this was for has already been left. Opening it now
            // would verify, and possibly rebuild, a database nobody is going
            // to ask anything.
            emit openFinished(epoch, false);
            return;
        }
        // Runs on the write thread, so everything cancelled for the previous
        // root has already unwound and the flag can be cleared here — clearing
        // it from the coordinator would revive a reconcile that was still
        // sitting in the queue.
        m_cancel.store(false);
        // The writer is the only side allowed to delete and recreate the file.
        emit openFinished(
            epoch, m_db.open(dbPath, SearchIndexDb::OpenMode::RebuildIfUnusable));
    }

    Q_INVOKABLE bool rebuildDb(const QString &dbPath)
    {
        m_cancel.store(false);
        m_db.close();
        if (dbPath != QStringLiteral(":memory:")) {
            QFile::remove(dbPath);
            QFile::remove(dbPath + QStringLiteral("-wal"));
            QFile::remove(dbPath + QStringLiteral("-shm"));
            // The close above certified the file that is being deleted; the
            // replacement inherits nothing from it.
            QFile::remove(SearchIndexDb::cleanMarkerPath(dbPath));
        }
        m_cancel.store(false);
        return m_db.open(dbPath, SearchIndexDb::OpenMode::RebuildIfUnusable);
    }

    Q_INVOKABLE void closeDb() { m_db.close(); }

    void requestCancel() { m_cancel.store(true); }

    // `epoch` stamps every unit of work with the root it was queued for, and
    // travels back out on the signals below. Both database connections are
    // long-lived and shared across roots, so a reconcile queued for the vault
    // the reader has just left completes on the worker thread after the next
    // vault's has begun; without the stamp its verdict lands on whatever is
    // open then — marking a healthy index degraded, or declaring indexing
    // finished for a build that has barely started.
    Q_INVOKABLE void reconcile(QList<ReconcileEntry> listing, quint64 epoch)
    {
        if (!m_db.isUsable()) {
            emit reconcileFinished(epoch, false);
            return;
        }
        PerfLog::ScopedTimer perf(QStringLiteral("search.index.rebuild"),
                                  QVariantMap{{QStringLiteral("notes"),
                                               listing.size()}});
        emit reconcileStarted();
        bool ok = true;

        // Drop notes that no longer exist on disk.
        QSet<QString> present;
        present.reserve(listing.size());
        for (const ReconcileEntry &e : listing)
            present.insert(e.relPath);
        const QStringList indexed = m_db.allRelPaths();
        for (const QString &relPath : indexed) {
            if (!present.contains(relPath))
                ok = m_db.removeNote(relPath) && ok;
        }

        // Parse and replace only notes whose content actually changed.
        int done = 0;
        const int total = listing.size();
        int reindexed = 0;
        int unreadable = 0;
        int skipped = 0;
        for (const ReconcileEntry &e : listing) {
            // Between notes: reading and parsing one body is the unit of
            // work, so this is the finest granularity available without
            // leaving the index half-written mid-note.
            if (m_cancel.load())
                break;
            // Three tiers, cheapest first.
            //
            // One stat. When the file's size, modification time and change
            // token are all exactly what they were when this note was
            // indexed, the file has not been written since and there is
            // nothing to learn from reading it. On a platform with no change
            // token this never fires and the pass falls through to the read
            // below, which is where every platform used to start.
            const CollectionSearchIndex::FileStamp stamp =
                CollectionSearchIndex::stampOf(e.absPath);
            if (stamp.exists
                && m_db.hasNoteStamp(e.relPath, stamp.fileSize,
                                     stamp.modifiedMs, stamp.changeToken)) {
                ++skipped;
                ++done;
                if ((done % 32) == 0 || done == total)
                    emit reconcileProgress(done, total);
                continue;
            }
            // One read and one hash. Freshness is decided on the file's
            // content, because size and modification time miss an equal-size
            // rewrite that kept the mtime — which the app itself performs, and
            // which used to leave the indexed text stale for as long as the
            // vault stayed open.
            const CollectionSearchIndex::NoteSnapshot snapshot =
                CollectionSearchIndex::readNoteSnapshot(e.absPath);
            if (!snapshot.ok) {
                ++unreadable;
                ++done;
                if ((done % 32) == 0 || done == total)
                    emit reconcileProgress(done, total);
                continue;
            }
            const QString hash =
                SearchIndexDb::contentFingerprint(snapshot.text);
            // Monotonicity: a token that has not moved past the stored one
            // says nothing about whether this file was written, so it is not
            // recorded. On a filesystem whose change time does not move on a
            // write this leaves every note on the fingerprint path forever,
            // which is the correct answer and the one the fast path must not
            // be able to talk itself out of.
            const qint64 token =
                snapshot.changeToken > m_db.changeTokenOf(e.relPath)
                    ? snapshot.changeToken
                    : 0;
            if (m_db.hasNoteFresh(e.relPath, snapshot.fileSize,
                                  snapshot.modifiedMs, hash)) {
                // Same content, so nothing is reparsed. Recording the stamp
                // costs one UPDATE and is what lets the next reconcile take
                // the stat-only path: a note saved inside the app is stored
                // with no change token, so without this it would be read on
                // every pass for the life of the index. Where there is no
                // token to record, the row is left alone unless its metadata
                // actually drifted.
                const bool worthRecording =
                    token != 0
                    || !m_db.hasNoteFresh(e.relPath, snapshot.fileSize,
                                          snapshot.modifiedMs);
                if (worthRecording) {
                    ok = m_db.touchNote(e.relPath, snapshot.fileSize,
                                        snapshot.modifiedMs, hash, token)
                        && ok;
                }
            } else {
                // The full cost: parse the note and rewrite its FTS postings.
                IndexedNote note = CollectionSearchIndex::parseNote(
                    e.relPath, snapshot.text, snapshot.fileSize,
                    snapshot.modifiedMs);
                note.changeToken = token;
                if (m_db.replaceNote(note))
                    ++reindexed;
                else
                    ok = false;
            }
            ++done;
            if ((done % 32) == 0 || done == total)
                emit reconcileProgress(done, total);
        }
        perf.addContext(QStringLiteral("reindexed"), reindexed);
        perf.addContext(QStringLiteral("unreadable"), unreadable);
        perf.addContext(QStringLiteral("unread"), skipped);
        emit reconcileFinished(epoch, ok);
    }

    Q_INVOKABLE void replaceFromText(const QString &relPath,
                                     const QString &fileText, qint64 fileSize,
                                     qint64 modifiedMs, quint64 epoch)
    {
        if (!m_db.isUsable())
            return;
        const IndexedNote note = CollectionSearchIndex::parseNote(
            relPath, fileText, fileSize, modifiedMs);
        if (m_db.replaceNote(note))
            emit noteReplaced();
        else
            emit writeFailed(epoch);
    }

    Q_INVOKABLE void replaceFromPath(const QString &relPath,
                                     const QString &absPath, quint64 epoch)
    {
        if (!m_db.isUsable())
            return;
        const CollectionSearchIndex::NoteSnapshot snapshot =
            CollectionSearchIndex::readNoteSnapshot(absPath);
        if (!snapshot.ok)
            return;
        IndexedNote note = CollectionSearchIndex::parseNote(
            relPath, snapshot.text, snapshot.fileSize, snapshot.modifiedMs);
        // Same two rules as the reconcile path: the snapshot has already
        // applied settling, and a token that has not moved past the stored one
        // is not recorded.
        note.changeToken = snapshot.changeToken > m_db.changeTokenOf(relPath)
                               ? snapshot.changeToken
                               : 0;
        if (m_db.replaceNote(note))
            emit noteReplaced();
        else
            emit writeFailed(epoch);
    }

    Q_INVOKABLE void removePath(const QString &relPath, quint64 epoch)
    {
        if (!m_db.isUsable())
            return;
        if (m_db.removeNote(relPath))
            emit noteReplaced();
        else
            emit writeFailed(epoch);
    }

signals:
    void openFinished(quint64 epoch, bool ok);
    void reconcileStarted();
    void reconcileProgress(int indexed, int total);
    void reconcileFinished(quint64 epoch, bool ok);
    void noteReplaced();
    void writeFailed(quint64 epoch);

private:
    SearchIndexDb m_db;
    RootEpoch m_rootEpoch;
    // Reconcile walks and reparses the whole vault on one thread. Without a
    // way out it runs to the end even when the vault it was reconciling has
    // been closed, holding the write connection and the thread against work
    // whose result nobody will use. Same idiom as the read worker below.
    std::atomic_bool m_cancel{false};
};

// The read side: one query at a time on one read connection, cancellable when a
// newer generation arrives.
class SearchIndexReadWorker : public QObject
{
    Q_OBJECT
public:
    explicit SearchIndexReadWorker(RootEpoch rootEpoch)
        : m_db(QStringLiteral("read"))
        , m_rootEpoch(std::move(rootEpoch))
    {
    }

    // Attach to `dbPath` whatever root the coordinator has moved on to. Only
    // the rebuild uses it: that path has just replaced this exact file and is
    // waiting for the answer, so there is no later request for a stale one to
    // lose to.
    bool openDbNow(const QString &dbPath)
    {
        // Runs on the read thread, so no query can be in flight: a fresh root
        // starts counting generations from zero again.
        m_target.store(0);
        // Read-side opens are non-destructive: the writer has already vetted
        // and, if necessary, rebuilt the file, and a second rebuild here would
        // unlink the database the writer is attached to.
        return m_db.open(dbPath, SearchIndexDb::OpenMode::RequireUsable);
    }

    Q_INVOKABLE void openDb(const QString &dbPath, quint64 epoch)
    {
        if (m_rootEpoch->load() != epoch) {
            emit openFinished(epoch, false);
            return;
        }
        emit openFinished(epoch, openDbNow(dbPath));
    }

    Q_INVOKABLE void closeDb() { m_db.close(); }

    Q_INVOKABLE qint64 revisionOf(const QString &relPath)
    {
        return m_db.revisionOf(relPath);
    }

    // Move the target forward. Cancellation is expressed only as "the target
    // has moved past you", so it is monotonic: no caller and no worker can
    // clear a cancellation another has already signalled.
    void advanceTarget(quint64 generation)
    {
        quint64 current = m_target.load();
        while (current < generation
               && !m_target.compare_exchange_weak(current, generation)) {
        }
    }

    quint64 target() const { return m_target.load(); }

    Q_INVOKABLE void runQuery(quint64 generation, SearchQuery request)
    {
        // A generation already superseded before it ran is dropped whole.
        if (generation < m_target.load())
            return;
        const GenerationCancel cancel(m_target, generation);
        const SearchResults results = m_db.query(request, &cancel);
        if (results.cancelled)
            return; // superseded mid-scan; the newer generation answers
        emit queryReady(generation, results);
    }

signals:
    void openFinished(quint64 epoch, bool ok);
    void queryReady(quint64 generation, SearchResults results);

private:
    SearchIndexDb m_db;
    RootEpoch m_rootEpoch;
    // The newest generation anyone has asked for. Work tagged with anything
    // older is obsolete; the previous shared bool let an older query reset the
    // flag a newer submission had just set, so the obsolete scan ran to the
    // end and the new one waited behind it.
    std::atomic<quint64> m_target{0};
};

namespace {

// The timeout on every call that still blocks the GUI thread. Five seconds is
// long enough that an ordinary close finishes inside it even when it is queued
// behind work that has just been cancelled — a cancelled reconcile stops at
// its next note and a cancelled query at its next row — and short enough that
// a search thread which has stopped answering costs a pause rather than the
// session.
std::atomic<int> g_workerReplyTimeoutMs{5000};

// Run `call` on `worker`'s own thread and wait for it to finish, for as long
// as workerReplyTimeoutMs() and no longer. Returns false when the worker did
// not answer in time; the caller then reports failure rather than waiting.
//
// Everything the queued call touches is owned by the call itself, because a
// timeout does not cancel it: the worker may run it minutes later, or never,
// and it must not write into a caller's stack frame that is long gone. That is
// what the shared semaphore and shared result holders below are for.
template <typename Worker, typename Call>
bool callWorker(Worker *worker, Call call)
{
    if (!worker)
        return false;
    const auto done = std::make_shared<QSemaphore>();
    QMetaObject::invokeMethod(
        worker,
        [worker, call, done]() {
            call(worker);
            done->release();
        },
        Qt::QueuedConnection);
    return done->tryAcquire(1, CollectionSearchIndex::workerReplyTimeoutMs());
}

} // namespace

// ======================================================================
// Coordinator
// ======================================================================

int CollectionSearchIndex::workerReplyTimeoutMs()
{
    return g_workerReplyTimeoutMs.load();
}

void CollectionSearchIndex::setWorkerReplyTimeoutMs(int timeoutMs)
{
    g_workerReplyTimeoutMs.store(timeoutMs);
}

CollectionSearchIndex::CollectionSearchIndex(QObject *parent)
    : QObject(parent)
    , m_rootEpoch(std::make_shared<std::atomic<quint64>>(0))
{
    qRegisterMetaType<SearchResults>("SearchResults");
    qRegisterMetaType<SearchQuery>("SearchQuery");
    qRegisterMetaType<QList<ReconcileEntry>>("QList<ReconcileEntry>");

    m_writeThread = new QThread(this);
    m_writeThread->setObjectName(QStringLiteral("kvit-search-write"));
    m_writeWorker = new SearchIndexWriteWorker(m_rootEpoch);
    m_writeWorker->moveToThread(m_writeThread);
    connect(m_writeThread, &QThread::finished, m_writeWorker,
            &QObject::deleteLater);
    connect(m_writeWorker, &SearchIndexWriteWorker::openFinished, this,
            &CollectionSearchIndex::onWriteOpened);
    connect(m_writeWorker, &SearchIndexWriteWorker::reconcileProgress, this,
            &CollectionSearchIndex::onReconcileProgress);
    connect(m_writeWorker, &SearchIndexWriteWorker::reconcileFinished, this,
            &CollectionSearchIndex::onReconcileFinished);
    connect(m_writeWorker, &SearchIndexWriteWorker::noteReplaced, this,
            &CollectionSearchIndex::onNoteReplaced);
    connect(m_writeWorker, &SearchIndexWriteWorker::writeFailed, this,
            [this](quint64 epoch) {
                // Ignore what a worker reports about a root that has already
                // been closed — the index is not degraded, it is gone — and
                // about one that has been left for another, where the report
                // is true of a database this object no longer has open.
                if (epoch == this->epoch() && m_usable)
                    setDegraded(true);
            });
    m_writeThread->start();

    m_readThread = new QThread(this);
    m_readThread->setObjectName(QStringLiteral("kvit-search-read"));
    m_readWorker = new SearchIndexReadWorker(m_rootEpoch);
    m_readWorker->moveToThread(m_readThread);
    connect(m_readThread, &QThread::finished, m_readWorker,
            &QObject::deleteLater);
    connect(m_readWorker, &SearchIndexReadWorker::openFinished, this,
            &CollectionSearchIndex::onReadOpened);
    connect(m_readWorker, &SearchIndexReadWorker::queryReady, this,
            &CollectionSearchIndex::onQueryReady);
    m_readThread->start();
}

CollectionSearchIndex::~CollectionSearchIndex()
{
    // The root is given up and the work in flight cancelled before the closes
    // are asked for, so teardown waits for one note or one row rather than for
    // a whole vault, and an open still sitting in a worker's queue is skipped
    // rather than started on the way out.
    nextEpoch();
    cancelWork();
    callWorker(m_readWorker, [](SearchIndexReadWorker *worker) {
        worker->closeDb();
    });
    callWorker(m_writeWorker, [](SearchIndexWriteWorker *worker) {
        worker->closeDb();
    });
    // A search thread that did not answer the close will not answer quit()
    // either, and waiting on it here is the hang this class exists to stop
    // having: destroying a QThread that is still running aborts the process,
    // so the thread and its worker are let go of instead and outlive the
    // session. That is a bounded leak — two threads and two SQLite
    // connections, once, in a session where the search has already stopped
    // working — and the alternative is taking the window down on the way out.
    for (QThread *thread : {m_readThread, m_writeThread}) {
        thread->quit();
        if (thread->wait(workerReplyTimeoutMs()))
            continue;
        qWarning("kvit-search: %s did not stop; leaving it running",
                 qPrintable(thread->objectName()));
        thread->setParent(nullptr);
    }
}

bool CollectionSearchIndex::capabilityAvailable()
{
    return SearchIndexDb::probeCapability();
}

QString CollectionSearchIndex::databasePathForRoot(const QString &rootPath)
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString clean = QDir::cleanPath(rootPath);
    const QByteArray key =
        QCryptographicHash::hash(clean.toUtf8(), QCryptographicHash::Sha1)
            .toHex();
    return base + QStringLiteral("/search/") + QString::fromLatin1(key)
        + QStringLiteral(".sqlite");
}

void CollectionSearchIndex::setUsable(bool usable)
{
    if (m_usable == usable)
        return;
    m_usable = usable;
    emit usableChanged();
}

void CollectionSearchIndex::setIndexing(bool indexing)
{
    if (m_indexing == indexing)
        return;
    m_indexing = indexing;
    emit indexingChanged();
}

void CollectionSearchIndex::setDegraded(bool degraded)
{
    if (m_degraded == degraded)
        return;
    m_degraded = degraded;
    emit degradedChanged();
}

quint64 CollectionSearchIndex::nextEpoch()
{
    return m_rootEpoch->fetch_add(1) + 1;
}

void CollectionSearchIndex::openForRoot(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        requestClose();
        return;
    }
    // Everything queued for the previous root belongs to the previous root,
    // whatever order it completes in.
    const quint64 openEpoch = nextEpoch();
    m_rootPath = rootPath;
    m_dbPath = databasePathForRoot(rootPath);
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());

    // Stop whatever the previous root left running, so the steps below wait
    // for one note or one row rather than for a whole vault.
    cancelWork();
    m_pendingReconciles = 0;
    setIndexing(false);
    setDegraded(false);
    // Not usable until it is: this object is answering about the previous
    // vault's database until the new one is attached, and it must not offer
    // that vault's notes as this one's.
    setUsable(false);

    // Both connections are told what to do now; the read connection's own open
    // follows when the writer reports back. Nothing here waits for any of it.
    //
    //   The write connection takes the new file, and takes it ahead of
    //   everything else this object will post to that thread. That ordering is
    //   what lets the writes queued during an open — the reconcile the caller
    //   issues in the same breath, a note saved a moment later — land on the
    //   new vault's database instead of being dropped for arriving before it
    //   was ready.
    //
    //   The read connection lets go of the vault being left. It is the one
    //   that answers queries, so a reader left attached answers about the
    //   wrong vault.
    //
    //   The read connection reattaches in onWriteOpened(), once the writer has
    //   vetted and if necessary rebuilt the file. The writer owns that, and a
    //   reader that opened first could race an empty database into a
    //   destructive rebuild.
    //
    // Each worker delivers what it is given in the order it was given, so a
    // close still running when this arrives finishes before the open behind it
    // starts. That is the whole of the ordering between the vault being left
    // and the one being taken, and none of it is on the caller's thread.
    QMetaObject::invokeMethod(m_writeWorker, "openDb", Qt::QueuedConnection,
                              Q_ARG(QString, m_dbPath),
                              Q_ARG(quint64, openEpoch));
    QMetaObject::invokeMethod(m_readWorker, "closeDb", Qt::QueuedConnection);
}

void CollectionSearchIndex::onWriteOpened(quint64 openEpoch, bool ok)
{
    if (openEpoch != epoch() || !serving())
        return;
    if (!ok) {
        failOpen();
        return;
    }
    QMetaObject::invokeMethod(m_readWorker, "openDb", Qt::QueuedConnection,
                              Q_ARG(QString, m_dbPath),
                              Q_ARG(quint64, openEpoch));
}

void CollectionSearchIndex::onReadOpened(quint64 openEpoch, bool ok)
{
    if (openEpoch != epoch() || !serving())
        return;
    if (!ok) {
        failOpen();
        return;
    }
    setUsable(true);
    emit openFinished(m_rootPath, true);
}

void CollectionSearchIndex::failOpen()
{
    const QString rootPath = m_rootPath;
    // An index that could not be opened is not an index. Forgetting the root
    // is what makes the next sync ask for it again: recording it as served
    // told every later sync this vault was already taken care of, and search
    // stayed dead for as long as the vault stayed open. The write connection
    // may have attached before the read connection failed, so it is let go of
    // too.
    QMetaObject::invokeMethod(m_writeWorker, "closeDb", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_readWorker, "closeDb", Qt::QueuedConnection);
    forgetRoot();
    emit openFinished(rootPath, false);
}

void CollectionSearchIndex::closeIndex()
{
    // The blocking teardown, for a caller that has to know the connections are
    // gone before it goes on — a test, or a vault whose cache directory is
    // about to be removed.
    //
    // The root is given up before anything is asked of the workers, so a
    // verdict arriving from the work cancelled below belongs to an epoch this
    // object has already left. Cancelling matters because the flags are plain
    // atomics, safe to set from here, and a reconcile or query that stops at
    // its next check turns a wait for the whole vault into a wait for one
    // note.
    //
    // The waits are bounded, and the reader goes first so the writer's close
    // is never the one competing with a query. A worker that does not answer
    // inside the bound is reported and left behind; the index is closed as far
    // as every caller of this object is concerned either way.
    forgetRoot();
    cancelWork();
    const bool readClosed =
        callWorker(m_readWorker,
                   [](SearchIndexReadWorker *worker) { worker->closeDb(); });
    const bool writeClosed =
        callWorker(m_writeWorker,
                   [](SearchIndexWriteWorker *worker) { worker->closeDb(); });
    if (!readClosed || !writeClosed) {
        qWarning("kvit-search: the index did not close inside %d ms; the "
                 "search thread that owes an answer is left to finish on its "
                 "own", workerReplyTimeoutMs());
    }
}

void CollectionSearchIndex::requestClose()
{
    // The non-blocking teardown, and the one a vault switch uses. The root is
    // given up here and now, both workers are told to abandon what they are
    // doing, and the two closes are posted rather than waited on, so a caller
    // switching vaults never waits behind a full-vault reconcile or a query
    // over a large index. Work queued after these closes — the next root's
    // opens — is delivered in order behind them, so reopening immediately is
    // safe.
    forgetRoot();
    cancelWork();
    QMetaObject::invokeMethod(m_readWorker, "closeDb", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_writeWorker, "closeDb", Qt::QueuedConnection);
}

void CollectionSearchIndex::parkWorkersForTesting(QSemaphore *gate)
{
    const auto park = [gate]() { gate->acquire(); };
    QMetaObject::invokeMethod(m_writeWorker, park, Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_readWorker, park, Qt::QueuedConnection);
}

void CollectionSearchIndex::cancelWork()
{
    if (m_writeWorker)
        m_writeWorker->requestCancel();
    if (m_readWorker) {
        // Past every generation any caller can hold, so a query already
        // running stops at its next row.
        m_readWorker->advanceTarget(std::numeric_limits<quint64>::max());
    }
}

void CollectionSearchIndex::forgetRoot()
{
    nextEpoch();
    m_rootPath.clear();
    m_dbPath.clear();
    m_pendingReconciles = 0;
    setIndexing(false);
    setDegraded(false);
    setUsable(false);
}

bool CollectionSearchIndex::rebuildIndex()
{
    if (m_rootPath.isEmpty() || m_dbPath.isEmpty())
        return false;
    const QString dbPath = m_dbPath;
    // The database about to be deleted is the one every queued job was
    // stamped for, so their verdicts are about a file that will not exist.
    nextEpoch();
    cancelWork();
    // Each step waits for the one before it, because this one really is a
    // sequence: the reader detaches first so the writer's unlink cannot leave
    // it on a deleted inode, and reattaches only after the writer has recreated
    // the file. The waits are bounded, so the recovery action reports failure
    // rather than taking the window down with it — which is the whole
    // difference from the switch path, where nothing waits at all.
    const auto shared = std::make_shared<std::atomic_bool>(false);
    bool ok = callWorker(m_readWorker,
                         [](SearchIndexReadWorker *worker) {
                             worker->closeDb();
                         });
    if (ok) {
        ok = callWorker(m_writeWorker,
                        [dbPath, shared](SearchIndexWriteWorker *worker) {
                            shared->store(worker->rebuildDb(dbPath));
                        })
            && shared->load();
    }
    if (ok) {
        shared->store(false);
        ok = callWorker(m_readWorker,
                        [dbPath, shared](SearchIndexReadWorker *worker) {
                            // Straight onto the file the rebuild just made,
                            // rather than through the epoch-stamped openDb:
                            // the epoch moved when this rebuild started, and
                            // the point of a rebuild is that this database is
                            // the one wanted now.
                            shared->store(worker->openDbNow(dbPath));
                        })
            && shared->load();
    }
    m_pendingReconciles = 0;
    setIndexing(false);
    setUsable(ok);
    setDegraded(!ok);
    return ok;
}

// ----------------------------------------------------------------------
// The change token, and what each platform's version of it is worth.
//
// The reconcile pass wants to answer "has this file changed since I indexed
// it?" without reading it. Size and modification time cannot answer it: both
// are writable from userspace, and this application itself rewrites a note to
// the same length and restores its timestamp when a tag rename does not change
// the byte count. That is exactly how stale text used to stay indexed.
//
// POSIX (Linux, macOS, the BSDs) has a third value that does answer it. The
// status-change time, st_ctime, is set by the kernel on every write and on
// every metadata change including a utimes() call, and there is no system call
// that sets it to a chosen value — not even for root. So if a file's size,
// modification time and status-change time are all exactly what they were when
// the note was indexed, the file has not been written since. Qt exposes it as
// QFileInfo::metadataChangeTime().
//
// Windows has no equivalent that Qt can reach. QFileInfo::metadataChangeTime()
// there is filled from ftLastWriteTime (Qt 6.10,
// qfilesystemmetadata_p.h: `changeTime_ = lastWriteTime_ = ...` in both
// fillFromFindData() and fillFromFindInfo()), so it is a second copy of the
// modification time and carries none of the guarantee above; older Qt filled
// it from the creation time, which does not move on writes at all. NTFS does
// maintain a real change time, reachable through GetFileInformationByHandleEx
// with FileBasicInfo, and SetFileTime cannot forge it — but reading it means a
// direct Win32 call, which nothing else in this module makes, and behaviour
// that cannot be verified from the machine this was written on. So Windows
// gets no token, and every reconcile there reads and hashes every note, which
// is what all three platforms did before.
//
// A timestamp is only as good as its resolution, and that is where the first
// version of this went wrong: two writes 40 microseconds apart carry the same
// status-change time once it is truncated to a millisecond, so an equal-size
// rewrite that restored the modification time was called unchanged and its new
// text never reached the index. Measured on ext4 under Linux 6.18, the kernel
// separates consecutive writes by 40 to 130 microseconds, and Qt reports the
// pair as identical. Two rules close it, and neither trusts the filesystem to
// behave:
//
//   Settling — a token is recorded only once the file has been quiet for
//     changeTokenSettleMs(). Any write after that lands in a later granule and
//     produces a strictly greater token. See settledChangeToken().
//   Monotonicity — a token is recorded only when it is strictly greater than
//     the one already stored for that note. A filesystem whose change time
//     does not move on a write (FAT and exFAT report the creation time) then
//     never gets a token recorded at all, and every reconcile decides those
//     notes on their fingerprint. This is a check rather than an assumption:
//     nothing here needs to know which filesystem it is on.
//
// The guarantee, stated per platform:
//   Linux, macOS, other Unix — a matching (size, mtime, ctime) tuple means the
//     file has not been written since it was indexed, on any filesystem whose
//     timestamp granularity is a second or finer, and on coarser ones the
//     token is simply never recorded and the fingerprint decides.
//   Windows — no token, no fast path, freshness decided by reading the file
//     and comparing its fingerprint, exactly as before.
// Either way the stored fingerprint stays the authority: the tuple only ever
// decides whether computing the fingerprint can be skipped.
// ----------------------------------------------------------------------

bool CollectionSearchIndex::changeTokenIsTrustworthy()
{
#if defined(Q_OS_UNIX)
    return true;
#else
    return false;
#endif
}

qint64 CollectionSearchIndex::changeTokenSettleMs()
{
    return 1000;
}

qint64 CollectionSearchIndex::settledChangeToken(const FileStamp &stamp,
                                                 qint64 nowMs)
{
    if (!stamp.exists || stamp.changeToken == 0)
        return 0;
    // A token exactly `settle` old is still refused: the comparison has to
    // leave no room at all for a later write to land in the same granule. A
    // clock that has stepped backwards makes the difference negative, and then
    // nothing is recorded, which is the safe direction.
    if (nowMs - stamp.changeToken <= changeTokenSettleMs())
        return 0;
    return stamp.changeToken;
}

CollectionSearchIndex::FileStamp
CollectionSearchIndex::stampOf(const QString &absPath)
{
    FileStamp stamp;
    const QFileInfo info(absPath);
    if (!info.exists() || !info.isFile())
        return stamp;
    stamp.exists = true;
    stamp.fileSize = info.size();
    stamp.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    if (changeTokenIsTrustworthy()) {
        const QDateTime changed = info.metadataChangeTime();
        // 0 doubles as "no token", so a genuine epoch-zero timestamp is
        // reported as no token and costs a read rather than a wrong answer.
        stamp.changeToken =
            changed.isValid() ? changed.toMSecsSinceEpoch() : 0;
    }
    return stamp;
}

CollectionSearchIndex::NoteSnapshot
CollectionSearchIndex::readNoteSnapshot(const QString &absPath, int maxAttempts)
{
    // Metadata is taken before and after the read and the read is repeated
    // while the two disagree, so the text, the size, and the timestamp stored
    // for a note always describe the same revision of the file. Reading first
    // and stating afterwards stores the old text under the new file's
    // metadata, and because that metadata then looks current, nothing ever
    // reads the note again.
    NoteSnapshot snapshot;
    for (int attempt = 0; attempt < qMax(1, maxAttempts); ++attempt) {
        const FileStamp before = stampOf(absPath);
        if (!before.exists)
            return NoteSnapshot();

        // Binary, so the bytes read can be compared against the size the two
        // stats report. Timestamps are only accurate to the millisecond, and
        // two rewrites inside one millisecond can leave both stats agreeing
        // over a read that caught the file half-written; the byte count is
        // what actually rules that out.
        QFile file(absPath);
        if (!file.open(QIODevice::ReadOnly))
            return NoteSnapshot();
        SearchIndexOps::recordFileRead();
        const QByteArray bytes = file.readAll();
        if (file.error() != QFileDevice::NoError)
            return NoteSnapshot();
        file.close();

        const FileStamp after = stampOf(absPath);
        if (!after.exists)
            return NoteSnapshot();
        // The change token is compared here too, and that is what makes it
        // safe to store: the token this snapshot reports describes the same
        // revision of the file as its text. Taking it from a separate stat
        // afterwards could pair the old text with a newer token, and the
        // reconcile would then skip the file forever.
        if (bytes.size() != before.fileSize || after.fileSize != before.fileSize
            || after.modifiedMs != before.modifiedMs
            || after.changeToken != before.changeToken) {
            continue; // the file moved under the read; take it again
        }

        // What QIODevice::Text used to do, done the same way everywhere
        // instead of only on Windows.
        QString text = QString::fromUtf8(bytes);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        snapshot.text = text;
        snapshot.fileSize = before.fileSize;
        snapshot.modifiedMs = before.modifiedMs;
        // The settling rule is applied here, at the one point where a token
        // becomes something a caller might store, and the clock is read after
        // the file has been proven quiet across the whole read. A token still
        // inside its granule is reported as 0 — do not record one — and the
        // note is decided on its fingerprint until it has been quiet long
        // enough for a token to mean something.
        snapshot.changeToken =
            settledChangeToken(before, QDateTime::currentMSecsSinceEpoch());
        snapshot.ok = true;
        return snapshot;
    }
    // Still changing after every attempt. Skipping it now is safe: the note
    // is left with whatever the index already held, and the next reconcile —
    // which compares content, not metadata — picks it up.
    return NoteSnapshot();
}

IndexedNote CollectionSearchIndex::parseNote(const QString &relPath,
                                             const QString &fileText,
                                             qint64 fileSize, qint64 modifiedMs)
{
    IndexedNote note;
    note.relPath = relPath;
    const int slash = relPath.lastIndexOf(QLatin1Char('/'));
    // A non-null empty string for root notes: a null QString binds as SQL NULL
    // and the folder column is NOT NULL.
    note.folder = slash < 0 ? QString::fromLatin1("") : relPath.left(slash);
    const QString name = slash < 0 ? relPath : relPath.mid(slash + 1);
    static const QString mdSuffix = QStringLiteral(".md");
    note.title = name.endsWith(mdSuffix, Qt::CaseInsensitive)
                     ? name.left(name.size() - mdSuffix.size())
                     : name;
    note.fileSize = fileSize;
    note.modifiedMs = modifiedMs;
    note.contentHash = SearchIndexDb::contentFingerprint(fileText);

    const NoteFrontMatter::Split split = NoteFrontMatter::split(fileText);
    note.tags = NoteFrontMatter::parse(split.block).tags;

    // The same block split and searchable-text rule the editor uses, so a
    // search matches what the editor shows. Both questions are the block
    // kind's to answer: whether its content is verbatim (a code fence's text
    // IS its markdown, so a match's offsets need no mapping) and what text a
    // match is found in (a divider has none).
    DocumentSerializer serializer;
    const QList<DocumentSerializer::BlockData> blocks =
        serializer.parse(split.body);
    int blockIndex = 0;
    for (const DocumentSerializer::BlockData &block : blocks) {
        IndexedBlock indexed;
        indexed.blockIndex = blockIndex++;
        const BlockKindDef *kind = BlockKindDefs::forState(block);
        indexed.verbatim = kind->isVerbatim();
        indexed.displayText = kind->searchText(block);
        note.blocks.append(indexed);
    }
    return note;
}

void CollectionSearchIndex::reconcile(const QList<ReconcileEntry> &listing)
{
    // Queued against the root being served, which is not the same as against
    // a database that is already attached: the open runs on these same worker
    // threads and delivers before anything posted after it, so work handed
    // over while a vault is still opening lands on that vault's database
    // rather than being dropped for arriving a few milliseconds early.
    if (!serving())
        return;
    // Indexing becomes true here, where the work is enqueued, not later when
    // the worker gets around to announcing it. A caller that queued a
    // reconcile and then asked whether the index was busy used to be told no,
    // and a test waiting for the index to settle could sail through the gap
    // before the job had started.
    ++m_pendingReconciles;
    setIndexing(true);
    QMetaObject::invokeMethod(m_writeWorker, "reconcile", Qt::QueuedConnection,
                              Q_ARG(QList<ReconcileEntry>, listing),
                              Q_ARG(quint64, epoch()));
}

void CollectionSearchIndex::replaceFromText(const QString &relPath,
                                            const QString &fileText,
                                            qint64 fileSize, qint64 modifiedMs)
{
    if (!serving())
        return;
    QMetaObject::invokeMethod(m_writeWorker, "replaceFromText",
                              Qt::QueuedConnection, Q_ARG(QString, relPath),
                              Q_ARG(QString, fileText), Q_ARG(qint64, fileSize),
                              Q_ARG(qint64, modifiedMs),
                              Q_ARG(quint64, epoch()));
}

void CollectionSearchIndex::replaceFromPath(const QString &relPath,
                                            const QString &absPath)
{
    if (!serving())
        return;
    QMetaObject::invokeMethod(m_writeWorker, "replaceFromPath",
                              Qt::QueuedConnection, Q_ARG(QString, relPath),
                              Q_ARG(QString, absPath),
                              Q_ARG(quint64, epoch()));
}

void CollectionSearchIndex::removePath(const QString &relPath)
{
    if (!serving())
        return;
    QMetaObject::invokeMethod(m_writeWorker, "removePath", Qt::QueuedConnection,
                              Q_ARG(QString, relPath),
                              Q_ARG(quint64, epoch()));
}

void CollectionSearchIndex::submitQuery(quint64 generation,
                                        const SearchQuery &request)
{
    if (!serving()) {
        // No index to ask. The reply carries ok=false so the caller can tell
        // "there is nothing to search" from "nothing matched".
        SearchResults empty;
        empty.ok = false;
        emit queryFinished(generation, empty);
        return;
    }
    m_submittedGeneration.store(generation);
    // Moving the target is the whole cancellation mechanism: everything older
    // is obsolete by definition, and this generation cannot be un-cancelled by
    // an older one arriving late.
    m_readWorker->advanceTarget(generation);
    QMetaObject::invokeMethod(m_readWorker, "runQuery", Qt::QueuedConnection,
                              Q_ARG(quint64, generation),
                              Q_ARG(SearchQuery, request));
}

void CollectionSearchIndex::cancelQueries(quint64 generation)
{
    if (!m_readWorker)
        return;
    m_submittedGeneration.store(generation);
    m_readWorker->advanceTarget(generation);
}

qint64 CollectionSearchIndex::revisionOf(const QString &relPath) const
{
    // Runs on the read worker's connection so it never opens a competing
    // connection (which could trip the destructive rebuild-on-open path).
    if (!m_usable || !m_readWorker)
        return 0;
    const auto revision = std::make_shared<std::atomic<qint64>>(0);
    // Bounded, like everything else here that waits on a search thread: a
    // read worker that never answers costs this lookup rather than the caller.
    // 0 is what an unknown note answers too, and a staleness check that cannot
    // be made is the same answer as a note the index has never seen.
    callWorker(m_readWorker, [relPath, revision](SearchIndexReadWorker *worker) {
        revision->store(worker->revisionOf(relPath));
    });
    return revision->load();
}

void CollectionSearchIndex::onReconcileProgress(int indexed, int total)
{
    emit indexingProgress(indexed, total);
}

void CollectionSearchIndex::onReconcileFinished(quint64 workEpoch, bool ok)
{
    // A verdict about a root this object has left, or about the database a
    // rebuild has since replaced, says nothing about the one open now. It also
    // must not touch the pending count: that belongs to the current root, and
    // decrementing it here published a half-built index as complete.
    if (workEpoch != epoch())
        return;
    if (!ok && m_usable)
        setDegraded(true);
    // Only the last outstanding job clears the flag. Two queued reconciles
    // otherwise emit finish/start pairs that make the index look idle in
    // between, which is a moment where a query can be published as a complete
    // answer against a half-built index.
    if (m_pendingReconciles > 0)
        --m_pendingReconciles;
    if (m_pendingReconciles == 0)
        setIndexing(false);
    emit indexUpdated();
}

void CollectionSearchIndex::onNoteReplaced()
{
    emit indexUpdated();
}

void CollectionSearchIndex::onQueryReady(quint64 generation,
                                         SearchResults results)
{
    // A query the engine could not run says something about the index, not
    // about the query: the answer would otherwise arrive as an ordinary empty
    // result set and replace valid results on screen.
    if (!results.ok && m_usable)
        setDegraded(true);
    // The coordinator forwards every completed generation; the facade keeps
    // only the latest.
    emit queryFinished(generation, results);
}

#include "collectionsearchindex.moc"
