// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "blockmodel.h"
#include "documentmanager.h"
#include "notecollection.h"
#include "perflog.h"
#include "perf/corpus.h"
#include "startupcontroller.h"
#include "testsetup.h"
#include "undostack.h"

#include <QQmlApplicationEngine>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QQuickWindow>
#include <QMultiMap>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

#include <algorithm>
#include <functional>

#include "timingbudget.h"

class TestPerformanceApp : public QObject
{
    Q_OBJECT

private slots:
    void guiStartupSamples();
    void guiColdStartObjectCount();
    void guiScrollWarAndPeaceFrameSample();
    void guiCollectionStartupDeferredVault10K();
    void guiCollectionStartupDeferredSmallVaultHugeNote();
    void guiCollectionPersistenceLiveSizedNote();
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

// Every QObject reachable from `root`, itself included. The QML object tree
// is a QObject parent tree, so this counts what the shell actually built.
int objectTreeSize(const QObject *root)
{
    if (!root)
        return 0;
    int count = 1;
    for (const QObject *child : root->children())
        count += objectTreeSize(child);
    return count;
}

// Peak resident set size in kilobytes, or -1 where the platform does not
// report it. Peak rather than current, because what matters is the high-water
// mark a cold start reaches.
qint64 peakResidentKb()
{
#if defined(Q_OS_LINUX)
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;
    // readAll() rather than atEnd()/readLine(): a /proc file reports size 0,
    // so atEnd() is true before a byte has been read.
    const QList<QByteArray> lines = status.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (!line.startsWith("VmHWM:"))
            continue;
        const QList<QByteArray> parts = line.simplified().split(' ');
        if (parts.size() >= 2)
            return parts.at(1).toLongLong();
    }
#endif
    return -1;
}

} // namespace

void TestPerformanceApp::guiStartupSamples()
{
    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setEmitToStderr(false);
    log.setLevel(PerfLog::Major);

    QQmlApplicationEngine engine;
    Setup setup;
    setup.qmlEngineAvailable(&engine);

    DocumentManager *manager = setup.context()->documentManager();
    QVERIFY(manager);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString notePath = dir.filePath(QStringLiteral("wp.md"));
    QFile file(notePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << PerfCorpus::warAndPeace().markdown;
    file.close();

    QElapsedTimer startup;
    startup.start();
    QVERIFY(manager->open(QUrl::fromLocalFile(notePath)));

    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty())
        QSKIP("QML window did not load in this environment");

    QQuickWindow *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        QSKIP("Root QML object is not a QQuickWindow");

    window->show();
    if (!QTest::qWaitForWindowExposed(window, 5000))
        QSKIP("No exposed Qt Quick window available for GUI performance sample");

    log.mark(QStringLiteral("startup.first_frame"),
             startup.elapsed(),
             QVariantMap{{QStringLiteral("source"), QStringLiteral("test")}});

    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("statusbar.count")).isEmpty(), 5000);

    QVERIFY(!log.samples(QStringLiteral("note.open")).isEmpty());
    QVERIFY(!log.samples(QStringLiteral("startup.first_frame")).isEmpty());
    qInfo("PERF GUI note.open: %.2f ms",
          log.samples(QStringLiteral("note.open")).last().durationMs);
    qInfo("PERF GUI startup.first_frame: %.2f ms",
          log.samples(QStringLiteral("startup.first_frame")).last().durationMs);
    qInfo("PERF GUI statusbar.count: %.2f ms",
          log.samples(QStringLiteral("statusbar.count")).last().durationMs);
}

