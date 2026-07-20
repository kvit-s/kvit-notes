// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notecollection.h"

#include "documentserializer.h"
#include "block.h"
#include "collectionsearchindex.h"
#include "perflog.h"
#include "wikilinkscanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QSet>
#include <QSignalBlocker>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <utility>

namespace {

const QString kvitDirName = QStringLiteral(".kvit");
const QString collectionFileName = QStringLiteral("collection.json");
const QString indexFileName = QStringLiteral("index.json");
const QString trashDirName = QStringLiteral("trash");
const QString mdSuffix = QStringLiteral(".md");

QString readTextFile(const QString &path, bool *ok = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (ok)
            *ok = false;
        return QString();
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    if (ok)
        *ok = true;
    return stream.readAll();
}

QByteArray readFileBytes(const QString &path, bool *ok = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return QByteArray();
    }
    if (ok)
        *ok = true;
    return file.readAll();
}

bool writeTextFileAtomic(const QString &path, const QString &content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << content;
    stream.flush();
    return file.commit();
}

bool writeFileBytesAtomic(const QString &path, const QByteArray &content)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    if (file.write(content) != content.size())
        return false;
    return file.commit();
}

QByteArray contentHash(const QByteArray &content)
{
    return QCryptographicHash::hash(content, QCryptographicHash::Sha256);
}

// Restores a file's modification time (metadata rewrites must not read as
// content edits in the modified-date sort).
void restoreFileTime(const QString &path, const QDateTime &mtime)
{
    QFile file(path);
    if (file.open(QIODevice::ReadWrite)) {
        file.setFileTime(mtime, QFileDevice::FileModificationTime);
        file.close();
    }
}

QString joinRelPath(const QString &folder, const QString &name)
{
    return folder.isEmpty() ? name : folder + QLatin1Char('/') + name;
}

// ---- vault containment ----
//
// A vault is the directory subtree the user selected, and every path the
// collection hands to the filesystem must land inside it. Textual prefix
// tests are not enough: a symbolic link is a path inside the root that names
// a file outside it, so containment is decided on canonical paths, with
// every link along the way already resolved.

// The canonical form of a path that need not exist yet. QFileInfo's own
// canonicalFilePath returns an empty string for anything missing, which
// would leave containment unanswerable for a note about to be created, so
// the deepest existing ancestor is canonicalized and the remaining segments
// are appended to it.
QString canonicalizeMissingOk(const QString &path)
{
    QString head = QDir::cleanPath(path);
    QStringList tail;  // segments trimmed off, nearest-last
    forever {
        const QString canonical = QFileInfo(head).canonicalFilePath();
        if (!canonical.isEmpty()) {
            QString result = canonical;
            while (!tail.isEmpty())
                result += QLatin1Char('/') + tail.takeLast();
            return result;
        }
        const int slash = head.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0)
            return QDir::cleanPath(path);  // nothing on the way exists
        tail.append(head.mid(slash + 1));
        head.truncate(slash);
    }
}

// True when `absPath` resolves to `canonicalRoot` itself or to something
// beneath it. Both sides are canonical, so a link pointing out of the vault
// fails here however innocent its textual path looks.
bool isWithinCanonicalRoot(const QString &canonicalRoot, const QString &absPath)
{
    if (canonicalRoot.isEmpty())
        return false;
    const QString canonical = canonicalizeMissingOk(absPath);
    if (canonical == canonicalRoot)
        return true;
    return canonical.startsWith(canonicalRoot + QLatin1Char('/'));
}

QString folderOfRelPath(const QString &relPath)
{
    int slash = relPath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : relPath.left(slash);
}

QString nameOfRelPath(const QString &relPath)
{
    int slash = relPath.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? relPath : relPath.mid(slash + 1);
}

QDateTime fileCreatedTime(const QFileInfo &info)
{
    QDateTime birth = info.birthTime();
    return birth.isValid() ? birth : info.lastModified();
}

qint64 dateTimeMs(const QDateTime &dt)
{
    return dt.isValid() ? dt.toMSecsSinceEpoch() : 0;
}

QDateTime dateTimeFromMs(qint64 ms)
{
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms) : QDateTime();
}

QJsonArray stringListToJson(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return array;
}

QStringList stringListFromJson(const QJsonArray &array)
{
    QStringList values;
    values.reserve(array.size());
    for (const QJsonValue &value : array)
        values.append(value.toString());
    return values;
}

// A finished future is not necessarily a future that produced a result, and
// reading one that did not is a null dereference rather than an empty value.
// Two routine situations deliver finished() with nothing behind it:
//
//   - cancel(). QFuture::cancel() marks the future finished-and-cancelled.
//     For a future from QtConcurrent::run() it cannot interrupt the running
//     function at all, so the cancel is purely a bookkeeping change: the
//     watcher reports finished immediately while the work carries on.
//   - setFuture() replacing a still-running task, which delivers finished
//     for the future being dropped.
//
// Both happen on ordinary paths — switching vaults, closing one, or starting
// a second refresh — so every handler asks this before it reads. The rule
// belongs in one place: it was previously applied to the saved-note handler
// alone, after a crash in the integration suite, while the scan, refresh and
// index-save handlers kept reading unconditionally.
template <typename T>
static bool hasDeliverableResult(const QFutureWatcher<T> &watcher)
{
    return !watcher.future().isCanceled() && watcher.future().resultCount() > 0;
}

} // namespace

NoteCollection::NoteCollection(QObject *parent)
    : QObject(parent)
{
    m_asyncRevisionTimer.setSingleShot(true);
    m_asyncRevisionTimer.setInterval(50);
    connect(&m_asyncListingWatcher, &QFutureWatcher<AsyncScanListing>::finished,
            this, &NoteCollection::applyAsyncScanListing);
    connect(&m_asyncWatcher, &QFutureWatcher<AsyncIndexResult>::resultReadyAt,
            this, &NoteCollection::applyAsyncIndexResult);
    connect(&m_asyncWatcher, &QFutureWatcher<AsyncIndexResult>::finished,
            this, &NoteCollection::finishAsyncScan);
    connect(&m_asyncRefreshWatcher, &QFutureWatcher<AsyncRefreshResult>::finished,
            this, &NoteCollection::applyAsyncRefreshResult);
    connect(&m_asyncSavedNoteWatcher,
            &QFutureWatcher<AsyncIndexResult>::finished,
            this, &NoteCollection::applyAsyncSavedNoteResult);
    connect(&m_asyncIndexSaveWatcher,
            &QFutureWatcher<AsyncIndexSaveResult>::finished,
            this, &NoteCollection::applyAsyncIndexSaveResult);
    connect(&m_asyncRevisionTimer, &QTimer::timeout,
            this, &NoteCollection::flushAsyncIndexUpdates);
}

NoteCollection::~NoteCollection()
{
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
}

// --------------------------------------------------------------- root

bool NoteCollection::openRoot(const QString &path)
{
    PerfLog::ScopedTimer perf(QStringLiteral("startup.scan"),
                              QVariantMap{{QStringLiteral("path"), path}});
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
    if (!prepareRootPath(path))
        return false;
    scan();

    // Journal files present when a root OPENS are crash evidence: orderly
    // shutdown removes them, and a mid-session refresh() must not ingest
    // the open note's live journal.
    loadRecoveryEntries();

    emit rootChanged();
    bump();
    syncSearchIndex();
    perf.addContext(QStringLiteral("notes"), noteCount());
    perf.addContext(QStringLiteral("folders"), m_folders.size());
    return true;
}

bool NoteCollection::openRootAsync(const QString &path)
{
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
    if (!prepareRootPath(path))
        return false;

    // Open the search index for the new root at once so queries hit the warm
    // database from the previous session while the background scan runs; the
    // reconcile that catches up to on-disk changes waits for finishAsyncScan.
    if (m_searchIndex) {
        m_searchIndex->openForRoot(m_rootPath);
        m_searchIndexRoot = m_rootPath;
    }

    scanAsync();
    loadRecoveryEntries();

    emit rootChanged();
    bump();
    return true;
}

void NoteCollection::closeRoot()
{
    if (!isOpen())
        return;
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
    m_rootPath.clear();
    m_canonicalRoot.clear();
    m_notes.clear();
    m_folders.clear();
    clearFolderNoteCounts();
    m_tagColors.clear();
    m_manualOrder.clear();
    m_lastOpenNote.clear();
    m_pendingRecovery.clear();
    m_indexDirty = false;
    if (m_searchIndex) {
        m_searchIndex->closeIndex();
        m_searchIndexRoot.clear();
    }
    // Released last: nothing above may still be writing when another process
    // is allowed in.
    m_vaultLock.release();
    emit rootChanged();
    bump();
}

bool NoteCollection::prepareRootPath(const QString &path)
{
    QFileInfo info(path);
    if (info.exists() && !info.isDir()) {
        emit operationFailed(tr("Not a folder: %1").arg(path));
        return false;
    }
    if (!QDir().mkpath(path)) {
        emit operationFailed(tr("Cannot create notes folder: %1").arg(path));
        return false;
    }

    // Take the vault before reading any of its state. Everything below this
    // point loads files that will later be written back whole, so a second
    // process reaching the same point would set up the lost update
    // tests/test_vaultlock.cpp demonstrates. Unavailable (no locking on this
    // filesystem, read-only directory) opens unlocked rather than refusing:
    // an unopenable vault is a worse outcome than an unguarded one.
    const QString absolute = QDir(path).absolutePath();
    if (m_vaultLock.acquire(absolute) == VaultLock::Result::HeldByAnother) {
        // One signal, not operationFailed as well: the UI needs to say
        // something specific here rather than show a generic failure, and two
        // signals for one event invites handling it twice.
        emit vaultInUse(absolute, m_vaultLock.blockingHolder().describe());
        return false;
    }

    m_rootPath = absolute;
    m_canonicalRoot = canonicalizeMissingOk(m_rootPath);
    return true;
}

void NoteCollection::loadRecoveryEntries()
{
    m_pendingRecovery.clear();
    const QDir recoveryDir(m_rootPath + QStringLiteral("/") + kvitDirName
                           + QStringLiteral("/recovery"));
    const QStringList journals = recoveryDir.entryList(QDir::Files, QDir::Name);
    for (const QString &encoded : journals) {
        m_pendingRecovery.append(QString::fromUtf8(
            QByteArray::fromPercentEncoding(encoded.toUtf8())));
    }
}

void NoteCollection::refresh()
{
    if (!isOpen())
        return;
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
    PerfLog::ScopedTimer perf(QStringLiteral("collection.refresh"),
                              QVariantMap{{QStringLiteral("path"), m_rootPath}});
    scan();
    perf.addContext(QStringLiteral("notes"), noteCount());
    perf.addContext(QStringLiteral("folders"), m_folders.size());
    bump();
    syncSearchIndex();
}

void NoteCollection::refreshPaths(const QStringList &absPaths)
{
    if (!isOpen())
        return;
    if (absPaths.isEmpty()) {
        refresh();
        return;
    }

    QStringList relPaths;
    QStringList relDirs;
    bool needsFullRefresh = false;
    for (const QString &absPath : absPaths) {
        const QString relPath = relativePath(absPath);
        if (relPath.isEmpty()) {
            needsFullRefresh = true;
            break;
        }
        if (relPath.startsWith(QLatin1Char('.'))) {
            continue;
        }
        const QFileInfo info(absPath);
        if (info.exists() && info.isDir()) {
            if (!relDirs.contains(relPath))
                relDirs.append(relPath);
            continue;
        }
        if (!info.exists() && m_folders.contains(relPath)) {
            if (!relDirs.contains(relPath))
                relDirs.append(relPath);
            continue;
        }
        if (!info.exists() && !m_notes.contains(relPath)) {
            continue;
        }
        if (!relPath.endsWith(mdSuffix, Qt::CaseInsensitive)) {
            needsFullRefresh = true;
            break;
        }
        if (!relPaths.contains(relPath))
            relPaths.append(relPath);
    }

    if (needsFullRefresh) {
        refresh();
        return;
    }

    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.refresh"),
        QVariantMap{{QStringLiteral("path"), m_rootPath},
                    {QStringLiteral("changedPaths"),
                     relPaths.size() + relDirs.size()},
                    {QStringLiteral("asyncDirs"), relDirs.size()}});

    auto isUnderDirectory = [](const QString &relPath, const QString &relDir) {
        return relPath.startsWith(relDir + QLatin1Char('/'));
    };

    QStringList effectiveRelDirs;
    for (const QString &relDir : std::as_const(relDirs)) {
        bool covered = false;
        for (const QString &parentDir : std::as_const(effectiveRelDirs)) {
            if (isUnderDirectory(relDir, parentDir)) {
                covered = true;
                break;
            }
        }
        if (!covered) {
            for (int i = effectiveRelDirs.size() - 1; i >= 0; --i) {
                if (isUnderDirectory(effectiveRelDirs.at(i), relDir))
                    effectiveRelDirs.removeAt(i);
            }
            effectiveRelDirs.append(relDir);
        }
    }

    bool changed = false;
    for (const QString &relPath : relPaths) {
        bool coveredByDirectory = false;
        for (const QString &relDir : std::as_const(effectiveRelDirs)) {
            if (isUnderDirectory(relPath, relDir)) {
                coveredByDirectory = true;
                break;
            }
        }
        if (coveredByDirectory)
            continue;

        const QString absPath = absolutePath(relPath);
        const QFileInfo info(absPath);
        if (info.exists() && info.isFile()) {
            indexNote(relPath);
            changed = true;
        } else if (m_notes.contains(relPath)) {
            const QString folderPath = m_notes.value(relPath).folder;
            removeNoteEntry(relPath);
            m_manualOrder[folderPath].removeAll(nameOfRelPath(relPath));
            if (m_lastOpenNote == relPath)
                m_lastOpenNote.clear();
            emit noteRemoved(relPath);
            markIndexDirty();
            changed = true;
        }
    }

    if (changed) {
        saveIndexFileIfDirty();
        perf.addContext(QStringLiteral("notes"), noteCount());
        perf.addContext(QStringLiteral("folders"), m_folders.size());
        bump();
    }

    if (!effectiveRelDirs.isEmpty())
        startAsyncDirectoryRefresh(effectiveRelDirs);
}

void NoteCollection::initializeIfEmpty()
{
    if (!isOpen() || !m_notes.isEmpty())
        return;

    const QString welcome = QStringLiteral(
        "# Welcome to Kvit\n"
        "\n"
        "This folder is your notes collection. Every note is a plain "
        "markdown file on disk — organize them into folders on the left, "
        "tag them, and search across all of them.\n"
        "\n"
        "- Press **Ctrl+N** to create a note\n"
        "- Type `/` in an empty block for block types\n"
        "- Press **Ctrl+Shift+F** to search every note\n");
    const QString relPath = QStringLiteral("Welcome.md");
    if (writeTextFileAtomic(absolutePath(relPath), welcome)) {
        indexNote(relPath);
        saveIndexFileIfDirty();
        bump();
    }
}

QString NoteCollection::absolutePath(const QString &relPath) const
{
    if (m_rootPath.isEmpty())
        return QString();
    return m_rootPath + QLatin1Char('/') + relPath;
}

QString NoteCollection::relativePath(const QString &absPath) const
{
    if (m_rootPath.isEmpty())
        return QString();
    // Absolute paths arrive here from the file watcher, from saves, and from
    // QML. Resolving links before the containment test is what stops a path
    // that merely looks like it is under the root from being treated as a
    // note — the empty return is every caller's "not mine".
    if (!isWithinCanonicalRoot(m_canonicalRoot, absPath))
        return QString();
    const QString canonical = canonicalizeMissingOk(absPath);
    if (canonical == m_canonicalRoot)
        return QString();
    return canonical.mid(m_canonicalRoot.size() + 1);
}

bool NoteCollection::ensureWithinRoot(const QString &relPath)
{
    if (!isOpen())
        return false;
    if (isWithinCanonicalRoot(m_canonicalRoot, m_rootPath + QLatin1Char('/')
                                                   + relPath))
        return true;
    emit operationFailed(tr("\"%1\" is outside the notes folder").arg(relPath));
    return false;
}

