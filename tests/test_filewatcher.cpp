// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QSaveFile>
#include "filewatcher.h"

// The external-file watcher. The debounce, the own-write guard,
// and the external/internal discriminator run through feedChange(), so they are
// tested deterministically; one test also exercises the real QFileSystemWatcher
// over a temp directory.
class TestFileWatcher : public QObject
{
    Q_OBJECT

private slots:
    void externalFileChangeIsAnnouncedAndRescans();
    void ownWriteDoesNotSelfTrigger();
    void guardExpiresSoAStaleGuardIsExternal();
    void burstOfChangesDebouncesToOneRescan();
    void burstOfChangesRearmsTreeWatchesOnce();
    void directoryChangeRescansWithoutNoteConflict();
    void realWatcherSeesAnAddedFile();
    void watchSurvivesAtomicReplacement();
    void watchingASecondFileDropsTheFirst();
    void failedRegistrationIsReported();
    void ownWriteToANonCurrentNoteIsNotAnExternalChange();
    void anAppMoveBetweenFoldersIsNotAnExternalChange();
    void anAppDeleteIsNotAnExternalChange();
    void aThirdPartyMoveOrDeleteIsStillReported();
    void unusedGuardsExpireInsteadOfAccumulating();
    void aLargeTreeIsDiscoveredWithoutBlockingTheEventLoop();
    void aMissingRootReportsDegradedCoverage();
    void stoppingAnnouncesThatWatchingEnded();
};

void TestFileWatcher::externalFileChangeIsAnnouncedAndRescans()
{
    FileWatcher w;
    w.setDebounceMs(20);
    QSignalSpy note(&w, &FileWatcher::noteChangedExternally);
    QSignalSpy rescan(&w, &FileWatcher::externalChange);
    QSignalSpy paths(&w, &FileWatcher::externalChangePaths);
    w.feedChange("/notes/a.md", true);
    QCOMPARE(note.count(), 1);
    QCOMPARE(note.at(0).at(0).toString(), QString("/notes/a.md"));
    QVERIFY(rescan.wait(500));    // debounced re-scan fires
    QCOMPARE(rescan.count(), 1);
    QTRY_COMPARE(paths.count(), 1);
    QCOMPARE(paths.at(0).at(0).toStringList(),
             QStringList{QStringLiteral("/notes/a.md")});
}

void TestFileWatcher::ownWriteDoesNotSelfTrigger()
{
    FileWatcher w;
    w.setDebounceMs(20);
    QSignalSpy note(&w, &FileWatcher::noteChangedExternally);
    QSignalSpy rescan(&w, &FileWatcher::externalChange);
    // The app guards the path it is about to write; the ensuing change is its own.
    w.noteOwnWrite("/notes/a.md");
    w.feedChange("/notes/a.md", true);
    QCOMPARE(note.count(), 0);
    QTest::qWait(80);
    QCOMPARE(rescan.count(), 0);
}

void TestFileWatcher::guardExpiresSoAStaleGuardIsExternal()
{
    FileWatcher w;
    w.setDebounceMs(10);
    w.setGuardMs(20);
    QSignalSpy note(&w, &FileWatcher::noteChangedExternally);
    w.noteOwnWrite("/notes/a.md");
    QTest::qWait(60);   // outlive the guard window
    w.feedChange("/notes/a.md", true);
    QCOMPARE(note.count(), 1);   // a change after the guard expired is external
}

void TestFileWatcher::burstOfChangesDebouncesToOneRescan()
{
    FileWatcher w;
    w.setDebounceMs(40);
    QSignalSpy rescan(&w, &FileWatcher::externalChange);
    QSignalSpy paths(&w, &FileWatcher::externalChangePaths);
    for (int i = 0; i < 5; ++i)
        w.feedChange(QStringLiteral("/notes/dir"), false);
    QVERIFY(rescan.wait(500));
    QTest::qWait(60);
    QCOMPARE(rescan.count(), 1);   // the burst coalesced
    QCOMPARE(paths.count(), 1);
    QCOMPARE(paths.at(0).at(0).toStringList(),
             QStringList{QStringLiteral("/notes/dir")});
}

