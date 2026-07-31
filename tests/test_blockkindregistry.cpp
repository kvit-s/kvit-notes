// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "blockkindregistry.h"
#include "blockmodel.h"

#include <QSet>

// The fence-language block-type registry: the seam that lets a linked
// module add a block kind — a `diff` fence, say — without an
// edit anywhere in the core. These cases pin the two halves of that promise:
// the built-in fences keep the kinds they have always had, and a registered
// language reaches BlockModel::delegateKindForBlock, which is what the QML
// DelegateChooser watches.
class TestBlockKindRegistry : public QObject
{
    Q_OBJECT

private slots:
    // No init()/cleanup() resetting shared state: each case builds its own
    // registry, so nothing one case registers can reach another. That
    // isolation is the reason the registry is instance owned.

    void builtinFencesKeepTheirKinds()
    {
        BlockKindRegistry registry;
        QCOMPARE(registry.kindForLanguage("kanban"), BlockModel::KanbanKind);
        QCOMPARE(registry.kindForLanguage("toc"), BlockModel::TocKind);
        QCOMPARE(registry.kindForLanguage("mermaid"), BlockModel::MermaidKind);
        QCOMPARE(registry.kindForLanguage("query"), BlockModel::QueryKind);

        // A fence language nobody claimed is not a kind of its own; the block
        // renders as an ordinary code block.
        QCOMPARE(registry.kindForLanguage("python"), 0);
        QCOMPARE(registry.kindForLanguage(QString()), 0);
    }

    void everyBuiltinKindWithADelegateDeclaresIt()
    {
        // The shell builds one DelegateChoice per entry here, so a kind with
        // no usable delegate URL renders as an empty row. The built-ins used
        // to declare theirs in main.qml by hand and carry nothing here.
        BlockKindRegistry registry;
        QCOMPARE(registry.delegateUrl(BlockModel::KanbanKind),
                 QStringLiteral("qrc:/qml/KanbanBlock.qml"));

        const QVariantList choices = registry.delegateChoices();
        QVERIFY(!choices.isEmpty());
        QSet<int> kinds;
        for (const QVariant &value : choices) {
            const QVariantMap choice = value.toMap();
            const QString url = choice.value("delegateUrl").toString();
            QVERIFY2(url.startsWith(QStringLiteral("qrc:/qml/")),
                     qPrintable(url));
            QVERIFY(!choice.value("id").toString().isEmpty());
            // One choice per delegate kind. Two choices claiming the same
            // value would leave the second unreachable, because the chooser
            // takes the first that matches.
            const int kind = choice.value("kind").toInt();
            QVERIFY2(!kinds.contains(kind),
                     qPrintable(QStringLiteral("kind %1 twice").arg(kind)));
            kinds.insert(kind);
        }
        // Paragraph and the four headings share one delegate and one kind
        // value, so the five of them contribute exactly one choice.
        QVERIFY(kinds.contains(0));
        QVERIFY(kinds.contains(BlockModel::KanbanKind));
        QVERIFY(kinds.contains(int(Block::Table)));
    }

    void registeringALanguageAssignsAKindAboveTheBuiltins()
    {
        BlockKindRegistry registry;
        const int kind = registry.registerFenceLanguage(
            "sample-fence", "qrc:/module/SampleBlock.qml");

        QVERIFY(kind >= BlockKindRegistry::FirstRegisteredKind);
        QVERIFY(kind != BlockModel::KanbanKind);
        QVERIFY(kind != BlockModel::MermaidKind);
        QCOMPARE(registry.kindForLanguage("sample-fence"), kind);
        QCOMPARE(registry.delegateUrl(kind),
                 QStringLiteral("qrc:/module/SampleBlock.qml"));
    }

    void registeredLanguagesGetDistinctKinds()
    {
        BlockKindRegistry registry;
        const int first = registry.registerFenceLanguage("diff", "qrc:/a.qml");
        const int second = registry.registerFenceLanguage("plan", "qrc:/b.qml");
        QVERIFY(first != second);
    }

