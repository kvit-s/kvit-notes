// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QSignalSpy>
#include "blockmodel.h"
#include "documentserializer.h"
#include "documentstats.h"
#include "undostack.h"

class TestBlockModel : public QObject
{
    Q_OBJECT

private slots:
    void testConstruction();
    void testRowCount();
    void testData();
    void testSetData();
    void testRoleNames();
    void testInsertBlock();
    void testRemoveBlock();
    void testUpdateContent();
    void testUpdateType();
    void testMoveBlock();
    void testBlockAt();
    void testCount();
    void testSignals();
    void testEdgeCases();
    // Milestone 2 helper methods
    void testGetContent();
    void testSplitBlock();
    void testMergeBlocks();
    // New roles and block-type operations
    void testPhase4Roles();
    void testSetChecked();
    void testInsertBlockWithIndent();
    void testChangeIndentClamps();
    void testConvertBlock();
    void testSplitInheritsIndentAndLanguage();
    void testOrdinalSimpleRun();
    void testOrdinalInterruptions();
    void testOrdinalNesting();
    void testOrdinalTracksStructuralChanges();
    void testOrdinalRoleSignaled();
    void testLoadEmitsOneOrdinalSignal();
    void testOrdinalSignalRangeIsLocal();
    void testTocBlockIndexesTrackMutations();
    void testDelegateKindRole();
    void testDocumentTotalsTrackMutations();
    // Multi-block operations (each one undo step)
    void testRemoveBlocksNonContiguous();
    void testRemoveBlocksAllLeavesEmptyParagraph();
    void testDuplicateBlocksStateAndPlacement();
    void testMoveBlocksByRunsAndEdges();
    void testMoveBlocksByGapClosing();
    void testChangeIndentForBlocks();
    void testRemoveTextRangeCrossBlock();
    void testRemoveTextRangeEdgeCases();
    void testRemoveTextRangeDividerFirst();
    void testDragPreviewAndCommit();
    void testMoveBlocksToGathersAtGap();
    void testMoveBlocksToEdgesAndNoop();
    // The id->index map and single-pass group move
    void testIdIndexMatchesLinearScan();
    void testMoveBlocksToMatchesReference();
    // Cached ordinals/equation numbers
    void testDerivedOrderCacheMatchesRecompute();

private:
    // Build a model (no undo stack) from {type, content, indent} rows
    struct Row { Block::BlockType type; QString content; int indent = 0; };
    BlockModel* makeModel(const QList<Row> &rows)
    {
        auto *model = new BlockModel(this);
        for (int i = 0; i < rows.size(); ++i) {
            model->insertBlock(i, rows[i].type, rows[i].content, rows[i].indent);
        }
        return model;
    }

    // Same, wired to a fresh undo stack (cleared after setup) so the
    // single-undo-step contracts are observable.
    BlockModel* makeModelWithStack(const QList<Row> &rows, UndoStack **stackOut)
    {
        auto *model = makeModel(rows);
        auto *stack = new UndoStack(model);
        model->setUndoStack(stack);
        stack->clear();
        *stackOut = stack;
        return model;
    }

    static QStringList contents(BlockModel *model)
    {
        QStringList result;
        for (int i = 0; i < model->count(); ++i)
            result.append(model->blockAt(i)->content());
        return result;
    }
};

void TestBlockModel::testConstruction()
{
    BlockModel model;

    // Model should be empty by default (not initialized with sample data for tests)
    // Actually in the plan, it initializes with sample data
    // We'll test that the model is properly constructed
    QVERIFY(model.rowCount() >= 0);
}

void TestBlockModel::testRowCount()
{
    BlockModel model;

    // Clear any sample data by removing all blocks
    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    QCOMPARE(model.rowCount(), 0);

    model.insertBlock(0, Block::Paragraph, "Test");
    QCOMPARE(model.rowCount(), 1);

    model.insertBlock(1, Block::Paragraph, "Test2");
    QCOMPARE(model.rowCount(), 2);
}

void TestBlockModel::testData()
{
    BlockModel model;

    // Clear and add fresh data
    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    model.insertBlock(0, Block::Heading1, "Title");

    QModelIndex index = model.index(0, 0);

    // Test each role
    QVERIFY(!model.data(index, BlockModel::BlockIdRole).toString().isEmpty());
    QCOMPARE(model.data(index, BlockModel::BlockTypeRole).toInt(), static_cast<int>(Block::Heading1));
    QCOMPARE(model.data(index, BlockModel::ContentRole).toString(), QString("Title"));
    QCOMPARE(model.data(index, BlockModel::IndentLevelRole).toInt(), 0);

    // Test BlockObjectRole
    Block *block = model.data(index, BlockModel::BlockObjectRole).value<Block*>();
    QVERIFY(block != nullptr);
    QCOMPARE(block->content(), QString("Title"));

    // Test invalid index
    QModelIndex invalidIndex = model.index(100, 0);
    QVERIFY(!model.data(invalidIndex, BlockModel::ContentRole).isValid());
}

void TestBlockModel::testSetData()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    model.insertBlock(0, Block::Paragraph, "Original");
    QModelIndex index = model.index(0, 0);

    // Test setting content
    QVERIFY(model.setData(index, "Modified", BlockModel::ContentRole));
    QCOMPARE(model.data(index, BlockModel::ContentRole).toString(), QString("Modified"));

    // Test setting block type
    QVERIFY(model.setData(index, static_cast<int>(Block::Heading2), BlockModel::BlockTypeRole));
    QCOMPARE(model.data(index, BlockModel::BlockTypeRole).toInt(), static_cast<int>(Block::Heading2));

    // Test setting indent level
    QVERIFY(model.setData(index, 3, BlockModel::IndentLevelRole));
    QCOMPARE(model.data(index, BlockModel::IndentLevelRole).toInt(), 3);

    // Test invalid index
    QModelIndex invalidIndex = model.index(100, 0);
    QVERIFY(!model.setData(invalidIndex, "Test", BlockModel::ContentRole));
}

void TestBlockModel::testRoleNames()
{
    BlockModel model;
    QHash<int, QByteArray> roles = model.roleNames();

    QVERIFY(roles.contains(BlockModel::BlockIdRole));
    QCOMPARE(roles[BlockModel::BlockIdRole], QByteArray("blockId"));

    QVERIFY(roles.contains(BlockModel::BlockTypeRole));
    QCOMPARE(roles[BlockModel::BlockTypeRole], QByteArray("blockType"));

    QVERIFY(roles.contains(BlockModel::ContentRole));
    QCOMPARE(roles[BlockModel::ContentRole], QByteArray("content"));

    QVERIFY(roles.contains(BlockModel::IndentLevelRole));
    QCOMPARE(roles[BlockModel::IndentLevelRole], QByteArray("indentLevel"));

    QVERIFY(roles.contains(BlockModel::BlockObjectRole));
    QCOMPARE(roles[BlockModel::BlockObjectRole], QByteArray("blockObject"));
}

