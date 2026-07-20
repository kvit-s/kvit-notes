// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "filewatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
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
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this](const QString &path) { feedChange(path, false); });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &path) { feedChange(path, true); });
}

void FileWatcher::watchRoot(const QString &root)
{
    stop();
    m_root = root;
    if (!root.isEmpty())
        addTreeWatches(root);
    emit watchingChanged();
}

bool FileWatcher::addPathChecked(const QString &path)
{
    if (path.isEmpty())
        return false;
    // Already registered: QFileSystemWatcher returns false for a duplicate,
    // which is success as far as coverage is concerned.
    if (m_watcher.files().contains(path) || m_watcher.directories().contains(path))
        return true;
    if (m_watcher.addPath(path))
        return true;
    setWatchDegraded(true);
    return false;
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
    addPathChecked(root);
    // Watch every folder so an add/rename/delete anywhere in the tree fires a
    // directoryChanged. The .kvit control directory is skipped — the app owns it.
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString dir = it.next();
        if (dir.contains(QStringLiteral("/.kvit")))
            continue;
        addPathChecked(dir);
    }
    // The open note lives inside the tree and is watched as a file in its own
    // right; a tree refresh is a good moment to confirm that registration is
    // still alive.
    if (!m_currentFile.isEmpty())
        addPathChecked(m_currentFile);
}

void FileWatcher::watchFile(const QString &absPath)
{
    if (m_currentFile == absPath) {
        // Same note: renew rather than return, since the caller reaches here
        // after a save that may have replaced the file underneath the watch.
        if (!absPath.isEmpty())
            addPathChecked(absPath);
        return;
    }

    // Exactly one note is open, so the previous note's watch is dead weight.
    if (!m_currentFile.isEmpty())
        m_watcher.removePath(m_currentFile);

    m_currentFile = absPath;
    if (absPath.isEmpty())
        return;
    if (!QFileInfo::exists(absPath)) {
        setWatchDegraded(true);
        return;
    }
    addPathChecked(absPath);
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
    m_watcher.removePath(m_currentFile);
    addPathChecked(m_currentFile);
}

void FileWatcher::unwatchFile(const QString &absPath)
{
    if (absPath.isEmpty())
        return;
    m_watcher.removePath(absPath);
    if (m_currentFile == absPath)
        m_currentFile.clear();
}

void FileWatcher::stop()
{
    const QStringList files = m_watcher.files();
    const QStringList dirs = m_watcher.directories();
    if (!files.isEmpty())
        m_watcher.removePaths(files);
    if (!dirs.isEmpty())
        m_watcher.removePaths(dirs);
    m_debounce.stop();
    m_pendingExternalPaths.clear();
    m_root.clear();
    m_currentFile.clear();
    setWatchDegraded(false);
}

void FileWatcher::noteOwnWrite(const QString &absPath)
{
    m_ownWrites.insert(absPath, nowMs() + m_guardMs);
}

bool FileWatcher::isOwnWrite(const QString &path)
{
    const auto it = m_ownWrites.constFind(path);
    if (it == m_ownWrites.constEnd())
        return false;
    const qint64 expiry = it.value();
    // Consume the guard: the app's write produces one change event.
    m_ownWrites.remove(path);
    return nowMs() <= expiry;
}

void FileWatcher::feedChange(const QString &path, bool isFile)
{
    if (isOwnWrite(path)) {
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