void TestFileWatcher::burstOfChangesRearmsTreeWatchesOnce()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FileWatcher w;
    w.setDebounceMs(40);
    w.watchRoot(dir.path());
    const int initialRearms = w.treeWatchRefreshCountForTests();
    QSignalSpy rescan(&w, &FileWatcher::externalChange);

    for (int i = 0; i < 5; ++i)
        w.feedChange(dir.path(), false);
    QCOMPARE(w.treeWatchRefreshCountForTests(), initialRearms);

    QVERIFY(rescan.wait(500));
    QCOMPARE(w.treeWatchRefreshCountForTests(), initialRearms + 1);
}

void TestFileWatcher::directoryChangeRescansWithoutNoteConflict()
{
    FileWatcher w;
    w.setDebounceMs(20);
    QSignalSpy note(&w, &FileWatcher::noteChangedExternally);
    QSignalSpy rescan(&w, &FileWatcher::externalChange);
    QSignalSpy paths(&w, &FileWatcher::externalChangePaths);
    w.feedChange("/notes/folder", false);   // a directory add/rename/delete
    QCOMPARE(note.count(), 0);               // not a specific-note conflict
    QVERIFY(rescan.wait(500));               // but the tree re-scans
    QTRY_COMPARE(paths.count(), 1);
    QCOMPARE(paths.at(0).at(0).toStringList(),
             QStringList{QStringLiteral("/notes/folder")});
}

void TestFileWatcher::realWatcherSeesAnAddedFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FileWatcher w;
    w.setDebounceMs(30);
    w.watchRoot(dir.path());
    QVERIFY(w.watching());
    QSignalSpy rescan(&w, &FileWatcher::externalChange);

    // Create a note file externally.
    QFile f(dir.filePath("external.md"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("added outside the app");
    f.close();

    // The real QFileSystemWatcher delivers a directoryChanged; debounced rescan.
    QVERIFY2(rescan.wait(3000),
             "the real watcher should see the added file");
}

// H11. A save replaces the open note atomically (QSaveFile writes a temp file
// and renames it over the target), so the watched path becomes a new inode and
// the kernel watch dies with the old one. The own-write guard then swallows the
// event that would have prompted a re-registration. Every later external edit
// is therefore invisible, and the keep-mine/load-theirs conflict banner the
// product promises never appears. The watch has to be re-established after the
// app's own write, precisely because that write is what destroyed it.
void TestFileWatcher::watchSurvivesAtomicReplacement()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString note = dir.filePath("open.md");
    {
        QFile f(note);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("original body");
    }

    FileWatcher w;
    w.setDebounceMs(30);
    w.watchRoot(dir.path());
    w.watchFile(note);

    // The app saves: QSaveFile semantics, guarded as its own write.
    w.noteOwnWrite(note);
    {
        QSaveFile s(note);
        QVERIFY(s.open(QIODevice::WriteOnly));
        s.write("saved by the app");
        QVERIFY(s.commit());
    }
    QTest::qWait(300);   // let the guarded event drain

    // Now somebody else edits the same note.
    QSignalSpy external(&w, &FileWatcher::noteChangedExternally);
    {
        QFile f(note);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("edited by another program");
    }

    QVERIFY2(external.wait(3000),
             "an external edit after the app's own atomic save must still be "
             "seen: the save replaced the inode, so the watch has to be "
             "re-established rather than silently lost");
}

// M15. Opening a second note previously added another file watch and left the
// first in place, so a long session accumulated watches until the per-process
// inotify limit was reached and registration began failing silently. Exactly
// one note is open, so exactly one note watch should exist.
void TestFileWatcher::watchingASecondFileDropsTheFirst()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = dir.filePath("first.md");
    const QString second = dir.filePath("second.md");
    for (const QString &p : {first, second}) {
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("body");
    }

    FileWatcher w;
    w.watchRoot(dir.path());
    w.watchFile(first);
    w.watchFile(second);

    QCOMPARE(w.watchedFilesForTests(), QStringList{second});
}

// M15. A registration that the kernel refuses (watch limit reached, path gone)
// used to be discarded, so the app went on claiming it was watching. The
// failure has to be observable, because the honest answer to "are external
// edits detected here?" is then no.
void TestFileWatcher::failedRegistrationIsReported()
{
    FileWatcher w;
    QSignalSpy degraded(&w, &FileWatcher::watchDegradedChanged);

    QVERIFY(!w.watchDegraded());
    w.watchFile(QStringLiteral("/nonexistent/directory/note.md"));

    QVERIFY2(w.watchDegraded(),
             "a refused registration must surface as degraded coverage");
    QCOMPARE(degraded.count(), 1);
}

