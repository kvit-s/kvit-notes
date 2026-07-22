// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtConcurrent>

#include "notecollection.h"
#include "timingbudget.h"
#include "persistencepool.h"

// Cancellation and responsiveness of the collection's background work
// (architecture finding A2).
//
// Background scans, refreshes, note parses and index writes all run through
// QtConcurrent. The futures they produce come from QtConcurrent::run over
// plain value-returning functions, which Qt cannot interrupt: QFuture::cancel()
// on such a future does nothing at all. Cancellation therefore has to be
// cooperative — the worker itself has to look at a flag and stop — and the GUI
// thread must not sit in waitForFinished() until the worker gets there.
//
// The user-visible symptom is a freeze: switching vaults or quitting while a
// large vault is still being scanned blocks the interface for as long as the
// scan takes. These tests put a number on that.
class TestAsyncCancellation : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void closingDuringDirectoryRefreshDoesNotCrash();
    void reportBlockingCostPerStage();
    void switchingRootDuringScanDoesNotBlockGui();
    void refreshPathsWithRootDoesNotBlockGui();
    void closingDuringScanDoesNotBlockGui();
    void resultsFromTheAbandonedRootNeverApply();
    void savesDoNotQueueBehindBulkBackgroundWork();
    void cancellingAnIndexSaveLeavesTheIndexOwedARewrite();
    void asupersededDirectoryRefreshStopsItsWorker();

private:
    // A vault big enough that its scan is still running when the switch
    // happens, without making the suite slow. Each note carries enough prose
    // that the body parse costs real time.
    void buildVault(const QString &root, int noteCount);

    QTemporaryDir *m_dirA = nullptr;
    QTemporaryDir *m_dirB = nullptr;
};

void TestAsyncCancellation::init()
{
    m_dirA = new QTemporaryDir();
    m_dirB = new QTemporaryDir();
    QVERIFY(m_dirA->isValid());
    QVERIFY(m_dirB->isValid());
}

void TestAsyncCancellation::cleanup()
{
    delete m_dirA; m_dirA = nullptr;
    delete m_dirB; m_dirB = nullptr;
}

void TestAsyncCancellation::buildVault(const QString &root, int noteCount)
{
    // ~4 KB of prose per note: heavy enough to parse, cheap enough to write.
    QString body;
    for (int line = 0; line < 60; ++line) {
        body += QStringLiteral(
            "alpha beta gamma delta epsilon zeta eta theta iota kappa "
            "lambda mu nu xi omicron pi rho sigma tau upsilon phi chi\n");
    }

    for (int i = 0; i < noteCount; ++i) {
        // Spread across folders so the directory walk has work to do too.
        const QString dir = QStringLiteral("%1/folder%2")
                                .arg(root)
                                .arg(i % 16);
        QDir().mkpath(dir);
        QFile file(QStringLiteral("%1/note%2.md").arg(dir).arg(i));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QStringLiteral("# Note %1\n\n").arg(i).toUtf8());
        file.write(body.toUtf8());
    }
}

