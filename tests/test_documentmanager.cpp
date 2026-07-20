// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>

#include "documentmanager.h"
#include "documentserializer.h"
#include "blockmodel.h"
#include "undostack.h"
#include "block.h"
#include "perflog.h"

class TestDocumentManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Basic properties
    void testInitialState();
    void testCurrentFileName();

    // New document
    void testNewDocument();
    void testNewDocumentClearsUndo();

    // Save operations
    void testSaveWithoutFile();
    void testSaveAs();
    void testSaveAsAddsExtension();
    void testSaveExisting();
    void testSaveFailsOnInvalidPath();
    void testSaveTimingSplitsRecorded();
    void testManualSaveAsyncTimingSplitsRecorded();
    void testInFlightSaveDoesNotResurrectARenamedNote();
    void testInFlightSaveDoesNotResurrectADeletedNote();
    void testFrontMatterChangeMarksDocumentDirty();
    void testOlderBodySnapshotDoesNotRestoreStaleFrontMatter();
    void testAutoSaveTimingSplitsRecorded();
    void testAutoSaveEditDuringAsyncWriteStaysDirtyAndNextCyclePersists();

    // Open operations
    void testOpenFile();
    void testOpenAsyncFile();
    void testOpenNonExistent();
    void testOpenClearsUndo();

    // Dirty state
    void testDirtyStateInitial();
    void testDirtyStateAfterEdit();
    void testDirtyStateAfterSave();
    void testDirtyStateAfterUndo();

    // Auto-save configuration
    void testAutoSaveConfiguration();

    // Round-trip test
    void testSaveAndOpenRoundTrip();

    // Front-matter pass-through and atomic saves
    void testForeignFrontMatterPreservedThroughEdit();
    void testFileWithoutFrontMatterGainsNone();
    void testDividerLedFileIsNotEatenAsFrontMatter();
    void testNewDocumentClearsFrontMatter();
    void testOpenReplacesFrontMatter();
    void testFailedSaveLeavesExistingFileIntact();

    // Journal and restore
    // One-time .bak before the first diverging overwrite
    void testDivergingLoadCreatesBackupOnFirstSave();
    void testCanonicalLoadCreatesNoBackup();
    void testParserLossyDivergenceCreatesBackup();
    void testSecondSaveLeavesBackupAlone();
    void testSaveAsCreatesNoBackupAndDisarms();
    void testExistingBackupNeverOverwritten();
    void testAsyncOpenArmsBackupToo();

    // Oversized-file guard: placeholder, not parse
    void testOversizedFileRefusedWithoutRead();
    void testOversizedOpenAnywayLoads();
    void testSizeCapConfigurable();

    void testJournalWritesOnDirtAndClearsOnSave();
    void testJournalTimingSplitsRecorded();
    void testJournalSkipsUnchangedSnapshot();
    void testAboutToSaveEmittedWithPath();
    void testRestoreBodyIsOneUndoStep();

private:
    QString readFile(const QString &path);

    BlockModel *m_model = nullptr;
    UndoStack *m_undoStack = nullptr;
    DocumentManager *m_manager = nullptr;
    QTemporaryDir *m_tempDir = nullptr;
};

void TestDocumentManager::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
}

void TestDocumentManager::cleanupTestCase()
{
    delete m_tempDir;
}

void TestDocumentManager::init()
{
    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setEmitToStderr(false);
    log.setLogFilePath(QString());
    log.setLevel(PerfLog::Off);
    log.setHumanOutput(false);

    m_model = new BlockModel();
    m_undoStack = new UndoStack();
    m_model->setUndoStack(m_undoStack);

    m_manager = new DocumentManager();
    m_manager->setBlockModel(m_model);
    m_manager->setUndoStack(m_undoStack);

    // Disable auto-save for tests
    m_manager->setAutoSaveEnabled(false);
}

void TestDocumentManager::cleanup()
{
    PerfLog::instance().clear();
    PerfLog::instance().setLevel(PerfLog::Off);

    delete m_manager;
    delete m_undoStack;
    delete m_model;
    m_manager = nullptr;
    m_undoStack = nullptr;
    m_model = nullptr;
}

void TestDocumentManager::testInitialState()
{
    QVERIFY(!m_manager->hasFile());
    QCOMPARE(m_manager->currentFilePath(), QString());
    QCOMPARE(m_manager->currentFileName(), QString("Untitled"));
    QVERIFY(!m_manager->isDirty());
}

void TestDocumentManager::testCurrentFileName()
{
    // Without file
    QCOMPARE(m_manager->currentFileName(), QString("Untitled"));

    // After saving
    QString filePath = m_tempDir->filePath("test_filename.md");
    m_model->insertBlockInternal(0, Block::Paragraph, "Test");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));
    QCOMPARE(m_manager->currentFileName(), QString("test_filename.md"));
}

void TestDocumentManager::testNewDocument()
{
    // Setup: add some content
    m_model->insertBlockInternal(0, Block::Heading1, "Title");
    m_model->insertBlockInternal(1, Block::Paragraph, "Content");
    QCOMPARE(m_model->count(), 2);

    // Create new document
    m_manager->newDocument();

    // Should have single empty paragraph
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(m_model->blockAt(0)->content(), QString(""));
    QVERIFY(!m_manager->hasFile());
}

void TestDocumentManager::testNewDocumentClearsUndo()
{
    // Setup: make some changes to build undo history
    m_model->insertBlock(0, Block::Paragraph, "First");
    m_model->insertBlock(1, Block::Paragraph, "Second");
    QVERIFY(m_undoStack->canUndo());

    // Create new document
    m_manager->newDocument();

    // Undo history should be cleared
    QVERIFY(!m_undoStack->canUndo());
    QVERIFY(m_undoStack->isClean());
}

void TestDocumentManager::testSaveWithoutFile()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Test");

    // save() without a file path should fail
    QVERIFY(!m_manager->save());
}

