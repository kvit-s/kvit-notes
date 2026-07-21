// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "notecollection.h"

#include "documentserializer.h"
#include "opendocumentsession.h"
#include "block.h"
#include "collectionsearchindex.h"
#include "notefileio.h"
#include "vaultpaths.h"
#include "noteindexfile.h"
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

// The four file primitives now live in notefileio.h, shared with the stores
// split out of this class. Pulled in unqualified so every call site below
// reads exactly as it did when they were defined here.
using NoteFileIo::readFileBytes;
using NoteFileIo::readTextFile;
using NoteFileIo::writeFileBytesAtomic;
using NoteFileIo::writeTextFileAtomic;

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

// Vault containment lives in vaultpaths.h, shared with the stores split out
// of this class and with the persisted-state validation that has to apply the
// same rule. Pulled in unqualified so the call sites below read as they did
// when the two functions were defined here.
using VaultPaths::canonicalizeMissingOk;
using VaultPaths::isWithinCanonicalRoot;

QDateTime fileCreatedTime(const QFileInfo &info)
{
    QDateTime birth = info.birthTime();
    return birth.isValid() ? birth : info.lastModified();
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
    , m_searchFeed(
          // Built here rather than passed in: the listing and the path
          // resolver are this collection's own state, and the feed is a
          // member with the same lifetime.
          [this]() { return searchReconcileListing(); },
          [this](const QString &relPath) { return absolutePath(relPath); })
    , m_wikiLinks(&m_notes,
                  [this]() { return m_revision; },
                  [this](const QString &relPath) { return absolutePath(relPath); },
                  [this](const QString &relPath) { return readNoteBody(relPath); })
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
    m_collectionState.setSnapshotProvider(
        [this]() { return collectionStateSnapshot(); });
    connect(&m_collectionState, &CollectionStateStore::saveFailed,
            this, &NoteCollection::operationFailed);
}

NoteCollection::~NoteCollection()
{
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
    m_collectionState.flushIfDirty();
}

void NoteCollection::setOpenDocument(OpenDocumentSession *session)
{
    m_openDocument = session;
}

// --------------------------------------------------------------- root

bool NoteCollection::openRoot(const QString &path)
{
    // Workspace state still owed belongs to the root being left. Writing it
    // after the root changes would put it in the new vault.
    m_collectionState.flushIfDirty();
    if (m_collectionState.isDirty())
        return false;
    PerfLog::ScopedTimer perf(QStringLiteral("startup.scan"),
                              QVariantMap{{QStringLiteral("path"), path}});
    // Nothing about the vault currently open is given up until the new one is
    // known to be openable. A switch that fails because the other vault is
    // held elsewhere leaves this collection exactly as it was, still locked
    // and still scanning, rather than showing a vault whose lock and
    // background work it has already thrown away.
    if (!prepareRootPath(path))
        return false;
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
    scan();

    // Journal files present when a root OPENS are crash evidence: orderly
    // shutdown removes them, and a mid-session refresh() must not ingest
    // the open note's live journal.
    loadRecoveryEntries();
    resumePendingOperations();

    emit rootChanged();
    bump();
    syncSearchIndex();
    perf.addContext(QStringLiteral("notes"), noteCount());
    perf.addContext(QStringLiteral("folders"), m_folders.size());
    return true;
}

bool NoteCollection::openRootAsync(const QString &path)
{
    // Workspace state still owed belongs to the root being left. Writing it
    // after the root changes would put it in the new vault.
    m_collectionState.flushIfDirty();
    if (m_collectionState.isDirty())
        return false;
    // As in openRoot(): the current vault is only given up once the new one
    // has been taken.
    if (!prepareRootPath(path))
        return false;
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();

    // Open the search index for the new root at once so queries hit the warm
    // database from the previous session while the background scan runs; the
    // reconcile that catches up to on-disk changes waits for finishAsyncScan.
    m_searchFeed.openFor(m_rootPath);

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
    m_collectionState.flushIfDirty();
    if (m_collectionState.isDirty())
        return;
    cancelAsyncScan();
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
    m_rootPath.clear();
    m_canonicalRoot.clear();
    attachStoresToRoot();
    m_notes.clear();
    m_folders.clear();
    clearFolderNoteCounts();
    m_tagColors.clear();
    m_manualOrder.clear();
    m_lastOpenNote.clear();
    m_recoveryJournals.clear();
    // A plan describes work in the vault being left; the next open reads
    // whatever is still recorded there.
    m_activeOperationPlan.clear();
    m_pendingOperations.clear();
    m_indexDirty = false;
    m_searchFeed.close();
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

    const QString absolute = QDir(path).absolutePath();

    // Establish the control directory before anything reads, writes or
    // deletes through it. Attaching the stores alone deletes the legacy cache
    // — one file and one recursive directory removal — and every later write
    // of state, templates, journals, backups and trash goes through the same
    // subtree. If `.kvit` is a link, all of that lands in a directory this
    // application was never pointed at, so the vault is refused instead.
    if (!VaultPaths::ownedDirIsSound(absolute, QStringLiteral(".kvit"))) {
        emit operationFailed(
            tr("\"%1\" cannot be opened: its .kvit folder is a link rather "
               "than a folder inside the vault").arg(absolute));
        return false;
    }
    // `assets` is owned in the same way, but only once it exists: a vault
    // that has never stored an image has no assets directory, and creating
    // one on open would be a write nobody asked for.
    if (QFileInfo::exists(QDir(absolute).filePath(QStringLiteral("assets")))
        && !VaultPaths::ownedDirIsSound(absolute, QStringLiteral("assets"))) {
        emit operationFailed(
            tr("\"%1\" cannot be opened: its assets folder is a link rather "
               "than a folder inside the vault").arg(absolute));
        return false;
    }

    // Take the vault before reading any of its state. Everything below this
    // point loads files that will later be written back whole, so a second
    // process reaching the same point would set up the lost update
    // tests/test_vaultlock.cpp demonstrates. Unavailable (no locking on this
    // filesystem, read-only directory) opens unlocked rather than refusing:
    // an unopenable vault is a worse outcome than an unguarded one.
    const VaultLock::Result lock = m_vaultLock.acquire(
        absolute, m_readOnly ? VaultLock::Access::Read
                             : VaultLock::Access::Write);
    if (lock == VaultLock::Result::HeldByAnother) {
        // One signal, not operationFailed as well: the UI needs to say
        // something specific here rather than show a generic failure, and two
        // signals for one event invites handling it twice.
        emit vaultInUse(absolute, m_vaultLock.blockingHolder().describe());
        return false;
    }

    m_rootPath = absolute;
    m_canonicalRoot = canonicalizeMissingOk(m_rootPath);
    attachStoresToRoot();
    // Fail-open is a deliberate choice, but it was only ever written to the
    // log, where nobody sees it: the vault is open with nothing stopping a
    // second session from saving over this one.
    if (lock == VaultLock::Result::Unavailable && !m_readOnly) {
        emit vaultUnprotected(
            absolute,
            tr("This folder does not support file locking, so Kvit cannot "
               "stop another window or computer from writing to it at the "
               "same time. Editing it from one place at a time is up to you."));
    }
    return true;
}

