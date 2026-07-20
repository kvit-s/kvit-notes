// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QSignalSpy>
#include "blockmodel.h"
#include "undostack.h"
#include "textchangecommand.h"
#include "insertblockcommand.h"
#include "removeblockcommand.h"
#include "moveblockcommand.h"
#include "changetypecommand.h"
#include "splitblockcommand.h"
#include "mergeblockscommand.h"
#include "setcheckedcommand.h"
#include "changeindentcommand.h"
#include "convertblockcommand.h"
#include "setblockattributescommand.h"

class TestCommands : public QObject
{
    Q_OBJECT

private slots:
    // TextChangeCommand tests
    void testTextChangeCommand();
    void testTextChangeUndo();
    void testTextChangeRedo();
    void testTextChangeMerge();
    void testTextChangeNoMergeLargeChange();
    void testTextChangeNoMergeDifferentBlock();

    // InsertBlockCommand tests
    void testInsertBlockCommand();
    void testInsertBlockUndo();
    void testInsertBlockRedo();

    // RemoveBlockCommand tests
    void testRemoveBlockCommand();
    void testRemoveBlockUndo();
    void testRemoveBlockPreservesType();

    // MoveBlockCommand tests
    void testMoveBlockCommand();
    void testMoveBlockUndo();

    // ChangeTypeCommand tests
    void testChangeTypeCommand();
    void testChangeTypeUndo();

    // SplitBlockCommand tests
    void testSplitBlockCommand();
    void testSplitBlockUndo();
    void testSplitBlockAtStart();
    void testSplitBlockAtEnd();

    // MergeBlocksCommand tests
    void testMergeBlocksCommand();
    void testMergeBlocksUndo();

    // Integration tests
    void testUndoStackWithTextChange();
    void testUndoStackWithInsertBlock();
    void testTypingMerge();

    // Full-state capture and the new commands
    void testRemoveBlockRestoresFullState();
    void testInsertBlockWithState();
    void testSplitBlockInheritsState();
    void testMergeBlocksRestoresFullState();
    void testSetCheckedCommand();
    void testChangeIndentCommand();
    void testConvertBlockCommand();
    void testSetBlockAttributesCommand();
    void testSetBlockAttributesOneUndoStep();

    // Index validation must not depend on whether a stack is attached
    void testSplitBlockInvalidIndexWithStack();
    void testMergeBlocksInvalidIndexWithStack();
    void testRemoveBlockInvalidIndexWithStack();
    void testInsertBlockClampedIndexUndoRedo();
    void testMergeBlocksReverseOrderRejected();

private:
    void clearModel(BlockModel &model);
};

void TestCommands::clearModel(BlockModel &model)
{
    while (model.rowCount() > 0) {
        model.removeBlock(0);
    }
}

// TextChangeCommand tests

void TestCommands::testTextChangeCommand()
{
    BlockModel model;
    clearModel(model);
    // Content is set to "Modified" to simulate user already having made the change
    model.insertBlock(0, Block::Paragraph, "Modified");

    TextChangeCommand cmd(&model, 0, "Original", "Modified");
    cmd.execute();

    // First execute should not change content (it's already applied)
    // The command just records the change for undo
    QCOMPARE(model.getContent(0), QString("Modified"));
}

void TestCommands::testTextChangeUndo()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Modified");

    TextChangeCommand cmd(&model, 0, "Original", "Modified");
    cmd.execute(); // Mark as executed

    cmd.undo();
    QCOMPARE(model.getContent(0), QString("Original"));
}

void TestCommands::testTextChangeRedo()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Original");

    TextChangeCommand cmd(&model, 0, "Original", "Modified");
    cmd.execute();
    cmd.undo();

    QCOMPARE(model.getContent(0), QString("Original"));

    cmd.redo();
    QCOMPARE(model.getContent(0), QString("Modified"));
}

void TestCommands::testTextChangeMerge()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Hello");

    TextChangeCommand cmd1(&model, 0, "", "H");
    TextChangeCommand cmd2(&model, 0, "H", "He");
    TextChangeCommand cmd3(&model, 0, "He", "Hello");

    // cmd1 should be able to merge with cmd2 (same block, small change)
    QVERIFY(cmd1.canMergeWith(&cmd2));

    cmd1.mergeWith(&cmd2);
    QCOMPARE(cmd1.insertedText(), QString("He"));
    QCOMPARE(cmd1.newLength(), 2);

    cmd1.mergeWith(&cmd3);
    QCOMPARE(cmd1.insertedText(), QString("Hello"));
    QCOMPARE(cmd1.newLength(), 5);
    QCOMPARE(cmd1.removedText(), QString());
    QCOMPARE(cmd1.offset(), 0);

    // After merge, undo should restore to original empty state
    cmd1.undo();
    QCOMPARE(model.getContent(0), QString(""));
}