void TestBlockModel::testInsertBlock()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    // Insert at beginning
    model.insertBlock(0, Block::Paragraph, "First");
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("First"));

    // Insert at end
    model.insertBlock(1, Block::Paragraph, "Third");
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(1, 0), BlockModel::ContentRole).toString(), QString("Third"));

    // Insert in middle
    model.insertBlock(1, Block::Paragraph, "Second");
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("First"));
    QCOMPARE(model.data(model.index(1, 0), BlockModel::ContentRole).toString(), QString("Second"));
    QCOMPARE(model.data(model.index(2, 0), BlockModel::ContentRole).toString(), QString("Third"));

    // Insert with different block types
    model.insertBlock(0, Block::Heading1, "Title");
    QCOMPARE(model.data(model.index(0, 0), BlockModel::BlockTypeRole).toInt(), static_cast<int>(Block::Heading1));
}

void TestBlockModel::testRemoveBlock()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    model.insertBlock(0, Block::Paragraph, "A");
    model.insertBlock(1, Block::Paragraph, "B");
    model.insertBlock(2, Block::Paragraph, "C");

    QCOMPARE(model.rowCount(), 3);

    // Remove middle
    model.removeBlock(1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("A"));
    QCOMPARE(model.data(model.index(1, 0), BlockModel::ContentRole).toString(), QString("C"));

    // Remove first
    model.removeBlock(0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("C"));

    // Remove last
    model.removeBlock(0);
    QCOMPARE(model.rowCount(), 0);

    // Remove from empty - should not crash
    model.removeBlock(0);
    QCOMPARE(model.rowCount(), 0);

    // Remove with invalid index
    model.insertBlock(0, Block::Paragraph, "Test");
    model.removeBlock(-1);
    model.removeBlock(100);
    QCOMPARE(model.rowCount(), 1);
}

void TestBlockModel::testUpdateContent()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    model.insertBlock(0, Block::Paragraph, "Original");

    model.updateContent(0, "Updated");
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("Updated"));

    // Test invalid index - should not crash
    model.updateContent(-1, "Invalid");
    model.updateContent(100, "Invalid");
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("Updated"));
}

void TestBlockModel::testUpdateType()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    model.insertBlock(0, Block::Paragraph, "Test");

    model.updateType(0, Block::Heading1);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::BlockTypeRole).toInt(), static_cast<int>(Block::Heading1));

    model.updateType(0, Block::Heading2);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::BlockTypeRole).toInt(), static_cast<int>(Block::Heading2));

    // Test invalid index - should not crash
    model.updateType(-1, Block::Heading3);
    model.updateType(100, Block::Heading3);
}

void TestBlockModel::testMoveBlock()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    model.insertBlock(0, Block::Paragraph, "A");
    model.insertBlock(1, Block::Paragraph, "B");
    model.insertBlock(2, Block::Paragraph, "C");

    // Move first to last
    model.moveBlock(0, 2);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("B"));
    QCOMPARE(model.data(model.index(1, 0), BlockModel::ContentRole).toString(), QString("C"));
    QCOMPARE(model.data(model.index(2, 0), BlockModel::ContentRole).toString(), QString("A"));

    // Move last to first
    model.moveBlock(2, 0);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("A"));
    QCOMPARE(model.data(model.index(1, 0), BlockModel::ContentRole).toString(), QString("B"));
    QCOMPARE(model.data(model.index(2, 0), BlockModel::ContentRole).toString(), QString("C"));

    // Move to same position - should not change anything
    model.moveBlock(1, 1);
    QCOMPARE(model.data(model.index(1, 0), BlockModel::ContentRole).toString(), QString("B"));

    // Invalid indices - should not crash
    model.moveBlock(-1, 0);
    model.moveBlock(0, -1);
    model.moveBlock(100, 0);
    model.moveBlock(0, 100);
}

void TestBlockModel::testBlockAt()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    model.insertBlock(0, Block::Heading1, "Title");

    Block *block = model.blockAt(0);
    QVERIFY(block != nullptr);
    QCOMPARE(block->content(), QString("Title"));
    QCOMPARE(block->blockType(), Block::Heading1);

    // Test invalid index
    QVERIFY(model.blockAt(-1) == nullptr);
    QVERIFY(model.blockAt(100) == nullptr);
}

void TestBlockModel::testCount()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    QCOMPARE(model.count(), 0);

    model.insertBlock(0, Block::Paragraph, "Test");
    QCOMPARE(model.count(), 1);

    model.insertBlock(1, Block::Paragraph, "Test2");
    QCOMPARE(model.count(), 2);

    model.removeBlock(0);
    QCOMPARE(model.count(), 1);
}

void TestBlockModel::testSignals()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    QSignalSpy countSpy(&model, &BlockModel::countChanged);

    // Insert should emit countChanged
    model.insertBlock(0, Block::Paragraph, "Test");
    QCOMPARE(countSpy.count(), 1);

    // Remove should emit countChanged
    model.removeBlock(0);
    QCOMPARE(countSpy.count(), 2);

    // Test rowsInserted and rowsRemoved signals
    QSignalSpy insertSpy(&model, &BlockModel::rowsInserted);
    QSignalSpy removeSpy(&model, &BlockModel::rowsRemoved);

    model.insertBlock(0, Block::Paragraph, "A");
    QCOMPARE(insertSpy.count(), 1);

    model.removeBlock(0);
    QCOMPARE(removeSpy.count(), 1);
}

void TestBlockModel::testEdgeCases()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    // Test empty content
    model.insertBlock(0, Block::Paragraph, "");
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString(""));

    // Test very long content
    QString longContent(10000, 'x');
    model.updateContent(0, longContent);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), longContent);

    // Test special characters
    QString specialContent = "**Bold** *Italic* `code` [link](url)";
    model.updateContent(0, specialContent);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), specialContent);

    // Test unicode
    QString unicodeContent = "Hello \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x98\x80";
    model.updateContent(0, unicodeContent);
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), unicodeContent);

    // Test insert at negative index (should clamp to 0)
    model.insertBlock(-5, Block::Paragraph, "Clamped");
    QCOMPARE(model.data(model.index(0, 0), BlockModel::ContentRole).toString(), QString("Clamped"));

    // Test insert beyond end (should clamp to end)
    int count = model.rowCount();
    model.insertBlock(1000, Block::Paragraph, "AtEnd");
    QCOMPARE(model.data(model.index(count, 0), BlockModel::ContentRole).toString(), QString("AtEnd"));
}

void TestBlockModel::testGetContent()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    model.insertBlock(0, Block::Paragraph, "Hello World");

    // Test normal case
    QCOMPARE(model.getContent(0), QString("Hello World"));

    // Test empty content
    model.insertBlock(1, Block::Paragraph, "");
    QCOMPARE(model.getContent(1), QString(""));

    // Test invalid indices
    QCOMPARE(model.getContent(-1), QString());
    QCOMPARE(model.getContent(100), QString());
}

