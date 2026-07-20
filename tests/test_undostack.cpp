// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QSignalSpy>
#include "undostack.h"
#include "undocommand.h"
#include "blockmodel.h"

// Mock command for testing UndoCommand and UndoStack basics
class MockCommand : public UndoCommand
{
public:
    MockCommand(BlockModel *model, const QString &description = "Mock")
        : UndoCommand(model, description)
        , m_executeCount(0)
        , m_undoCount(0)
        , m_redoCount(0)
    {}

    void execute() override { m_executeCount++; }
    void undo() override { m_undoCount++; }
    void redo() override { m_redoCount++; }
    Type type() const override { return Type::TextChange; }

    int executeCount() const { return m_executeCount; }
    int undoCount() const { return m_undoCount; }
    int redoCount() const { return m_redoCount; }

private:
    int m_executeCount;
    int m_undoCount;
    int m_redoCount;
};

// Mock command that supports merging
class MergeableMockCommand : public UndoCommand
{
public:
    MergeableMockCommand(BlockModel *model, int value)
        : UndoCommand(model, "Mergeable")
        , m_value(value)
    {}

    void execute() override {}
    void undo() override {}
    Type type() const override { return Type::TextChange; }

    bool canMergeWith(const UndoCommand *other) const override {
        return other->type() == Type::TextChange;
    }

    void mergeWith(const UndoCommand *other) override {
        auto *cmd = static_cast<const MergeableMockCommand*>(other);
        m_value = cmd->m_value;
        m_timestamp = cmd->timestamp();
    }

    int value() const { return m_value; }

private:
    int m_value;
};

class TestUndoStack : public QObject
{
    Q_OBJECT

private slots:
    // UndoCommand tests
    void testUndoCommandConstruction();
    void testUndoCommandTimestamp();
    void testUndoCommandMergeId();

    // UndoStack basic tests
    void testUndoStackConstruction();
    void testPushCommand();
    void testUndo();
    void testRedo();
    void testCanUndoRedo();
    void testPushClearsRedo();
    void testUndoText();
    void testRedoText();

    // Clean state tests
    void testCleanState();
    void testSetClean();
    void testCleanAfterUndo();

    // Merge tests
    void testMergeCommands();
    void testMergeWindowExpired();
    void testMergeWithDifferentTypes();

    // Signal tests
    void testSignals();

    // Edge cases
    void testUndoOnEmpty();
    void testRedoOnEmpty();
    void testClear();
    void testCount();

    // Macro tests
    void testMacroBasic();
    void testMacroEmpty();

    // Phase 4: single-step guarantees through the model API
    void testConvertBlockIsOneUndoStep();
    void testSetCheckedThroughStack();
    void testChangeIndentThroughStack();
    void testRemoveBlockUndoRestoresFullState();

    // Performance Phase 7 (A6): bounded history and diff-based text edits
    void testLimitDropsOldest();
    void testLimitCleanIndexBehavior();
    void testDiffTextCommandRoundTrips();
    void testDiffTextCommandMergedTypingUndoes();
};

void TestUndoStack::testUndoCommandConstruction()
{
    BlockModel model;
    MockCommand cmd(&model, "Test Command");

    QCOMPARE(cmd.description(), QString("Test Command"));
    QCOMPARE(cmd.mergeId(), -1);
    QVERIFY(cmd.timestamp().isValid());
}

void TestUndoStack::testUndoCommandTimestamp()
{
    BlockModel model;
    QDateTime before = QDateTime::currentDateTime();
    MockCommand cmd(&model);
    QDateTime after = QDateTime::currentDateTime();

    QVERIFY(cmd.timestamp() >= before);
    QVERIFY(cmd.timestamp() <= after);
}

void TestUndoStack::testUndoCommandMergeId()
{
    BlockModel model;
    MockCommand cmd(&model);

    QCOMPARE(cmd.mergeId(), -1);
    cmd.setMergeId(42);
    QCOMPARE(cmd.mergeId(), 42);
}

void TestUndoStack::testUndoStackConstruction()
{
    UndoStack stack;

    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
    QCOMPARE(stack.count(), 0);
    QVERIFY(stack.isClean());
}