// Closing or switching a vault while a directory refresh is running crashes.
//
// cancelAsyncRefresh() cancels the refresh future. QFuture::cancel() marks the
// future finished-and-cancelled, and a cancelled future carries NO results —
// but applyAsyncRefreshResult() opens with m_asyncRefreshWatcher.result(),
// which reads result zero unconditionally, before any generation check can
// reject it. Reading a result that was never produced is a null dereference.
//
// The QSignalBlocker in cancelAsyncRefresh() only suppresses the signal for
// the duration of the cancel call itself; the watcher still delivers finished
// afterwards.
void TestAsyncCancellation::closingDuringDirectoryRefreshDoesNotCrash()
{
    // Enough notes under one folder that the refresh is still running when
    // the close lands.
    const QString bulk = m_dirA->path() + QStringLiteral("/bulk");
    QDir().mkpath(bulk);
    QString body;
    for (int line = 0; line < 40; ++line) {
        body += QStringLiteral(
            "alpha beta gamma delta epsilon zeta eta theta iota kappa "
            "lambda mu nu xi omicron pi rho sigma tau upsilon phi chi\n");
    }
    for (int i = 0; i < 800; ++i) {
        QFile f(QStringLiteral("%1/bulk%2.md").arg(bulk).arg(i));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("# Bulk %1\n\n").arg(i).toUtf8());
        f.write(body.toUtf8());
    }

    NoteCollection collection;
    QVERIFY(collection.openRoot(m_dirA->path()));

    // Every note in the folder now looks changed, so the refresh reparses.
    QDirIterator it(bulk, QStringList{QStringLiteral("*.md")}, QDir::Files);
    while (it.hasNext()) {
        QFile f(it.next());
        if (f.open(QIODevice::Append))
            f.write("\nchanged\n");
    }

    collection.refreshPaths(QStringList{bulk});
    QVERIFY2(collection.refreshWatcherIsRunningForTesting(),
             "the refresh must still be running for this to be the case "
             "under test");

    collection.closeRoot();

    // Let the abandoned worker finish and the watcher deliver its signal.
    QTest::qWait(3000);

    // Reaching here at all is the assertion: before the fix this segfaults
    // in applyAsyncRefreshResult.
    QVERIFY(!collection.isOpen());
}