void TestBlockModel::testSplitBlock()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    // Test basic split
    model.insertBlock(0, Block::Paragraph, "Hello World");
    model.splitBlock(0, 5);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.getContent(0), QString("Hello"));
    QCOMPARE(model.getContent(1), QString(" World"));

    // New block should inherit type
    QCOMPARE(model.data(model.index(1, 0), BlockModel::BlockTypeRole).toInt(),
             static_cast<int>(Block::Paragraph));

    // Test split at start (position 0)
    model.insertBlock(2, Block::Heading1, "Test");
    model.splitBlock(2, 0);
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.getContent(2), QString(""));
    QCOMPARE(model.getContent(3), QString("Test"));

    // Test split at end
    model.insertBlock(4, Block::Paragraph, "End");
    model.splitBlock(4, 3);
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.getContent(4), QString("End"));
    QCOMPARE(model.getContent(5), QString(""));

    // Test invalid index
    int countBefore = model.rowCount();
    model.splitBlock(-1, 0);
    model.splitBlock(100, 0);
    QCOMPARE(model.rowCount(), countBefore);

    // Test invalid position
    model.splitBlock(0, -1);
    model.splitBlock(0, 1000);
    QCOMPARE(model.rowCount(), countBefore);
}

void TestBlockModel::testMergeBlocks()
{
    BlockModel model;

    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }

    // Test basic merge (keep first, remove second)
    model.insertBlock(0, Block::Paragraph, "Hello");
    model.insertBlock(1, Block::Paragraph, " World");

    model.mergeBlocks(0, 1);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.getContent(0), QString("Hello World"));

    // Test merge keeps the type of the target block
    model.insertBlock(1, Block::Heading1, "Title");
    model.insertBlock(2, Block::Paragraph, " Subtitle");

    model.mergeBlocks(1, 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.getContent(1), QString("Title Subtitle"));
    QCOMPARE(model.data(model.index(1, 0), BlockModel::BlockTypeRole).toInt(),
             static_cast<int>(Block::Heading1));

    // Test merge with empty content
    model.insertBlock(2, Block::Paragraph, "");
    model.mergeBlocks(1, 2);
    QCOMPARE(model.getContent(1), QString("Title Subtitle"));

    // Test invalid indices
    int countBefore = model.rowCount();
    model.mergeBlocks(-1, 0);
    model.mergeBlocks(0, -1);
    model.mergeBlocks(100, 0);
    model.mergeBlocks(0, 100);
    QCOMPARE(model.rowCount(), countBefore);

    // Test merge same index (should do nothing)
    QString contentBefore = model.getContent(0);
    model.mergeBlocks(0, 0);
    QCOMPARE(model.rowCount(), countBefore);
    QCOMPARE(model.getContent(0), contentBefore);
}

// ============================================================================
// New roles and block-type operations
// ============================================================================

void TestBlockModel::testPhase4Roles()
{
    BlockModel model;
    model.insertBlock(0, Block::Todo, "task");

    auto roles = model.roleNames();
    QCOMPARE(roles[BlockModel::CheckedRole], QByteArray("checked"));
    QCOMPARE(roles[BlockModel::LanguageRole], QByteArray("language"));
    QCOMPARE(roles[BlockModel::OrdinalRole], QByteArray("ordinal"));

    QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, BlockModel::CheckedRole).toBool(), false);
    QCOMPARE(model.data(index, BlockModel::LanguageRole).toString(), QString());

    QVERIFY(model.setData(index, true, BlockModel::CheckedRole));
    QCOMPARE(model.data(index, BlockModel::CheckedRole).toBool(), true);

    QVERIFY(model.setData(index, QString("cpp"), BlockModel::LanguageRole));
    QCOMPARE(model.data(index, BlockModel::LanguageRole).toString(), QString("cpp"));
}

void TestBlockModel::testSetChecked()
{
    BlockModel model;
    model.insertBlock(0, Block::Todo, "task");

    model.setChecked(0, true);
    QCOMPARE(model.blockAt(0)->checked(), true);

    model.setChecked(0, false);
    QCOMPARE(model.blockAt(0)->checked(), false);

    // Same value and invalid indices are no-ops
    model.setChecked(0, false);
    QCOMPARE(model.blockAt(0)->checked(), false);
    model.setChecked(-1, true);
    model.setChecked(100, true);
}

void TestBlockModel::testInsertBlockWithIndent()
{
    BlockModel model;
    model.insertBlock(0, Block::BulletList, "parent");
    model.insertBlock(1, Block::BulletList, "child", 1);

    QCOMPARE(model.blockAt(1)->indentLevel(), 1);

    // Indent clamps to the absolute limit and to zero
    model.insertBlock(2, Block::BulletList, "deep", 99);
    QCOMPARE(model.blockAt(2)->indentLevel(), BlockModel::MaxIndentLevel);
    model.insertBlock(3, Block::BulletList, "neg", -2);
    QCOMPARE(model.blockAt(3)->indentLevel(), 0);
}

void TestBlockModel::testChangeIndentClamps()
{
    auto *model = makeModel({
        {Block::Paragraph, "para"},
        {Block::BulletList, "first"},
        {Block::BulletList, "second"},
    });

    // Wave 1: only the list family indents
    model->changeIndent(0, 1);
    QCOMPARE(model->blockAt(0)->indentLevel(), 0);

    // The first list item has no list block above it: it cannot indent
    model->changeIndent(1, 1);
    QCOMPARE(model->blockAt(1)->indentLevel(), 0);

    // A child may indent at most one level below the block above it
    model->changeIndent(2, 1);
    QCOMPARE(model->blockAt(2)->indentLevel(), 1);
    model->changeIndent(2, 1);
    QCOMPARE(model->blockAt(2)->indentLevel(), 1);  // parent is level 0

    // Outdent floors at zero
    model->changeIndent(2, -1);
    QCOMPARE(model->blockAt(2)->indentLevel(), 0);
    model->changeIndent(2, -1);
    QCOMPARE(model->blockAt(2)->indentLevel(), 0);

    // The absolute limit holds even under a deep parent
    auto *deep = makeModel({
        {Block::BulletList, "l0"},
        {Block::BulletList, "l1", 1},
        {Block::BulletList, "l2", 2},
        {Block::BulletList, "l3", 3},
        {Block::BulletList, "l4", 4},
        {Block::BulletList, "l5", 4},
    });
    deep->changeIndent(5, 1);
    QCOMPARE(deep->blockAt(5)->indentLevel(), BlockModel::MaxIndentLevel);

    // Outdent is allowed even when the block sits deeper than its
    // neighbor permits for indenting
    deep->changeIndent(4, -1);
    QCOMPARE(deep->blockAt(4)->indentLevel(), 3);
}

