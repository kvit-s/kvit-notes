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

// Phase 12 Step 8: system integration seams. The tray and global hotkey route
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

QTEST_MAIN(TestSystemIntegration)
#include "test_systemintegration.moc"