void TestCommands::testTextChangeNoMergeLargeChange()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "");

    TextChangeCommand cmd1(&model, 0, "", "Hello");

    // A large paste (>20 chars) should not merge
    QString largeText(30, 'x');
    TextChangeCommand cmd2(&model, 0, "Hello", "Hello" + largeText);

    QVERIFY(!cmd1.canMergeWith(&cmd2));
}

void TestCommands::testTextChangeNoMergeDifferentBlock()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Block 0");
    model.insertBlock(1, Block::Paragraph, "Block 1");

    TextChangeCommand cmd1(&model, 0, "Block 0", "Block 0 modified");
    TextChangeCommand cmd2(&model, 1, "Block 1", "Block 1 modified");

    QVERIFY(!cmd1.canMergeWith(&cmd2));
}

// InsertBlockCommand tests

void TestCommands::testInsertBlockCommand()
{
    BlockModel model;
    clearModel(model);

    InsertBlockCommand cmd(&model, 0, Block::Paragraph, "New block");
    cmd.execute();

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.getContent(0), QString("New block"));
}

void TestCommands::testInsertBlockUndo()
{
    BlockModel model;
    clearModel(model);

    InsertBlockCommand cmd(&model, 0, Block::Heading1, "Title");
    cmd.execute();

    QCOMPARE(model.count(), 1);

    cmd.undo();

    QCOMPARE(model.count(), 0);
}

void TestCommands::testInsertBlockRedo()
{
    BlockModel model;
    clearModel(model);

    InsertBlockCommand cmd(&model, 0, Block::Paragraph, "Test");
    cmd.execute();
    cmd.undo();

    QCOMPARE(model.count(), 0);

    cmd.redo();

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.getContent(0), QString("Test"));
}

// RemoveBlockCommand tests

void TestCommands::testRemoveBlockCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "To be removed");

    RemoveBlockCommand cmd(&model, 0);
    cmd.execute();

    QCOMPARE(model.count(), 0);
}

void TestCommands::testRemoveBlockUndo()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Heading1, "Title");

    RemoveBlockCommand cmd(&model, 0);
    cmd.execute();

    QCOMPARE(model.count(), 0);

    cmd.undo();

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.getContent(0), QString("Title"));
}

void TestCommands::testRemoveBlockPreservesType()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Heading2, "Section");

    RemoveBlockCommand cmd(&model, 0);
    cmd.execute();
    cmd.undo();

    QCOMPARE(model.blockAt(0)->blockType(), Block::Heading2);
}

// MoveBlockCommand tests

void TestCommands::testMoveBlockCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "A");
    model.insertBlock(1, Block::Paragraph, "B");
    model.insertBlock(2, Block::Paragraph, "C");

    MoveBlockCommand cmd(&model, 0, 2);
    cmd.execute();

    QCOMPARE(model.getContent(0), QString("B"));
    QCOMPARE(model.getContent(1), QString("C"));
    QCOMPARE(model.getContent(2), QString("A"));
}

void TestCommands::testMoveBlockUndo()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "A");
    model.insertBlock(1, Block::Paragraph, "B");
    model.insertBlock(2, Block::Paragraph, "C");

    MoveBlockCommand cmd(&model, 0, 2);
    cmd.execute();
    cmd.undo();

    QCOMPARE(model.getContent(0), QString("A"));
    QCOMPARE(model.getContent(1), QString("B"));
    QCOMPARE(model.getContent(2), QString("C"));
}

// ChangeTypeCommand tests

void TestCommands::testChangeTypeCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Text");

    ChangeTypeCommand cmd(&model, 0, Block::Paragraph, Block::Heading1);
    cmd.execute();

    QCOMPARE(model.blockAt(0)->blockType(), Block::Heading1);
}

void TestCommands::testChangeTypeUndo()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Text");

    ChangeTypeCommand cmd(&model, 0, Block::Paragraph, Block::Heading1);
    cmd.execute();
    cmd.undo();

    QCOMPARE(model.blockAt(0)->blockType(), Block::Paragraph);
}

// SplitBlockCommand tests

