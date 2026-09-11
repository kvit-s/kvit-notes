// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "blockmodel.h"
#include "documentmanager.h"
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

// The whole host. A window, a NoteSession, and nothing else.
//
// Every line of it matters to what this suite claims. It is a Window, so it is
// the kind of thing qml/main.qml is and could have been written instead of it.
// It declares no sidebar view, no panel widths, no collapse flags, no focus or
// typewriter mode — none of the editor screen — and it instantiates no pane,
// no toolbar, no status bar and no menu. It reaches `import Kvit 1.0` for the
// session the same way an application reaches it for BlockEditor, and it never
// mentions main.qml.
constexpr const char *kMinimalHost = R"(
    import QtQuick
    import QtQuick.Window
    import Kvit 1.0
    Window {
        width: 400
        height: 300
        property alias session: openNote
        NoteSession { id: openNote }
    }
)";

// The editor screen: the members qml/main.qml holds because it draws the
// application's one window, which a session must never need. If any of these
// resolves on the session or on the host below, the split this suite is about
// did not happen.
const QStringList kScreenMembers = {
    QStringLiteral("panelsVisible"),      QStringLiteral("navigationRailsVisible"),
    QStringLiteral("sidebarView"),        QStringLiteral("sidebarCollapsed"),
    QStringLiteral("sidebarWidth"),       QStringLiteral("noteListCollapsed"),
    QStringLiteral("noteListWidth"),      QStringLiteral("outlineVisible"),
    QStringLiteral("backlinksVisible"),   QStringLiteral("statusBarVisible"),
    QStringLiteral("bottomDockCollapsed"), QStringLiteral("bottomDockHeight"),
    QStringLiteral("focusMode"),          QStringLiteral("typewriterMode"),
    QStringLiteral("notesFamilyView"),    QStringLiteral("focusedPane"),
};

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QStringLiteral("<unreadable: %1>").arg(path);
    return QString::fromUtf8(file.readAll());
}

void writeAll(const QString &path, const QString &text)
{
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(path));
    file.write(text.toUtf8());
}

// Call a function declared in QML. Everything a QML document declares has the
// signature QVariant f(QVariant...), so the arguments and the result go
// through QVariant whatever they are written as.
QVariant call(QObject *target, const char *name,
              const QVariant &a1 = QVariant(), bool oneArgument = true)
{
    QVariant result;
    const bool invoked = oneArgument
        ? QMetaObject::invokeMethod(target, name, Q_RETURN_ARG(QVariant, result),
                                    Q_ARG(QVariant, a1))
        : QMetaObject::invokeMethod(target, name, Q_RETURN_ARG(QVariant, result));
    if (!invoked)
        return QVariant();
    return result;
}

QVariant call0(QObject *target, const char *name)
{
    return call(target, name, QVariant(), false);
}

} // namespace

