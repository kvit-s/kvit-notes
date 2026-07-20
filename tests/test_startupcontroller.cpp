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
    QCOMPARE(collection.noteCount(), 0);
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

QTEST_MAIN(TestStartupController)
#include "test_startupcontroller.moc"