// --------------------------------------------------------------- scan

void NoteCollection::scan()
{
    cancelAsyncScan();
    m_notes.clear();
    m_folders.clear();
    clearFolderNoteCounts();
    m_tagColors.clear();
    m_manualOrder.clear();
    m_lastOpenNote.clear();
    m_indexDirty = false;

    bool indexOk = false;
    const QHash<QString, NoteEntry> cachedNotes = loadIndexFile(&indexOk);
    m_notes.reserve(cachedNotes.size());
    m_folderOwnNoteCounts.reserve(cachedNotes.size());
    m_folderRecursiveNoteCounts.reserve(cachedNotes.size());
    m_indexDirty = !indexOk && QFileInfo::exists(indexFilePath());

    QSet<QString> visitedDirs;
    scanDirectory(QString(), cachedNotes, &visitedDirs);
    loadCollectionFile();

    if (!indexOk || cachedNotes.size() != m_notes.size())
        m_indexDirty = true;
    saveIndexFileIfDirty();
}

void NoteCollection::scanAsync()
{
    cancelAsyncScan();
    m_notes.clear();
    m_folders.clear();
    clearFolderNoteCounts();
    m_tagColors.clear();
    m_manualOrder.clear();
    m_lastOpenNote.clear();
    m_indexDirty = false;
    m_asyncPendingUpdates = 0;
    m_asyncScanTimer.start();
    const quint64 generation = ++m_asyncScanGeneration;

    bool indexOk = false;
    QHash<QString, NoteEntry> cachedNotes = loadIndexFile(&indexOk);
    const bool indexFileExists = QFileInfo::exists(indexFilePath());
    m_notes.reserve(cachedNotes.size());
    m_folderOwnNoteCounts.reserve(cachedNotes.size());
    m_folderRecursiveNoteCounts.reserve(cachedNotes.size());
    m_indexDirty = !indexOk && indexFileExists;

    // Load workspace state now so startup can open a remembered note from
    // disk before the background directory listing has caught up. Folder
    // visual state is applied again after the listing publishes folders.
    loadCollectionFile();

    setScanInProgress(true);
    emit scanStarted();

    AsyncScanRequest request;
    request.rootPath = m_rootPath;
    request.cachedNotes = std::move(cachedNotes);
    request.indexOk = indexOk;
    request.indexFileExists = indexFileExists;
    request.generation = generation;
    m_scanCancel = makeCancellationToken();
    request.cancel = m_scanCancel;
    m_asyncListingWatcher.setFuture(
        QtConcurrent::run(&NoteCollection::buildAsyncScanListing, request));
}

void NoteCollection::scanDirectory(const QString &relDir,
                                   const QHash<QString, NoteEntry> &cachedNotes,
                                   QSet<QString> *visitedDirs)
{
    const QString absDir = relDir.isEmpty() ? m_rootPath : absolutePath(relDir);
    const QString canonicalDir = canonicalizeMissingOk(absDir);
    if (visitedDirs->contains(canonicalDir))
        return;
    visitedDirs->insert(canonicalDir);
    QDir dir(absDir);
    const QFileInfoList entries = dir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDir::Name);
    for (const QFileInfo &info : entries) {
        const QString name = info.fileName();
        if (name.startsWith(QLatin1Char('.'))) // .kvit and other dot entries
            continue;
        const QString relPath = joinRelPath(relDir, name);
        if (info.isDir()) {
            FolderEntry folder;
            folder.relPath = relPath;
            folder.name = name;
            m_folders.insert(relPath, folder);
            scanDirectory(relPath, cachedNotes, visitedDirs);
        } else if (name.endsWith(mdSuffix, Qt::CaseInsensitive)) {
            indexNote(relPath, cachedNotes);
        }
    }
}

void NoteCollection::indexNote(const QString &relPath)
{
    indexNote(relPath, QHash<QString, NoteEntry>());
}

void NoteCollection::indexNote(const QString &relPath,
                               const QHash<QString, NoteEntry> &cachedNotes)
{
    PerfLog::ScopedTimer perf(QStringLiteral("scan.note"),
                              QVariantMap{{QStringLiteral("note"), relPath}},
                              PerfLog::Verbose);
    const QString absPath = absolutePath(relPath);
    const QFileInfo info(absPath);
    if (tryIndexNoteFromCache(relPath, info, cachedNotes)) {
        perf.addContext(QStringLiteral("ok"), true);
        perf.addContext(QStringLiteral("cached"), true);
        const NoteEntry *entry = note(relPath);
        if (entry) {
            perf.addContext(QStringLiteral("words"), entry->wordCount);
        }
        return;
    }

    bool ok = false;
    const QString fileText = readTextFile(absPath, &ok);
    if (!ok) {
        perf.addContext(QStringLiteral("ok"), false);
        return;
    }

    indexNoteFromText(relPath, fileText, info);
    const NoteEntry *entry = note(relPath);
    perf.addContext(QStringLiteral("ok"), true);
    perf.addContext(QStringLiteral("cached"), false);
    if (entry) {
        perf.addContext(QStringLiteral("words"), entry->wordCount);
    }
}

bool NoteCollection::tryIndexNoteFromCache(
    const QString &relPath,
    const QFileInfo &info,
    const QHash<QString, NoteEntry> &cachedNotes)
{
    const auto it = cachedNotes.constFind(relPath);
    if (it == cachedNotes.constEnd() || !info.exists() || !info.isFile())
        return false;

    const NoteEntry &cached = it.value();
    if (cached.fileSize != info.size()
        || dateTimeMs(cached.modified) != dateTimeMs(info.lastModified())) {
        markIndexDirty();
        return false;
    }

    NoteEntry entry = cached;
    entry.relPath = relPath;
    entry.folder = folderOfRelPath(relPath);
    QString name = nameOfRelPath(relPath);
    entry.title = name.endsWith(mdSuffix, Qt::CaseInsensitive)
                      ? name.left(name.size() - mdSuffix.size())
                      : name;
    insertNoteEntry(relPath, entry);
    return true;
}

NoteCollection::NoteEntry NoteCollection::placeholderEntry(
    const QString &relPath,
    const QFileInfo &info)
{
    NoteEntry entry;
    entry.relPath = relPath;
    entry.folder = folderOfRelPath(relPath);
    const QString name = nameOfRelPath(relPath);
    entry.title = name.endsWith(mdSuffix, Qt::CaseInsensitive)
                      ? name.left(name.size() - mdSuffix.size())
                      : name;
    entry.modified = info.lastModified();
    // Not a complete index entry yet. If parsing fails before this placeholder
    // is replaced, the persisted sidecar must force a retry on the next launch.
    entry.fileSize = -1;
    entry.created = fileCreatedTime(info);
    return entry;
}

NoteCollection::NoteEntry NoteCollection::cachedEntryForPath(
    const QString &relPath,
    const NoteEntry &cached,
    const QFileInfo &info)
{
    NoteEntry entry = cached;
    entry.relPath = relPath;
    entry.folder = folderOfRelPath(relPath);
    const QString name = nameOfRelPath(relPath);
    entry.title = name.endsWith(mdSuffix, Qt::CaseInsensitive)
                      ? name.left(name.size() - mdSuffix.size())
                      : name;
    if (!entry.created.isValid())
        entry.created = fileCreatedTime(info);
    return entry;
}

NoteCollection::NoteEntry NoteCollection::entryFromText(
    const QString &relPath,
    const QString &fileText,
    const QFileInfo &info)
{
    NoteFrontMatter::Split split = NoteFrontMatter::split(fileText);

    NoteEntry entry;
    entry.relPath = relPath;
    entry.folder = folderOfRelPath(relPath);
    const QString name = nameOfRelPath(relPath);
    entry.title = name.endsWith(mdSuffix, Qt::CaseInsensitive)
                      ? name.left(name.size() - mdSuffix.size())
                      : name;
    entry.meta = NoteFrontMatter::parse(split.block);

    entry.modified = info.lastModified();
    entry.fileSize = info.size();
    entry.created =
        entry.meta.created.isValid() ? entry.meta.created : fileCreatedTime(info);
    BodyStats stats = analyzeBody(split.body);
    entry.wordCount = stats.wordCount;
    entry.snippet = stats.snippet;
    entry.links = extractWikiLinks(split.body);
    return entry;
}

NoteCollection::AsyncScanListing NoteCollection::buildAsyncScanListing(
    const AsyncScanRequest &request)
{
    AsyncScanListing listing;
    listing.rootPath = request.rootPath;
    listing.indexDirty = !request.indexOk && request.indexFileExists;
    listing.generation = request.generation;

    int noteCount = 0;
    // Canonical directories already walked. Symbolic links are excluded
    // below, but a directory can be re-entered by other means (a bind mount,
    // a junction), and a scan that revisits one never finishes.
    QSet<QString> visitedDirs;
    std::function<void(const QString &)> scanDir =
        [&](const QString &currentRelDir) {
            // A vault the user has already left should stop being walked.
            // QtConcurrent::run cannot interrupt this, so the check is the
            // only way out before the last directory.
            if (isCancelled(request.cancel))
                return;
            const QString absDir = currentRelDir.isEmpty()
                ? request.rootPath
                : request.rootPath + QLatin1Char('/') + currentRelDir;
            const QString canonicalDir = canonicalizeMissingOk(absDir);
            if (visitedDirs.contains(canonicalDir))
                return;
            visitedDirs.insert(canonicalDir);
            QDir dir(absDir);
            const QFileInfoList entries = dir.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot
                    | QDir::NoSymLinks,
                QDir::Name);
            for (const QFileInfo &info : entries) {
                if (isCancelled(request.cancel))
                    return;
                const QString name = info.fileName();
                if (name.startsWith(QLatin1Char('.')))
                    continue;

                const QString relPath = joinRelPath(currentRelDir, name);
                if (info.isDir()) {
                    FolderEntry folder;
                    folder.relPath = relPath;
                    folder.name = name;
                    listing.folders.append(folder);
                    scanDir(relPath);
                    continue;
                }

                if (!name.endsWith(mdSuffix, Qt::CaseInsensitive))
                    continue;

                ++noteCount;
                const auto cachedIt = request.cachedNotes.constFind(relPath);
                if (cachedIt != request.cachedNotes.constEnd()
                    && cachedIt->fileSize == info.size()
                    && dateTimeMs(cachedIt->modified)
                        == dateTimeMs(info.lastModified())) {
                    listing.entries.append(
                        cachedEntryForPath(relPath, cachedIt.value(), info));
                    continue;
                }

                if (cachedIt == request.cachedNotes.constEnd())
                    listing.entries.append(placeholderEntry(relPath, info));
                else
                    listing.entries.append(
                        cachedEntryForPath(relPath, cachedIt.value(), info));

                AsyncIndexTask task;
                task.relPath = relPath;
                task.absPath = info.absoluteFilePath();
                task.createdFallback = fileCreatedTime(info);
                task.modified = info.lastModified();
                task.fileSize = info.size();
                task.generation = request.generation;
                listing.tasks.append(task);
                listing.indexDirty = true;
            }
        };

    scanDir(QString());

    if ((!request.indexOk && noteCount > 0)
        || request.cachedNotes.size() != noteCount) {
        listing.indexDirty = true;
    }
    listing.cancelled = isCancelled(request.cancel);
    return listing;
}

NoteCollection::AsyncIndexResult NoteCollection::parseIndexTask(
    const AsyncIndexTask &task)
{
    AsyncIndexResult result;
    result.relPath = task.relPath;
    result.generation = task.generation;

    bool ok = false;
    const QString fileText = readTextFile(task.absPath, &ok);
    if (!ok)
        return result;

    QFileInfo info(task.absPath);
    result.entry = entryFromText(task.relPath, fileText, info);
    result.ok = true;
    return result;
}

NoteCollection::AsyncRefreshResult NoteCollection::buildAsyncRefreshResult(
    const AsyncRefreshRequest &request)
{
    AsyncRefreshResult result;
    result.rootPath = request.rootPath;
    result.relDirs = request.relDirs;
    result.generation = request.generation;

    QSet<QString> visitedDirs;
    std::function<void(const QString &)> scanDir =
        [&](const QString &currentRelDir) {
            // Between directories, and again between notes below: this walk
            // reads and parses every changed body, so an abandoned refresh
            // that ran to the end would hold a pool thread against work whose
            // result is already going to be thrown away.
            if (isCancelled(request.cancel))
                return;
            const QString absDir =
                request.rootPath + QLatin1Char('/') + currentRelDir;
            const QString canonicalDir = canonicalizeMissingOk(absDir);
            if (visitedDirs.contains(canonicalDir))
                return;
            visitedDirs.insert(canonicalDir);
            QDir dir(absDir);
            const QFileInfoList entries = dir.entryInfoList(
                QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot
                    | QDir::NoSymLinks,
                QDir::Name);
            for (const QFileInfo &info : entries) {
                if (isCancelled(request.cancel))
                    return;
                const QString name = info.fileName();
                if (name.startsWith(QLatin1Char('.')))
                    continue;

                const QString relPath = joinRelPath(currentRelDir, name);
                if (info.isDir()) {
                    FolderEntry folder;
                    folder.relPath = relPath;
                    folder.name = name;
                    result.folders.append(folder);
                    result.seenFolders.insert(relPath);
                    scanDir(relPath);
                    continue;
                }

                if (!name.endsWith(mdSuffix, Qt::CaseInsensitive))
                    continue;

                result.seenNotes.insert(relPath);
                const auto current = request.currentNotes.constFind(relPath);
                if (current != request.currentNotes.constEnd()
                    && current->fileSize == info.size()
                    && dateTimeMs(current->modified)
                        == dateTimeMs(info.lastModified())) {
                    continue;
                }

                bool ok = false;
                const QString fileText = readTextFile(info.absoluteFilePath(), &ok);
                if (!ok)
                    continue;

                result.entries.append(
                    entryFromText(relPath, fileText, QFileInfo(info.absoluteFilePath())));
            }
        };

    for (const QString &relDir : request.relDirs) {
        const QString absDir = request.rootPath + QLatin1Char('/') + relDir;
        const QFileInfo dirInfo(absDir);
        if (!dirInfo.exists() || !dirInfo.isDir()) {
            result.missingDirs.append(relDir);
            continue;
        }

        if (!relDir.isEmpty()) {
            FolderEntry folder;
            folder.relPath = relDir;
            folder.name = nameOfRelPath(relDir);
            result.folders.append(folder);
            result.seenFolders.insert(relDir);
        }
        scanDir(relDir);
    }

    result.cancelled = isCancelled(request.cancel);
    return result;
}

NoteCollection::AsyncIndexResult NoteCollection::parseSavedNoteTask(
    const AsyncSavedNoteTask &task)
{
    AsyncIndexResult result;
    result.relPath = task.relPath;
    result.generation = task.generation;

    QString fileText = task.fileText;
    if (fileText.isNull()) {
        bool ok = false;
        fileText = readTextFile(task.absPath, &ok);
        if (!ok)
            return result;
    }

    PerfLog::ScopedTimer split(
        QStringLiteral("collection.note_saved.index"),
        QVariantMap{{QStringLiteral("path"), task.absPath},
                    {QStringLiteral("relPath"), task.relPath},
                    {QStringLiteral("fromMemory"), !task.fileText.isNull()}});

    const QFileInfo info(task.absPath);
    NoteEntry entry = entryFromText(task.relPath, fileText, info);
    entry.modified = task.modified;
    entry.fileSize = task.fileSize;
    entry.created = entry.meta.created.isValid()
        ? entry.meta.created : task.createdFallback;

    result.entry = std::move(entry);
    result.ok = true;
    return result;
}

