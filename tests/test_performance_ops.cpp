// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "blockmodel.h"
#include "documentoutline.h"
#include "documentsearch.h"
#include "documentselection.h"
#include "documentserializer.h"
#include "documentstats.h"
#include "documentmanager.h"
#include "notecollection.h"
#include "perflog.h"
#include "startupcontroller.h"
#include "undostack.h"
#include "perf/corpus.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

class TestPerformanceOps : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void loadSignalCount();
    void fullDocumentCount();
    void incrementalTotalUpdate();
    void mixed100KeystrokeLatency();
    void collectionScanVault10K();
    void collectionAsyncOpenVault10K();
    void collectionWarmStartVault10K();
    void collectionAsyncOpenSmallVaultHugeNote();
    void collectionAsyncRefreshDirectoryHugeNote();
    void startupControllerRememberedHugeNoteStartsAsync();
    void persistenceTimingSplits();
    void documentSearchRecompute();
    void documentSearchHighMatchRecompute();
    void outlineRebuild();
    void outlineNonHeadingEditDoesNotRebuild();
    void bulkDelete();
    void selectionPrune();
    // Phase 6: O(1) selection via the id->index map
    void selectionIdLookup();
    void selectAllDelete();
    void selectionDragTick();
    // Phase 7: cached ordinals on pathological list documents
    void ordinalSweepList5K();
};

namespace {

bool writeCollectionState(const QString &rootPath, const QString &lastOpenNote)
{
    const QString kvitPath = QDir(rootPath).filePath(QStringLiteral(".kvit"));
    if (!QDir().mkpath(kvitPath))
        return false;

    QFile file(QDir(kvitPath).filePath(QStringLiteral("collection.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << QStringLiteral("{\"lastOpenNote\":\"") << lastOpenNote
           << QStringLiteral("\"}\n");
    return true;
}

} // namespace

void TestPerformanceOps::init()
{
    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setEmitToStderr(false);
    log.setLevel(PerfLog::Major);
}

void TestPerformanceOps::loadSignalCount()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeace();
    DocumentSerializer serializer;
    BlockModel model;
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    serializer.loadIntoModel(&model, fixture.markdown);

    int ordinalSignals = 0;
    for (const auto &emission : dataSpy) {
        const auto roles = emission.at(2).value<QVector<int>>();
        if (roles.contains(BlockModel::OrdinalRole))
            ++ordinalSignals;
    }

    QCOMPARE(model.count(), fixture.blocks);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(insertSpy.count(), 0);
    QCOMPARE(ordinalSignals, 1);
    qInfo("PERF load.signal_count WP: ordinal dataChanged=%d rowsInserted=%d modelReset=%d",
          ordinalSignals, int(insertSpy.count()), int(resetSpy.count()));
}

void TestPerformanceOps::fullDocumentCount()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeace();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    DocumentStats stats;
    stats.setModel(&model);

    QVariantMap result;
    {
        PerfLog::ScopedTimer scope(
            QStringLiteral("statusbar.count"),
            QVariantMap{{QStringLiteral("blocks"), model.count()}});
        result = stats.documentStats();
        scope.addContext(QStringLiteral("words"),
                         result.value(QStringLiteral("words")).toInt());
    }

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("statusbar.count"));
    QCOMPARE(samples.size(), 1);
    QCOMPARE(result.value(QStringLiteral("words")).toInt(), fixture.words);
    const double ms = samples.first().durationMs;
    const QByteArray message =
        QStringLiteral("statusbar.count WP took %1 ms").arg(ms).toUtf8();
    QVERIFY2(ms <= 50.0, message.constData());
    qInfo("PERF statusbar.count WP: %.2f ms", samples.first().durationMs);
}

void TestPerformanceOps::incrementalTotalUpdate()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeace();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    const int index = model.count() / 2;
    const QString oldContent = model.getContent(index);
    QElapsedTimer timer;
    timer.start();
    model.updateContent(index, oldContent + QStringLiteral(" typed"));
    const double ms = timer.nsecsElapsed() / 1000000.0;

    QCOMPARE(model.documentWordCount(), fixture.words + 1);
    const QByteArray message =
        QStringLiteral("incremental total update took %1 ms").arg(ms).toUtf8();
    QVERIFY2(ms <= 2.0, message.constData());
    qInfo("PERF block.incremental_count_update WP: %.2f ms", ms);
}