void TestCommands::testSplitBlockCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Hello World");

    SplitBlockCommand cmd(&model, 0, 5);
    cmd.execute();

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.getContent(0), QString("Hello"));
    QCOMPARE(model.getContent(1), QString(" World"));
}

void TestCommands::testSplitBlockUndo()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Hello World");

    SplitBlockCommand cmd(&model, 0, 5);
    cmd.execute();
    cmd.undo();

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.getContent(0), QString("Hello World"));
}

void TestCommands::testSplitBlockAtStart()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Text");

    SplitBlockCommand cmd(&model, 0, 0);
    cmd.execute();

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.getContent(0), QString(""));
    QCOMPARE(model.getContent(1), QString("Text"));
}

void TestCommands::testSplitBlockAtEnd()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Text");

    SplitBlockCommand cmd(&model, 0, 4);
    cmd.execute();

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.getContent(0), QString("Text"));
    QCOMPARE(model.getContent(1), QString(""));
}

// MergeBlocksCommand tests

void TestCommands::testMergeBlocksCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Hello");
    model.insertBlock(1, Block::Paragraph, " World");

    MergeBlocksCommand cmd(&model, 0, 1);
    cmd.execute();

    QCOMPARE(model.count(), 1);
    QCOMPARE(model.getContent(0), QString("Hello World"));
}

void TestCommands::testMergeBlocksUndo()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Hello");
    model.insertBlock(1, Block::Paragraph, " World");

    MergeBlocksCommand cmd(&model, 0, 1);
    cmd.execute();
    cmd.undo();

    QCOMPARE(model.count(), 2);
    QCOMPARE(model.getContent(0), QString("Hello"));
    QCOMPARE(model.getContent(1), QString(" World"));
}

// Integration tests

void TestCommands::testUndoStackWithTextChange()
{
    BlockModel model;
    UndoStack stack;
    model.setUndoStack(&stack);

    clearModel(model);
    model.insertBlockInternal(0, Block::Paragraph, "Original");

    // Simulate user typing
    model.updateContent(0, "Modified");

    QVERIFY(stack.canUndo());
    QCOMPARE(model.getContent(0), QString("Modified"));

    stack.undo();

    QCOMPARE(model.getContent(0), QString("Original"));
}

void TestCommands::testUndoStackWithInsertBlock()
{
    BlockModel model;
    UndoStack stack;
    model.setUndoStack(&stack);

    clearModel(model);

    model.insertBlock(0, Block::Paragraph, "New block");

    QCOMPARE(model.count(), 1);
    QVERIFY(stack.canUndo());

    stack.undo();

    QCOMPARE(model.count(), 0);
}

void TestCommands::testTypingMerge()
{
    BlockModel model;
    UndoStack stack;
    stack.setMergeWindow(1000); // 1 second for testing
    model.setUndoStack(&stack);

    clearModel(model);
    model.insertBlockInternal(0, Block::Paragraph, "");

    // Simulate typing "Hello" character by character
    model.updateContent(0, "H");
    model.updateContent(0, "He");
    model.updateContent(0, "Hel");
    model.updateContent(0, "Hell");
    model.updateContent(0, "Hello");

    // Should be merged into one command
    QCOMPARE(stack.count(), 1);

    stack.undo();
    QCOMPARE(model.getContent(0), QString(""));
}

// ============================================================================
// Full-state capture and the new commands
// ============================================================================

static Block::State todoState(const QString &content)
{
    Block::State s;
    s.type = Block::Todo;
    s.content = content;
    s.indentLevel = 2;
    s.checked = true;
    s.language = "meta";
    return s;
}

void TestCommands::testRemoveBlockRestoresFullState()
{
    BlockModel model;
    clearModel(model);
    model.insertBlockInternal(0, todoState("task"));

    RemoveBlockCommand cmd(&model, 0);
    cmd.execute();
    QCOMPARE(model.rowCount(), 0);

    cmd.undo();
    Block *block = model.blockAt(0);
    QCOMPARE(block->blockType(), Block::Todo);
    QCOMPARE(block->content(), QString("task"));
    QCOMPARE(block->indentLevel(), 2);
    QCOMPARE(block->checked(), true);
    QCOMPARE(block->language(), QString("meta"));
}

void TestCommands::testInsertBlockWithState()
{
    BlockModel model;
    clearModel(model);

    InsertBlockCommand cmd(&model, 0, todoState("task"));
    cmd.execute();

    Block *block = model.blockAt(0);
    QCOMPARE(block->blockType(), Block::Todo);
    QCOMPARE(block->indentLevel(), 2);
    QCOMPARE(block->checked(), true);
    QCOMPARE(block->language(), QString("meta"));

    cmd.undo();
    QCOMPARE(model.rowCount(), 0);

    // Redo restores the same state
    cmd.execute();
    QCOMPARE(model.blockAt(0)->checked(), true);
}

