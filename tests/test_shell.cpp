// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QAccessible>
#include <QFile>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlContext>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>
#include <utility>

#include "appcontext.h"
#include "blockkinddef.h"
#include "blockkindregistry.h"
#include "block.h"
#include "blockmodel.h"
#include "extensionregistry.h"
#include "menuaccesskeys.h"
#include "perflog.h"
#include "documentmanager.h"
#include "textfileviewmodel.h"
#include "theme.h"
#include "qmlservices.h"

#include <QQmlContext>
#include <QRegularExpression>
#include <QSet>

namespace {

// Warnings emitted while this suite runs. QML resolves bindings lazily and
// reports every failure — an unknown context property, a type the qrc does
// not carry, a binding loop — as a warning on the message handler and then
// carries on with an undefined value. Loading therefore "succeeds" no matter
// how much of the shell failed to wire up, which is precisely how a renamed
// context property or a resource missing from the qrc used to merge green.
// Capturing the warnings turns each one into a test failure.
//
// The handler stays installed for the whole suite rather than only across the
// load. Most of what the shell does happens after loadComplete: a binding
// that only evaluates once a delegate is created, a signal handler that only
// runs when something changes, and Qt's own deprecation warnings about the
// QML the shell contains. Restoring the handler as soon as the engine
// returned meant none of that could fail the test.
QStringList g_warnings;
QtMessageHandler g_previousHandler = nullptr;

// Warnings a test provokes deliberately. The case that proves a module cannot
// take a core singleton's namespace has to trigger the refusal to observe it,
// and that refusal is a warning; without this it would fail the suite-wide
// check below. QTest::ignoreMessage handles the same message for QTest's own
// handler, which is a separate mechanism from this list.
QList<QRegularExpression> g_expectedWarnings;

void capturingHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        bool expected = false;
        for (const QRegularExpression &pattern : std::as_const(g_expectedWarnings)) {
            if (message.contains(pattern)) {
                expected = true;
                break;
            }
        }
        if (!expected)
            g_warnings << message;
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

// Format `warnings` from `first` onward as a failure report.
QString warningReport(const QString &what, const QStringList &warnings, int first)
{
    QString report =
        QStringLiteral("%1 produced %2 warning(s):\n").arg(what).arg(warnings.size() - first);
    for (int i = first; i < warnings.size(); ++i)
        report += QStringLiteral("  - ") + warnings.at(i) + QLatin1Char('\n');
    return report;
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
        // The style the launcher applies. Without it this suite loads the
        // shipped shell under whatever style the platform defaults to, which
        // on macOS is the native one, and reports the theme's own backgrounds
        // as warnings for a configuration the app never runs in.
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
        m_context = std::make_unique<AppContext>();
        m_context->openSettings(m_dir.filePath(QStringLiteral("settings.json")));
        m_context->installContextProperties(&m_engine);

        g_warnings.clear();
        g_expectedWarnings.clear();
        // Not the shell's warnings: Qt's multimedia backend reports a missing
        // PipeWire on any machine without one, which a headless runner is.
        // This gate is about the composition - a renamed context property, a
        // QML file missing from the resources, an import that does not
        // resolve - and an audio stack the test never asked for is noise
        // that would make it fail wherever the runner image lacks a library.
        g_expectedWarnings << QRegularExpression(QStringLiteral("pipewire"));
        // Also the runner's, not the shell's: a machine with no "Sans Serif"
        // family makes Qt populate its alias table and say how long it took.
        g_expectedWarnings << QRegularExpression(
            QStringLiteral("Populating font family aliases"));
        g_previousHandler = qInstallMessageHandler(capturingHandler);
        m_engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
        // Bindings evaluate as the scene is built; let the queue drain so a
        // late failure is captured too.
        QCoreApplication::processEvents();
        // Where the load stopped and the rest of the suite begins. The handler
        // is deliberately left installed; cleanupTestCase() takes it back.
        m_warningsAfterLoad = g_warnings.size();
    }