void TestPerformanceOps::mixed100KeystrokeLatency()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::mixed100();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    QElapsedTimer timer;
    timer.start();
    model.updateContent(0, model.getContent(0) + QStringLiteral(" typed"));
    const double ms = timer.nsecsElapsed() / 1000000.0;

    const QByteArray message =
        QStringLiteral("MIXED-100 content update took %1 ms").arg(ms).toUtf8();
    QVERIFY2(ms <= 16.0, message.constData());
    qInfo("PERF keystroke MIXED-100: %.2f ms", ms);
}

void TestPerformanceOps::collectionScanVault10K()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(PerfCorpus::writeVault10K(dir.path()), 10003);

    NoteCollection collection;
    QVERIFY(collection.openRoot(dir.path()));
    QCOMPARE(collection.noteCount(), 10003);

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("startup.scan"));
    QVERIFY(!samples.isEmpty());
    qInfo("PERF startup.scan VAULT-10K: %.2f ms",
          samples.last().durationMs);
}

void TestPerformanceOps::collectionAsyncOpenVault10K()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(PerfCorpus::writeVault10K(dir.path()), 10003);

    QStringList parsed;
    NoteCollection collection;
    collection.setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QSignalSpy revisionSpy(&collection, &NoteCollection::revisionChanged);

    QElapsedTimer timer;
    timer.start();
    QVERIFY(collection.openRootAsync(dir.path()));
    const double ms = timer.nsecsElapsed() / 1000000.0;

    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(collection.noteCount(), 0);
    QVERIFY(collection.scanInProgress());
    QCOMPARE(parsed.size(), 0);

    QVERIFY(revisionSpy.wait(5000));
    QCOMPARE(collection.noteCount(), 10003);
    QCOMPARE(parsed.size(), 0);

    const NoteCollection::NoteEntry *entry =
        collection.note(QStringLiteral("Folder_00/Sub_00/Note_00000.md"));
    QVERIFY(entry);
    QCOMPARE(entry->title, QStringLiteral("Note_00000"));
    QCOMPARE(entry->wordCount, 0);
    QCOMPARE(entry->fileSize, qint64(-1));

    const QByteArray message =
        QStringLiteral("openRootAsync cold VAULT-10K returned in %1 ms")
            .arg(ms)
            .toUtf8();
    QVERIFY2(ms <= 1000.0, message.constData());
    qInfo("PERF startup.scan VAULT-10K async return: %.2f ms", ms);
}

void TestPerformanceOps::collectionWarmStartVault10K()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(PerfCorpus::writeVault10K(dir.path()), 10003);

    {
        NoteCollection cold;
        QVERIFY(cold.openRoot(dir.path()));
        QCOMPARE(cold.noteCount(), 10003);
    }

    QStringList parsed;
    NoteCollection warm;
    warm.setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });

    PerfLog::instance().clear();
    QElapsedTimer timer;
    timer.start();
    QVERIFY(warm.openRoot(dir.path()));
    const double elapsedMs = timer.nsecsElapsed() / 1000000.0;

    QCOMPARE(warm.noteCount(), 10003);
    QCOMPARE(parsed.size(), 0);

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("startup.scan"));
    QVERIFY(!samples.isEmpty());
    const double loggedMs = samples.last().durationMs;
    const QByteArray message =
        QStringLiteral("warm startup.scan VAULT-10K took %1 ms")
            .arg(loggedMs)
            .toUtf8();
    QVERIFY2(loggedMs <= 300.0, message.constData());
    qInfo("PERF startup.scan VAULT-10K warm: %.2f ms (wall %.2f ms)",
          loggedMs, elapsedMs);
}

