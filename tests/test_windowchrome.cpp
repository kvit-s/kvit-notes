// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "notecollection.h"
#include "extensionregistry.h"
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

// The QML a module puts in the two slots this suite is about. Each is a strip
// with one keyboard-reachable button in it, which is all that is needed to ask
// whether the window is drawing the strip, whether Tab can get into it and
// whether a screen reader is offered it.
//
// Written to a temporary directory when the suite starts and loaded from
// there. A module's QML normally lives in the module's own resource, and this
// suite has no resource of its own on purpose: it must load the shipped
// qml/main.qml and nothing that stands in for it.
constexpr const char *kModuleBottomBarQml = R"(
    import QtQuick
    import QtQuick.Controls
    Rectangle {
        objectName: "moduleBottomBarBody"
        implicitHeight: 28
        color: "#2d3a4a"
        Button {
            objectName: "moduleBottomBarButton"
            anchors.centerIn: parent
            text: "Module bottom bar action"
        }
    }
)";

constexpr const char *kModuleDockPaneQml = R"(
    import QtQuick
    import QtQuick.Controls
    Rectangle {
        objectName: "moduleDockPaneBody"
        color: "#22303f"
        Button {
            objectName: "moduleDockPaneButton"
            anchors.centerIn: parent
            text: "Module dock pane action"
        }
    }
)";

// A region belonging to the application that composed this window: one item
// with one keyboard-reachable control in it, put into the window's content
// item the way a host inheriting from this ApplicationWindow puts its own.
// Sized rather than anchored, because it is created without a parent and an
// anchor to one would be a warning this suite counts.
constexpr const char *kHostRegionQml = R"(
    import QtQuick
    import QtQuick.Controls
    Rectangle {
        objectName: "hostRegion"
        width: 240
        height: 60
        color: "#101820"
        Button {
            objectName: "hostRegionButton"
            anchors.centerIn: parent
            text: "Host region action"
        }
    }
)";

// A module that fills both slots, so the window has all three pieces of chrome
// to draw. Without one the bottom bar is an empty Loader and the bottom dock
// has no tabs, and a suite about hiding them would be hiding nothing.
class ChromeModule : public KvitExtension
{
public:
    explicit ChromeModule(const QString &qmlDir) : m_qmlDir(qmlDir) {}

    QString name() const override { return QStringLiteral("chrome-demo"); }
    QString qmlNamespace() const override { return QStringLiteral("chromedemo"); }

    QString qmlSlot(const QString &slot) const override
    {
        if (slot == QLatin1String(KvitSlots::BottomBar))
            return fileUrl(QStringLiteral("ModuleBottomBar.qml"));
        return QString();
    }

    QVariantList bottomDockTabs() const override
    {
        return {QVariantMap{
            {QStringLiteral("id"), QStringLiteral("chromedemo.output")},
            {QStringLiteral("title"), QStringLiteral("Module output")},
            {QStringLiteral("source"), fileUrl(QStringLiteral("ModuleDockPane.qml"))}}};
    }

private:
    QString fileUrl(const QString &name) const
    {
        return QUrl::fromLocalFile(QDir(m_qmlDir).filePath(name)).toString();
    }

    QString m_qmlDir;
};

// The names a person operating the window through an assistive technology is
// offered for each piece of chrome. The first two are the shipped window's
// own; the third is the module's button above.
const QString kToolbarNode = QStringLiteral("Insert block");
const QString kDockNode = QStringLiteral("Bottom dock tab: Module output");
const QString kBottomBarNode = QStringLiteral("Module bottom bar action");
// And of the content area: the one block this suite's document holds, which a
// screen reader is offered as an editable text field naming its kind.
const QString kContentNode = QStringLiteral("Paragraph block");

// Warnings the shell emits while this suite runs, captured the way
// tests/test_shell.cpp captures them and for the same reason: QML reports a
// binding it could not resolve as a warning and then carries on with an
// undefined value, so a visibility property nothing reads would pass every
// assertion below about an item that is not drawn.
QStringList g_warnings;
QtMessageHandler g_previousHandler = nullptr;

void capturingHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        // The runner's problems rather than the shell's: see test_shell.cpp.
        if (!message.contains(QLatin1String("pipewire"))
            && !message.contains(QLatin1String("PulseAudio"), Qt::CaseInsensitive)
            && !message.contains(QLatin1String("font family aliases")))
            g_warnings << message;
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

// Call a function declared in QML. Everything a QML document declares has the
// signature QVariant f(QVariant...), so the arguments and the result go
// through QVariant whatever they are written as.
QVariant call(QObject *target, const char *name, const QVariant &argument)
{
    QVariant result;
    if (!QMetaObject::invokeMethod(target, name, Q_RETURN_ARG(QVariant, result),
                                   Q_ARG(QVariant, argument)))
        return QVariant();
    return result;
}