// What an empty document costs before the user has done anything: how many
// QObjects the shell built, and the peak memory reaching that point.
//
// This is the measurement the audit's cold-start finding asked for. main.qml
// creates most dialogs, popups and side panels eagerly rather than on first
// use, and the question was whether that is worth changing. A number here
// makes the answer checkable rather than arguable, and pins it so a new
// eagerly-created dialog shows up as a step change instead of drifting in
// unnoticed.
void TestPerformanceApp::guiColdStartObjectCount()
{
    QQmlApplicationEngine engine;
    Setup setup;
    setup.qmlEngineAvailable(&engine);

    QElapsedTimer startup;
    startup.start();
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty())
        QSKIP("QML window did not load in this environment");

    QQuickWindow *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        QSKIP("Root QML object is not a QQuickWindow");

    window->show();
    if (!QTest::qWaitForWindowExposed(window, 5000))
        QSKIP("No exposed Qt Quick window available for GUI performance sample");
    const double firstFrameMs = startup.elapsed();

    const int objects = objectTreeSize(window);
    const qint64 rssKb = peakResidentKb();

    // Where they are, so the answer to "is eager creation worth changing?"
    // names the specific things worth changing.
    if (qEnvironmentVariableIsSet("KVIT_COLD_START_BREAKDOWN")) {
        QMultiMap<int, QString> bySize;
        const QObject *contentItem = window->findChild<QObject *>(
            QString(), Qt::FindDirectChildrenOnly);
        Q_UNUSED(contentItem);
        std::function<void(const QObject *, int)> walk =
            [&](const QObject *node, int depth) {
                for (const QObject *child : node->children()) {
                    const int size = objectTreeSize(child);
                    if (size >= 40 && depth < 3) {
                        const QString name = child->objectName().isEmpty()
                            ? QString::fromLatin1(child->metaObject()->className())
                            : child->objectName() + QStringLiteral(" [")
                                  + QString::fromLatin1(
                                        child->metaObject()->className())
                                  + QStringLiteral("]");
                        bySize.insert(size, QString(depth * 2, QLatin1Char(' '))
                                                + name);
                    }
                    walk(child, depth + 1);
                }
            };
        walk(window, 0);
        auto it = bySize.constEnd();
        int shown = 0;
        while (it != bySize.constBegin() && shown < 30) {
            --it;
            qInfo("PERF GUI cold_start.subtree %5d  %s", it.key(),
                  qPrintable(it.value()));
            ++shown;
        }
    }

    qInfo("PERF GUI cold_start.objects: %d", objects);
    qInfo("PERF GUI cold_start.peak_rss: %lld KiB", rssKb);
    qInfo("PERF GUI cold_start.first_frame: %.2f ms", firstFrameMs);

    // A ceiling, not a target. Measured at about 4,360 objects on an empty
    // document, down from 5,750 before the context menus, session dialogs
    // and insertion pickers were moved behind Loaders. The bound sits above
    // the current figure so ordinary shell work does not trip it, and below
    // where it started so another set of eagerly-built dialogs would.
    QVERIFY2(objects < 5200,
             qPrintable(QStringLiteral(
                 "cold start built %1 QObjects, which is more than the shell "
                 "should need before the user has done anything. Something "
                 "was probably added to main.qml eagerly that belongs behind "
                 "a Loader.").arg(objects)));
}