void TestPerformanceOps::collectionAsyncOpenSmallVaultHugeNote()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PerfCorpus::DocumentFixture wp = PerfCorpus::warAndPeace();
    const QString huge =
        wp.markdown + QStringLiteral("\n\n") + wp.markdown;
    QCOMPARE(PerfCorpus::writeSingleNote(dir.path(),
                                         QStringLiteral("Huge.md"),
                                         huge),
             1);
    QCOMPARE(PerfCorpus::writeSingleNote(dir.path(),
                                         QStringLiteral("Small.md"),
                                         QStringLiteral("one small note\n")),
             1);

    QStringList parsed;
    NoteCollection collection;
    collection.setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QSignalSpy revisionSpy(&collection, &NoteCollection::revisionChanged);

    QElapsedTimer timer;
    timer.start();
    QVERIFY(collection.openRootAsync(dir.path()));
    const double ms = timer.nsecsElapsed() / 1000000.0;

    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(collection.noteCount(), 0);
    QVERIFY(collection.scanInProgress());
    QCOMPARE(parsed.size(), 0);

    QVERIFY(revisionSpy.wait(5000));
    QCOMPARE(collection.noteCount(), 2);
    QCOMPARE(parsed.size(), 0);

    const NoteCollection::NoteEntry *hugeEntry =
        collection.note(QStringLiteral("Huge.md"));
    QVERIFY(hugeEntry);
    QCOMPARE(hugeEntry->title, QStringLiteral("Huge"));
    QCOMPARE(hugeEntry->wordCount, 0);
    QCOMPARE(hugeEntry->fileSize, qint64(-1));

    const QByteArray message =
        QStringLiteral("openRootAsync small vault/huge note returned in %1 ms")
            .arg(ms)
            .toUtf8();
    QVERIFY2(ms <= 300.0, message.constData());
    qInfo("PERF startup.scan small-vault-huge-note async return: %.2f ms",
          ms);
}

void TestPerformanceOps::collectionAsyncRefreshDirectoryHugeNote()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PerfCorpus::DocumentFixture wp = PerfCorpus::warAndPeace();
    const QString huge =
        wp.markdown + QStringLiteral("\n\n") + wp.markdown;
    QCOMPARE(PerfCorpus::writeSingleNote(dir.path(),
                                         QStringLiteral("Big/Huge.md"),
                                         huge),
             1);
    QCOMPARE(PerfCorpus::writeSingleNote(dir.path(),
                                         QStringLiteral("Big/Small.md"),
                                         QStringLiteral("one small note\n")),
             1);

    NoteCollection collection;
    QVERIFY(collection.openRoot(dir.path()));
    QCOMPARE(collection.noteCount(), 2);

    QStringList parsed;
    collection.setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });
    QSignalSpy revisionSpy(&collection, &NoteCollection::revisionChanged);

    QCOMPARE(PerfCorpus::writeSingleNote(
                 dir.path(),
                 QStringLiteral("Big/Huge.md"),
                 huge + QStringLiteral("\n\nfresh external words\n")),
             1);

    QElapsedTimer timer;
    timer.start();
    collection.refreshPaths(
        QStringList{QDir(dir.path()).filePath(QStringLiteral("Big"))});
    const double ms = timer.nsecsElapsed() / 1000000.0;

    QCOMPARE(parsed.size(), 0);
    QCOMPARE(revisionSpy.count(), 0);
    const QByteArray message =
        QStringLiteral("directory refresh with huge note returned in %1 ms")
            .arg(ms)
            .toUtf8();
    QVERIFY2(ms <= 50.0, message.constData());

    QVERIFY(revisionSpy.wait(10000));
    QTRY_VERIFY_WITH_TIMEOUT(parsed.contains(QStringLiteral("Big/Huge.md")),
                             10000);
    QCOMPARE(collection.noteCount(), 2);
    QVERIFY(collection.note(QStringLiteral("Big/Huge.md"))->wordCount
            > wp.words);
    qInfo("PERF collection.refresh directory huge-note async return: %.2f ms",
          ms);
}