QVariant call0(QObject *target, const char *name)
{
    QVariant result;
    if (!QMetaObject::invokeMethod(target, name, Q_RETURN_ARG(QVariant, result)))
        return QVariant();
    return result;
}

// Where the keyboard focus is, for a failure message: an item's objectName if
// it has one, and otherwise the QML type it was declared as.
QString describe(QQuickItem *item)
{
    if (!item)
        return QStringLiteral("nothing");
    const QString name = item->objectName();
    return name.isEmpty() ? QString::fromLatin1(item->metaObject()->className())
                          : name;
}

bool isInside(QQuickItem *item, QQuickItem *ancestor)
{
    for (QQuickItem *walk = item; walk; walk = walk->parentItem())
        if (walk == ancestor)
            return true;
    return false;
}

// Every item Tab, or Backtab, can stop on, starting from where the editor left
// the keyboard focus.
//
// Walked with the call Qt's own Tab handling uses rather than by pressing the
// key, because the block being edited is a text field and a text field keeps
// Tab for itself: pressing it there inserts a tab character and moves no
// focus, so a key-driven walk would prove nothing about what the chain
// contains. The walk skips exactly what Tab skips — an item that is disabled,
// one that does not take focus from the keyboard, and one that is not drawn.
QList<QQuickItem *> focusChainFrom(QQuickItem *start, bool forward)
{
    QList<QQuickItem *> visited;
    QQuickItem *item = start;
    for (int step = 0; step < 500 && item; ++step) {
        QQuickItem *next = item->nextItemInFocusChain(forward);
        if (!next || next == start || visited.contains(next))
            break;
        visited << next;
        item = next;
    }
    return visited;
}

// Depth-first over the accessibility tree, collecting the name of every node
// it offers. A node the window is not currently showing ends the descent, the
// way tests/test_shell.cpp ends it: an assistive technology is not offered a
// pane that is switched off, or anything inside it.
void collectAccessibleNames(QAccessibleInterface *node, QStringList *names)
{
    if (!node || !node->isValid() || node->state().invisible)
        return;
    const QString name = node->text(QAccessible::Name).trimmed();
    if (!name.isEmpty())
        names->append(name);
    const int count = node->childCount();
    for (int i = 0; i < count; ++i)
        collectAccessibleNames(node->child(i), names);
}

} // namespace

