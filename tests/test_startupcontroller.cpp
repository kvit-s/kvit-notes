// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "appactions.h"
#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "documentmanager.h"
#include "notecollection.h"
#include "startupcontroller.h"
#include "undostack.h"

class TestStartupController : public QObject
{
    Q_OBJECT

private slots:
    void deferredStartOpensLastNoteAsynchronously();
    void freshVaultFinishesOnlyAfterWelcomeNoteOpens();
    void traversalLastOpenNoteIsIgnored();
    void oversizedFirstCandidateDoesNotCancelTheSecond();
    void coldVaultOpenDoesNotStripLiveFrontMatter();
    void repositoryWarningsReachTheUser();
};

namespace {

void writeText(const QString &path, const QString &text)
{
    QFileInfo info(path);
    QVERIFY(QDir().mkpath(info.absolutePath()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(text.toUtf8());
}

} // namespace

void TestStartupController::deferredStartOpensLastNoteAsynchronously()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeText(dir.filePath(QStringLiteral("A.md")),
              QStringLiteral("# A\n\nfirst note\n"));
    writeText(dir.filePath(QStringLiteral("B.md")),
              QStringLiteral("# B\n\nsecond note\n"));

    QDir().mkpath(dir.filePath(QStringLiteral(".kvit")));
    QJsonObject state;
    state.insert(QStringLiteral("lastOpenNote"), QStringLiteral("B.md"));
    writeText(dir.filePath(QStringLiteral(".kvit/collection.json")),
              QString::fromUtf8(QJsonDocument(state).toJson()));

    UndoStack undoStack;
    BlockModel blockModel;
    blockModel.setUndoStack(&undoStack);
    DocumentManager documentManager;
    documentManager.setBlockModel(&blockModel);
    documentManager.setUndoStack(&undoStack);
    NoteCollection collection;
    QStringList parsed;
    collection.setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });

    StartupController controller;
    controller.setCollection(&collection);
    controller.setDocumentManager(&documentManager);
    controller.setBlockModel(&blockModel);
    controller.setUndoStack(&undoStack);
    controller.setRootPath(dir.path());

    QSignalSpy finishedSpy(&controller, &StartupController::finishedChanged);
    QSignalSpy scanFinishedSpy(&collection, &NoteCollection::scanFinished);

    controller.start();

    QVERIFY(controller.started());
    QVERIFY(!controller.finished());
    QVERIFY(collection.isOpen());
    // The persisted last-open path is validated and published as one indexed
    // placeholder before it is opened; the rest of the vault remains on the
    // background listing path.
    QCOMPARE(collection.noteCount(), 1);
    QCOMPARE(collection.lastOpenNote(), QStringLiteral("B.md"));
    QVERIFY(documentManager.openInProgress());
    QCOMPARE(parsed.size(), 0);
    QVERIFY(collection.scanInProgress());

    QTRY_VERIFY_WITH_TIMEOUT(controller.finished(), 5000);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(documentManager.currentFilePath(),
             collection.absolutePath(QStringLiteral("B.md")));
    QVERIFY(blockModel.count() > 0);
    QVERIFY(!documentManager.openInProgress());

    QTRY_COMPARE_WITH_TIMEOUT(collection.noteCount(), 2, 5000);
    // noteCount() is published by the same asynchronous scan. Processing
    // events for the comparison above may therefore have observed
    // scanFinished already; wait on the accumulated spy count instead of
    // requiring a second emission.
    QTRY_COMPARE_WITH_TIMEOUT(scanFinishedSpy.count(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!collection.scanInProgress(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(parsed.size() == 2, 5000);
    QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral(".kvit/cache/index.json"))));
}