void TestCommands::testSplitBlockInheritsState()
{
    BlockModel model;
    clearModel(model);
    model.insertBlockInternal(0, todoState("firstsecond"));

    SplitBlockCommand cmd(&model, 0, 5);
    cmd.execute();

    QCOMPARE(model.rowCount(), 2);
    Block *second = model.blockAt(1);
    QCOMPARE(second->blockType(), Block::Todo);
    QCOMPARE(second->indentLevel(), 2);
    QCOMPARE(second->language(), QString("meta"));
    // The new half starts unchecked; the first half keeps its state
    QCOMPARE(second->checked(), false);
    QCOMPARE(model.blockAt(0)->checked(), true);

    cmd.undo();
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.getContent(0), QString("firstsecond"));
    QCOMPARE(model.blockAt(0)->checked(), true);
}

void TestCommands::testMergeBlocksRestoresFullState()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "keep");
    model.insertBlockInternal(1, todoState("task"));

    MergeBlocksCommand cmd(&model, 0, 1);
    cmd.execute();
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.getContent(0), QString("keeptask"));

    cmd.undo();
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.getContent(0), QString("keep"));
    Block *restored = model.blockAt(1);
    QCOMPARE(restored->blockType(), Block::Todo);
    QCOMPARE(restored->content(), QString("task"));
    QCOMPARE(restored->indentLevel(), 2);
    QCOMPARE(restored->checked(), true);
    QCOMPARE(restored->language(), QString("meta"));
}

void TestCommands::testSetCheckedCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Todo, "task");

    SetCheckedCommand cmd(&model, 0, true);
    cmd.execute();
    QCOMPARE(model.blockAt(0)->checked(), true);

    cmd.undo();
    QCOMPARE(model.blockAt(0)->checked(), false);

    cmd.redo();
    QCOMPARE(model.blockAt(0)->checked(), true);
}

void TestCommands::testChangeIndentCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::BulletList, "item");

    ChangeIndentCommand cmd(&model, 0, 0, 1);
    cmd.execute();
    QCOMPARE(model.blockAt(0)->indentLevel(), 1);

    cmd.undo();
    QCOMPARE(model.blockAt(0)->indentLevel(), 0);
}

void TestCommands::testConvertBlockCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "- item");
    Block::State oldState = model.blockAt(0)->state();

    Block::State newState;
    newState.type = Block::BulletList;
    newState.content = "item";

    ConvertBlockCommand cmd(&model, 0, oldState, newState);
    cmd.execute();
    QCOMPARE(model.blockAt(0)->blockType(), Block::BulletList);
    QCOMPARE(model.blockAt(0)->content(), QString("item"));

    // One undo restores content and type together
    cmd.undo();
    QCOMPARE(model.blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(model.blockAt(0)->content(), QString("- item"));
}

void TestCommands::testSetBlockAttributesCommand()
{
    BlockModel model;
    clearModel(model);
    model.insertBlock(0, Block::Paragraph, "Text");
    model.blockAt(0)->setAttributes("align=left");

    SetBlockAttributesCommand cmd(&model, 0, "align=center");
    cmd.execute();
    QCOMPARE(model.blockAt(0)->attributes(), QString("align=center"));

    // Undo restores the exact prior payload (captured on first execute).
    cmd.undo();
    QCOMPARE(model.blockAt(0)->attributes(), QString("align=left"));

    cmd.redo();
    QCOMPARE(model.blockAt(0)->attributes(), QString("align=center"));
}

void TestCommands::testSetBlockAttributesOneUndoStep()
{
    // Through the model's undo stack: a styling change is a single undo step.
    BlockModel model;
    UndoStack stack;
    model.setUndoStack(&stack);
    clearModel(model);
    model.insertBlock(0, Block::Image, "![a](x.png)");

    model.setBlockAttributes(0, "align=center rounded");
    QCOMPARE(model.blockAt(0)->attributes(), QString("align=center rounded"));

    stack.undo();
    QVERIFY(model.blockAt(0)->attributes().isEmpty());

    stack.redo();
    QCOMPARE(model.blockAt(0)->attributes(), QString("align=center rounded"));

    // Setting the same value again is a no-op — no extra undo step.
    const int before = stack.count();
    model.setBlockAttributes(0, "align=center rounded");
    QCOMPARE(stack.count(), before);
}

