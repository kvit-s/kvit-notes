// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "collectionsearchindex.h"

#include "block.h"
#include "documentserializer.h"
#include "notefrontmatter.h"
#include "perflog.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>

#include <limits>

// ======================================================================
// Worker objects. Each owns a thread-affine SearchIndexDb connection and runs
// on its own QThread; the coordinator posts work through queued invocations.
// ======================================================================

// The write side: reconcile, per-note replace, and remove, all serialized on
// one thread with one write connection.
class SearchIndexWriteWorker : public QObject
{
    Q_OBJECT
public:
    SearchIndexWriteWorker()
        : m_db(QStringLiteral("write"))
    {
    }

    Q_INVOKABLE bool openDb(const QString &dbPath)
    {
        // Runs on the write thread, so everything cancelled for the previous
        // root has already unwound and the flag can be cleared here — clearing
        // it from the coordinator would revive a reconcile that was still
        // sitting in the queue.
        m_cancel.store(false);
        // The writer is the only side allowed to delete and recreate the file.
        return m_db.open(dbPath, SearchIndexDb::OpenMode::RebuildIfUnusable);
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
        return openDb(dbPath);
    }

    Q_INVOKABLE void closeDb() { m_db.close(); }

    void requestCancel() { m_cancel.store(true); }

    Q_INVOKABLE void reconcile(QList<ReconcileEntry> listing)
    {
        if (!m_db.isUsable()) {
            emit reconcileFinished(false);
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
                    snapshot.changeToken != 0
                    || !m_db.hasNoteFresh(e.relPath, snapshot.fileSize,
                                          snapshot.modifiedMs);
                if (worthRecording) {
                    ok = m_db.touchNote(e.relPath, snapshot.fileSize,
                                        snapshot.modifiedMs, hash,
                                        snapshot.changeToken)
                        && ok;
                }
            } else {
                // The full cost: parse the note and rewrite its FTS postings.
                IndexedNote note = CollectionSearchIndex::parseNote(
                    e.relPath, snapshot.text, snapshot.fileSize,
                    snapshot.modifiedMs);
                note.changeToken = snapshot.changeToken;
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
        emit reconcileFinished(ok);
    }

    Q_INVOKABLE void replaceFromText(const QString &relPath,
                                     const QString &fileText, qint64 fileSize,
                                     qint64 modifiedMs)
    {
        if (!m_db.isUsable())
            return;
        const IndexedNote note = CollectionSearchIndex::parseNote(
            relPath, fileText, fileSize, modifiedMs);
        if (m_db.replaceNote(note))
            emit noteReplaced();
        else
            emit writeFailed();
    }

    Q_INVOKABLE void replaceFromPath(const QString &relPath,
                                     const QString &absPath)
    {
        if (!m_db.isUsable())
            return;
        const CollectionSearchIndex::NoteSnapshot snapshot =
            CollectionSearchIndex::readNoteSnapshot(absPath);
        if (!snapshot.ok)
            return;
        IndexedNote note = CollectionSearchIndex::parseNote(
            relPath, snapshot.text, snapshot.fileSize, snapshot.modifiedMs);
        note.changeToken = snapshot.changeToken;
        if (m_db.replaceNote(note))
            emit noteReplaced();
        else
            emit writeFailed();
    }

    Q_INVOKABLE void removePath(const QString &relPath)
    {
        if (!m_db.isUsable())
            return;
        if (m_db.removeNote(relPath))
            emit noteReplaced();
        else
            emit writeFailed();
    }

signals:
    void reconcileStarted();
    void reconcileProgress(int indexed, int total);
    void reconcileFinished(bool ok);
    void noteReplaced();
    void writeFailed();

private:
    SearchIndexDb m_db;
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
    SearchIndexReadWorker()
        : m_db(QStringLiteral("read"))
    {
    }

    Q_INVOKABLE bool openDb(const QString &dbPath)
    {
        // Runs on the read thread, so no query can be in flight: a fresh root
        // starts counting generations from zero again.
        m_target.store(0);
        // Read-side opens are non-destructive: the writer has already vetted
        // and, if necessary, rebuilt the file, and a second rebuild here would
        // unlink the database the writer is attached to.
        return m_db.open(dbPath, SearchIndexDb::OpenMode::RequireUsable);
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
    void queryReady(quint64 generation, SearchResults results);

private:
    SearchIndexDb m_db;
    // The newest generation anyone has asked for. Work tagged with anything
    // older is obsolete; the previous shared bool let an older query reset the
    // flag a newer submission had just set, so the obsolete scan ran to the
    // end and the new one waited behind it.
    std::atomic<quint64> m_target{0};
};

// ======================================================================
// Coordinator
// ======================================================================

CollectionSearchIndex::CollectionSearchIndex(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<SearchResults>("SearchResults");
    qRegisterMetaType<SearchQuery>("SearchQuery");
    qRegisterMetaType<QList<ReconcileEntry>>("QList<ReconcileEntry>");

    m_writeThread = new QThread(this);
    m_writeThread->setObjectName(QStringLiteral("kvit-search-write"));
    m_writeWorker = new SearchIndexWriteWorker;
    m_writeWorker->moveToThread(m_writeThread);
    connect(m_writeThread, &QThread::finished, m_writeWorker,
            &QObject::deleteLater);
    connect(m_writeWorker, &SearchIndexWriteWorker::reconcileProgress, this,
            &CollectionSearchIndex::onReconcileProgress);
    connect(m_writeWorker, &SearchIndexWriteWorker::reconcileFinished, this,
            &CollectionSearchIndex::onReconcileFinished);
    connect(m_writeWorker, &SearchIndexWriteWorker::noteReplaced, this,
            &CollectionSearchIndex::onNoteReplaced);
    connect(m_writeWorker, &SearchIndexWriteWorker::writeFailed, this,
            [this]() {
                // Ignore what a worker reports about a root that has already
                // been closed: the index is not degraded, it is gone.
                if (m_usable)
                    setDegraded(true);
            });
    m_writeThread->start();

    m_readThread = new QThread(this);
    m_readThread->setObjectName(QStringLiteral("kvit-search-read"));
    m_readWorker = new SearchIndexReadWorker;
    m_readWorker->moveToThread(m_readThread);
    connect(m_readThread, &QThread::finished, m_readWorker,
            &QObject::deleteLater);
    connect(m_readWorker, &SearchIndexReadWorker::queryReady, this,
            &CollectionSearchIndex::onQueryReady);
    m_readThread->start();
}

CollectionSearchIndex::~CollectionSearchIndex()
{
    // Cancel before the blocking closes, so teardown waits for one note or one
    // row rather than for a whole vault.
    cancelWork();
    if (m_writeWorker)
        QMetaObject::invokeMethod(m_writeWorker, "closeDb",
                                  Qt::BlockingQueuedConnection);
    if (m_readWorker)
        QMetaObject::invokeMethod(m_readWorker, "closeDb",
                                  Qt::BlockingQueuedConnection);
    m_writeThread->quit();
    m_writeThread->wait();
    m_readThread->quit();
    m_readThread->wait();
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

void CollectionSearchIndex::openForRoot(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        closeIndex();
        return;
    }
    m_rootPath = rootPath;
    m_dbPath = databasePathForRoot(rootPath);
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());

    // Stop whatever the previous root left running before waiting on the
    // worker threads: both opens below are blocking, so without this they
    // queue behind a full-vault reconcile or a query over a large index.
    cancelWork();

    // Open the write connection first: it owns schema creation and rebuild, so
    // the read connection never races an empty database into a destructive
    // rebuild.
    bool writeOk = false;
    QMetaObject::invokeMethod(m_writeWorker, "openDb",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, writeOk),
                              Q_ARG(QString, m_dbPath));
    bool readOk = false;
    if (writeOk) {
        QMetaObject::invokeMethod(m_readWorker, "openDb",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, readOk),
                                  Q_ARG(QString, m_dbPath));
    }
    m_pendingReconciles = 0;
    setIndexing(false);
    setDegraded(false);
    setUsable(writeOk && readOk);
}