void TestUndoStack::testPushCommand()
{
    BlockModel model;
    UndoStack stack;

    auto cmd = std::make_unique<MockCommand>(&model);
    MockCommand *cmdPtr = cmd.get();

    stack.push(std::move(cmd));

    QCOMPARE(stack.count(), 1);
    QCOMPARE(cmdPtr->executeCount(), 1);
    QVERIFY(stack.canUndo());
}

void TestUndoStack::testUndo()
{
    BlockModel model;
    UndoStack stack;

    auto cmd = std::make_unique<MockCommand>(&model);
    MockCommand *cmdPtr = cmd.get();

    stack.push(std::move(cmd));
    stack.undo();

    QCOMPARE(cmdPtr->undoCount(), 1);
    QVERIFY(!stack.canUndo());
    QVERIFY(stack.canRedo());
}

void TestUndoStack::testRedo()
{
    BlockModel model;
    UndoStack stack;

    auto cmd = std::make_unique<MockCommand>(&model);
    MockCommand *cmdPtr = cmd.get();

    stack.push(std::move(cmd));
    stack.undo();
    stack.redo();

    QCOMPARE(cmdPtr->redoCount(), 1);
    QVERIFY(stack.canUndo());
    QVERIFY(!stack.canRedo());
}

void TestUndoStack::testCanUndoRedo()
{
    BlockModel model;
    UndoStack stack;

    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());

    stack.push(std::make_unique<MockCommand>(&model, "A"));
    QVERIFY(stack.canUndo());
    QVERIFY(!stack.canRedo());

    stack.push(std::make_unique<MockCommand>(&model, "B"));
    QVERIFY(stack.canUndo());
    QVERIFY(!stack.canRedo());

    stack.undo();
    QVERIFY(stack.canUndo());
    QVERIFY(stack.canRedo());

    stack.undo();
    QVERIFY(!stack.canUndo());
    QVERIFY(stack.canRedo());
}

void TestUndoStack::testPushClearsRedo()
{
    BlockModel model;
    UndoStack stack;

    stack.push(std::make_unique<MockCommand>(&model, "A"));
    stack.push(std::make_unique<MockCommand>(&model, "B"));
    stack.undo();

    QVERIFY(stack.canRedo());

    stack.push(std::make_unique<MockCommand>(&model, "C"));

    QVERIFY(!stack.canRedo());
    QCOMPARE(stack.count(), 2); // A and C, B was cleared
}

void TestUndoStack::testUndoText()
{
    BlockModel model;
    UndoStack stack;

    QCOMPARE(stack.undoText(), QString());

    stack.push(std::make_unique<MockCommand>(&model, "First"));
    QCOMPARE(stack.undoText(), QString("First"));

    stack.push(std::make_unique<MockCommand>(&model, "Second"));
    QCOMPARE(stack.undoText(), QString("Second"));

    stack.undo();
    QCOMPARE(stack.undoText(), QString("First"));
}

void TestUndoStack::testRedoText()
{
    BlockModel model;
    UndoStack stack;

    QCOMPARE(stack.redoText(), QString());

    stack.push(std::make_unique<MockCommand>(&model, "First"));
    stack.push(std::make_unique<MockCommand>(&model, "Second"));

    QCOMPARE(stack.redoText(), QString());

    stack.undo();
    QCOMPARE(stack.redoText(), QString("Second"));

    stack.undo();
    QCOMPARE(stack.redoText(), QString("First"));
}

void TestUndoStack::testCleanState()
{
    BlockModel model;
    UndoStack stack;

    QVERIFY(stack.isClean());

    stack.push(std::make_unique<MockCommand>(&model));
    QVERIFY(!stack.isClean());
}

void TestUndoStack::testSetClean()
{
    BlockModel model;
    UndoStack stack;

    stack.push(std::make_unique<MockCommand>(&model));
    QVERIFY(!stack.isClean());

    stack.setClean();
    QVERIFY(stack.isClean());

    stack.push(std::make_unique<MockCommand>(&model));
    QVERIFY(!stack.isClean());
}

