// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "globalhotkey.h"
#include "systemtray.h"
#include "settingsstore.h"
#include "notecollection.h"

#include "faultinjection.h"

// System integration seams. The tray and global hotkey route
// their actions through signals so the in-app path is exercised without a live
// desktop (spike (b): WSLg grants neither a tray nor a system-wide hotkey). This
// covers the seam contracts and the quick-capture write.
class TestSystemIntegration : public QObject
{
    Q_OBJECT

private slots:
    void hotkeyRegistersOnlyWhenSupported();
    void hotkeyTriggerEmitsActivated();
    void trayActionsRouteToSignals();
    void trayNotifyRecordsAndEmits();
    void trayCloseToTrayIsOptInAndPersists();
    void captureNoteWritesBodyTitledFromFirstLine();
    void captureNoteFallsBackToUntitled();
    void captureNoteLeavesNothingBehindWhenTheWriteFails();
    void captureNoteLeavesNoEmptyNoteWhenOnlyTheBodyFails();
    void quickCaptureChordFollowsTheSetting();
};

void TestSystemIntegration::hotkeyRegistersOnlyWhenSupported()
{
    GlobalHotkey hk;
    // Unsupported platform (the WSLg default): the chord is stored but the grab
    // fails, so the in-app path is the only route.
    QCOMPARE(hk.registerShortcut("Ctrl+Alt+N"), false);
    QCOMPARE(hk.shortcut(), QString("Ctrl+Alt+N"));
    QCOMPARE(hk.registered(), false);

    // A supported platform accepts the grab.
    hk.setSupported(true);
    QCOMPARE(hk.registerShortcut("Ctrl+Alt+N"), true);
    QCOMPARE(hk.registered(), true);
    hk.unregister();
    QCOMPARE(hk.registered(), false);
}

void TestSystemIntegration::hotkeyTriggerEmitsActivated()
{
    GlobalHotkey hk;
    QSignalSpy spy(&hk, &GlobalHotkey::activated);
    hk.trigger();
    QCOMPARE(spy.count(), 1);
}

void TestSystemIntegration::trayActionsRouteToSignals()
{
    SystemTray tray;
    QSignalSpy newNote(&tray, &SystemTray::newNoteRequested);
    QSignalSpy capture(&tray, &SystemTray::quickCaptureRequested);
    QSignalSpy show(&tray, &SystemTray::showWindowRequested);
    QSignalSpy quit(&tray, &SystemTray::quitRequested);
    tray.triggerAction("newNote");
    tray.triggerAction("quickCapture");
    tray.triggerAction("show");
    tray.triggerAction("quit");
    QCOMPARE(newNote.count(), 1);
    QCOMPARE(capture.count(), 1);
    QCOMPARE(show.count(), 1);
    QCOMPARE(quit.count(), 1);
}

void TestSystemIntegration::trayNotifyRecordsAndEmits()
{
    SystemTray tray;
    QSignalSpy spy(&tray, &SystemTray::notified);
    tray.notify("Kvit", "Note captured");
    QCOMPARE(tray.lastNotification(), QString("Note captured"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QString("Note captured"));
}

void TestSystemIntegration::trayCloseToTrayIsOptInAndPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store;
    QVERIFY(store.open(dir.filePath("settings.json")));

    // Default: closing the last window quits; tray residency is opt-in.
    SystemTray tray;
    QSignalSpy changed(&tray, &SystemTray::closeToTrayChanged);
    tray.setSettings(&store);
    QCOMPARE(tray.closeToTray(), false);
    QCOMPARE(changed.count(), 0);

    // Opting in persists and notifies (the quit policy re-applies live).
    tray.setCloseToTray(true);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(store.value("tray.closeToTray").toBool(), true);

    // A fresh seam attached to the same store picks the choice up.
    SystemTray reopened;
    reopened.setSettings(&store);
    QCOMPARE(reopened.closeToTray(), true);
}

void TestSystemIntegration::captureNoteWritesBodyTitledFromFirstLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    NoteCollection col;
    QVERIFY(col.openRoot(dir.path()));

    const QString rel = col.captureNote("Buy milk\nand eggs on the way home");
    QVERIFY(!rel.isEmpty());
    // Titled from the first line.
    QVERIFY2(rel.contains("Buy milk"), qPrintable("relPath was " + rel));
    // The body is the captured text.
    QFile f(col.absolutePath(rel));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString body = QString::fromUtf8(f.readAll());
    QVERIFY(body.contains("Buy milk"));
    QVERIFY(body.contains("and eggs on the way home"));
}

void TestSystemIntegration::captureNoteFallsBackToUntitled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    NoteCollection col;
    QVERIFY(col.openRoot(dir.path()));

    // A body whose first line cannot be a valid file name still captures.
    const QString rel = col.captureNote("/// only punctuation ///\nsome body");
    QVERIFY(!rel.isEmpty());
    QFile f(col.absolutePath(rel));
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(QString::fromUtf8(f.readAll()).contains("some body"));
}