void TestPerformanceOps::startupControllerRememberedHugeNoteStartsAsync()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PerfCorpus::DocumentFixture wp = PerfCorpus::warAndPeace();
    const QString huge =
        wp.markdown + QStringLiteral("\n\n") + wp.markdown;
    QCOMPARE(PerfCorpus::writeSingleNote(dir.path(),
                                         QStringLiteral("Huge.md"),
                                         huge),
             1);
    QCOMPARE(PerfCorpus::writeSingleNote(dir.path(),
                                         QStringLiteral("Small.md"),
                                         QStringLiteral("# Small\n\none small note\n")),
             1);
    QVERIFY(writeCollectionState(dir.path(), QStringLiteral("Huge.md")));

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

    QElapsedTimer timer;
    timer.start();
    controller.start();
    const double startMs = timer.nsecsElapsed() / 1000000.0;

    QVERIFY(controller.started());
    QVERIFY(!controller.finished());
    QVERIFY(documentManager.openInProgress());
    QVERIFY(collection.scanInProgress());
    QCOMPARE(parsed.size(), 0);

    const QByteArray message =
        QStringLiteral("StartupController::start remembered huge note took %1 ms")
            .arg(startMs)
            .toUtf8();
    QVERIFY2(startMs <= 50.0, message.constData());

    QTRY_VERIFY_WITH_TIMEOUT(controller.finished(), 10000);
    QCOMPARE(documentManager.currentFilePath(),
             collection.absolutePath(QStringLiteral("Huge.md")));
    QCOMPARE(collection.lastOpenNote(), QStringLiteral("Huge.md"));
    QVERIFY(blockModel.count() > wp.blocks);
    QVERIFY(!documentManager.openInProgress());

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("startup.initial_open"));
    QVERIFY(!samples.isEmpty());
    const QList<PerfLog::Sample> workerSamples =
        PerfLog::instance().samples(QStringLiteral("note.open.worker"));
    const QList<PerfLog::Sample> applySamples =
        PerfLog::instance().samples(QStringLiteral("note.open.apply"));
    QVERIFY(!workerSamples.isEmpty());
    QVERIFY(!applySamples.isEmpty());
    qInfo("PERF startup.controller remembered huge-note start return: %.2f ms",
          startMs);
    qInfo("PERF startup.initial_open remembered huge-note async total: %.2f ms",
          samples.last().durationMs);
    qInfo("PERF note.open.worker remembered huge-note: %.2f ms",
          workerSamples.last().durationMs);
    qInfo("PERF note.open.apply remembered huge-note: %.2f ms",
          applySamples.last().durationMs);
}