void TestDocumentManager::testSaveAs()
{
    m_model->insertBlockInternal(0, Block::Heading1, "Test Title");
    m_model->insertBlockInternal(1, Block::Paragraph, "Test content.");

    QString filePath = m_tempDir->filePath("saveAs_test.md");
    QSignalSpy saveSpy(m_manager, &DocumentManager::saveSucceeded);

    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));
    QCOMPARE(saveSpy.count(), 1);
    QCOMPARE(m_manager->currentFilePath(), filePath);
    QVERIFY(m_manager->hasFile());

    // Verify file contents
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    QVERIFY(content.contains("# Test Title"));
    QVERIFY(content.contains("Test content."));
}

void TestDocumentManager::testSaveAsAddsExtension()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Test");

    QString filePath = m_tempDir->filePath("no_extension");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));

    // Should have added .md extension
    QCOMPARE(m_manager->currentFilePath(), filePath + ".md");
    QVERIFY(QFile::exists(filePath + ".md"));
}

void TestDocumentManager::testSaveExisting()
{
    // First save
    m_model->insertBlockInternal(0, Block::Paragraph, "Initial content.");
    QString filePath = m_tempDir->filePath("existing_test.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));

    // Modify content
    m_model->updateContentInternal(0, "Modified content.");

    // Save again using save()
    QSignalSpy saveSpy(m_manager, &DocumentManager::saveSucceeded);
    QVERIFY(m_manager->save());
    QCOMPARE(saveSpy.count(), 1);

    // Verify file was updated
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    QVERIFY(content.contains("Modified content."));
    QVERIFY(!content.contains("Initial content."));
}

void TestDocumentManager::testSaveFailsOnInvalidPath()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Test");

    QSignalSpy failSpy(m_manager, &DocumentManager::saveFailed);

    // Try to save to a non-existent directory
    QString invalidPath = "/nonexistent/directory/file.md";
    QVERIFY(!m_manager->saveAs(QUrl::fromLocalFile(invalidPath)));
    QCOMPARE(failSpy.count(), 1);
}

void TestDocumentManager::testSaveTimingSplitsRecorded()
{
    PerfLog &log = PerfLog::instance();
    log.setLevel(PerfLog::Major);

    m_model->insertBlockInternal(0, Block::Paragraph, "Timed save");
    const QString filePath = m_tempDir->filePath("timed_save.md");

    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));

    const QStringList operations{
        QStringLiteral("note.save"),
        QStringLiteral("note.save.backup"),
        QStringLiteral("note.save.open"),
        QStringLiteral("note.save.serialize"),
        QStringLiteral("note.save.write"),
        QStringLiteral("note.save.flush"),
        QStringLiteral("note.save.commit"),
    };
    for (const QString &operation : operations)
        QCOMPARE(log.samples(operation).size(), 1);

    QCOMPARE(log.samples(QStringLiteral("note.save")).first()
                 .context.value(QStringLiteral("path")).toString(),
             filePath);
    QCOMPARE(log.samples(QStringLiteral("note.save")).first()
                 .context.value(QStringLiteral("blocks")).toInt(),
             1);
    QVERIFY(log.samples(QStringLiteral("note.save.serialize")).first()
                .context.value(QStringLiteral("chars")).toInt() > 0);
    QVERIFY(log.samples(QStringLiteral("note.save.write")).first()
                .context.value(QStringLiteral("chars")).toInt() > 0);
    QVERIFY(log.samples(QStringLiteral("note.save.open")).first()
                .context.value(QStringLiteral("ok")).toBool());
    QVERIFY(log.samples(QStringLiteral("note.save.commit")).first()
                .context.value(QStringLiteral("ok")).toBool());
    QCOMPARE(log.samples(QStringLiteral("note.autosave")).size(), 0);
}

// H5. Front matter is part of the document but not part of the block model, so
// the undo stack cannot speak for it. A metadata change used to leave the
// document calling itself clean, which meant every save/close/autosave decision
// that consults isDirty() was entitled to throw the change away.
void TestDocumentManager::testFrontMatterChangeMarksDocumentDirty()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Body");
    const QString path = m_tempDir->filePath("metadata_dirty.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(path)));
    QVERIFY(!m_manager->isDirty());

    QSignalSpy dirtySpy(m_manager, &DocumentManager::isDirtyChanged);
    m_manager->setFrontMatter(QStringLiteral("---\ntags: [added]\n---\n"));

    QVERIFY2(m_manager->isDirty(),
             "a metadata change leaves the document differing from disk");
    QVERIFY(dirtySpy.count() >= 1);

    // And saving it actually persists the metadata.
    QVERIFY(m_manager->save());
    QVERIFY(!m_manager->isDirty());
    QVERIFY(readFile(path).contains(QStringLiteral("tags: [added]")));
}