    void cleanupTestCase()
    {
        if (g_previousHandler)
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
        if (m_warningsAfterLoad > 0)
            QFAIL(qPrintable(warningReport(QStringLiteral("Loading qml/main.qml"),
                                           g_warnings.mid(0, m_warningsAfterLoad), 0)));
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
        // The list comes from the registry that declares the singletons
        // (KVIT_QML_SINGLETONS in qmlsingletons.h, expanded by
        // KvitQml::singletonNames()), not from a copy kept here. The copy had
        // already drifted: it named 41 of the 44 services and silently omitted
        // RemoteMediaCache, AssetStore and AppActions, so a broken factory or
        // a wrong-instance composition for any of those three passed a test
        // whose whole claim is that it covers every singleton.
        const QStringList singletons = KvitQml::singletonNames();

        // An empty registry would make the loop below vacuous and still green,
        // which is the one failure mode driving the list from code introduces.
        QVERIFY(!singletons.isEmpty());

        // The partition: a per-vault singleton resolves to each composition's
        // own instance, a global one to the single ProcessServices instance
        // both compositions share. Both sets come from the same macro lists in
        // qmlsingletons.h that declare the wrappers, so a service in the wrong
        // list fails here rather than becoming a silent shared-state bug.
        const QSet<QString> globals(KvitQml::globalSingletonNames().cbegin(),
                                    KvitQml::globalSingletonNames().cend());
        QVERIFY(!globals.isEmpty());
        qInfo("checking %lld Kvit singletons (%lld global, %lld per-vault)",
              qint64(singletons.size()), qint64(globals.size()),
              qint64(KvitQml::perVaultSingletonNames().size()));

        // A second composition that SHARES this one's process globals — exactly
        // what a second window is. It borrows m_context's ProcessServices, so
        // it must NOT open settings itself: that would re-point the shared
        // store at a different file underneath the first composition.
        AppContext other(*m_context->processServices());
        QQmlEngine otherEngine;
        other.installContextProperties(&otherEngine);

        // PerfLog is shared through PerfLog::instance() rather than the
        // per-engine table, so both engines are SUPPOSED to see one object.
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
            // registered for that same type is what catches it. Holds for both
            // halves: a global is registered as the shared instance, a
            // per-vault one as this composition's own.
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

            if (globals.contains(type)) {
                QVERIFY2(mine == theirs,
                         qPrintable(type + QStringLiteral(" is a process-global "
                             "singleton but resolved to a different instance in "
                             "each composition; every window must share the one "
                             "ProcessServices instance")));
            } else {
                QVERIFY2(mine != theirs,
                         qPrintable(type + QStringLiteral(" is a per-vault "
                             "singleton but is shared between two compositions; "
                             "each window must get its own")));
            }
        }
    }

    void withNoModuleInstalledEverySlotIsInert()
    {
        // What the open build looks like: the seams are present and empty, so
        // the shell lays out exactly as it did before they existed.
        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);

        for (const char *name : {"extensionBanner", "extensionBottomBar",
                                 "extensionSidePanel",
                                 "extensionDocumentHeader"}) {
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
        QCOMPARE(window->findChild<QObject *>("extensionDocumentHeader")
                     ->property("height").toReal(), 0.0);
        QObject *dock = window->findChild<QObject *>("bottomDock");
        QVERIFY(dock);
        QVERIFY(!dock->property("visible").toBool());
        QCOMPARE(dock->property("height").toReal(), 0.0);

        // The document-decoration seam is the same story: nothing registered,
        // so no margin column is reserved and the rows are as wide as the
        // list, which is what keeps the text where it was.
        QVERIFY(!m_context->documentDecorations()->isActive());
        QVERIFY(!m_context->documentDecorations()->marginColumnReserved());
    }

    // Unit tests cover classification and loading in isolation. This case
    // pins the final composition seam: an activation from the shipped Files
    // pane must select the matching production surface, not merely emit a
    // correctly classified signal that nobody consumes.
    void fileTreeActivationsReachTheShippedViewingSurfaces()
    {
        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);

        const QString sourcePath = m_dir.filePath(QStringLiteral("route.rs"));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QVERIFY(source.write(
            "fn main() {\r\n    println!(\"hello\");\r\n}\r\n") > 0);
        source.close();

        QVariant result;
        QVERIFY(QMetaObject::invokeMethod(
            window, "openFileTreeEntry", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, sourcePath), Q_ARG(QVariant, QStringLiteral("text")),
            Q_ARG(QVariant, QStringLiteral("route.rs"))));
        QVERIFY(result.toBool());
        QCOMPARE(window->property("contentView").toString(), QStringLiteral("text"));
        QCOMPARE(m_context->textFileViewModel()->state(), QStringLiteral("ready"));
        QCOMPARE(m_context->textFileViewModel()->language(), QStringLiteral("rust"));
        QVERIFY(m_context->textFileViewModel()->text().contains(
            QStringLiteral("println!")));
        QVERIFY(window->findChild<QObject *>("readOnlyTextFile")
                    ->property("visible").toBool());

        const QString imagePath = m_dir.filePath(QStringLiteral("route.png"));
        QImage image(2, 2, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::magenta);
        QVERIFY(image.save(imagePath));

        result.clear();
        QVERIFY(QMetaObject::invokeMethod(
            window, "openFileTreeEntry", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, imagePath), Q_ARG(QVariant, QStringLiteral("image")),
            Q_ARG(QVariant, QStringLiteral("route.png"))));
        QVERIFY(result.toBool());
        QCOMPARE(window->property("contentView").toString(), QStringLiteral("media"));
        QObject *standalone = window->findChild<QObject *>("standaloneFileView");
        QVERIFY(standalone);
        QCOMPARE(standalone->property("path").toString(), imagePath);
        QCOMPARE(standalone->property("kind").toString(), QStringLiteral("image"));
        QObject *imageSurface = standalone->findChild<QObject *>(
            "standaloneImageSurface");
        QVERIFY(imageSurface);
        QTRY_VERIFY(imageSurface->property("ready").toBool());
        QVERIFY(imageSurface->property("source").toString().startsWith(
            QStringLiteral("image://local/")));

        // Leave the shared shell in its ordinary document state for the
        // remaining delegate and accessibility cases.
        m_context->textFileViewModel()->close();
        m_context->documentManager()->newDocument();
        window->setProperty("contentView", QStringLiteral("document"));
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
        // The three the data set never had, and the reason the embed kind
        // could have resolved to nothing without a unit suite noticing: an
        // image, a media file, and an image expression whose URL names a web
        // page, which draws as a preview card rather than a picture.
        QTest::newRow("image") << int(Block::Image) << QString();
        QTest::newRow("media") << int(Block::Media) << QString();
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

    // An image expression whose URL names a web page is the one kind decided
    // by the block's CONTENT rather than by its type or its fence language,
    // so no row of the type-driven data set above reaches it.
    void anEmbedUrlGetsThePreviewCardDelegate()
    {
        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);
        QObject *listView = window->findChild<QObject *>("blockListView");
        QVERIFY(listView);

        BlockModel *model = m_context->blockModel();
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);

        model->insertBlock(0, Block::Image, QString());
        model->convertBlock(0, Block::Image,
                            QStringLiteral("![](https://example.com/page)"),
                            false, QString());
        QCOMPARE(model->blockAt(0)->kind()->id(), QStringLiteral("embed"));

        QQuickItem *row = nullptr;
        QTRY_VERIFY2(QMetaObject::invokeMethod(
                         listView, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row),
                         Q_ARG(int, 0)) && row,
                     "the delegate chooser produced no delegate for an embed");
        QVERIFY(row->height() > 0);
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
        if (blockType == Block::Image || blockType == Block::Media) {
            // Both hold a markdown image expression, and an empty one is not
            // the shape either delegate parses.
            model->convertBlock(index, blockType,
                                blockType == Block::Media
                                    ? QStringLiteral("![clip](clip.mp4)")
                                    : QStringLiteral("![alt](pic.png)"),
                                false, QString());
        }
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
        const QRegularExpression refusal(QStringLiteral(
            "the editor already publishes '%1'").arg(reserved.first()));
        QTest::ignoreMessage(QtWarningMsg, refusal);
        // The suite-wide warning check runs after this case, and this warning
        // is the thing the case exists to provoke.
        g_expectedWarnings << refusal;
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
            std::make_unique<NameGrabbingExtension>(QStringLiteral("agent")));
        QQmlEngine cleanEngine;
        clean.installContextProperties(&cleanEngine);
        QCOMPARE(clean.extensions()->publishedNamespaces(),
                 QStringList{QStringLiteral("agent")});
    }

    // Every kind the registry knows must have a delegate that loads.
    //
    // This is the guard on the pairing between a block kind and the QML that
    // draws it. It used to check that main.qml named each kind of the
    // BlockKinds enum, because the shell listed seventeen DelegateChoice
    // blocks by hand and a kind whose choice nobody added drew an empty row
    // with nothing to say so. The shell now builds one choice per registered
    // kind, so the pairing that can break is a kind whose delegate URL is
    // missing or names a file that will not load — which is what this checks,
    // over every kind rather than over the five that happened to be
    // enumerated.
    void everyRegisteredKindHasADelegateThatLoads()
    {
        const QVariantList choices =
            m_context->blockKinds()->delegateChoices();
        QVERIFY(choices.size() >= 17);

        for (const QVariant &value : choices) {
            const QVariantMap choice = value.toMap();
            const QString url = choice.value(QStringLiteral("delegateUrl")).toString();
            const QString id = choice.value(QStringLiteral("id")).toString();
            QVERIFY2(!url.isEmpty(), qPrintable(id));

            QQmlComponent component(&m_engine, QUrl(url));
            QVERIFY2(component.status() == QQmlComponent::Ready,
                     qPrintable(QStringLiteral("kind '%1' names %2, which does "
                                               "not load: %3")
                                    .arg(id, url, component.errorString())));
        }
    }

    // The other half: a kind with no delegate of its own must be sharing one,
    // and the only kinds that do are the paragraph and its four heading
    // levels, which publish kind 0 between them. Anything else with no
    // delegate URL would draw nothing at all.
    void onlyTheTextKindsShareADelegate()
    {
        const BlockKindRegistry *registry = m_context->blockKinds();
        QSet<int> covered;
        const QVariantList choices = registry->delegateChoices();
        for (const QVariant &value : choices)
            covered.insert(value.toMap().value(QStringLiteral("kind")).toInt());

        for (const BlockKindDef *kind : registry->all()) {
            QVERIFY2(covered.contains(kind->delegateKind()),
                     qPrintable(QStringLiteral("kind '%1' publishes delegate "
                                               "kind %2, which no delegate "
                                               "renders")
                                    .arg(kind->id())
                                    .arg(kind->delegateKind())));
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

    // A file-less window is the state the Windows duplicate-instance bug used
    // to expose after the second process lost the vault lock. The menu must
    // still offer persistence, must show collection-only entries as disabled
    // rather than as active-looking or blank rows, and the status bar must say
    // plainly that there is no location on disk yet.
    void singleFileMenusExplainTheirState()
    {
        QVERIFY(!m_engine.rootObjects().isEmpty());
        QObject *root = m_engine.rootObjects().first();
        QVERIFY(!m_context->noteCollection()->isOpen());
        QVERIFY(!m_context->documentManager()->hasFile());

        QObject *save = root->findChild<QObject *>(
            QStringLiteral("fileMenuSave"));
        QObject *saveAs = root->findChild<QObject *>(
            QStringLiteral("fileMenuSaveAs"));
        QVERIFY(save);
        QVERIFY(saveAs);
        // Labels are written with their access key marked ("&Save"), and the
        // running platform decides whether the marker stays; asking
        // MenuAccessKeys for the same label is what the menu itself does.
        QCOMPARE(save->property("text").toString(),
                 MenuAccessKeys::label(QStringLiteral("&Save")));
        QVERIFY(save->property("enabled").toBool());
        QVERIFY(saveAs->property("enabled").toBool());

        QObject *importItem = root->findChild<QObject *>(
            QStringLiteral("fileMenuImport"));
        QObject *quickCapture = root->findChild<QObject *>(
            QStringLiteral("fileMenuQuickCapture"));
        QObject *sidebar = root->findChild<QObject *>(
            QStringLiteral("viewMenuSidebar"));
        QObject *fileMenu = root->findChild<QObject *>(
            QStringLiteral("toolbarFileMenu"));
        QVERIFY(importItem);
        QVERIFY(quickCapture);
        QVERIFY(sidebar);
        QVERIFY(fileMenu);
        // QQuickItem::visible reports effective visibility, so open the parent
        // popup before checking that Import is a real labelled row rather than
        // the hidden full-height row that produced the blank slot.
        QVERIFY(QMetaObject::invokeMethod(fileMenu, "open"));
        QCoreApplication::processEvents();
        QVERIFY(importItem->property("visible").toBool());
        QVERIFY(!importItem->property("enabled").toBool());
        QVERIFY(!quickCapture->property("enabled").toBool());
        QVERIFY(!sidebar->property("enabled").toBool());
        // Disabled means drawn in the theme's disabled text color. Fusion
        // paints a menu label with palette.text whatever the entry's state,
        // so without DiscoverableMenuItem doing this per entry the three
        // rows below carry the same color as Save above them.
        const QColor disabledText = m_context->theme()->textDisabled();
        const QColor liveText = m_context->theme()->textPrimary();
        const auto labelColor = [](QObject *item) {
            QObject *label = item->property("contentItem").value<QObject *>();
            return label ? label->property("color").value<QColor>() : QColor();
        };
        QVERIFY(disabledText != liveText);
        QCOMPARE(labelColor(save), liveText);
        QCOMPARE(labelColor(importItem), disabledText);
        QCOMPARE(labelColor(quickCapture), disabledText);
        QCOMPARE(labelColor(sidebar), disabledText);

        // Submenus are represented by generated MenuItems, so the parent
        // menu's delegate (rather than the Menu popup object) supplies their
        // disabled appearance. Check those rows too: these were the largest
        // active-looking stretch in the reported hover path.
        const auto menuRowWithText = [fileMenu](const QString &text) {
            const int count = fileMenu->property("count").toInt();
            for (int i = 0; i < count; ++i) {
                QQuickItem *row = nullptr;
                if (QMetaObject::invokeMethod(
                        fileMenu, "itemAt", Q_RETURN_ARG(QQuickItem *, row),
                        Q_ARG(int, i))
                    && row && row->property("text").toString() == text)
                    return row;
            }
            return static_cast<QQuickItem *>(nullptr);
        };
        QQuickItem *recent = menuRowWithText(
            MenuAccessKeys::label(QStringLiteral("Open &Recent")));
        QQuickItem *fromTemplate = menuRowWithText(
            MenuAccessKeys::label(QStringLiteral("&New from template")));
        QVERIFY(recent);
        QVERIFY(fromTemplate);
        QVERIFY(!recent->isEnabled());
        QVERIFY(!fromTemplate->isEnabled());
        QCOMPARE(labelColor(recent), disabledText);
        QCOMPARE(labelColor(fromTemplate), disabledText);
        QVERIFY(QMetaObject::invokeMethod(fileMenu, "close"));

        QObject *path = root->findChild<QObject *>(
            QStringLiteral("filePathText"));
        QVERIFY(path);
        QCOMPARE(path->property("text").toString(),
                 QStringLiteral("Not saved to disk"));
    }

    // The accessibility tree the shipped shell actually serves, walked from
    // its root (accessibility.md, "Tests and gates to add").
    //
    // tools/check-accessible-names.py reads the QML and catches a control
    // that was written without a name. This catches what reading the source
    // cannot: a name bound to an expression that evaluates to nothing — a
    // model role that is empty for this row, a `tip` nobody set at the call
    // site, a translation that came back blank. Both are needed, and neither
    // subsumes the other.
    //
    // Only nodes a person can operate are held to it. A layout item or a
    // decorative rectangle may legitimately have no name; a button, a
    // checkbox, a menu button or a link that reports one is a control an
    // assistive technology can find and cannot describe.
    void everyOperableNodeInTheAccessibilityTreeHasAName()
    {
        QAccessible::setActive(true);
        QVERIFY(!m_engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(m_engine.rootObjects().first());
        QVERIFY2(window, "the shell's root is the window");
        QAccessibleInterface *root =
            QAccessible::queryAccessibleInterface(window);
        QVERIFY2(root, "the window publishes an accessibility interface");

        QStringList unnamed;
        int operable = 0;
        walkForNames(root, QString(), &unnamed, &operable);

        // The finding first, so a real naming gap is reported even on a
        // platform whose count comes out lower than expected. Asserting the
        // count first hid exactly that: the macOS run stopped on the count
        // and never said whether any node was nameless.
        QVERIFY2(unnamed.isEmpty(),
                 qPrintable(QStringLiteral(
                     "%1 operable node(s) in the accessibility tree report no "
                     "name:\n  %2")
                     .arg(unnamed.size()).arg(unnamed.join(QStringLiteral("\n  ")))));

        // And a floor, because without one the case passes on an empty tree,
        // which is what a broken walk or an inactive accessibility layer
        // produces.
        //
        // Deliberately far below what any platform actually serves rather
        // than tuned to one. The count is not the same everywhere: macOS
        // hangs File and View on the system menu bar and does not build those
        // two toolbar buttons at all, so it serves fewer nodes than Linux and
        // Windows for the same shell. A floor set from the Linux count failed
        // there for no defect. What this is guarding is "a tree is being
        // served", so it only has to clear zero by a margin no platform
        // difference can close.
        QVERIFY2(operable >= 10,
                 qPrintable(QStringLiteral("the walk found only %1 operable "
                                           "node(s); the tree is not being "
                                           "served").arg(operable)));
    }

    // Declared last on purpose: QtTest runs test functions in declaration
    // order, so this sees everything the cases above provoked.
    //
    // The load-time check earlier covers only what the engine reported before
    // it returned, which is a small fraction of what the shell says. Bindings
    // inside a delegate evaluate when the delegate is created — this suite
    // creates one per block kind — and Qt reports deprecated QML constructs
    // as the handlers containing them run rather than at parse time. Both
    // classes were escaping the gate entirely.
    //
    // A warning a case raises on purpose belongs in g_expectedWarnings, next
    // to the QTest::ignoreMessage that documents why it is expected.
    void noWarningsAppearAfterTheShellHasLoaded()
    {
        if (g_warnings.size() > m_warningsAfterLoad)
            QFAIL(qPrintable(warningReport(
                QStringLiteral("Exercising the loaded shell"),
                g_warnings, m_warningsAfterLoad)));
    }

private:
    // The roles whose whole purpose is to be operated. A node with one of
    // these and no name is unusable through an assistive technology: it can
    // be reached and activated, and there is nothing to say about it first.
    static bool isOperable(QAccessible::Role role)
    {
        switch (role) {
        case QAccessible::Button:
        case QAccessible::ButtonMenu:
        case QAccessible::CheckBox:
        case QAccessible::RadioButton:
        case QAccessible::Link:
        case QAccessible::MenuItem:
        case QAccessible::Slider:
            return true;
        default:
            return false;
        }
    }

    // Depth-first from `node`, collecting a description of every operable
    // node with an empty name. `path` accumulates the named ancestors, so a
    // failure says where in the window the nameless control is rather than
    // only that one exists.
    static void walkForNames(QAccessibleInterface *node, const QString &path,
                             QStringList *unnamed, int *operable)
    {
        if (!node || !node->isValid())
            return;
        const QAccessible::State state = node->state();
        // An invisible node is one the window is not currently showing — a
        // closed dialog, a pane that is switched off. It will be walked when
        // whatever shows it does.
        if (state.invisible)
            return;

        const QString name = node->text(QAccessible::Name).trimmed();
        const QString here = name.isEmpty()
            ? path
            : (path.isEmpty() ? name : path + QStringLiteral(" > ") + name);

        if (isOperable(node->role()))
            ++*operable;
        if (name.isEmpty() && isOperable(node->role())) {
            unnamed->append(
                QStringLiteral("%1 (role %2) under \"%3\"")
                    .arg(node->object() ? node->object()->objectName()
                                        : QStringLiteral("<no objectName>"))
                    .arg(int(node->role()))
                    .arg(path.isEmpty() ? QStringLiteral("the window") : path));
        }

        const int count = node->childCount();
        for (int i = 0; i < count; ++i)
            walkForNames(node->child(i), here, unnamed, operable);
    }

    QTemporaryDir m_dir;
    // The context outlives the engine, as it does in KvitApplication.
    std::unique_ptr<AppContext> m_context;
    QQmlApplicationEngine m_engine;
    // How many warnings the shell load itself produced; everything after that
    // index belongs to the cases that ran afterwards.
    int m_warningsAfterLoad = 0;
};

QTEST_MAIN(TestShell)
#include "test_shell.moc"