// Only the open note is watched as a file; every other note in the vault is
// covered by its folder's watch. So when the app writes one of those -- a
// link rewrite, closed-note metadata, a recovery write -- the only event it
// receives is a directoryChanged, and a guard recorded against the file path
// never matches it. The write was then classified as somebody else's and the
// whole collection was re-scanned for it.
void TestFileWatcher::ownWriteToANonCurrentNoteIsNotAnExternalChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString other = dir.filePath("other.md");
    {
        QFile f(other);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("body");
    }

    FileWatcher w;
    w.setDebounceMs(30);
    w.setGuardMs(400);
    w.watchRoot(dir.path());     // the folder only: this note is not open
    QVERIFY(w.watching());
    QSignalSpy rescan(&w, &FileWatcher::externalChange);

    // The app writes it, the way NoteCollection does: guard, then replace.
    w.noteOwnWrite(other);
    {
        QSaveFile s(other);
        QVERIFY(s.open(QIODevice::WriteOnly));
        s.write("written by the app");
        QVERIFY(s.commit());
    }
    QTest::qWait(700);
    QCOMPARE(rescan.count(), 0);

    // The guard covers a batch, not the folder forever: once it has lapsed,
    // somebody else's change to the same folder is seen again.
    QFile added(dir.filePath("added-by-someone-else.md"));
    QVERIFY(added.open(QIODevice::WriteOnly));
    added.write("outside");
    added.close();
    QVERIFY2(rescan.wait(3000),
             "the directory guard outlived its window and hid a real change");
}

// Renaming, moving and deleting a note change a folder's listing without
// writing a file, so nothing calls noteOwnWrite for them and the watcher sees
// only a directoryChanged. AppContext therefore guards the affected folders
// directly (src/qml/appcontext.cpp, NoteCollection::noteMoved and
// ::noteRemoved -> FileWatcher::noteOwnDirectoryChange). These three tests
// drive that arrangement against the real QFileSystemWatcher.
//
// Ordering matters and is reproduced here rather than tidied away: both
// signals are emitted after the operation has already happened, so the guard
// is installed after the kernel has queued its event. What makes it work is
// that the guard call and the operation share one turn of the event loop --
// the notification cannot be delivered, and the debounce cannot elapse, until
// control returns to the loop. A caller that let the loop run between the two
// would defeat the guard, which is why the app's own signal handler is the
// right place for it.
namespace {

// Builds a vault with two folders and one note in the first, and a watcher
// over it. `from`/`to` come back as absolute paths.
void makeTwoFolderVault(const QTemporaryDir &dir, QString *from, QString *to,
                        QString *note)
{
    QDir root(dir.path());
    QVERIFY(root.mkpath(QStringLiteral("from")));
    QVERIFY(root.mkpath(QStringLiteral("to")));
    *from = root.filePath(QStringLiteral("from"));
    *to = root.filePath(QStringLiteral("to"));
    *note = QDir(*from).filePath(QStringLiteral("note.md"));
    QFile f(*note);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("body");
}

} // namespace

void TestFileWatcher::anAppMoveBetweenFoldersIsNotAnExternalChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString from, to, note;
    makeTwoFolderVault(dir, &from, &to, &note);

    FileWatcher w;
    w.setDebounceMs(30);
    w.watchRoot(dir.path());
    QVERIFY(w.watching());
    QSignalSpy rescan(&w, &FileWatcher::externalChange);
    QSignalSpy paths(&w, &FileWatcher::externalChangePaths);

    // Exactly what AppContext does when NoteCollection reports the move: the
    // rename has already happened, and both parent directories are guarded.
    const QString moved = QDir(to).filePath(QStringLiteral("note.md"));
    QVERIFY(QFile::rename(note, moved));
    w.noteOwnDirectoryChange(QFileInfo(note).absolutePath());
    w.noteOwnDirectoryChange(QFileInfo(moved).absolutePath());

    QTest::qWait(700);
    QCOMPARE(rescan.count(), 0);
    QCOMPARE(paths.count(), 0);
    QVERIFY(QFileInfo::exists(moved));
}