void CollectionSearchIndex::closeIndex()
{
    // Both closes are BlockingQueuedConnection, so each waits for whatever
    // its worker is doing to return first. Cancel first: they are plain
    // atomics, safe to set from here, and a reconcile or query that stops at
    // its next check turns a wait for the whole vault into a wait for one
    // note.
    cancelWork();
    if (m_writeWorker)
        QMetaObject::invokeMethod(m_writeWorker, "closeDb",
                                  Qt::BlockingQueuedConnection);
    if (m_readWorker)
        QMetaObject::invokeMethod(m_readWorker, "closeDb",
                                  Qt::BlockingQueuedConnection);
    forgetRoot();
}

void CollectionSearchIndex::requestClose()
{
    // The non-blocking teardown. Both workers are told to abandon what they
    // are doing, and the two closes are posted rather than waited on, so a
    // caller switching vaults on the GUI thread never waits behind a full-
    // vault reconcile or a query over a large index. Work queued after these
    // closes — the next root's opens — is delivered in order behind them, so
    // reopening immediately is safe.
    cancelWork();
    if (m_writeWorker)
        QMetaObject::invokeMethod(m_writeWorker, "closeDb",
                                  Qt::QueuedConnection);
    if (m_readWorker)
        QMetaObject::invokeMethod(m_readWorker, "closeDb",
                                  Qt::QueuedConnection);
    forgetRoot();
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
    cancelWork();
    // The reader detaches first so the writer's unlink cannot leave it on a
    // deleted inode, and reattaches only after the writer has recreated the
    // file.
    QMetaObject::invokeMethod(m_readWorker, "closeDb",
                              Qt::BlockingQueuedConnection);
    bool writeOk = false;
    QMetaObject::invokeMethod(m_writeWorker, "rebuildDb",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, writeOk),
                              Q_ARG(QString, dbPath));
    bool readOk = false;
    if (writeOk) {
        QMetaObject::invokeMethod(m_readWorker, "openDb",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(bool, readOk),
                                  Q_ARG(QString, dbPath));
    }
    m_pendingReconciles = 0;
    setIndexing(false);
    setUsable(writeOk && readOk);
    setDegraded(!(writeOk && readOk));
    return writeOk && readOk;
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
// The guarantee, stated per platform:
//   Linux, macOS, other Unix — a matching (size, mtime, ctime) tuple means the
//     file has not been written since it was indexed, subject only to the
//     millisecond resolution Qt reports: a rewrite landing inside the same
//     millisecond as the previous one, with the size and modification time
//     restored, is invisible. That is a race window, where the (size, mtime)
//     pair alone was an open door that any rewrite walked through.
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
        snapshot.changeToken = before.changeToken;
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

    // The same block split and display-text rule the editor and the note-list
    // scan use, so search matches exactly what the editor shows. Divider
    // blocks have empty searchable text.
    DocumentSerializer serializer;
    const QList<DocumentSerializer::BlockData> blocks =
        serializer.parse(split.body);
    int blockIndex = 0;
    for (const DocumentSerializer::BlockData &block : blocks) {
        IndexedBlock indexed;
        indexed.blockIndex = blockIndex++;
        indexed.verbatim = block.type == Block::CodeBlock;
        if (block.type == Block::Divider) {
            indexed.displayText = QString();
        } else {
            const Block cachedBlock(block.type, block.content);
            indexed.displayText = cachedBlock.displayText();
        }
        note.blocks.append(indexed);
    }
    return note;
}

