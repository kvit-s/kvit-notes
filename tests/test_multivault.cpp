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

#include <QDir>
#include <QFile>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>

#include "appactions.h"
#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "documentmanager.h"
#include "filesystemtreemodel.h"
#include "filewatcher.h"
#include "notecollection.h"
#include "notelistmodel.h"
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

        // The root rail's close action reaches the owning window even when
        // it was requested from the other one.
        first->context()->appActions()->requestCloseVault(vault2.path());
        QTRY_COMPARE(registry.windowCount(), 1);
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

    // Switching a window to another vault gives up the first vault's lock, so
    // the note on screen has to reach disk while this window is still its one
    // writer, and has to be let go afterwards. Without both, the editor kept a
    // file open — and saveable — in a vault the process no longer held, where
    // another session was by then free to edit the same file.
    void switchingVaultsSavesAndReleasesTheOpenNote()
    {
        QTemporaryDir settingsDir;
        QTemporaryDir vault1;
        QTemporaryDir vault2;
        ProcessServices globals(headlessOptions());
        globals.openSettings(settingsDir.filePath(QStringLiteral("settings.json")));
        AppContext context(globals);

        const QString note = vault1.filePath(QStringLiteral("note.md"));
        {
            QFile seed(note);
            QVERIFY(seed.open(QIODevice::WriteOnly));
            seed.write("first\n");
        }

        QVERIFY(context.openVaultRoot(vault1.path()));
        QVERIFY(context.documentManager()->open(QUrl::fromLocalFile(note)));
        context.blockModel()->updateContent(0, QStringLiteral("edited"));
        QVERIFY(context.documentManager()->isDirty());

        QVERIFY(context.openVaultRoot(vault2.path()));

        QFile written(note);
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(written.readAll()), QStringLiteral("edited\n"));
        QVERIFY2(!context.documentManager()->currentFilePath()
                      .startsWith(vault1.path()),
                 "the editor kept a note open in the vault it had just left");
        QCOMPARE(context.noteCollection()->rootPath(),
                 QDir(vault2.path()).absolutePath());
    }

    // The complete in-place path through the production shell: dirty work
    // stops at the existing document dialog; a completed switch replaces all
    // watch registrations; and returning restores the per-root file, scroll,
    // sidebar, panel and sort state recorded by SessionPersistence.qml.
    void inPlaceSwitchSettlesDirtyWorkAndRestoresPerRootState()
    {
        QTemporaryDir settingsDir;
        QTemporaryDir vault1;
        QTemporaryDir vault2;
        ProcessServices globals(headlessOptions());
        globals.openSettings(settingsDir.filePath(QStringLiteral("settings.json")));

        QByteArray longNote;
        for (int i = 0; i < 80; ++i) {
            longNote += QByteArray("## Section ") + QByteArray::number(i)
                      + QByteArray("\n\nA paragraph long enough to occupy its "
                                   "own visible editor row.\n\n");
        }
        const QString note1 = vault1.filePath(QStringLiteral("a.md"));
        const QString note2 = vault2.filePath(QStringLiteral("b.md"));
        for (const auto &[path, bytes] : {
                 std::pair<QString, QByteArray>{note1, longNote},
                 std::pair<QString, QByteArray>{note2, QByteArray("# Other\n")}}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        }

        WindowRegistry registry(globals,
                                QUrl(QStringLiteral("qrc:/qml/main.qml")));
        QVERIFY(registry.openStartup(vault1.path()));
        VaultWindow *vaultWindow = registry.activeWindow();
        QVERIFY(vaultWindow);
        AppContext *context = vaultWindow->context();
        QQuickWindow *window = vaultWindow->window();
        QVERIFY(window);

        const QString root1 = QDir(vault1.path()).absolutePath();
        const QString root2 = QDir(vault2.path()).absolutePath();
        QTRY_COMPARE(context->noteCollection()->rootPath(), root1);
        QTRY_VERIFY(context->startupController()->finished());
        QTRY_COMPARE(context->documentManager()->currentFilePath(), note1);

        NoteCollection *const oneCollection = context->noteCollection();
        FileWatcher *const oneWatcher = context->fileWatcher();
        FileSystemTreeModel *const oneFileTree = context->fileSystemTreeModel();
        QTRY_VERIFY(!oneWatcher->discoveryPending());
        QCOMPARE(oneWatcher->watchedDirectoriesForTests().size(), 1);
        QCOMPARE(oneFileTree->watchedDirectoryCountForTests(), 1);

        window->setProperty("sidebarView", QStringLiteral("files"));
        window->setProperty("sidebarWidth", 231);
        window->setProperty("noteListWidth", 321);
        context->noteListModel()->setSortMode(QStringLiteral("title"));
        context->noteListModel()->setAscending(true);

        // Wait for the long note to lay out before choosing a non-zero place.
        QVariant scroll;
        QTRY_VERIFY(QMetaObject::invokeMethod(
                        window, "setEditorContentY", Q_ARG(QVariant, 180.0))
                    && QMetaObject::invokeMethod(
                        window, "editorContentY", Q_RETURN_ARG(QVariant, scroll))
                    && scroll.toReal() > 100.0);
        const qreal savedScroll = scroll.toReal();

        context->blockModel()->updateContent(
            0, context->blockModel()->getContent(0) + QStringLiteral(" edited"));
        QVERIFY(context->documentManager()->isDirty());
        context->appActions()->requestOpenVault(root2);

        // The request has not crossed roots; the shell has put the same
        // save/discard/cancel settlement used by other document transitions
        // in front of it.
        QCOMPARE(context->noteCollection()->rootPath(), root1);
        QObject *switchDialog = nullptr;
        QTRY_VERIFY((switchDialog =
                         window->findChild<QObject *>("vaultSwitchDialog")));
        QTRY_VERIFY(switchDialog->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(switchDialog, "close"));
        QCOMPARE(context->noteCollection()->rootPath(), root1);

        QVERIFY(context->documentManager()->save());
        context->appActions()->requestOpenVault(root2);
        QTRY_COMPARE(context->noteCollection()->rootPath(), root2);
        QTRY_VERIFY(context->startupController()->finished());

        // The old registrations are gone, not accumulated beside the new
        // ones, and the per-vault service graph itself still has one instance
        // of each owner rather than retaining a departed composition.
        QCOMPARE(context->noteCollection(), oneCollection);
        QCOMPARE(context->fileWatcher(), oneWatcher);
        QCOMPARE(context->fileSystemTreeModel(), oneFileTree);
        QTRY_VERIFY(!oneWatcher->discoveryPending());
        QCOMPARE(oneWatcher->watchedDirectoriesForTests(), QStringList{root2});
        QCOMPARE(oneFileTree->watchedDirectoryCountForTests(), 1);

        const QVariantMap states =
            globals.settings()->value(QStringLiteral("root.viewState")).toMap();
        const QVariantMap leftState = states.value(root1).toMap();
        QCOMPARE(leftState.value("sidebarView").toString(),
                 QStringLiteral("files"));
        QCOMPARE(leftState.value("sidebarWidth").toInt(), 231);
        QCOMPARE(leftState.value("noteListWidth").toInt(), 321);
        QCOMPARE(leftState.value("sortMode").toString(), QStringLiteral("title"));
        QCOMPARE(leftState.value("sortAscending").toBool(), true);
        QCOMPARE(leftState.value("openFile").toString(), QStringLiteral("a.md"));
        QVERIFY(qAbs(leftState.value("scrollY").toReal() - savedScroll) < 2.0);

        // The lock travels with the live root: the one just left can be
        // opened by another collection, while the current one cannot.
        NoteCollection lockProbe;
        QVERIFY(lockProbe.openRoot(root1));
        QVERIFY(!lockProbe.openRoot(root2));
        lockProbe.closeRoot();

        window->setProperty("sidebarView", QStringLiteral("notes"));
        window->setProperty("sidebarWidth", 190);
        window->setProperty("noteListWidth", 250);
        context->noteListModel()->setSortMode(QStringLiteral("modified"));
        context->noteListModel()->setAscending(false);
        context->appActions()->requestOpenVault(root1);

        QTRY_COMPARE(context->noteCollection()->rootPath(), root1);
        QTRY_VERIFY(context->startupController()->finished());
        QTRY_COMPARE(context->documentManager()->currentFilePath(), note1);
        QCOMPARE(window->property("sidebarView").toString(),
                 QStringLiteral("files"));
        QCOMPARE(window->property("sidebarWidth").toInt(), 231);
        QCOMPARE(window->property("noteListWidth").toInt(), 321);
        QCOMPARE(context->noteListModel()->sortMode(), QStringLiteral("title"));
        QCOMPARE(context->noteListModel()->ascending(), true);
        QTRY_VERIFY(QMetaObject::invokeMethod(
                        window, "editorContentY", Q_RETURN_ARG(QVariant, scroll))
                    && qAbs(scroll.toReal() - savedScroll) < 2.0);
        QTRY_VERIFY(!oneWatcher->discoveryPending());
        QCOMPARE(oneWatcher->watchedDirectoriesForTests(), QStringList{root1});
        QCOMPARE(oneFileTree->watchedDirectoryCountForTests(), 1);
    }

    // Quitting from the tray is a close of every window, and a close is where
    // the orderly save lives. QApplication::quit() sends no close event, so a
    // document with unsaved changes went with the process; the registry's
    // close-every-window path is what the tray's Quit now goes through.
    void requestCloseAllSavesEveryWindowsDocument()
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
        registry.openVaultInNewWindow(vault2.path());
        VaultWindow *second = registry.activeWindow();
        QVERIFY(second && second != first);

        // Each window has a note open with an edit that has not reached disk.
        const QString noteA = vault1.filePath(QStringLiteral("a.md"));
        const QString noteB = vault2.filePath(QStringLiteral("b.md"));
        const auto seed = [](VaultWindow *w, const QString &path,
                             const QString &text) {
            DocumentManager *docs = w->context()->documentManager();
            w->context()->blockModel()->clear();
            w->context()->blockModel()->insertBlockInternal(
                0, Block::Paragraph, QStringLiteral("saved"));
            QVERIFY(docs->saveAs(QUrl::fromLocalFile(path)));
            w->context()->blockModel()->updateContent(0, text);
            QVERIFY(docs->isDirty());
        };
        seed(first, noteA, QStringLiteral("edited in the first window"));
        seed(second, noteB, QStringLiteral("edited in the second window"));

        QVERIFY2(registry.requestCloseAll(),
                 "a window refused a close it had nothing to object to");

        const auto readAll = [](const QString &path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly)
                       ? QString::fromUtf8(f.readAll()) : QString();
        };
        QCOMPARE(readAll(noteA),
                 QStringLiteral("edited in the first window\n"));
        QCOMPARE(readAll(noteB),
                 QStringLiteral("edited in the second window\n"));
        QVERIFY(!first->context()->documentManager()->isDirty());
        QVERIFY(!second->context()->documentManager()->isDirty());
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