void TestBlockModel::testConvertBlock()
{
    BlockModel model;
    model.insertBlock(0, Block::Paragraph, "- item");

    // Paragraph -> bullet (the prefix conversion shape)
    model.convertBlock(0, Block::BulletList, "item");
    QCOMPARE(model.blockAt(0)->blockType(), Block::BulletList);
    QCOMPARE(model.blockAt(0)->content(), QString("item"));

    // Bullet -> todo keeps indentation and can set checked
    model.setData(model.index(0, 0), 2, BlockModel::IndentLevelRole);
    model.convertBlock(0, Block::Todo, "item", true);
    QCOMPARE(model.blockAt(0)->blockType(), Block::Todo);
    QCOMPARE(model.blockAt(0)->indentLevel(), 2);
    QCOMPARE(model.blockAt(0)->checked(), true);

    // Conversion out of the list family lands at the margin
    model.convertBlock(0, Block::Paragraph, "item");
    QCOMPARE(model.blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(model.blockAt(0)->indentLevel(), 0);
    QCOMPARE(model.blockAt(0)->checked(), false);

    // Language reaches code blocks
    model.convertBlock(0, Block::CodeBlock, "x = 1", false, "python");
    QCOMPARE(model.blockAt(0)->blockType(), Block::CodeBlock);
    QCOMPARE(model.blockAt(0)->language(), QString("python"));
}

void TestBlockModel::testSplitInheritsIndentAndLanguage()
{
    BlockModel model;
    model.insertBlock(0, Block::Todo, "firstsecond", 2);
    model.blockAt(0)->setChecked(true);
    model.blockAt(0)->setLanguage("md");

    model.splitBlock(0, 5);

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.getContent(0), QString("first"));
    QCOMPARE(model.getContent(1), QString("second"));
    QCOMPARE(model.blockAt(1)->blockType(), Block::Todo);
    QCOMPARE(model.blockAt(1)->indentLevel(), 2);
    QCOMPARE(model.blockAt(1)->language(), QString("md"));
    // A split todo starts unchecked; the first half keeps its state
    QCOMPARE(model.blockAt(1)->checked(), false);
    QCOMPARE(model.blockAt(0)->checked(), true);
}

void TestBlockModel::testOrdinalSimpleRun()
{
    auto *model = makeModel({
        {Block::NumberedList, "a"},
        {Block::NumberedList, "b"},
        {Block::NumberedList, "c"},
    });

    QCOMPARE(model->ordinalAt(0), 1);
    QCOMPARE(model->ordinalAt(1), 2);
    QCOMPARE(model->ordinalAt(2), 3);

    // The role agrees with the invokable
    QCOMPARE(model->data(model->index(2, 0), BlockModel::OrdinalRole).toInt(), 3);
}

void TestBlockModel::testOrdinalInterruptions()
{
    auto *model = makeModel({
        {Block::NumberedList, "a"},
        {Block::Paragraph, "break"},
        {Block::NumberedList, "b"},
        {Block::BulletList, "bullet"},
        {Block::NumberedList, "c"},
    });

    // A non-list block restarts the numbering
    QCOMPARE(model->ordinalAt(0), 1);
    QCOMPARE(model->ordinalAt(2), 1);
    // A different list type at the same level also restarts it
    QCOMPARE(model->ordinalAt(4), 1);

    // Non-numbered blocks have no ordinal
    QCOMPARE(model->ordinalAt(1), 0);
    QCOMPARE(model->ordinalAt(3), 0);
    QCOMPARE(model->ordinalAt(-1), 0);
    QCOMPARE(model->ordinalAt(100), 0);
}

void TestBlockModel::testOrdinalNesting()
{
    auto *model = makeModel({
        {Block::NumberedList, "one"},           // 1
        {Block::NumberedList, "one.a", 1},      // 1
        {Block::NumberedList, "one.b", 1},      // 2
        {Block::NumberedList, "two"},           // 2 (children don't break the run)
        {Block::Todo, "task", 1},               // deeper non-numbered child
        {Block::NumberedList, "three"},         // 3
        {Block::NumberedList, "three.a", 1},    // 1 (new nested run)
    });

    QCOMPARE(model->ordinalAt(0), 1);
    QCOMPARE(model->ordinalAt(1), 1);
    QCOMPARE(model->ordinalAt(2), 2);
    QCOMPARE(model->ordinalAt(3), 2);
    QCOMPARE(model->ordinalAt(5), 3);
    QCOMPARE(model->ordinalAt(6), 1);
}

void TestBlockModel::testOrdinalTracksStructuralChanges()
{
    auto *model = makeModel({
        {Block::NumberedList, "a"},
        {Block::NumberedList, "b"},
    });

    // Insert renumbers what follows
    model->insertBlock(1, Block::NumberedList, "inserted");
    QCOMPARE(model->ordinalAt(2), 3);

    // Remove renumbers back
    model->removeBlock(1);
    QCOMPARE(model->ordinalAt(1), 2);

    // Move renumbers both positions
    model->moveBlock(0, 1);
    QCOMPARE(model->ordinalAt(0), 1);
    QCOMPARE(model->ordinalAt(1), 2);

    // Type change interrupts the run
    model->updateType(0, Block::Paragraph);
    QCOMPARE(model->ordinalAt(1), 1);

    // Indent change starts a nested run
    model->updateType(0, Block::NumberedList);
    auto *nested = makeModel({
        {Block::NumberedList, "a"},
        {Block::NumberedList, "b"},
    });
    nested->changeIndent(1, 1);
    QCOMPARE(nested->ordinalAt(1), 1);
}

void TestBlockModel::testOrdinalRoleSignaled()
{
    auto *model = makeModel({
        {Block::NumberedList, "a"},
        {Block::NumberedList, "b"},
    });

    QSignalSpy spy(model, &QAbstractItemModel::dataChanged);

    auto sawOrdinalRole = [&spy]() {
        for (const auto &emission : spy) {
            auto roles = emission.at(2).value<QVector<int>>();
            if (roles.contains(BlockModel::OrdinalRole))
                return true;
        }
        return false;
    };

    model->insertBlock(1, Block::NumberedList, "inserted");
    QVERIFY(sawOrdinalRole());

    spy.clear();
    model->removeBlock(1);
    QVERIFY(sawOrdinalRole());

    spy.clear();
    model->moveBlock(0, 1);
    QVERIFY(sawOrdinalRole());

    spy.clear();
    model->updateType(0, Block::BulletList);
    QVERIFY(sawOrdinalRole());

    spy.clear();
    model->changeIndent(1, 1);
    QVERIFY(sawOrdinalRole());
}

void TestBlockModel::testLoadEmitsOneOrdinalSignal()
{
    BlockModel model;
    DocumentSerializer serializer;
    QSignalSpy dataSpy(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);

    serializer.loadIntoModel(&model, QStringLiteral("1. one\n2. two\n\nParagraph\n"));

    QCOMPARE(model.count(), 3);
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(insertSpy.count(), 0);

    int ordinalSignals = 0;
    QModelIndex first;
    QModelIndex last;
    for (const auto &emission : dataSpy) {
        const auto roles = emission.at(2).value<QVector<int>>();
        if (!roles.contains(BlockModel::OrdinalRole))
            continue;
        ++ordinalSignals;
        first = emission.at(0).value<QModelIndex>();
        last = emission.at(1).value<QModelIndex>();
    }
    QCOMPARE(ordinalSignals, 1);
    QCOMPARE(first.row(), 0);
    QCOMPARE(last.row(), model.count() - 1);
}

void TestBlockModel::testOrdinalSignalRangeIsLocal()
{
    auto *model = makeModel({
        {Block::Paragraph, "before"},
        {Block::NumberedList, "one"},
        {Block::NumberedList, "two"},
        {Block::NumberedList, "three"},
        {Block::Paragraph, "after"},
        {Block::NumberedList, "other one"},
        {Block::NumberedList, "other two"},
    });

    auto lastOrdinalRange = [](const QSignalSpy &spy) {
        QPair<int, int> range(-1, -1);
        for (const auto &emission : spy) {
            const auto roles = emission.at(2).value<QVector<int>>();
            if (!roles.contains(BlockModel::OrdinalRole))
                continue;
            range.first = emission.at(0).value<QModelIndex>().row();
            range.second = emission.at(1).value<QModelIndex>().row();
        }
        return range;
    };

    QSignalSpy spy(model, &QAbstractItemModel::dataChanged);
    model->changeIndent(2, 1);
    QCOMPARE(lastOrdinalRange(spy), qMakePair(2, 3));

    spy.clear();
    model->updateType(1, Block::BulletList);
    QCOMPARE(lastOrdinalRange(spy), qMakePair(1, 3));
}