QByteArray NoteCollection::buildIndexFileBytes(
    const QHash<QString, NoteEntry> &notes)
{
    QJsonObject root;
    // Version 2: the sidecar no longer caches full
    // bodies or per-block display text — global search reads those from the
    // SQLite index. Wiki-link targets, previously re-derived from the cached
    // body on every load, are persisted so warm startup keeps the backlink
    // graph without reading every note. An older version-1 cache is discarded.
    root.insert(QStringLiteral("version"), 2);

    QJsonArray entries;
    QStringList paths = notes.keys();
    paths.sort();
    for (const QString &relPath : paths) {
        const auto it = notes.constFind(relPath);
        if (it == notes.constEnd())
            continue;
        const NoteEntry &entry = it.value();
        QJsonObject object;
        object.insert(QStringLiteral("relPath"), entry.relPath);
        object.insert(QStringLiteral("title"), entry.title);
        object.insert(QStringLiteral("createdMs"),
                      double(dateTimeMs(entry.created)));
        object.insert(QStringLiteral("modifiedMs"),
                      double(dateTimeMs(entry.modified)));
        object.insert(QStringLiteral("size"), double(entry.fileSize));
        object.insert(QStringLiteral("wordCount"), entry.wordCount);
        object.insert(QStringLiteral("snippet"), entry.snippet);
        object.insert(QStringLiteral("links"), stringListToJson(entry.links));
        object.insert(QStringLiteral("frontMatter"),
                      NoteFrontMatter::serialize(entry.meta));
        entries.append(object);
    }
    root.insert(QStringLiteral("notes"), entries);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

NoteCollection::AsyncIndexSaveResult
NoteCollection::writeIndexFileSnapshot(const AsyncIndexSaveRequest &request)
{
    AsyncIndexSaveResult result;
    result.path = request.path;
    result.notes = request.notes.size();
    result.generation = request.generation;

    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.index_save"),
        QVariantMap{{QStringLiteral("path"), request.path},
                    {QStringLiteral("notes"), request.notes.size()},
                    {QStringLiteral("async"), true}});

    const QByteArray bytes = buildIndexFileBytes(request.notes);
    result.bytes = bytes.size();
    perf.addContext(QStringLiteral("bytes"), result.bytes);
    result.ok = writeFileBytesAtomic(request.path, bytes);
    perf.addContext(QStringLiteral("ok"), result.ok);
    return result;
}

void NoteCollection::indexNoteFromText(const QString &relPath,
                                       const QString &fileText,
                                       const QFileInfo &info)
{
    if (m_indexParseObserver)
        m_indexParseObserver(relPath);

    const NoteEntry entry = entryFromText(relPath, fileText, info);
    ensureFolderEntriesFor(entry.folder);
    insertNoteEntry(relPath, entry);
    markIndexDirty();
}

int NoteCollection::wordCountForMarkdown(const QString &markdown,
                                         bool verbatim) const
{
    const Block cachedBlock(verbatim ? Block::CodeBlock : Block::Paragraph,
                            markdown);
    return cachedBlock.wordCount();
}

int NoteCollection::charCountForMarkdown(const QString &markdown,
                                         bool verbatim) const
{
    const Block cachedBlock(verbatim ? Block::CodeBlock : Block::Paragraph,
                            markdown);
    return cachedBlock.charCount(true);
}

// One parse serves the index: per-block display text (what the user sees
// and what search matches over), word count, and the list snippet.
NoteCollection::BodyStats NoteCollection::analyzeBody(const QString &markdownBody)
{
    static const int snippetLength = 120;
    DocumentSerializer serializer;

    BodyStats stats;
    const QList<DocumentSerializer::BlockData> blocks =
        serializer.parse(markdownBody);
    for (const DocumentSerializer::BlockData &block : blocks) {
        if (block.type == Block::Divider)
            continue;
        const Block cachedBlock(block.type, block.content);
        const QString text = cachedBlock.displayText();
        stats.wordCount += cachedBlock.wordCount();
        if (stats.snippet.size() < snippetLength && !block.content.isEmpty()) {
            if (!stats.snippet.isEmpty())
                stats.snippet += QLatin1Char(' ');
            stats.snippet += text.simplified();
        }
    }
    stats.snippet = stats.snippet.left(snippetLength);
    return stats;
}

void NoteCollection::ensureFolderEntriesFor(const QString &folderPath)
{
    if (folderPath.isEmpty())
        return;

    QString accumulated;
    const QStringList parts = folderPath.split(QLatin1Char('/'),
                                               Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        accumulated = joinRelPath(accumulated, part);
        if (m_folders.contains(accumulated))
            continue;
        FolderEntry folder;
        folder.relPath = accumulated;
        folder.name = part;
        m_folders.insert(accumulated, folder);
    }
}

void NoteCollection::insertNoteEntry(const QString &relPath,
                                     const NoteEntry &entry)
{
    removeNoteEntry(relPath);
    m_notes.insert(relPath, entry);
    adjustFolderNoteCounts(entry.folder, 1);
    invalidateWikiIndex();
}

void NoteCollection::removeNoteEntry(const QString &relPath)
{
    const auto it = m_notes.constFind(relPath);
    if (it == m_notes.constEnd())
        return;
    adjustFolderNoteCounts(it.value().folder, -1);
    m_notes.remove(relPath);
    invalidateWikiIndex();
}

void NoteCollection::adjustFolderNoteCounts(const QString &folderPath, int delta)
{
    auto adjust = [delta](QHash<QString, int> *counts, const QString &key) {
        const int next = counts->value(key) + delta;
        if (next == 0)
            counts->remove(key);
        else
            counts->insert(key, next);
    };

    adjust(&m_folderOwnNoteCounts, folderPath);

    QString current = folderPath;
    while (true) {
        adjust(&m_folderRecursiveNoteCounts, current);
        if (current.isEmpty())
            break;
        current = folderOfRelPath(current);
    }
}

void NoteCollection::rebuildFolderNoteCounts()
{
    clearFolderNoteCounts();
    for (const NoteEntry &entry : m_notes)
        adjustFolderNoteCounts(entry.folder, 1);
}

void NoteCollection::clearFolderNoteCounts()
{
    m_folderOwnNoteCounts.clear();
    m_folderRecursiveNoteCounts.clear();
}

QString NoteCollection::indexFilePath() const
{
    if (!isOpen())
        return QString();
    return m_rootPath + QLatin1Char('/') + kvitDirName
        + QLatin1Char('/') + indexFileName;
}

QHash<QString, NoteCollection::NoteEntry>
NoteCollection::loadIndexFile(bool *ok) const
{
    if (ok)
        *ok = false;

    QHash<QString, NoteEntry> notes;
    const QString path = indexFilePath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return notes;

    bool readOk = false;
    const QByteArray bytes = readFileBytes(path, &readOk);
    if (!readOk)
        return notes;

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return notes;

    const QJsonObject root = doc.object();
    // Only the current sidecar format is trusted; an older cache is dropped and
    // rebuilt from Markdown.
    if (root.value(QStringLiteral("version")).toInt() != 2)
        return notes;

    const QJsonArray entries = root.value(QStringLiteral("notes")).toArray();
    notes.reserve(entries.size());
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        const QString relPath = object.value(QStringLiteral("relPath")).toString();
        if (relPath.isEmpty()
            || !relPath.endsWith(mdSuffix, Qt::CaseInsensitive))
            continue;

        NoteEntry entry;
        entry.relPath = relPath;
        entry.folder = folderOfRelPath(relPath);
        const QString fallbackName = nameOfRelPath(relPath);
        entry.title = object.value(QStringLiteral("title")).toString(
            fallbackName.endsWith(mdSuffix, Qt::CaseInsensitive)
                ? fallbackName.left(fallbackName.size() - mdSuffix.size())
                : fallbackName);
        entry.created = dateTimeFromMs(
            qint64(object.value(QStringLiteral("createdMs")).toDouble()));
        entry.modified = dateTimeFromMs(
            qint64(object.value(QStringLiteral("modifiedMs")).toDouble()));
        entry.fileSize =
            qint64(object.value(QStringLiteral("size")).toDouble(-1));
        entry.wordCount = object.value(QStringLiteral("wordCount")).toInt();
        entry.snippet = object.value(QStringLiteral("snippet")).toString();
        // Wiki-link targets are read from the sidecar; without the body cached
        // they can no longer be re-derived here.
        entry.links = stringListFromJson(
            object.value(QStringLiteral("links")).toArray());
        entry.meta = NoteFrontMatter::parse(
            object.value(QStringLiteral("frontMatter")).toString());
        notes.insert(relPath, entry);
    }

    if (ok)
        *ok = true;
    return notes;
}

bool NoteCollection::saveIndexFile() const
{
    const QString path = indexFilePath();
    if (path.isEmpty())
        return false;

    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.index_save"),
        QVariantMap{{QStringLiteral("path"), path},
                    {QStringLiteral("notes"), m_notes.size()}});

    if (m_notes.isEmpty() && !QFileInfo::exists(path)) {
        perf.addContext(QStringLiteral("skipped"), true);
        return true; // nothing to write is not a failure
    }

    const QString dir = m_rootPath + QLatin1Char('/') + kvitDirName;
    QDir().mkpath(dir);
    const QByteArray bytes = buildIndexFileBytes(m_notes);
    perf.addContext(QStringLiteral("bytes"), bytes.size());
    const bool ok = writeFileBytesAtomic(path, bytes);
    perf.addContext(QStringLiteral("ok"), ok);
    return ok;
}

void NoteCollection::saveIndexFileIfDirty()
{
    if (!m_indexDirty)
        return;
    // Only a write that actually landed clears the flag. Clearing it
    // regardless left the sidecar stale with nothing remembering that it
    // needed rewriting, so the next session paid for a full rescan and any
    // later chance to retry was gone.
    if (saveIndexFile())
        m_indexDirty = false;
}

void NoteCollection::markIndexDirty()
{
    m_indexDirty = true;
}

void NoteCollection::setIndexParseObserverForTesting(
    std::function<void(const QString &)> observer)
{
    m_indexParseObserver = std::move(observer);
}

void NoteCollection::setScanInProgress(bool inProgress)
{
    if (m_scanInProgress == inProgress)
        return;
    m_scanInProgress = inProgress;
    emit scanInProgressChanged();
}

void NoteCollection::applyAsyncScanListing()
{
    if (!hasDeliverableResult(m_asyncListingWatcher))
        return;
    const AsyncScanListing listing = m_asyncListingWatcher.result();
    // A stopped walk lists only part of the vault; publishing it would show
    // a truncated folder tree and index only what it happened to reach.
    if (listing.cancelled)
        return;
    if (!isOpen() || listing.rootPath != m_rootPath
        || listing.generation != m_asyncScanGeneration)
        return;

    m_folders.clear();
    clearFolderNoteCounts();
    for (const FolderEntry &folder : listing.folders)
        m_folders.insert(folder.relPath, folder);

    m_notes.clear();
    m_notes.reserve(listing.entries.size());
    m_folderOwnNoteCounts.reserve(listing.entries.size());
    m_folderRecursiveNoteCounts.reserve(listing.entries.size());
    for (const NoteEntry &entry : listing.entries)
        insertNoteEntry(entry.relPath, entry);

    m_indexDirty = m_indexDirty || listing.indexDirty;
    loadCollectionFile();

    // Publish filename placeholders/cached entries before parsing bodies, so
    // startup can open a first note without waiting for collection indexing.
    bump();

    if (listing.tasks.isEmpty()) {
        saveIndexFileIfDirty();
        setScanInProgress(false);
        PerfLog::instance().record(
            QStringLiteral("startup.scan"),
            m_asyncScanTimer.elapsed(),
            QVariantMap{
                {QStringLiteral("path"), m_rootPath},
                {QStringLiteral("notes"), noteCount()},
                {QStringLiteral("folders"), m_folders.size()},
                {QStringLiteral("async"), true},
                {QStringLiteral("parsed"), 0},
            });
        emit scanFinished();
        return;
    }

    m_asyncParseGeneration = listing.generation;
    m_asyncWatcher.setFuture(QtConcurrent::mapped(listing.tasks, parseIndexTask));
}

void NoteCollection::applyAsyncIndexResult(int index)
{
    const AsyncIndexResult result = m_asyncWatcher.resultAt(index);
    if (result.generation != m_asyncScanGeneration)
        return;
    if (!result.ok)
        return;

    const QString absPath = absolutePath(result.relPath);
    const QFileInfo info(absPath);
    if (!info.exists() || !info.isFile())
        return;
    if (info.size() != result.entry.fileSize
        || dateTimeMs(info.lastModified()) != dateTimeMs(result.entry.modified))
        return;

    if (m_indexParseObserver)
        m_indexParseObserver(result.relPath);

    ensureFolderEntriesFor(result.entry.folder);
    insertNoteEntry(result.relPath, result.entry);
    markIndexDirty();
    ++m_asyncPendingUpdates;
    if (!m_asyncRevisionTimer.isActive())
        m_asyncRevisionTimer.start();
}

void NoteCollection::finishAsyncScan()
{
    if (m_asyncParseGeneration != m_asyncScanGeneration)
        return;
    m_asyncParseGeneration = 0;
    flushAsyncIndexUpdates();
    setScanInProgress(false);
    if (m_asyncSavedNotePendingFlush) {
        saveIndexFileIfDirtyAsync();
        m_asyncSavedNotePendingFlush = false;
    } else {
        saveIndexFileIfDirty();
    }
    PerfLog::instance().record(
        QStringLiteral("startup.scan"),
        m_asyncScanTimer.isValid() ? m_asyncScanTimer.elapsed() : 0,
        QVariantMap{
            {QStringLiteral("path"), m_rootPath},
            {QStringLiteral("notes"), noteCount()},
            {QStringLiteral("folders"), m_folders.size()},
            {QStringLiteral("async"), true},
        });
    syncSearchIndex();
    emit scanFinished();
}

void NoteCollection::flushAsyncIndexUpdates()
{
    if (m_asyncRevisionTimer.isActive())
        m_asyncRevisionTimer.stop();
    // Keep async revision batches cheap; finishAsyncScan() persists the sidecar
    // once after the background parse is complete.
    if (m_asyncPendingUpdates <= 0) {
        if (!m_scanInProgress) {
            if (m_asyncSavedNotePendingFlush) {
                saveIndexFileIfDirtyAsync();
                m_asyncSavedNotePendingFlush = false;
            } else {
                saveIndexFileIfDirty();
            }
        }
        return;
    }

    m_asyncPendingUpdates = 0;
    if (!m_scanInProgress) {
        if (m_asyncSavedNotePendingFlush) {
            saveIndexFileIfDirtyAsync();
            m_asyncSavedNotePendingFlush = false;
        } else {
            saveIndexFileIfDirty();
        }
    }
    bump();
}

bool NoteCollection::listingWatcherIsRunningForTesting() const
{
    return m_asyncListingWatcher.isRunning();
}

bool NoteCollection::refreshWatcherIsRunningForTesting() const
{
    return m_asyncRefreshWatcher.isRunning();
}

void NoteCollection::cancelAsyncScan()
{
    ++m_asyncScanGeneration;
    if (m_asyncRevisionTimer.isActive())
        m_asyncRevisionTimer.stop();
    m_asyncPendingUpdates = 0;
    m_asyncParseGeneration = 0;
    // Signal first: the listing walk polls this and returns early, so the
    // wait below is bounded by the gap between two checks rather than by the
    // rest of the vault.
    if (m_scanCancel)
        m_scanCancel->cancel();
    m_scanCancel.reset();
    if (m_asyncListingWatcher.isRunning()) {
        const QSignalBlocker blocker(&m_asyncListingWatcher);
        m_asyncListingWatcher.cancel();
        m_asyncListingWatcher.waitForFinished();
    }
    if (m_asyncWatcher.isRunning()) {
        const QSignalBlocker blocker(&m_asyncWatcher);
        m_asyncWatcher.cancel();
        m_asyncWatcher.waitForFinished();
    }
    setScanInProgress(false);
}

void NoteCollection::startAsyncDirectoryRefresh(const QStringList &relDirs)
{
    if (relDirs.isEmpty() || !isOpen())
        return;

    if (m_asyncRefreshWatcher.isRunning()) {
        const QSignalBlocker blocker(&m_asyncRefreshWatcher);
        ++m_asyncRefreshGeneration;
        m_asyncRefreshWatcher.cancel();
        m_asyncRefreshWatcher.waitForFinished();
    }

    AsyncRefreshRequest request;
    request.rootPath = m_rootPath;
    request.relDirs = relDirs;
    request.currentNotes = m_notes;
    request.generation = ++m_asyncRefreshGeneration;
    m_refreshCancel = makeCancellationToken();
    request.cancel = m_refreshCancel;
    m_asyncRefreshTimer.start();
    m_asyncRefreshWatcher.setFuture(
        QtConcurrent::run(&NoteCollection::buildAsyncRefreshResult, request));
}