void TestPerformanceOps::persistenceTimingSplits()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeace();
    DocumentSerializer serializer;
    UndoStack undoStack;
    BlockModel model;
    model.setUndoStack(&undoStack);
    serializer.loadIntoModel(&model, fixture.markdown);

    DocumentManager manager;
    manager.setBlockModel(&model);
    manager.setUndoStack(&undoStack);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString filePath = QDir(dir.path()).filePath(QStringLiteral("wp.md"));

    QVERIFY(manager.saveAs(QUrl::fromLocalFile(filePath)));

    model.updateContent(0, model.getContent(0) + QStringLiteral(" manual"));

    PerfLog::instance().clear();
    QVERIFY(manager.saveAsync());
    QTRY_VERIFY_WITH_TIMEOUT(
        !PerfLog::instance()
             .samples(QStringLiteral("note.save.commit"))
             .isEmpty(),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isDirty(), 5000);

    const auto saveMain =
        PerfLog::instance().samples(QStringLiteral("note.save"));
    const auto saveSerialize =
        PerfLog::instance().samples(QStringLiteral("note.save.serialize"));
    const auto saveWrite =
        PerfLog::instance().samples(QStringLiteral("note.save.write"));
    const auto saveFlush =
        PerfLog::instance().samples(QStringLiteral("note.save.flush"));
    const auto saveCommit =
        PerfLog::instance().samples(QStringLiteral("note.save.commit"));
    QVERIFY(!saveMain.isEmpty());
    QVERIFY(!saveSerialize.isEmpty());
    QVERIFY(!saveWrite.isEmpty());
    QVERIFY(!saveFlush.isEmpty());
    QVERIFY(!saveCommit.isEmpty());
    QVERIFY(saveMain.last().durationMs <= 5.0);
    QVERIFY(saveCommit.last().context.value(QStringLiteral("ok")).toBool());
    qInfo("PERF note.save main-thread WP: %.2f ms",
          saveMain.last().durationMs);
    qInfo("PERF note.save.serialize WP: %.2f ms",
          saveSerialize.last().durationMs);
    qInfo("PERF note.save.write WP worker: %.2f ms",
          saveWrite.last().durationMs);
    qInfo("PERF note.save.flush WP worker: %.2f ms",
          saveFlush.last().durationMs);
    qInfo("PERF note.save.commit WP worker: %.2f ms",
          saveCommit.last().durationMs);

    model.updateContent(0, model.getContent(0) + QStringLiteral(" autosave"));

    PerfLog::instance().clear();
    QVERIFY(QMetaObject::invokeMethod(&manager, "onAutoSaveTimer",
                                      Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(
        !PerfLog::instance()
             .samples(QStringLiteral("note.autosave.commit"))
             .isEmpty(),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(!manager.isDirty(), 5000);

    const auto autosaveMain =
        PerfLog::instance().samples(QStringLiteral("note.autosave"));
    const auto autosaveSerialize = PerfLog::instance().samples(
        QStringLiteral("note.autosave.serialize"));
    const auto autosaveWrite =
        PerfLog::instance().samples(QStringLiteral("note.autosave.write"));
    const auto autosaveFlush =
        PerfLog::instance().samples(QStringLiteral("note.autosave.flush"));
    const auto autosaveCommit =
        PerfLog::instance().samples(QStringLiteral("note.autosave.commit"));
    QVERIFY(!autosaveMain.isEmpty());
    QVERIFY(!autosaveSerialize.isEmpty());
    QVERIFY(!autosaveWrite.isEmpty());
    QVERIFY(!autosaveFlush.isEmpty());
    QVERIFY(!autosaveCommit.isEmpty());
    QVERIFY(autosaveMain.last().durationMs <= 5.0);
    QVERIFY(autosaveCommit.last().context.value(QStringLiteral("ok")).toBool());
    qInfo("PERF note.autosave main-thread WP: %.2f ms",
          autosaveMain.last().durationMs);
    qInfo("PERF note.autosave.serialize WP: %.2f ms",
          autosaveSerialize.last().durationMs);
    qInfo("PERF note.autosave.write WP worker: %.2f ms",
          autosaveWrite.last().durationMs);
    qInfo("PERF note.autosave.flush WP worker: %.2f ms",
          autosaveFlush.last().durationMs);
    qInfo("PERF note.autosave.commit WP worker: %.2f ms",
          autosaveCommit.last().durationMs);

    const QString journalPath =
        QDir(dir.path()).filePath(QStringLiteral("wp.journal"));
    manager.setJournalPath(journalPath);
    model.updateContent(0, model.getContent(0) + QStringLiteral(" dirty"));

    PerfLog::instance().clear();
    QVERIFY(QMetaObject::invokeMethod(&manager, "writeJournal",
                                      Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(
        !PerfLog::instance()
             .samples(QStringLiteral("note.journal_write.commit"))
             .isEmpty(),
        5000);

    const auto journalMain =
        PerfLog::instance().samples(QStringLiteral("note.journal_write"));
    const auto journalSerialize = PerfLog::instance().samples(
        QStringLiteral("note.journal_write.serialize"));
    const auto journalWrite = PerfLog::instance().samples(
        QStringLiteral("note.journal_write.write"));
    const auto journalFlush = PerfLog::instance().samples(
        QStringLiteral("note.journal_write.flush"));
    const auto journalCommit = PerfLog::instance().samples(
        QStringLiteral("note.journal_write.commit"));
    QVERIFY(!journalMain.isEmpty());
    QVERIFY(!journalSerialize.isEmpty());
    QVERIFY(!journalWrite.isEmpty());
    QVERIFY(!journalFlush.isEmpty());
    QVERIFY(!journalCommit.isEmpty());
    QVERIFY(journalMain.last().durationMs <= 2.0);
    QVERIFY(journalCommit.last().context.value(QStringLiteral("ok")).toBool());
    qInfo("PERF note.journal_write main-thread WP: %.2f ms",
          journalMain.last().durationMs);
    qInfo("PERF note.journal_write.serialize WP: %.2f ms",
          journalSerialize.last().durationMs);
    qInfo("PERF note.journal_write.write WP: %.2f ms",
          journalWrite.last().durationMs);
    qInfo("PERF note.journal_write.flush WP: %.2f ms",
          journalFlush.last().durationMs);
    qInfo("PERF note.journal_write.commit WP: %.2f ms",
          journalCommit.last().durationMs);
}

void TestPerformanceOps::documentSearchRecompute()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeace();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    DocumentSearch search;
    search.setModel(&model);
    PerfLog::instance().clear();
    search.setActive(true);
    search.setQuery(QStringLiteral("Prince"));

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("search.doc_recompute"));
    QVERIFY(!samples.isEmpty());
    QVERIFY(search.matchCount() > 0);
    const double ms = samples.last().durationMs;
    const QByteArray message =
        QStringLiteral("search.doc_recompute WP took %1 ms").arg(ms).toUtf8();
    QVERIFY2(ms <= 16.0, message.constData());
    qInfo("PERF search.doc_recompute WP: %.2f ms, matches=%d",
          ms, search.matchCount());
}

void TestPerformanceOps::documentSearchHighMatchRecompute()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeace();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    DocumentSearch search;
    search.setModel(&model);
    PerfLog::instance().clear();
    search.setActive(true);
    search.setQuery(QStringLiteral("e"));

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("search.doc_recompute"));
    QVERIFY(!samples.isEmpty());
    QVERIFY(search.matchCount() > 100000);
    const double ms = samples.last().durationMs;
    const QByteArray message =
        QStringLiteral("high-match search.doc_recompute WP took %1 ms with %2 matches")
            .arg(ms)
            .arg(search.matchCount())
            .toUtf8();
    QVERIFY2(ms <= 100.0, message.constData());
    qInfo("PERF search.doc_recompute WP high-match: %.2f ms, matches=%d",
          ms, search.matchCount());
}