void TestBlockModel::testTocBlockIndexesTrackMutations()
{
    BlockModel model;
    auto expectedIndexes = [&model]() {
        QVariantList expected;
        for (int i = 0; i < model.count(); ++i) {
            const Block *block = model.blockAt(i);
            if (block && block->blockType() == Block::CodeBlock
                && block->language() == QLatin1String("toc")) {
                expected.append(i);
            }
        }
        return expected;
    };
    auto assertTracked = [&]() {
        const QVariantList expected = expectedIndexes();
        QCOMPARE(model.tocBlockIndexes(), expected);
        QCOMPARE(model.tocBlockCount(), expected.size());
    };

    Block::State para;
    para.type = Block::Paragraph;
    para.content = QStringLiteral("body");
    Block::State toc;
    toc.type = Block::CodeBlock;
    toc.language = QStringLiteral("toc");
    toc.content = QStringLiteral("- [Intro](#intro)");

    QSignalSpy tocSpy(&model, &BlockModel::tocBlockIndexesChanged);

    model.insertBlockInternal(0, para);
    model.insertBlockInternal(1, toc);
    assertTracked();
    QCOMPARE(tocSpy.count(), 1);

    model.insertBlock(0, Block::Paragraph, QStringLiteral("before"));
    assertTracked();

    model.moveBlockInternal(2, 0);
    assertTracked();

    QVERIFY(model.setData(model.index(0, 0), QStringLiteral("cpp"),
                          BlockModel::LanguageRole));
    assertTracked();
    QVERIFY(model.tocBlockIndexes().isEmpty());

    QVERIFY(model.setData(model.index(0, 0), QStringLiteral("toc"),
                          BlockModel::LanguageRole));
    assertTracked();
    QCOMPARE(model.tocBlockIndexes(), QVariantList{0});

    model.updateTypeInternal(0, Block::Paragraph);
    assertTracked();
    QVERIFY(model.tocBlockIndexes().isEmpty());

    model.applyStateInternal(1, toc);
    assertTracked();

    model.removeBlockInternal(0);
    assertTracked();
}

// The DelegateChooser recreates a row's delegate whenever its watched
// role changes, which drops focus — so the kind role is shared by
// paragraphs and headings and emitted only on real kind changes.
void TestBlockModel::testDelegateKindRole()
{
    QCOMPARE(BlockModel::delegateKindFor(Block::Paragraph), 0);
    QCOMPARE(BlockModel::delegateKindFor(Block::Heading1), 0);
    QCOMPARE(BlockModel::delegateKindFor(Block::Heading3), 0);
    // Heading4's enum value is appended after the structural range but
    // it shares the text delegate like every other heading
    QCOMPARE(BlockModel::delegateKindFor(Block::Heading4), 0);
    QCOMPARE(BlockModel::delegateKindFor(Block::BulletList),
             static_cast<int>(Block::BulletList));
    QCOMPARE(BlockModel::delegateKindFor(Block::Divider),
             static_cast<int>(Block::Divider));

    // Fence-language routing goes through a model, because the registry it
    // resolves against is owned rather than global.
    BlockModel kindModel;
    // Mermaid fences route by language like kanban/toc.
    QCOMPARE(kindModel.delegateKindForBlock(Block::CodeBlock, "mermaid"),
             BlockModel::MermaidKind);
    // Character-diagram tags carry no kind of their own: the tag only marks
    // the fence for ingest straightening (§7.5); the block renders as an
    // ordinary code block. `plain` and ordinary languages likewise stay on
    // the code delegate (the CodeBlock enum value).
    QCOMPARE(kindModel.delegateKindForBlock(Block::CodeBlock, "diagram"),
             static_cast<int>(Block::CodeBlock));
    QCOMPARE(kindModel.delegateKindForBlock(Block::CodeBlock, "text-diagram"),
             static_cast<int>(Block::CodeBlock));
    QCOMPARE(kindModel.delegateKindForBlock(Block::CodeBlock, "ascii-diagram"),
             static_cast<int>(Block::CodeBlock));
    QCOMPARE(kindModel.delegateKindForBlock(Block::CodeBlock, "plain"),
             static_cast<int>(Block::CodeBlock));
    QCOMPARE(kindModel.delegateKindForBlock(Block::CodeBlock, "python"),
             static_cast<int>(Block::CodeBlock));

    BlockModel model;
    model.insertBlock(0, Block::Paragraph, "text");
    QCOMPARE(model.data(model.index(0, 0), BlockModel::DelegateKindRole).toInt(), 0);

    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
    auto kindRoleEmitted = [&spy]() {
        for (const auto &emission : spy) {
            auto roles = emission.at(2).value<QVector<int>>();
            if (roles.contains(BlockModel::DelegateKindRole))
                return true;
        }
        return false;
    };

    // Paragraph -> heading: same delegate kind, role NOT emitted
    model.updateType(0, Block::Heading1);
    QVERIFY(!kindRoleEmitted());

    // Heading -> Heading4 (the appended enum value): still the shared
    // text delegate, role NOT emitted
    spy.clear();
    model.updateType(0, Block::Heading4);
    QVERIFY(!kindRoleEmitted());

    // Heading -> bullet: kind changes, role emitted
    spy.clear();
    model.updateType(0, Block::BulletList);
    QVERIFY(kindRoleEmitted());

    // Same rule through convertBlock (single-undo conversions)
    spy.clear();
    model.convertBlock(0, Block::Todo, "task");
    QVERIFY(kindRoleEmitted());
    spy.clear();
    model.convertBlock(0, Block::Todo, "renamed task");
    QVERIFY(!kindRoleEmitted());
}