void NoteCollection::applyAsyncRefreshResult()
{
    if (!hasDeliverableResult(m_asyncRefreshWatcher))
        return;
    const AsyncRefreshResult result = m_asyncRefreshWatcher.result();
    // A stopped walk saw only part of the subtree, and seenNotes drives
    // removals below: applying it would drop every note the walk had not
    // reached yet.
    if (result.cancelled)
        return;
    if (!isOpen() || result.rootPath != m_rootPath
        || result.generation != m_asyncRefreshGeneration) {
        return;
    }

    bool changed = false;

    auto isUnderDirectory = [](const QString &relPath, const QString &relDir) {
        return relPath.startsWith(relDir + QLatin1Char('/'));
    };
    auto isFolderOrChild = [&isUnderDirectory](const QString &relPath,
                                               const QString &relDir) {
        return relPath == relDir || isUnderDirectory(relPath, relDir);
    };
    auto removeIndexedNote = [&](const QString &relPath) {
        const QString folderPath = m_notes.value(relPath).folder;
        removeNoteEntry(relPath);
        m_manualOrder[folderPath].removeAll(nameOfRelPath(relPath));
        if (m_lastOpenNote == relPath)
            m_lastOpenNote.clear();
        emit noteRemoved(relPath);
        markIndexDirty();
        changed = true;
    };
    auto removeIndexedDirectory = [&](const QString &relDir) {
        const QString absDir = absolutePath(relDir);
        const QFileInfo currentInfo(absDir);
        if (currentInfo.exists() && currentInfo.isDir())
            return;

        const QString prefix = relDir + QLatin1Char('/');
        const QStringList noteKeys = m_notes.keys();
        for (const QString &key : noteKeys) {
            if (key.startsWith(prefix))
                removeIndexedNote(key);
        }

        const QStringList folderKeys = m_folders.keys();
        for (const QString &key : folderKeys) {
            if (!isFolderOrChild(key, relDir))
                continue;
            m_folders.remove(key);
            changed = true;
        }

        const QStringList orderKeys = m_manualOrder.keys();
        for (const QString &key : orderKeys) {
            if (isFolderOrChild(key, relDir))
                m_manualOrder.remove(key);
        }
    };

    for (const QString &relDir : result.missingDirs)
        removeIndexedDirectory(relDir);

    for (const FolderEntry &folder : result.folders) {
        const int before = m_folders.size();
        ensureFolderEntriesFor(folder.relPath);
        if (m_folders.size() != before)
            changed = true;
    }

    for (const NoteEntry &entry : result.entries) {
        const QString absPath = absolutePath(entry.relPath);
        const QFileInfo info(absPath);
        if (!info.exists() || !info.isFile())
            continue;
        if (info.size() != entry.fileSize
            || dateTimeMs(info.lastModified()) != dateTimeMs(entry.modified)) {
            continue;
        }

        if (m_indexParseObserver)
            m_indexParseObserver(entry.relPath);

        ensureFolderEntriesFor(entry.folder);
        insertNoteEntry(entry.relPath, entry);
        markIndexDirty();
        changed = true;
    }

    const QStringList noteKeys = m_notes.keys();
    for (const QString &key : noteKeys) {
        bool underScannedDirectory = false;
        for (const QString &relDir : result.relDirs) {
            if (!result.missingDirs.contains(relDir)
                && isUnderDirectory(key, relDir)) {
                underScannedDirectory = true;
                break;
            }
        }
        if (!underScannedDirectory || result.seenNotes.contains(key))
            continue;
        if (QFileInfo::exists(absolutePath(key)))
            continue;
        removeIndexedNote(key);
    }

    const QStringList folderKeys = m_folders.keys();
    for (const QString &key : folderKeys) {
        bool underScannedDirectory = false;
        for (const QString &relDir : result.relDirs) {
            if (!result.missingDirs.contains(relDir)
                && isFolderOrChild(key, relDir)) {
                underScannedDirectory = true;
                break;
            }
        }
        if (!underScannedDirectory || result.seenFolders.contains(key))
            continue;
        const QFileInfo info(absolutePath(key));
        if (info.exists() && info.isDir())
            continue;
        m_folders.remove(key);
        changed = true;
    }

    const QStringList orderKeys = m_manualOrder.keys();
    for (const QString &key : orderKeys) {
        bool underScannedDirectory = false;
        for (const QString &relDir : result.relDirs) {
            if (isFolderOrChild(key, relDir)) {
                underScannedDirectory = true;
                break;
            }
        }
        if (underScannedDirectory && !key.isEmpty() && !m_folders.contains(key))
            m_manualOrder.remove(key);
    }

    if (changed) {
        saveIndexFileIfDirty();
        bump();
        syncSearchIndex();
    }

    PerfLog::instance().record(
        QStringLiteral("collection.refresh"),
        m_asyncRefreshTimer.isValid() ? m_asyncRefreshTimer.elapsed() : 0,
        QVariantMap{
            {QStringLiteral("path"), m_rootPath},
            {QStringLiteral("dirs"), result.relDirs.size()},
            {QStringLiteral("notes"), noteCount()},
            {QStringLiteral("folders"), m_folders.size()},
            {QStringLiteral("async"), true},
            {QStringLiteral("changed"), changed},
        });
}

void NoteCollection::cancelAsyncRefresh()
{
    ++m_asyncRefreshGeneration;
    if (m_refreshCancel)
        m_refreshCancel->cancel();
    m_refreshCancel.reset();
    if (m_asyncRefreshWatcher.isRunning()) {
        const QSignalBlocker blocker(&m_asyncRefreshWatcher);
        m_asyncRefreshWatcher.cancel();
        m_asyncRefreshWatcher.waitForFinished();
    }
}

void NoteCollection::startAsyncSavedNoteIndex(AsyncSavedNoteTask task)
{
    if (!isOpen())
        return;

    task.generation = m_asyncSavedNoteGeneration;
    if (m_asyncSavedNoteWatcher.isRunning()) {
        for (AsyncSavedNoteTask &pending : m_pendingSavedNoteTasks) {
            if (pending.relPath == task.relPath) {
                pending = std::move(task);
                return;
            }
        }
        m_pendingSavedNoteTasks.append(std::move(task));
        return;
    }

    m_asyncSavedNoteWatcher.setFuture(
        QtConcurrent::run(&NoteCollection::parseSavedNoteTask,
                          std::move(task)));
}

void NoteCollection::applyAsyncSavedNoteResult()
{
    // Skip the apply but still drain the pending queue below. This was the
    // first place the resultless-finished crash was observed (a SIGSEGV in
    // the integration suite's tag-rename test under WSLg); the shared check
    // is now what all four handlers use.
    const bool hasResult = hasDeliverableResult(m_asyncSavedNoteWatcher);
    const AsyncIndexResult result =
        hasResult ? m_asyncSavedNoteWatcher.result() : AsyncIndexResult();

    if (hasResult && isOpen() && result.ok
        && result.generation == m_asyncSavedNoteGeneration) {
        PerfLog::ScopedTimer perf(
            QStringLiteral("collection.note_saved.apply"),
            QVariantMap{{QStringLiteral("path"), absolutePath(result.relPath)},
                        {QStringLiteral("relPath"), result.relPath}});

        const QString absPath = absolutePath(result.relPath);
        const QFileInfo info(absPath);
        if (info.exists() && info.isFile()
            && info.size() == result.entry.fileSize
            && dateTimeMs(info.lastModified())
                == dateTimeMs(result.entry.modified)) {
            if (m_indexParseObserver)
                m_indexParseObserver(result.relPath);

            ensureFolderEntriesFor(result.entry.folder);
            insertNoteEntry(result.relPath, result.entry);
            markIndexDirty();
            ++m_asyncPendingUpdates;
            m_asyncSavedNotePendingFlush = true;
            if (!m_asyncRevisionTimer.isActive())
                m_asyncRevisionTimer.start();
            perf.addContext(QStringLiteral("applied"), true);
        } else {
            perf.addContext(QStringLiteral("applied"), false);
            perf.addContext(QStringLiteral("stale"), true);
        }
    }

    if (!m_pendingSavedNoteTasks.isEmpty()) {
        AsyncSavedNoteTask task = std::move(m_pendingSavedNoteTasks.first());
        m_pendingSavedNoteTasks.removeFirst();
        m_asyncSavedNoteWatcher.setFuture(
            QtConcurrent::run(&NoteCollection::parseSavedNoteTask,
                              std::move(task)));
    }
}

void NoteCollection::cancelAsyncSavedNote()
{
    ++m_asyncSavedNoteGeneration;
    m_pendingSavedNoteTasks.clear();
    if (m_asyncSavedNoteWatcher.isRunning()) {
        const QSignalBlocker blocker(&m_asyncSavedNoteWatcher);
        m_asyncSavedNoteWatcher.cancel();
        m_asyncSavedNoteWatcher.waitForFinished();
    }
}

void NoteCollection::saveIndexFileIfDirtyAsync()
{
    if (!m_indexDirty)
        return;

    const QString path = indexFilePath();
    if (path.isEmpty())
        return;

    if (m_notes.isEmpty() && !QFileInfo::exists(path)) {
        m_indexDirty = false;
        return;
    }

    const QString dir = m_rootPath + QLatin1Char('/') + kvitDirName;
    QDir().mkpath(dir);

    AsyncIndexSaveRequest request;
    request.path = path;
    request.notes = m_notes;
    request.generation = m_asyncIndexSaveGeneration;
    m_indexDirty = false;
    startAsyncIndexSave(std::move(request));
}

void NoteCollection::startAsyncIndexSave(AsyncIndexSaveRequest request)
{
    if (request.path.isEmpty())
        return;

    request.generation = m_asyncIndexSaveGeneration;
    if (m_asyncIndexSaveWatcher.isRunning()) {
        m_pendingIndexSaveRequest = std::move(request);
        m_indexSaveQueued = true;
        return;
    }

    m_asyncIndexSaveWatcher.setFuture(
        QtConcurrent::run(&NoteCollection::writeIndexFileSnapshot,
                          std::move(request)));
}

void NoteCollection::applyAsyncIndexSaveResult()
{
    // A cancelled index save produced nothing, but the queue below still has
    // to drain, so this cannot return early the way the others do.
    const bool hasResult = hasDeliverableResult(m_asyncIndexSaveWatcher);
    const AsyncIndexSaveResult result =
        hasResult ? m_asyncIndexSaveWatcher.result() : AsyncIndexSaveResult();
    if (hasResult && result.generation == m_asyncIndexSaveGeneration
        && !result.ok && !m_indexSaveQueued)
        m_indexDirty = true;

    if (m_indexSaveQueued) {
        AsyncIndexSaveRequest request = std::move(m_pendingIndexSaveRequest);
        m_indexSaveQueued = false;
        m_asyncIndexSaveWatcher.setFuture(
            QtConcurrent::run(&NoteCollection::writeIndexFileSnapshot,
                              std::move(request)));
    }
}

void NoteCollection::cancelAsyncIndexSave()
{
    ++m_asyncIndexSaveGeneration;

    // saveIndexFileIfDirtyAsync clears the dirty flag BEFORE the write starts,
    // and leaves it to applyAsyncIndexSaveResult to set it back when the write
    // fails. Cancelling suppresses that handler and discards any queued
    // request too, so whatever those carried would be forgotten: the
    // collection would believe the sidecar on disk matches memory when nothing
    // had been written.
    //
    // refresh() cancels while the collection stays open, so this is ordinary
    // use, not only shutdown. Restoring the flag here rather than in a result
    // handler is deliberate — the handler is precisely what cancellation stops
    // running. Being wrong in this direction costs one redundant rewrite;
    // being wrong in the other leaves a stale index nothing will ever correct.
    const bool abandonedUnwrittenWork =
        m_indexSaveQueued || m_asyncIndexSaveWatcher.isRunning();

    m_indexSaveQueued = false;
    if (m_asyncIndexSaveWatcher.isRunning()) {
        const QSignalBlocker blocker(&m_asyncIndexSaveWatcher);
        m_asyncIndexSaveWatcher.cancel();
        m_asyncIndexSaveWatcher.waitForFinished();
    }

    if (abandonedUnwrittenWork)
        m_indexDirty = true;
}

// ----------------------------------------------------------- wiki-links

QStringList NoteCollection::extractWikiLinks(const QString &body)
{
    QStringList targets;
    const QList<WikiLinkScanner::Occurrence> occurrences =
        WikiLinkScanner::scan(body);
    for (const WikiLinkScanner::Occurrence &occurrence : occurrences) {
        if (!occurrence.note.isEmpty()) // bare [[#heading]] is same-note only
            targets.append(occurrence.rawTarget);
    }
    return targets;
}

int NoteCollection::rewriteWikiTargetsInText(QString *text,
                                             const QSet<QString> &oldKeys,
                                             const QString &replacement)
{
    if (!text || oldKeys.isEmpty())
        return 0;
    QList<WikiLinkScanner::Occurrence> replacements;
    const QList<WikiLinkScanner::Occurrence> occurrences =
        WikiLinkScanner::scan(*text);
    for (const WikiLinkScanner::Occurrence &occurrence : occurrences) {
        QString key = occurrence.note.toLower();
        if (key.endsWith(mdSuffix))
            key.chop(mdSuffix.size());
        if (!key.isEmpty() && oldKeys.contains(key))
            replacements.append(occurrence);
    }
    for (int i = replacements.size() - 1; i >= 0; --i) {
        const WikiLinkScanner::Occurrence &occurrence = replacements.at(i);
        text->replace(occurrence.noteStart, occurrence.noteLength, replacement);
    }
    return replacements.size();
}

QHash<QString, QSet<QString>> NoteCollection::collectWikiReferrers(
    const QString &relPath) const
{
    QHash<QString, QSet<QString>> referrers;
    for (auto it = m_notes.constBegin(); it != m_notes.constEnd(); ++it) {
        QSet<QString> keys;
        for (const QString &raw : it->links) {
            QString notePart = raw;
            const int hash = notePart.indexOf(QLatin1Char('#'));
            if (hash >= 0)
                notePart = notePart.left(hash);
            notePart = notePart.trimmed();
            if (notePart.isEmpty())
                continue;
            if (resolveWikiTarget(notePart) != relPath)
                continue;
            QString key = notePart.toLower();
            if (key.endsWith(mdSuffix))
                key.chop(mdSuffix.size());
            keys.insert(key);
        }
        if (!keys.isEmpty())
            referrers.insert(it.key(), keys);
    }
    return referrers;
}

QHash<QString, NoteCollection::RewriteSnapshot>
NoteCollection::snapshotNoteReferrers(const QString &relPath) const
{
    QHash<QString, RewriteSnapshot> snapshots;
    const auto referrers = collectWikiReferrers(relPath);
    for (auto it = referrers.constBegin(); it != referrers.constEnd(); ++it) {
        bool ok = false;
        const QByteArray bytes = readFileBytes(absolutePath(it.key()), &ok);
        if (!ok)
            continue;
        RewriteSnapshot snapshot;
        snapshot.keys = it.value();
        snapshot.hash = contentHash(bytes);
        snapshot.modified = QFileInfo(absolutePath(it.key())).lastModified();
        // Scan the file text just read for content hashing, not a resident body
        // cache.
        const QString referrerBody =
            NoteFrontMatter::split(QString::fromUtf8(bytes)).body;
        for (const WikiLinkScanner::Occurrence &occurrence :
             WikiLinkScanner::scan(referrerBody)) {
            QString key = occurrence.note.toLower();
            if (key.endsWith(mdSuffix))
                key.chop(mdSuffix.size());
            if (snapshot.keys.contains(key))
                ++snapshot.linkCount;
        }
        snapshots.insert(it.key(), snapshot);
    }
    return snapshots;
}