void CollectionSearchIndex::reconcile(const QList<ReconcileEntry> &listing)
{
    if (!m_usable)
        return;
    // Indexing becomes true here, where the work is enqueued, not later when
    // the worker gets around to announcing it. A caller that queued a
    // reconcile and then asked whether the index was busy used to be told no,
    // and a test waiting for the index to settle could sail through the gap
    // before the job had started.
    ++m_pendingReconciles;
    setIndexing(true);
    QMetaObject::invokeMethod(m_writeWorker, "reconcile", Qt::QueuedConnection,
                              Q_ARG(QList<ReconcileEntry>, listing));
}

void CollectionSearchIndex::replaceFromText(const QString &relPath,
                                            const QString &fileText,
                                            qint64 fileSize, qint64 modifiedMs)
{
    if (!m_usable)
        return;
    QMetaObject::invokeMethod(m_writeWorker, "replaceFromText",
                              Qt::QueuedConnection, Q_ARG(QString, relPath),
                              Q_ARG(QString, fileText), Q_ARG(qint64, fileSize),
                              Q_ARG(qint64, modifiedMs));
}

void CollectionSearchIndex::replaceFromPath(const QString &relPath,
                                            const QString &absPath)
{
    if (!m_usable)
        return;
    QMetaObject::invokeMethod(m_writeWorker, "replaceFromPath",
                              Qt::QueuedConnection, Q_ARG(QString, relPath),
                              Q_ARG(QString, absPath));
}

void CollectionSearchIndex::removePath(const QString &relPath)
{
    if (!m_usable)
        return;
    QMetaObject::invokeMethod(m_writeWorker, "removePath", Qt::QueuedConnection,
                              Q_ARG(QString, relPath));
}

void CollectionSearchIndex::submitQuery(quint64 generation,
                                        const SearchQuery &request)
{
    if (!m_usable) {
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
    if (!m_usable || !m_readWorker)
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
    qint64 revision = 0;
    QMetaObject::invokeMethod(m_readWorker, "revisionOf",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(qint64, revision),
                              Q_ARG(QString, relPath));
    return revision;
}

void CollectionSearchIndex::onReconcileProgress(int indexed, int total)
{
    emit indexingProgress(indexed, total);
}

void CollectionSearchIndex::onReconcileFinished(bool ok)
{
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
