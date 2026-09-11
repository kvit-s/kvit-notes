// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "notecollection.h"
#include "processservices.h"

namespace {

ProcessServices::Options headlessOptions()
{
    // The two seams that reach the desktop session, off for a harness — the
    // same choice tests/testsetup.h makes for the shell suite.
    ProcessServices::Options options;
    options.showSystemTray = false;
    options.configureLoggingFromSettings = false;
    return options;
}

// A window that composes the keyboard map and answers its commands itself.
//
// This is the application the suite is about: it links the library, takes
// qml/AppShortcuts.qml for the window-level keys, and draws screens of its own
// that Back and Forward move through. None of its four answers touches the
// session, so a key that went round this window to the session instead would
// leave both marks — the count here would not move, and the note history
// would.
constexpr const char *kHostThatAnswersTheKeysItself = R"(
    import QtQuick
    import QtQuick.Window
    import Kvit 1.0
    Window {
        id: host
        width: 400
        height: 300
        property alias session: openNote

        // What the map asks this window before any key is pressed. Both are
        // `enabled` bindings over the window's own screen, which is why a host
        // declares them and the session does not have them at all.
        property bool focusMode: false
        property bool blockContextShortcutEnabled: false

        property int backs: 0
        property int forwards: 0
        property int saves: 0
        property int newNotes: 0
        function navigateBack() { host.backs++ }
        function navigateForward() { host.forwards++ }
        function saveCurrentDocument(forceSaveAs) { host.saves++ }
        function createNoteInCurrentScope() { host.newNotes++ }

        NoteSession { id: openNote }
        AppShortcuts {
            anchors.fill: parent
            appWindow: host
            noteSession: openNote
        }
    }
)";

// The same window with nothing decided differently: each command is handed
// straight to the session, which is what qml/main.qml does with all five.
constexpr const char *kHostThatForwardsToItsSession = R"(
    import QtQuick
    import QtQuick.Window
    import Kvit 1.0
    Window {
        id: host
        width: 400
        height: 300
        property alias session: openNote

        property bool focusMode: false
        property bool blockContextShortcutEnabled: false

        function navigateBack() { openNote.navigateBack() }
        function navigateForward() { openNote.navigateForward() }
        function saveCurrentDocument(forceSaveAs) {
            return openNote.saveCurrentDocument(forceSaveAs)
        }
        function createNoteInCurrentScope() {
            openNote.createNoteInCurrentScope()
        }

        NoteSession { id: openNote }
        AppShortcuts {
            anchors.fill: parent
            appWindow: host
            noteSession: openNote
        }
    }
)";

void writeAll(const QString &path, const QString &text)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(text.toUtf8());
}

// Everything a QML document declares has the signature QVariant f(QVariant...),
// so an argument goes through QVariant whatever it is written as.
QVariant call(QObject *target, const char *name, const QVariant &argument)
{
    QVariant result;
    QMetaObject::invokeMethod(target, name, Q_RETURN_ARG(QVariant, result),
                              Q_ARG(QVariant, argument));
    return result;
}

// What one entry of a Shortcut's `sequence` or `sequences` answers to. QML
// writes those three ways and each has to be read differently: "Alt+Left" as
// written, a StandardKey as whatever this platform binds it to — which is how
// a second Alt+Left could arrive without the string appearing anywhere — and
// anything else through QVariant.
QList<QKeySequence> sequencesIn(const QVariant &value)
{
    if (value.userType() == QMetaType::Int) {
        return QKeySequence::keyBindings(
            QKeySequence::StandardKey(value.toInt()));
    }
    if (value.userType() == QMetaType::QString)
        return {QKeySequence(value.toString())};
    if (value.canConvert<QKeySequence>())
        return {value.value<QKeySequence>()};
    return {};
}

// Every key sequence one Shortcut answers to, whether it was declared as the
// single `sequence` or in the `sequences` list.
QList<QKeySequence> sequencesOf(QObject *shortcut)
{
    QList<QKeySequence> found = sequencesIn(shortcut->property("sequence"));
    for (const QVariant &each : shortcut->property("sequences").toList())
        found.append(sequencesIn(each));
    return found;
}

int shortcutsFor(QObject *window, const QKeySequence &wanted)
{
    int count = 0;
    for (QObject *child : window->findChildren<QObject *>()) {
        if (qstrcmp(child->metaObject()->className(), "QQuickShortcut") != 0)
            continue;
        for (const QKeySequence &sequence : sequencesOf(child)) {
            if (sequence == wanted)
                ++count;
        }
    }
    return count;
}

} // namespace