// H5. A body snapshot taken before a metadata change carries the old front
// matter, so it must not be accepted afterwards as a successful save of the
// current document.
//
// Worth recording what running this established, because the review predicted
// worse: the stale snapshot does reach disk, but it never marked the document
// clean even before setFrontMatter advanced the revision - the undo-index arm
// of the same check already rejected it. The revision bump is defence in depth
// here rather than the sole guard. The defect that did bite is the one in
// testFrontMatterChangeMarksDocumentDirty, where the document called itself
// clean while holding metadata that had never been written.
void TestDocumentManager::testOlderBodySnapshotDoesNotRestoreStaleFrontMatter()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Body");
    const QString path = m_tempDir->filePath("snapshot_vs_metadata.md");
    m_manager->setFrontMatter(QStringLiteral("---\ntags: [before]\n---\n"));
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(path)));

    // A body save starts, carrying the current (old) front matter.
    m_manager->setAsyncPersistenceDelayMsForTests(400);
    m_model->updateContent(0, QStringLiteral("Edited body"));
    QVERIFY(m_manager->saveAsync());

    // The metadata changes while that snapshot is still in flight.
    m_manager->setFrontMatter(QStringLiteral("---\ntags: [after]\n---\n"));

    // The snapshot in flight carries the OLD front matter, so accepting it as
    // a successful save of the current document is the defect: it would mark
    // the document clean while the newer metadata exists only in memory.
    QSignalSpy okSpy(m_manager, &DocumentManager::saveSucceeded);
    m_manager->setAsyncPersistenceDelayMsForTests(0);
    QTest::qWait(800);

    QCOMPARE(okSpy.count(), 0);
    QVERIFY2(m_manager->isDirty(),
             "the in-flight snapshot predates the metadata change, so the "
             "document still holds unsaved state and must not be called clean");

    // The stale snapshot did reach disk - it was already committed - so the
    // guarantee that matters is that the document knows it is behind and the
    // next save corrects the file.
    QVERIFY(m_manager->save());
    const QString written = readFile(path);
    QVERIFY2(written.contains(QStringLiteral("tags: [after]")),
             "the newer metadata must survive the older body snapshot");
    QVERIFY(!written.contains(QStringLiteral("tags: [before]")));
}

// H4. Autosave hands a worker the path the note had when the save started.
// Renaming the note in the collection while that worker is still running used
// to leave two files: the renamed note, and the old name recreated by a write
// that had no idea the note had moved. The rebind has to call the write off.
void TestDocumentManager::testInFlightSaveDoesNotResurrectARenamedNote()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Initial");
    const QString oldPath = m_tempDir->filePath("before_rename.md");
    const QString newPath = m_tempDir->filePath("after_rename.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(oldPath)));

    // A save is in flight against the old path.
    m_manager->setAsyncPersistenceDelayMsForTests(400);
    m_model->updateContent(0, QStringLiteral("Edited before the rename"));
    QVERIFY(m_manager->saveAsync());

    // The collection renames the note underneath it, then rebinds.
    QVERIFY(QFile::rename(oldPath, newPath));
    m_manager->rebindFilePath(newPath);

    m_manager->setAsyncPersistenceDelayMsForTests(0);
    QTest::qWait(800);   // outlive the worker either way

    QVERIFY2(!QFile::exists(oldPath),
             "the abandoned save recreated the note at the name it no longer "
             "has, leaving a duplicate beside the renamed file");
    QVERIFY(QFile::exists(newPath));
    QCOMPARE(m_manager->currentFilePath(), newPath);
}

// H4, the destructive variant. Deleting the open note moves it to the trash;
// a save still running against the old path puts it straight back outside the
// trash, so the note the user deleted reappears.
void TestDocumentManager::testInFlightSaveDoesNotResurrectADeletedNote()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Initial");
    const QString path = m_tempDir->filePath("deleted_while_saving.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(path)));

    m_manager->setAsyncPersistenceDelayMsForTests(400);
    m_model->updateContent(0, QStringLiteral("Edited before the delete"));
    QVERIFY(m_manager->saveAsync());

    // The collection removes the note, and the shell closes the document.
    QVERIFY(QFile::remove(path));
    m_manager->newDocument();

    m_manager->setAsyncPersistenceDelayMsForTests(0);
    QTest::qWait(800);

    QVERIFY2(!QFile::exists(path),
             "the abandoned save recreated a note the user deleted");
}

void TestDocumentManager::testManualSaveAsyncTimingSplitsRecorded()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Initial");
    const QString filePath = m_tempDir->filePath("timed_async_save.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));

    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setLevel(PerfLog::Major);

    m_manager->setAsyncPersistenceDelayMsForTests(150);
    m_model->updateContent(0, QStringLiteral("Async manual save"));
    QVERIFY(m_manager->isDirty());

    QSignalSpy saveSpy(m_manager, &DocumentManager::saveSucceeded);
    QVERIFY(m_manager->saveAsync());
    QVERIFY(m_manager->isDirty());

    const QList<PerfLog::Sample> main =
        log.samples(QStringLiteral("note.save"));
    QCOMPARE(main.size(), 1);
    QCOMPARE(main.first().context.value(QStringLiteral("path")).toString(),
             filePath);
    QVERIFY(main.first().context.value(QStringLiteral("async")).toBool());

    QTRY_COMPARE_WITH_TIMEOUT(saveSpy.count(), 1, 5000);
    QCOMPARE(readFile(filePath), QStringLiteral("Async manual save\n"));
    QVERIFY(!m_manager->isDirty());

    const QStringList operations{
        QStringLiteral("note.save"),
        QStringLiteral("note.save.backup"),
        QStringLiteral("note.save.open"),
        QStringLiteral("note.save.serialize"),
        QStringLiteral("note.save.write"),
        QStringLiteral("note.save.flush"),
        QStringLiteral("note.save.commit"),
    };
    for (const QString &operation : operations)
        QCOMPARE(log.samples(operation).size(), 1);

    QVERIFY(log.samples(QStringLiteral("note.save.serialize")).first()
                .context.value(QStringLiteral("chars")).toInt() > 0);
    QVERIFY(log.samples(QStringLiteral("note.save.open")).first()
                .context.value(QStringLiteral("ok")).toBool());
    QVERIFY(log.samples(QStringLiteral("note.save.commit")).first()
                .context.value(QStringLiteral("ok")).toBool());
    QCOMPARE(log.samples(QStringLiteral("note.autosave")).size(), 0);
}