void TestUndoStack::testCleanAfterUndo()
{
    BlockModel model;
    UndoStack stack;

    stack.push(std::make_unique<MockCommand>(&model));
    stack.setClean();
    QVERIFY(stack.isClean());

    stack.push(std::make_unique<MockCommand>(&model));
    QVERIFY(!stack.isClean());

    stack.undo();
    QVERIFY(stack.isClean());
}

void TestUndoStack::testMergeCommands()
{
    BlockModel model;
    UndoStack stack;
    stack.setMergeWindow(1000); // 1 second for testing

    stack.push(std::make_unique<MergeableMockCommand>(&model, 1));
    stack.push(std::make_unique<MergeableMockCommand>(&model, 2));
    stack.push(std::make_unique<MergeableMockCommand>(&model, 3));

    // Should merge into single command
    QCOMPARE(stack.count(), 1);

    // The merged command should have the last value
    const MergeableMockCommand *cmd = static_cast<const MergeableMockCommand*>(stack.command(0));
    QCOMPARE(cmd->value(), 3);
}

void TestUndoStack::testMergeWindowExpired()
{
    BlockModel model;
    UndoStack stack;
    stack.setMergeWindow(0); // No merge window

    stack.push(std::make_unique<MergeableMockCommand>(&model, 1));
    stack.push(std::make_unique<MergeableMockCommand>(&model, 2));

    // Should not merge because window is 0
    QCOMPARE(stack.count(), 2);
}

void TestUndoStack::testMergeWithDifferentTypes()
{
    BlockModel model;
    UndoStack stack;
    stack.setMergeWindow(1000);

    stack.push(std::make_unique<MockCommand>(&model, "Non-mergeable"));
    stack.push(std::make_unique<MergeableMockCommand>(&model, 1));

    // Should not merge different types (MockCommand doesn't support merging)
    QCOMPARE(stack.count(), 2);
}

void TestUndoStack::testSignals()
{
    BlockModel model;
    UndoStack stack;

    QSignalSpy canUndoSpy(&stack, &UndoStack::canUndoChanged);
    QSignalSpy canRedoSpy(&stack, &UndoStack::canRedoChanged);
    QSignalSpy cleanSpy(&stack, &UndoStack::cleanChanged);
    QSignalSpy countSpy(&stack, &UndoStack::countChanged);

    stack.push(std::make_unique<MockCommand>(&model));

    QVERIFY(canUndoSpy.count() >= 1);
    QVERIFY(cleanSpy.count() >= 1);
    QVERIFY(countSpy.count() >= 1);

    stack.undo();

    QVERIFY(canRedoSpy.count() >= 1);
}

void TestUndoStack::testUndoOnEmpty()
{
    UndoStack stack;

    // Should not crash
    stack.undo();
    QVERIFY(!stack.canUndo());
}

void TestUndoStack::testRedoOnEmpty()
{
    UndoStack stack;

    // Should not crash
    stack.redo();
    QVERIFY(!stack.canRedo());
}

void TestUndoStack::testClear()
{
    BlockModel model;
    UndoStack stack;

    stack.push(std::make_unique<MockCommand>(&model));
    stack.push(std::make_unique<MockCommand>(&model));
    stack.undo();

    QCOMPARE(stack.count(), 2);
    QVERIFY(stack.canUndo());
    QVERIFY(stack.canRedo());

    stack.clear();

    QCOMPARE(stack.count(), 0);
    QVERIFY(!stack.canUndo());
    QVERIFY(!stack.canRedo());
    QVERIFY(stack.isClean());
}

void TestUndoStack::testCount()
{
    BlockModel model;
    UndoStack stack;

    QCOMPARE(stack.count(), 0);

    stack.push(std::make_unique<MockCommand>(&model));
    QCOMPARE(stack.count(), 1);

    stack.push(std::make_unique<MockCommand>(&model));
    QCOMPARE(stack.count(), 2);

    stack.undo();
    QCOMPARE(stack.count(), 2); // Count includes undone commands

    stack.push(std::make_unique<MockCommand>(&model));
    QCOMPARE(stack.count(), 2); // Redo stack was cleared, new command replaces
}

