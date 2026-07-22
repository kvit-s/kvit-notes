// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef FILEWATCHER_H
#define FILEWATCHER_H

#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

class QDirIterator;

// External-file watcher (§12.1). Wraps QFileSystemWatcher over the notes root and
// feeds NoteCollection a debounced re-scan when notes or folders change on disk
// outside the app. The app's own writes are distinguished from external ones by a
// short-lived own-write guard (noteOwnWrite, wired to the save hook): a change to
// a guarded path within the guard window is the app's own and does not
// self-trigger. A change to the open note that is not the app's own raises
// noteChangedExternally, which the UI turns into a keep-mine/load-theirs conflict
// when that note is also dirty. The core logic runs through feedChange, so it is
// deterministically testable without waiting on real filesystem events.
class FileWatcher : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool watching READ watching NOTIFY watchingChanged)
    // True once any registration has been refused (watch limit reached, path
    // vanished). Coverage is then incomplete: some external edits will go
    // unseen, so the UI must stop implying that every outside change is
    // noticed and offer a manual re-scan instead.
    Q_PROPERTY(bool watchDegraded READ watchDegraded NOTIFY watchDegradedChanged)
public:
    explicit FileWatcher(QObject *parent = nullptr);
    // Out of line: the discovery iterator is only forward-declared here.
    ~FileWatcher() override;

    bool watching() const { return !m_root.isEmpty(); }
    bool watchDegraded() const { return m_watchDegraded; }

    void setDebounceMs(int ms) { m_debounce.setInterval(ms); }
    void setGuardMs(int ms) { m_guardMs = ms; }

    // Start watching the notes tree (root + its folders). Restarting rewatches.
    Q_INVOKABLE void watchRoot(const QString &root);
    // Watch the open note closely for external edits. Exactly one note is open
    // at a time, so this replaces any previous note watch rather than adding to
    // it — accumulating watches is how a long session runs into the kernel's
    // per-process limit.
    Q_INVOKABLE void watchFile(const QString &absPath);
    Q_INVOKABLE void unwatchFile(const QString &absPath);
    Q_INVOKABLE void stop();

    // Re-establish the current note's watch. An atomic save replaces the file,
    // and the kernel watch belongs to the inode that was replaced, so after any
    // write to the open note the registration has to be renewed.
    Q_INVOKABLE void rewatchCurrentFile();
    QString currentFile() const { return m_currentFile; }

    // Guard a path the app is about to write, so the ensuing change is its
    // own. The directory the file lives in is guarded too: a note that is not
    // the open one is covered only by its parent directory's watch, so the
    // sole event the app sees for its own write to it is a directoryChanged,
    // and a guard recorded against the file alone never matches. Writing a
    // file also mutates its directory, so the two guards describe one write.
    Q_INVOKABLE void noteOwnWrite(const QString &absPath);

    // Guard a directory the app is about to mutate without writing a file
    // through noteOwnWrite -- creating, renaming, moving or deleting entries.
    // Unlike a file guard this is not consumed by the first event: one
    // internal operation commonly produces several directory notifications
    // (a temporary file appearing, then the rename over the target), so the
    // guard covers a batch and lapses on time instead.
    Q_INVOKABLE void noteOwnDirectoryChange(const QString &absDirPath);

    // The change entry point — the real watcher's signals and the tests both
    // call this. isFile marks a specific note (vs a directory) change.
    Q_INVOKABLE void feedChange(const QString &path, bool isFile = false);

    // Test helper: current monotonic clock in ms (so tests can reason about the
    // guard window without wall-clock).
    static qint64 nowMs();
    int treeWatchRefreshCountForTests() const { return m_treeWatchRefreshCount; }
    // Guards still held. Zero once the window has passed and any event has
    // been fed: an unused guard must not survive its window, or a session
    // that opens many notes accumulates one entry per path it ever wrote.
    int pendingGuardCountForTests() const
    {
        return m_ownWrites.size() + m_ownDirWrites.size();
    }
    // True while tree discovery is still walking. Discovery yields to the
    // event loop every DiscoverySliceEntries directories, so a large vault
    // does not freeze the GUI thread for the length of the walk.
    bool discoveryPending() const { return m_discovery != nullptr; }
    int watchedDirectoryCountForTests() const { return m_watchedDirs.size(); }
    static constexpr int DiscoverySliceEntries = 256;