// Index validation must not depend on whether a stack is attached.
// BlockModel::splitBlock/mergeBlocks/removeBlock/insertBlock are invokable
// from QML, where a delegate can hand over a stale or -1 index; the
// stack-attached path must reject exactly what the direct path rejects.

void TestCommands::testSplitBlockInvalidIndexWithStack()
{
    BlockModel model;
    UndoStack stack;
    model.setUndoStack(&stack);

    clearModel(model);
    model.insertBlockInternal(0, Block::Paragraph, "Hello World");

    const int countBefore = model.count();

    model.splitBlock(-1, 0);
    model.splitBlock(100, 0);
    QCOMPARE(model.count(), countBefore);
    QVERIFY(!stack.canUndo());

    // Invalid position within a valid block is equally a no-op.
    model.splitBlock(0, -1);
    model.splitBlock(0, 1000);
    QCOMPARE(model.count(), countBefore);
    QCOMPARE(model.getContent(0), QString("Hello World"));
    QVERIFY(!stack.canUndo());
}

void TestCommands::testMergeBlocksInvalidIndexWithStack()
{
    BlockModel model;
    UndoStack stack;
    model.setUndoStack(&stack);

    clearModel(model);
    model.insertBlockInternal(0, Block::Paragraph, "First");
    model.insertBlockInternal(1, Block::Paragraph, "Second");

    const int countBefore = model.count();

    model.mergeBlocks(-1, 0);
    model.mergeBlocks(0, -1);
    model.mergeBlocks(100, 0);
    model.mergeBlocks(0, 100);
    model.mergeBlocks(0, 0);

    QCOMPARE(model.count(), countBefore);
    QCOMPARE(model.getContent(0), QString("First"));
    QCOMPARE(model.getContent(1), QString("Second"));
    QVERIFY(!stack.canUndo());
}

void TestCommands::testRemoveBlockInvalidIndexWithStack()
{
    BlockModel model;
    UndoStack stack;
    model.setUndoStack(&stack);

    clearModel(model);
    model.insertBlockInternal(0, Block::Heading1, "Title");

    model.removeBlock(-1);
    model.removeBlock(100);

    QCOMPARE(model.count(), 1);
    QVERIFY(!stack.canUndo());

    // Undoing an out-of-range removal must not conjure a default block.
    stack.undo();
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.getContent(0), QString("Title"));
}

void TestCommands::testInsertBlockClampedIndexUndoRedo()
{
    BlockModel model;
    UndoStack stack;
    model.setUndoStack(&stack);

    clearModel(model);
    model.insertBlockInternal(0, Block::Paragraph, "Existing");

    // Out-of-range insertion positions clamp; the command must remember the
    // clamped index so undo removes the block it actually inserted.
    model.insertBlock(-5, Block::Paragraph, "Clamped");
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.getContent(0), QString("Clamped"));

    stack.undo();
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.getContent(0), QString("Existing"));

    stack.redo();
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.getContent(0), QString("Clamped"));

    model.insertBlock(1000, Block::Paragraph, "AtEnd");
    QCOMPARE(model.count(), 3);
    QCOMPARE(model.getContent(2), QString("AtEnd"));

    stack.undo();
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.getContent(0), QString("Clamped"));
    QCOMPARE(model.getContent(1), QString("Existing"));
}

void TestCommands::testMergeBlocksReverseOrderRejected()
{
    BlockModel model;
    UndoStack stack;
    model.setUndoStack(&stack);

    clearModel(model);
    model.insertBlockInternal(0, Block::Paragraph, "First");
    model.insertBlockInternal(1, Block::Paragraph, "Second");

    // Merging backwards has no caller and its undo cannot restore the
    // original order, so it is rejected outright on both paths.
    model.mergeBlocks(1, 0);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.getContent(0), QString("First"));
    QCOMPARE(model.getContent(1), QString("Second"));
    QVERIFY(!stack.canUndo());

    BlockModel direct;
    while (direct.rowCount() > 0)
        direct.removeBlock(0);
    direct.insertBlockInternal(0, Block::Paragraph, "First");
    direct.insertBlockInternal(1, Block::Paragraph, "Second");
    direct.mergeBlocks(1, 0);
    QCOMPARE(direct.count(), 2);
    QCOMPARE(direct.getContent(0), QString("First"));
    QCOMPARE(direct.getContent(1), QString("Second"));
}

QTEST_MAIN(TestCommands)
#include "test_commands.moc"