void TestUndoStack::testMacroBasic()
{
    BlockModel model;
    UndoStack stack;

    stack.beginMacro("Macro Test");
    stack.push(std::make_unique<MockCommand>(&model, "A"));
    stack.push(std::make_unique<MockCommand>(&model, "B"));
    stack.push(std::make_unique<MockCommand>(&model, "C"));
    stack.endMacro();

    // All three commands should be grouped as one
    QCOMPARE(stack.count(), 1);
    QCOMPARE(stack.undoText(), QString("Macro Test"));

    // Single undo should undo all three
    stack.undo();
    QVERIFY(!stack.canUndo());
}

void TestUndoStack::testMacroEmpty()
{
    UndoStack stack;

    stack.beginMacro("Empty");
    stack.endMacro();

    // Empty macro should not add anything
    QCOMPARE(stack.count(), 0);
}

// ============================================================================
// Phase 4: single-step guarantees through the model API (phase4-plan.md)
// ============================================================================

void TestUndoStack::testConvertBlockIsOneUndoStep()
{
    UndoStack stack;
    BlockModel model;
    model.setUndoStack(&stack);
    model.insertBlock(0, Block::Paragraph, "- item");
    const int countBefore = stack.count();

    // The prefix-conversion shape: content and type change together
    model.convertBlock(0, Block::BulletList, "item");
    QCOMPARE(model.blockAt(0)->blockType(), Block::BulletList);
    QCOMPARE(model.getContent(0), QString("item"));
    QCOMPARE(stack.count(), countBefore + 1);

    // One undo restores the literal typed text and the type together
    stack.undo();
    QCOMPARE(model.blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(model.getContent(0), QString("- item"));

    stack.redo();
    QCOMPARE(model.blockAt(0)->blockType(), Block::BulletList);
    QCOMPARE(model.getContent(0), QString("item"));
}

void TestUndoStack::testSetCheckedThroughStack()
{
    UndoStack stack;
    BlockModel model;
    model.setUndoStack(&stack);
    model.insertBlock(0, Block::Todo, "task");
    const int countBefore = stack.count();

    model.setChecked(0, true);
    QCOMPARE(model.blockAt(0)->checked(), true);
    QCOMPARE(stack.count(), countBefore + 1);

    stack.undo();
    QCOMPARE(model.blockAt(0)->checked(), false);
    stack.redo();
    QCOMPARE(model.blockAt(0)->checked(), true);

    // A no-op toggle pushes nothing
    model.setChecked(0, true);
    QCOMPARE(stack.count(), countBefore + 1);
}

void TestUndoStack::testChangeIndentThroughStack()
{
    UndoStack stack;
    BlockModel model;
    model.setUndoStack(&stack);
    model.insertBlock(0, Block::BulletList, "parent");
    model.insertBlock(1, Block::BulletList, "child");
    const int countBefore = stack.count();

    model.changeIndent(1, 1);
    QCOMPARE(model.blockAt(1)->indentLevel(), 1);
    QCOMPARE(stack.count(), countBefore + 1);

    stack.undo();
    QCOMPARE(model.blockAt(1)->indentLevel(), 0);

    // A clamped-to-no-op indent pushes nothing
    model.changeIndent(1, -1);
    QCOMPARE(stack.count(), countBefore + 1);
}

void TestUndoStack::testRemoveBlockUndoRestoresFullState()
{
    UndoStack stack;
    BlockModel model;
    model.setUndoStack(&stack);
    model.insertBlock(0, Block::Todo, "task", 2);
    model.setChecked(0, true);

    model.removeBlock(0);
    QCOMPARE(model.count(), 0);

    stack.undo();
    Block *restored = model.blockAt(0);
    QCOMPARE(restored->blockType(), Block::Todo);
    QCOMPARE(restored->content(), QString("task"));
    QCOMPARE(restored->indentLevel(), 2);
    QCOMPARE(restored->checked(), true);
}

void TestUndoStack::testLimitDropsOldest()
{
    BlockModel model;
    model.insertBlock(0, Block::Paragraph, "");
    UndoStack stack;
    stack.setMergeWindow(0); // keep each edit its own command
    model.setUndoStack(&stack);
    stack.setLimit(5);
    QCOMPARE(stack.limit(), 5);

    // Eight edits: only the newest five survive.
    for (int i = 1; i <= 8; ++i)
        model.updateContent(0, QString::number(i));
    QCOMPARE(stack.count(), 5);
    QCOMPARE(stack.index(), 5);

    // Undoing everything lands on the oldest RETAINED state ("3"),
    // not the original empty content.
    while (stack.canUndo())
        stack.undo();
    QCOMPARE(model.getContent(0), QString("3"));

    // Redo still replays the surviving tail.
    while (stack.canRedo())
        stack.redo();
    QCOMPARE(model.getContent(0), QString("8"));

    // Lowering the limit trims immediately.
    stack.setLimit(2);
    QCOMPARE(stack.count(), 2);
    while (stack.canUndo())
        stack.undo();
    QCOMPARE(model.getContent(0), QString("6"));
}

void TestUndoStack::testLimitCleanIndexBehavior()
{
    BlockModel model;
    model.insertBlock(0, Block::Paragraph, "");
    UndoStack stack;
    stack.setMergeWindow(0);
    model.setUndoStack(&stack);
    stack.setLimit(3);

    // Clean state inside the surviving window shifts with the drop.
    model.updateContent(0, "1");
    model.updateContent(0, "2");
    stack.setClean();
    QVERIFY(stack.isClean());
    model.updateContent(0, "3");
    model.updateContent(0, "4"); // drops "1"; clean index shifts 2 -> 1
    QVERIFY(!stack.isClean());
    stack.undo();
    stack.undo();
    QVERIFY(stack.isClean());
    QCOMPARE(model.getContent(0), QString("2"));

    // A clean state older than the drop can never be reached again.
    stack.clear();
    stack.setClean(); // clean at index 0 (the current content, "2")
    model.updateContent(0, "a");
    model.updateContent(0, "b");
    model.updateContent(0, "c");
    model.updateContent(0, "d"); // drops "a"'s command; clean 0 -> -1
    while (stack.canUndo())
        stack.undo();
    QVERIFY(!stack.isClean());
}

void TestUndoStack::testDiffTextCommandRoundTrips()
{
    // Every edit shape round-trips through undo/redo to identical
    // content: insert, delete, replace, prepend, append, and a
    // whole-content swap.
    const QList<QPair<QString, QString>> cases = {
        { "hello world", "hello brave world" },   // insert
        { "hello brave world", "hello world" },   // delete
        { "hello world", "hello there" },         // replace tail
        { "world", "hello world" },               // prepend
        { "hello", "hello world" },               // append
        { "abc", "xyz" },                         // full swap
        { "", "typed" },                          // from empty
        { "gone", "" },                           // to empty
        { "aaaa", "aaa" },                        // ambiguous overlap
    };

    for (const auto &c : cases) {
        BlockModel model;
        model.insertBlock(0, Block::Paragraph, c.first);
        UndoStack stack;
        stack.setMergeWindow(0);
        model.setUndoStack(&stack);

        model.updateContent(0, c.second);
        QCOMPARE(model.getContent(0), c.second);
        stack.undo();
        QCOMPARE(model.getContent(0), c.first);
        stack.redo();
        QCOMPARE(model.getContent(0), c.second);
        stack.undo();
        QCOMPARE(model.getContent(0), c.first);
    }
}

void TestUndoStack::testDiffTextCommandMergedTypingUndoes()
{
    // A typing burst (with backspaces) merges into one command whose
    // undo restores the pre-burst content exactly.
    BlockModel model;
    model.insertBlock(0, Block::Paragraph, "note: ");
    UndoStack stack;
    model.setUndoStack(&stack);

    const QStringList burst = {
        "note: h", "note: he", "note: hel", "note: hell",
        "note: hello", "note: hell", "note: hello!",
    };
    QString previous = "note: ";
    for (const QString &next : burst) {
        model.updateContent(0, next);
        previous = next;
    }
    QCOMPARE(stack.count(), 1);
    QCOMPARE(model.getContent(0), QString("note: hello!"));
    stack.undo();
    QCOMPARE(model.getContent(0), QString("note: "));
    stack.redo();
    QCOMPARE(model.getContent(0), QString("note: hello!"));
}

QTEST_MAIN(TestUndoStack)
#include "test_undostack.moc"