// Diagnostic, not a gate: prints what each cancel path costs the GUI thread
// so the claim "switching vaults freezes the app" carries a number. An async
// scan has two stages with very different cancellation behaviour, and they
// have to be timed separately:
//
//   listing  QtConcurrent::run over buildAsyncScanListing. A QFuture from
//            run() CANNOT be cancelled — QFuture::cancel() is a no-op on it —
//            so waitForFinished() here waits out the whole directory walk.
//   parse    QtConcurrent::mapped over the note list. mapped() CAN be
//            cancelled: cancel() stops further items being scheduled, so the
//            wait is bounded by the items already in flight, roughly one note
//            per pool thread.
void TestAsyncCancellation::reportBlockingCostPerStage()
{
    buildVault(m_dirA->path(), 2000);
    buildVault(m_dirB->path(), 2);

    // For scale: what the whole scan costs when nothing interrupts it.
    {
        NoteCollection collection;
        QElapsedTimer timer;
        timer.start();
        collection.openRoot(m_dirA->path());
        qInfo() << "reference: synchronous openRoot of 2000 notes"
                << timer.elapsed() << "ms";
    }

    // Switch issued before the event loop runs, i.e. while the listing is
    // still going. Repeated, because whether the wait is entered at all is a
    // race: cancelAsyncScan() only waits when the watcher already reports
    // isRunning(), and a future that has been handed to QtConcurrent but not
    // yet picked up by a pool thread does not.
    {
        qint64 worst = 0;
        int enteredWait = 0;
        const int trials = 20;
        for (int i = 0; i < trials; ++i) {
            NoteCollection collection;
            collection.openRootAsync(m_dirA->path());
            if (collection.listingWatcherIsRunningForTesting())
                ++enteredWait;
            QElapsedTimer timer;
            timer.start();
            collection.openRootAsync(m_dirB->path());
            worst = qMax(worst, timer.elapsed());
            QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 60000);
        }
        qInfo() << "switch during LISTING stage: worst of" << trials << "was"
                << worst << "ms;" << enteredWait << "of" << trials
                << "found the listing already running";
    }

    // Switch issued once the listing has been applied and the parse stage is
    // running.
    {
        NoteCollection collection;
        collection.openRootAsync(m_dirA->path());
        QTRY_VERIFY_WITH_TIMEOUT(collection.noteCount() > 0, 60000);
        const bool parsing = collection.scanInProgress();
        QElapsedTimer timer;
        timer.start();
        collection.openRootAsync(m_dirB->path());
        qInfo() << "switch during PARSE stage blocked" << timer.elapsed()
                << "ms (scan was in progress:" << parsing << ")";
        QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 60000);
    }

    // The shutdown path.
    {
        NoteCollection collection;
        collection.openRootAsync(m_dirA->path());
        QTRY_VERIFY_WITH_TIMEOUT(collection.noteCount() > 0, 60000);
        QElapsedTimer timer;
        timer.start();
        collection.closeRoot();
        qInfo() << "closeRoot during PARSE stage blocked" << timer.elapsed()
                << "ms";
    }

    // The directory refresh. Unlike the scan, this one reads and parses every
    // changed note body INSIDE a QtConcurrent::run future, so the work that
    // cannot be cancelled is the expensive kind rather than a bare directory
    // walk. It has to be aimed at a SUBDIRECTORY: refreshPaths() given the
    // root does a full async rescan (fullRefreshAsync) rather than this
    // narrower directory-reparse path, so it would not exercise the same work.
    {
        // 1500 notes under one folder, so a single directory refresh has to
        // reparse all of them.
        const QString bulk = m_dirA->path() + QStringLiteral("/bulk");
        QDir().mkpath(bulk);
        QString body;
        for (int line = 0; line < 60; ++line) {
            body += QStringLiteral(
                "alpha beta gamma delta epsilon zeta eta theta iota kappa "
                "lambda mu nu xi omicron pi rho sigma tau upsilon phi chi\n");
        }
        for (int i = 0; i < 1500; ++i) {
            QFile f(QStringLiteral("%1/bulk%2.md").arg(bulk).arg(i));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QStringLiteral("# Bulk %1\n\n").arg(i).toUtf8());
            f.write(body.toUtf8());
        }

        NoteCollection collection;
        collection.openRoot(m_dirA->path());

        // Make every note in that folder look changed.
        QDirIterator it(bulk, QStringList{QStringLiteral("*.md")}, QDir::Files);
        while (it.hasNext()) {
            QFile f(it.next());
            if (f.open(QIODevice::Append))
                f.write("\nchanged\n");
        }

        collection.refreshPaths(QStringList{bulk});
        const bool running = collection.refreshWatcherIsRunningForTesting();
        QElapsedTimer timer;
        timer.start();
        collection.closeRoot();
        const qint64 blockedMs = timer.elapsed();
        qInfo() << "closeRoot during REFRESH (1500 notes reparsed) blocked"
                << blockedMs << "ms (refresh was running:" << running << ")";

        // The wait returning immediately does not mean the work stopped.
        // QFuture::cancel() on a future from QtConcurrent::run() marks the
        // future finished but cannot interrupt the function, which keeps
        // running detached and keeps holding a pool thread. Watch the pool
        // after the collection has been closed.
        QThreadPool *pool = QThreadPool::globalInstance();
        QElapsedTimer after;
        after.start();
        qint64 busyUntilMs = 0;
        while (after.elapsed() < 10000) {
            if (pool->activeThreadCount() > 0)
                busyUntilMs = after.elapsed();
            QTest::qWait(20);
        }
        qInfo() << "abandoned refresh kept the global pool busy for a further"
                << busyUntilMs << "ms after closeRoot returned";
    }
}

// The vault-switch freeze. openRootAsync() begins by cancelling the running
// scan, and that cancellation must not wait for the scan to finish.
void TestAsyncCancellation::switchingRootDuringScanDoesNotBlockGui()
{
    buildVault(m_dirA->path(), 600);
    buildVault(m_dirB->path(), 1);

    NoteCollection collection;
    QVERIFY(collection.openRootAsync(m_dirA->path()));
    QVERIFY(collection.scanInProgress());

    QElapsedTimer timer;
    timer.start();
    QVERIFY(collection.openRootAsync(m_dirB->path()));
    const qint64 switchMs = timer.elapsed();

    qInfo() << "root switch during scan blocked the GUI thread for"
            << switchMs << "ms";

    // A switch is a user action; it has to feel immediate rather than wait
    // out however long the abandoned vault's scan had left to run.
    //
    // Wall-clock, because responsiveness is what this is about - CPU time
    // would say nothing about how long the GUI thread sat still. That makes
    // it load-sensitive and so not judged on a busy machine
    // (tests/timingbudget.h). What the test guards does not rest on the
    // number: scanInProgress() above proves a scan was genuinely running, so
    // returning at all is the evidence that the switch cancelled it rather
    // than waiting it out.
    KVIT_ASSERT_WALL_BUDGET(switchMs, "root switch during scan", 250.0);

    QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 30000);
}