// The open note as an object, obtained without the window.
//
// qml/main.qml is an ApplicationWindow that assembles the QML faces of every
// library under it, and until qml/NoteSession.qml became a type of its own,
// everything about the open note was declared on it: which note is open, every
// transition into another one, saving, the conflict and recovery questions,
// and the status line the answers are reported through. Twenty-six other QML
// files reached back into that window to use any of it, which made "open this
// note" a request only the application's one window could serve. The visible
// cost was qml/QuickCaptureWindow.qml, the editor's other window, which writes
// through NoteCollection.captureNote() directly because none of it was
// reachable from a window that is not the main one.
//
// This suite is what says that is over. It builds a host that is a window and
// nothing else — no pane, no menu, no toolbar, no view state, and no
// qml/main.qml — and drives a whole note session through it: opening a note by
// path, saving it, moving back and forward through the history, answering a
// change made on disk by another program, and reading back a status message.
// None of it could be written before.
class TestNoteSession : public QObject
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

    // The host, built fresh for each case so no case inherits another's open
    // note, history or conflict.
    void init()
    {
        QQmlComponent component(&m_engine);
        component.setData(kMinimalHost,
                          QUrl(QStringLiteral("inmemory:minimal-host.qml")));
        QTRY_VERIFY(component.status() != QQmlComponent::Loading);
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        m_host.reset(component.create(m_engine.rootContext()));
        QVERIFY2(m_host, qPrintable(component.errorString()));
        m_session = m_host->property("session").value<QObject *>();
        QVERIFY(m_session);
    }

    void cleanup()
    {
        m_session = nullptr;
        m_host.reset();
    }

    // The claim in one case: neither the host nor the session has any part of
    // the editor screen. A session that read a pane width or the focus mode
    // would be a session only the full editor window could own, which is the
    // arrangement this type exists to end.
    void neitherTheHostNorTheSessionHasAnyPartOfTheScreen()
    {
        for (const QString &member : kScreenMembers) {
            const QByteArray name = member.toUtf8();
            QVERIFY2(!m_session->property(name.constData()).isValid(),
                     qPrintable(QStringLiteral(
                         "NoteSession resolves the screen member \"%1\"")
                             .arg(member)));
            QVERIFY2(!m_host->property(name.constData()).isValid(),
                     qPrintable(QStringLiteral(
                         "the minimal host declares the screen member \"%1\"")
                             .arg(member)));
        }
        // And it is a window rather than something pretending to be one.
        QVERIFY(m_host->inherits("QQuickWindow"));
        // The session knows there is a collection without being told by a pane.
        QVERIFY(m_session->property("collectionOpen").toBool());
    }

    void thisHostOpensANoteByPath()
    {
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("one.md")).toBool());
        QCOMPARE(m_session->property("currentNoteRelPath").toString(),
                 QStringLiteral("one.md"));
        QCOMPARE(m_context->documentManager()->currentFilePath(),
                 notePath(QStringLiteral("one.md")));
        QCOMPARE(m_context->blockModel()->getContent(0),
                 QStringLiteral("first note"));
    }

    void thisHostSavesTheNoteItOpened()
    {
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("one.md")).toBool());
        m_context->blockModel()->updateContent(
            0, QStringLiteral("edited through the session"));
        QVERIFY(m_context->documentManager()->isDirty());

        call(m_session, "saveCurrentDocument", false);

        QTRY_VERIFY(!m_context->documentManager()->isDirty());
        QCOMPARE(readAll(notePath(QStringLiteral("one.md"))),
                 QStringLiteral("edited through the session\n"));
    }

    void thisHostMovesBackAndForwardThroughTheNoteHistory()
    {
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("one.md")).toBool());
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("two.md")).toBool());
        QCOMPARE(m_session->property("currentNoteRelPath").toString(),
                 QStringLiteral("two.md"));

        call0(m_session, "navigateBack");
        QTRY_COMPARE(m_session->property("currentNoteRelPath").toString(),
                     QStringLiteral("one.md"));

        call0(m_session, "navigateForward");
        QTRY_COMPARE(m_session->property("currentNoteRelPath").toString(),
                     QStringLiteral("two.md"));
    }

    // The open note changed on disk while this host was holding unsaved edits
    // to it (features.md §12.1). Both versions are real work, so the session
    // raises the question rather than choosing; the host's answer is one of the
    // two functions below. There is no banner here — a banner is something a
    // window draws, and the flags it would be drawn from are the session's.
    void thisHostAnswersAChangeMadeOnDisk()
    {
        const QString path = notePath(QStringLiteral("two.md"));
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("two.md")).toBool());
        m_context->blockModel()->updateContent(0, QStringLiteral("my version"));
        QVERIFY(m_context->documentManager()->isDirty());

        writeAll(path, QStringLiteral("their version\n"));
        call(m_session, "noteChangedOnDisk", path);

        QVERIFY(m_session->property("externalConflict").toBool());
        QCOMPARE(m_session->property("conflictPath").toString(), path);

        // Taking the disk version clears the question and loads the bytes the
        // other program wrote.
        // No argument: the session falls back to the path it recorded when it
        // raised the question, which is the route the host's banner takes.
        QVERIFY(call(m_session, "loadTheirs", QVariant()).toBool());
        QVERIFY(!m_session->property("externalConflict").toBool());
        QCOMPARE(m_context->blockModel()->getContent(0),
                 QStringLiteral("their version"));
    }

    void thisHostReceivesATransientStatusMessage()
    {
        QCOMPARE(m_session->property("transientStatus").toString(), QString());
        call(m_session, "showTransientStatus",
             QStringLiteral("Updated 3 links in 2 notes"));
        QCOMPARE(m_session->property("transientStatus").toString(),
                 QStringLiteral("Updated 3 links in 2 notes"));
    }

    // A wiki-link that resolves to nothing creates the note and says so — the
    // one path that proves the session reports through its own status line
    // rather than through a window that would have to exist to hear it.
    void aDanglingWikiLinkIsCreatedAndReportedWithNoWindowToReportTo()
    {
        QVERIFY(call(m_session, "openNoteByPath",
                     QStringLiteral("one.md")).toBool());
        call(m_session, "followWikiLink", QStringLiteral("A brand new note"));

        QTRY_COMPARE(m_session->property("currentNoteRelPath").toString(),
                     QStringLiteral("A brand new note.md"));
        QVERIFY(m_session->property("transientStatus").toString()
                    .contains(QStringLiteral("A brand new note")));
    }

private:
    QString notePath(const QString &name) const
    {
        return QDir(m_vault.path()).absoluteFilePath(name);
    }

    QTemporaryDir m_settings;
    QTemporaryDir m_vault;
    QQmlApplicationEngine m_engine;
    std::unique_ptr<ProcessServices> m_globals;
    std::unique_ptr<AppContext> m_context;
    std::unique_ptr<QObject> m_host;
    QObject *m_session = nullptr;
};

QTEST_MAIN(TestNoteSession)
#include "test_notesession.moc"
