// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>

#include <memory>

#include "blockkindregistry.h"
#include "extensionregistry.h"

namespace {

// A stand-in for a linked module: it does what the agent module does —
// claims a fence language, publishes a QML object, and fills one UI slot —
// without the test depending on that module being compiled in.
class FakeExtension : public KvitExtension
{
public:
    explicit FakeExtension(const QString &name, const QString &language,
                           const QString &ns = QString())
        : m_name(name), m_language(language),
          m_namespace(ns.isEmpty() ? name : ns)
    {
        m_published.setObjectName(QStringLiteral("fake-extension-object"));
    }

    QString name() const override { return m_name; }
    QString qmlNamespace() const override { return m_namespace; }

    void registerBlockKinds(BlockKindRegistry &registry) override
    {
        kind = registry.registerFenceLanguage(m_language, QStringLiteral("qrc:/fake.qml"));
    }

    QVariantMap contextObjects() override
    {
        return {{QStringLiteral("published"), QVariant::fromValue(&m_published)}};
    }

    QString qmlSlot(const QString &slot) const override
    {
        if (slot == QLatin1String(KvitSlots::BottomBar))
            return QStringLiteral("qrc:/fake/BottomBar.qml");
        return QString();
    }

    QVariantList bottomDockTabs() const override
    {
        return {QVariantMap{
            {QStringLiteral("id"), m_name + QStringLiteral(".output")},
            {QStringLiteral("title"), QStringLiteral("Output")},
            {QStringLiteral("source"), QStringLiteral("qrc:/fake/Output.qml")}}};
    }

    QVariantList sidebarViews() const override
    {
        return {QVariantMap{
            {QStringLiteral("id"), m_name + QStringLiteral(".view")},
            {QStringLiteral("title"), QStringLiteral("Module view")},
            {QStringLiteral("source"), QStringLiteral("qrc:/fake/View.qml")}}};
    }

    int kind = 0;

private:
    QString m_name;
    QString m_language;
    QString m_namespace;
    QObject m_published;
};

// A module that adds content to a note's export: the fifth thing the seam
// carries, and the only one whose answer depends on which note is being asked
// about.
class ExportingExtension : public KvitExtension
{
public:
    ExportingExtension(QString name, QString relPath, QString label)
        : m_name(std::move(name)), m_relPath(std::move(relPath)),
          m_label(std::move(label))
    {
    }

    QString name() const override { return m_name; }
    QString qmlNamespace() const override { return m_name; }

    QString exportAppendix(const QString &noteRelPath) const override
    {
        return noteRelPath == m_relPath
            ? m_name + QStringLiteral("'s paragraph.\n")
            : QString();
    }
    QString exportAppendixBaseDir() const override
    {
        return QStringLiteral("/modules/") + m_name;
    }
    QString exportAppendixLabel() const override { return m_label; }

private:
    QString m_name;
    QString m_relPath;
    QString m_label;
};

} // namespace

// The extension seam: the one place a linked module attaches to the open
// core. The core installs nothing itself, so the cases below are
// also the record of what the open build does — namely nothing, with every
// slot resolving empty and the shell laying out as if the seam were absent.
class TestExtensionRegistry : public QObject
{
    Q_OBJECT

private slots:
    // Each case owns its registries. Nothing is reset between cases because
    // nothing is shared between them.

    void anEmptyRegistryFillsNoSlot()
    {
        ExtensionRegistry registry;
        QCOMPARE(registry.count(), 0);
        QVERIFY(registry.slotSource(KvitSlots::BottomBar).isEmpty());
        QVERIFY(registry.slotSource(KvitSlots::Banner).isEmpty());
        QVERIFY(registry.slotSource(KvitSlots::SidePanel).isEmpty());
        QVERIFY(registry.slotSource("no-such-slot").isEmpty());
    }

    void installingAModuleReportsItAndFillsItsSlot()
    {
        ExtensionRegistry registry;
        QSignalSpy changed(&registry, &ExtensionRegistry::extensionsChanged);

        registry.install(std::make_unique<FakeExtension>("fake", "fake-fence"));

        QCOMPARE(changed.count(), 1);
        QCOMPARE(registry.count(), 1);
        QCOMPARE(registry.names(), QStringList{QStringLiteral("fake")});
        QCOMPARE(registry.slotSource(KvitSlots::BottomBar),
                 QStringLiteral("qrc:/fake/BottomBar.qml"));
        // A slot the module leaves alone stays empty, which keeps the shell's
        // Loader inactive and zero-sized.
        QVERIFY(registry.slotSource(KvitSlots::SidePanel).isEmpty());
    }