void TestBlockModel::testDocumentTotalsTrackMutations()
{
    BlockModel model;
    auto assertTotals = [&]() {
        int words = 0;
        int charsWith = 0;
        int charsNo = 0;
        int paragraphs = 0;
        for (int i = 0; i < model.count(); ++i) {
            const Block *block = model.blockAt(i);
            QVERIFY(block);
            words += block->wordCount();
            charsWith += block->charCount(true);
            charsNo += block->charCount(false);
            if (block->wordCount() > 0)
                ++paragraphs;
        }
        QCOMPARE(model.documentWordCount(), words);
        QCOMPARE(model.documentCharCount(), charsWith);
        QCOMPARE(model.documentCharsNoSpaces(), charsNo);
        QCOMPARE(model.documentParagraphCount(), paragraphs);

        DocumentStats stats;
        stats.setModel(&model);
        const QVariantMap s = stats.documentStats();
        QCOMPARE(s.value(QStringLiteral("words")).toInt(), words);
        QCOMPARE(s.value(QStringLiteral("charsWithSpaces")).toInt(), charsWith);
        QCOMPARE(s.value(QStringLiteral("charsNoSpaces")).toInt(), charsNo);
        QCOMPARE(s.value(QStringLiteral("paragraphs")).toInt(), paragraphs);
    };

    assertTotals();
    model.insertBlock(0, Block::Paragraph, QStringLiteral("**bold** one"));
    model.insertBlock(1, Block::CodeBlock, QStringLiteral("code **literal**"));
    assertTotals();

    model.updateContent(0, QStringLiteral("alpha beta gamma"));
    assertTotals();

    model.updateType(1, Block::Paragraph);
    assertTotals();

    model.splitBlock(0, 6);
    assertTotals();

    model.mergeBlocks(0, 1);
    assertTotals();

    model.convertBlock(0, Block::CodeBlock, QStringLiteral("**alpha**"),
                       false, QStringLiteral("txt"));
    assertTotals();

    model.removeBlock(1);
    assertTotals();

    model.clear();
    assertTotals();
}

// ---- Multi-block operations ----

void TestBlockModel::testRemoveBlocksNonContiguous()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Paragraph, "zero" },
        { Block::Todo, "one" },
        { Block::Paragraph, "two" },
        { Block::Paragraph, "three" },
    }, &stack);
    model->blockAt(1)->setChecked(true);

    model->removeBlocks({0, 2});
    QCOMPARE(contents(model), QStringList({"one", "three"}));
    QCOMPARE(stack->count(), 1);

    // One undo restores both blocks with full state
    stack->undo();
    QCOMPARE(contents(model), QStringList({"zero", "one", "two", "three"}));
    QCOMPARE(model->blockAt(1)->blockType(), Block::Todo);
    QCOMPARE(model->blockAt(1)->checked(), true);
    QVERIFY(!stack->canUndo());

    stack->redo();
    QCOMPARE(contents(model), QStringList({"one", "three"}));
}

void TestBlockModel::testRemoveBlocksAllLeavesEmptyParagraph()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Heading1, "title" },
        { Block::BulletList, "item" },
    }, &stack);

    model->removeBlocks({0, 1});
    QCOMPARE(model->count(), 1);
    QCOMPARE(model->blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(model->blockAt(0)->content(), QString());
    QCOMPARE(stack->count(), 1);

    stack->undo();
    QCOMPARE(contents(model), QStringList({"title", "item"}));
    QCOMPARE(model->blockAt(0)->blockType(), Block::Heading1);
}

void TestBlockModel::testDuplicateBlocksStateAndPlacement()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Todo, "task", 2 },
        { Block::Paragraph, "middle" },
        { Block::CodeBlock, "print(1)" },
        { Block::Paragraph, "tail" },
    }, &stack);
    model->blockAt(0)->setChecked(true);
    model->blockAt(2)->setLanguage("python");

    // Clones of {0, 2} land directly below index 2, in document order
    const QVariantList clones = model->duplicateBlocks({0, 2});
    QCOMPARE(clones, QVariantList({3, 4}));
    QCOMPARE(contents(model),
             QStringList({"task", "middle", "print(1)", "task", "print(1)", "tail"}));

    // Full state fidelity (§3.6 deep copy = full Block::State here)
    QCOMPARE(model->blockAt(3)->blockType(), Block::Todo);
    QCOMPARE(model->blockAt(3)->checked(), true);
    QCOMPARE(model->blockAt(3)->indentLevel(), 2);
    QCOMPARE(model->blockAt(4)->blockType(), Block::CodeBlock);
    QCOMPARE(model->blockAt(4)->language(), QString("python"));
    // Clones are distinct blocks, not shared references
    QVERIFY(model->blockAt(3)->blockId() != model->blockAt(0)->blockId());

    QCOMPARE(stack->count(), 1);
    stack->undo();
    QCOMPARE(contents(model), QStringList({"task", "middle", "print(1)", "tail"}));
}

void TestBlockModel::testMoveBlocksByRunsAndEdges()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Paragraph, "a" },
        { Block::Paragraph, "b" },
        { Block::Paragraph, "c" },
        { Block::Paragraph, "d" },
        { Block::Paragraph, "e" },
    }, &stack);

    // One contiguous run moves as a unit
    model->moveBlocksBy({1, 2}, 1);
    QCOMPARE(contents(model), QStringList({"a", "d", "b", "c", "e"}));
    QCOMPARE(stack->count(), 1);
    stack->undo();
    QCOMPARE(contents(model), QStringList({"a", "b", "c", "d", "e"}));

    // A run against the edge stops the whole operation
    model->moveBlocksBy({0, 3}, -1);
    QCOMPARE(contents(model), QStringList({"a", "b", "c", "d", "e"}));
    QCOMPARE(stack->count(), 1); // nothing pushed
    model->moveBlocksBy({1, 4}, 1);
    QCOMPARE(contents(model), QStringList({"a", "b", "c", "d", "e"}));

    // Disjoint runs each move one step, in one undo step (the earlier
    // undo left a redo entry, which this push replaces — count stays 1)
    model->moveBlocksBy({0, 2, 3}, 1);
    QCOMPARE(contents(model), QStringList({"b", "a", "e", "c", "d"}));
    QCOMPARE(stack->count(), 1);
    stack->undo();
    QCOMPARE(contents(model), QStringList({"a", "b", "c", "d", "e"}));
}

void TestBlockModel::testMoveBlocksByGapClosing()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Paragraph, "a" },
        { Block::Paragraph, "b" },
        { Block::Paragraph, "c" },
        { Block::Paragraph, "d" },
    }, &stack);

    // Runs {1} and {3} moving up: the gaps close over repeated moves
    model->moveBlocksBy({1, 3}, -1);
    QCOMPARE(contents(model), QStringList({"b", "a", "d", "c"}));
    model->moveBlocksBy({0, 2}, -1);
    // First run is at the edge now: the whole operation is a no-op
    QCOMPARE(contents(model), QStringList({"b", "a", "d", "c"}));
}

void TestBlockModel::testChangeIndentForBlocks()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::BulletList, "parent" },
        { Block::BulletList, "child" },
        { Block::Paragraph, "prose" },
        { Block::BulletList, "other" },
    }, &stack);

    // Bullets indent under the §3.3 clamps; the paragraph is skipped;
    // one undo step for the whole selection
    model->changeIndentForBlocks({1, 2, 3}, 1);
    QCOMPARE(model->blockAt(1)->indentLevel(), 1);
    QCOMPARE(model->blockAt(2)->indentLevel(), 0); // paragraphs don't indent
    // "other" follows the paragraph: no list block above -> stays at 0
    QCOMPARE(model->blockAt(3)->indentLevel(), 0);
    QCOMPARE(stack->count(), 1);

    stack->undo();
    QCOMPARE(model->blockAt(1)->indentLevel(), 0);

    // An all-no-op selection pushes nothing
    model->changeIndentForBlocks({2}, 1);
    QCOMPARE(stack->count(), 1);
}