void TestPerformanceApp::guiScrollWarAndPeaceFrameSample()
{
    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setEmitToStderr(false);
    log.setLevel(PerfLog::Major);

    QQmlApplicationEngine engine;
    Setup setup;
    setup.qmlEngineAvailable(&engine);

    DocumentManager *manager = setup.context()->documentManager();
    QVERIFY(manager);

    const PerfCorpus::DocumentFixture fixture = PerfCorpus::warAndPeace();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString notePath = dir.filePath(QStringLiteral("wp.md"));
    QFile file(notePath);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << fixture.markdown;
    file.close();

    QVERIFY(manager->open(QUrl::fromLocalFile(notePath)));

    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty())
        QSKIP("QML window did not load in this environment");

    QQuickWindow *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        QSKIP("Root QML object is not a QQuickWindow");

    window->show();
    if (!QTest::qWaitForWindowExposed(window, 5000))
        QSKIP("No exposed Qt Quick window available for GUI performance sample");

    QObject *listObj = window->findChild<QObject *>(QStringLiteral("blockListView"));
    QVERIFY(listObj);
    QTRY_COMPARE_WITH_TIMEOUT(listObj->property("count").toInt(),
                              fixture.blocks, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        listObj->property("contentHeight").toDouble()
            > listObj->property("height").toDouble(),
        5000);

    log.clear();
    QList<double> frames;
    QElapsedTimer frameTimer;
    frameTimer.start();
    const QMetaObject::Connection frameConnection =
        QObject::connect(window, &QQuickWindow::afterFrameEnd, window, [&]() {
            const double frameMs = double(frameTimer.nsecsElapsed()) / 1000000.0;
            frameTimer.restart();
            frames.append(frameMs);
            log.record(QStringLiteral("frame"),
                       frameMs,
                       QVariantMap{{QStringLiteral("source"),
                                    QStringLiteral("scroll-test")},
                                   {QStringLiteral("fixture"),
                                    QStringLiteral("WP")},
                                   {QStringLiteral("blocks"), fixture.blocks}});
        });

    const double maxY =
        listObj->property("contentHeight").toDouble()
        - listObj->property("height").toDouble();
    QVERIFY(maxY > 0.0);

    constexpr int steps = 80;
    QElapsedTimer scrollTimer;
    scrollTimer.start();
    for (int i = 1; i <= steps; ++i) {
        listObj->setProperty("contentY", maxY * double(i) / double(steps));
        window->requestUpdate();
        QTest::qWait(8);
    }
    QTRY_VERIFY_WITH_TIMEOUT(frames.size() >= 5, 3000);
    QObject::disconnect(frameConnection);

    QList<double> measured = frames.size() > 2 ? frames.mid(2) : frames;
    QVERIFY(!measured.isEmpty());
    std::sort(measured.begin(), measured.end());
    double sum = 0.0;
    for (double frame : measured)
        sum += frame;
    const double avg = sum / double(measured.size());
    const double p95 = measured.at(qMin(measured.size() - 1,
                                        int(measured.size() * 95 / 100)));
    const double maxFrame = measured.last();

    QVERIFY(!log.samples(QStringLiteral("frame")).isEmpty());
    QVERIFY2(maxFrame < 500.0,
             QStringLiteral("scroll frame sample had a pathological stall: %1 ms")
                 .arg(maxFrame)
                 .toUtf8()
                 .constData());

    qInfo("PERF GUI scroll WP frames: count=%d avg=%.2f ms p95=%.2f ms max=%.2f ms total=%.2f ms",
          int(measured.size()),
          avg,
          p95,
          maxFrame,
          double(scrollTimer.nsecsElapsed()) / 1000000.0);
    qInfo("PERF GUI scroll WP cacheBuffer: %.2f px",
          listObj->property("cacheBuffer").toDouble());
}

void TestPerformanceApp::guiCollectionStartupDeferredVault10K()
{
    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setEmitToStderr(false);
    log.setLevel(PerfLog::Major);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(PerfCorpus::writeVault10K(dir.path()), 10003);

    QQmlApplicationEngine engine;
    Setup setup;
    setup.qmlEngineAvailable(&engine);

    DocumentManager *manager = setup.context()->documentManager();
    QVERIFY(manager);

    BlockModel *model = setup.context()->blockModel();
    QVERIFY(model);

    UndoStack *undoStack = setup.context()->undoStack();
    QVERIFY(undoStack);

    NoteCollection *collection = setup.context()->noteCollection();
    QVERIFY(collection);

    QStringList parsed;
    collection->setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });

    // The composed context already owns a StartupController, wired to this
    // same collection, manager, model and undo stack, and already published
    // as the startupController context property. Building a second one here
    // left two of them driving one collection.
    StartupController *controller = setup.context()->startupController();
    QVERIFY(controller);
    controller->setRootPath(dir.path());

    QElapsedTimer startup;
    startup.start();
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty())
        QSKIP("QML window did not load in this environment");

    QQuickWindow *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        QSKIP("Root QML object is not a QQuickWindow");

    window->show();
    if (!QTest::qWaitForWindowExposed(window, 5000))
        QSKIP("No exposed Qt Quick window available for GUI performance sample");

    const double firstFrameMs = startup.elapsed();
    log.mark(QStringLiteral("startup.first_frame"),
             firstFrameMs,
             QVariantMap{{QStringLiteral("source"),
                          QStringLiteral("collection-test")},
                         {QStringLiteral("fixture"),
                          QStringLiteral("VAULT-10K")}});

    KVIT_ASSERT_WALL_BUDGET(firstFrameMs, "gui.first_frame before VAULT-10K scan", 1000.0);

    QMetaObject::invokeMethod(controller, "start", Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(controller->finished(), 5000);

    QVERIFY(collection->isOpen());
    QCOMPARE(collection->noteCount(), 10003);
    QVERIFY(collection->scanInProgress());
    QVERIFY(!manager->currentFilePath().isEmpty());
    QVERIFY(model->count() > 0);
    QVERIFY(!log.samples(QStringLiteral("startup.initial_open")).isEmpty());

    qInfo("PERF GUI startup.first_frame VAULT-10K deferred: %.2f ms",
          firstFrameMs);
    qInfo("PERF GUI startup.initial_open VAULT-10K deferred: %.2f ms",
          log.samples(QStringLiteral("startup.initial_open")).last().durationMs);
    qInfo("PERF GUI startup.scan VAULT-10K parsed-before-finish: %d",
          int(parsed.size()));
}