// The external file watcher calls refreshPaths() on a debounced burst of
// changes. When a change cannot be narrowed to a known note - the root itself,
// or a path outside the vault - refreshPaths() used to fall through to the
// fully synchronous refresh() and block the GUI thread for the whole rescan.
// It now takes a full async rescan instead, so the call returns at once and
// the scan runs on a worker thread.
void TestAsyncCancellation::refreshPathsWithRootDoesNotBlockGui()
{
    buildVault(m_dirA->path(), 600);

    NoteCollection collection;
    QVERIFY(collection.openRoot(m_dirA->path()));   // synchronous: fully loaded
    QVERIFY(!collection.scanInProgress());

    QElapsedTimer timer;
    timer.start();
    collection.refreshPaths(QStringList{m_dirA->path()});   // the root path
    const qint64 blockedMs = timer.elapsed();

    qInfo() << "refreshPaths(root) blocked the GUI thread for"
            << blockedMs << "ms";

    // A scan is now genuinely running on a worker thread, which is what proves
    // the call went async rather than completing inline. Returning while that
    // scan is still in flight is the evidence the GUI thread was not held for
    // it; the wall-clock budget is the coarse backstop and load-sensitive, so
    // it is not judged on a busy machine (tests/timingbudget.h).
    QVERIFY2(collection.scanInProgress(),
             "refreshPaths(root) should start an async scan, not block on a "
             "synchronous one");
    KVIT_ASSERT_WALL_BUDGET(blockedMs, "refreshPaths(root)", 250.0);

    QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 30000);
    QVERIFY(collection.note("folder0/note0.md"));  // rescan republished the vault
}

// The same wait sits on the shutdown path, where it shows up as a window that
// will not close.
void TestAsyncCancellation::closingDuringScanDoesNotBlockGui()
{
    buildVault(m_dirA->path(), 600);

    NoteCollection collection;
    QVERIFY(collection.openRootAsync(m_dirA->path()));
    QVERIFY(collection.scanInProgress());

    QElapsedTimer timer;
    timer.start();
    collection.closeRoot();
    const qint64 closeMs = timer.elapsed();

    qInfo() << "close during scan blocked the GUI thread for"
            << closeMs << "ms";

    // Wall-clock latency, deferred on a busy machine; scanInProgress() above
    // is what proves the close had a live scan to cancel.
    KVIT_ASSERT_WALL_BUDGET(closeMs, "close during scan", 250.0);
}

// Not blocking is only safe if a late result from the abandoned vault can
// never be mistaken for the new one's. The generation carried by each result
// is what makes that true.
void TestAsyncCancellation::resultsFromTheAbandonedRootNeverApply()
{
    buildVault(m_dirA->path(), 400);
    buildVault(m_dirB->path(), 3);

    NoteCollection collection;
    QVERIFY(collection.openRootAsync(m_dirA->path()));
    QVERIFY(collection.scanInProgress());

    QVERIFY(collection.openRootAsync(m_dirB->path()));
    QCOMPARE(collection.rootPath(), m_dirB->path());

    // Let whatever the first vault's workers were doing land.
    QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 30000);
    QTest::qWait(200);

    QCOMPARE(collection.rootPath(), m_dirB->path());
    QCOMPARE(collection.noteCount(), 3);
    for (const QString &relPath : collection.noteRelPaths()) {
        QVERIFY2(!relPath.contains(QLatin1String("note5")),
                 "a note from the abandoned vault reached the new collection");
    }
}