void TestPerformanceOps::outlineRebuild()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::headings2K();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    PerfLog::instance().clear();
    DocumentOutline outline;
    outline.setModel(&model);

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("outline.rebuild"));
    QVERIFY(!samples.isEmpty());
    QCOMPARE(outline.headings().size(), 2000);
    qInfo("PERF outline.rebuild HEADINGS-2K: %.2f ms",
          samples.last().durationMs);
}

void TestPerformanceOps::outlineNonHeadingEditDoesNotRebuild()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::headings2K();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    DocumentOutline outline;
    outline.setModel(&model);
    QSignalSpy resetSpy(&outline, &QAbstractItemModel::modelReset);
    const int revision = outline.revision();

    PerfLog::instance().clear();
    QElapsedTimer timer;
    timer.start();
    model.updateContent(1, model.getContent(1) + QStringLiteral(" typed"));
    QCoreApplication::processEvents();
    const double ms = timer.nsecsElapsed() / 1000000.0;

    QCOMPARE(outline.revision(), revision);
    QCOMPARE(resetSpy.count(), 0);
    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("outline.rebuild"));
    QCOMPARE(samples.size(), 0);
    const QByteArray message =
        QStringLiteral("HEADINGS-2K non-heading edit took %1 ms").arg(ms).toUtf8();
    QVERIFY2(ms <= 16.0, message.constData());
    qInfo("PERF outline.non_heading_edit HEADINGS-2K: %.2f ms, rebuilds=%d",
          ms, int(samples.size()));
}

void TestPerformanceOps::bulkDelete()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeaceSynth();
    DocumentSerializer serializer;
    UndoStack stack;
    BlockModel model;
    model.setUndoStack(&stack);
    serializer.loadIntoModel(&model, fixture.markdown);

    QVariantList indexes;
    indexes.reserve(model.count());
    for (int i = 0; i < model.count(); ++i)
        indexes.append(i);

    PerfLog::instance().clear();
    model.removeBlocks(indexes);

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("block.bulk_delete"));
    QCOMPARE(samples.size(), 1);
    QCOMPARE(model.count(), 1);
    qInfo("PERF block.bulk_delete WP-SYNTH: %.2f ms",
          samples.first().durationMs);
}

void TestPerformanceOps::selectionPrune()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeaceSynth();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    DocumentSelection selection;
    selection.setModel(&model);
    selection.selectAllBlocks();

    PerfLog::instance().clear();
    model.removeBlockInternal(0);

    const QList<PerfLog::Sample> samples =
        PerfLog::instance().samples(QStringLiteral("selection.prune"));
    QVERIFY(!samples.isEmpty());
    qInfo("PERF selection.prune WP-SYNTH: %.2f ms",
          samples.last().durationMs);
}

// Phase 6 gate: id lookups through the map stay effectively constant.
// 10,000 lookups of edge and middle ids on WP-SYNTH must complete far
// below what 10,000 linear scans would take (~6M block visits).
void TestPerformanceOps::selectionIdLookup()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeaceSynth();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    const QString firstId = model.blockAt(0)->blockId();
    const QString middleId = model.blockAt(model.count() / 2)->blockId();
    const QString lastId = model.blockAt(model.count() - 1)->blockId();

    // Warm the lazily built map, then time steady-state lookups.
    QCOMPARE(model.indexOfBlockId(firstId), 0);

    QElapsedTimer timer;
    timer.start();
    int checksum = 0;
    for (int i = 0; i < 10000; ++i) {
        checksum += model.indexOfBlockId(firstId);
        checksum += model.indexOfBlockId(middleId);
        checksum += model.indexOfBlockId(lastId);
    }
    const double ms = timer.nsecsElapsed() / 1000000.0;

    QCOMPARE(checksum,
             10000 * (0 + model.count() / 2 + model.count() - 1));
    const QByteArray message =
        QStringLiteral("30,000 id lookups took %1 ms").arg(ms).toUtf8();
    QVERIFY2(ms <= 50.0, message.constData());
    qInfo("PERF selection.id_lookup WP-SYNTH: %.2f ms for 30,000 lookups", ms);
}