// Every collaborator that addresses a directory under <root>/.kvit learns the
// root from one place, so opening or closing a vault cannot leave one of them
// pointed at the previous one.
void NoteCollection::attachStoresToRoot()
{
    m_trash.setRootPath(m_rootPath);
    m_backups.setRootPath(m_rootPath);
    m_recoveryJournals.setRootPath(m_rootPath);
    m_indexFile.setRootPath(m_rootPath);
    m_operations.setRootPath(m_rootPath);
    m_collectionState.setRoot(m_rootPath, m_canonicalRoot);
}

void NoteCollection::loadRecoveryEntries()
{
    m_recoveryJournals.reload();
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

void NoteCollection::fullRefreshAsync()
{
    if (!isOpen())
        return;
    // scanAsync() cancels the async scan itself; the other three in-flight
    // operations are cancelled here for parity with refresh(), because a full
    // rebuild supersedes any narrower work still running.
    cancelAsyncRefresh();
    cancelAsyncSavedNote();
    cancelAsyncIndexSave();
    scanAsync();
}

void NoteCollection::refreshPaths(const QStringList &absPaths)
{
    if (!isOpen())
        return;
    if (absPaths.isEmpty()) {
        // Driven by the external file watcher, never a deliberate user action,
        // so it must not block the GUI thread on a full scan.
        fullRefreshAsync();
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
        fullRefreshAsync();
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
    if (!isOpen() || !m_notes.isEmpty() || refuseWhenReadOnly())
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
    emit aboutToWrite(absolutePath(relPath));
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

bool NoteCollection::refuseWhenReadOnly()
{
    if (!m_readOnly)
        return false;
    emit operationFailed(tr("This vault is open for reading only"));
    return true;
}

bool NoteCollection::ensureWithinRoot(const QString &relPath)
{
    if (!isOpen())
        return false;
    if (m_readOnly) {
        emit operationFailed(
            tr("This vault is open for reading only; \"%1\" was not changed")
                .arg(relPath));
        return false;
    }
    if (relPath.isEmpty())
        return true;
    // Shape first: a value that is absolute, dot-segmented or non-canonical
    // is malformed, and saying so names the problem better than reporting it
    // as being outside the vault.
    if (!VaultPaths::isPlainRelativePath(relPath)) {
        emit operationFailed(tr("\"%1\" is not a safe relative path")
                                 .arg(relPath));
        return false;
    }
    if (isWithinCanonicalRoot(m_canonicalRoot, m_rootPath + QLatin1Char('/')
                                                   + relPath))
        return true;
    emit operationFailed(tr("\"%1\" is outside the notes folder").arg(relPath));
    return false;
}

bool NoteCollection::openDocumentIs(const QString &relPath) const
{
    return m_openDocument && !relPath.isEmpty()
        && QDir::cleanPath(m_openDocument->openFilePath())
            == QDir::cleanPath(absolutePath(relPath));
}

bool NoteCollection::prepareOpenDocumentMutation(const QString &relPath)
{
    if (!openDocumentIs(relPath))
        return true;
    m_openDocument->flushPendingEdits();
    m_openDocument->cancelPendingWrites();
    return true;
}

bool NoteCollection::persistOpenDocumentBeforeRemoval(const QString &relPath)
{
    if (!openDocumentIs(relPath))
        return true;
    // flushPendingEdits() has already brought delegate-local text into the
    // document and cancelPendingWrites() has stopped anything older, so what
    // the session holds now is the newest revision there is. Deletion moves
    // the file to the trash and then closes the document, which is the last
    // moment that revision exists anywhere.
    if (!m_openDocument->hasUnsavedChanges())
        return true;
    return m_openDocument->persistCurrentRevision();
}

void NoteCollection::rebindOpenDocument(const QString &oldRelPath,
                                        const QString &newRelPath)
{
    if (openDocumentIs(oldRelPath))
        m_openDocument->rebindFilePath(absolutePath(newRelPath));
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
    QHash<QString, NoteEntry> cachedNotes = m_indexFile.load(&indexOk);
    dropJournalledEntriesFromCache(&cachedNotes);
    m_notes.reserve(cachedNotes.size());
    m_folderOwnNoteCounts.reserve(cachedNotes.size());
    m_folderRecursiveNoteCounts.reserve(cachedNotes.size());
    m_indexDirty = !indexOk && QFileInfo::exists(m_indexFile.path());

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
    QHash<QString, NoteEntry> cachedNotes = m_indexFile.load(&indexOk);
    dropJournalledEntriesFromCache(&cachedNotes);
    const bool indexFileExists = QFileInfo::exists(m_indexFile.path());
    m_notes.reserve(cachedNotes.size());
    m_folderOwnNoteCounts.reserve(cachedNotes.size());
    m_folderRecursiveNoteCounts.reserve(cachedNotes.size());
    m_indexDirty = !indexOk && indexFileExists;

    // Load workspace state now so startup can open a remembered note from
    // disk before the background directory listing has caught up. Folder
    // visual state is applied again after the listing publishes folders.
    loadCollectionFile(true);

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
        QtConcurrent::run(&VaultScan::buildScanListing, request));
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

    if (VaultScan::maxNoteBytes() > 0
        && info.size() > VaultScan::maxNoteBytes()) {
        // Listed but not read: see VaultScan::maxNoteBytes(). The synchronous
        // scan is the one that runs on the GUI thread, so it is the path
        // where reading an enormous file is felt as a freeze.
        ensureFolderEntriesFor(folderOfRelPath(relPath));
        insertNoteEntry(relPath, VaultScan::unparsedEntry(relPath, info));
        perf.addContext(QStringLiteral("ok"), true);
        perf.addContext(QStringLiteral("oversized"), true);
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

void NoteCollection::indexNoteFromText(const QString &relPath,
                                       const QString &fileText,
                                       const QFileInfo &info)
{
    if (m_indexParseObserver)
        m_indexParseObserver(relPath);

    const NoteEntry entry = VaultScan::entryFromText(relPath, fileText, info);
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
    // A placeholder carries fileSize -1 (VaultScan::placeholderEntry): the
    // note is listed but nothing has read it, so its metadata is unknown
    // rather than empty.
    const auto previous = m_notes.constFind(relPath);
    const bool wasUnknown = previous == m_notes.constEnd()
        || previous->fileSize < 0;
    removeNoteEntry(relPath);
    m_notes.insert(relPath, entry);
    adjustFolderNoteCounts(entry.folder, 1);
    m_wikiLinks.invalidate();
    if (wasUnknown && entry.fileSize >= 0)
        emit noteMetadataReady(relPath);
}

bool NoteCollection::hasParsedMetadata(const QString &relPath) const
{
    const NoteEntry *entry = note(relPath);
    return entry && entry->fileSize >= 0;
}

void NoteCollection::removeNoteEntry(const QString &relPath)
{
    const auto it = m_notes.constFind(relPath);
    if (it == m_notes.constEnd())
        return;
    adjustFolderNoteCounts(it.value().folder, -1);
    m_notes.remove(relPath);
    m_wikiLinks.invalidate();
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




void NoteCollection::saveIndexFileIfDirty()
{
    if (!m_indexDirty) {
        finishOperationPlanAfterIndexSave();
        return;
    }
    // Only a write that actually landed clears the flag. Clearing it
    // regardless left the sidecar stale with nothing remembering that it
    // needed rewriting, so the next session paid for a full rescan and any
    // later chance to retry was gone.
    if (m_indexFile.save(m_notes)) {
        m_indexDirty = false;
        // The plan exists to say "these files may not match the sidecar". A
        // sidecar that has just been written from the same state answers
        // that, and only then is the operation over.
        finishOperationPlanAfterIndexSave();
    }
}

void NoteCollection::beginOperationPlan(const QString &kind,
                                        const QJsonObject &payload,
                                        const QStringList &files)
{
    if (files.isEmpty())
        return;
    m_activeOperationPlan = m_operations.begin(kind, payload, files);
}

void NoteCollection::abandonOwnOperationPlan()
{
    if (m_activeOperationPlan.isEmpty())
        return;
    m_operations.finish(m_activeOperationPlan);
    m_activeOperationPlan.clear();
}

void NoteCollection::finishOperationPlanAfterIndexSave()
{
    if (m_activeOperationPlan.isEmpty() || m_indexDirty)
        return;
    m_operations.finish(m_activeOperationPlan);
    m_activeOperationPlan.clear();
}

void NoteCollection::dropJournalledEntriesFromCache(
    QHash<QString, NoteEntry> *cachedNotes)
{
    m_pendingOperations = m_operations.pending();
    if (m_pendingOperations.isEmpty() || !cachedNotes)
        return;
    // Whatever the sidecar says about these notes was written before an
    // operation that did not finish, so it is not evidence of anything. They
    // are parsed from the file instead, which is the only durable answer.
    for (const OperationJournal::Plan &plan : m_pendingOperations) {
        for (const QString &relPath : plan.files) {
            if (cachedNotes->remove(relPath) > 0)
                markIndexDirty();
        }
    }
}

void NoteCollection::resumePendingOperations()
{
    if (m_pendingOperations.isEmpty())
        return;
    const QList<OperationJournal::Plan> plans = m_pendingOperations;
    m_pendingOperations.clear();

    QStringList unfinished;
    for (const OperationJournal::Plan &plan : plans) {
        const QStringList remaining = plan.remaining();
        bool resumed = true;
        if (plan.kind == QLatin1String("tagRename")
            || plan.kind == QLatin1String("tagDelete")) {
            // Both are the same shape and both are idempotent: applying the
            // mapping again to a note that already has it changes nothing, so
            // finishing an interrupted run is simply running it over what is
            // left.
            const QString from =
                plan.payload.value(QStringLiteral("from")).toString();
            const QString to =
                plan.payload.value(QStringLiteral("to")).toString();
            beginOperationPlan(plan.kind, plan.payload, remaining);
            for (const QString &relPath : remaining) {
                NoteEntry *entry = const_cast<NoteEntry *>(note(relPath));
                if (!entry || !entry->meta.tags.contains(from))
                    continue;
                QStringList tags;
                for (const QString &tag : entry->meta.tags) {
                    const QString mapped = (tag == from) ? to : tag;
                    if (!mapped.isEmpty() && !tags.contains(mapped))
                        tags.append(mapped);
                }
                const NoteFrontMatter::Metadata before = entry->meta;
                entry->meta.tags = tags;
                if (rewriteFrontMatter(relPath, before)) {
                    markIndexDirty();
                } else {
                    entry->meta = before;
                    resumed = false;
                }
            }
        } else if (plan.kind == QLatin1String("wikiLinks")
                   && !remaining.isEmpty()) {
            // A link rewrite cannot be replayed from the plan alone — the
            // rename it followed has already happened — so what the plan
            // buys here is the report, plus the reparse above that keeps the
            // index honest about the files it did reach.
            resumed = false;
        }
        // Anything else — a single metadata edit interrupted before it wrote
        // — leaves nothing to finish. The file was not changed, and the
        // cached entry for it has already been discarded above.
        if (resumed)
            m_operations.finish(plan.id);
        else
            unfinished += remaining;
    }
    saveIndexFileIfDirty();
    if (!unfinished.isEmpty()) {
        unfinished.removeDuplicates();
        emit operationIncomplete(unfinished);
    }
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
    m_asyncWatcher.setFuture(QtConcurrent::mapped(listing.tasks, VaultScan::parseIndexTask));
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
    // The index now holds every note, which is what finishing an interrupted
    // multi-file operation needs: the startup path cannot do it before the
    // background scan has caught up.
    resumePendingOperations();
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
        // Signal the worker's own token first. QFuture::cancel() on a future
        // from QtConcurrent::run() only marks it cancelled for bookkeeping;
        // the walk carries on regardless, and dropping the token reference
        // without signalling it left that walk running to completion over a
        // subtree whose result is already discarded. A burst of watcher
        // events could accumulate one such walk per event.
        if (m_refreshCancel)
            m_refreshCancel->cancel();
        m_refreshCancel.reset();
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
        QtConcurrent::run(&VaultScan::buildRefreshResult, request));
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
        QtConcurrent::run(&VaultScan::parseSavedNoteTask,
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
            QtConcurrent::run(&VaultScan::parseSavedNoteTask,
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

    const QString path = m_indexFile.path();
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
        QtConcurrent::run(&VaultScan::writeIndexFileSnapshot,
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
            QtConcurrent::run(&VaultScan::writeIndexFileSnapshot,
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
    return WikiLinkIndex::extractLinks(body);
}

int NoteCollection::rewriteWikiTargetsInText(QString *text,
                                             const QSet<QString> &oldKeys,
                                             const QString &replacement)
{
    return WikiLinkIndex::rewriteTargetsInText(text, oldKeys, replacement);
}


QHash<QString, NoteCollection::RewriteSnapshot>
NoteCollection::snapshotNoteReferrers(const QString &relPath) const
{
    return m_wikiLinks.snapshotNoteReferrers(relPath);
}

QHash<QString, NoteCollection::RewriteSnapshot>
NoteCollection::snapshotFolderReferrers(const QString &oldPrefix) const
{
    return m_wikiLinks.snapshotFolderReferrers(oldPrefix);
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

    // Every referrer is named before the first one is rewritten. A crash part
    // way through used to leave some notes pointing at the new name and some
    // at the old, with nothing on disk saying that one operation was meant to
    // cover both; the next start can now at least reparse them all and tell
    // the user which ones were not reached.
    QJsonObject journalPayload;
    journalPayload.insert(QStringLiteral("kind"), plan.kind);
    journalPayload.insert(QStringLiteral("oldPath"), plan.oldPath);
    journalPayload.insert(QStringLiteral("newPath"), plan.newPath);
    journalPayload.insert(QStringLiteral("replacement"), replacement);
    beginOperationPlan(QStringLiteral("wikiLinks"), journalPayload, sorted);
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
        } else {
            emit aboutToWrite(absolutePath(actual));
            if (!writeTextFileAtomic(absolutePath(actual), frontMatter + body)) {
                failed.append(actual);
                continue;
            }
            m_operations.markDone(m_activeOperationPlan, referrer);
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
    } else {
        // Nothing was written, so there is no half-finished operation for the
        // next start to hear about.
        abandonOwnOperationPlan();
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


QString NoteCollection::resolveWikiTarget(const QString &target) const
{
    return m_wikiLinks.resolve(target);
}

QVariantMap NoteCollection::wikiTargetResolution(const QString &target) const
{
    return m_wikiLinks.resolution(target);
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
    return m_wikiLinks.headingsFor(relPath);
}

QVariantList NoteCollection::backlinksTo(const QString &relPath) const
{
    return m_wikiLinks.backlinksTo(relPath);
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
    emit aboutToWrite(absolutePath(relPath));
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
    m_collectionState.save();
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
    prepareOpenDocumentMutation(relPath);
    if (!QFile::rename(absolutePath(relPath), absolutePath(newRelPath))) {
        emit operationFailed(tr("Cannot rename \"%1\"").arg(entry->title));
        return false;
    }
    rebindOpenDocument(relPath, newRelPath);

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

    m_collectionState.save();
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
    prepareOpenDocumentMutation(relPath);
    if (!QFile::rename(absolutePath(relPath), absolutePath(newRelPath))) {
        emit operationFailed(tr("Cannot move \"%1\"").arg(entry->title));
        return false;
    }
    rebindOpenDocument(relPath, newRelPath);

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

    m_collectionState.save();
    saveIndexFileIfDirty();
    emit noteMoved(relPath, newRelPath);
    bump();
    dropNoteInSearch(relPath);
    reindexNoteInSearch(newRelPath);
    return true;
}

int NoteCollection::trashItemCount() const
{
    return m_trash.itemCount();
}

bool NoteCollection::emptyTrash()
{
    if (!isOpen() || refuseWhenReadOnly())
        return false;
    const bool ok = m_trash.empty();
    bump();
    return ok;
}

bool NoteCollection::moveToTrash(const QString &relPath)
{
    // The containment gate stays here, ahead of the store: the store is
    // handed an absolute path and does not re-derive it, so this is the only
    // place that decides whether the path is inside the vault at all.
    if (!ensureWithinRoot(relPath))
        return false;
    return m_trash.moveIn(absolutePath(relPath), nameOfRelPath(relPath));
}

bool NoteCollection::deleteNote(const QString &relPath)
{
    const NoteEntry *entry = note(relPath);
    if (!entry) {
        emit operationFailed(tr("No such note: %1").arg(relPath));
        return false;
    }
    const bool wasOpen = openDocumentIs(relPath);
    prepareOpenDocumentMutation(relPath);
    // The trash is this application's undo for a deletion, so what lands in
    // it has to be what the user last saw. An unsaved open note is written
    // first, and a note whose newest revision cannot be written is not
    // deleted at all.
    if (!persistOpenDocumentBeforeRemoval(relPath)) {
        emit operationFailed(
            tr("\"%1\" has unsaved changes that could not be saved, so it was "
               "not deleted").arg(entry->title));
        return false;
    }
    if (!moveToTrash(relPath)) {
        emit operationFailed(tr("Cannot delete \"%1\"").arg(entry->title));
        return false;
    }
    if (wasOpen)
        m_openDocument->closeDocument();

    const QString folderPath = entry->folder;
    m_manualOrder[folderPath].removeAll(nameOfRelPath(relPath));
    removeNoteEntry(relPath);
    if (m_lastOpenNote == relPath)
        m_lastOpenNote.clear();

    m_collectionState.save();
    markIndexDirty();
    saveIndexFileIfDirty();
    emit noteRemoved(relPath);
    bump();
    if (wasOpen)
        emit openNoteRemoved(relPath);
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
            rebindOpenDocument(key, newRelPath);
            emit noteMoved(key, newRelPath);
        }
    }
    m_wikiLinks.invalidate();
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
    const QString openRelPath = m_openDocument
        ? relativePath(m_openDocument->openFilePath()) : QString();
    if (openRelPath == relPath
        || openRelPath.startsWith(relPath + QLatin1Char('/')))
        prepareOpenDocumentMutation(openRelPath);
    if (!QDir().rename(absolutePath(relPath), absolutePath(newRelPath))) {
        emit operationFailed(tr("Cannot rename folder \"%1\"").arg(entry->name));
        return false;
    }

    renamePathsUnderFolder(relPath, newRelPath);
    m_collectionState.save();
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
    const QString openRelPath = m_openDocument
        ? relativePath(m_openDocument->openFilePath()) : QString();
    const bool removedOpen = openRelPath.startsWith(relPath + QLatin1Char('/'));
    if (removedOpen) {
        prepareOpenDocumentMutation(openRelPath);
        // Same rule as deleting the note directly: the folder about to move
        // into the trash contains the open note, so the trashed copy has to
        // be the newest revision of it.
        if (!persistOpenDocumentBeforeRemoval(openRelPath)) {
            emit operationFailed(
                tr("\"%1\" contains a note with unsaved changes that could not "
                   "be saved, so it was not deleted").arg(entry->name));
            return false;
        }
    }
    if (!moveToTrash(relPath)) {
        emit operationFailed(tr("Cannot delete folder \"%1\"").arg(entry->name));
        return false;
    }
    if (removedOpen)
        m_openDocument->closeDocument();

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

    m_collectionState.save();
    markIndexDirty();
    saveIndexFileIfDirty();
    bump();
    if (removedOpen)
        emit openNoteRemoved(openRelPath);
    // Reconcile drops every note that moved into the trash.
    syncSearchIndex();
    return true;
}

void NoteCollection::setFolderExpanded(const QString &relPath, bool expanded)
{
    auto it = m_folders.find(relPath);
    if (it == m_folders.end() || it->expanded == expanded || refuseWhenReadOnly())
        return;
    it->expanded = expanded;
    m_collectionState.save();
    bump();
}

void NoteCollection::setFolderColor(const QString &relPath, const QString &color)
{
    auto it = m_folders.find(relPath);
    if (it == m_folders.end() || it->color == color || refuseWhenReadOnly())
        return;
    it->color = color;
    m_collectionState.save();
    bump();
}

// ------------------------------------------------------------ metadata

// The three-way merge a metadata write performs. `disk` is what the file says
// now, `before` is what this collection believed before the edit, and `after`
// is what it believes now. Only the fields the edit actually changed are
// taken from `after`; everything else keeps what the file says, so a tag
// added by another tool between the last scan and this write survives an
// unrelated pin, and foreign front-matter keys are never replaced by a cached
// copy of themselves.
static NoteFrontMatter::Metadata mergeMetadata(
    const NoteFrontMatter::Metadata &disk,
    const NoteFrontMatter::Metadata &before,
    const NoteFrontMatter::Metadata &after)
{
    NoteFrontMatter::Metadata merged = disk;
    if (after.tags != before.tags)
        merged.tags = after.tags;
    if (after.pinned != before.pinned)
        merged.pinned = after.pinned;
    if (after.favorite != before.favorite)
        merged.favorite = after.favorite;
    if (after.goal != before.goal)
        merged.goal = after.goal;
    if (after.created != before.created)
        merged.created = after.created;
    return merged;
}

bool NoteCollection::rewriteFrontMatter(const QString &relPath,
                                        const NoteFrontMatter::Metadata &before)
{
    if (!ensureWithinRoot(relPath))
        return false;
    NoteEntry *entry = const_cast<NoteEntry *>(note(relPath));
    if (!entry)
        return false;

    const QString absPath = absolutePath(relPath);
    const QDateTime mtime = QFileInfo(absPath).lastModified();

    // The live session is the sole full-file writer for the open note. It
    // combines the repository's new metadata with the authoritative in-memory
    // body, drains any older snapshot, and reports the real persistence
    // result. Closed notes continue through the repository writer below.
    if (openDocumentIs(relPath)) {
        if (!m_openDocument->saveWithFrontMatter(
                NoteFrontMatter::serialize(entry->meta)))
            return false;
        restoreFileTime(absPath, mtime);
        reindexNoteInSearch(relPath);
        return true;
    }

    // The file's bytes are authoritative for the body (the open note's
    // unsaved edits flow through DocumentManager, not here). Read as bytes,
    // not as text: a metadata write must leave the body byte for byte as it
    // was, and decoding to a QString and re-encoding it normalizes CRLF line
    // endings and replaces every byte that is not valid UTF-8.
    bool ok = false;
    const QByteArray bytes = readFileBytes(absPath, &ok);
    if (!ok)
        return false;

    const NoteFrontMatter::Split split =
        NoteFrontMatter::split(QString::fromUtf8(bytes));
    // split() is byte-preserving on the decoded text, so the encoded block is
    // the file's leading bytes whenever the front matter itself decoded
    // cleanly. When it did not, there is no offset to splice at and the file
    // is left alone rather than rewritten from a lossy decode.
    const QByteArray blockBytes = split.block.toUtf8();
    if (!bytes.startsWith(blockBytes))
        return false;

    // What is on disk wins for every field this edit did not touch.
    const NoteFrontMatter::Metadata merged =
        mergeMetadata(NoteFrontMatter::parse(split.block), before, entry->meta);
    const QByteArray rewritten = NoteFrontMatter::serialize(merged).toUtf8()
        + bytes.mid(blockBytes.size());

    // Record the intent before touching the file. The write below restores
    // the modification time, so an equal-length change leaves the file
    // looking untouched to the freshness check: without this record, a crash
    // before the index sidecar is written would leave the stale cached
    // metadata trusted indefinitely, and later written back over the file.
    const bool ownPlan = m_activeOperationPlan.isEmpty();
    if (ownPlan) {
        m_activeOperationPlan = m_operations.begin(
            QStringLiteral("metadata"), QJsonObject(), {relPath});
    }

    // Announce the write first, then verify, then commit. Immediately before
    // the replacement the same bytes must still be there; anything else means
    // somebody wrote the file after it was read, and committing would
    // silently drop their change. An abandoned write leaves an own-write
    // registration behind for a write that never happened, which is why the
    // conflict is reported directly rather than left for the watcher to
    // notice.
    emit aboutToWrite(absPath);
    bool recheckOk = false;
    const QByteArray current = readFileBytes(absPath, &recheckOk);
    if (!recheckOk || current != bytes) {
        if (ownPlan)
            abandonOwnOperationPlan();
        emit noteChangedExternally(relPath);
        return false;
    }

    if (!writeFileBytesAtomic(absPath, rewritten)) {
        if (ownPlan)
            abandonOwnOperationPlan();
        return false;
    }
    restoreFileTime(absPath, mtime);
    entry->meta = merged;
    m_operations.markDone(m_activeOperationPlan, relPath);
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

    const NoteFrontMatter::Metadata before = entry->meta;
    entry->meta.tags = cleaned;
    if (!rewriteFrontMatter(relPath, before)) {
        entry->meta = before;
        emit operationFailed(tr("Cannot write \"%1\"").arg(entry->title));
        return false;
    }
    assignColorsToNewTags(entry->meta.tags);
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
        m_collectionState.save();
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
    const NoteFrontMatter::Metadata before = entry->meta;
    entry->meta.pinned = pinned;
    if (!rewriteFrontMatter(relPath, before)) {
        entry->meta = before;
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
    const NoteFrontMatter::Metadata before = entry->meta;
    entry->meta.favorite = favorite;
    if (!rewriteFrontMatter(relPath, before)) {
        entry->meta = before;
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
    const NoteFrontMatter::Metadata before = entry->meta;
    entry->meta.goal = clamped;
    if (!rewriteFrontMatter(relPath, before)) {
        entry->meta = before;
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
    if (m_tagColors.value(tag) == color || refuseWhenReadOnly())
        return;
    if (color.isEmpty())
        m_tagColors.remove(tag);
    else
        m_tagColors.insert(tag, color);
    m_collectionState.save();
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

    // One plan for the whole rename. Every note that carries the tag is named
    // before the first of them is rewritten, so a crash half way through is
    // visible at the next start as an operation to finish rather than as a
    // vault where some notes say `books` and some say `draft`.
    QStringList affected;
    const QStringList keys = m_notes.keys();
    for (const QString &key : keys) {
        if (m_notes.value(key).meta.tags.contains(oldName))
            affected.append(key);
    }
    affected.sort();
    QJsonObject payload;
    payload.insert(QStringLiteral("from"), oldName);
    payload.insert(QStringLiteral("to"), target);
    beginOperationPlan(QStringLiteral("tagRename"), payload, affected);

    bool anyFailed = false;
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
        const NoteFrontMatter::Metadata before = entry.meta;
        entry.meta.tags = tags;
        if (!rewriteFrontMatter(key, before)) {
            entry.meta = before;
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

    m_collectionState.save();
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
    QStringList affected;
    const QStringList keys = m_notes.keys();
    for (const QString &key : keys) {
        if (m_notes.value(key).meta.tags.contains(tag))
            affected.append(key);
    }
    affected.sort();
    QJsonObject payload;
    payload.insert(QStringLiteral("from"), tag);
    beginOperationPlan(QStringLiteral("tagDelete"), payload, affected);

    bool anyFailed = false;
    for (const QString &key : keys) {
        NoteEntry &entry = m_notes[key];
        if (!entry.meta.tags.contains(tag))
            continue;
        const NoteFrontMatter::Metadata before = entry.meta;
        entry.meta.tags.removeAll(tag);
        if (!rewriteFrontMatter(key, before)) {
            entry.meta = before;
            anyFailed = true;
        } else {
            const QFileInfo info(absolutePath(key));
            entry.modified = info.lastModified();
            entry.fileSize = info.size();
            markIndexDirty();
        }
    }
    m_tagColors.remove(tag);
    m_collectionState.save();
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
    if (refuseWhenReadOnly())
        return false;
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
    m_collectionState.save();
    bump();
    return true;
}

// ---------------------------------------------------- workspace state

void NoteCollection::setLastOpenNote(const QString &relPath)
{
    if (m_lastOpenNote == relPath || m_readOnly)
        return;
    m_lastOpenNote = relPath;
    m_collectionState.save();
    // Deliberately no bump: which note is open is not collection content.
}

// ------------------------------------------------------------- backups
//
// The rotation policy, the directory layout and the pool-thread copy live in
// NoteBackupStore. What stays here is the mapping from an absolute path to a
// note in this collection, and the body-preview rule, which has to be the
// same analyzeBody the scan and the status bar use so a backup listing and
// the note list never describe the same text differently.

void NoteCollection::setClockForTesting(std::function<QDateTime()> clock)
{
    m_backups.setClockForTesting(std::move(clock));
}

void NoteCollection::setBackupWriterForTesting(
    std::function<void(const QString &, const QString &, const QByteArray &)>
        writer)
{
    m_backups.setSnapshotWriterForTesting(std::move(writer));
}

void NoteCollection::setClockOffsetForTesting(int secs)
{
    if (secs == 0)
        m_backups.setClockForTesting(nullptr);
    else
        m_backups.setClockForTesting(
            [secs]() { return QDateTime::currentDateTime().addSecs(secs); });
}

void NoteCollection::backupBeforeOverwrite(const QString &absPath)
{
    if (refuseWhenReadOnly())
        return;
    m_backups.backupBeforeOverwrite(relativePath(absPath), absPath);
}

QVariantList NoteCollection::backupsFor(const QString &relPath) const
{
    return m_backups.listFor(relPath, [](const QString &body) {
        return VaultScan::analyzeBody(body).snippet;
    });
}

QString NoteCollection::backupBody(const QString &relPath,
                                   const QString &fileName) const
{
    return m_backups.bodyOf(relPath, fileName);
}

// ------------------------------------------------------ crash recovery

QString NoteCollection::journalPathFor(const QString &relPath) const
{
    return m_recoveryJournals.journalPathFor(relPath);
}

QVariantList NoteCollection::recoveryEntries() const
{
    QVariantList entries;
    const QStringList pending = m_recoveryJournals.pending();
    for (const QString &relPath : pending) {
        const QString journal = m_recoveryJournals.journalPathFor(relPath);
        bool ok = false;
        const QString text = m_recoveryJournals.readJournal(relPath, &ok);
        if (!ok)
            continue;
        const NoteFrontMatter::Split split = NoteFrontMatter::split(text);
        QString title = nameOfRelPath(relPath);
        if (title.endsWith(mdSuffix, Qt::CaseInsensitive))
            title.chop(mdSuffix.size());
        entries.append(QVariantMap{
            {QStringLiteral("relPath"), relPath},
            {QStringLiteral("title"), title},
            {QStringLiteral("preview"), VaultScan::analyzeBody(split.body).snippet},
            {QStringLiteral("journalPath"), journal},
        });
    }
    return entries;
}

bool NoteCollection::restoreRecovery(const QString &relPath)
{
    if (!m_recoveryJournals.isPending(relPath) || !ensureWithinRoot(relPath))
        return false;
    bool ok = false;
    const QString text = m_recoveryJournals.readJournal(relPath, &ok);
    if (!ok) {
        emit operationFailed(tr("Cannot read the recovered content"));
        return false;
    }

    // The journal is a full file image (front-matter included). Restore
    // recreates a deleted note's folder if needed.
    const QString absPath = absolutePath(relPath);
    QDir().mkpath(QFileInfo(absPath).absolutePath());
    emit aboutToWrite(absPath);
    if (!writeTextFileAtomic(absPath, text)) {
        emit operationFailed(tr("Cannot restore \"%1\"").arg(relPath));
        return false;
    }
    if (!m_recoveryJournals.resolve(relPath)) {
        // The note now holds the recovered text, but the journal is still on
        // disk. Saying so is the whole point: a journal nobody could delete
        // is offered again at the next launch as evidence of a crash that has
        // already been dealt with, and accepting it then would put this same
        // text back over whatever has been written since.
        emit operationFailed(
            tr("\"%1\" was restored, but its recovery file could not be "
               "removed and will be offered again").arg(relPath));
    }

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
    if (!m_recoveryJournals.isPending(relPath) || !ensureWithinRoot(relPath))
        return;
    if (!m_recoveryJournals.resolve(relPath)) {
        // Still pending, deliberately: the user discarded it, but the file
        // that says otherwise is still there.
        emit operationFailed(
            tr("The recovery file for \"%1\" could not be removed").arg(relPath));
    }
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
    if (!fileText.isNull()) {
        m_searchFeed.reindexNoteFromText(
            m_rootPath, relPath, fileText, info.size(),
            info.lastModified().toMSecsSinceEpoch());
    } else {
        m_searchFeed.reindexNote(m_rootPath, relPath);
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
//
// The file itself is CollectionStateStore's; what stays here is the two
// directions of the conversion between it and the live index.

void NoteCollection::loadCollectionFile(bool indexSafeLastOpen)
{
    const CollectionStateStore::Snapshot state = m_collectionState.load();
    m_tagColors = state.tagColors;
    m_manualOrder = state.manualOrder;

    // Folder appearance is applied against the folders that actually exist.
    // An entry for a folder deleted outside the app is simply dropped, and
    // the next save stops carrying it.
    for (auto it = state.folders.begin(); it != state.folders.end(); ++it) {
        auto folderIt = m_folders.find(it.key());
        if (folderIt == m_folders.end())
            continue;
        folderIt->color = it.value().color;
        folderIt->expanded = it.value().expanded;
    }

    // The store has already refused anything outside the vault. What remains
    // is whether the index knows the note: during an asynchronous open the
    // background listing has not reached it yet, and startup wants to open it
    // now rather than after the scan, so it goes in as a placeholder entry.
    m_lastOpenNote = state.lastOpenNote;
    if (m_lastOpenNote.isEmpty() || m_notes.contains(m_lastOpenNote))
        return;
    if (!indexSafeLastOpen) {
        m_lastOpenNote.clear();
        return;
    }
    const QFileInfo info(absolutePath(m_lastOpenNote));
    insertNoteEntry(m_lastOpenNote, VaultScan::placeholderEntry(m_lastOpenNote, info));
}

CollectionStateStore::Snapshot NoteCollection::collectionStateSnapshot() const
{
    CollectionStateStore::Snapshot snapshot;
    snapshot.tagColors = m_tagColors;
    snapshot.manualOrder = m_manualOrder;
    snapshot.lastOpenNote = m_lastOpenNote;
    for (const FolderEntry &entry : m_folders)
        snapshot.folders.insert(entry.relPath, {entry.color, entry.expanded});
    return snapshot;
}

// ----------------------------------------------------------------- misc

void NoteCollection::bump()
{
    ++m_revision;
    emit revisionChanged();
}

void NoteCollection::setSearchIndex(CollectionSearchIndex *index)
{
    m_searchFeed.setIndex(index);
    if (m_searchFeed.hasIndex() && isOpen())
        syncSearchIndex();
}

// The repository's side of reconcile: what every indexed note looks like to
// the freshness check, without reading a body.
QList<ReconcileEntry> NoteCollection::searchReconcileListing() const
{
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
    return listing;
}

void NoteCollection::syncSearchIndex()
{
    m_searchFeed.syncTo(m_rootPath);
}

void NoteCollection::reindexNoteInSearch(const QString &relPath)
{
    m_searchFeed.reindexNote(m_rootPath, relPath);
}

void NoteCollection::dropNoteInSearch(const QString &relPath)
{
    m_searchFeed.dropNote(m_rootPath, relPath);
}