void TestFileWatcher::anAppDeleteIsNotAnExternalChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString from, to, note;
    makeTwoFolderVault(dir, &from, &to, &note);

    FileWatcher w;
    w.setDebounceMs(30);
    w.watchRoot(dir.path());
    QSignalSpy rescan(&w, &FileWatcher::externalChange);
    QStringList reportedPaths;
    QObject::connect(&w, &FileWatcher::externalChangePaths, &w,
                     [&reportedPaths](const QStringList &paths) {
                         reportedPaths += paths;
                     });

    // The NoteCollection::noteRemoved handler's shape: delete, then guard the
    // folder the note was in.
    QVERIFY(QFile::remove(note));
    w.noteOwnDirectoryChange(QFileInfo(note).absolutePath());

    QTest::qWait(700);
    // Which path arrived, when one arrives: the guard is recorded against the
    // note's folder, and a platform that reports the change against some
    // other directory of the tree - the vault root, say - is guarded for a
    // path its notification never carries.
    if (rescan.count() != 0) {
        qInfo("guarded %s; reported %s",
              qPrintable(QFileInfo(note).absolutePath()),
              qPrintable(reportedPaths.join(QStringLiteral(", "))));
    }
    QCOMPARE(rescan.count(), 0);
}

// The other half of the contract: guarding a folder must not blind the
// watcher to what somebody else does, either in an unguarded folder at the
// same time or in the guarded one once its window has passed.
void TestFileWatcher::aThirdPartyMoveOrDeleteIsStillReported()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString from, to, note;
    makeTwoFolderVault(dir, &from, &to, &note);
    const QString neighbour = QDir(to).filePath(QStringLiteral("theirs.md"));
    {
        QFile f(neighbour);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not ours");
    }

    FileWatcher w;
    w.setDebounceMs(30);
    w.setGuardMs(300);
    w.watchRoot(dir.path());

    // A move nobody guarded is an outside edit, and it is announced with the
    // folder it happened in.
    {
        QSignalSpy rescan(&w, &FileWatcher::externalChange);
        QSignalSpy paths(&w, &FileWatcher::externalChangePaths);
        QVERIFY(QFile::rename(note, QDir(to).filePath(QStringLiteral("note.md"))));
        QVERIFY2(rescan.wait(3000),
                 "a move made outside the app went unreported");
        QVERIFY(!paths.isEmpty());
        QVERIFY(paths.at(0).at(0).toStringList().contains(from)
                || paths.at(0).at(0).toStringList().contains(to));
    }

    // A guard is scoped to the folder it names: guarding one folder leaves a
    // simultaneous change in another as visible as it ever was.
    {
        w.noteOwnDirectoryChange(from);
        QSignalSpy rescan(&w, &FileWatcher::externalChange);
        QVERIFY(QFile::remove(neighbour));
        QVERIFY2(rescan.wait(3000),
                 "guarding one folder hid a change in a different one");
    }

    // And once the guarded folder's window has passed, it is watched again.
    QTest::qWait(400);
    {
        QSignalSpy rescan(&w, &FileWatcher::externalChange);
        QFile added(QDir(from).filePath(QStringLiteral("theirs-too.md")));
        QVERIFY(added.open(QIODevice::WriteOnly));
        added.write("outside");
        added.close();
        QVERIFY2(rescan.wait(3000),
                 "the folder guard outlived its window");
    }
}

// A guard that never matched used to stay forever, one entry per path the app
// had ever written, so a long session grew a map of every note it touched.
void TestFileWatcher::unusedGuardsExpireInsteadOfAccumulating()
{
    FileWatcher w;
    w.setGuardMs(20);
    for (int i = 0; i < 200; ++i)
        w.noteOwnWrite(QStringLiteral("/notes/n%1.md").arg(i));
    QVERIFY(w.pendingGuardCountForTests() > 100);

    QTest::qWait(80);   // every one of those windows has now lapsed
    w.noteOwnWrite(QStringLiteral("/notes/current.md"));
    // What is left is the new guard and the one for its folder.
    QCOMPARE(w.pendingGuardCountForTests(), 2);
}

