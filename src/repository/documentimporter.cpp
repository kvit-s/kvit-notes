// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "documentimporter.h"
#include "notecollection.h"
#include "perflog.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {
QString sanitizeBase(const QString &name)
{
    QString out = name.trimmed();
    out.remove(QRegularExpression(QStringLiteral("[/\\\\:*?\"<>|]")));
    return out.isEmpty() ? QStringLiteral("Imported") : out;
}

// One block of a streamed copy. Large enough that the syscall overhead is
// irrelevant, small enough that a hundred concurrent imports would not be
// worth worrying about.
constexpr qint64 kCopyBlockBytes = 1 << 20;

// 64 MiB. A note is prose; the cap exists so that choosing the wrong file in
// the import dialog fails fast instead of reading a disk image into memory.
qint64 g_maxFileBytes = 64LL * 1024 * 1024;

// How long one burst of a stepped import may run before yielding. Short
// enough that a click lands within a frame or two, long enough that the whole
// import is not paying an event-loop turn per file.
constexpr qint64 kStepBudgetMs = 12;
} // namespace

DocumentImporter::DocumentImporter(QObject *parent)
    : QObject(parent)
{
    // Zero-interval and single-shot: each burst asks for the next one, so the
    // event loop gets a turn between them and nothing is queued twice.
    m_stepTimer.setSingleShot(true);
    m_stepTimer.setInterval(0);
    connect(&m_stepTimer, &QTimer::timeout,
            this, &DocumentImporter::stepImportFolder);
}

void DocumentImporter::setCollection(NoteCollection *collection)
{
    m_collection = collection;
}

bool DocumentImporter::isImportable(const QString &path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".md"))
        || lower.endsWith(QLatin1String(".markdown"))
        || lower.endsWith(QLatin1String(".txt"));
}

bool DocumentImporter::noteFileExists(const QString &relPath) const
{
    if (!m_collection)
        return false;
    return QFile::exists(m_collection->absolutePath(relPath));
}

QString DocumentImporter::uniqueRelPath(const QString &folder,
                                        const QString &baseName) const
{
    const QString base = sanitizeBase(baseName);
    const QString prefix = folder.isEmpty() ? QString() : folder + QLatin1Char('/');
    QString rel = prefix + base + QStringLiteral(".md");
    int n = 2;
    while (noteFileExists(rel)) {
        rel = prefix + base + QStringLiteral(" ") + QString::number(n)
            + QStringLiteral(".md");
        ++n;
    }
    return rel;
}

qint64 DocumentImporter::maxFileBytes()
{
    return g_maxFileBytes;
}

void DocumentImporter::setMaxFileBytes(qint64 bytes)
{
    g_maxFileBytes = bytes;
}

void DocumentImporter::requestCancel()
{
    if (m_qmlCancel)
        m_qmlCancel->cancel();
}

CancellationTokenPtr DocumentImporter::tokenForRun(
    const CancellationTokenPtr &cancel)
{
    if (cancel)
        return cancel;
    // A fresh token per run: cancellation is one-way, so reusing the previous
    // one would call off a run the user has not asked to stop.
    m_qmlCancel = makeCancellationToken();
    return m_qmlCancel;
}

bool DocumentImporter::copyInto(const QString &sourcePath,
                                const QString &targetRelPath)
{
    if (!m_collection || !m_collection->ensureWithinRoot(targetRelPath))
        return false;
    QFile in(sourcePath);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const qint64 size = in.size();
    if (maxFileBytes() > 0 && size > maxFileBytes())
        return false;

    const QString destAbs = m_collection->absolutePath(targetRelPath);
    QDir().mkpath(QFileInfo(destAbs).absolutePath());
    // QSaveFile writes a temporary alongside the destination and renames it
    // into place only on commit, so a disk that fills partway through leaves
    // no half-written note for the collection to index. Every byte must land:
    // a short write is a failed import, not a smaller one.
    QSaveFile out(destAbs);
    if (!out.open(QIODevice::WriteOnly))
        return false;

    // Block by block rather than readAll(): the peak memory of an import is
    // one block, whatever the file is, and a read that fails part way is seen
    // here rather than committed as a shorter note. A source that shrinks
    // under us fails the byte count below for the same reason.
    QByteArray block;
    block.resize(int(kCopyBlockBytes));
    qint64 copied = 0;
    forever {
        const qint64 read = in.read(block.data(), kCopyBlockBytes);
        if (read < 0 || in.error() != QFileDevice::NoError) {
            out.cancelWriting();
            return false;
        }
        if (read == 0)
            break;
        if (out.write(block.constData(), read) != read) {
            out.cancelWriting();
            return false;
        }
        copied += read;
        if (maxFileBytes() > 0 && copied > maxFileBytes()) {
            out.cancelWriting();
            return false;
        }
    }
    if (size > 0 && copied != size) {
        out.cancelWriting();
        return false;
    }
    return out.commit();
}