void TestPerformanceApp::guiCollectionStartupDeferredSmallVaultHugeNote()
{
    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setEmitToStderr(false);
    log.setLevel(PerfLog::Major);

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
    QVERIFY(writeCollectionState(dir.path(), QStringLiteral("Small.md")));

    QQmlApplicationEngine engine;
    Setup setup;
    setup.qmlEngineAvailable(&engine);

    DocumentManager *manager = setup.context()->documentManager();
    QVERIFY(manager);

    BlockModel *model = setup.context()->blockModel();
    QVERIFY(model);

    UndoStack *undoStack = setup.context()->undoStack();
    QVERIFY(undoStack);

    NoteCollection *collection = setup.context()->noteCollection();
    QVERIFY(collection);

    QStringList parsed;
    collection->setIndexParseObserverForTesting(
        [&parsed](const QString &relPath) { parsed.append(relPath); });

    // The composed context already owns a StartupController, wired to this
    // same collection, manager, model and undo stack, and already published
    // as the startupController context property. Building a second one here
    // left two of them driving one collection.
    StartupController *controller = setup.context()->startupController();
    QVERIFY(controller);
    controller->setRootPath(dir.path());

    QElapsedTimer startup;
    startup.start();
    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty())
        QSKIP("QML window did not load in this environment");

    QQuickWindow *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        QSKIP("Root QML object is not a QQuickWindow");

    window->show();
    if (!QTest::qWaitForWindowExposed(window, 5000))
        QSKIP("No exposed Qt Quick window available for GUI performance sample");

    const double firstFrameMs = startup.elapsed();
    log.mark(QStringLiteral("startup.first_frame"),
             firstFrameMs,
             QVariantMap{{QStringLiteral("source"),
                          QStringLiteral("collection-test")},
                         {QStringLiteral("fixture"),
                          QStringLiteral("SMALL-HUGE")}});

    KVIT_ASSERT_WALL_BUDGET(firstFrameMs, "gui.first_frame before small-vault/huge-note scan", 1000.0);

    QMetaObject::invokeMethod(controller, "start", Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(controller->finished(), 5000);

    QVERIFY(collection->isOpen());
    QVERIFY(collection->scanInProgress());
    QCOMPARE(collection->lastOpenNote(), QStringLiteral("Small.md"));
    QCOMPARE(manager->currentFilePath(),
             collection->absolutePath(QStringLiteral("Small.md")));
    QVERIFY(model->count() > 0);
    QVERIFY(!log.samples(QStringLiteral("startup.initial_open")).isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(collection->noteCount(), 2, 5000);

    qInfo("PERF GUI startup.first_frame small-vault-huge-note deferred: %.2f ms",
          firstFrameMs);
    qInfo("PERF GUI startup.initial_open small-vault-huge-note deferred: %.2f ms",
          log.samples(QStringLiteral("startup.initial_open")).last().durationMs);
    qInfo("PERF GUI startup.scan small-vault-huge-note parsed-before-finish: %d",
          int(parsed.size()));
}

void TestPerformanceApp::guiCollectionPersistenceLiveSizedNote()
{
    PerfLog &log = PerfLog::instance();
    log.clear();
    log.setEmitToStderr(false);
    log.setLevel(PerfLog::Major);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const PerfCorpus::DocumentFixture fixture =
        PerfCorpus::warAndPeaceLiveSized();
    QCOMPARE(PerfCorpus::writeSingleNote(dir.path(),
                                         QStringLiteral("Huge.md"),
                                         fixture.markdown),
             1);
    QVERIFY(writeCollectionState(dir.path(), QStringLiteral("Huge.md")));

    QQmlApplicationEngine engine;
    Setup setup;
    setup.qmlEngineAvailable(&engine);

    DocumentManager *manager = setup.context()->documentManager();
    QVERIFY(manager);

    BlockModel *model = setup.context()->blockModel();
    QVERIFY(model);

    UndoStack *undoStack = setup.context()->undoStack();
    QVERIFY(undoStack);

    NoteCollection *collection = setup.context()->noteCollection();
    QVERIFY(collection);

    // The composed context already owns a StartupController, wired to this
    // same collection, manager, model and undo stack, and already published
    // as the startupController context property. Building a second one here
    // left two of them driving one collection.
    StartupController *controller = setup.context()->startupController();
    QVERIFY(controller);
    controller->setRootPath(dir.path());

    const QUrl url(QStringLiteral("qrc:/qml/main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty())
        QSKIP("QML window did not load in this environment");

    QQuickWindow *window =
        qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window)
        QSKIP("Root QML object is not a QQuickWindow");

    window->show();
    if (!QTest::qWaitForWindowExposed(window, 5000))
        QSKIP("No exposed Qt Quick window available for GUI performance sample");

    QMetaObject::invokeMethod(controller, "start", Qt::QueuedConnection);
    QTRY_VERIFY_WITH_TIMEOUT(controller->finished(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(collection->noteCount(), 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!collection->scanInProgress(), 10000);

    QCOMPARE(collection->lastOpenNote(), QStringLiteral("Huge.md"));
    QCOMPARE(manager->currentFilePath(),
             collection->absolutePath(QStringLiteral("Huge.md")));
    QCOMPARE(model->count(), fixture.blocks);

    log.clear();
    model->updateContent(0, model->getContent(0)
                            + QStringLiteral(" manual-save"));
    QVERIFY(manager->isDirty());
    QVERIFY(manager->saveAsync());
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("note.save.commit")).isEmpty(),
        10000);

    const auto manual = log.samples(QStringLiteral("note.save"));
    const auto manualSerialize =
        log.samples(QStringLiteral("note.save.serialize"));
    const auto manualBackup =
        log.samples(QStringLiteral("collection.backup_before_overwrite"));
    QVERIFY(!manual.isEmpty());
    QVERIFY(!manualSerialize.isEmpty());
    QVERIFY(!manualBackup.isEmpty());
    QVERIFY(!log.samples(QStringLiteral("collection.note_saved")).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("collection.note_saved.index")).isEmpty(),
        10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("collection.note_saved.apply")).isEmpty(),
        10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("collection.index_save")).isEmpty(),
        10000);
    const auto manualCollection =
        log.samples(QStringLiteral("collection.note_saved"));
    const auto manualCollectionIndex =
        log.samples(QStringLiteral("collection.note_saved.index"));
    const auto manualCollectionApply =
        log.samples(QStringLiteral("collection.note_saved.apply"));
    const auto manualIndexSave =
        log.samples(QStringLiteral("collection.index_save"));
    QVERIFY(!manualCollection.isEmpty());
    QVERIFY(!manualCollectionIndex.isEmpty());
    QVERIFY(!manualCollectionApply.isEmpty());
    QVERIFY(!manualIndexSave.isEmpty());
    QVERIFY(manualIndexSave.last().context.value(QStringLiteral("async")).toBool());
    QVERIFY(!manager->isDirty());

    qInfo("PERF GUI note.save main-thread live-sized: %.2f ms",
          manual.last().durationMs);
    qInfo("PERF GUI note.save.serialize live-sized: %.2f ms",
          manualSerialize.last().durationMs);
    qInfo("PERF GUI collection.backup_before_overwrite live-sized: %.2f ms",
          manualBackup.last().durationMs);
    qInfo("PERF GUI collection.note_saved live-sized: %.2f ms",
          manualCollection.last().durationMs);
    qInfo("PERF GUI collection.note_saved.index live-sized: %.2f ms",
          manualCollectionIndex.last().durationMs);
    qInfo("PERF GUI collection.note_saved.apply live-sized: %.2f ms",
          manualCollectionApply.last().durationMs);
    qInfo("PERF GUI collection.index_save live-sized: %.2f ms",
          manualIndexSave.last().durationMs);

    log.clear();
    model->updateContent(0, model->getContent(0)
                            + QStringLiteral(" autosave"));
    QVERIFY(manager->isDirty());
    QVERIFY(QMetaObject::invokeMethod(manager, "onAutoSaveTimer",
                                      Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("note.autosave.commit")).isEmpty(),
        10000);
    QTRY_VERIFY_WITH_TIMEOUT(!manager->isDirty(), 10000);

    const auto autosave = log.samples(QStringLiteral("note.autosave"));
    const auto autosaveSerialize =
        log.samples(QStringLiteral("note.autosave.serialize"));
    QVERIFY(!autosave.isEmpty());
    QVERIFY(!autosaveSerialize.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("collection.note_saved.index")).isEmpty(),
        10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("collection.note_saved.apply")).isEmpty(),
        10000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("collection.index_save")).isEmpty(),
        10000);
    const auto autosaveCollection =
        log.samples(QStringLiteral("collection.note_saved"));
    const auto autosaveCollectionIndex =
        log.samples(QStringLiteral("collection.note_saved.index"));
    const auto autosaveCollectionApply =
        log.samples(QStringLiteral("collection.note_saved.apply"));
    const auto autosaveIndexSave =
        log.samples(QStringLiteral("collection.index_save"));
    QVERIFY(!autosaveCollection.isEmpty());
    QVERIFY(!autosaveCollectionIndex.isEmpty());
    QVERIFY(!autosaveCollectionApply.isEmpty());
    QVERIFY(!autosaveIndexSave.isEmpty());
    QVERIFY(autosaveIndexSave.last().context.value(QStringLiteral("async")).toBool());

    qInfo("PERF GUI note.autosave main-thread live-sized: %.2f ms",
          autosave.last().durationMs);
    qInfo("PERF GUI note.autosave.serialize live-sized: %.2f ms",
          autosaveSerialize.last().durationMs);
    qInfo("PERF GUI collection.note_saved autosave live-sized: %.2f ms",
          autosaveCollection.last().durationMs);
    qInfo("PERF GUI collection.note_saved.index autosave live-sized: %.2f ms",
          autosaveCollectionIndex.last().durationMs);
    qInfo("PERF GUI collection.note_saved.apply autosave live-sized: %.2f ms",
          autosaveCollectionApply.last().durationMs);
    qInfo("PERF GUI collection.index_save autosave live-sized: %.2f ms",
          autosaveIndexSave.last().durationMs);

    log.clear();
    if (manager->journalPath().isEmpty()) {
        manager->setJournalPath(
            collection->journalPathFor(QStringLiteral("Huge.md")));
    }
    QVERIFY(!manager->journalPath().isEmpty());
    model->updateContent(0, model->getContent(0)
                            + QStringLiteral(" journal"));
    QVERIFY(manager->isDirty());
    QVERIFY(QMetaObject::invokeMethod(manager, "writeJournal",
                                      Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(
        !log.samples(QStringLiteral("note.journal_write.commit")).isEmpty(),
        10000);

    const auto journal =
        log.samples(QStringLiteral("note.journal_write"));
    const auto journalSerialize =
        log.samples(QStringLiteral("note.journal_write.serialize"));
    const auto journalWrite =
        log.samples(QStringLiteral("note.journal_write.write"));
    const auto journalFlush =
        log.samples(QStringLiteral("note.journal_write.flush"));
    const auto journalCommit =
        log.samples(QStringLiteral("note.journal_write.commit"));
    QVERIFY(!journal.isEmpty());
    QVERIFY(!journalSerialize.isEmpty());
    QVERIFY(!journalWrite.isEmpty());
    QVERIFY(!journalFlush.isEmpty());
    QVERIFY(!journalCommit.isEmpty());
    QVERIFY(journalCommit.last().context.value(QStringLiteral("ok")).toBool());

    qInfo("PERF GUI note.journal_write main-thread live-sized: %.2f ms",
          journal.last().durationMs);
    qInfo("PERF GUI note.journal_write.serialize live-sized: %.2f ms",
          journalSerialize.last().durationMs);
    qInfo("PERF GUI note.journal_write.write live-sized worker: %.2f ms",
          journalWrite.last().durationMs);
    qInfo("PERF GUI note.journal_write.flush live-sized worker: %.2f ms",
          journalFlush.last().durationMs);
    qInfo("PERF GUI note.journal_write.commit live-sized worker: %.2f ms",
          journalCommit.last().durationMs);
}

QTEST_MAIN(TestPerformanceApp)
#include "test_performance_app.moc"