// A first run on an empty folder seeds a welcome note and opens it. That
// open is asynchronous exactly like the restore-last-note path, which waits
// for onStartupNoteOpenFinished before finishing, but the fresh-vault branch
// calls finishStartup() the moment the open is REQUESTED. Anything gated on
// finished — hiding the splash, focusing the editor — then runs against a
// document that has not loaded yet.
void TestStartupController::freshVaultFinishesOnlyAfterWelcomeNoteOpens()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    UndoStack undoStack;
    BlockModel blockModel;
    blockModel.setUndoStack(&undoStack);
    DocumentManager documentManager;
    documentManager.setBlockModel(&blockModel);
    documentManager.setUndoStack(&undoStack);
    NoteCollection collection;

    StartupController controller;
    controller.setCollection(&collection);
    controller.setDocumentManager(&documentManager);
    controller.setBlockModel(&blockModel);
    controller.setUndoStack(&undoStack);
    controller.setRootPath(dir.path());

    // Sampled at the instant startup reports itself finished.
    bool openStillRunningAtFinish = false;
    int blocksAtFinish = -1;
    connect(&controller, &StartupController::finishedChanged,
            &controller, [&]() {
                openStillRunningAtFinish = documentManager.openInProgress();
                blocksAtFinish = blockModel.count();
            });

    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(controller.finished(), 5000);

    QVERIFY2(!openStillRunningAtFinish,
             "startup reported finished while the welcome note was still opening");
    QVERIFY2(blocksAtFinish > 0,
             "startup reported finished before the welcome note had any blocks");
}

void TestStartupController::traversalLastOpenNoteIsIgnored()
{
    QTemporaryDir outer;
    QVERIFY(outer.isValid());
    const QString vault = outer.filePath(QStringLiteral("vault"));
    QVERIFY(QDir().mkpath(QDir(vault).filePath(QStringLiteral(".kvit"))));
    writeText(QDir(vault).filePath(QStringLiteral("Safe.md")),
              QStringLiteral("safe note\n"));
    writeText(outer.filePath(QStringLiteral("outside.md")),
              QStringLiteral("outside secret\n"));

    QJsonObject state;
    state.insert(QStringLiteral("lastOpenNote"),
                 QStringLiteral("../outside.md"));
    writeText(QDir(vault).filePath(QStringLiteral(".kvit/collection.json")),
              QString::fromUtf8(QJsonDocument(state).toJson()));

    UndoStack undoStack;
    BlockModel blockModel;
    blockModel.setUndoStack(&undoStack);
    DocumentManager documentManager;
    documentManager.setBlockModel(&blockModel);
    documentManager.setUndoStack(&undoStack);
    NoteCollection collection;
    StartupController controller;
    controller.setCollection(&collection);
    controller.setDocumentManager(&documentManager);
    controller.setBlockModel(&blockModel);
    controller.setUndoStack(&undoStack);
    controller.setRootPath(vault);

    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(controller.finished(), 5000);
    QCOMPARE(documentManager.currentFilePath(),
             collection.absolutePath(QStringLiteral("Safe.md")));
    QVERIFY(documentManager.currentFilePath()
            != outer.filePath(QStringLiteral("outside.md")));
}