    void reRegisteringKeepsTheFirstDelegate()
    {
        // A module cannot take over a language another module (or the core)
        // already claimed, and a double install is harmless.
        BlockKindRegistry registry;
        const int first = registry.registerFenceLanguage("diff", "qrc:/a.qml");
        const int again = registry.registerFenceLanguage("diff", "qrc:/other.qml");
        QCOMPARE(again, first);
        QCOMPARE(registry.delegateUrl(first), QStringLiteral("qrc:/a.qml"));

        const int kanban = registry.registerFenceLanguage("kanban", "qrc:/hijack.qml");
        QCOMPARE(kanban, BlockModel::KanbanKind);
        QCOMPARE(registry.delegateUrl(BlockModel::KanbanKind),
                 QStringLiteral("qrc:/qml/KanbanBlock.qml"));
    }

    void aModuleKindJoinsTheDelegateChoices()
    {
        BlockKindRegistry registry;
        const int before = registry.delegateChoices().size();
        const int kind = registry.registerFenceLanguage("diff", "qrc:/a.qml");

        const QVariantList entries = registry.delegateChoices();
        QCOMPARE(entries.size(), before + 1);
        // A module's kinds come after the built-ins, in registration order.
        const QVariantMap entry = entries.last().toMap();
        QCOMPARE(entry.value("kind").toInt(), kind);
        QCOMPARE(entry.value("id").toString(), QStringLiteral("diff"));
        QCOMPARE(entry.value("delegateUrl").toString(), QStringLiteral("qrc:/a.qml"));
    }

    void theModelRoutesARegisteredFenceToItsKind()
    {
        // The acceptance case: a new block kind reaches the delegate chooser
        // through delegateKindForBlock without that function knowing the
        // language exists.
        BlockKindRegistry registry;
        BlockModel model;
        model.setBlockKindRegistry(&registry);
        QCOMPARE(model.delegateKindForBlock(Block::CodeBlock, "diff"),
                 BlockModel::delegateKindFor(Block::CodeBlock));

        const int kind = registry.registerFenceLanguage("diff", "qrc:/a.qml");
        QCOMPARE(model.delegateKindForBlock(Block::CodeBlock, "diff"), kind);

        // Only code fences carry a language, so registering one cannot change
        // how any other block type renders.
        QCOMPARE(model.delegateKindForBlock(Block::Paragraph, "diff"), 0);
        QCOMPARE(model.delegateKindForBlock(Block::Quote, "diff"),
                 static_cast<int>(Block::Quote));
    }

    void resetDropsModuleRegistrations()
    {
        BlockKindRegistry registry;
        const int diff = registry.registerFenceLanguage("diff", "qrc:/a.qml");
        registry.reset();

        QCOMPARE(registry.kindForLanguage("diff"), 0);
        QCOMPARE(registry.kindForLanguage("mermaid"), BlockModel::MermaidKind);
        QVERIFY(registry.delegateUrl(diff).isEmpty());
    }

    // Two registries in one process do not see each other's registrations.
    // Under the old process-wide registry this case could not be written at
    // all, and a test that forgot to reset() silently inherited whatever the
    // previous one registered.
    void registriesAreIndependent()
    {
        BlockKindRegistry first;
        BlockKindRegistry second;
        const int kind = first.registerFenceLanguage("diff", "qrc:/a.qml");
        QVERIFY(kind != 0);
        QCOMPARE(second.kindForLanguage("diff"), 0);
        QVERIFY(second.delegateUrl(kind).isEmpty());
        // Both still carry the built-ins.
        QCOMPARE(second.kindForLanguage("mermaid"), BlockModel::MermaidKind);
    }

    // A model with no registry wired resolves the built-in fences from its
    // own, so a unit test that constructs a bare BlockModel still renders a
    // `kanban` fence as a board.
    void aModelWithNoRegistryStillKnowsTheBuiltins()
    {
        BlockModel model;
        QVERIFY(model.blockKindRegistry() != nullptr);
        QCOMPARE(model.delegateKindForBlock(Block::CodeBlock, "kanban"),
                 BlockModel::KanbanKind);
        QCOMPARE(model.delegateKindForBlock(Block::CodeBlock, "diff"),
                 BlockModel::delegateKindFor(Block::CodeBlock));
    }
};

QTEST_MAIN(TestBlockKindRegistry)
#include "test_blockkindregistry.moc"