QHash<QString, NoteCollection::RewriteSnapshot>
NoteCollection::snapshotFolderReferrers(const QString &oldPrefix) const
{
    QHash<QString, RewriteSnapshot> snapshots;
    const QString lowered = oldPrefix.toLower() + QLatin1Char('/');
    for (auto it = m_notes.constBegin(); it != m_notes.constEnd(); ++it) {
        // Prefilter with the resident wiki-link targets so only notes that may
        // link under the folder are read from disk.
        bool candidate = false;
        for (const QString &raw : it->links) {
            QString notePart = raw;
            const int hash = notePart.indexOf(QLatin1Char('#'));
            if (hash >= 0)
                notePart = notePart.left(hash);
            while (notePart.startsWith(QLatin1Char('/')))
                notePart.remove(0, 1);
            if (notePart.toLower().startsWith(lowered)) {
                candidate = true;
                break;
            }
        }
        if (!candidate)
            continue;

        // Read the candidate once and take the accurate count from its body.
        bool ok = false;
        const QByteArray bytes = readFileBytes(absolutePath(it.key()), &ok);
        if (!ok)
            continue;
        const QString body =
            NoteFrontMatter::split(QString::fromUtf8(bytes)).body;
        int count = 0;
        for (const WikiLinkScanner::Occurrence &occurrence :
             WikiLinkScanner::scan(body)) {
            QString note = occurrence.note;
            while (note.startsWith(QLatin1Char('/')))
                note.remove(0, 1);
            if (note.toLower().startsWith(lowered))
                ++count;
        }
        if (count == 0)
            continue;
        RewriteSnapshot snapshot;
        snapshot.hash = contentHash(bytes);
        snapshot.modified = QFileInfo(absolutePath(it.key())).lastModified();
        snapshot.linkCount = count;
        snapshots.insert(it.key(), snapshot);
    }
    return snapshots;
}

QVariantMap NoteCollection::renamePlanMap(const RenamePlan &plan) const
{
    int links = 0;
    QStringList files = plan.referrers.keys();
    files.sort();
    QVariantList details;
    for (const QString &file : files) {
        const RewriteSnapshot &snapshot = plan.referrers.value(file);
        links += snapshot.linkCount;
        details.append(QVariantMap{
            {QStringLiteral("relPath"), file},
            {QStringLiteral("linkCount"), snapshot.linkCount},
            {QStringLiteral("modified"), snapshot.modified},
            {QStringLiteral("hash"), QString::fromLatin1(snapshot.hash.toHex())},
        });
    }
    return {{QStringLiteral("ok"), !plan.id.isEmpty()},
            {QStringLiteral("id"), plan.id},
            {QStringLiteral("kind"), plan.kind},
            {QStringLiteral("oldPath"), plan.oldPath},
            {QStringLiteral("newPath"), plan.newPath},
            {QStringLiteral("linkCount"), links},
            {QStringLiteral("noteCount"), files.size()},
            {QStringLiteral("files"), details}};
}

QVariantMap NoteCollection::applyWikiLinkRewrites(
    const RenamePlan &plan, const QString &openRelPath, const QString &openBody)
{
    if (plan.referrers.isEmpty())
        return {{QStringLiteral("linkCount"), 0},
                {QStringLiteral("noteCount"), 0}};

    QString title = nameOfRelPath(plan.newPath);
    if (title.endsWith(mdSuffix, Qt::CaseInsensitive))
        title.chop(mdSuffix.size());
    QString replacement = title;
    if (resolveWikiTarget(title) != plan.newPath) {
        replacement = plan.newPath;
        if (replacement.endsWith(mdSuffix, Qt::CaseInsensitive))
            replacement.chop(mdSuffix.size());
    }

    int linkCount = 0;
    int noteCount = 0;
    int indexedNoteCount = 0;
    QStringList skipped;
    QStringList failed;
    QString rewrittenOpenBody;
    int openRewriteCount = 0;
    QStringList sorted = plan.referrers.keys();
    sorted.sort();
    for (const QString &referrer : sorted) {
        const RewriteSnapshot &snapshot = plan.referrers.value(referrer);
        QString actual = referrer;
        if ((plan.kind == QLatin1String("noteRename")
             || plan.kind == QLatin1String("noteMove"))
            && referrer == plan.oldPath) {
            actual = plan.newPath;
        } else if (plan.kind == QLatin1String("folderRename")
                   && (referrer == plan.oldFolderPrefix
                       || referrer.startsWith(plan.oldFolderPrefix
                                              + QLatin1Char('/')))) {
            actual = plan.newFolderPrefix
                + referrer.mid(plan.oldFolderPrefix.size());
        }

        const bool isOpen = !openRelPath.isEmpty()
            && (openRelPath == referrer || openRelPath == actual);
        bool ok = false;
        const QByteArray bytes = readFileBytes(absolutePath(actual), &ok);
        if (!ok) {
            failed.append(actual);
            continue;
        }
        if (!isOpen && contentHash(bytes) != snapshot.hash) {
            skipped.append(actual);
            continue;
        }

        QString body;
        QString frontMatter;
        if (isOpen) {
            body = openBody;
        } else {
            const NoteFrontMatter::Split split =
                NoteFrontMatter::split(QString::fromUtf8(bytes));
            frontMatter = split.block;
            body = split.body;
        }

        int n = 0;
        if (plan.kind == QLatin1String("folderRename")) {
            QList<WikiLinkScanner::Occurrence> replacements;
            const QString oldLower = plan.oldFolderPrefix.toLower()
                + QLatin1Char('/');
            for (const WikiLinkScanner::Occurrence &occurrence :
                 WikiLinkScanner::scan(body)) {
                QString note = occurrence.note;
                bool leadingSlash = note.startsWith(QLatin1Char('/'));
                while (note.startsWith(QLatin1Char('/')))
                    note.remove(0, 1);
                if (!note.toLower().startsWith(oldLower))
                    continue;
                WikiLinkScanner::Occurrence repl = occurrence;
                repl.note = (leadingSlash ? QStringLiteral("/") : QString())
                    + plan.newFolderPrefix
                    + note.mid(plan.oldFolderPrefix.size());
                replacements.append(repl);
            }
            for (int i = replacements.size() - 1; i >= 0; --i) {
                const auto &repl = replacements.at(i);
                body.replace(repl.noteStart, repl.noteLength, repl.note);
            }
            n = replacements.size();
        } else {
            QSet<QString> staleKeys;
            for (const QString &key : snapshot.keys) {
                if (resolveWikiTarget(key) != plan.newPath)
                    staleKeys.insert(key);
            }
            n = rewriteWikiTargetsInText(&body, staleKeys, replacement);
        }
        if (n <= 0)
            continue;

        if (isOpen) {
            rewrittenOpenBody = body;
            openRewriteCount = n;
        } else if (!writeTextFileAtomic(absolutePath(actual), frontMatter + body)) {
            failed.append(actual);
            continue;
        } else {
            indexNote(actual);
            reindexNoteInSearch(actual);
            ++indexedNoteCount;
        }
        linkCount += n;
        ++noteCount;
    }
    if (noteCount > 0)
        emit wikiLinksRewritten(linkCount, noteCount);
    if (indexedNoteCount > 0) {
        markIndexDirty();
        saveIndexFileIfDirty();
        bump();
    }
    if (!skipped.isEmpty() || !failed.isEmpty())
        emit wikiLinkRewriteIncomplete(skipped, failed);
    return {{QStringLiteral("linkCount"), linkCount},
            {QStringLiteral("noteCount"), noteCount},
            {QStringLiteral("skipped"), skipped},
            {QStringLiteral("failed"), failed},
            {QStringLiteral("openBody"), rewrittenOpenBody},
            {QStringLiteral("openRewriteCount"), openRewriteCount}};
}

void NoteCollection::ensureWikiIndex() const
{
    if (m_wikiIndexRevision == m_revision
        && m_wikiIndexNoteCount == m_notes.size())
        return;
    m_wikiBasenames.clear();
    for (auto it = m_notes.constBegin(); it != m_notes.constEnd(); ++it) {
        QString base = nameOfRelPath(it.key());
        if (base.endsWith(mdSuffix, Qt::CaseInsensitive))
            base.chop(mdSuffix.size());
        m_wikiBasenames[base.toLower()].append(it.key());
    }
    m_wikiIndexRevision = m_revision;
    m_wikiIndexNoteCount = m_notes.size();
}

QString NoteCollection::resolveWikiTarget(const QString &target) const
{
    const QVariantMap result = wikiTargetResolution(target);
    return result.value(QStringLiteral("status")) == QLatin1String("unique")
        ? result.value(QStringLiteral("relPath")).toString() : QString();
}

QVariantMap NoteCollection::wikiTargetResolution(const QString &target) const
{
    QString wanted = target.trimmed();
    const int hash = wanted.indexOf(QLatin1Char('#'));
    if (hash >= 0)
        wanted = wanted.left(hash).trimmed();
    if (wanted.endsWith(mdSuffix, Qt::CaseInsensitive))
        wanted.chop(mdSuffix.size());
    while (wanted.startsWith(QLatin1Char('/')))
        wanted.remove(0, 1);
    if (wanted.isEmpty())
        return {{QStringLiteral("status"), QStringLiteral("missing")},
                {QStringLiteral("relPath"), QString()},
                {QStringLiteral("candidates"), QStringList()}};

    ensureWikiIndex();
    const QString lowered = wanted.toLower();
    const int slash = lowered.lastIndexOf(QLatin1Char('/'));
    const QString base = slash >= 0 ? lowered.mid(slash + 1) : lowered;

    QStringList matches;
    const QStringList candidates = m_wikiBasenames.value(base);
    for (const QString &relPath : candidates) {
        QString path = relPath;
        if (path.endsWith(mdSuffix, Qt::CaseInsensitive))
            path.chop(mdSuffix.size());
        const QString lowerPath = path.toLower();
        if (lowerPath != lowered
            && !lowerPath.endsWith(QLatin1Char('/') + lowered))
            continue;
        matches.append(relPath);
    }
    matches.sort(Qt::CaseInsensitive);
    const QString status = matches.isEmpty() ? QStringLiteral("missing")
        : matches.size() == 1 ? QStringLiteral("unique")
                              : QStringLiteral("ambiguous");
    return {{QStringLiteral("status"), status},
            {QStringLiteral("relPath"), matches.size() == 1
                 ? matches.first() : QString()},
            {QStringLiteral("candidates"), matches}};
}

QStringList NoteCollection::linksFrom(const QString &relPath) const
{
    const NoteEntry *entry = note(relPath);
    return entry ? entry->links : QStringList();
}

QString NoteCollection::readNoteBody(const QString &relPath) const
{
    if (!note(relPath))
        return QString();
    bool ok = false;
    const QString text = readTextFile(absolutePath(relPath), &ok);
    if (!ok)
        return QString();
    return NoteFrontMatter::split(text).body;
}

QStringList NoteCollection::headingsFor(const QString &relPath) const
{
    const NoteEntry *entry = note(relPath);
    if (!entry)
        return {};
    QStringList headings;
    bool inFence = false;
    // Read only this note, not a resident body cache.
    const QStringList lines = readNoteBody(relPath).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("```"))
            || trimmed.startsWith(QLatin1String("~~~"))) {
            inFence = !inFence;
            continue;
        }
        if (inFence || !trimmed.startsWith(QLatin1Char('#')))
            continue;
        int level = 0;
        while (level < trimmed.size()
               && trimmed.at(level) == QLatin1Char('#'))
            ++level;
        if (level > 6 || level >= trimmed.size()
            || trimmed.at(level) != QLatin1Char(' '))
            continue;
        const QString text = trimmed.mid(level + 1).trimmed();
        if (!text.isEmpty())
            headings.append(text);
    }
    return headings;
}

QVariantList NoteCollection::backlinksTo(const QString &relPath) const
{
    QVariantList out;
    if (relPath.isEmpty())
        return out;

    QStringList paths = m_notes.keys();
    paths.sort();
    for (const QString &referrer : paths) {
        if (referrer == relPath)
            continue;
        const NoteEntry &entry = m_notes[referrer];
        int count = 0;
        for (const QString &raw : entry.links) {
            if (resolveWikiTarget(raw) == relPath)
                ++count;
        }
        if (count == 0)
            continue;

        // Context lines: the referrer's raw body lines whose links resolve
        // to the target — the surrounding text the panel shows per match. Only
        // the notes that actually refer here are read, and only after the
        // resident links established count > 0.
        const QString body = readNoteBody(referrer);
        QStringList contexts;
        QSet<int> contextLineStarts;
        for (const WikiLinkScanner::Occurrence &occurrence :
             WikiLinkScanner::scan(body)) {
            if (occurrence.note.isEmpty()
                || resolveWikiTarget(occurrence.rawTarget) != relPath)
                continue;
            int start = body.lastIndexOf(QLatin1Char('\n'),
                                         occurrence.start - 1);
            start = start < 0 ? 0 : start + 1;
            if (contextLineStarts.contains(start))
                continue;
            contextLineStarts.insert(start);
            int end = body.indexOf(QLatin1Char('\n'), occurrence.start);
            if (end < 0)
                end = body.size();
            contexts.append(body.mid(start, end - start).trimmed().left(200));
        }

        out.append(QVariantMap{
            {QStringLiteral("relPath"), referrer},
            {QStringLiteral("title"), entry.title},
            {QStringLiteral("count"), count},
            {QStringLiteral("contexts"), contexts},
        });
    }
    return out;
}

// ----------------------------------------------------------- queries

QStringList NoteCollection::noteRelPaths() const
{
    QStringList paths = m_notes.keys();
    paths.sort();
    return paths;
}

QStringList NoteCollection::notesInFolder(const QString &folder) const
{
    return manualOrder(folder);
}

const NoteCollection::NoteEntry *NoteCollection::note(const QString &relPath) const
{
    auto it = m_notes.constFind(relPath);
    return it == m_notes.constEnd() ? nullptr : &it.value();
}

QStringList NoteCollection::folderRelPaths() const
{
    return m_folders.keys(); // QMap keys are sorted
}

const NoteCollection::FolderEntry *NoteCollection::folder(const QString &relPath) const
{
    auto it = m_folders.constFind(relPath);
    return it == m_folders.constEnd() ? nullptr : &it.value();
}

QVariantMap NoteCollection::noteInfo(const QString &relPath) const
{
    const NoteEntry *entry = note(relPath);
    if (!entry)
        return QVariantMap();
    return {
        {QStringLiteral("relPath"), entry->relPath},
        {QStringLiteral("folder"), entry->folder},
        {QStringLiteral("title"), entry->title},
        {QStringLiteral("snippet"), entry->snippet},
        // Bodies are no longer resident; read this note's saved body on demand
        // for the callers that need it, e.g. folder export.
        {QStringLiteral("body"), readNoteBody(relPath)},
        {QStringLiteral("wordCount"), entry->wordCount},
        {QStringLiteral("pinned"), entry->meta.pinned},
        {QStringLiteral("favorite"), entry->meta.favorite},
        {QStringLiteral("tags"), entry->meta.tags},
        {QStringLiteral("created"), entry->created},
        {QStringLiteral("modified"), entry->modified},
    };
}

int NoteCollection::noteCountInFolder(const QString &folder, bool recursive) const
{
    if (recursive && folder.isEmpty())
        return m_notes.size();

    return recursive ? m_folderRecursiveNoteCounts.value(folder)
                     : m_folderOwnNoteCounts.value(folder);
}

// ------------------------------------------------------ note operations

bool NoteCollection::validName(const QString &name, QString *reason) const
{
    if (name.trimmed().isEmpty()) {
        *reason = tr("Name is empty");
        return false;
    }
    if (name != name.trimmed()) {
        *reason = tr("Name has leading or trailing spaces");
        return false;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        *reason = tr("Name cannot contain slashes");
        return false;
    }
    if (name.startsWith(QLatin1Char('.'))) {
        *reason = tr("Name cannot start with a dot");
        return false;
    }
    return true;
}

QString NoteCollection::uniqueUntitled(const QString &folder) const
{
    const QString base = tr("Untitled");
    QString name = base;
    int n = 2;
    while (QFileInfo::exists(absolutePath(joinRelPath(folder, name + mdSuffix))))
        name = base + QLatin1Char(' ') + QString::number(n++);
    return name;
}