// APP-4. DocumentManager::openAsync() refuses an oversized file before any
// read, and reports that by emitting openAsyncFinished(false) BEFORE it
// returns. The handler picks the next candidate and starts it from inside that
// call; control then returns to the failing call, which used to clear the
// in-progress flag and the pending path belonging to the NEWER request. Its
// completion was then ignored and startup fell back to sample content, so the
// reader saw the welcome document instead of their own note.
void TestStartupController::oversizedFirstCandidateDoesNotCancelTheSecond()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Names chosen so the oversized note is the first candidate in relPath
    // order; no lastOpenNote is recorded, so that order is what decides.
    const QString oversized =
        QStringLiteral("A too large to open\n\n")
        + QString(1200 * 1024, QLatin1Char('x')) + QStringLiteral("\n");
    writeText(dir.filePath(QStringLiteral("AAA_big.md")), oversized);
    writeText(dir.filePath(QStringLiteral("ZZZ_small.md")),
              QStringLiteral("# Small\n\nthe note that must open\n"));

    // Warm the on-disk index so the controller's scan finishes without
    // publishing the two notes one revision at a time; the candidate list is
    // then complete the first time startup looks at it.
    {
        NoteCollection warm;
        QVERIFY(warm.openRoot(dir.path()));
        warm.refresh();
    }

    UndoStack undoStack;
    BlockModel blockModel;
    blockModel.setUndoStack(&undoStack);
    DocumentManager documentManager;
    documentManager.setBlockModel(&blockModel);
    documentManager.setUndoStack(&undoStack);
    documentManager.setMaxOpenFileSizeMiB(1);
    NoteCollection collection;

    StartupController controller;
    controller.setCollection(&collection);
    controller.setDocumentManager(&documentManager);
    controller.setBlockModel(&blockModel);
    controller.setUndoStack(&undoStack);
    controller.setRootPath(dir.path());

    // Sampled at the instant startup reports itself finished: that is when
    // the shell stops showing the splash and starts showing whatever document
    // is in the model.
    QString pathAtFinish;
    QString firstBlockAtFinish;
    connect(&controller, &StartupController::finishedChanged,
            &controller, [&]() {
                pathAtFinish = documentManager.currentFilePath();
                firstBlockAtFinish = blockModel.count() > 0
                    ? blockModel.blockAt(0)->content()
                    : QString();
            });

    QSignalSpy rejectedSpy(&documentManager,
                           &DocumentManager::openRejectedTooLarge);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(controller.finished(), 10000);

    QCOMPARE(rejectedSpy.count(), 1);
    QCOMPARE(pathAtFinish,
             collection.absolutePath(QStringLiteral("ZZZ_small.md")));
    QVERIFY2(firstBlockAtFinish == QStringLiteral("Small"),
             qPrintable(QStringLiteral("startup finished on sample content "
                                       "rather than the second note; first "
                                       "block was '%1'")
                            .arg(firstBlockAtFinish)));
}

// APP-2. Pointing Kvit at the folder a note lives in turns a single-file
// session into a vault. The vault opens before the background scan has read
// anything, and it publishes a PLACEHOLDER entry for the remembered note so it
// can be opened at once. frontMatterFor() answers "" for that placeholder just
// as it does for a note with genuinely no metadata, and AppContext projected
// that "" straight into the live document: the note's tags, favourite state
// and foreign keys were replaced with nothing, and the document was marked
// dirty, so a save — or an autosave, or shutdown — before the scan caught up
// wrote the file back without its metadata.
//
// This composes the real AppContext, because the defect is in its wiring.
void TestStartupController::coldVaultOpenDoesNotStripLiveFrontMatter()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString noteText =
        QStringLiteral("---\ntags: [work, urgent]\nfavorite: true\n"
                       "reviewer: ada\n---\n# Subject\n\nBody text.\n");
    const QString notePath = dir.filePath(QStringLiteral("Subject.md"));
    writeText(notePath, noteText);
    // Enough company that the scan cannot be dismissed as trivially fast.
    for (int i = 0; i < 40; ++i) {
        writeText(dir.filePath(QStringLiteral("Filler%1.md").arg(i)),
                  QStringLiteral("filler %1\n").arg(i));
    }
    // The remembered note is what gets the placeholder entry.
    QDir().mkpath(dir.filePath(QStringLiteral(".kvit")));
    QJsonObject state;
    state.insert(QStringLiteral("lastOpenNote"), QStringLiteral("Subject.md"));
    writeText(dir.filePath(QStringLiteral(".kvit/collection.json")),
              QString::fromUtf8(QJsonDocument(state).toJson()));

    AppContext::Options options;
    options.showSystemTray = false;
    options.configureLoggingFromSettings = false;
    AppContext context(options);
    QTemporaryDir config;
    context.openSettings(config.filePath(QStringLiteral("settings.json")));

    DocumentManager *manager = context.documentManager();
    NoteCollection *collection = context.noteCollection();
    manager->setAutoSaveEnabled(false);

    // The note is open on its own, front matter and all.
    QVERIFY(manager->open(QUrl::fromLocalFile(notePath)));
    QVERIFY(!manager->isDirty());
    QVERIFY(manager->frontMatter().contains(QStringLiteral("reviewer: ada")));

    // The vault opens around it. Nothing has read Subject.md yet.
    QVERIFY(collection->openRootAsync(dir.path()));
    QVERIFY(collection->scanInProgress());
    QVERIFY2(!collection->hasParsedMetadata(QStringLiteral("Subject.md")),
             "this test needs the entry to still be a placeholder");
    QVERIFY2(!manager->isDirty(),
             "a vault open that has not read the note's file yet must not "
             "change the live document");
    QCOMPARE(manager->frontMatter(), QStringLiteral(
                 "---\ntags: [work, urgent]\nfavorite: true\nreviewer: ada\n---\n"));

    // The user saves immediately — or the app is closing.
    QVERIFY(manager->save());
    QFile written(notePath);
    QVERIFY(written.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString onDisk = QString::fromUtf8(written.readAll());
    QVERIFY2(onDisk.contains(QStringLiteral("reviewer: ada")),
             "foreign front matter was lost to a vault open");
    QVERIFY(onDisk.contains(QStringLiteral("tags: [work, urgent]")));
    QVERIFY(onDisk.contains(QStringLiteral("favorite: true")));

    // Once the scan has actually read the note, the repository is the
    // authority again and its metadata does reach the document.
    QTRY_VERIFY_WITH_TIMEOUT(
        collection->hasParsedMetadata(QStringLiteral("Subject.md")), 10000);
    QVERIFY(manager->frontMatter().contains(QStringLiteral("reviewer: ada")));
}