// Saves run on a pool of their own, so a global pool saturated with bulk
// background work cannot delay one. The measurement is the time from
// submitting a task to that task starting, with every global-pool thread
// already occupied.
void TestAsyncCancellation::savesDoNotQueueBehindBulkBackgroundWork()
{
    QThreadPool *global = QThreadPool::globalInstance();
    const int threads = global->maxThreadCount();

    // Occupy every global-pool thread for a good while.
    QAtomicInt release{0};
    for (int i = 0; i < threads; ++i) {
        (void)QtConcurrent::run(global, [&release]() {
            while (release.loadAcquire() == 0)
                QThread::msleep(1);
        });
    }
    // Let them all actually start.
    QTRY_COMPARE_WITH_TIMEOUT(global->activeThreadCount(), threads, 5000);

    QElapsedTimer onGlobal;
    onGlobal.start();
    QAtomicInt globalStarted{0};
    (void)QtConcurrent::run(global, [&globalStarted]() {
        globalStarted.storeRelease(1);
    });

    QElapsedTimer onPersistence;
    onPersistence.start();
    QAtomicInt persistenceStarted{0};
    (void)QtConcurrent::run(persistenceThreadPool(), [&persistenceStarted]() {
        persistenceStarted.storeRelease(1);
    });

    QTRY_VERIFY_WITH_TIMEOUT(persistenceStarted.loadAcquire() == 1, 5000);
    const qint64 persistenceWaitMs = onPersistence.elapsed();

    // The global-pool task is still stuck behind the occupied threads.
    const bool globalStillWaiting = globalStarted.loadAcquire() == 0;

    release.storeRelease(1);
    QTRY_VERIFY_WITH_TIMEOUT(globalStarted.loadAcquire() == 1, 10000);
    const qint64 globalWaitMs = onGlobal.elapsed();

    qInfo() << "with the global pool saturated: a persistence task started"
            << "after" << persistenceWaitMs << "ms, a global-pool task after"
            << globalWaitMs << "ms";

    QVERIFY2(globalStillWaiting,
             "the control task should still have been queued; the pool was "
             "not actually saturated and this measures nothing");
    // Wall-clock latency, deferred on a busy machine. The globalStillWaiting
    // assertion above is the load-independent half of this test: it proves
    // the global pool really was saturated, so a save starting promptly is
    // evidence of the separate persistence pool rather than of a quiet
    // machine.
    KVIT_ASSERT_WALL_BUDGET(persistenceWaitMs, "save behind saturated pool",
                            250.0);

    global->waitForDone();
}

// saveIndexFileIfDirtyAsync clears the dirty flag BEFORE the write starts and
// leaves it to the result handler to set it back if the write failed.
// Cancelling suppresses that handler and additionally discards any queued
// request, so a cancelled save leaves the collection believing the index on
// disk matches memory when nothing was written.
//
// refresh() cancels while the collection stays OPEN, so this is reachable in
// ordinary use rather than only at shutdown. It is masked today only because
// an index save cannot actually be interrupted; the moment it can be, the
// index goes stale with nothing remembering it is owed.
void TestAsyncCancellation::cancellingAnIndexSaveLeavesTheIndexOwedARewrite()
{
    buildVault(m_dirA->path(), 400);

    NoteCollection collection;
    QVERIFY(collection.openRoot(m_dirA->path()));
    QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 30000);

    // Dirty the index and start the asynchronous save, then cancel it the way
    // refresh() does.
    collection.markIndexDirtyForTesting();
    QVERIFY(collection.indexDirtyForTesting());
    collection.saveIndexFileIfDirtyAsyncForTesting();
    QVERIFY2(!collection.indexDirtyForTesting(),
             "the flag is cleared before the write; that is the precondition "
             "this test is about");

    collection.cancelAsyncIndexSaveForTesting();

    QVERIFY2(collection.indexDirtyForTesting(),
             "a cancelled index save must leave the index marked dirty, or "
             "nothing will ever rewrite it");
}