QString NoteCollection::createNote(const QString &folder, const QString &title,
                                   const QString &body)
{
    if (!isOpen())
        return QString();
    if (!folder.isEmpty() && !m_folders.contains(folder)) {
        emit operationFailed(tr("No such folder: %1").arg(folder));
        return QString();
    }

    QString name = title.trimmed();
    if (name.isEmpty()) {
        name = uniqueUntitled(folder);
    } else {
        QString reason;
        if (!validName(name, &reason)) {
            emit operationFailed(reason);
            return QString();
        }
    }

    const QString relPath = joinRelPath(folder, name + mdSuffix);
    if (!ensureWithinRoot(relPath))
        return QString();
    if (QFileInfo::exists(absolutePath(relPath))) {
        emit operationFailed(tr("A note named \"%1\" already exists").arg(name));
        return QString();
    }
    if (!writeTextFileAtomic(absolutePath(relPath), body)) {
        emit operationFailed(tr("Cannot create note \"%1\"").arg(name));
        return QString();
    }

    // Record the new note at the end of the folder's order. The current
    // reconciled order is materialized first so the note lands last even
    // when created-time ties make reconciliation ambiguous.
    QStringList names;
    const QStringList order = manualOrder(folder);
    for (const QString &path : order)
        names.append(nameOfRelPath(path));
    names.append(name + mdSuffix);

    indexNote(relPath);
    saveIndexFileIfDirty();
    m_manualOrder.insert(folder, names);
    saveCollectionFile();
    bump();
    reindexNoteInSearch(relPath);
    return relPath;
}

QString NoteCollection::captureNote(const QString &text)
{
    if (!isOpen())
        return QString();

    // A title from the first non-empty line, sanitized to a valid name and
    // capped; anything unusable falls back to an Untitled name.
    QString title;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString t = line.trimmed();
        if (t.isEmpty())
            continue;
        QString candidate = t;
        candidate.remove(QLatin1Char('/'));
        candidate.remove(QLatin1Char('\\'));
        while (candidate.startsWith(QLatin1Char('.')))
            candidate.remove(0, 1);
        candidate = candidate.trimmed().left(60);
        QString reason;
        if (validName(candidate, &reason))
            title = candidate;
        break;
    }

    // The captured text is usually the only copy — the window holds no
    // draft and the user typed it seconds ago — so the note reaches disk in
    // ONE write. Creating an empty note first and filling it afterwards left
    // a window where the create succeeded and the body write did not, which
    // published an empty note and reported failure at the same time.
    QString relPath = createNote(QString(), title, text);
    if (relPath.isEmpty())                       // collision or invalid name
        relPath = createNote(QString(), QString(), text);
    if (relPath.isEmpty())
        return QString();

    indexNote(relPath);
    saveIndexFileIfDirty();
    bump();
    reindexNoteInSearch(relPath);
    return relPath;
}

QVariantMap NoteCollection::planNoteRename(const QString &relPath,
                                           const QString &newTitle)
{
    const NoteEntry *entry = note(relPath);
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return {{QStringLiteral("ok"), false}};
    }
    const QString name = newTitle.trimmed();
    QString reason;
    if (!validName(name, &reason)) {
        emit operationFailed(reason);
        return {{QStringLiteral("ok"), false}};
    }
    const QString newRelPath = joinRelPath(entry->folder, name + mdSuffix);
    if (newRelPath != relPath && QFileInfo::exists(absolutePath(newRelPath))) {
        emit operationFailed(tr("A note named \"%1\" already exists").arg(name));
        return {{QStringLiteral("ok"), false}};
    }
    RenamePlan plan;
    plan.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.kind = QStringLiteral("noteRename");
    plan.oldPath = relPath;
    plan.argument = name;
    plan.newPath = newRelPath;
    if (newRelPath != relPath)
        plan.referrers = snapshotNoteReferrers(relPath);
    m_pendingRenamePlan = plan;
    return renamePlanMap(plan);
}

QVariantMap NoteCollection::planNoteMove(const QString &relPath,
                                         const QString &targetFolder)
{
    const NoteEntry *entry = note(relPath);
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return {{QStringLiteral("ok"), false}};
    }
    if (!targetFolder.isEmpty() && !m_folders.contains(targetFolder)) {
        emit operationFailed(tr("No such folder: %1").arg(targetFolder));
        return {{QStringLiteral("ok"), false}};
    }
    const QString newRelPath = joinRelPath(targetFolder, nameOfRelPath(relPath));
    if (newRelPath != relPath && QFileInfo::exists(absolutePath(newRelPath))) {
        emit operationFailed(tr("\"%1\" already exists in the target folder")
                             .arg(entry->title));
        return {{QStringLiteral("ok"), false}};
    }
    RenamePlan plan;
    plan.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.kind = QStringLiteral("noteMove");
    plan.oldPath = relPath;
    plan.argument = targetFolder;
    plan.newPath = newRelPath;
    if (newRelPath != relPath)
        plan.referrers = snapshotNoteReferrers(relPath);
    m_pendingRenamePlan = plan;
    return renamePlanMap(plan);
}

QVariantMap NoteCollection::planFolderRename(const QString &relPath,
                                             const QString &newName)
{
    const FolderEntry *entry = folder(relPath);
    if (!entry) {
        emit operationFailed(tr("No such folder: %1").arg(relPath));
        return {{QStringLiteral("ok"), false}};
    }
    const QString trimmed = newName.trimmed();
    QString reason;
    if (!validName(trimmed, &reason)) {
        emit operationFailed(reason);
        return {{QStringLiteral("ok"), false}};
    }
    const QString newRelPath = joinRelPath(folderOfRelPath(relPath), trimmed);
    if (newRelPath != relPath && QFileInfo::exists(absolutePath(newRelPath))) {
        emit operationFailed(tr("A folder named \"%1\" already exists").arg(trimmed));
        return {{QStringLiteral("ok"), false}};
    }
    RenamePlan plan;
    plan.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    plan.kind = QStringLiteral("folderRename");
    plan.oldPath = relPath;
    plan.argument = trimmed;
    plan.newPath = newRelPath;
    plan.oldFolderPrefix = relPath;
    plan.newFolderPrefix = newRelPath;
    if (newRelPath != relPath)
        plan.referrers = snapshotFolderReferrers(relPath);
    m_pendingRenamePlan = plan;
    return renamePlanMap(plan);
}

void NoteCollection::cancelRenamePlan(const QString &planId)
{
    if (m_pendingRenamePlan.id == planId)
        m_pendingRenamePlan = RenamePlan();
}

QVariantMap NoteCollection::applyRenamePlan(const QString &planId,
                                            bool updateLinks,
                                            const QString &openRelPath,
                                            const QString &openBody)
{
    if (planId.isEmpty() || m_pendingRenamePlan.id != planId)
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), tr("The rename plan has expired")}};

    const RenamePlan plan = m_pendingRenamePlan;
    bool operationOk = false;
    if (plan.oldPath == plan.newPath) {
        operationOk = true;
    } else if (plan.kind == QLatin1String("noteRename")) {
        operationOk = note(plan.oldPath)
            ? renameNote(plan.oldPath, plan.argument)
            : note(plan.newPath) != nullptr;
    } else if (plan.kind == QLatin1String("noteMove")) {
        operationOk = note(plan.oldPath)
            ? moveNote(plan.oldPath, plan.argument)
            : note(plan.newPath) != nullptr;
    } else if (plan.kind == QLatin1String("folderRename")) {
        operationOk = folder(plan.oldPath)
            ? renameFolder(plan.oldPath, plan.argument)
            : folder(plan.newPath) != nullptr;
    }
    if (!operationOk)
        return {{QStringLiteral("ok"), false}};

    QVariantMap result{{QStringLiteral("ok"), true},
                       {QStringLiteral("oldPath"), plan.oldPath},
                       {QStringLiteral("newPath"), plan.newPath},
                       {QStringLiteral("linkCount"), 0},
                       {QStringLiteral("noteCount"), 0}};
    if (updateLinks) {
        const QVariantMap rewrite =
            applyWikiLinkRewrites(plan, openRelPath, openBody);
        for (auto it = rewrite.constBegin(); it != rewrite.constEnd(); ++it)
            result.insert(it.key(), it.value());
        if (!rewrite.value(QStringLiteral("skipped")).toStringList().isEmpty()
            || !rewrite.value(QStringLiteral("failed")).toStringList().isEmpty()) {
            // An explicit Retry should operate on the latest bytes and is
            // idempotent because the scanner only replaces still-old targets.
            for (auto it = m_pendingRenamePlan.referrers.begin();
                 it != m_pendingRenamePlan.referrers.end(); ++it) {
                QString actual = it.key();
                if ((plan.kind == QLatin1String("noteRename")
                     || plan.kind == QLatin1String("noteMove"))
                    && actual == plan.oldPath)
                    actual = plan.newPath;
                else if (plan.kind == QLatin1String("folderRename")
                         && actual.startsWith(plan.oldFolderPrefix
                                              + QLatin1Char('/')))
                    actual = plan.newFolderPrefix
                        + actual.mid(plan.oldFolderPrefix.size());
                bool ok = false;
                const QByteArray bytes = readFileBytes(absolutePath(actual), &ok);
                if (ok) {
                    it->hash = contentHash(bytes);
                    it->modified = QFileInfo(absolutePath(actual)).lastModified();
                }
            }
            return result;
        }
    }
    m_pendingRenamePlan = RenamePlan();
    return result;
}

bool NoteCollection::renameNote(const QString &relPath, const QString &newTitle)
{
    NoteEntry *entry = const_cast<NoteEntry *>(note(relPath));
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    const QString name = newTitle.trimmed();
    QString reason;
    if (!validName(name, &reason)) {
        emit operationFailed(reason);
        return false;
    }
    if (name == entry->title)
        return true; // nothing to do, not an error

    const QString newRelPath = joinRelPath(entry->folder, name + mdSuffix);
    if (!ensureWithinRoot(relPath) || !ensureWithinRoot(newRelPath))
        return false;
    if (QFileInfo::exists(absolutePath(newRelPath))) {
        emit operationFailed(tr("A note named \"%1\" already exists").arg(name));
        return false;
    }
    if (!QFile::rename(absolutePath(relPath), absolutePath(newRelPath))) {
        emit operationFailed(tr("Cannot rename \"%1\"").arg(entry->title));
        return false;
    }

    NoteEntry moved = *entry;
    removeNoteEntry(relPath);
    moved.relPath = newRelPath;
    moved.title = name;
    moved.fileSize = QFileInfo(absolutePath(newRelPath)).size();
    insertNoteEntry(newRelPath, moved);
    markIndexDirty();

    // Keep the manual-order position.
    QStringList &order = m_manualOrder[moved.folder];
    const int pos = order.indexOf(nameOfRelPath(relPath));
    if (pos >= 0)
        order[pos] = name + mdSuffix;

    if (m_lastOpenNote == relPath)
        m_lastOpenNote = newRelPath;

    saveCollectionFile();
    saveIndexFileIfDirty();
    emit noteMoved(relPath, newRelPath);
    bump();
    dropNoteInSearch(relPath);
    reindexNoteInSearch(newRelPath);
    return true;
}

bool NoteCollection::moveNote(const QString &relPath, const QString &targetFolder)
{
    NoteEntry *entry = const_cast<NoteEntry *>(note(relPath));
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    if (!targetFolder.isEmpty() && !m_folders.contains(targetFolder)) {
        emit operationFailed(tr("No such folder: %1").arg(targetFolder));
        return false;
    }
    if (entry->folder == targetFolder)
        return true;

    const QString name = nameOfRelPath(relPath);
    const QString newRelPath = joinRelPath(targetFolder, name);
    if (!ensureWithinRoot(relPath) || !ensureWithinRoot(newRelPath))
        return false;
    if (QFileInfo::exists(absolutePath(newRelPath))) {
        emit operationFailed(
            tr("\"%1\" already exists in the target folder").arg(entry->title));
        return false;
    }
    if (!QFile::rename(absolutePath(relPath), absolutePath(newRelPath))) {
        emit operationFailed(tr("Cannot move \"%1\"").arg(entry->title));
        return false;
    }

    NoteEntry moved = *entry;
    removeNoteEntry(relPath);
    const QString oldFolder = moved.folder;
    moved.relPath = newRelPath;
    moved.folder = targetFolder;
    moved.fileSize = QFileInfo(absolutePath(newRelPath)).size();
    insertNoteEntry(newRelPath, moved);
    markIndexDirty();

    m_manualOrder[oldFolder].removeAll(name);
    m_manualOrder[targetFolder].append(name);

    if (m_lastOpenNote == relPath)
        m_lastOpenNote = newRelPath;

    saveCollectionFile();
    saveIndexFileIfDirty();
    emit noteMoved(relPath, newRelPath);
    bump();
    dropNoteInSearch(relPath);
    reindexNoteInSearch(newRelPath);
    return true;
}

int NoteCollection::trashItemCount() const
{
    if (!isOpen())
        return 0;
    const QDir trashDir(m_rootPath + QLatin1Char('/') + kvitDirName
                        + QLatin1Char('/') + trashDirName);
    if (!trashDir.exists())
        return 0;
    return int(trashDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                   .size());
}

bool NoteCollection::emptyTrash()
{
    if (!isOpen())
        return false;
    QDir trashDir(m_rootPath + QLatin1Char('/') + kvitDirName
                  + QLatin1Char('/') + trashDirName);
    if (!trashDir.exists()) {
        return true;
    }
    // Permanent by design (the §12.4 safety net ends here); the
    // confirmation dialog names the item count first.
    const bool ok = trashDir.removeRecursively();
    bump();
    return ok;
}

bool NoteCollection::moveToTrash(const QString &relPath)
{
    if (!ensureWithinRoot(relPath))
        return false;
    const QString trashDir =
        m_rootPath + QLatin1Char('/') + kvitDirName + QLatin1Char('/') + trashDirName;
    if (!QDir().mkpath(trashDir))
        return false;

    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString name = nameOfRelPath(relPath);
    QString target = trashDir + QLatin1Char('/') + stamp + QLatin1Char('-') + name;
    int n = 2;
    while (QFileInfo::exists(target)) {
        target = trashDir + QLatin1Char('/') + stamp + QLatin1Char('-')
            + QString::number(n++) + QLatin1Char('-') + name;
    }
    return QFile::rename(absolutePath(relPath), target)
        || QDir().rename(absolutePath(relPath), target);
}

bool NoteCollection::deleteNote(const QString &relPath)
{
    const NoteEntry *entry = note(relPath);
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    if (!moveToTrash(relPath)) {
        emit operationFailed(tr("Cannot delete \"%1\"").arg(entry->title));
        return false;
    }

    const QString folderPath = entry->folder;
    m_manualOrder[folderPath].removeAll(nameOfRelPath(relPath));
    removeNoteEntry(relPath);
    if (m_lastOpenNote == relPath)
        m_lastOpenNote.clear();

    saveCollectionFile();
    markIndexDirty();
    saveIndexFileIfDirty();
    emit noteRemoved(relPath);
    bump();
    dropNoteInSearch(relPath);
    return true;
}

// ---------------------------------------------------- folder operations

QString NoteCollection::createFolder(const QString &parent, const QString &name)
{
    if (!isOpen())
        return QString();
    if (!parent.isEmpty() && !m_folders.contains(parent)) {
        emit operationFailed(tr("No such folder: %1").arg(parent));
        return QString();
    }
    const QString trimmed = name.trimmed();
    QString reason;
    if (!validName(trimmed, &reason)) {
        emit operationFailed(reason);
        return QString();
    }

    const QString relPath = joinRelPath(parent, trimmed);
    if (!ensureWithinRoot(relPath))
        return QString();
    if (QFileInfo::exists(absolutePath(relPath))) {
        emit operationFailed(
            tr("A folder named \"%1\" already exists").arg(trimmed));
        return QString();
    }
    if (!QDir().mkpath(absolutePath(relPath))) {
        emit operationFailed(tr("Cannot create folder \"%1\"").arg(trimmed));
        return QString();
    }

    FolderEntry folder;
    folder.relPath = relPath;
    folder.name = trimmed;
    m_folders.insert(relPath, folder);
    bump();
    return relPath;
}