// Phase 6 gate: select-all + delete on WP-SYNTH with a live selection
// and undo stack attached completes within 200 ms (from multi-second).
void TestPerformanceOps::selectAllDelete()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeaceSynth();
    DocumentSerializer serializer;
    UndoStack stack;
    BlockModel model;
    model.setUndoStack(&stack);
    serializer.loadIntoModel(&model, fixture.markdown);

    DocumentSelection selection;
    selection.setModel(&model);

    QElapsedTimer timer;
    timer.start();
    selection.selectAllBlocks();
    const QVariantList indexes = selection.selectedIndexes();
    model.removeBlocks(indexes);
    const double ms = timer.nsecsElapsed() / 1000000.0;

    QCOMPARE(indexes.size(), fixture.blocks);
    QCOMPARE(model.count(), 1);
    QVERIFY(!selection.hasBlockSelection());
    const QByteArray message =
        QStringLiteral("select-all + delete took %1 ms").arg(ms).toUtf8();
    QVERIFY2(ms <= 200.0, message.constData());
    qInfo("PERF selection.select_all_delete WP-SYNTH: %.2f ms for %d blocks",
          ms, int(indexes.size()));

    // The whole delete stays one undoable step set that restores the doc.
    stack.undo(); // the trailing empty-paragraph insert + removals macro
    QCOMPARE(model.count(), fixture.blocks);
}

// Phase 6 gate: a selection-drag tick — extend the text-selection head
// and resolve the visible rows' portions — costs at most 2 ms per
// revision bump on WP-SYNTH.
void TestPerformanceOps::selectionDragTick()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeaceSynth();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    DocumentSelection selection;
    selection.setModel(&model);
    selection.beginTextSelection(10, 0,
                                 DocumentSelection::CharacterGranularity);

    const int visibleRows = 30;
    double worstMs = 0.0;
    // Sweep the head deep into the document, resolving a screenful of
    // portions per tick like the delegates would.
    for (int head = 500; head <= 5500; head += 500) {
        QElapsedTimer timer;
        timer.start();
        selection.updateTextSelectionHead(head, 3);
        int selectedCount = 0;
        for (int row = head - visibleRows; row < head; ++row) {
            const QVariantMap portion = selection.portionForBlock(row);
            if (portion.value(QStringLiteral("selected")).toBool())
                ++selectedCount;
        }
        const double ms = timer.nsecsElapsed() / 1000000.0;
        worstMs = qMax(worstMs, ms);
        QCOMPARE(selectedCount, visibleRows);
    }

    const QByteArray message =
        QStringLiteral("worst drag tick took %1 ms").arg(worstMs).toUtf8();
    QVERIFY2(worstMs <= 2.0, message.constData());
    qInfo("PERF selection.drag_tick WP-SYNTH: worst %.3f ms per tick", worstMs);
}

// Phase 7 gate (A7): rendering every ordinal on LIST-5K after a
// structural change costs one linear cache rebuild plus O(1) reads —
// not a backward run scan per row (which is quadratic on one long run).
void TestPerformanceOps::ordinalSweepList5K()
{
    const PerfCorpus::DocumentFixture fixture = PerfCorpus::list5K();
    DocumentSerializer serializer;
    BlockModel model;
    serializer.loadIntoModel(&model, fixture.markdown);

    // A structural change invalidates the cache; the sweep pays one
    // rebuild and then O(1) per row, like delegates rendering OrdinalRole.
    model.insertBlock(0, Block::Paragraph, "breaker");

    QElapsedTimer timer;
    timer.start();
    qint64 checksum = 0;
    for (int i = 0; i < model.count(); ++i)
        checksum += model.ordinalAt(i);
    const double ms = timer.nsecsElapsed() / 1000000.0;

    QVERIFY(checksum > 0);
    const QByteArray message =
        QStringLiteral("LIST-5K ordinal sweep took %1 ms").arg(ms).toUtf8();
    QVERIFY2(ms <= 16.0, message.constData());
    qInfo("PERF block.ordinal_sweep LIST-5K: %.2f ms for %d rows",
          ms, int(model.count()));
}

QTEST_MAIN(TestPerformanceOps)
#include "test_performance_ops.moc"
