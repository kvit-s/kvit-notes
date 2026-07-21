// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef FILEWATCHER_H
#define FILEWATCHER_H

#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QHash>
#include <QString>
#include <QStringList>

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

    // Guard a path the app is about to write, so the ensuing change is its own.
    Q_INVOKABLE void noteOwnWrite(const QString &absPath);

    // The change entry point — the real watcher's signals and the tests both
    // call this. isFile marks a specific note (vs a directory) change.
    Q_INVOKABLE void feedChange(const QString &path, bool isFile = false);

    // Test helper: current monotonic clock in ms (so tests can reason about the
    // guard window without wall-clock).
    static qint64 nowMs();
    int treeWatchRefreshCountForTests() const { return m_treeWatchRefreshCount; }

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
    bool isOwnWrite(const QString &path);
    void addTreeWatches(const QString &root);
    void emitDebouncedExternalChange();
    // Every registration goes through here so a refusal is recorded once,
    // rather than being discarded at a dozen call sites.
    bool addPathChecked(const QString &path);
    void setWatchDegraded(bool degraded);

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QHash<QString, qint64> m_ownWrites;   // absPath -> guard expiry (ms)
    QStringList m_pendingExternalPaths;
    QString m_root;
    QString m_currentFile;   // the open note, watched closely; "" when none
    int m_guardMs = 1500;
    int m_treeWatchRefreshCount = 0;
    bool m_watchDegraded = false;
};

#endif // FILEWATCHER_H