QList<QPair<QString, QString>>
DocumentImporter::importableFilesUnder(const QString &dirPath,
                                       const CancellationTokenPtr &cancel) const
{
    QList<QPair<QString, QString>> files;
    QDir base(dirPath);
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        // The walk is the unbounded half of a folder import: a directory tree
        // can be arbitrarily deep and wide, and until this check existed the
        // only way to stop one was to wait for it.
        if (isCancelled(cancel))
            return files;
        const QString abs = it.next();
        if (!isImportable(abs))
            continue;
        // The relative subdirectory of this file within dirPath.
        const QString relFile = base.relativeFilePath(abs);
        const int slash = relFile.lastIndexOf(QLatin1Char('/'));
        const QString relSub = slash >= 0 ? relFile.left(slash) : QString();
        files.append({abs, relSub});
    }
    return files;
}

// ---- dry runs ----

QVariantMap DocumentImporter::dryRunFiles(const QStringList &paths,
                                          const QString &targetFolder) const
{
    int files = 0;
    int collisions = 0;
    for (const QString &p : paths) {
        if (!isImportable(p))
            continue;
        ++files;
        const QString base = QFileInfo(p).completeBaseName();
        const QString prefix = targetFolder.isEmpty()
            ? QString() : targetFolder + QLatin1Char('/');
        if (noteFileExists(prefix + base + QStringLiteral(".md")))
            ++collisions;
    }
    return QVariantMap{
        {QStringLiteral("files"), files},
        {QStringLiteral("collisions"), collisions},
        {QStringLiteral("folder"), targetFolder},
    };
}

QVariantMap DocumentImporter::dryRunFolder(const QString &dirPath,
                                           const QString &targetFolder) const
{
    const auto entries = importableFilesUnder(dirPath);
    int collisions = 0;
    QSet<QString> subdirs;
    for (const auto &e : entries) {
        if (!e.second.isEmpty())
            subdirs.insert(e.second);
        const QString base = QFileInfo(e.first).completeBaseName();
        const QString folder = targetFolder.isEmpty()
            ? e.second
            : (e.second.isEmpty() ? targetFolder
                                  : targetFolder + QLatin1Char('/') + e.second);
        const QString prefix = folder.isEmpty()
            ? QString() : folder + QLatin1Char('/');
        if (noteFileExists(prefix + base + QStringLiteral(".md")))
            ++collisions;
    }
    return QVariantMap{
        {QStringLiteral("files"), entries.size()},
        {QStringLiteral("collisions"), collisions},
        {QStringLiteral("folders"), subdirs.size()},
    };
}

// ---- imports ----

int DocumentImporter::importFiles(const QStringList &paths,
                                  const QString &targetFolder)
{
    return importFiles(paths, targetFolder, CancellationTokenPtr());
}

int DocumentImporter::importFolder(const QString &dirPath,
                                   const QString &targetFolder)
{
    return importFolder(dirPath, targetFolder, CancellationTokenPtr());
}

int DocumentImporter::importFiles(const QStringList &paths,
                                  const QString &targetFolder,
                                  const CancellationTokenPtr &cancel)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("import.run"),
        QVariantMap{
            {QStringLiteral("paths"), paths.size()},
            {QStringLiteral("targetFolder"), targetFolder},
        });
    setLastSkipped(0);
    if (!m_collection || !m_collection->isOpen())
        return 0;
    if (!m_collection->ensureWithinRoot(targetFolder))
        return 0;
    if (!targetFolder.isEmpty()
        && !QDir().mkpath(m_collection->absolutePath(targetFolder)))
        return 0;
    const CancellationTokenPtr token = tokenForRun(cancel);
    int imported = 0;
    int done = 0;
    for (const QString &p : paths) {
        if (isCancelled(token))
            break;
        if (!isImportable(p))
            continue;
        const QString base = QFileInfo(p).completeBaseName();
        const QString rel = uniqueRelPath(targetFolder, base);
        if (copyInto(p, rel))
            ++imported;
        else
            setLastSkipped(m_lastSkipped + 1);
        emit importProgress(++done, int(paths.size()));
    }
    if (imported > 0)
        m_collection->refresh();
    perf.addContext(QStringLiteral("imported"), imported);
    perf.addContext(QStringLiteral("skipped"), m_lastSkipped);
    return imported;
}