void NoteCollection::renamePathsUnderFolder(const QString &oldPrefix,
                                            const QString &newPrefix)
{
    // Folders under the renamed one.
    const QStringList folderKeys = m_folders.keys();
    for (const QString &key : folderKeys) {
        if (key == oldPrefix || key.startsWith(oldPrefix + QLatin1Char('/'))) {
            FolderEntry entry = m_folders.take(key);
            entry.relPath = newPrefix + key.mid(oldPrefix.size());
            entry.name = nameOfRelPath(entry.relPath);
            m_folders.insert(entry.relPath, entry);
        }
    }

    // Notes under it (any depth).
    const QStringList noteKeys = m_notes.keys();
    for (const QString &key : noteKeys) {
        if (key.startsWith(oldPrefix + QLatin1Char('/'))) {
            NoteEntry entry = m_notes.take(key);
            const QString newRelPath = newPrefix + key.mid(oldPrefix.size());
            entry.relPath = newRelPath;
            entry.folder = folderOfRelPath(newRelPath);
            m_notes.insert(newRelPath, entry);
            if (m_lastOpenNote == key)
                m_lastOpenNote = newRelPath;
            emit noteMoved(key, newRelPath);
        }
    }
    invalidateWikiIndex();
    rebuildFolderNoteCounts();

    // Manual-order keys.
    const QStringList orderKeys = m_manualOrder.keys();
    for (const QString &key : orderKeys) {
        if (key == oldPrefix || key.startsWith(oldPrefix + QLatin1Char('/'))) {
            m_manualOrder.insert(newPrefix + key.mid(oldPrefix.size()),
                                 m_manualOrder.take(key));
        }
    }
}

bool NoteCollection::renameFolder(const QString &relPath, const QString &newName)
{
    const FolderEntry *entry = folder(relPath);
    if (!entry) {
        emit operationFailed(tr("No such folder: %1").arg(relPath));
        return false;
    }
    const QString trimmed = newName.trimmed();
    QString reason;
    if (!validName(trimmed, &reason)) {
        emit operationFailed(reason);
        return false;
    }
    if (trimmed == entry->name)
        return true;

    const QString parent = folderOfRelPath(relPath);
    const QString newRelPath = joinRelPath(parent, trimmed);
    if (!ensureWithinRoot(relPath) || !ensureWithinRoot(newRelPath))
        return false;
    if (QFileInfo::exists(absolutePath(newRelPath))) {
        emit operationFailed(
            tr("A folder named \"%1\" already exists").arg(trimmed));
        return false;
    }
    if (!QDir().rename(absolutePath(relPath), absolutePath(newRelPath))) {
        emit operationFailed(tr("Cannot rename folder \"%1\"").arg(entry->name));
        return false;
    }

    renamePathsUnderFolder(relPath, newRelPath);
    saveCollectionFile();
    markIndexDirty();
    saveIndexFileIfDirty();
    bump();
    // Many notes changed relative path; reconcile drops the old paths and
    // indexes the new ones.
    syncSearchIndex();
    return true;
}

bool NoteCollection::deleteFolder(const QString &relPath)
{
    const FolderEntry *entry = folder(relPath);
    if (!entry) {
        emit operationFailed(tr("No such folder: %1").arg(relPath));
        return false;
    }
    if (!moveToTrash(relPath)) {
        emit operationFailed(tr("Cannot delete folder \"%1\"").arg(entry->name));
        return false;
    }

    // Remove contained folders, notes, and order lists from the index.
    const QString prefix = relPath + QLatin1Char('/');
    const QStringList folderKeys = m_folders.keys();
    for (const QString &key : folderKeys) {
        if (key == relPath || key.startsWith(prefix))
            m_folders.remove(key);
    }
    const QStringList noteKeys = m_notes.keys();
    for (const QString &key : noteKeys) {
        if (key.startsWith(prefix)) {
            removeNoteEntry(key);
            if (m_lastOpenNote == key)
                m_lastOpenNote.clear();
            emit noteRemoved(key);
        }
    }
    const QStringList orderKeys = m_manualOrder.keys();
    for (const QString &key : orderKeys) {
        if (key == relPath || key.startsWith(prefix))
            m_manualOrder.remove(key);
    }
    rebuildFolderNoteCounts();

    saveCollectionFile();
    markIndexDirty();
    saveIndexFileIfDirty();
    bump();
    // Reconcile drops every note that moved into the trash.
    syncSearchIndex();
    return true;
}

void NoteCollection::setFolderExpanded(const QString &relPath, bool expanded)
{
    auto it = m_folders.find(relPath);
    if (it == m_folders.end() || it->expanded == expanded)
        return;
    it->expanded = expanded;
    saveCollectionFile();
    bump();
}

void NoteCollection::setFolderColor(const QString &relPath, const QString &color)
{
    auto it = m_folders.find(relPath);
    if (it == m_folders.end() || it->color == color)
        return;
    it->color = color;
    saveCollectionFile();
    bump();
}

// ------------------------------------------------------------ metadata

bool NoteCollection::rewriteFrontMatter(const QString &relPath)
{
    if (!ensureWithinRoot(relPath))
        return false;
    // The file's bytes are authoritative for the body (the open note's
    // unsaved edits flow through DocumentManager, not here).
    const QString absPath = absolutePath(relPath);
    bool ok = false;
    const QString fileText = readTextFile(absPath, &ok);
    if (!ok)
        return false;

    const NoteEntry *entry = note(relPath);
    const QDateTime mtime = QFileInfo(absPath).lastModified();
    NoteFrontMatter::Split split = NoteFrontMatter::split(fileText);
    const QString rewritten =
        NoteFrontMatter::serialize(entry->meta) + split.body;
    if (!writeTextFileAtomic(absPath, rewritten))
        return false;
    restoreFileTime(absPath, mtime);
    // The reconcile pass decides freshness from file size and mtime, and
    // this rewrite deliberately restores the mtime so metadata edits do not
    // reorder "recently modified". A same-length change — renaming a tag
    // `books` to `draft`, say — therefore moves neither key, and reconcile
    // would skip the note and serve the old tags forever. Tell the search
    // index outright instead of hoping it notices.
    reindexNoteInSearch(relPath);
    return true;
}

bool NoteCollection::setTags(const QString &relPath, const QStringList &tags)
{
    NoteEntry *entry = const_cast<NoteEntry *>(note(relPath));
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    QStringList cleaned;
    for (const QString &tag : tags) {
        const QString t = tag.trimmed();
        if (!t.isEmpty() && !cleaned.contains(t))
            cleaned.append(t);
    }
    if (entry->meta.tags == cleaned)
        return true;

    const QStringList before = entry->meta.tags;
    entry->meta.tags = cleaned;
    if (!rewriteFrontMatter(relPath)) {
        entry->meta.tags = before;
        emit operationFailed(tr("Cannot write \"%1\"").arg(entry->title));
        return false;
    }
    assignColorsToNewTags(cleaned);
    {
        const QFileInfo info(absolutePath(relPath));
        entry->modified = info.lastModified();
        entry->fileSize = info.size();
    }
    markIndexDirty();
    saveIndexFileIfDirty();
    bump();
    // A metadata write restores the file's mtime, so a reconcile would not see
    // it as changed; reindex explicitly to keep the tag filter current.
    reindexNoteInSearch(relPath);
    return true;
}

// A tag first entering the registry gets the next palette color (§8.2
// tag colors; deterministic round-robin, changeable in the manage
// dialog).
void NoteCollection::assignColorsToNewTags(const QStringList &tags)
{
    static const QStringList palette = {
        QStringLiteral("#e05c5c"), QStringLiteral("#e0a04c"),
        QStringLiteral("#58a866"), QStringLiteral("#4a90d9"),
        QStringLiteral("#9068c8"), QStringLiteral("#d06ca8"),
    };
    bool changed = false;
    for (const QString &tag : tags) {
        if (!m_tagColors.contains(tag)) {
            m_tagColors.insert(
                tag, palette.at(m_tagColors.size() % palette.size()));
            changed = true;
        }
    }
    if (changed)
        saveCollectionFile();
}

bool NoteCollection::addTag(const QString &relPath, const QString &tag)
{
    const NoteEntry *entry = note(relPath);
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    QStringList tags = entry->meta.tags;
    tags.append(tag);
    return setTags(relPath, tags);
}

bool NoteCollection::removeTag(const QString &relPath, const QString &tag)
{
    const NoteEntry *entry = note(relPath);
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    QStringList tags = entry->meta.tags;
    tags.removeAll(tag.trimmed());
    return setTags(relPath, tags);
}

bool NoteCollection::setPinned(const QString &relPath, bool pinned)
{
    NoteEntry *entry = const_cast<NoteEntry *>(note(relPath));
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    if (entry->meta.pinned == pinned)
        return true;
    entry->meta.pinned = pinned;
    if (!rewriteFrontMatter(relPath)) {
        entry->meta.pinned = !pinned;
        emit operationFailed(tr("Cannot write \"%1\"").arg(entry->title));
        return false;
    }
    {
        const QFileInfo info(absolutePath(relPath));
        entry->modified = info.lastModified();
        entry->fileSize = info.size();
    }
    markIndexDirty();
    saveIndexFileIfDirty();
    bump();
    return true;
}

bool NoteCollection::setFavorite(const QString &relPath, bool favorite)
{
    NoteEntry *entry = const_cast<NoteEntry *>(note(relPath));
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    if (entry->meta.favorite == favorite)
        return true;
    entry->meta.favorite = favorite;
    if (!rewriteFrontMatter(relPath)) {
        entry->meta.favorite = !favorite;
        emit operationFailed(tr("Cannot write \"%1\"").arg(entry->title));
        return false;
    }
    {
        const QFileInfo info(absolutePath(relPath));
        entry->modified = info.lastModified();
        entry->fileSize = info.size();
    }
    markIndexDirty();
    saveIndexFileIfDirty();
    bump();
    return true;
}

bool NoteCollection::setGoal(const QString &relPath, int goal)
{
    NoteEntry *entry = const_cast<NoteEntry *>(note(relPath));
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    const int clamped = qMax(0, goal);
    if (entry->meta.goal == clamped)
        return true;
    const int previous = entry->meta.goal;
    entry->meta.goal = clamped;
    if (!rewriteFrontMatter(relPath)) {
        entry->meta.goal = previous;
        emit operationFailed(tr("Cannot write \"%1\"").arg(entry->title));
        return false;
    }
    {
        const QFileInfo info(absolutePath(relPath));
        entry->modified = info.lastModified();
        entry->fileSize = info.size();
    }
    markIndexDirty();
    saveIndexFileIfDirty();
    bump();
    return true;
}

int NoteCollection::goalFor(const QString &relPath) const
{
    const NoteEntry *entry = note(relPath);
    return entry ? entry->meta.goal : 0;
}

// --------------------------------------------------------------- tags

QStringList NoteCollection::allTags() const
{
    QStringList tags;
    for (const NoteEntry &entry : m_notes) {
        for (const QString &tag : entry.meta.tags) {
            if (!tags.contains(tag))
                tags.append(tag);
        }
    }
    std::sort(tags.begin(), tags.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    return tags;
}

QVariantList NoteCollection::tagListing() const
{
    QVariantList listing;
    const QStringList tags = allTags();
    for (const QString &tag : tags) {
        listing.append(QVariantMap{
            {QStringLiteral("name"), tag},
            {QStringLiteral("count"), tagCount(tag)},
            {QStringLiteral("color"), tagColor(tag)},
        });
    }
    return listing;
}

int NoteCollection::tagCount(const QString &tag) const
{
    int count = 0;
    for (const NoteEntry &entry : m_notes) {
        if (entry.meta.tags.contains(tag))
            ++count;
    }
    return count;
}

QString NoteCollection::tagColor(const QString &tag) const
{
    return m_tagColors.value(tag);
}

void NoteCollection::setTagColor(const QString &tag, const QString &color)
{
    if (m_tagColors.value(tag) == color)
        return;
    if (color.isEmpty())
        m_tagColors.remove(tag);
    else
        m_tagColors.insert(tag, color);
    saveCollectionFile();
    bump();
}

bool NoteCollection::renameTag(const QString &oldName, const QString &newName)
{
    const QString target = newName.trimmed();
    if (target.isEmpty()) {
        emit operationFailed(tr("Tag name is empty"));
        return false;
    }
    if (target == oldName)
        return true;

    bool anyFailed = false;
    const QStringList keys = m_notes.keys();
    for (const QString &key : keys) {
        NoteEntry &entry = m_notes[key];
        if (!entry.meta.tags.contains(oldName))
            continue;
        QStringList tags;
        for (const QString &tag : entry.meta.tags) {
            const QString mapped = (tag == oldName) ? target : tag;
            if (!tags.contains(mapped))
                tags.append(mapped); // merge dedupes
        }
        const QStringList before = entry.meta.tags;
        entry.meta.tags = tags;
        if (!rewriteFrontMatter(key)) {
            entry.meta.tags = before;
            anyFailed = true;
        } else {
            const QFileInfo info(absolutePath(key));
            entry.modified = info.lastModified();
            entry.fileSize = info.size();
            markIndexDirty();
        }
    }

    // The color follows the name unless the target already has one.
    if (m_tagColors.contains(oldName) && !m_tagColors.contains(target))
        m_tagColors.insert(target, m_tagColors.value(oldName));
    m_tagColors.remove(oldName);

    saveCollectionFile();
    saveIndexFileIfDirty();
    bump();
    // Front-matter rewrites change file size, so reconcile reindexes every
    // touched note and the tag filter stays current.
    syncSearchIndex();
    if (anyFailed)
        emit operationFailed(tr("Some notes could not be rewritten"));
    return !anyFailed;
}

bool NoteCollection::deleteTag(const QString &tag)
{
    bool anyFailed = false;
    const QStringList keys = m_notes.keys();
    for (const QString &key : keys) {
        NoteEntry &entry = m_notes[key];
        if (!entry.meta.tags.contains(tag))
            continue;
        const QStringList before = entry.meta.tags;
        entry.meta.tags.removeAll(tag);
        if (!rewriteFrontMatter(key)) {
            entry.meta.tags = before;
            anyFailed = true;
        } else {
            const QFileInfo info(absolutePath(key));
            entry.modified = info.lastModified();
            entry.fileSize = info.size();
            markIndexDirty();
        }
    }
    m_tagColors.remove(tag);
    saveCollectionFile();
    saveIndexFileIfDirty();
    bump();
    syncSearchIndex();
    if (anyFailed)
        emit operationFailed(tr("Some notes could not be rewritten"));
    return !anyFailed;
}

// ------------------------------------------------------- manual order

QStringList NoteCollection::manualOrder(const QString &folder) const
{
    // Names listed in collection.json that still exist, in order...
    QStringList relPaths;
    const QStringList &names = m_manualOrder.value(folder);
    for (const QString &name : names) {
        const QString relPath = joinRelPath(folder, name);
        if (m_notes.contains(relPath))
            relPaths.append(relPath);
    }
    // ...then everything unlisted, oldest first (stable for new files).
    QList<const NoteEntry *> rest;
    for (const NoteEntry &entry : m_notes) {
        if (entry.folder == folder && !relPaths.contains(entry.relPath))
            rest.append(&entry);
    }
    std::sort(rest.begin(), rest.end(),
              [](const NoteEntry *a, const NoteEntry *b) {
                  if (a->created != b->created)
                      return a->created < b->created;
                  return a->relPath < b->relPath;
              });
    for (const NoteEntry *entry : rest)
        relPaths.append(entry->relPath);
    return relPaths;
}

bool NoteCollection::setManualPosition(const QString &relPath, int position)
{
    const NoteEntry *entry = note(relPath);
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }

    QStringList order = manualOrder(entry->folder); // reconciled relPaths
    order.removeAll(relPath);
    position = qBound(0, position, order.size());
    order.insert(position, relPath);

    QStringList names;
    for (const QString &path : order)
        names.append(nameOfRelPath(path));
    m_manualOrder.insert(entry->folder, names);
    saveCollectionFile();
    bump();
    return true;
}