signals:
    // Debounced: the tree changed externally — re-scan the collection.
    void externalChange();
    void externalChangePaths(const QStringList &absPaths);
    // A specific note changed externally (not the app's own write).
    void noteChangedExternally(const QString &absPath);
    void watchingChanged();
    void watchDegradedChanged();

public:
    // Test seam: what the watcher believes it has registered, so a test can
    // assert the absence of an accumulated watch rather than inferring it.
    QStringList watchedFilesForTests() const { return m_watcher.files(); }

private:
    bool isOwnChange(const QString &path, bool isFile);
    // Guards are keyed by canonical path. The path the app announces and the
    // path the notification carries do not have to be spelled the same: macOS
    // watches directories through FSEvents, which reports every path in its
    // canonical form, so a vault under /var - where a temporary directory
    // lives - is announced as /var/... and reported as /private/var/..., and
    // a guard keyed by the announced string never matched a single one of its
    // own writes.
    static QString guardKey(const QString &path);
    void pruneExpiredGuards();
    void addTreeWatches(const QString &root);
    void continueDiscovery();
    void finishDiscovery();
    void emitDebouncedExternalChange();
    // Every registration goes through here so a refusal is recorded once,
    // rather than being discarded at a dozen call sites. The watcher's own
    // path lists are copies of every registered string, so membership is
    // answered from the sets below instead: consulting the watcher made
    // registering N directories cost O(N^2) string copies.
    bool addPathChecked(const QString &path, bool isDirectory);
    void forgetPath(const QString &path);
    void clearRegistrations();
    void setWatchDegraded(bool degraded);

    // A file guard, which has to recognize the app's own write across as many
    // notifications as the platform chooses to send for it. `stamped` records
    // whether the file's size and modification time have been read yet; from
    // the second notification on, matching that reading means nothing has
    // happened to the file since the app's own write, and a different one
    // means somebody else has written it and the guard is done.
    struct OwnWrite {
        qint64 expiryMs = 0;
        qint64 createdMs = 0;
        bool stamped = false;
        qint64 size = -1;
        qint64 modifiedMs = -1;
    };

    // How long an unstamped guard may wait for the notification belonging to
    // the write it was created for. Past it the guard stops claiming events:
    // the app announces a write and then performs it, so its own notification
    // arrives within milliseconds, and a first notification arriving a quarter
    // of a second later is somebody else's edit. Windows needs this because
    // the notification for a QSaveFile replacement can fail to arrive at all,
    // and without the limit the next external edit inherited the guard and
    // vanished - the conflict banner the watcher exists to raise.
    static constexpr qint64 UnstampedSettleMs = 250;

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QHash<QString, OwnWrite> m_ownWrites;    // file absPath -> guard
    QHash<QString, qint64> m_ownDirWrites;   // dir absPath  -> guard expiry (ms)
    QStringList m_pendingExternalPaths;
    QSet<QString> m_watchedFiles;
    QSet<QString> m_watchedDirs;
    // Directories this discovery pass has seen, so registrations for
    // directories that have since disappeared can be dropped when it ends.
    QSet<QString> m_discoveredDirs;
    std::unique_ptr<QDirIterator> m_discovery;
    QTimer m_discoverySlice;
    QString m_root;
    QString m_currentFile;   // the open note, watched closely; "" when none
    int m_guardMs = 1500;
    int m_treeWatchRefreshCount = 0;
    bool m_watchDegraded = false;
};

#endif // FILEWATCHER_H
