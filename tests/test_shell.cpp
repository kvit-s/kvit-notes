// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "blockkindregistry.h"
#include "block.h"
#include "blockmodel.h"
#include "extensionregistry.h"

#include <QRegularExpression>
#include <QSet>

namespace {

// Warnings emitted while the shell loads. QML resolves bindings lazily and
// reports every failure — an unknown context property, a type the qrc does
// not carry, a binding loop — as a warning on the message handler and then
// carries on with an undefined value. Loading therefore "succeeds" no matter
// how much of the shell failed to wire up, which is precisely how a renamed
// context property or a resource missing from the qrc used to merge green.
// Capturing the warnings turns each one into a test failure.
QStringList g_loadWarnings;
QtMessageHandler g_previousHandler = nullptr;

void capturingHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        g_loadWarnings << message;
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

} // namespace

// The shell as the application actually composes it: AppContext wired up and
// qml/main.qml loaded from the shipped resource.
//
// Two things are checked here that unit tests cannot see. First, that the
// composition root really does compose a working application — the case that
// gives the app/library split its meaning, since a second binary is supposed
// to get an editor by constructing this one object. Second, that the delegate
// chooser has a delegate for every block type there is: the chooser matches on
// a numeric kind, so a type whose kind no choice claims would silently render
// as an empty row, and no unit test on the model would notice.
class TestShell : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        ExtensionRegistry::instance().clear();
        BlockKindRegistry::instance().reset();

        AppContext::registerQmlTypes();
        m_context = std::make_unique<AppContext>();
        m_context->openSettings(m_dir.filePath(QStringLiteral("settings.json")));
        m_context->installContextProperties(&m_engine);

        g_loadWarnings.clear();
        g_previousHandler = qInstallMessageHandler(capturingHandler);
        m_engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
        // Bindings evaluate as the scene is built; let the queue drain so a
        // late failure is captured too.
        QCoreApplication::processEvents();
        qInstallMessageHandler(g_previousHandler);
    }

    void theComposedContextLoadsTheShell()
    {
        QVERIFY(!m_engine.rootObjects().isEmpty());
    }

    // The composition gate. A context property renamed out from under the
    // shell, a QML file missing from resources.qrc, an import that does not
    // resolve, or a binding that references something that is not there all
    // surface here as a warning rather than as a failed load.
    void loadingTheShellEmitsNoQmlWarnings()
    {
        if (!g_loadWarnings.isEmpty()) {
            QString report = QStringLiteral(
                "Loading qml/main.qml produced %1 warning(s):\n")
                    .arg(g_loadWarnings.size());
            for (const QString &warning : g_loadWarnings)
                report += QStringLiteral("  - ") + warning + QLatin1Char('\n');
            QFAIL(qPrintable(report));
        }
    }

    // The names the shell binds to are a contract between C++ and QML that
    // neither compiler checks. Pinning the published set means a rename has
    // to be made deliberately, in both places, rather than discovered later
    // as an undefined value at runtime.
    void everyPublishedContextPropertyIsAccountedFor()
    {
        static const QStringList expected = {
            "blockModel", "markdownFormatter", "undoStack", "documentManager",
            "clipboard", "blockMenuModel", "mathCommandModel",
            "documentSelection", "documentSearch", "documentOutline",
            "documentStats", "documentExporter", "documentSerializer",
            "noteCollection", "folderTreeModel", "noteListModel",
            "collectionSearch", "startupController", "noteTemplates",
            "documentImporter", "embedMetadata", "appSettings", "perfLog",
            "theme", "typography", "codeLanguageList", "imageAssets",
            "blockAttributes", "shortcutCatalog", "a11y", "systemTray",
            "globalHotkey", "fileWatcher", "navigationHistory", "updateChecker",
            "quickSwitcherModel", "tableTools", "todoMeta", "kanbanTools",
            "queryTools", "mathRenderer", "blockKinds", "extensions",
        };
        const QStringList actual = m_context->installedContextPropertyNames();

        const QSet<QString> expectedSet(expected.begin(), expected.end());
        const QSet<QString> actualSet(actual.begin(), actual.end());
        const QSet<QString> added = actualSet - expectedSet;
        const QSet<QString> removed = expectedSet - actualSet;
        QVERIFY2(added.isEmpty() && removed.isEmpty(),
                 qPrintable(QStringLiteral(
                     "Published context properties changed. Added: [%1]. "
                     "Removed: [%2]. Update the shell's bindings and this "
                     "list together.")
                        .arg(QStringList(added.begin(), added.end()).join(", "),
                             QStringList(removed.begin(), removed.end())
                                 .join(", "))));
        QCOMPARE(actual.size(), expected.size());   // no duplicate publishes
    }

    void withNoModuleInstalledEverySlotIsInert()
    {
        // What the open build looks like: the seams are present and empty, so
        // the shell lays out exactly as it did before they existed.
        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);

        for (const char *name : {"extensionBanner", "extensionBottomBar",
                                 "extensionSidePanel"}) {
            QObject *slot = window->findChild<QObject *>(name);
            QVERIFY2(slot, name);
            QVERIFY2(slot->property("source").toString().isEmpty(), name);
            QVERIFY2(!slot->property("active").toBool(), name);
        }
        // The two horizontal slots reserve no height and the vertical one no
        // width, which is what keeps the editor pane's margins unchanged.
        QCOMPARE(window->findChild<QObject *>("extensionBanner")
                     ->property("height").toReal(), 0.0);
        QCOMPARE(window->findChild<QObject *>("extensionBottomBar")
                     ->property("height").toReal(), 0.0);
        QCOMPARE(window->findChild<QObject *>("extensionSidePanel")
                     ->property("width").toReal(), 0.0);
    }

    void everyBlockTypeGetsADelegate_data()
    {
        QTest::addColumn<int>("blockType");
        QTest::addColumn<QString>("language");

        QTest::newRow("paragraph") << int(Block::Paragraph) << QString();
        QTest::newRow("heading1") << int(Block::Heading1) << QString();
        QTest::newRow("heading2") << int(Block::Heading2) << QString();
        QTest::newRow("heading3") << int(Block::Heading3) << QString();
        // Heading4 was appended after Divider, so it is the type most likely
        // to fall outside a range-based kind rule.
        QTest::newRow("heading4") << int(Block::Heading4) << QString();
        QTest::newRow("bullet") << int(Block::BulletList) << QString();
        QTest::newRow("numbered") << int(Block::NumberedList) << QString();
        QTest::newRow("todo") << int(Block::Todo) << QString();
        QTest::newRow("quote") << int(Block::Quote) << QString();
        QTest::newRow("divider") << int(Block::Divider) << QString();
        QTest::newRow("callout") << int(Block::Callout) << QString();
        QTest::newRow("math") << int(Block::MathBlock) << QString();
        QTest::newRow("table") << int(Block::Table) << QString();
        QTest::newRow("code-plain") << int(Block::CodeBlock) << QString();
        QTest::newRow("code-python") << int(Block::CodeBlock) << QStringLiteral("python");
        // The fence languages that route by the registry.
        QTest::newRow("code-kanban") << int(Block::CodeBlock) << QStringLiteral("kanban");
        QTest::newRow("code-toc") << int(Block::CodeBlock) << QStringLiteral("toc");
        QTest::newRow("code-mermaid") << int(Block::CodeBlock) << QStringLiteral("mermaid");
        QTest::newRow("code-query") << int(Block::CodeBlock) << QStringLiteral("query");
        // A fence language nobody registered must still render — as a plain
        // code block, the way an unknown highlight language always has.
        QTest::newRow("code-unregistered")
            << int(Block::CodeBlock) << QStringLiteral("no-such-language");
    }

    void everyBlockTypeGetsADelegate()
    {
        QFETCH(int, blockType);
        QFETCH(QString, language);

        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);
        QObject *listView = window->findChild<QObject *>("blockListView");
        QVERIFY(listView);

        BlockModel *model = m_context->blockModel();
        // Each case works on a document holding just this block: a ListView
        // creates delegates only for rows near the viewport, so a growing
        // document would report a null row for reasons having nothing to do
        // with the chooser.
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);

        const int index = 0;
        model->insertBlock(index, blockType, QString());
        if (!language.isEmpty()) {
            model->convertBlock(index, blockType, QStringLiteral("```\ntext\n```"),
                                false, language);
        }

        QQuickItem *row = nullptr;
        QTRY_VERIFY2(QMetaObject::invokeMethod(
                         listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row),
                         Q_ARG(int, index)) && row,
                     "the delegate chooser produced no delegate for this block");
        QVERIFY(row->height() > 0);
    }

    // Theme and typography snapshot the store when attached, so they must
    // attach after openSettings() has loaded the file — attached in wire(),
    // before the store opens, a saved dark theme silently came back light.
    // Composing a fresh context over a seeded file is the same startup path
    // KvitApplication runs.
    void persistedAppearanceSettingsApplyAtStartup()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("settings.json"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{ \"theme.id\": \"dark\", \"typography.fontSize\": 19 }");
        }

        AppContext context;
        context.openSettings(path);

        QCOMPARE(context.theme()->themeId(), QStringLiteral("dark"));
        QCOMPARE(context.theme()->resolvedTheme(), QStringLiteral("dark"));
        QCOMPARE(context.typography()->baseSize(), 19);
    }

private:
    QTemporaryDir m_dir;
    // The context outlives the engine, as it does in KvitApplication.
    std::unique_ptr<AppContext> m_context;
    QQmlApplicationEngine m_engine;
};

QTEST_MAIN(TestShell)
#include "test_shell.moc"