// A refresh that supersedes another one waits for the first to finish before
// replacing it. Cancellation is cooperative — QFuture::cancel() cannot stop a
// QtConcurrent::run worker — so unless the first worker's own token is
// signalled first, that wait is the whole remaining walk, on the GUI thread,
// and the abandoned walk goes on holding a pool thread while it happens. A
// burst of watcher events turned that into one accumulated walk per event.
void TestAsyncCancellation::asupersededDirectoryRefreshStopsItsWorker()
{
    // One folder heavy enough that walking and parsing it takes measurable
    // time, and one trivial folder to supersede it with.
    const QString bulk = m_dirA->path() + QStringLiteral("/bulk");
    const QString tiny = m_dirA->path() + QStringLiteral("/tiny");
    QDir().mkpath(bulk);
    QDir().mkpath(tiny);
    QString body;
    for (int line = 0; line < 60; ++line) {
        body += QStringLiteral(
            "alpha beta gamma delta epsilon zeta eta theta iota kappa "
            "lambda mu nu xi omicron pi rho sigma tau upsilon phi chi\n");
    }
    for (int i = 0; i < 4000; ++i) {
        QFile f(QStringLiteral("%1/bulk%2.md").arg(bulk).arg(i));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("# Bulk %1\n\n").arg(i).toUtf8());
        f.write(body.toUtf8());
    }
    QFile lone(tiny + QStringLiteral("/one.md"));
    QVERIFY(lone.open(QIODevice::WriteOnly));
    lone.write("# one\n");
    lone.close();

    NoteCollection collection;
    QVERIFY(collection.openRoot(m_dirA->path()));
    QVERIFY(!collection.scanInProgress());

    auto touchBulk = [&]() {
        QDirIterator it(bulk, QStringList{QStringLiteral("*.md")}, QDir::Files);
        while (it.hasNext()) {
            QFile f(it.next());
            if (f.open(QIODevice::Append))
                f.write("\nchanged\n");
        }
    };

    // How long the walk takes when nothing interrupts it. The comparison
    // below is against this rather than against a fixed number of
    // milliseconds, because what is being measured is the difference between
    // "wait for the rest of the walk" and "tell it to stop".
    touchBulk();
    QElapsedTimer whole;
    whole.start();
    collection.refreshPaths(QStringList{bulk});
    QTRY_VERIFY_WITH_TIMEOUT(!collection.refreshWatcherIsRunningForTesting(),
                             60000);
    const qint64 wholeWalkMs = whole.elapsed();
    if (wholeWalkMs < 400) {
        QSKIP("this machine parses the fixture faster than the measurement "
              "needs; the superseded walk would be too short to observe");
    }

    // Now supersede a walk that is genuinely under way. A task that has not
    // started yet can simply be dropped by the pool; it is the started one
    // that has to be told to stop.
    touchBulk();
    collection.refreshPaths(QStringList{bulk});
    QThread::msleep(100);
    QVERIFY2(collection.refreshWatcherIsRunningForTesting(),
             "the first refresh must still be running for this to be the case "
             "under test");

    QElapsedTimer timer;
    timer.start();
    collection.refreshPaths(QStringList{tiny});
    const qint64 blockedMs = timer.elapsed();

    qInfo() << "superseding a running directory refresh blocked the GUI thread"
            << "for" << blockedMs << "ms; the whole walk takes" << wholeWalkMs
            << "ms";
    // Cancellation is cooperative, so the wait is bounded by the gap between
    // two checks in the walk rather than by the rest of the vault. Half the
    // whole walk is a generous line: without the token being signalled this
    // is the entire remainder, and the abandoned worker goes on holding a
    // pool thread for it.
    QVERIFY2(blockedMs < wholeWalkMs / 2,
             qPrintable(QStringLiteral("superseding a refresh waited %1 ms for "
                                       "the abandoned walk (whole walk %2 ms)")
                            .arg(blockedMs).arg(wholeWalkMs)));

    QTRY_VERIFY_WITH_TIMEOUT(!collection.refreshWatcherIsRunningForTesting(),
                             60000);
    collection.closeRoot();
    QTest::qWait(100);
}

QTEST_MAIN(TestAsyncCancellation)
#include "test_asynccancellation.moc"