// The window's chrome, hidden by the application that composes it.
//
// qml/main.qml is an ApplicationWindow that an application can compose into
// something larger and draw its own chrome around. Most of what the window
// draws already has a property saying whether it should be drawn — the panels,
// the navigation rails, the outline and backlinks panes, the status bar — and
// this suite is about the three that did not: the toolbar, the bottom bar a
// linked module fills, and the bottom dock. A host with a toolbar of its own
// had no supported way to say so, and was reduced to finding the item by its
// objectName and assigning over the binding that would otherwise have brought
// it back when focus mode ended.
//
// Turning an item off has to do more than stop painting it. A pane that is not
// drawn must also be out of the F6 region cycle, out of the Tab chain, absent
// from the accessibility tree, and not still holding the keyboard focus — five
// things that agree in the shipped window and that a new way of hiding an item
// can break one at a time.
//
// The window is the shipped one, loaded from qrc:/qt/qml/Kvit/main.qml, with a
// demonstration module installed so that all three pieces of chrome exist to
// be hidden.
class TestWindowChrome : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_moduleDir.isValid());
        QVERIFY(m_settingsDir.isValid());
        writeQml(QStringLiteral("ModuleBottomBar.qml"), kModuleBottomBarQml);
        writeQml(QStringLiteral("ModuleDockPane.qml"), kModuleDockPaneQml);

        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();

        m_globals = std::make_unique<ProcessServices>(headlessOptions());
        m_globals->openSettings(
            m_settingsDir.filePath(QStringLiteral("settings.json")));
        // Installed before the shell loads, exactly as a module's main() would:
        // the bottom-bar Loader resolves its source as it is created, and the
        // dock reads its tabs once when it is completed.
        m_globals->extensions()->install(std::make_unique<ChromeModule>(
            m_moduleDir.path()));

        m_context = std::make_unique<AppContext>(*m_globals);
        m_context->installContextProperties(&m_engine);

        g_warnings.clear();
        g_previousHandler = qInstallMessageHandler(capturingHandler);
        m_engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kvit/main.qml")));
        QCoreApplication::processEvents();
        QVERIFY(!m_engine.rootObjects().isEmpty());
        m_warningsAfterLoad = g_warnings.size();

        // One paragraph, so that focusing the editor has somewhere to put the
        // caret. An empty document gives the focus nowhere to go, and "the
        // focus ended up in the editor" would then be unprovable.
        m_context->blockModel()->insertBlock(0, Block::Paragraph,
                                             QStringLiteral("a paragraph"));
        QAccessible::setActive(true);
        QCoreApplication::processEvents();
    }

    void cleanupTestCase()
    {
        if (g_previousHandler)
            qInstallMessageHandler(g_previousHandler);
    }

    void init()
    {
        // Every case starts from the window nobody has composed: all three
        // pieces of chrome asked for, no focus mode, the dock expanded, and
        // the keyboard in the editor.
        QQuickWindow *w = window();
        QVERIFY(w);
        w->setProperty("focusMode", false);
        w->setProperty("toolbarVisible", true);
        w->setProperty("extensionBottomBarVisible", true);
        w->setProperty("bottomDockVisible", true);
        w->setProperty("contentAreaVisible", true);
        w->setProperty("bottomDockCollapsed", false);
        w->setProperty("focusedPane", 2);
        call0(w, "focusEditor");
        QCoreApplication::processEvents();
    }

    // The default, which is the shipped editor: nobody has composed this
    // window, so it draws all three and the keyboard reaches all three.
    void withNoHostComposingItTheWindowDrawsAllThree()
    {
        QQuickWindow *w = window();
        QVERIFY(w->property("toolbarVisible").toBool());
        QVERIFY(w->property("extensionBottomBarVisible").toBool());
        QVERIFY(w->property("bottomDockVisible").toBool());

        for (const char *name : {"toolbar", "extensionBottomBar", "bottomDock"}) {
            QQuickItem *chrome = item(name);
            QVERIFY2(chrome, name);
            QVERIFY2(chrome->isVisible(), name);
            QVERIFY2(chrome->height() > 0.0, name);
        }

        // F6 stops on the toolbar and on the dock, which is the whole reason
        // they are in the cycle: the toolbar's Insert, View and File menus and
        // the dock's tabs have no other keyboard route.
        const QList<int> cycle = paneCycle();
        QVERIFY2(cycle.contains(3), "F6 reaches the toolbar");
        QVERIFY2(cycle.contains(4), "F6 reaches the bottom dock");
        QVERIFY2(cycle.contains(2), "F6 reaches the editor");

        // And Tab reaches all three, which is what makes the same claim for a
        // hidden one mean something.
        const QList<QQuickItem *> forward = tabChainFromTheEditor(true);
        QVERIFY2(!forward.isEmpty(), "Tab from the editor moves the focus at all");
        QVERIFY2(chainReaches(forward, item("toolbar")), "Tab reaches the toolbar");
        QVERIFY2(chainReaches(forward, item("extensionBottomBar")),
                 "Tab reaches the module's bottom bar");
        QVERIFY2(chainReaches(forward, item("bottomDock")),
                 "Tab reaches the bottom dock");

        const QStringList names = accessibleNames();
        QVERIFY2(names.contains(kToolbarNode), qPrintable(kToolbarNode));
        QVERIFY2(names.contains(kDockNode), qPrintable(kDockNode));
        QVERIFY2(names.contains(kBottomBarNode), qPrintable(kBottomBarNode));
    }

    // Focus mode is the window deciding it wants no chrome, and it still
    // decides that for all three — and gives all three back on the way out.
    void focusModeStillHidesAllThreeAndGivesThemBack()
    {
        QQuickWindow *w = window();
        w->setProperty("focusMode", true);
        QTRY_VERIFY(!item("toolbar")->isVisible());
        QVERIFY(!item("extensionBottomBar")->isVisible());
        QVERIFY(!item("bottomDock")->isVisible());

        // Nothing to stop on but the editor while the chrome is gone.
        const QList<int> hidden = paneCycle();
        QCOMPARE(hidden, QList<int>{2});

        w->setProperty("focusMode", false);
        QTRY_VERIFY(item("toolbar")->isVisible());
        QVERIFY(item("extensionBottomBar")->isVisible());
        QVERIFY(item("bottomDock")->isVisible());
        const QList<int> back = paneCycle();
        QVERIFY(back.contains(3));
        QVERIFY(back.contains(4));
    }

    void aHostThatDrawsItsOwnToolbarGetsNone()
    {
        verifyHostCanHide("toolbarVisible", "toolbar", 3, kToolbarNode,
                          "toolbarInsertButton");
        if (QTest::currentTestFailed())
            return;
        // The strip reserves no height, so the editor column starts at the top
        // of the window where the host's own toolbar would otherwise be
        // stacked on top of this one.
        QCOMPARE(item("toolbar")->height(), 0.0);
    }

    void aHostThatPlacesAModulesBarItselfGetsNone()
    {
        // Not one of the F6 regions — the bar belongs to the module that fills
        // it — so there is no pane number to check, which -1 says.
        const qreal chromeWithBar = window()->property("bottomChromeHeight").toReal();
        verifyHostCanHide("extensionBottomBarVisible", "extensionBottomBar", -1,
                          kBottomBarNode, "moduleBottomBarButton");
        if (QTest::currentTestFailed())
            return;
        // The Loader keeps the height of the item it loaded, since it has not
        // unloaded it; what has to change is the space the window reserves
        // along its foot for bottom chrome.
        const qreal chromeWithout = window()->property("bottomChromeHeight").toReal();
        QVERIFY2(chromeWithout < chromeWithBar,
                 qPrintable(QStringLiteral("bottom chrome is still %1, was %2")
                                .arg(chromeWithout).arg(chromeWithBar)));
    }

    void aHostThatDocksAModulesPanesItselfGetsNoDock()
    {
        verifyHostCanHide("bottomDockVisible", "bottomDock", 4, kDockNode,
                          "bottomDockTabBar");
        if (QTest::currentTestFailed())
            return;
        QCOMPARE(item("bottomDock")->height(), 0.0);

        // And the key that collapses the dock is not offered either: toggling
        // something nobody can see would do nothing but write a setting.
        QObject *dock = window()->findChild<QObject *>(QStringLiteral("bottomDock"));
        QVERIFY(dock);
        bool found = false;
        const QList<QObject *> shortcuts = dock->findChildren<QObject *>();
        for (QObject *child : shortcuts) {
            if (child->property("sequence").toString() != QLatin1String("Ctrl+J"))
                continue;
            found = true;
            QVERIFY2(!child->property("enabled").toBool(),
                     "Ctrl+J still reaches a dock the host is not drawing");
        }
        QVERIFY2(found, "the dock declares its Ctrl+J shortcut");
    }

    // Focus mode is the window's own reason and it wins: a host asking for all
    // three back while focus mode is on gets none of them.
    void focusModeBeatsAHostAskingForTheChromeBack()
    {
        QQuickWindow *w = window();
        // Turned off first, so that asking for them back while focus mode is
        // running is a change the bindings see rather than a no-op: every case
        // starts with all three already asked for.
        w->setProperty("toolbarVisible", false);
        w->setProperty("extensionBottomBarVisible", false);
        w->setProperty("bottomDockVisible", false);
        w->setProperty("focusMode", true);
        QTRY_VERIFY(!item("toolbar")->isVisible());

        w->setProperty("toolbarVisible", true);
        w->setProperty("extensionBottomBarVisible", true);
        w->setProperty("bottomDockVisible", true);
        QCoreApplication::processEvents();

        QVERIFY2(!item("toolbar")->isVisible(),
                 "the host got the toolbar back in focus mode");
        QVERIFY2(!item("extensionBottomBar")->isVisible(),
                 "the host got the module's bottom bar back in focus mode");
        QVERIFY2(!item("bottomDock")->isVisible(),
                 "the host got the bottom dock back in focus mode");
    }

    // The dock's other reason, which is not the host's either: there is
    // nothing docked in it. Proved on a second composition with no module
    // installed, since that is the only way to have an empty dock — this
    // suite's own window has a module in it from the first line.
    void nothingDockedKeepsTheDockOutOfTheWindow()
    {
        QTemporaryDir settings;
        QVERIFY(settings.isValid());
        ProcessServices globals(headlessOptions());
        globals.openSettings(settings.filePath(QStringLiteral("settings.json")));
        AppContext context(globals);
        QQmlApplicationEngine engine;
        context.installContextProperties(&engine);
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kvit/main.qml")));
        QCoreApplication::processEvents();
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *bare = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(bare);

        // The host asking for the dock as loudly as it can.
        QVERIFY(bare->property("bottomDockVisible").toBool());
        bare->setProperty("bottomDockVisible", true);
        QCoreApplication::processEvents();

        QQuickItem *dock = bare->findChild<QQuickItem *>(QStringLiteral("bottomDock"));
        QVERIFY(dock);
        QVERIFY2(!dock->isVisible(), "an empty dock appeared because a host asked");
        QCOMPARE(dock->height(), 0.0);

        // And F6 does not stop on it, for the same reason it does not stop on
        // one the host turned off.
        bare->setProperty("focusedPane", 2);
        QVERIFY(!paneCycleOf(bare).contains(4));
    }

    // The content area — the pane a note is drawn and edited in — is the sixth
    // thing a host can turn off, and the one an application composing this
    // window is likeliest to replace, since drawing a document view of its own
    // is usually why it composed the window at all. Proved the way the three
    // pieces of chrome above are proved: not drawn, out of the F6 region
    // cycle, out of the Tab chain in both directions, and absent from the
    // accessibility tree.
    //
    // The Tab chain is walked from the toolbar rather than from the editor
    // here, because the walk has to start from something that is drawn and in
    // this case the editor is what is not.
    void aHostThatDrawsItsOwnDocumentViewGetsNoContentArea()
    {
        QQuickWindow *w = window();
        QQuickItem *pane = item("documentPane");
        QVERIFY(pane);

        // Drawn until a host says otherwise, and reachable while it is. Every
        // claim below is also true of a window that never drew the pane at
        // all, so this is what makes them mean something.
        QVERIFY(w->property("contentAreaVisible").toBool());
        QVERIFY(pane->isVisible());
        QVERIFY2(paneCycle().contains(2), "F6 reaches the content area");
        QVERIFY2(chainReaches(tabChainFrom("toolbarInsertButton", true), pane),
                 "Tab reaches the content area");
        QVERIFY2(accessibleNames().contains(kContentNode),
                 qPrintable(kContentNode));

        w->setProperty("contentAreaVisible", false);
        QTRY_VERIFY2(!pane->isVisible(), "contentAreaVisible");

        // F6 stops on what is left, and no longer on the content area.
        const QList<int> cycle = paneCycle();
        QVERIFY2(!cycle.contains(2), "F6 still stops on the content area");
        QVERIFY2(cycle.contains(3), "F6 stopped reaching the toolbar too");
        QVERIFY2(cycle.contains(4), "F6 stopped reaching the bottom dock too");

        const QList<QQuickItem *> forward = tabChainFrom("toolbarInsertButton", true);
        QVERIFY2(!forward.isEmpty(), "Tab moves the focus at all");
        QVERIFY2(!chainReaches(forward, pane),
                 "Tab still enters the content area");
        QVERIFY2(!chainReaches(tabChainFrom("toolbarInsertButton", false), pane),
                 "Backtab still enters the content area");

        QVERIFY2(!accessibleNames().contains(kContentNode),
                 qPrintable(QStringLiteral("the accessibility tree still offers "
                                           "\"%1\"").arg(kContentNode)));

        // The content area's other two surfaces go with it. Which one of the
        // three is current is the window's own answer, in `contentView`, and a
        // host that is drawing its own document view is drawing over whichever
        // it happens to be — so opening a source file into a content area the
        // host turned off must not put the window's viewer back on screen.
        for (const char *view : {"text", "media"}) {
            w->setProperty("contentView", view);
            const char *surface = qstrcmp(view, "text") == 0 ? "textFilePane"
                                                             : "standaloneFilePane";
            QTRY_VERIFY2(item(surface) && !item(surface)->isVisible(), surface);
        }
        w->setProperty("contentView", "document");
    }

    // A host drawing its own content area draws its own regions with it, and
    // those regions hold the keyboard. Hiding a piece of the window's chrome
    // must not disturb that: the focus was never in the item being hidden, so
    // there is nothing to recover from and nothing to move.
    //
    // What this case guards is the recovery staying out of the way — a
    // recovery that moved the focus whenever a host turned something off,
    // rather than only when the focus had nowhere to be, would take a region
    // of the host's own away from the reader on every toggle. The two cases
    // below are the ones that say where the focus goes when it does have to
    // move.
    void hidingChromeLeavesTheHostsOwnFocusWhereItWas()
    {
        QQuickWindow *w = window();
        std::unique_ptr<QQuickItem> host = createHostRegion();
        QVERIFY(host);
        QQuickItem *hostControl =
            host->findChild<QQuickItem *>(QStringLiteral("hostRegionButton"));
        QVERIFY(hostControl);

        w->setProperty("contentAreaVisible", false);
        QTRY_VERIFY(!item("documentPane")->isVisible());

        for (const char *property : {"toolbarVisible", "extensionBottomBarVisible",
                                     "bottomDockVisible"}) {
            hostControl->forceActiveFocus(Qt::TabFocusReason);
            QTRY_COMPARE(w->activeFocusItem(), hostControl);

            w->setProperty(property, false);
            QCoreApplication::processEvents();
            // The recovery runs through Qt.callLater, so the frame after the
            // change is where it would take the focus away.
            QTest::qWait(50);
            QCoreApplication::processEvents();

            QVERIFY2(w->activeFocusItem() == hostControl,
                     qPrintable(QStringLiteral("hiding %1 moved the keyboard "
                                               "from the host's own region to %2")
                                    .arg(QLatin1String(property),
                                         describe(w->activeFocusItem()))));
            w->setProperty(property, true);
            QCoreApplication::processEvents();
        }
    }

    // And when the focus really was in the item being hidden, with no content
    // area to fall back on: it goes to another region the window is drawing —
    // here the bottom dock — and never into the pane that is not on screen.
    void withNoContentAreaTheFocusMovesToAPaneThatIsDrawn()
    {
        QQuickWindow *w = window();
        QQuickItem *pane = item("documentPane");
        w->setProperty("contentAreaVisible", false);
        QTRY_VERIFY(!pane->isVisible());

        call(w, "focusPane", 3);
        QTRY_VERIFY2(isInside(w->activeFocusItem(), item("toolbar")),
                     "the focus never reached the toolbar");

        w->setProperty("toolbarVisible", false);
        QTRY_VERIFY(!item("toolbar")->isVisible());

        QTRY_VERIFY2(w->activeFocusItem() && w->activeFocusItem()->isVisible()
                         && isInside(w->activeFocusItem(), item("bottomDock")),
                     qPrintable(QStringLiteral("hiding the toolbar left the "
                                               "keyboard on %1")
                                    .arg(describe(w->activeFocusItem()))));
        QVERIFY2(!isInside(w->activeFocusItem(), pane),
                 "the focus went into the content area the host is not drawing");

        // Asking for the pane that has just been hidden is refused the same
        // way. focusPane() has always fallen back to the content area for a
        // pane it will not focus, and that fallback asks whether the content
        // area is drawn before it takes it.
        call(w, "focusPane", 3);
        QCoreApplication::processEvents();
        QVERIFY2(!isInside(w->activeFocusItem(), pane),
                 qPrintable(QStringLiteral("asking for the hidden toolbar put "
                                           "the keyboard on %1")
                                .arg(describe(w->activeFocusItem()))));
    }

    // With every region of the window's own turned off as well, there is
    // nowhere of the window's left to put the focus. It is dropped rather than
    // pushed into something invisible: the window's own shortcuts are bound on
    // the window and still arrive, and a host's region keeps the keystrokes it
    // was getting.
    void withNothingLeftToFocusTheKeyboardIsNotLeftInsideAHiddenPane()
    {
        QQuickWindow *w = window();
        QQuickItem *pane = item("documentPane");
        w->setProperty("contentAreaVisible", false);
        w->setProperty("extensionBottomBarVisible", false);
        QTRY_VERIFY(!pane->isVisible());

        call(w, "focusPane", 4);
        QTRY_VERIFY2(isInside(w->activeFocusItem(), item("bottomDock")),
                     "the focus never reached the bottom dock");

        w->setProperty("bottomDockVisible", false);
        w->setProperty("toolbarVisible", false);
        QTRY_VERIFY(!item("bottomDock")->isVisible());
        QTRY_VERIFY(!item("toolbar")->isVisible());

        QTRY_VERIFY2(!w->activeFocusItem() || w->activeFocusItem()->isVisible(),
                     qPrintable(QStringLiteral("the keyboard was left on %1, "
                                               "which is not drawn")
                                    .arg(describe(w->activeFocusItem()))));
        QVERIFY2(!isInside(w->activeFocusItem(), pane),
                 "the focus went into the content area the host is not drawing");
    }

    // The two columns down the left, which answer the same way the rest do:
    // from the item, so that every reason it is off screen counts. A column is
    // gone because the reader collapsed it, because no collection is open,
    // because focus mode is running, or because a host turned the panels off,
    // and only the item knows all four.
    //
    // On a composition of its own, because this suite's own window has no
    // collection open and a window without one draws neither column whatever
    // any property says.
    void aColumnTheHostTurnedOffIsNotOfferedEither()
    {
        QTemporaryDir settings;
        QTemporaryDir vault;
        QVERIFY(settings.isValid());
        QVERIFY(vault.isValid());
        ProcessServices globals(headlessOptions());
        globals.openSettings(settings.filePath(QStringLiteral("settings.json")));
        AppContext context(globals);
        QQmlApplicationEngine engine;
        context.installContextProperties(&engine);
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/Kvit/main.qml")));
        QCoreApplication::processEvents();
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(w);

        QVERIFY(context.noteCollection()->openRoot(vault.path()));
        QCoreApplication::processEvents();
        QVERIFY2(w->property("collectionOpen").toBool(),
                 "the window never saw the collection open");

        QQuickItem *sidebar = w->findChild<QQuickItem *>(QStringLiteral("sidebar"));
        QQuickItem *noteList = w->findChild<QQuickItem *>(QStringLiteral("noteListPane"));
        QVERIFY(sidebar);
        QVERIFY(noteList);
        QTRY_VERIFY(sidebar->isVisible());
        QVERIFY(noteList->isVisible());
        const QList<int> shown = paneCycleOf(w);
        QVERIFY2(shown.contains(0), "F6 reaches the sidebar");
        QVERIFY2(shown.contains(1), "F6 reaches the note list");

        // `panelsVisible` takes both columns away without collapsing either
        // one, so a pane answering from `sidebarCollapsed` would still call
        // them drawn.
        w->setProperty("panelsVisible", false);
        QTRY_VERIFY(!sidebar->isVisible());
        QVERIFY(!noteList->isVisible());
        QVERIFY(!w->property("sidebarCollapsed").toBool());
        QVERIFY(!w->property("noteListCollapsed").toBool());

        const QList<int> hidden = paneCycleOf(w);
        QVERIFY2(!hidden.contains(0), "F6 still stops on a sidebar nobody can see");
        QVERIFY2(!hidden.contains(1), "F6 still stops on a note list nobody can see");
        QVERIFY2(hidden.contains(2), "F6 stopped reaching the content area too");

        // And asking for one directly is refused, which leaves the keyboard in
        // the content area — the fallback focusPane() has for a pane it will
        // not focus.
        call(w, "focusPane", 0);
        QCoreApplication::processEvents();
        QVERIFY2(!isInside(w->activeFocusItem(), sidebar),
                 "asking for the hidden sidebar put the keyboard in it");
    }

    // Declared last on purpose: QtTest runs test functions in declaration
    // order, so this sees everything the cases above provoked.
    void noWarningsAppearAfterTheShellHasLoaded()
    {
        if (m_warningsAfterLoad > 0)
            QFAIL(qPrintable(QStringLiteral("Loading the shell produced "
                                            "warning(s):\n  ")
                             + g_warnings.mid(0, m_warningsAfterLoad)
                                   .join(QStringLiteral("\n  "))));
        if (g_warnings.size() > m_warningsAfterLoad)
            QFAIL(qPrintable(QStringLiteral("Exercising the window produced "
                                            "warning(s):\n  ")
                             + g_warnings.mid(m_warningsAfterLoad)
                                   .join(QStringLiteral("\n  "))));
    }

