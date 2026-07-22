// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "filewatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>

namespace {
// A process-wide monotonic clock. QElapsedTimer avoids wall-clock jumps and the
// scripting Date restriction does not apply to C++.
QElapsedTimer &monoClock()
{
    static QElapsedTimer t = [] { QElapsedTimer e; e.start(); return e; }();
    return t;
}
} // namespace

qint64 FileWatcher::nowMs()
{
    return monoClock().elapsed();
}

FileWatcher::FileWatcher(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);
    connect(&m_debounce, &QTimer::timeout,
            this, &FileWatcher::emitDebouncedExternalChange);
    m_discoverySlice.setSingleShot(true);
    m_discoverySlice.setInterval(0);
    connect(&m_discoverySlice, &QTimer::timeout,
            this, &FileWatcher::continueDiscovery);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &path) { feedChange(path, false); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) { feedChange(path, true); });
}

FileWatcher::~FileWatcher() = default;

void FileWatcher::watchRoot(const QString &root)
{
    stop();
    m_root = root;
    if (root.isEmpty())
        return;               // stop() already reported the transition
    // A root that does not exist watches nothing. Reporting `watching` while
    // no registration exists tells the UI every outside edit is noticed, and
    // none is.
    if (!QFileInfo::exists(root))
        setWatchDegraded(true);
    else
        addTreeWatches(root);
    emit watchingChanged();
}

bool FileWatcher::addPathChecked(const QString &path, bool isDirectory)
{
    if (path.isEmpty())
        return false;
    QSet<QString> &registered = isDirectory ? m_watchedDirs : m_watchedFiles;
    // Already registered: QFileSystemWatcher returns false for a duplicate,
    // which is success as far as coverage is concerned.
    if (registered.contains(path))
        return true;
    if (m_watcher.addPath(path)) {
        registered.insert(path);
        return true;
    }
    setWatchDegraded(true);
    return false;
}

void FileWatcher::forgetPath(const QString &path)
{
    if (path.isEmpty())
        return;
    m_watcher.removePath(path);
    m_watchedFiles.remove(path);
    m_watchedDirs.remove(path);
}

void FileWatcher::clearRegistrations()
{
    const QStringList files = m_watcher.files();
    const QStringList dirs = m_watcher.directories();
    if (!files.isEmpty())
        m_watcher.removePaths(files);
    if (!dirs.isEmpty())
        m_watcher.removePaths(dirs);
    m_watchedFiles.clear();
    m_watchedDirs.clear();
    m_discoveredDirs.clear();
    m_discovery.reset();
    m_discoverySlice.stop();
}

void FileWatcher::setWatchDegraded(bool degraded)
{
    if (m_watchDegraded == degraded)
        return;
    m_watchDegraded = degraded;
    emit watchDegradedChanged();
}

