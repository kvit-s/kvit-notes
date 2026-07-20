// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "blockkindregistry.h"
#include "blockmodel.h"

// The fence-language block-type registry (chat.md §8, seam 2): the seam that
// lets a linked module add a block kind — a `diff` fence, say — without an
// edit anywhere in the core. These cases pin the two halves of that promise:
// the built-in fences keep the kinds they have always had, and a registered
// language reaches BlockModel::delegateKindForBlock, which is what the QML
// DelegateChooser watches.
class TestBlockKindRegistry : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Every case starts from the built-ins alone: the registry is
        // process-wide, so a leftover registration would leak between cases.
        BlockKindRegistry::instance().reset();
    }

    void cleanupTestCase()
    {
        BlockKindRegistry::instance().reset();
    }

    void builtinFencesKeepTheirKinds()
    {
        BlockKindRegistry &registry = BlockKindRegistry::instance();
        QCOMPARE(registry.kindForLanguage("kanban"), BlockModel::KanbanKind);
        QCOMPARE(registry.kindForLanguage("toc"), BlockModel::TocKind);
        QCOMPARE(registry.kindForLanguage("mermaid"), BlockModel::MermaidKind);
        QCOMPARE(registry.kindForLanguage("query"), BlockModel::QueryKind);

        // A fence language nobody claimed is not a kind of its own; the block
        // renders as an ordinary code block.
        QCOMPARE(registry.kindForLanguage("python"), 0);
        QCOMPARE(registry.kindForLanguage(QString()), 0);
    }

    void builtinFencesDeclareNoDelegate()
    {
        // main.qml declares a DelegateChoice for each built-in statically, so
        // the common rendering path never consults the registry for a URL.
        BlockKindRegistry &registry = BlockKindRegistry::instance();
        QVERIFY(registry.delegateUrl(BlockModel::KanbanKind).isEmpty());
        QVERIFY(registry.registeredDelegates().isEmpty());
    }

    void registeringALanguageAssignsAKindAboveTheBuiltins()
    {
        BlockKindRegistry &registry = BlockKindRegistry::instance();
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
        BlockKindRegistry &registry = BlockKindRegistry::instance();
        const int first = registry.registerFenceLanguage("diff", "qrc:/a.qml");
        const int second = registry.registerFenceLanguage("plan", "qrc:/b.qml");
        QVERIFY(first != second);
    }

    void reRegisteringKeepsTheFirstDelegate()
    {
        // A module cannot take over a language another module (or the core)
        // already claimed, and a double install is harmless.
        BlockKindRegistry &registry = BlockKindRegistry::instance();
        const int first = registry.registerFenceLanguage("diff", "qrc:/a.qml");
        const int again = registry.registerFenceLanguage("diff", "qrc:/other.qml");
        QCOMPARE(again, first);
        QCOMPARE(registry.delegateUrl(first), QStringLiteral("qrc:/a.qml"));

        const int kanban = registry.registerFenceLanguage("kanban", "qrc:/hijack.qml");
        QCOMPARE(kanban, BlockModel::KanbanKind);
        QVERIFY(registry.delegateUrl(BlockModel::KanbanKind).isEmpty());
    }

    void registeredDelegatesListsOnlyModuleEntries()
    {
        BlockKindRegistry &registry = BlockKindRegistry::instance();
        const int kind = registry.registerFenceLanguage("diff", "qrc:/a.qml");

        const QVariantList entries = registry.registeredDelegates();
        QCOMPARE(entries.size(), 1);
        const QVariantMap entry = entries.first().toMap();
        QCOMPARE(entry.value("kind").toInt(), kind);
        QCOMPARE(entry.value("language").toString(), QStringLiteral("diff"));
        QCOMPARE(entry.value("delegateUrl").toString(), QStringLiteral("qrc:/a.qml"));
    }

    void theModelRoutesARegisteredFenceToItsKind()
    {
        // The acceptance case: a new block kind reaches the delegate chooser
        // through delegateKindForBlock without that function knowing the
        // language exists.
        BlockKindRegistry &registry = BlockKindRegistry::instance();
        QCOMPARE(BlockModel::delegateKindForBlock(Block::CodeBlock, "diff"),
                 BlockModel::delegateKindFor(Block::CodeBlock));

        const int kind = registry.registerFenceLanguage("diff", "qrc:/a.qml");
        QCOMPARE(BlockModel::delegateKindForBlock(Block::CodeBlock, "diff"), kind);

        // Only code fences carry a language, so registering one cannot change
        // how any other block type renders.
        QCOMPARE(BlockModel::delegateKindForBlock(Block::Paragraph, "diff"), 0);
        QCOMPARE(BlockModel::delegateKindForBlock(Block::Quote, "diff"),
                 static_cast<int>(Block::Quote));
    }

    void resetDropsModuleRegistrations()
    {
        BlockKindRegistry &registry = BlockKindRegistry::instance();
        registry.registerFenceLanguage("diff", "qrc:/a.qml");
        registry.reset();

        QCOMPARE(registry.kindForLanguage("diff"), 0);
        QCOMPARE(registry.kindForLanguage("mermaid"), BlockModel::MermaidKind);
        QVERIFY(registry.registeredDelegates().isEmpty());
    }
};

QTEST_MAIN(TestBlockKindRegistry)
#include "test_blockkindregistry.moc"