// ---------------------------------------------------- workspace state

void NoteCollection::setLastOpenNote(const QString &relPath)
{
    if (m_lastOpenNote == relPath)
        return;
    m_lastOpenNote = relPath;
    saveCollectionFile();
    // Deliberately no bump: which note is open is not collection content.
}

// ------------------------------------------------------------- backups

namespace {
const int backupFloorSecs = 10 * 60;
const int backupKeep = 10;
const QString backupStampFormat = QStringLiteral("yyyyMMdd-HHmmss");

void writeBackupSnapshot(const QString &dirPath,
                         const QString &target,
                         QByteArray bytes)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.backup_before_overwrite.write"),
        QVariantMap{{QStringLiteral("path"), target},
                    {QStringLiteral("bytes"), bytes.size()},
                    {QStringLiteral("async"), true}});

    QDir().mkpath(dirPath);
    const bool copied = writeFileBytesAtomic(target, bytes);
    perf.addContext(QStringLiteral("copied"), copied);
    if (!copied)
        return;

    QDir dir(dirPath);
    QStringList existing = dir.entryList({QStringLiteral("*.md")},
                                         QDir::Files, QDir::Name);
    while (existing.size() > backupKeep)
        QFile::remove(dirPath + QLatin1Char('/') + existing.takeFirst());
}
} // namespace

void NoteCollection::setClockForTesting(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

void NoteCollection::setClockOffsetForTesting(int secs)
{
    if (secs == 0)
        m_clock = nullptr;
    else
        m_clock = [secs]() { return QDateTime::currentDateTime().addSecs(secs); };
}

void NoteCollection::backupBeforeOverwrite(const QString &absPath)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.backup_before_overwrite"),
        QVariantMap{{QStringLiteral("path"), absPath}});

    const QString relPath = relativePath(absPath);
    if (relPath.isEmpty() || !QFileInfo::exists(absPath)) {
        perf.addContext(QStringLiteral("skipped"), true);
        return;
    }

    const QString dirPath = m_rootPath + QStringLiteral("/") + kvitDirName
        + QStringLiteral("/") + QStringLiteral("backups") + QLatin1Char('/')
        + relPath;
    QDir dir(dirPath);
    const QDateTime now = m_clock ? m_clock() : QDateTime::currentDateTime();

    // Rotation floor: at most one backup per window, whatever the
    // auto-save cadence.
    QStringList existing = dir.entryList({QStringLiteral("*.md")},
                                         QDir::Files, QDir::Name);
    if (!existing.isEmpty()) {
        const QString newest = existing.last();
        const QDateTime newestStamp = QDateTime::fromString(
            newest.left(newest.size() - 3), backupStampFormat);
        if (newestStamp.isValid()
            && newestStamp.secsTo(now) < backupFloorSecs) {
            perf.addContext(QStringLiteral("skipped"), true);
            perf.addContext(QStringLiteral("reason"),
                            QStringLiteral("rotation_floor"));
            return;
        }
    }

    QString target = dirPath + QLatin1Char('/')
        + now.toString(backupStampFormat) + mdSuffix;
    if (QFileInfo::exists(target)) {
        perf.addContext(QStringLiteral("skipped"), true);
        perf.addContext(QStringLiteral("reason"),
                        QStringLiteral("duplicate_stamp"));
        return; // same-second duplicate: the window already has its copy
    }

    bool ok = false;
    QByteArray bytes = readFileBytes(absPath, &ok);
    perf.addContext(QStringLiteral("copied"), ok);
    perf.addContext(QStringLiteral("bytes"), bytes.size());
    perf.addContext(QStringLiteral("async"), ok);
    if (!ok)
        return;

    QtConcurrent::run(writeBackupSnapshot, dirPath, target, std::move(bytes));
}

QVariantList NoteCollection::backupsFor(const QString &relPath) const
{
    QVariantList listing;
    const QString dirPath = m_rootPath + QStringLiteral("/") + kvitDirName
        + QStringLiteral("/backups/") + relPath;
    const QStringList files = QDir(dirPath).entryList(
        {QStringLiteral("*.md")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &fileName : files) {
        const QDateTime stamp = QDateTime::fromString(
            fileName.left(fileName.size() - 3), backupStampFormat);
        bool ok = false;
        const QString text =
            readTextFile(dirPath + QLatin1Char('/') + fileName, &ok);
        if (!ok)
            continue;
        const NoteFrontMatter::Split split = NoteFrontMatter::split(text);
        listing.append(QVariantMap{
            {QStringLiteral("fileName"), fileName},
            {QStringLiteral("timestamp"), stamp},
            {QStringLiteral("preview"), analyzeBody(split.body).snippet},
        });
    }
    return listing;
}

QString NoteCollection::backupBody(const QString &relPath,
                                   const QString &fileName) const
{
    if (fileName.contains(QLatin1Char('/')))
        return QString();
    const QString path = m_rootPath + QStringLiteral("/") + kvitDirName
        + QStringLiteral("/backups/") + relPath + QLatin1Char('/') + fileName;
    bool ok = false;
    const QString text = readTextFile(path, &ok);
    return ok ? NoteFrontMatter::split(text).body : QString();
}

// ------------------------------------------------------ crash recovery

QString NoteCollection::journalPathFor(const QString &relPath) const
{
    if (!isOpen() || relPath.isEmpty())
        return QString();
    const QString dirPath = m_rootPath + QStringLiteral("/") + kvitDirName
        + QStringLiteral("/recovery");
    QDir().mkpath(dirPath);
    // The file name IS the relPath, percent-encoded (flat directory).
    const QString encoded = QString::fromUtf8(
        QUrl::toPercentEncoding(relPath));
    return dirPath + QLatin1Char('/') + encoded;
}

QVariantList NoteCollection::recoveryEntries() const
{
    QVariantList entries;
    for (const QString &relPath : m_pendingRecovery) {
        const QString journal = journalPathFor(relPath);
        bool ok = false;
        const QString text = readTextFile(journal, &ok);
        if (!ok)
            continue;
        const NoteFrontMatter::Split split = NoteFrontMatter::split(text);
        QString title = nameOfRelPath(relPath);
        if (title.endsWith(mdSuffix, Qt::CaseInsensitive))
            title.chop(mdSuffix.size());
        entries.append(QVariantMap{
            {QStringLiteral("relPath"), relPath},
            {QStringLiteral("title"), title},
            {QStringLiteral("preview"), analyzeBody(split.body).snippet},
            {QStringLiteral("journalPath"), journal},
        });
    }
    return entries;
}

bool NoteCollection::restoreRecovery(const QString &relPath)
{
    if (!m_pendingRecovery.contains(relPath))
        return false;
    const QString journal = journalPathFor(relPath);
    bool ok = false;
    const QString text = readTextFile(journal, &ok);
    if (!ok) {
        emit operationFailed(tr("Cannot read the recovered content"));
        return false;
    }

    // The journal is a full file image (front-matter included). Restore
    // recreates a deleted note's folder if needed.
    const QString absPath = absolutePath(relPath);
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    if (!writeTextFileAtomic(absPath, text)) {
        emit operationFailed(tr("Cannot restore \"%1\"").arg(relPath));
        return false;
    }
    QFile::remove(journal);
    m_pendingRecovery.removeAll(relPath);

    // The folder may be new to the index (recreated path).
    const QString folderPath = folderOfRelPath(relPath);
    if (!folderPath.isEmpty() && !m_folders.contains(folderPath)) {
        QString accumulated;
        const QStringList parts = folderPath.split(QLatin1Char('/'));
        for (const QString &part : parts) {
            accumulated = joinRelPath(accumulated, part);
            if (!m_folders.contains(accumulated)) {
                FolderEntry folder;
                folder.relPath = accumulated;
                folder.name = part;
                m_folders.insert(accumulated, folder);
            }
        }
    }
    indexNote(relPath);
    // indexNote only refreshes the in-memory entry and its JSON sidecar;
    // global search is a separate database and would otherwise keep serving
    // the pre-crash text.
    reindexNoteInSearch(relPath);
    saveIndexFileIfDirty();
    bump();
    return true;
}

void NoteCollection::discardRecovery(const QString &relPath)
{
    if (!m_pendingRecovery.contains(relPath))
        return;
    QFile::remove(journalPathFor(relPath));
    m_pendingRecovery.removeAll(relPath);
    bump();
}

// ------------------------------------------------- open-note seams

void NoteCollection::noteSaved(const QString &absPath)
{
    noteSaved(absPath, QString());
}

void NoteCollection::noteSaved(const QString &absPath, const QString &fileText)
{
    PerfLog::ScopedTimer perf(
        QStringLiteral("collection.note_saved"),
        QVariantMap{{QStringLiteral("path"), absPath},
                    {QStringLiteral("fromMemory"), !fileText.isNull()}});

    const QString relPath = relativePath(absPath);
    if (relPath.isEmpty() || !relPath.endsWith(mdSuffix, Qt::CaseInsensitive)) {
        perf.addContext(QStringLiteral("skipped"), true);
        return;
    }
    const QFileInfo info(absPath);
    if (!info.exists() || !info.isFile()) {
        perf.addContext(QStringLiteral("skipped"), true);
        return;
    }
    perf.addContext(QStringLiteral("relPath"), relPath);
    if (!fileText.isNull())
        perf.addContext(QStringLiteral("chars"), fileText.size());

    // Feed the just-saved content to the search index. When the text is in hand
    // it is passed straight through so the worker skips a redundant disk read;
    // otherwise the worker reads the file. Queued FIFO writes
    // mean two rapid saves cannot let an older parse win.
    if (m_searchIndex && m_searchIndexRoot == m_rootPath) {
        if (!fileText.isNull())
            m_searchIndex->replaceFromText(
                relPath, fileText, info.size(),
                info.lastModified().toMSecsSinceEpoch());
        else
            m_searchIndex->replaceFromPath(relPath, info.absoluteFilePath());
    }

    AsyncSavedNoteTask task;
    task.relPath = relPath;
    task.absPath = info.absoluteFilePath();
    task.fileText = fileText;
    task.createdFallback = fileCreatedTime(info);
    task.modified = info.lastModified();
    task.fileSize = info.size();
    startAsyncSavedNoteIndex(std::move(task));
}

QString NoteCollection::frontMatterFor(const QString &relPath) const
{
    const NoteEntry *entry = note(relPath);
    return entry ? NoteFrontMatter::serialize(entry->meta) : QString();
}

// ------------------------------------------------------ collection.json

void NoteCollection::loadCollectionFile()
{
    const QString path = m_rootPath + QLatin1Char('/') + kvitDirName
        + QLatin1Char('/') + collectionFileName;
    bool ok = false;
    const QString text = readTextFile(path, &ok);
    if (!ok)
        return; // absent or unreadable: defaults (never touches notes)

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return; // corrupt: defaults

    const QJsonObject root = doc.object();

    const QJsonObject tagColors = root.value(QStringLiteral("tagColors")).toObject();
    for (auto it = tagColors.begin(); it != tagColors.end(); ++it)
        m_tagColors.insert(it.key(), it.value().toString());

    const QJsonObject folders = root.value(QStringLiteral("folders")).toObject();
    for (auto it = folders.begin(); it != folders.end(); ++it) {
        auto folderIt = m_folders.find(it.key());
        if (folderIt == m_folders.end())
            continue; // stale entry for a folder that no longer exists
        const QJsonObject state = it.value().toObject();
        folderIt->color = state.value(QStringLiteral("color")).toString();
        folderIt->expanded =
            state.value(QStringLiteral("expanded")).toBool(true);
    }

    const QJsonObject order = root.value(QStringLiteral("manualOrder")).toObject();
    for (auto it = order.begin(); it != order.end(); ++it) {
        QStringList names;
        const QJsonArray array = it.value().toArray();
        for (const QJsonValue &value : array)
            names.append(value.toString());
        m_manualOrder.insert(it.key(), names);
    }

    m_lastOpenNote = root.value(QStringLiteral("lastOpenNote")).toString();
    if (!m_lastOpenNote.isEmpty()
        && !m_notes.contains(m_lastOpenNote)
        && !QFileInfo::exists(absolutePath(m_lastOpenNote)))
        m_lastOpenNote.clear();
}

void NoteCollection::saveCollectionFile()
{
    if (!isOpen())
        return;

    QJsonObject root;

    QJsonObject tagColors;
    for (auto it = m_tagColors.begin(); it != m_tagColors.end(); ++it)
        tagColors.insert(it.key(), it.value());
    root.insert(QStringLiteral("tagColors"), tagColors);

    QJsonObject folders;
    for (const FolderEntry &entry : m_folders) {
        if (entry.color.isEmpty() && entry.expanded)
            continue; // defaults need no entry
        QJsonObject state;
        if (!entry.color.isEmpty())
            state.insert(QStringLiteral("color"), entry.color);
        state.insert(QStringLiteral("expanded"), entry.expanded);
        folders.insert(entry.relPath, state);
    }
    root.insert(QStringLiteral("folders"), folders);

    QJsonObject order;
    for (auto it = m_manualOrder.begin(); it != m_manualOrder.end(); ++it) {
        if (it.value().isEmpty())
            continue;
        order.insert(it.key(), QJsonArray::fromStringList(it.value()));
    }
    root.insert(QStringLiteral("manualOrder"), order);

    if (!m_lastOpenNote.isEmpty())
        root.insert(QStringLiteral("lastOpenNote"), m_lastOpenNote);

    const QString dir = m_rootPath + QLatin1Char('/') + kvitDirName;
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + collectionFileName;
    if (!writeTextFileAtomic(path,
                             QString::fromUtf8(QJsonDocument(root).toJson(
                                 QJsonDocument::Indented)))) {
        // Tag colours, folder expansion, manual order and the last-open
        // note all live in this file. Silently dropping the write loses
        // them at the next start with nothing shown to the user.
        emit operationFailed(
            tr("Cannot save collection settings to \"%1\"").arg(path));
    }
}

// ----------------------------------------------------------------- misc

void NoteCollection::bump()
{
    ++m_revision;
    emit revisionChanged();
}

void NoteCollection::setSearchIndex(CollectionSearchIndex *index)
{
    m_searchIndex = index;
    m_searchIndexRoot.clear();
    if (m_searchIndex && isOpen())
        syncSearchIndex();
}

void NoteCollection::syncSearchIndex()
{
    if (!m_searchIndex)
        return;
    if (!isOpen()) {
        if (!m_searchIndexRoot.isEmpty()) {
            m_searchIndex->closeIndex();
            m_searchIndexRoot.clear();
        }
        return;
    }
    // Open (or reopen) the cache database for the current root, then reconcile
    // it against the on-disk listing: parse new or changed notes, drop missing
    // ones. hasNoteFresh makes warm startup skip unchanged notes,
    // so this stays cheap after the first cold build.
    if (m_searchIndexRoot != m_rootPath) {
        m_searchIndex->openForRoot(m_rootPath);
        m_searchIndexRoot = m_rootPath;
    }
    QList<ReconcileEntry> listing;
    listing.reserve(m_notes.size());
    for (auto it = m_notes.constBegin(); it != m_notes.constEnd(); ++it) {
        const NoteEntry &entry = it.value();
        ReconcileEntry e;
        e.relPath = entry.relPath;
        e.absPath = absolutePath(entry.relPath);
        e.fileSize = entry.fileSize;
        e.modifiedMs = entry.modified.toMSecsSinceEpoch();
        listing.append(e);
    }
    m_searchIndex->reconcile(listing);
}

void NoteCollection::reindexNoteInSearch(const QString &relPath)
{
    if (m_searchIndex && m_searchIndexRoot == m_rootPath && !m_rootPath.isEmpty())
        m_searchIndex->replaceFromPath(relPath, absolutePath(relPath));
}

void NoteCollection::dropNoteInSearch(const QString &relPath)
{
    if (m_searchIndex && m_searchIndexRoot == m_rootPath && !m_rootPath.isEmpty())
        m_searchIndex->removePath(relPath);
}