// Who a window-level keystroke asks.
//
// qml/AppShortcuts.qml is the window-level keyboard map — Ctrl+S, Ctrl+N,
// Ctrl+O, Alt+Left and Alt+Right among them — and each of those keys means
// something about the window it is bound in: save what this window is showing,
// make a new one here, go back to what I was looking at. What the open note
// does about that is the session's, but whether the keystroke is about the open
// note at all is the window's, so the map issues the command to the window and
// the window answers it out of the session it hosts.
//
// The difference is invisible in the application this repository ships, which
// has one window whose answer is always "hand it to the session". It is the
// whole behaviour for an application that composes this map into a window of
// its own, and for the editor's own second window: a preview or capture window
// that grows a Back key should move what it is showing rather than silently
// driving the main window's note history.
//
// So this suite builds both windows. One answers the keys itself and is never
// allowed to reach the session; one forwards every command the way qml/main.qml
// does, and the note history moves. Alt+Left is one shortcut in either of them,
// because two enabled shortcuts on one sequence are an ambiguous overload that
// Qt resolves by firing neither.
class TestWindowCommands : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
        m_globals = std::make_unique<ProcessServices>(headlessOptions());
        m_globals->openSettings(
            m_settings.filePath(QStringLiteral("settings.json")));
        m_context = std::make_unique<AppContext>(*m_globals);
        m_context->installContextProperties(&m_engine);

        writeAll(notePath(QStringLiteral("one.md")),
                 QStringLiteral("first note\n"));
        writeAll(notePath(QStringLiteral("two.md")),
                 QStringLiteral("second note\n"));
        QVERIFY(m_context->openVaultRoot(m_vault.path()));
        QTRY_COMPARE(m_context->noteCollection()->rootPath(),
                     QDir(m_vault.path()).absolutePath());
    }

    // One window at a time. Both hosts bind Alt+Left in the application
    // context, so two of them at once would be the ambiguous overload this
    // suite ends by asserting there is none of.
    void cleanup()
    {
        m_session = nullptr;
        m_window = nullptr;
        m_host.reset();
    }

    // Alt+Left and Alt+Right reach the window's own definitions, and the
    // session they are not handed to stays where it was.
    void theNavigationKeysReachTheWindowsOwnAnswers()
    {
        build(kHostThatAnswersTheKeysItself);
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("one.md")).toBool());
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("two.md")).toBool());

        press(Qt::Key_Left, Qt::AltModifier);
        QTRY_COMPARE(m_host->property("backs").toInt(), 1);
        press(Qt::Key_Right, Qt::AltModifier);
        QTRY_COMPARE(m_host->property("forwards").toInt(), 1);

        QCOMPARE(m_session->property("currentNoteRelPath").toString(),
                 QStringLiteral("two.md"));
    }

    // Ctrl+S and Ctrl+N are the same arrangement: the key says what this
    // window does, and this window says what that is. (Ctrl+O is the fifth
    // command and is not driven here — outside collection mode it opens a
    // native file picker, and the suite has a collection open.)
    void theSaveAndNewNoteKeysReachThemToo()
    {
        build(kHostThatAnswersTheKeysItself);
        pressStandard(QKeySequence::Save);
        QTRY_COMPARE(m_host->property("saves").toInt(), 1);
        pressStandard(QKeySequence::New);
        QTRY_COMPARE(m_host->property("newNotes").toInt(), 1);
    }

    // With nothing decided differently, both keys move the note history, which
    // is what they do in the window this application ships.
    void withNothingRedefinedTheSameKeysMoveTheNoteHistory()
    {
        build(kHostThatForwardsToItsSession);
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("one.md")).toBool());
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("two.md")).toBool());

        press(Qt::Key_Left, Qt::AltModifier);
        QTRY_COMPARE(m_session->property("currentNoteRelPath").toString(),
                     QStringLiteral("one.md"));
        press(Qt::Key_Right, Qt::AltModifier);
        QTRY_COMPARE(m_session->property("currentNoteRelPath").toString(),
                     QStringLiteral("two.md"));
    }

    void exactlyOneAltLeftShortcutExistsEitherWay_data()
    {
        QTest::addColumn<QString>("host");
        QTest::newRow("answers the keys itself")
            << QString::fromUtf8(kHostThatAnswersTheKeysItself);
        QTest::newRow("forwards to its session")
            << QString::fromUtf8(kHostThatForwardsToItsSession);
    }

    void exactlyOneAltLeftShortcutExistsEitherWay()
    {
        QFETCH(QString, host);
        build(host.toUtf8().constData());
        QCOMPARE(shortcutsFor(m_window, QKeySequence(QStringLiteral("Alt+Left"))),
                 1);
        QCOMPARE(shortcutsFor(m_window, QKeySequence(QStringLiteral("Alt+Right"))),
                 1);
    }

private:
    QString notePath(const QString &name) const
    {
        return QDir(m_vault.path()).absoluteFilePath(name);
    }

    void build(const char *host)
    {
        QQmlComponent component(&m_engine);
        component.setData(host, QUrl(QStringLiteral("inmemory:host.qml")));
        QTRY_VERIFY(component.status() != QQmlComponent::Loading);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        m_host.reset(component.create(m_engine.rootContext()));
        QVERIFY2(m_host, qPrintable(component.errorString()));
        m_session = m_host->property("session").value<QObject *>();
        QVERIFY(m_session);
        m_window = qobject_cast<QQuickWindow *>(m_host.get());
        QVERIFY(m_window);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_window->requestActivate();
        QTRY_VERIFY(m_window->isActive());
    }

    void press(Qt::Key key, Qt::KeyboardModifiers modifiers)
    {
        QTest::keyClick(m_window, key, modifiers);
    }

    // The platform's own binding for a standard command: Ctrl+S and Ctrl+N
    // here, Cmd+S and Cmd+N on macOS, which is what the map declares.
    void pressStandard(QKeySequence::StandardKey standard)
    {
        const QKeySequence sequence(standard);
        QVERIFY(sequence.count() > 0);
        const QKeyCombination combination = sequence[0];
        QTest::keyClick(m_window, combination.key(),
                        combination.keyboardModifiers());
    }

    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    QQmlApplicationEngine m_engine;
    std::unique_ptr<ProcessServices> m_globals;
    std::unique_ptr<AppContext> m_context;
    std::unique_ptr<QObject> m_host;
    QQuickWindow *m_window = nullptr;
    QObject *m_session = nullptr;
};

QTEST_MAIN(TestWindowCommands)
#include "test_windowcommands.moc"