// Discovering a large vault used to run to completion inside watchRoot (and
// inside every debounced refresh after it), searching the watcher's own path
// lists once per folder along the way. The walk now yields to the event loop
// between slices, so the window keeps repainting while a big tree is
// registered, and membership is answered from a set rather than by scanning.
//
// The observable half is the yielding: after watchRoot returns, a tree larger
// than one slice is still being discovered, and the timer below -- queued
// before the walk started -- gets to run before it finishes.
void TestFileWatcher::aLargeTreeIsDiscoveredWithoutBlockingTheEventLoop()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const int folders = FileWatcher::DiscoverySliceEntries * 4;
    QDir root(dir.path());
    for (int i = 0; i < folders; ++i)
        QVERIFY(root.mkpath(QStringLiteral("folder-%1").arg(i)));

    FileWatcher w;
    w.setDebounceMs(10);

    bool eventLoopRan = false;
    bool ranBeforeDiscoveryEnded = false;
    QTimer::singleShot(0, &w, [&]() {
        eventLoopRan = true;
        ranBeforeDiscoveryEnded = w.discoveryPending();
    });

    QElapsedTimer clock;
    clock.start();
    w.watchRoot(dir.path());
    QVERIFY2(w.discoveryPending(),
             "the whole tree was walked in one call: nothing else on the GUI "
             "thread could run while it did");

    QTRY_VERIFY_WITH_TIMEOUT(!w.discoveryPending(), 60000);
    const qint64 firstPass = clock.elapsed();
    QVERIFY(eventLoopRan);
    QVERIFY2(ranBeforeDiscoveryEnded,
             "queued work waited for the whole walk to finish");

    // Yielding must not lose folders: every one of them is registered by the
    // time discovery ends (the root itself included).
    QCOMPARE(w.watchedDirectoryCountForTests(), folders + 1);

    // A refresh with nothing changed happens on every external event. It
    // reconciles rather than re-registering, and stays inside a budget a
    // linear walk of this tree has no trouble meeting.
    QSignalSpy rescan(&w, &FileWatcher::externalChange);
    clock.restart();
    w.feedChange(dir.path(), false);
    QVERIFY(rescan.wait(60000));
    QTRY_VERIFY_WITH_TIMEOUT(!w.discoveryPending(), 60000);
    const qint64 refresh = clock.elapsed();
    QCOMPARE(w.watchedDirectoryCountForTests(), folders + 1);

    QVERIFY2(firstPass < 5000,
             qPrintable(QStringLiteral("registering %1 folders took %2 ms")
                            .arg(folders).arg(firstPass)));
    QVERIFY2(refresh < 5000,
             qPrintable(QStringLiteral("re-scanning %1 folders took %2 ms")
                            .arg(folders).arg(refresh)));

    // A folder that has gone away stops being watched, rather than leaving a
    // registration behind for every folder the vault ever had.
    QVERIFY(root.rmdir(QStringLiteral("folder-0")));
    QSignalSpy second(&w, &FileWatcher::externalChange);
    w.feedChange(dir.path(), false);
    QVERIFY(second.wait(60000));
    QTRY_VERIFY_WITH_TIMEOUT(!w.discoveryPending(), 60000);
    QCOMPARE(w.watchedDirectoryCountForTests(), folders);
}

// A root that does not exist watches nothing. Reporting `watching` for it told
// the UI every outside edit would be noticed, when none would.
void TestFileWatcher::aMissingRootReportsDegradedCoverage()
{
    FileWatcher w;
    QSignalSpy degraded(&w, &FileWatcher::watchDegradedChanged);
    w.watchRoot(QStringLiteral("/nonexistent/kvit/vault"));
    QVERIFY2(w.watchDegraded(),
             "a root that cannot be watched must report degraded coverage");
    QCOMPARE(degraded.count(), 1);
    QVERIFY(w.watchedFilesForTests().isEmpty());
}

// stop() ends watching as much as watchRoot("") does, and a binding on
// `watching` has to hear about it either way.
void TestFileWatcher::stoppingAnnouncesThatWatchingEnded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FileWatcher w;
    w.watchRoot(dir.path());
    QVERIFY(w.watching());

    QSignalSpy watching(&w, &FileWatcher::watchingChanged);
    w.stop();
    QVERIFY(!w.watching());
    QCOMPARE(watching.count(), 1);

    // Stopping again is not a transition.
    w.stop();
    QCOMPARE(watching.count(), 1);
}

QTEST_MAIN(TestFileWatcher)
#include "test_filewatcher.moc"
