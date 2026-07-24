// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// The multi-window, multi-vault guarantees: two compositions in one process
// share exactly the process-global services and nothing else, and the window
// registry opens one window per vault, raising an already-open vault rather
// than opening a duplicate. The second test loads the real shell per window,
// so it runs offscreen alongside the other `shell`-labelled suites.
#include <QtTest>

#include <QTemporaryDir>
#include <QUrl>

#include "appactions.h"
#include "appcontext.h"
#include "blockmodel.h"
#include "processservices.h"
#include "vaultwindow.h"
#include "windowregistry.h"

namespace {
ProcessServices::Options headlessOptions()
{
    // The two seams that reach the desktop session, off for a harness — the
    // same choice testsetup.h makes for the shell suite.
    ProcessServices::Options options;
    options.showSystemTray = false;
    options.configureLoggingFromSettings = false;
    return options;
}
}

class TestMultiVault : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
    }

    // Two AppContexts over one ProcessServices: the globals are one shared set,
    // the per-vault services are each window's own, and an edit in one does not
    // reach the other. This is the isolation the whole feature rests on.
    void twoContextsShareGlobalsAndIsolatePerVault()
    {
        QTemporaryDir dir;
        ProcessServices globals(headlessOptions());
        globals.openSettings(dir.filePath(QStringLiteral("settings.json")));

        AppContext a(globals);
        AppContext b(globals);

        // Shared process globals: one object, seen by both.
        QCOMPARE(a.processServices(), &globals);
        QCOMPARE(b.processServices(), &globals);
        QCOMPARE(a.settings(), b.settings());
        QCOMPARE(a.theme(), b.theme());
        QCOMPARE(a.systemTray(), b.systemTray());
        QCOMPARE(a.egressPolicy(), b.egressPolicy());
        QCOMPARE(a.extensions(), b.extensions());
        QCOMPARE(a.blockKinds(), b.blockKinds());

        // Per-vault services: each composition's own.
        QVERIFY(a.noteCollection() != b.noteCollection());
        QVERIFY(a.documentManager() != b.documentManager());
        QVERIFY(a.blockModel() != b.blockModel());
        QVERIFY(a.undoStack() != b.undoStack());

        // An edit in one is invisible to the other.
        const int before = b.blockModel()->count();
        a.blockModel()->initializeWithSampleData();
        QVERIFY(a.blockModel()->count() > 0);
        QCOMPARE(b.blockModel()->count(), before);
    }

    // The registry opens a window per vault and raises — rather than opening a
    // duplicate — when the same vault is asked for again.
    void registryRaisesInsteadOfDuplicating()
    {
        QTemporaryDir settingsDir;
        QTemporaryDir vault1;
        QTemporaryDir vault2;
        ProcessServices globals(headlessOptions());
        globals.openSettings(settingsDir.filePath(QStringLiteral("settings.json")));
        WindowRegistry registry(globals,
                                QUrl(QStringLiteral("qrc:/qml/main.qml")));

        QVERIFY(registry.openStartup(vault1.path()));
        QCOMPARE(registry.windowCount(), 1);
        VaultWindow *first = registry.activeWindow();
        QVERIFY(first);

        // The same vault again: the existing window is raised, not duplicated.
        QVERIFY(registry.openStartup(vault1.path()));
        QCOMPARE(registry.windowCount(), 1);
        QCOMPARE(registry.activeWindow(), first);

        // A different vault: a second window, sharing the process globals but
        // with its own per-vault composition.
        registry.openVaultInNewWindow(vault2.path());
        QCOMPARE(registry.windowCount(), 2);
        VaultWindow *second = registry.activeWindow();
        QVERIFY(second);
        QVERIFY(second != first);
        QVERIFY(first->context() != second->context());
        QCOMPARE(first->context()->settings(), second->context()->settings());

        // Asking for the first vault again — even "in a new window" — raises
        // the one already open rather than adding a third.
        registry.openVaultInNewWindow(vault1.path());
        QCOMPARE(registry.windowCount(), 2);
    }

    // A bare cold launch reopens the vaults that were open at the last quit.
    void openSessionReopensLastVaults()
    {
        QTemporaryDir settingsDir;
        QTemporaryDir vault1;
        QTemporaryDir vault2;
        const QString settingsPath =
            settingsDir.filePath(QStringLiteral("settings.json"));

        // First session: two vaults open, so two are remembered.
        {
            ProcessServices globals(headlessOptions());
            globals.openSettings(settingsPath);
            WindowRegistry registry(globals,
                                    QUrl(QStringLiteral("qrc:/qml/main.qml")));
            QVERIFY(registry.openStartup(vault1.path()));
            registry.openVaultInNewWindow(vault2.path());
            QCOMPARE(registry.windowCount(), 2);
            globals.settings()->flush();   // land the debounced write
        }

        // A fresh process reopens both remembered vaults on a bare launch.
        {
            ProcessServices globals(headlessOptions());
            globals.openSettings(settingsPath);
            WindowRegistry registry(globals,
                                    QUrl(QStringLiteral("qrc:/qml/main.qml")));
            QVERIFY(registry.openSession());
            QCOMPARE(registry.windowCount(), 2);
        }
    }

    // Exactly one window is the tray's target — the active one — so a tray menu
    // action reaches one window rather than firing in all of them.
    void onlyTheActiveWindowIsTheTrayTarget()
    {
        QTemporaryDir settingsDir;
        QTemporaryDir vault1;
        QTemporaryDir vault2;
        ProcessServices globals(headlessOptions());
        globals.openSettings(settingsDir.filePath(QStringLiteral("settings.json")));
        WindowRegistry registry(globals,
                                QUrl(QStringLiteral("qrc:/qml/main.qml")));

        QVERIFY(registry.openStartup(vault1.path()));
        VaultWindow *first = registry.activeWindow();
        QVERIFY(first->context()->appActions()->trayTarget());

        registry.openVaultInNewWindow(vault2.path());
        VaultWindow *second = registry.activeWindow();
        QVERIFY(second != first);
        QVERIFY(second->context()->appActions()->trayTarget());
        QVERIFY(!first->context()->appActions()->trayTarget());
    }
};

QTEST_MAIN(TestMultiVault)
#include "test_multivault.moc"