// Four conditions the repository reports that nothing was listening for, so
// the only record of each was the log: a vault opened without a lock because
// the filesystem has none (the fail-open is deliberate, being told about it is
// the point), a write abandoned because the note changed on disk underneath
// it, and a multi-file operation the last session left half-finished. Each now
// reaches the shell's status line through AppActions, which main.qml already
// renders. The fourth, noteMetadataReady, drives the front-matter projection
// covered above.
void TestStartupController::repositoryWarningsReachTheUser()
{
    AppContext::Options options;
    options.showSystemTray = false;
    options.configureLoggingFromSettings = false;
    AppContext context(options);
    QTemporaryDir config;
    context.openSettings(config.filePath(QStringLiteral("settings.json")));

    NoteCollection *collection = context.noteCollection();
    AppActions *actions = context.appActions();
    QVERIFY(actions);
    QSignalSpy status(actions, &AppActions::transientStatusRequested);

    emit collection->vaultUnprotected(QStringLiteral("/vaults/Shared"),
                                      QStringLiteral("no locking on NFS"));
    QCOMPARE(status.count(), 1);
    QString message = status.takeFirst().at(0).toString();
    QVERIFY(message.contains(QStringLiteral("Shared")));
    QVERIFY(message.contains(QStringLiteral("no locking on NFS")));

    emit collection->noteChangedExternally(QStringLiteral("Meeting notes.md"));
    QCOMPARE(status.count(), 1);
    message = status.takeFirst().at(0).toString();
    QVERIFY(message.contains(QStringLiteral("Meeting notes.md")));

    emit collection->operationIncomplete(
        QStringLiteral("A.md,B.md").split(QLatin1Char(',')));
    QCOMPARE(status.count(), 1);
    message = status.takeFirst().at(0).toString();
    QVERIFY(message.contains(QStringLiteral("A.md")));
    QVERIFY(message.contains(QStringLiteral("B.md")));

    // An empty list is not a condition worth interrupting anyone for.
    emit collection->operationIncomplete(QStringList());
    QCOMPARE(status.count(), 0);
}

QTEST_MAIN(TestStartupController)
#include "test_startupcontroller.moc"
