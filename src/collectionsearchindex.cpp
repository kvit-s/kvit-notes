// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "collectionsearchindex.h"

#include "block.h"
#include "documentserializer.h"
#include "notefrontmatter.h"
#include "perflog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>

// ======================================================================
// Worker objects. Each owns a thread-affine SearchIndexDb connection and runs
// on its own QThread; the coordinator posts work through queued invocations.
// ======================================================================

// The write side: reconcile, per-note replace, and remove, all serialized on
// one thread with one write connection (search.md §5).
class SearchIndexWriteWorker : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE bool openDb(const QString &dbPath)
    {
        return m_db.open(dbPath, QStringLiteral("kvit_search_write"));
    }

    Q_INVOKABLE void closeDb() { m_db.close(); }

    Q_INVOKABLE void reconcile(QList<ReconcileEntry> listing)
    {
        if (!m_db.isUsable()) {
            emit reconcileFinished();
            return;
        }
        PerfLog::ScopedTimer perf(QStringLiteral("search.index.rebuild"),
                                  QVariantMap{{QStringLiteral("notes"),
                                               listing.size()}});
        emit reconcileStarted();

        // Drop notes that no longer exist on disk.
        QSet<QString> present;
        present.reserve(listing.size());
        for (const ReconcileEntry &e : listing)
            present.insert(e.relPath);
        const QStringList indexed = m_db.allRelPaths();
        for (const QString &relPath : indexed) {
            if (!present.contains(relPath))
                m_db.removeNote(relPath);
        }

        // Parse and replace only new or changed notes.
        int done = 0;
        const int total = listing.size();
        int reindexed = 0;
        for (const ReconcileEntry &e : listing) {
            if (!m_db.hasNoteFresh(e.relPath, e.fileSize, e.modifiedMs)) {
                QString text;
                if (readFile(e.absPath, &text)) {
                    const IndexedNote note = CollectionSearchIndex::parseNote(
                        e.relPath, text, e.fileSize, e.modifiedMs);
                    m_db.replaceNote(note);
                    ++reindexed;
                }
            }
            ++done;
            if ((done % 32) == 0 || done == total)
                emit reconcileProgress(done, total);
        }
        perf.addContext(QStringLiteral("reindexed"), reindexed);
        emit reconcileFinished();
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
    }

    Q_INVOKABLE void replaceFromPath(const QString &relPath,
                                     const QString &absPath)
    {
        if (!m_db.isUsable())
            return;
        QString text;
        if (!readFile(absPath, &text))
            return;
        const QFileInfo info(absPath);
        const IndexedNote note = CollectionSearchIndex::parseNote(
            relPath, text, info.size(),
            info.lastModified().toMSecsSinceEpoch());
        if (m_db.replaceNote(note))
            emit noteReplaced();
    }

    Q_INVOKABLE void removePath(const QString &relPath)
    {
        if (!m_db.isUsable())
            return;
        if (m_db.removeNote(relPath))
            emit noteReplaced();
    }

signals:
    void reconcileStarted();
    void reconcileProgress(int indexed, int total);
    void reconcileFinished();
    void noteReplaced();

private:
    static bool readFile(const QString &absPath, QString *out)
    {
        QFile file(absPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return false;
        *out = QString::fromUtf8(file.readAll());
        return true;
    }

    SearchIndexDb m_db;
};

// The read side: one query at a time on one read connection, cancellable when a
// newer generation arrives (search.md §7).
class SearchIndexReadWorker : public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE bool openDb(const QString &dbPath)
    {
        return m_db.open(dbPath, QStringLiteral("kvit_search_read"));
    }

    Q_INVOKABLE void closeDb() { m_db.close(); }

    Q_INVOKABLE qint64 revisionOf(const QString &relPath)
    {
        return m_db.revisionOf(relPath);
    }

    void requestCancel() { m_cancel.store(true); }
    void setTarget(quint64 generation) { m_target.store(generation); }

    Q_INVOKABLE void runQuery(quint64 generation, SearchQuery request)
    {
        // A generation already superseded before it ran is dropped whole.
        if (generation < m_target.load())
            return;
        m_cancel.store(false);
        const SearchResults results = m_db.query(request, &m_cancel);
        emit queryReady(generation, results);
    }

signals:
    void queryReady(quint64 generation, SearchResults results);

private:
    SearchIndexDb m_db;
    std::atomic_bool m_cancel{false};
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
    connect(m_writeWorker, &SearchIndexWriteWorker::reconcileStarted, this,
            [this]() { setIndexing(true); });
    connect(m_writeWorker, &SearchIndexWriteWorker::reconcileProgress, this,
            &CollectionSearchIndex::onReconcileProgress);
    connect(m_writeWorker, &SearchIndexWriteWorker::reconcileFinished, this,
            &CollectionSearchIndex::onReconcileFinished);
    connect(m_writeWorker, &SearchIndexWriteWorker::noteReplaced, this,
            &CollectionSearchIndex::onNoteReplaced);
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

void CollectionSearchIndex::openForRoot(const QString &rootPath)
{
    if (rootPath.isEmpty()) {
        closeIndex();
        return;
    }
    m_rootPath = rootPath;
    m_dbPath = databasePathForRoot(rootPath);
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());

    // Open the write connection first: it owns schema creation and rebuild, so
    // the read connection never races an empty database into a destructive
    // rebuild (search.md §6.3).
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
    setUsable(writeOk && readOk);
}

void CollectionSearchIndex::closeIndex()
{
    if (m_writeWorker)
        QMetaObject::invokeMethod(m_writeWorker, "closeDb",
                                  Qt::BlockingQueuedConnection);
    if (m_readWorker)
        QMetaObject::invokeMethod(m_readWorker, "closeDb",
                                  Qt::BlockingQueuedConnection);
    m_rootPath.clear();
    m_dbPath.clear();
    setIndexing(false);
    setUsable(false);
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

    const NoteFrontMatter::Split split = NoteFrontMatter::split(fileText);
    note.tags = NoteFrontMatter::parse(split.block).tags;

    // The same block split and display-text rule the editor and the note-list
    // scan use, so search matches exactly what the editor shows (search.md
    // §4.5). Divider blocks have empty searchable text.
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
        // No index: report an empty result so the facade clears cleanly.
        emit queryFinished(generation, SearchResults());
        return;
    }
    m_submittedGeneration.store(generation);
    m_readWorker->setTarget(generation);
    m_readWorker->requestCancel(); // stop any older query still running
    QMetaObject::invokeMethod(m_readWorker, "runQuery", Qt::QueuedConnection,
                              Q_ARG(quint64, generation),
                              Q_ARG(SearchQuery, request));
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

void CollectionSearchIndex::onReconcileFinished()
{
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
    // The coordinator forwards every completed generation; the facade keeps
    // only the latest (search.md §7).
    emit queryFinished(generation, results);
}

#include "collectionsearchindex.moc"