void TestDocumentManager::testAutoSaveTimingSplitsRecorded()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Initial");
    const QString filePath = m_tempDir->filePath("timed_autosave.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));

    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setLevel(PerfLog::Major);

    m_model->updateContent(0, QStringLiteral("Dirty autosave"));
    QVERIFY(m_manager->isDirty());

    QSignalSpy saveSpy(m_manager, &DocumentManager::saveSucceeded);
    QVERIFY(QMetaObject::invokeMethod(m_manager, "onAutoSaveTimer",
                                      Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(saveSpy.count(), 1, 5000);
    QVERIFY(!m_manager->isDirty());

    const QStringList operations{
        QStringLiteral("note.autosave"),
        QStringLiteral("note.autosave.backup"),
        QStringLiteral("note.autosave.open"),
        QStringLiteral("note.autosave.serialize"),
        QStringLiteral("note.autosave.write"),
        QStringLiteral("note.autosave.flush"),
        QStringLiteral("note.autosave.commit"),
    };
    for (const QString &operation : operations)
        QCOMPARE(log.samples(operation).size(), 1);

    QCOMPARE(log.samples(QStringLiteral("note.save")).size(), 0);
    QCOMPARE(log.samples(QStringLiteral("note.autosave")).first()
                 .context.value(QStringLiteral("path")).toString(),
             filePath);
    QVERIFY(log.samples(QStringLiteral("note.autosave.commit")).first()
                .context.value(QStringLiteral("ok")).toBool());
}

void TestDocumentManager::testAutoSaveEditDuringAsyncWriteStaysDirtyAndNextCyclePersists()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Initial");
    const QString filePath = m_tempDir->filePath("autosave_race.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));

    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setLevel(PerfLog::Major);

    m_manager->setAsyncPersistenceDelayMsForTests(150);
    m_model->updateContent(0, QStringLiteral("Snapshot one"));
    QSignalSpy saveSpy(m_manager, &DocumentManager::saveSucceeded);
    QVERIFY(QMetaObject::invokeMethod(m_manager, "onAutoSaveTimer",
                                      Qt::DirectConnection));

    m_model->updateContent(0, QStringLiteral("Snapshot two"));
    QTRY_COMPARE_WITH_TIMEOUT(
        log.samples(QStringLiteral("note.autosave.commit")).size(), 1, 5000);
    QCOMPARE(readFile(filePath), QStringLiteral("Snapshot one\n"));
    QVERIFY(m_manager->isDirty());
    QCOMPARE(saveSpy.count(), 0);

    m_manager->setAsyncPersistenceDelayMsForTests(0);
    QVERIFY(QMetaObject::invokeMethod(m_manager, "onAutoSaveTimer",
                                      Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(saveSpy.count(), 1, 5000);
    QCOMPARE(readFile(filePath), QStringLiteral("Snapshot two\n"));
    QVERIFY(!m_manager->isDirty());
}

void TestDocumentManager::testOpenFile()
{
    // Create a test file
    QString filePath = m_tempDir->filePath("open_test.md");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("# Opened Title\n\nOpened content.\n");
    file.close();

    // Open the file
    QSignalSpy openSpy(m_manager, &DocumentManager::openSucceeded);
    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    QCOMPARE(openSpy.count(), 1);

    // Verify content
    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Heading1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("Opened Title"));
    QCOMPARE(m_model->blockAt(1)->blockType(), Block::Paragraph);
    QCOMPARE(m_model->blockAt(1)->content(), QString("Opened content."));
    QCOMPARE(m_manager->currentFilePath(), filePath);
}

void TestDocumentManager::testOpenAsyncFile()
{
    const QString frontMatter =
        QStringLiteral("---\ntags: [async]\n---\n");
    QString filePath = m_tempDir->filePath("open_async_test.md");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write((frontMatter
                + QStringLiteral("# Async Title\n\nAsync content.\n"))
                   .toUtf8());
    file.close();

    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("Dirty draft"));
    QVERIFY(m_manager->isDirty());

    QSignalSpy openSpy(m_manager, &DocumentManager::openSucceeded);
    QSignalSpy finishedSpy(m_manager, &DocumentManager::openAsyncFinished);
    QSignalSpy progressSpy(m_manager, &DocumentManager::openInProgressChanged);

    QVERIFY(m_manager->openAsync(QUrl::fromLocalFile(filePath)));
    QVERIFY(m_manager->openInProgress());
    QVERIFY(progressSpy.count() >= 1);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 5000);
    QCOMPARE(finishedSpy.first().at(0).toString(), filePath);
    QCOMPARE(finishedSpy.first().at(1).toBool(), true);
    QCOMPARE(openSpy.count(), 1);
    QVERIFY(!m_manager->openInProgress());

    QCOMPARE(m_manager->frontMatter(), frontMatter);
    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Heading1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("Async Title"));
    QCOMPARE(m_model->blockAt(1)->blockType(), Block::Paragraph);
    QCOMPARE(m_model->blockAt(1)->content(), QString("Async content."));
    QCOMPARE(m_manager->currentFilePath(), filePath);
    QVERIFY(!m_manager->isDirty());
    QVERIFY(m_undoStack->isClean());
}

void TestDocumentManager::testOpenNonExistent()
{
    QSignalSpy failSpy(m_manager, &DocumentManager::openFailed);

    QString invalidPath = m_tempDir->filePath("does_not_exist.md");
    QVERIFY(!m_manager->open(QUrl::fromLocalFile(invalidPath)));
    QCOMPARE(failSpy.count(), 1);
}

void TestDocumentManager::testOpenClearsUndo()
{
    // Build some undo history
    m_model->insertBlock(0, Block::Paragraph, "Test");
    QVERIFY(m_undoStack->canUndo());

    // Create and open a file
    QString filePath = m_tempDir->filePath("clear_undo_test.md");
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("New content.\n");
    file.close();

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));

    // Undo stack should be cleared
    QVERIFY(!m_undoStack->canUndo());
    QVERIFY(m_undoStack->isClean());
}

void TestDocumentManager::testDirtyStateInitial()
{
    m_undoStack->setClean();
    QVERIFY(!m_manager->isDirty());
    QVERIFY(m_undoStack->isClean());
}