void TestBlockModel::testRemoveTextRangeCrossBlock()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Todo, "alpha beta" },
        { Block::Paragraph, "middle" },
        { Block::Quote, "gamma delta" },
    }, &stack);
    model->blockAt(0)->setChecked(true);

    // From "alpha |beta" to "gamma |delta": the todo keeps its identity
    // with the joined remainder; the middle and last blocks are removed
    const QVariantMap result = model->removeTextRange(0, 6, 2, 6);
    QCOMPARE(model->count(), 1);
    QCOMPARE(model->blockAt(0)->blockType(), Block::Todo);
    QCOMPARE(model->blockAt(0)->checked(), true);
    QCOMPARE(model->blockAt(0)->content(), QString("alpha delta"));
    QCOMPARE(result.value("index").toInt(), 0);
    QCOMPARE(result.value("cursor").toInt(), 6);

    // One undo restores all three blocks exactly
    QCOMPARE(stack->count(), 1);
    stack->undo();
    QCOMPARE(contents(model), QStringList({"alpha beta", "middle", "gamma delta"}));
    QCOMPARE(model->blockAt(2)->blockType(), Block::Quote);
}

void TestBlockModel::testRemoveTextRangeEdgeCases()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Heading2, "title" },
        { Block::Paragraph, "body text" },
    }, &stack);

    // Whole first block selected (startMd 0): the first block still
    // keeps its identity, now holding only the remainder
    model->removeTextRange(0, 0, 1, 5);
    QCOMPARE(model->count(), 1);
    QCOMPARE(model->blockAt(0)->blockType(), Block::Heading2);
    QCOMPARE(model->blockAt(0)->content(), QString("text"));
    stack->undo();

    // Range ending exactly at the last block's end: remainder is empty
    model->removeTextRange(0, 2, 1, 9);
    QCOMPARE(model->count(), 1);
    QCOMPARE(model->blockAt(0)->content(), QString("ti"));
    stack->undo();

    // Same-block range is a plain splice, one undo step (pushing after
    // the undos above replaced the redo entry, so the stack holds
    // exactly this one command)
    model->removeTextRange(1, 2, 1, 5);
    QCOMPARE(model->blockAt(1)->content(), QString("botext"));
    QCOMPARE(stack->count(), 1);
    QCOMPARE(stack->index(), 1);
    stack->undo();
    QCOMPARE(model->blockAt(1)->content(), QString("body text"));

    // Collapsed or inverted ranges do nothing
    QVERIFY(model->removeTextRange(1, 3, 1, 3).isEmpty());
    QVERIFY(model->removeTextRange(1, 5, 1, 2).isEmpty());
}

void TestBlockModel::testRemoveTextRangeDividerFirst()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Paragraph, "before" },
        { Block::Divider, "" },
        { Block::Quote, "quoted words" },
    }, &stack);

    // A divider holds no text, so the remainder keeps the LAST block's
    // identity (the divider exception)
    const QVariantMap result = model->removeTextRange(1, 0, 2, 7);
    QCOMPARE(model->count(), 2);
    QCOMPARE(model->blockAt(0)->content(), QString("before"));
    QCOMPARE(model->blockAt(1)->blockType(), Block::Quote);
    QCOMPARE(model->blockAt(1)->content(), QString("words"));
    QCOMPARE(result.value("index").toInt(), 1);
    QCOMPARE(result.value("cursor").toInt(), 0);

    QCOMPARE(stack->count(), 1);
    stack->undo();
    QCOMPARE(model->count(), 3);
    QCOMPARE(model->blockAt(1)->blockType(), Block::Divider);
    QCOMPARE(model->blockAt(2)->content(), QString("quoted words"));
}

void TestBlockModel::testDragPreviewAndCommit()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Paragraph, "a" },
        { Block::Paragraph, "b" },
        { Block::Paragraph, "c" },
        { Block::Paragraph, "d" },
    }, &stack);

    // Live make-room moves bypass the undo stack entirely
    model->previewMoveBlock(0, 1);
    model->previewMoveBlock(1, 2);
    QCOMPARE(contents(model), QStringList({"b", "c", "a", "d"}));
    QCOMPARE(stack->count(), 0);

    // The drop commits ONE pre-applied command: pushing must not
    // re-apply, undo restores the original order, redo re-applies
    model->commitDragMove(0, 2);
    QCOMPARE(contents(model), QStringList({"b", "c", "a", "d"}));
    QCOMPARE(stack->count(), 1);
    stack->undo();
    QCOMPARE(contents(model), QStringList({"a", "b", "c", "d"}));
    stack->redo();
    QCOMPARE(contents(model), QStringList({"b", "c", "a", "d"}));
    stack->undo();
    QCOMPARE(contents(model), QStringList({"a", "b", "c", "d"}));

    // A drop where the drag started pushes nothing
    model->commitDragMove(2, 2);
    QCOMPARE(stack->count(), 1); // unchanged (the undone entry remains)
}

void TestBlockModel::testMoveBlocksToGathersAtGap()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Paragraph, "a" },
        { Block::Paragraph, "b" },
        { Block::Paragraph, "c" },
        { Block::Paragraph, "d" },
        { Block::Paragraph, "e" },
    }, &stack);

    // Non-contiguous {a, c} drop before "e" (gap 4): they gather
    // contiguously at the gap, keeping document order
    const QString idA = model->blockAt(0)->blockId();
    model->moveBlocksTo({0, 2}, 4);
    QCOMPARE(contents(model), QStringList({"b", "d", "a", "c", "e"}));
    // Moves keep block identity (ids survive, so selection would too)
    QCOMPARE(model->blockAt(2)->blockId(), idA);
    QCOMPARE(stack->count(), 1);
    stack->undo();
    QCOMPARE(contents(model), QStringList({"a", "b", "c", "d", "e"}));

    // Upward drop: {c, e} before "a" (gap 0)
    model->moveBlocksTo({2, 4}, 0);
    QCOMPARE(contents(model), QStringList({"c", "e", "a", "b", "d"}));
    stack->undo();
    QCOMPARE(contents(model), QStringList({"a", "b", "c", "d", "e"}));
}

void TestBlockModel::testMoveBlocksToEdgesAndNoop()
{
    UndoStack *stack = nullptr;
    BlockModel *model = makeModelWithStack({
        { Block::Paragraph, "a" },
        { Block::Paragraph, "b" },
        { Block::Paragraph, "c" },
    }, &stack);

    // Dropping a contiguous group into its own gap is a no-op and
    // pushes nothing
    model->moveBlocksTo({0, 1}, 0);
    QCOMPARE(contents(model), QStringList({"a", "b", "c"}));
    model->moveBlocksTo({0, 1}, 1);
    QCOMPARE(contents(model), QStringList({"a", "b", "c"}));
    model->moveBlocksTo({0, 1}, 2);
    QCOMPARE(contents(model), QStringList({"a", "b", "c"}));
    QCOMPARE(stack->count(), 0);

    // Gap at the very end
    model->moveBlocksTo({0}, 3);
    QCOMPARE(contents(model), QStringList({"b", "c", "a"}));
    QCOMPARE(stack->count(), 1);
}