int DocumentImporter::importFolder(const QString &dirPath,
                                   const QString &targetFolder,
                                   const CancellationTokenPtr &cancel)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("import.run"),
        QVariantMap{
            {QStringLiteral("dir"), dirPath},
            {QStringLiteral("targetFolder"), targetFolder},
        });
    setLastSkipped(0);
    if (!m_collection || !m_collection->isOpen())
        return 0;
    if (!m_collection->ensureWithinRoot(targetFolder))
        return 0;
    const CancellationTokenPtr token = tokenForRun(cancel);
    const auto entries = importableFilesUnder(dirPath, token);
    perf.addContext(QStringLiteral("paths"), entries.size());
    int imported = 0;
    int done = 0;
    for (const auto &e : entries) {
        if (isCancelled(token))
            break;
        if (importOne(e.first, e.second, targetFolder))
            ++imported;
        emit importProgress(++done, int(entries.size()));
    }
    if (imported > 0)
        m_collection->refresh();
    perf.addContext(QStringLiteral("imported"), imported);
    perf.addContext(QStringLiteral("skipped"), m_lastSkipped);
    return imported;
}

bool DocumentImporter::importOne(const QString &absSource,
                                 const QString &relSubDir,
                                 const QString &targetFolder)
{
    // The destination folder mirrors the source subfolder under target.
    QString folder;
    if (targetFolder.isEmpty())
        folder = relSubDir;
    else if (relSubDir.isEmpty())
        folder = targetFolder;
    else
        folder = targetFolder + QLatin1Char('/') + relSubDir;
    if (!m_collection->ensureWithinRoot(folder)
        || (!folder.isEmpty()
            && !QDir().mkpath(m_collection->absolutePath(folder)))) {
        setLastSkipped(m_lastSkipped + 1);
        return false;
    }
    const QString base = QFileInfo(absSource).completeBaseName();
    const QString rel = uniqueRelPath(folder, base);
    if (copyInto(absSource, rel))
        return true;
    setLastSkipped(m_lastSkipped + 1);
    return false;
}

void DocumentImporter::setLastSkipped(int skipped)
{
    if (m_lastSkipped == skipped)
        return;
    m_lastSkipped = skipped;
    emit lastSkippedCountChanged();
}

QStringList DocumentImporter::listImportableFiles(const QString &dirPath) const
{
    QStringList paths;
    const auto entries = importableFilesUnder(dirPath);
    paths.reserve(entries.size());
    for (const auto &entry : entries)
        paths.append(entry.first);
    return paths;
}

void DocumentImporter::startImportFolder(const QString &dirPath,
                                         const QString &targetFolder)
{
    if (m_running)
        return; // one run at a time; the dialog offers cancel, not a queue
    if (!m_collection || !m_collection->isOpen())
        return;
    if (!m_collection->ensureWithinRoot(targetFolder))
        return;

    setLastSkipped(0);
    m_stepImported = 0;
    m_queueIndex = 0;
    m_stepTargetFolder = targetFolder;
    // A fresh token, as every run gets: cancellation is one-way, so reusing
    // the previous one would call this run off before it started.
    tokenForRun(CancellationTokenPtr());
    // The walk itself is not stepped. It is the same enumeration the dry run
    // in front of this dialog has already paid for, and it costs one stat per
    // file against a copy per file.
    m_queue = importableFilesUnder(dirPath, m_qmlCancel);

    m_running = true;
    emit importInProgressChanged();
    if (m_queue.isEmpty()) {
        finishSteppedImport(isCancelled(m_qmlCancel));
        return;
    }
    // Deliberately not copying anything yet: the caller gets its event loop
    // back first, so the dialog it just opened can paint before the first
    // file lands.
    m_stepTimer.start();
}

void DocumentImporter::stepImportFolder()
{
    if (!m_running)
        return;
    if (!m_collection || !m_collection->isOpen()) {
        finishSteppedImport(true);
        return;
    }

    QElapsedTimer burst;
    burst.start();
    const int total = int(m_queue.size());
    while (m_queueIndex < total) {
        if (isCancelled(m_qmlCancel)) {
            finishSteppedImport(true);
            return;
        }
        const auto &entry = m_queue.at(m_queueIndex);
        if (importOne(entry.first, entry.second, m_stepTargetFolder))
            ++m_stepImported;
        ++m_queueIndex;
        emit importProgress(m_queueIndex, total);
        if (burst.elapsed() >= kStepBudgetMs)
            break;
    }

    if (m_queueIndex >= total) {
        finishSteppedImport(false);
        return;
    }
    m_stepTimer.start();
}

void DocumentImporter::finishSteppedImport(bool cancelled)
{
    m_stepTimer.stop();
    const int imported = m_stepImported;
    // One rescan for the whole run rather than one per file: refreshing after
    // each copy would cost a full scan of the vault per imported note.
    if (imported > 0 && m_collection && m_collection->isOpen())
        m_collection->refresh();
    m_queue.clear();
    m_queueIndex = 0;
    m_stepImported = 0;
    m_running = false;
    emit importInProgressChanged();
    emit importFinished(imported, m_lastSkipped, cancelled);
}