private:
    // The five things hiding one piece of chrome has to do, whichever piece it
    // is: stop drawing it, drop it from the F6 cycle (for the two that are
    // regions), drop it from the Tab chain in both directions, stop offering
    // it to an assistive technology, and hand the keyboard focus back to
    // something the reader can see.
    //
    // `pane` is the F6 region number, or -1 for an item that is not a region.
    // `focusTarget` is a control inside the item, used to put the focus there
    // before it is hidden.
    void verifyHostCanHide(const char *property, const char *itemName, int pane,
                           const QString &accessibleName, const char *focusTarget)
    {
        QQuickWindow *w = window();
        QQuickItem *chrome = item(itemName);
        QVERIFY2(chrome, itemName);
        QQuickItem *editor = item("blockEditor");
        QVERIFY(editor);

        // Drawn and reachable to begin with. Without this every proof below
        // would also pass on a window that never drew the item at all.
        QVERIFY2(chrome->isVisible(), itemName);
        QVERIFY2(accessibleNames().contains(accessibleName),
                 qPrintable(accessibleName));
        QVERIFY2(chainReaches(tabChainFromTheEditor(true), chrome), itemName);
        if (pane >= 0)
            QVERIFY2(paneCycle().contains(pane), itemName);

        // With the keyboard inside it, so that hiding it has a focus to move.
        // The two regions are reached the way F6 reaches them; the module's bar
        // is not a region, so its own button is focused directly.
        if (pane >= 0)
            call(w, "focusPane", pane);
        else
            item(focusTarget)->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY2(isInside(w->activeFocusItem(), chrome),
                     qPrintable(QStringLiteral("the focus never reached %1")
                                    .arg(QLatin1String(itemName))));

        w->setProperty(property, false);

        // Not drawn.
        QTRY_VERIFY2(!chrome->isVisible(), property);

        // The keyboard is somewhere the reader can see, which is the editor:
        // the one pane that is always there. Asked first, before anything
        // below moves the focus itself — walking the Tab chain and cycling the
        // regions both start by focusing the editor, so asking afterwards
        // would be asking about the probe rather than about the window.
        QTRY_VERIFY2(w->activeFocusItem() && w->activeFocusItem()->isVisible()
                         && isInside(w->activeFocusItem(), editor),
                     qPrintable(QStringLiteral("hiding %1 left the keyboard on %2")
                                    .arg(QLatin1String(itemName),
                                         describe(w->activeFocusItem()))));

        // Not in the F6 cycle, while every other region still is.
        if (pane >= 0) {
            const QList<int> cycle = paneCycle();
            QVERIFY2(!cycle.contains(pane), property);
            QVERIFY2(cycle.contains(2), "F6 still reaches the editor");
            const int otherPane = pane == 3 ? 4 : 3;
            QVERIFY2(cycle.contains(otherPane),
                     "F6 stopped reaching the other region too");
        }

        // Not in the Tab chain, in either direction.
        QVERIFY2(!chainReaches(tabChainFromTheEditor(true), chrome),
                 qPrintable(QStringLiteral("Tab still reaches %1")
                                .arg(QLatin1String(itemName))));
        QVERIFY2(!chainReaches(tabChainFromTheEditor(false), chrome),
                 qPrintable(QStringLiteral("Backtab still reaches %1")
                                .arg(QLatin1String(itemName))));

        // Not offered to an assistive technology.
        QVERIFY2(!accessibleNames().contains(accessibleName),
                 qPrintable(QStringLiteral("the accessibility tree still offers "
                                           "\"%1\"").arg(accessibleName)));
    }

    QQuickWindow *window() const
    {
        return qobject_cast<QQuickWindow *>(m_engine.rootObjects().value(0));
    }

    QQuickItem *item(const char *name) const
    {
        return window()->findChild<QQuickItem *>(QLatin1String(name));
    }

    // The regions F6 stops on, in the order it stops on them, starting from
    // the editor and going round until it repeats itself.
    QList<int> paneCycle() const { return paneCycleOf(window()); }

    static QList<int> paneCycleOf(QQuickWindow *w)
    {
        w->setProperty("focusedPane", 2);
        QList<int> seen;
        for (int step = 0; step < 12; ++step) {
            call0(w, "cyclePane");
            const int pane = w->property("focusedPane").toInt();
            if (seen.contains(pane))
                break;
            seen << pane;
        }
        return seen;
    }

    // The host's own region, created in the window's content item the way an
    // application composing this window declares one. Owned by the caller, so
    // that it is gone again before the next case runs.
    std::unique_ptr<QQuickItem> createHostRegion()
    {
        QQmlComponent component(&m_engine);
        component.setData(kHostRegionQml, QUrl());
        if (component.isError()) {
            qWarning() << component.errorString();
            return {};
        }
        std::unique_ptr<QQuickItem> region(
            qobject_cast<QQuickItem *>(component.create()));
        if (region)
            region->setParentItem(window()->contentItem());
        QCoreApplication::processEvents();
        return region;
    }

    // Every stop Tab, or Backtab, makes starting from a named control. The
    // walk has to start somewhere that is drawn, which is why the case that
    // hides the editor starts it in the toolbar instead.
    QList<QQuickItem *> tabChainFrom(const char *name, bool forward) const
    {
        QQuickItem *start = item(name);
        return start ? focusChainFrom(start, forward) : QList<QQuickItem *>();
    }

    QList<QQuickItem *> tabChainFromTheEditor(bool forward) const
    {
        QQuickWindow *w = window();
        call0(w, "focusEditor");
        QCoreApplication::processEvents();
        QQuickItem *start = w->activeFocusItem();
        if (!start || !isInside(start, item("blockEditor")))
            start = item("blockEditor");
        return focusChainFrom(start, forward);
    }

    static bool chainReaches(const QList<QQuickItem *> &chain, QQuickItem *ancestor)
    {
        for (QQuickItem *stop : chain)
            if (isInside(stop, ancestor))
                return true;
        return false;
    }

    QStringList accessibleNames() const
    {
        QStringList names;
        collectAccessibleNames(QAccessible::queryAccessibleInterface(window()),
                               &names);
        return names;
    }

    void writeQml(const QString &name, const char *text)
    {
        QFile file(QDir(m_moduleDir.path()).filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(text);
    }

    QTemporaryDir m_moduleDir;
    QTemporaryDir m_settingsDir;
    // The globals outlive the context, and the context outlives the engine, as
    // they do in KvitApplication.
    std::unique_ptr<ProcessServices> m_globals;
    std::unique_ptr<AppContext> m_context;
    QQmlApplicationEngine m_engine;
    int m_warningsAfterLoad = 0;
};

QTEST_MAIN(TestWindowChrome)
#include "test_windowchrome.moc"
