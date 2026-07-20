// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QQmlContext>
#include <QQmlEngine>

#include <memory>

#include "blockkindregistry.h"
#include "extensionregistry.h"

namespace {

// A stand-in for a linked module: it does what the premium agent module does —
// claims a fence language, publishes a QML object, and fills one UI slot —
// without the test depending on that module being compiled in.
class FakeExtension : public KvitExtension
{
public:
    explicit FakeExtension(const QString &name, const QString &language)
        : m_name(name), m_language(language)
    {
        m_published.setObjectName(QStringLiteral("fake-extension-object"));
    }

    QString name() const override { return m_name; }

    void registerBlockKinds(BlockKindRegistry &registry) override
    {
        kind = registry.registerFenceLanguage(m_language, QStringLiteral("qrc:/fake.qml"));
    }

    void installContextProperties(QQmlContext *context) override
    {
        context->setContextProperty("fakeExtension", &m_published);
    }

    QString qmlSlot(const QString &slot) const override
    {
        if (slot == QLatin1String(KvitSlots::BottomBar))
            return QStringLiteral("qrc:/fake/BottomBar.qml");
        return QString();
    }

    int kind = 0;

private:
    QString m_name;
    QString m_language;
    QObject m_published;
};

} // namespace

// The extension seam (chat.md §8, seam 1): the one place premium code attaches
// to the open core. The core installs nothing itself, so the cases below are
// also the record of what the open build does — namely nothing, with every
// slot resolving empty and the shell laying out as if the seam were absent.
class TestExtensionRegistry : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        ExtensionRegistry::instance().clear();
        BlockKindRegistry::instance().reset();
    }

    void cleanupTestCase()
    {
        ExtensionRegistry::instance().clear();
        BlockKindRegistry::instance().reset();
    }

    void anEmptyRegistryFillsNoSlot()
    {
        ExtensionRegistry &registry = ExtensionRegistry::instance();
        QCOMPARE(registry.count(), 0);
        QVERIFY(registry.slotSource(KvitSlots::BottomBar).isEmpty());
        QVERIFY(registry.slotSource(KvitSlots::Banner).isEmpty());
        QVERIFY(registry.slotSource(KvitSlots::SidePanel).isEmpty());
        QVERIFY(registry.slotSource("no-such-slot").isEmpty());
    }

    void installingAModuleReportsItAndFillsItsSlot()
    {
        ExtensionRegistry &registry = ExtensionRegistry::instance();
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
        ExtensionRegistry &registry = ExtensionRegistry::instance();
        registry.install(std::make_unique<FakeExtension>("fake", "fake-fence"));
        registry.install(std::make_unique<FakeExtension>("fake", "other-fence"));

        QCOMPARE(registry.count(), 1);
        registry.registerBlockKinds(BlockKindRegistry::instance());
        QCOMPARE(BlockKindRegistry::instance().kindForLanguage("other-fence"), 0);
    }

    void theFirstModuleClaimingASlotKeepsIt()
    {
        ExtensionRegistry &registry = ExtensionRegistry::instance();
        registry.install(std::make_unique<FakeExtension>("first", "fence-a"));
        registry.install(std::make_unique<FakeExtension>("second", "fence-b"));

        QCOMPARE(registry.count(), 2);
        QCOMPARE(registry.slotSource(KvitSlots::BottomBar),
                 QStringLiteral("qrc:/fake/BottomBar.qml"));
    }

    void blockKindRegistrationFansOutToEveryModule()
    {
        ExtensionRegistry &registry = ExtensionRegistry::instance();
        registry.install(std::make_unique<FakeExtension>("first", "fence-a"));
        registry.install(std::make_unique<FakeExtension>("second", "fence-b"));

        BlockKindRegistry &kinds = BlockKindRegistry::instance();
        registry.registerBlockKinds(kinds);

        QVERIFY(kinds.kindForLanguage("fence-a") >= BlockKindRegistry::FirstRegisteredKind);
        QVERIFY(kinds.kindForLanguage("fence-b") >= BlockKindRegistry::FirstRegisteredKind);
        QVERIFY(kinds.kindForLanguage("fence-a") != kinds.kindForLanguage("fence-b"));
    }

    void contextPropertiesReachTheQmlRootContext()
    {
        ExtensionRegistry &registry = ExtensionRegistry::instance();
        registry.install(std::make_unique<FakeExtension>("fake", "fence-a"));

        QQmlEngine engine;
        registry.installContextProperties(engine.rootContext());

        QObject *published =
            engine.rootContext()->contextProperty("fakeExtension").value<QObject *>();
        QVERIFY(published);
        QCOMPARE(published->objectName(), QStringLiteral("fake-extension-object"));
    }

    void clearRemovesEveryModule()
    {
        ExtensionRegistry &registry = ExtensionRegistry::instance();
        registry.install(std::make_unique<FakeExtension>("fake", "fence-a"));
        registry.clear();

        QCOMPARE(registry.count(), 0);
        QVERIFY(registry.slotSource(KvitSlots::BottomBar).isEmpty());
    }
};

QTEST_MAIN(TestExtensionRegistry)
#include "test_extensionregistry.moc"