void TestDocumentManager::testDirtyStateAfterEdit()
{
    m_undoStack->setClean();
    QVERIFY(!m_manager->isDirty());

    // Make a change through the undo-aware method
    m_model->insertBlock(0, Block::Paragraph, "New content");

    // Should now be dirty
    QVERIFY(m_manager->isDirty());
    QVERIFY(!m_undoStack->isClean());
}

void TestDocumentManager::testDirtyStateAfterSave()
{
    m_model->insertBlock(0, Block::Paragraph, "Test");
    QVERIFY(m_manager->isDirty());

    QString filePath = m_tempDir->filePath("dirty_state_test.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));

    // Should be clean after save
    QVERIFY(!m_manager->isDirty());
}

void TestDocumentManager::testDirtyStateAfterUndo()
{
    m_undoStack->setClean();

    // Make change
    m_model->insertBlock(0, Block::Paragraph, "Test");
    QVERIFY(m_manager->isDirty());

    // Undo
    m_undoStack->undo();

    // Should be clean again (back to clean state)
    QVERIFY(!m_manager->isDirty());
}

void TestDocumentManager::testAutoSaveConfiguration()
{
    // Test default values
    QVERIFY(!m_manager->autoSaveEnabled());  // Disabled in init()
    QCOMPARE(m_manager->autoSaveInterval(), 30);  // Default

    // Change values
    m_manager->setAutoSaveEnabled(true);
    QVERIFY(m_manager->autoSaveEnabled());

    m_manager->setAutoSaveInterval(60);
    QCOMPARE(m_manager->autoSaveInterval(), 60);

    // Minimum interval should be enforced
    m_manager->setAutoSaveInterval(2);
    QCOMPARE(m_manager->autoSaveInterval(), 5);  // Minimum is 5
}

void TestDocumentManager::testSaveAndOpenRoundTrip()
{
    // Create a document with various block types
    m_model->insertBlockInternal(0, Block::Heading1, "Document Title");
    m_model->insertBlockInternal(1, Block::Paragraph, "First paragraph with **bold**.");
    m_model->insertBlockInternal(2, Block::Heading2, "Section");
    m_model->insertBlockInternal(3, Block::Paragraph, "Second paragraph with *italic*.");
    m_model->insertBlockInternal(4, Block::Heading3, "Subsection");

    // Save
    QString filePath = m_tempDir->filePath("roundtrip_test.md");
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));

    // Create a new manager and model to open the file
    BlockModel model2;
    UndoStack undoStack2;
    model2.setUndoStack(&undoStack2);
    DocumentManager manager2;
    manager2.setBlockModel(&model2);
    manager2.setUndoStack(&undoStack2);

    QVERIFY(manager2.open(QUrl::fromLocalFile(filePath)));

    // Verify content matches
    QCOMPARE(model2.count(), m_model->count());
    for (int i = 0; i < m_model->count(); ++i) {
        QCOMPARE(model2.blockAt(i)->blockType(), m_model->blockAt(i)->blockType());
        QCOMPARE(model2.blockAt(i)->content(), m_model->blockAt(i)->content());
    }
}

QString TestDocumentManager::readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    return stream.readAll();
}

// ---- one-time .bak before the first diverging overwrite ----

namespace {
void writeRawFile(const QString &path, const QString &content)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(content.toUtf8());
    file.close();
}
} // namespace

void TestDocumentManager::testDivergingLoadCreatesBackupOnFirstSave()
{
    // Obsidian-style bullets: parse reads "*" natively, serialize writes
    // "-" — the load-time round-trip comparison diverges.
    const QString original = QStringLiteral("* one\n* two\n");
    const QString filePath = m_tempDir->filePath("diverge.md");
    writeRawFile(filePath, original);

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("edited"));
    QVERIFY(m_manager->save());

    // The backup holds the original bytes; the file is canonical.
    QCOMPARE(readFile(filePath + QStringLiteral(".bak")), original);
    QCOMPARE(readFile(filePath),
             QStringLiteral("edited\n\n- one\n- two\n"));
}

void TestDocumentManager::testCanonicalLoadCreatesNoBackup()
{
    // A Kvit-authored file round-trips byte-identically, so the load-time
    // comparison matches and no backup is ever made.
    const QString filePath = m_tempDir->filePath("canonical.md");
    writeRawFile(filePath, QStringLiteral("# Title\n\n- one\n- two\n"));

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("edited"));
    QVERIFY(m_manager->save());

    QVERIFY(!QFile::exists(filePath + QStringLiteral(".bak")));
}

void TestDocumentManager::testParserLossyDivergenceCreatesBackup()
{
    // Lossy changes that live in the parser rather than the text pass also
    // trip the trigger — it is deliberately layer-agnostic. H5 demotes to
    // H4, and an overlong table row truncates.
    const QString h5 = QStringLiteral("##### Deep Heading\n");
    const QString h5Path = m_tempDir->filePath("lossy_h5.md");
    writeRawFile(h5Path, h5);
    QVERIFY(m_manager->open(QUrl::fromLocalFile(h5Path)));
    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("edit"));
    QVERIFY(m_manager->save());
    QCOMPARE(readFile(h5Path + QStringLiteral(".bak")), h5);

    const QString ragged =
        QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 | 3 |\n");
    const QString raggedPath = m_tempDir->filePath("lossy_table.md");
    writeRawFile(raggedPath, ragged);
    QVERIFY(m_manager->open(QUrl::fromLocalFile(raggedPath)));
    m_model->insertBlock(1, Block::Paragraph, QStringLiteral("edit"));
    QVERIFY(m_manager->save());
    QCOMPARE(readFile(raggedPath + QStringLiteral(".bak")), ragged);
}

void TestDocumentManager::testSecondSaveLeavesBackupAlone()
{
    const QString original = QStringLiteral("* bullet\n");
    const QString filePath = m_tempDir->filePath("second_save.md");
    writeRawFile(filePath, original);

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("first edit"));
    QVERIFY(m_manager->save());
    QCOMPARE(readFile(filePath + QStringLiteral(".bak")), original);

    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("second edit"));
    QVERIFY(m_manager->save());
    // Still the original bytes: one backup per load, ever.
    QCOMPARE(readFile(filePath + QStringLiteral(".bak")), original);
}