// The half-written window specifically, and a bug found by looking for it.
//
// The case below denies writes to the whole vault, so the note is never
// created; a mutation audit showed that the two-step "create empty, then
// fill" implementation the H12 fix replaced passes that test unchanged.
// Capping the file size instead lets a small write succeed and a large one
// fail, which is the shape that tells the two apart.
//
// Doing that surfaced a real defect that is not about capture at all:
// writeTextFileAtomic (notecollection.cpp) streams through QTextStream and
// returns QSaveFile::commit() without ever checking stream.status(), so a
// write that stops short reports success. writeFileBytesAtomic beside it does
// check. With a 4 KB cap and a 64 KB note the capture returns a relative path
// and leaves a file holding 4096 of 65545 bytes: the user is told the note
// was captured, and most of it is gone.
//
// The QEXPECT_FAIL below records that. When the writer is fixed, this test
// reports an unexpected pass and fails, which is the signal to remove it.
void TestSystemIntegration::captureNoteLeavesNoEmptyNoteWhenOnlyTheBodyFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    NoteCollection col;
    QVERIFY(col.openRoot(dir.path()));

    // Large enough that the body cannot land, small enough that creating an
    // empty file can.
    const QString body =
        QStringLiteral("Buy milk\n") + QString(64 * 1024, QLatin1Char('x'));
    const qint64 intended = body.toUtf8().size();

    FaultInjection::FileSizeLimit limit(4096);
    if (!limit.supported())
        QSKIP(qPrintable(limit.skipReason()));

    const QString rel = col.captureNote(body);

    QEXPECT_FAIL("", "writeTextFileAtomic ignores QTextStream::status(), so a "
                     "short write reports success and the note is silently "
                     "truncated", Abort);
    QVERIFY2(rel.isEmpty(),
             "capture reported success for a note whose body did not fit");

    // Unreached until the writer is fixed; kept so the intent is explicit.
    QCOMPARE(col.noteCount(), 0);
    const QStringList left =
        QDir(dir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot);
    QVERIFY2(left.isEmpty(),
             qPrintable(QStringLiteral("capture left %1 behind holding none of "
                                       "the captured text").arg(left.join(", "))));
    Q_UNUSED(intended);
}

void TestSystemIntegration::captureNoteLeavesNothingBehindWhenTheWriteFails()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    NoteCollection col;
    QVERIFY(col.openRoot(dir.path()));

    // A vault the process cannot write to stands in for the read-only
    // folder and the full disk. The injection is at the OS boundary rather
    // than through a seam in the collection: it exercises the real write
    // path, including the parts Qt implements. The guard restores the
    // permissions however this test leaves, so a failed assertion below
    // cannot leak a read-only directory into the next one.
    FaultInjection::DeniedWrites denied(dir.path());
    if (!denied.supported())
        QSKIP(qPrintable(denied.skipReason()));

    const QString rel = col.captureNote("Buy milk\nand eggs on the way home");

    // Failure is reported, so the window can keep the text on screen.
    QCOMPARE(rel, QString());
    // And nothing half-made survives: no empty note on disk, none indexed.
    QCOMPARE(col.noteCount(), 0);
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::NoDotAndDotDot),
             QStringList());
}

void TestSystemIntegration::quickCaptureChordFollowsTheSetting()
{
    // The chord is stored in one place and read by two consumers: this
    // registration and the in-app Shortcut in main.qml. Editing the setting
    // has to move both, or the setting silently does nothing on every
    // platform without a working system-wide grab -- which is all of them.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SettingsStore store;
    QVERIFY(store.open(dir.filePath("settings.json")));

    GlobalHotkey hk;
    hk.setSupported(true);   // pretend a backend exists, to observe re-arming
    const auto apply = [&]() {
        hk.registerShortcut(store.value(QStringLiteral("hotkey.quickCapture"),
                                        QStringLiteral("Ctrl+Alt+N")).toString());
    };
    QObject::connect(&store, &SettingsStore::valueChanged, &hk,
                     [&](const QString &key) {
                         if (key == QLatin1String("hotkey.quickCapture"))
                             apply();
                     });
    apply();
    QCOMPARE(hk.shortcut(), QStringLiteral("Ctrl+Alt+N"));

    store.setValue(QStringLiteral("hotkey.quickCapture"),
                   QStringLiteral("Ctrl+Shift+Space"));
    QCOMPARE(hk.shortcut(), QStringLiteral("Ctrl+Shift+Space"));
    QVERIFY(hk.registered());

    // An unrelated setting must not disturb the registration.
    store.setValue(QStringLiteral("view.equationNumbers"), true);
    QCOMPARE(hk.shortcut(), QStringLiteral("Ctrl+Shift+Space"));
}

QTEST_MAIN(TestSystemIntegration)
#include "test_systemintegration.moc"