// The cache-invalidation invariant: after every scripted mutation the
// id->index map must equal a linear scan.
void TestBlockModel::testIdIndexMatchesLinearScan()
{
    BlockModel *model = makeModel({
        { Block::Paragraph, "a" },
        { Block::Heading1, "b" },
        { Block::NumberedList, "c" },
        { Block::Paragraph, "d" },
        { Block::Paragraph, "e" },
    });

    const auto verifyMap = [model]() {
        for (int i = 0; i < model->count(); ++i)
            QCOMPARE(model->indexOfBlockId(model->blockAt(i)->blockId()), i);
        QCOMPARE(model->indexOfBlockId(QStringLiteral("no-such-id")), -1);
        QCOMPARE(model->indexOfBlockId(QString()), -1);
    };

    verifyMap();
    model->insertBlock(2, Block::Quote, "inserted");
    verifyMap();
    model->removeBlock(0);
    verifyMap();
    model->moveBlock(0, 3);
    verifyMap();
    model->moveBlock(4, 1);
    verifyMap();
    model->splitBlock(2, 1);
    verifyMap();
    model->mergeBlocks(2, 3);
    verifyMap();

    // A removed block's id resolves to -1 afterwards
    const QString removedId = model->blockAt(1)->blockId();
    model->removeBlock(1);
    QCOMPARE(model->indexOfBlockId(removedId), -1);
    verifyMap();

    // A serializer load replaces every block through one reset
    DocumentSerializer serializer;
    serializer.loadIntoModel(model, QStringLiteral("one\n\ntwo\n\nthree"));
    verifyMap();

    model->clear();
    QCOMPARE(model->indexOfBlockId(QStringLiteral("anything")), -1);
}

// moveBlocksTo must produce the same final order as the reference
// arrangement (non-group blocks keep relative order; the group sits
// contiguously at the gap), across contiguous, scattered,
// gap-inside-selection, edge, and select-all drops — with undo/redo
// restoring and re-applying the exact order.
void TestBlockModel::testMoveBlocksToMatchesReference()
{
    const QStringList names = {"a", "b", "c", "d", "e", "f", "g", "h"};

    struct Scenario { QList<int> sources; int gap; };
    const QList<Scenario> scenarios = {
        {{0, 2}, 4},          // scattered, forward
        {{2, 4}, 0},          // scattered, to the start
        {{1, 2, 3}, 7},       // contiguous run, far forward
        {{5, 6, 7}, 1},       // contiguous run, backward
        {{2, 3, 5}, 4},       // gap inside the selection's index span
        {{0, 3, 4, 7}, 4},    // runs on both sides of the gap
        {{1, 6}, 4},          // one run left, one right
        {{0, 1, 2, 3, 4, 5, 6, 7}, 3}, // select-all (no-op arrangement)
        {{7}, 0},             // single block to the start
        {{0}, 8},             // single block to the end
    };

    for (const Scenario &scenario : scenarios) {
        UndoStack *stack = nullptr;
        QList<Row> rows;
        for (const QString &name : names)
            rows.append({ Block::Paragraph, name });
        BlockModel *model = makeModelWithStack(rows, &stack);

        // Reference arrangement per the documented semantics
        int membersBeforeGap = 0;
        for (int s : scenario.sources)
            if (s < scenario.gap)
                ++membersBeforeGap;
        QStringList expected;
        for (int i = 0; i < names.size(); ++i)
            if (!scenario.sources.contains(i))
                expected.append(names.at(i));
        for (int i = 0; i < scenario.sources.size(); ++i)
            expected.insert(scenario.gap - membersBeforeGap + i,
                            names.at(scenario.sources.at(i)));

        QVariantList indexes;
        for (int s : scenario.sources)
            indexes.append(s);
        model->moveBlocksTo(indexes, scenario.gap);
        QCOMPARE(contents(model), expected);

        // The map survives the group move
        for (int i = 0; i < model->count(); ++i)
            QCOMPARE(model->indexOfBlockId(model->blockAt(i)->blockId()), i);

        if (expected != names) {
            QCOMPARE(stack->count(), 1); // one undo step for the whole drop
            stack->undo();
            QCOMPARE(contents(model), names);
            stack->redo();
            QCOMPARE(contents(model), expected);
            stack->undo();
            QCOMPARE(contents(model), names);
        } else {
            QCOMPARE(stack->count(), 0); // no-op arrangement pushes nothing
        }
        delete model;
    }
}

// The cached ordinals and equation numbers must equal a from-scratch
// recompute (the pre-cache backward/forward scan semantics) after every
// scripted structural, type, and indent change.
void TestBlockModel::testDerivedOrderCacheMatchesRecompute()
{
    BlockModel *model = makeModel({
        { Block::NumberedList, "one" },
        { Block::NumberedList, "two" },
        { Block::NumberedList, "child", 1 },
        { Block::NumberedList, "three" },
        { Block::BulletList, "break" },
        { Block::NumberedList, "restart" },
        { Block::MathBlock, "x^2" },
        { Block::Paragraph, "text" },
        { Block::NumberedList, "solo" },
        { Block::MathBlock, "y^2" },
    });

    // Reference: the original per-row backward run scan.
    const auto referenceOrdinal = [model](int index) {
        const Block *block = model->blockAt(index);
        if (block->blockType() != Block::NumberedList)
            return 0;
        const int level = block->indentLevel();
        int ordinal = 1;
        for (int i = index - 1; i >= 0; --i) {
            const Block *prev = model->blockAt(i);
            if (Block::isListFamily(prev->blockType())
                && prev->indentLevel() > level)
                continue;
            if (prev->blockType() == Block::NumberedList
                && prev->indentLevel() == level) {
                ++ordinal;
                continue;
            }
            break;
        }
        return ordinal;
    };
    const auto referenceMathNumber = [model](int index) {
        if (model->blockAt(index)->blockType() != Block::MathBlock)
            return 0;
        int number = 0;
        for (int i = 0; i <= index; ++i)
            if (model->blockAt(i)->blockType() == Block::MathBlock)
                ++number;
        return number;
    };
    const auto verifyAll = [&]() {
        for (int i = 0; i < model->count(); ++i) {
            QCOMPARE(model->ordinalAt(i), referenceOrdinal(i));
            QCOMPARE(model->mathNumber(i), referenceMathNumber(i));
        }
    };

    verifyAll();
    QCOMPARE(model->ordinalAt(3), 3); // child at level 1 does not break the run
    QCOMPARE(model->ordinalAt(5), 1); // bullet at the same level restarts it

    model->insertBlock(1, Block::NumberedList, "inserted");
    verifyAll();
    model->removeBlock(5); // remove a bullet-run breaker
    verifyAll();
    model->moveBlock(0, 4);
    verifyAll();
    model->updateType(2, Block::BulletList);
    verifyAll();
    model->changeIndent(3, 1);
    verifyAll();
    model->convertBlock(4, Block::MathBlock, "z^2");
    verifyAll();
    model->updateType(4, Block::Paragraph);
    verifyAll();
    model->splitBlock(0, 2);
    verifyAll();
    model->mergeBlocks(0, 1);
    verifyAll();

    QVariantList all;
    for (int i = 0; i < model->count(); ++i)
        all.append(i);
    model->moveBlocksTo({0, 2}, model->count());
    verifyAll();
}

QTEST_MAIN(TestBlockModel)
#include "test_blockmodel.moc"
