// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
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

QTEST_MAIN(TestFileWatcher)
#include "test_filewatcher.moc"