void TestDocumentManager::testSaveAsCreatesNoBackupAndDisarms()
{
    const QString original = QStringLiteral("* bullet\n");
    const QString filePath = m_tempDir->filePath("saveas_src.md");
    const QString otherPath = m_tempDir->filePath("saveas_dst.md");
    writeRawFile(filePath, original);

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("edited"));
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(otherPath)));

    // The original file and its absence of a backup are both left alone,
    // and the new path gets none either.
    QCOMPARE(readFile(filePath), original);
    QVERIFY(!QFile::exists(filePath + QStringLiteral(".bak")));
    QVERIFY(!QFile::exists(otherPath + QStringLiteral(".bak")));

    // The flag disarmed: a later save (now bound to the new path) makes
    // no backup.
    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("edited again"));
    QVERIFY(m_manager->save());
    QVERIFY(!QFile::exists(otherPath + QStringLiteral(".bak")));
}

void TestDocumentManager::testExistingBackupNeverOverwritten()
{
    // An existing .bak is presumed to hold an earlier (closer-to-original)
    // form; the save proceeds without touching it.
    const QString sentinel = QStringLiteral("earlier form\n");
    const QString filePath = m_tempDir->filePath("keep_bak.md");
    writeRawFile(filePath, QStringLiteral("* bullet\n"));
    writeRawFile(filePath + QStringLiteral(".bak"), sentinel);

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("edited"));
    QVERIFY(m_manager->save());

    QCOMPARE(readFile(filePath + QStringLiteral(".bak")), sentinel);
}

void TestDocumentManager::testAsyncOpenArmsBackupToo()
{
    // The async loader computes the same divergence bit on its worker
    // thread; front matter passes through the comparison verbatim.
    const QString original =
        QStringLiteral("---\ntags: [x]\n---\n* bullet\n");
    const QString filePath = m_tempDir->filePath("async_diverge.md");
    writeRawFile(filePath, original);

    QSignalSpy finishedSpy(m_manager, &DocumentManager::openAsyncFinished);
    QVERIFY(m_manager->openAsync(QUrl::fromLocalFile(filePath)));
    QTRY_COMPARE(finishedSpy.count(), 1);

    m_model->insertBlock(0, Block::Paragraph, QStringLiteral("edited"));
    QVERIFY(m_manager->save());
    QCOMPARE(readFile(filePath + QStringLiteral(".bak")), original);
}

void TestDocumentManager::testForeignFrontMatterPreservedThroughEdit()
{
    // A note written by another tool: its properties block must survive a
    // Kvit edit byte-identically while the body changes.
    const QString frontMatter = QStringLiteral(
        "---\nlayout: post\naliases:\n  - n\ntags: [imported]\n---\n");
    QString filePath = m_tempDir->filePath("foreign_meta.md");
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write((frontMatter + QStringLiteral("Original body\n")).toUtf8());
    }

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    QCOMPARE(m_manager->frontMatter(), frontMatter);

    // Only the body reaches the model.
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("Original body"));

    m_model->updateContent(0, QStringLiteral("Edited body"));
    QVERIFY(m_manager->save());

    QCOMPARE(readFile(filePath), frontMatter + QStringLiteral("Edited body\n"));
}

void TestDocumentManager::testFileWithoutFrontMatterGainsNone()
{
    QString filePath = m_tempDir->filePath("plain_body.md");
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Plain paragraph\n");
    }

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    QCOMPARE(m_manager->frontMatter(), QString());

    m_model->updateContent(0, QStringLiteral("Still plain"));
    QVERIFY(m_manager->save());
    QCOMPARE(readFile(filePath), QStringLiteral("Still plain\n"));
}

void TestDocumentManager::testDividerLedFileIsNotEatenAsFrontMatter()
{
    // A document whose first block is a divider starts with "---" exactly
    // like a front-matter fence; it must load as blocks and round-trip.
    const QString content =
        QStringLiteral("---\n\nBetween dividers\n\n---\n");
    QString filePath = m_tempDir->filePath("divider_led.md");
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write(content.toUtf8());
    }

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    QCOMPARE(m_manager->frontMatter(), QString());
    QCOMPARE(m_model->count(), 3);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Divider);
    QCOMPARE(m_model->blockAt(1)->content(), QString("Between dividers"));
    QCOMPARE(m_model->blockAt(2)->blockType(), Block::Divider);

    QVERIFY(m_manager->save());
    QCOMPARE(readFile(filePath), content);
}

void TestDocumentManager::testNewDocumentClearsFrontMatter()
{
    const QString frontMatter = QStringLiteral("---\ntags: [a]\n---\n");
    QString filePath = m_tempDir->filePath("with_meta.md");
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write((frontMatter + QStringLiteral("Body\n")).toUtf8());
    }
    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    QCOMPARE(m_manager->frontMatter(), frontMatter);

    m_manager->newDocument();
    QCOMPARE(m_manager->frontMatter(), QString());

    QString newPath = m_tempDir->filePath("fresh.md");
    m_model->updateContent(0, QStringLiteral("Fresh"));
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(newPath)));
    QCOMPARE(readFile(newPath), QStringLiteral("Fresh\n"));
}

void TestDocumentManager::testOpenReplacesFrontMatter()
{
    const QString metaA = QStringLiteral("---\ntags: [a]\n---\n");
    QString pathA = m_tempDir->filePath("meta_a.md");
    {
        QFile file(pathA);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write((metaA + QStringLiteral("A\n")).toUtf8());
    }
    QString pathB = m_tempDir->filePath("meta_b.md");
    {
        QFile file(pathB);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("B\n");
    }

    QVERIFY(m_manager->open(QUrl::fromLocalFile(pathA)));
    QCOMPARE(m_manager->frontMatter(), metaA);
    QVERIFY(m_manager->open(QUrl::fromLocalFile(pathB)));
    QCOMPARE(m_manager->frontMatter(), QString());

    // Saving B must not leak A's metadata into it.
    m_model->updateContent(0, QStringLiteral("B edited"));
    QVERIFY(m_manager->save());
    QCOMPARE(readFile(pathB), QStringLiteral("B edited\n"));
}