void FileWatcher::addTreeWatches(const QString &root)
{
    if (!QFileInfo::exists(root))
        return;
    ++m_treeWatchRefreshCount;
    m_discoveredDirs.clear();
    m_discoveredDirs.insert(root);
    addPathChecked(root, true);
    // Watch every folder so an add/rename/delete anywhere in the tree fires a
    // directoryChanged. The .kvit control directory is skipped — the app owns it.
    m_discovery = std::make_unique<QDirIterator>(
        root, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    continueDiscovery();
}

// One slice of the walk. A vault with thousands of folders takes long enough
// to traverse and register that doing it in one go visibly stalls the window,
// and this runs on every debounced refresh, not only at startup. Yielding to
// the event loop between slices keeps the app responsive; the registrations
// themselves must stay on this thread, since QFileSystemWatcher belongs to it.
void FileWatcher::continueDiscovery()
{
    if (!m_discovery)
        return;
    int budget = DiscoverySliceEntries;
    while (m_discovery->hasNext()) {
        const QString dir = m_discovery->next();
        if (dir.contains(QStringLiteral("/.kvit")))
            continue;
        m_discoveredDirs.insert(dir);
        addPathChecked(dir, true);
        if (--budget <= 0) {
            m_discoverySlice.start();
            return;
        }
    }
    finishDiscovery();
}

void FileWatcher::finishDiscovery()
{
    m_discovery.reset();
    m_discoverySlice.stop();
    // Reconcile rather than rebuild: a directory that has gone away leaves a
    // dead registration behind, and re-adding every surviving one on each
    // refresh is what made a refresh cost O(N^2).
    const QSet<QString> stale = m_watchedDirs - m_discoveredDirs;
    for (const QString &dir : stale)
        forgetPath(dir);
    m_discoveredDirs.clear();
    // The open note lives inside the tree and is watched as a file in its own
    // right; a tree refresh is a good moment to confirm that registration is
    // still alive.
    if (!m_currentFile.isEmpty())
        addPathChecked(m_currentFile, false);
}

void FileWatcher::watchFile(const QString &absPath)
{
    if (m_currentFile == absPath) {
        // Same note: renew rather than return, since the caller reaches here
        // after a save that may have replaced the file underneath the watch.
        if (!absPath.isEmpty())
            addPathChecked(absPath, false);
        return;
    }

    // Exactly one note is open, so the previous note's watch is dead weight.
    if (!m_currentFile.isEmpty())
        forgetPath(m_currentFile);

    m_currentFile = absPath;
    if (absPath.isEmpty())
        return;
    if (!QFileInfo::exists(absPath)) {
        setWatchDegraded(true);
        return;
    }
    addPathChecked(absPath, false);
}

void FileWatcher::rewatchCurrentFile()
{
    if (m_currentFile.isEmpty())
        return;
    if (!QFileInfo::exists(m_currentFile))
        return;
    // An atomic replacement leaves the watcher holding a registration against
    // the old inode. Dropping it first forces a genuinely new one; addPath
    // alone would see the path already listed and do nothing.
    forgetPath(m_currentFile);
    addPathChecked(m_currentFile, false);
}

void FileWatcher::unwatchFile(const QString &absPath)
{
    if (absPath.isEmpty())
        return;
    forgetPath(absPath);
    if (m_currentFile == absPath)
        m_currentFile.clear();
}

void FileWatcher::stop()
{
    const bool wasWatching = watching();
    clearRegistrations();
    m_debounce.stop();
    m_pendingExternalPaths.clear();
    m_ownWrites.clear();
    m_ownDirWrites.clear();
    m_root.clear();
    m_currentFile.clear();
    setWatchDegraded(false);
    // stop() called directly is as much a transition as watchRoot("") is:
    // `watching` just became false and a binding on it has to see that.
    if (wasWatching)
        emit watchingChanged();
}

QString FileWatcher::guardKey(const QString &path)
{
    if (path.isEmpty())
        return path;
    const QString canonical = QFileInfo(path).canonicalFilePath();
    // A path that does not exist has no canonical form - a delete guard is
    // recorded after the entry is gone - so the cleaned absolute path stands
    // in, and both sides of the comparison reach it the same way.
    return canonical.isEmpty() ? QDir::cleanPath(QFileInfo(path).absoluteFilePath())
                               : canonical;
}

void FileWatcher::noteOwnWrite(const QString &absPath)
{
    if (absPath.isEmpty())
        return;
    pruneExpiredGuards();
    OwnWrite guard;
    guard.createdMs = nowMs();
    guard.expiryMs = guard.createdMs + m_guardMs;
    m_ownWrites.insert(guardKey(absPath), guard);
    // The write mutates the containing directory too, and for every note but
    // the open one that directory's notification is the only event the app
    // receives. Guarding just the file leaves that event looking external.
    noteOwnDirectoryChange(QFileInfo(absPath).absolutePath());
}

void FileWatcher::noteOwnDirectoryChange(const QString &absDirPath)
{
    if (absDirPath.isEmpty())
        return;
    pruneExpiredGuards();
    const qint64 expiry = nowMs() + m_guardMs;
    const QString key = guardKey(absDirPath);
    const auto it = m_ownDirWrites.find(key);
    if (it == m_ownDirWrites.end())
        m_ownDirWrites.insert(key, expiry);
    else
        it.value() = qMax(it.value(), expiry);
}

// Guards that were never matched used to stay forever, one per path the app
// ever wrote, so a long session accumulated an entry for every note it had
// touched. A guard is only meaningful inside its window, so anything past it
// is discarded here rather than waiting for an event that may never come.
void FileWatcher::pruneExpiredGuards()
{
    const qint64 now = nowMs();
    for (auto it = m_ownWrites.begin(); it != m_ownWrites.end();)
        it = it.value().expiryMs < now ? m_ownWrites.erase(it) : std::next(it);
    for (auto it = m_ownDirWrites.begin(); it != m_ownDirWrites.end();)
        it = it.value() < now ? m_ownDirWrites.erase(it) : std::next(it);
}

bool FileWatcher::isOwnChange(const QString &path, bool isFile)
{
    pruneExpiredGuards();
    if (isFile) {
        // The guard is not spent by the first notification, because one save
        // does not produce one notification everywhere: a save replaces the
        // file through QSaveFile, and Windows reports the replacement and the
        // write separately against the same path, which left the second event
        // looking like an outside edit of a note the app had just written.
        //
        // Nor does it swallow everything inside its window, which would hide
        // a real edit arriving moments after a save - the case the conflict
        // banner exists for. What it compares is the file itself: the first
        // notification reads the size and modification time, and any further
        // notification that finds them unchanged is another report of the
        // same write. A reading that differs means somebody else has written
        // the file, so the guard is finished and the change is external.
        const auto it = m_ownWrites.find(guardKey(path));
        if (it == m_ownWrites.end())
            return false;
        if (nowMs() > it.value().expiryMs) {
            m_ownWrites.erase(it);
            return false;
        }
        // An unstamped guard past the settle window has missed its own
        // notification, so the next event is not the write it was created
        // for.
        if (!it.value().stamped
            && nowMs() - it.value().createdMs > UnstampedSettleMs) {
            m_ownWrites.erase(it);
            return false;
        }
        const QFileInfo info(path);
        const qint64 size = info.exists() ? info.size() : -1;
        const qint64 modifiedMs = info.exists()
            ? info.lastModified().toMSecsSinceEpoch()
            : -1;
        if (!it.value().stamped) {
            it.value().stamped = true;
            it.value().size = size;
            it.value().modifiedMs = modifiedMs;
            return true;
        }
        if (it.value().size == size && it.value().modifiedMs == modifiedMs)
            return true;
        m_ownWrites.erase(it);
        return false;
    }

    // A directory guard covers a batch, not a single event: one save writes a
    // temporary file and renames it over the target, which is two
    // notifications for the same directory, and a note collection operation
    // can touch several entries. It therefore lapses on time rather than
    // being consumed by the first event that matches it.
    const QString key = guardKey(path);
    const auto it = m_ownDirWrites.constFind(key);
    if (it != m_ownDirWrites.constEnd() && nowMs() <= it.value())
        return true;
#ifdef Q_OS_MACOS
    // macOS reports a change against a directory above the one that changed:
    // FSEvents named the vault root for a note the app had just deleted from
    // a folder inside it, so a guard on that folder matched nothing.
    //
    //   guarded  .../test_filewatcher-tnGWIm/from
    //   reported .../test_filewatcher-tnGWIm
    //
    // A guarded directory under the reported one therefore answers for the
    // event there: writing inside a folder changes every folder above it, and
    // the app had just said it was writing there. It costs a real change to
    // the reported directory made by somebody else while a guard below it is
    // live, which is why it is not done on the platforms that name the
    // directory that actually changed - on Linux it swallowed the external
    // changes the Qt Quick suites wait for and hung them.
    const QString prefix = key.endsWith(QLatin1Char('/'))
        ? key
        : key + QLatin1Char('/');
    const qint64 now = nowMs();
    for (auto guard = m_ownDirWrites.constBegin();
         guard != m_ownDirWrites.constEnd(); ++guard) {
        if (now <= guard.value() && guard.key().startsWith(prefix))
            return true;
    }
#endif
    return false;
}

void FileWatcher::feedChange(const QString &path, bool isFile)
{
    if (isOwnChange(path, isFile)) {
        // The app's own write is not an external change, but it is exactly what
        // destroys the watch: saves go through QSaveFile, which renames a temp
        // file over the target, so the registration now refers to a replaced
        // inode. Renew it here — this is the only notification the app gets
        // that the file it cares about was swapped.
        if (path == m_currentFile)
            rewatchCurrentFile();
        return;
    }

    if (!m_pendingExternalPaths.contains(path))
        m_pendingExternalPaths.append(path);

    // A file (note) change that is not our own is a candidate conflict.
    if (isFile)
        emit noteChangedExternally(path);

    // Any external change re-scans the tree, debounced so a burst of events
    // (a git checkout, a folder copy) coalesces into one re-scan. Re-add watches
    // in the debounced handler, not per raw event.
    m_debounce.start();
}

void FileWatcher::emitDebouncedExternalChange()
{
    const QStringList paths = m_pendingExternalPaths;
    m_pendingExternalPaths.clear();
    if (!m_root.isEmpty())
        addTreeWatches(m_root);
    emit externalChange();
    emit externalChangePaths(paths);
}