    void installingTheSameModuleTwiceIsIgnored()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("fake", "fake-fence"));
        registry.install(std::make_unique<FakeExtension>("fake", "other-fence"));

        QCOMPARE(registry.count(), 1);
        BlockKindRegistry kinds;
        registry.registerBlockKinds(kinds);
        QCOMPARE(kinds.kindForLanguage("other-fence"), 0);
    }

    void namedDockTabsAndSidebarViewsAggregate()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("first", "fence-a"));
        registry.install(std::make_unique<FakeExtension>("second", "fence-b"));

        const QVariantList tabs = registry.bottomDockTabs();
        QCOMPARE(tabs.size(), 2);
        QCOMPARE(tabs.at(0).toMap().value("id").toString(),
                 QStringLiteral("first.output"));
        QCOMPARE(tabs.at(1).toMap().value("module").toString(),
                 QStringLiteral("second"));

        const QVariantList views = registry.sidebarViews();
        QCOMPARE(views.size(), 2);
        QCOMPARE(registry.sidebarViewSource(QStringLiteral("second.view")),
                 QStringLiteral("qrc:/fake/View.qml"));
        QVERIFY(registry.sidebarViewSource(QStringLiteral("missing")).isEmpty());
    }

    void modulesCanMarkAnyRoot()
    {
        ExtensionRegistry registry;
        QSignalSpy changed(&registry, &ExtensionRegistry::rootStatusChanged);
        registry.setRootStatus(QStringLiteral("/tmp/one"),
                               QStringLiteral("#55aaee"),
                               QStringLiteral("Running"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(registry.rootStatus(QStringLiteral("/tmp/one"))
                     .value("tooltip").toString(), QStringLiteral("Running"));
        QCOMPARE(registry.rootStatusRevision(), 1);

        registry.clearRootStatus(QStringLiteral("/tmp/one"));
        QCOMPARE(changed.count(), 2);
        QVERIFY(registry.rootStatus(QStringLiteral("/tmp/one")).isEmpty());
    }

    void theFirstModuleClaimingASlotKeepsIt()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("first", "fence-a"));
        registry.install(std::make_unique<FakeExtension>("second", "fence-b"));

        QCOMPARE(registry.count(), 2);
        QCOMPARE(registry.slotSource(KvitSlots::BottomBar),
                 QStringLiteral("qrc:/fake/BottomBar.qml"));
    }

    void blockKindRegistrationFansOutToEveryModule()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("first", "fence-a"));
        registry.install(std::make_unique<FakeExtension>("second", "fence-b"));

        BlockKindRegistry kinds;
        registry.registerBlockKinds(kinds);

        QVERIFY(kinds.kindForLanguage("fence-a") >= BlockKindRegistry::FirstRegisteredKind);
        QVERIFY(kinds.kindForLanguage("fence-b") >= BlockKindRegistry::FirstRegisteredKind);
        QVERIFY(kinds.kindForLanguage("fence-a") != kinds.kindForLanguage("fence-b"));
    }

    // A module's objects arrive under its own namespace, so `fake.published`
    // resolves and no bare global name is taken.
    void contextObjectsArriveUnderTheModuleNamespace()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("fake", "fence-a"));

        QQmlEngine engine;
        registry.installContextProperties(engine.rootContext());
        QCOMPARE(registry.publishedNamespaces(), QStringList{QStringLiteral("fake")});

        const QVariantMap published =
            engine.rootContext()->contextProperty("fake").toMap();
        QObject *object = published.value(QStringLiteral("published")).value<QObject *>();
        QVERIFY(object);
        QCOMPARE(object->objectName(), QStringLiteral("fake-extension-object"));

        // The old contract put this straight on the root context as a global.
        QVERIFY(!engine.rootContext()->contextProperty("published").isValid());
    }

    // A namespace the core already uses is refused rather than shadowing it.
    // The modules are first party, so this is a mistake-catcher, not a
    // defence — but a silent shadow of `blockModel` would be very hard to see.
    void aNamespaceCollidingWithTheCoreIsRefused()
    {
        ExtensionRegistry registry;
        registry.install(
            std::make_unique<FakeExtension>("greedy", "fence-a",
                                            QStringLiteral("blockModel")));

        QQmlEngine engine;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("already taken")));
        registry.installContextProperties(engine.rootContext(),
                                          {QStringLiteral("blockModel")});

        QVERIFY(registry.publishedNamespaces().isEmpty());
        QVERIFY(!engine.rootContext()->contextProperty("blockModel").isValid());
    }

    void twoModulesCannotShareANamespace()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("first", "fence-a",
                                                         QStringLiteral("shared")));
        registry.install(std::make_unique<FakeExtension>("second", "fence-b",
                                                         QStringLiteral("shared")));

        QQmlEngine engine;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("already taken")));
        registry.installContextProperties(engine.rootContext());

        // The first one keeps it; the second publishes nothing.
        QCOMPARE(registry.publishedNamespaces(),
                 QStringList{QStringLiteral("shared")});
    }

    void aNamespaceThatIsNotAnIdentifierIsRefused()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("odd", "fence-a",
                                                         QStringLiteral("not a name")));

        QQmlEngine engine;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("not a valid identifier")));
        registry.installContextProperties(engine.rootContext());
        QVERIFY(registry.publishedNamespaces().isEmpty());
    }

    // Two registries in one process are independent, which is what makes a
    // case like the collision ones above safe to write at all.
    void registriesAreIndependent()
    {
        ExtensionRegistry first;
        ExtensionRegistry second;
        first.install(std::make_unique<FakeExtension>("fake", "fence-a"));
        QCOMPARE(first.count(), 1);
        QCOMPARE(second.count(), 0);
        QVERIFY(second.slotSource(KvitSlots::BottomBar).isEmpty());
    }

    // ---- what a module adds to an export ----

    // The default: a module that does not implement the seam contributes
    // nothing and is named nowhere, which is what keeps every export in the
    // open build byte-identical to what it was.
    void aModuleThatDoesNotExportContributesNothing()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("fake", "fence-a"));

        QVERIFY(registry.exportContributions(QStringLiteral("Note.md")).isEmpty());
        QVERIFY(registry.exportAppendixLabels().isEmpty());
    }

    // The fan-out is per note: the registry asks each module about the note
    // being exported, so a module that has something to say about one note
    // leaves every other note's export alone.
    void contributionsAreCollectedPerNoteInInstallationOrder()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<ExportingExtension>(
            "first", "Alpha.md", QStringLiteral("Review comments")));
        registry.install(std::make_unique<ExportingExtension>(
            "second", "Alpha.md", QStringLiteral("Conversation")));
        registry.install(std::make_unique<ExportingExtension>(
            "third", "Beta.md", QStringLiteral("Review comments")));

        const auto alpha = registry.exportContributions(QStringLiteral("Alpha.md"));
        QCOMPARE(alpha.size(), 2);
        QCOMPARE(alpha.at(0).module, QStringLiteral("first"));
        QCOMPARE(alpha.at(1).module, QStringLiteral("second"));
        QCOMPARE(alpha.at(0).markdown, QStringLiteral("first's paragraph.\n"));
        // Each keeps its own base directory rather than being joined into one
        // string here: two modules may write relative image paths against
        // different folders.
        QCOMPARE(alpha.at(0).baseDir, QStringLiteral("/modules/first"));
        QCOMPARE(alpha.at(1).baseDir, QStringLiteral("/modules/second"));

        const auto beta = registry.exportContributions(QStringLiteral("Beta.md"));
        QCOMPARE(beta.size(), 1);
        QCOMPARE(beta.first().module, QStringLiteral("third"));

        QVERIFY(registry.exportContributions(QStringLiteral("Gamma.md")).isEmpty());
    }

    // What the export dialog shows. It is a question about the installed set
    // rather than about one note, so a collection export can ask it once
    // instead of per note, and two modules calling their contribution the same
    // thing say it once.
    void theLabelsNameEachContributionOnce()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<ExportingExtension>(
            "first", "Alpha.md", QStringLiteral("Review comments")));
        registry.install(std::make_unique<ExportingExtension>(
            "second", "Beta.md", QStringLiteral("Conversation")));
        registry.install(std::make_unique<ExportingExtension>(
            "third", "Gamma.md", QStringLiteral("Review comments")));

        QCOMPARE(registry.exportAppendixLabels(),
                 (QStringList{QStringLiteral("Review comments"),
                              QStringLiteral("Conversation")}));
    }

    void clearRemovesEveryModule()
    {
        ExtensionRegistry registry;
        registry.install(std::make_unique<FakeExtension>("fake", "fence-a"));
        registry.clear();

        QCOMPARE(registry.count(), 0);
        QVERIFY(registry.slotSource(KvitSlots::BottomBar).isEmpty());
    }
};

QTEST_MAIN(TestExtensionRegistry)
#include "test_extensionregistry.moc"
