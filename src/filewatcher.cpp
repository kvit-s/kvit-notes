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

void FileWatcher::addTreeWatches(const QString &root)
{
    if (!QFileInfo::exists(root))
        return;
    ++m_treeWatchRefreshCount;
    m_watcher.addPath(root);
    // Watch every folder so an add/rename/delete anywhere in the tree fires a
    // directoryChanged. The .kvit control directory is skipped — the app owns it.
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString dir = it.next();
        if (dir.contains(QStringLiteral("/.kvit")))
            continue;
        m_watcher.addPath(dir);
    }
}

void FileWatcher::watchFile(const QString &absPath)
{
    if (!absPath.isEmpty() && QFileInfo::exists(absPath))
        m_watcher.addPath(absPath);
}

void FileWatcher::unwatchFile(const QString &absPath)
{
    if (!absPath.isEmpty())
        m_watcher.removePath(absPath);
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
    if (isOwnWrite(path))
        return;   // the app's own write — not an external change

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
