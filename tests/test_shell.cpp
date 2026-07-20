// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QFile>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQmlContext>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "blockkindregistry.h"
#include "block.h"
#include "blockmodel.h"
#include "extensionregistry.h"
#include "perflog.h"
#include "qmlservices.h"

#include <QQmlContext>
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

// A stand-in module that asks for a given QML namespace, so a test can aim
// it at a name the core already publishes.
class NameGrabbingExtension : public KvitExtension
{
public:
    explicit NameGrabbingExtension(const QString &ns) : m_namespace(ns) {}
    QString name() const override { return QStringLiteral("name-grabber"); }
    QString qmlNamespace() const override { return m_namespace; }
    QVariantMap contextObjects() override
    {
        return {{QStringLiteral("marker"), QVariant::fromValue(&m_object)}};
    }

private:
    QString m_namespace;
    QObject m_object;
};

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
        // Nothing to reset: the context owns its registries, so this suite
        // starts from the built-ins whatever else ran in the process.
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

    // The core publishes no context properties at all any more: every service
    // reaches QML as a `Kvit` module singleton, where qmllint checks its uses
    // statically instead of a list here pinning them by hand.
    //
    // The empty expectation is the assertion, not a leftover. A context
    // property added back would be invisible to the lint gate — that is the
    // whole reason they were removed — so the one thing worth checking is
    // that none reappears. Anything genuinely needing to reach QML should be
    // registered in qmlsingletons.h, which the extension registry also
    // reserves names against.
    void everyPublishedContextPropertyIsAccountedFor()
    {
        static const QStringList expected = {
        };
        const QStringList actual = m_context->installedContextPropertyNames();

        const QSet<QString> expectedSet(expected.begin(), expected.end());
        const QSet<QString> actualSet(actual.begin(), actual.end());
        const QSet<QString> added = actualSet - expectedSet;
        const QSet<QString> removed = expectedSet - actualSet;
        QVERIFY2(added.isEmpty() && removed.isEmpty(),
                 qPrintable(QStringLiteral(
                     "Published context properties changed. Added: [%1]. "
                     "Removed: [%2]. The core is meant to publish none — a "
                     "new one is invisible to qmllint; register it in "
                     "qmlsingletons.h instead.")
                        .arg(QStringList(added.begin(), added.end()).join(", "),
                             QStringList(removed.begin(), removed.end())
                                 .join(", "))));
        QCOMPARE(actual.size(), expected.size());   // no duplicate publishes
    }

    // The services QML now reaches as `Kvit` module singletons rather than as
    // context properties. Two properties matter and neither is visible from
    // QML alone.
    //
    // A singleton that resolves to null is not an error QML raises on its
    // own: every member read off it is undefined, which is the same quiet
    // wrongness the context properties had. Asserting the instance exists is
    // what turns a broken factory into a failure here.
    //
    // And the instance has to be THIS composition's. Registering singletons
    // with qmlRegisterSingletonInstance would bind one object for the whole
    // process, which would break the second AppContext that tests rely on for
    // isolation; the per-engine create() seam exists to avoid that, so the
    // second half checks a second composition really does get its own.
    void everySingletonResolvesWithinItsOwnComposition()
    {
        static const QStringList singletons = {
            QStringLiteral("QueryTools"),      QStringLiteral("GlobalHotkey"),
            QStringLiteral("FileWatcher"),     QStringLiteral("ShortcutCatalog"),
            QStringLiteral("QuickSwitcherModel"),
            QStringLiteral("FolderTreeModel"),
            QStringLiteral("MarkdownFormatter"),
            QStringLiteral("BlockMenuModel"),
            QStringLiteral("MathCommandModel"),
            QStringLiteral("DocumentStats"),
            QStringLiteral("DocumentExporter"),
            QStringLiteral("DocumentSerializer"),
            QStringLiteral("DocumentImporter"),
            QStringLiteral("EmbedMetadata"),
            QStringLiteral("SystemTray"),
            QStringLiteral("NavigationHistory"),
            QStringLiteral("UpdateChecker"),
            QStringLiteral("TableTools"),
            QStringLiteral("KanbanTools"),
            QStringLiteral("TodoMeta"),
            QStringLiteral("MathRenderer"),
            QStringLiteral("UndoStack"),
            QStringLiteral("DocumentOutline"),
            QStringLiteral("CollectionSearch"),
            QStringLiteral("NoteTemplates"),
            QStringLiteral("EgressPolicy"),
            QStringLiteral("Typography"),
            QStringLiteral("ImageAssets"),
            QStringLiteral("BlockAttributes"),
            QStringLiteral("Clipboard"),
            QStringLiteral("A11y"),
            QStringLiteral("Extensions"),
            QStringLiteral("BlockKindRegistry"),
            QStringLiteral("DocumentSearch"),
            QStringLiteral("NoteListModel"),
            QStringLiteral("AppSettings"),
            QStringLiteral("DocumentManager"),
            QStringLiteral("NoteCollection"),
            QStringLiteral("BlockModel"),
            QStringLiteral("Theme"),
            QStringLiteral("DocumentSelection"),
        };

        // A second composition, wired exactly like the one under test.
        QTemporaryDir otherDir;
        AppContext other;
        other.openSettings(otherDir.filePath(QStringLiteral("settings.json")));
        QQmlEngine otherEngine;
        other.installContextProperties(&otherEngine);

        // PerfLog is the exception the loop below would get wrong. It is a
        // process-global that every composition shares, so it resolves
        // through PerfLog::instance() rather than the per-engine table, and
        // both engines are SUPPOSED to see one object. Asserting that
        // directly pins the sharing rather than leaving it untested.
        QObject *minePerfLog = m_engine.singletonInstance<QObject *>(
            QStringLiteral("Kvit"), QStringLiteral("PerfLog"));
        QCOMPARE(minePerfLog, static_cast<QObject *>(&PerfLog::instance()));
        QCOMPARE(otherEngine.singletonInstance<QObject *>(
                     QStringLiteral("Kvit"), QStringLiteral("PerfLog")),
                 minePerfLog);

        for (const QString &type : singletons) {

            QObject *mine =
                m_engine.singletonInstance<QObject *>(QStringLiteral("Kvit"), type);
            QVERIFY2(mine, qPrintable(type + QStringLiteral(" resolved to null")));

            // Identity, not just existence. Qt default-constructs a
            // QML_SINGLETON whose factory it does not find, and the result is
            // a valid object of the right class, distinct per engine, wired
            // to nothing — so every cheaper assertion here passes while the
            // shell renders empty. Comparing against what this composition
            // registered for that same type is what catches it.
            QObject *registered =
                m_context->services()->lookup(mine->metaObject());
            QVERIFY2(mine == registered,
                     qPrintable(type + QStringLiteral(" is not this context's "
                                                      "instance; the engine "
                                                      "constructed its own")));

            QObject *theirs =
                otherEngine.singletonInstance<QObject *>(QStringLiteral("Kvit"), type);
            QVERIFY2(theirs, qPrintable(type + QStringLiteral(" resolved to null "
                                                             "in the second context")));

            QVERIFY2(mine != theirs,
                     qPrintable(type + QStringLiteral(" is shared between two "
                                                      "AppContexts; the singleton "
                                                      "is process-global rather "
                                                      "than per-engine")));
        }
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
        // The fence languages that route by the registry, taken FROM the
        // registry rather than listed here. A fence kind added to the
        // registry gets a case automatically; a hand-written list would let a
        // new kind ship with no coverage, which is the drift this suite
        // exists to catch.
        for (const QString &language : m_context->blockKinds()->languages()) {
            QTest::newRow(qPrintable(QStringLiteral("code-") + language))
                << int(Block::CodeBlock) << language;
        }
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

    // The published-name list has two readers after A5 and A6 met: the case
    // above compares it with what the shell binds, and ExtensionRegistry
    // refuses a module namespace that collides with a name on it. This is the
    // second reader, driven through the real composition root rather than a
    // hand-passed list, so a future change that stops feeding the real names
    // to the registry fails here.
    void aModuleCannotTakeACoreContextPropertyName()
    {
        AppContext::Options options;
        options.showSystemTray = false;
        options.configureLoggingFromSettings = false;

        QTemporaryDir dir;

        // Derived from what the core actually occupies, rather than a name
        // written in here. Services migrated to the Kvit module one batch at
        // a time, and a hardcoded name stopped being a collision the moment
        // its service moved — which is how this test once broke rather than
        // caught anything.
        //
        // The singleton names are the half that still matters. The core
        // publishes no context properties now, so a module cannot collide
        // with one; what it can do is ask for `theme` while the core owns the
        // `Theme` singleton, which is the confusion the case-insensitive rule
        // exists to refuse.
        const QStringList reserved = KvitQml::singletonNames();
        QVERIFY2(!reserved.isEmpty(),
                 "The Kvit module registers no singletons, so there is nothing "
                 "for a module namespace to collide with and this test cannot "
                 "demonstrate the refusal.");
        const QString coreName = reserved.first().toLower();

        AppContext context(options);
        context.openSettings(dir.filePath(QStringLiteral("settings.json")));
        context.extensions()->install(
            std::make_unique<NameGrabbingExtension>(coreName));

        QQmlEngine engine;
        // Matching the explanation, not just the refusal: someone hitting
        // this needs to learn that the core owns a singleton of that name and
        // that the two would be confusable, which is the whole reason the
        // comparison ignores case.
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(QStringLiteral(
                "the editor already publishes '%1'").arg(reserved.first())));
        context.installContextProperties(&engine);

        QVERIFY(context.extensions()->publishedNamespaces().isEmpty());
        // The module got nothing, and the name it asked for is not on the
        // context either — the refusal is a refusal, not a silent rename.
        QVERIFY(!engine.rootContext()
                     ->contextProperty(coreName).isValid());

        // A namespace that collides with nothing is published, which is what
        // shows the refusal above was about the collision.
        AppContext clean(options);
        clean.openSettings(dir.filePath(QStringLiteral("settings2.json")));
        clean.extensions()->install(
            std::make_unique<NameGrabbingExtension>(QStringLiteral("premium")));
        QQmlEngine cleanEngine;
        clean.installContextProperties(&cleanEngine);
        QCOMPARE(clean.extensions()->publishedNamespaces(),
                 QStringList{QStringLiteral("premium")});
    }

    // The block-kind numbers exist once, in the BlockKinds enum, and the
    // shipped shell names them. This is the guard on that pairing: a kind
    // added to the enum with no DelegateChoice to render it would otherwise
    // produce an empty row at runtime and nothing would say so. The enum is
    // read from the metaobject, so the check cannot fall behind it.
    void everyBuiltinKindIsNamedByTheShell()
    {
        const QMetaObject &meta = BlockKinds::staticMetaObject;
        const QMetaEnum kinds = meta.enumerator(meta.indexOfEnumerator("Kind"));
        QVERIFY(kinds.isValid());
        QVERIFY(kinds.keyCount() > 0);

        QFile shell(QStringLiteral(":/qml/main.qml"));
        QVERIFY2(shell.open(QIODevice::ReadOnly | QIODevice::Text),
                 "the shipped shell is not in the test's resources");
        const QString source = QString::fromUtf8(shell.readAll());

        for (int i = 0; i < kinds.keyCount(); ++i) {
            const QString token =
                QStringLiteral("BlockKinds.") + QString::fromLatin1(kinds.key(i));
            QVERIFY2(source.contains(token),
                     qPrintable(QStringLiteral(
                                    "qml/main.qml has no DelegateChoice for %1; "
                                    "a block of that kind would render as an "
                                    "empty row").arg(token)));
        }

        // The other half of the pairing: the shell must not carry a bare
        // number where a kind belongs, which is how these fell out of step
        // before.
        for (int i = 0; i < kinds.keyCount(); ++i) {
            const QString literal =
                QStringLiteral("roleValue: %1").arg(kinds.value(i));
            QVERIFY2(!source.contains(literal),
                     qPrintable(QStringLiteral(
                                    "qml/main.qml still hard-codes %1; use the "
                                    "BlockKinds enum so the number lives in one "
                                    "place").arg(literal)));
        }
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