void TestDocumentManager::testFailedSaveLeavesExistingFileIntact()
{
    // Atomic writes (QSaveFile): when the save cannot complete, the
    // previous file content survives untouched.
    QString dirPath = m_tempDir->filePath("locked_dir");
    QVERIFY(QDir().mkpath(dirPath));
    QString filePath = dirPath + QStringLiteral("/note.md");
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Original content\n");
    }

    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));
    m_model->updateContent(0, QStringLiteral("Changed"));

    // A read-only directory rejects the temporary file QSaveFile needs.
    // NTFS ignores the read-only attribute on directories for file
    // creation, so on Windows the obstacle is the target file itself,
    // which QSaveFile::open() refuses while it is not writable.
#ifdef Q_OS_WIN
    QFile obstacle(filePath);
#else
    QFile obstacle(dirPath);
#endif
    QVERIFY(obstacle.setPermissions(QFileDevice::ReadOwner
                                    | QFileDevice::ExeOwner));

    QSignalSpy failSpy(m_manager, &DocumentManager::saveFailed);
    QVERIFY(!m_manager->save());
    QCOMPARE(failSpy.count(), 1);

    QVERIFY(obstacle.setPermissions(QFileDevice::ReadOwner
                                    | QFileDevice::WriteOwner
                                    | QFileDevice::ExeOwner));
    QCOMPARE(readFile(filePath), QStringLiteral("Original content\n"));

    // And the same save succeeds once the obstacle is gone.
    QVERIFY(m_manager->save());
    QCOMPARE(readFile(filePath), QStringLiteral("Changed\n"));
}

// ---- oversized-file guard ----

namespace {
// A ~2 MiB prose-shaped markdown file.
QString writeBigFile(QTemporaryDir *dir, const QString &name)
{
    const QString path = dir->filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    const QByteArray line("A line of ordinary prose to bulk the file up.\n");
    qint64 written = 0;
    while (written < qint64(2) * 1024 * 1024) {
        file.write(line);
        written += line.size();
    }
    file.close();
    return path;
}
} // namespace

void TestDocumentManager::testOversizedFileRefusedWithoutRead()
{
    const QString path = writeBigFile(m_tempDir, "whale.md");
    QVERIFY(!path.isEmpty());
    m_manager->setMaxOpenFileSizeMiB(1);

    m_model->insertBlockInternal(0, Block::Paragraph, "existing");
    QSignalSpy rejectedSpy(m_manager,
                           &DocumentManager::openRejectedTooLarge);
    QSignalSpy openedSpy(m_manager, &DocumentManager::openSucceeded);

    // Sync path: refused, no read, model untouched.
    QVERIFY(!m_manager->open(QUrl::fromLocalFile(path)));
    QCOMPARE(rejectedSpy.count(), 1);
    QCOMPARE(openedSpy.count(), 0);
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("existing"));
    QCOMPARE(rejectedSpy.at(0).at(0).toString(), path);
    QVERIFY(rejectedSpy.at(0).at(1).toDouble() > 2.0 * 1024 * 1024 - 64);
    QCOMPARE(rejectedSpy.at(0).at(2).toDouble(), 1.0 * 1024 * 1024);

    // Async path: refused the same way, and the finished signal resolves.
    QSignalSpy finishedSpy(m_manager, &DocumentManager::openAsyncFinished);
    QVERIFY(!m_manager->openAsync(QUrl::fromLocalFile(path)));
    QCOMPARE(rejectedSpy.count(), 2);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.at(0).at(1).toBool(), false);
}

void TestDocumentManager::testOversizedOpenAnywayLoads()
{
    const QString path = writeBigFile(m_tempDir, "whale_anyway.md");
    QVERIFY(!path.isEmpty());
    m_manager->setMaxOpenFileSizeMiB(1);

    // Open anyway: the normal path, unmodified — slow but correct.
    QVERIFY(m_manager->open(QUrl::fromLocalFile(path), true));
    QVERIFY(m_model->count() >= 1);
    QCOMPARE(m_manager->currentFilePath(), path);
}

void TestDocumentManager::testSizeCapConfigurable()
{
    const QString path = writeBigFile(m_tempDir, "whale_cap.md");
    QVERIFY(!path.isEmpty());

    // A larger cap admits the file...
    m_manager->setMaxOpenFileSizeMiB(3);
    QVERIFY(m_manager->open(QUrl::fromLocalFile(path)));

    // ...and 0 disables the guard entirely.
    m_manager->setMaxOpenFileSizeMiB(0);
    QVERIFY(m_manager->open(QUrl::fromLocalFile(path)));
}

void TestDocumentManager::testJournalWritesOnDirtAndClearsOnSave()
{
    // Open a real file so dirty state and saves behave normally.
    QString filePath = m_tempDir->filePath("journaled.md");
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Original\n");
    }
    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));

    QString journalPath = m_tempDir->filePath("journal-entry");
    m_manager->setJournalPath(journalPath);
    m_manager->setJournalDebounceMs(30);

    // An edit starts the debounce; the snapshot lands after it.
    m_model->updateContent(0, QStringLiteral("Edited but unsaved"));
    QVERIFY(m_manager->isDirty());
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(journalPath), 2000);
    QCOMPARE(readFile(journalPath), QStringLiteral("Edited but unsaved\n"));

    // A clean save removes the journal; the disk file has the content.
    QVERIFY(m_manager->save());
    QVERIFY(!QFileInfo::exists(journalPath));

    // Clean-state edits later journal again; disabling stops it.
    m_model->updateContent(0, QStringLiteral("More edits"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(journalPath), 2000);
    QFile::remove(journalPath);
    m_manager->setJournalPath(QString());
    m_model->updateContent(0, QStringLiteral("Untracked edits"));
    QTest::qWait(100);
    QVERIFY(!QFileInfo::exists(journalPath));
}

void TestDocumentManager::testJournalTimingSplitsRecorded()
{
    QString filePath = m_tempDir->filePath("timed_journal_source.md");
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Original\n");
    }
    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));

    const QString journalPath = m_tempDir->filePath("timed-journal");
    m_manager->setJournalPath(journalPath);
    m_model->updateContent(0, QStringLiteral("Journal timing"));
    QVERIFY(m_manager->isDirty());

    PerfLog &log = PerfLog::instance();
    log.setLevel(PerfLog::Major);

    QVERIFY(QMetaObject::invokeMethod(m_manager, "writeJournal",
                                      Qt::DirectConnection));

    QTRY_COMPARE_WITH_TIMEOUT(
        log.samples(QStringLiteral("note.journal_write.commit")).size(), 1,
        5000);

    const QStringList operations{
        QStringLiteral("note.journal_write"),
        QStringLiteral("note.journal_write.open"),
        QStringLiteral("note.journal_write.serialize"),
        QStringLiteral("note.journal_write.write"),
        QStringLiteral("note.journal_write.flush"),
        QStringLiteral("note.journal_write.commit"),
    };
    for (const QString &operation : operations)
        QCOMPARE(log.samples(operation).size(), 1);

    QCOMPARE(log.samples(QStringLiteral("note.journal_write")).first()
                 .context.value(QStringLiteral("path")).toString(),
             journalPath);
    QCOMPARE(log.samples(QStringLiteral("note.journal_write")).first()
                 .context.value(QStringLiteral("blocks")).toInt(),
             1);
    QVERIFY(log.samples(QStringLiteral("note.journal_write.serialize")).first()
                .context.value(QStringLiteral("chars")).toInt() > 0);
    QVERIFY(log.samples(QStringLiteral("note.journal_write.commit")).first()
                .context.value(QStringLiteral("ok")).toBool());
    QCOMPARE(readFile(journalPath), QStringLiteral("Journal timing\n"));
}

void TestDocumentManager::testJournalSkipsUnchangedSnapshot()
{
    QString filePath = m_tempDir->filePath("hash_guard_source.md");
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("Original\n");
    }
    QVERIFY(m_manager->open(QUrl::fromLocalFile(filePath)));

    const QString journalPath = m_tempDir->filePath("hash-guard-journal");
    m_manager->setJournalPath(journalPath);
    m_model->updateContent(0, QStringLiteral("Same dirty content"));

    PerfLog &log = PerfLog::instance();
    log.setLevel(PerfLog::Major);

    QVERIFY(QMetaObject::invokeMethod(m_manager, "writeJournal",
                                      Qt::DirectConnection));
    QTRY_COMPARE_WITH_TIMEOUT(
        log.samples(QStringLiteral("note.journal_write.commit")).size(), 1,
        5000);
    QCOMPARE(readFile(journalPath), QStringLiteral("Same dirty content\n"));

    log.clear();
    QVERIFY(QMetaObject::invokeMethod(m_manager, "writeJournal",
                                      Qt::DirectConnection));
    QCOMPARE(log.samples(QStringLiteral("note.journal_write")).size(), 1);
    QCOMPARE(log.samples(QStringLiteral("note.journal_write.serialize")).size(), 1);
    QCOMPARE(log.samples(QStringLiteral("note.journal_write.open")).size(), 0);
    QCOMPARE(log.samples(QStringLiteral("note.journal_write.write")).size(), 0);
    QCOMPARE(log.samples(QStringLiteral("note.journal_write.commit")).size(), 0);
    QVERIFY(log.samples(QStringLiteral("note.journal_write")).first()
                .context.value(QStringLiteral("skipped")).toBool());
}

void TestDocumentManager::testAboutToSaveEmittedWithPath()
{
    QString filePath = m_tempDir->filePath("hooked.md");
    m_model->insertBlockInternal(0, Block::Paragraph, "Hook me");

    QSignalSpy aboutSpy(m_manager, &DocumentManager::aboutToSave);
    QVERIFY(m_manager->saveAs(QUrl::fromLocalFile(filePath)));
    QCOMPARE(aboutSpy.count(), 1);
    QCOMPARE(aboutSpy.at(0).at(0).toString(), filePath);

    m_model->updateContent(0, QStringLiteral("Hook me again"));
    QVERIFY(m_manager->save());
    QCOMPARE(aboutSpy.count(), 2);
}

void TestDocumentManager::testRestoreBodyIsOneUndoStep()
{
    m_model->insertBlockInternal(0, Block::Heading1, "Current title");
    m_model->insertBlockInternal(1, Block::Paragraph, "Current body");
    m_undoStack->clear();
    m_undoStack->setClean();

    QVERIFY(m_manager->restoreBody(
        QStringLiteral("Restored paragraph\n\n- restored item\n")));
    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(m_model->blockAt(0)->content(), QString("Restored paragraph"));
    QCOMPARE(m_model->blockAt(1)->blockType(), Block::BulletList);

    // ONE undo returns the entire pre-restore document.
    QVERIFY(m_undoStack->canUndo());
    m_undoStack->undo();
    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Heading1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("Current title"));
    QCOMPARE(m_model->blockAt(1)->content(), QString("Current body"));

    // An empty backup restores to one empty paragraph, still undoable.
    m_undoStack->redo();
    QVERIFY(m_manager->restoreBody(QString()));
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(m_model->blockAt(0)->content(), QString());
}

QTEST_MAIN(TestDocumentManager)
#include "test_documentmanager.moc"
