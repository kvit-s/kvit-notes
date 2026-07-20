// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include "block.h"

class TestBlock : public QObject
{
    Q_OBJECT

private slots:
    void testDefaultConstruction();
    void testParameterizedConstruction();
    void testCopyConstruction();
    void testBlockId();
    void testBlockType();
    void testContent();
    void testIndentLevel();
    void testSignals();
    // Phase 4: new block-type state
    void testEnumValuesPinned();
    void testIsListFamily();
    void testCheckedProperty();
    void testLanguageProperty();
    void testStateRoundTrip();
    void testDisplayTextAndCountsCache();
    void testCacheInvalidatesOnContentAndType();
};

void TestBlock::testDefaultConstruction()
{
    Block block;

    QVERIFY(!block.blockId().isEmpty());
    QCOMPARE(block.blockType(), Block::Paragraph);
    QCOMPARE(block.content(), QString(""));
    QCOMPARE(block.indentLevel(), 0);
}

void TestBlock::testParameterizedConstruction()
{
    Block block(Block::Heading1, "Test Content");

    QVERIFY(!block.blockId().isEmpty());
    QCOMPARE(block.blockType(), Block::Heading1);
    QCOMPARE(block.content(), QString("Test Content"));
    QCOMPARE(block.indentLevel(), 0);
}

void TestBlock::testCopyConstruction()
{
    Block original(Block::Heading2, "Original Content");
    original.setIndentLevel(2);

    original.setChecked(true);
    original.setLanguage("python");

    Block copy(original);

    // Copy should have a different ID
    QVERIFY(copy.blockId() != original.blockId());

    // But same content and properties
    QCOMPARE(copy.blockType(), Block::Heading2);
    QCOMPARE(copy.content(), QString("Original Content"));
    QCOMPARE(copy.indentLevel(), 2);
    QCOMPARE(copy.checked(), true);
    QCOMPARE(copy.language(), QString("python"));
}

void TestBlock::testBlockId()
{
    Block block1;
    Block block2;

    // Each block should have a unique ID
    QVERIFY(block1.blockId() != block2.blockId());

    // ID should be a valid UUID format (36 characters without braces)
    QCOMPARE(block1.blockId().length(), 36);
}

void TestBlock::testBlockType()
{
    Block block;

    // Test all block types
    block.setBlockType(Block::Paragraph);
    QCOMPARE(block.blockType(), Block::Paragraph);

    block.setBlockType(Block::Heading1);
    QCOMPARE(block.blockType(), Block::Heading1);

    block.setBlockType(Block::Heading2);
    QCOMPARE(block.blockType(), Block::Heading2);

    block.setBlockType(Block::Heading3);
    QCOMPARE(block.blockType(), Block::Heading3);
}

void TestBlock::testContent()
{
    Block block;

    block.setContent("Hello World");
    QCOMPARE(block.content(), QString("Hello World"));

    block.setContent("New Content");
    QCOMPARE(block.content(), QString("New Content"));

    // Test empty content
    block.setContent("");
    QCOMPARE(block.content(), QString(""));

    // Test content with special characters
    block.setContent("**Bold** and *italic*");
    QCOMPARE(block.content(), QString("**Bold** and *italic*"));
}

void TestBlock::testIndentLevel()
{
    Block block;

    block.setIndentLevel(1);
    QCOMPARE(block.indentLevel(), 1);

    block.setIndentLevel(5);
    QCOMPARE(block.indentLevel(), 5);

    // Negative values should be clamped to 0
    block.setIndentLevel(-1);
    QCOMPARE(block.indentLevel(), 0);
}

void TestBlock::testSignals()
{
    Block block;

    // Test blockTypeChanged signal
    QSignalSpy typeSpy(&block, &Block::blockTypeChanged);
    block.setBlockType(Block::Heading1);
    QCOMPARE(typeSpy.count(), 1);

    // Setting same value should not emit signal
    block.setBlockType(Block::Heading1);
    QCOMPARE(typeSpy.count(), 1);

    // Test contentChanged signal
    QSignalSpy contentSpy(&block, &Block::contentChanged);
    block.setContent("New");
    QCOMPARE(contentSpy.count(), 1);

    // Setting same value should not emit signal
    block.setContent("New");
    QCOMPARE(contentSpy.count(), 1);

    // Test indentLevelChanged signal
    QSignalSpy indentSpy(&block, &Block::indentLevelChanged);
    block.setIndentLevel(2);
    QCOMPARE(indentSpy.count(), 1);

    // Setting same value should not emit signal
    block.setIndentLevel(2);
    QCOMPARE(indentSpy.count(), 1);
}

// Enum values are persisted through the model's type role and used by
// QML as integers; they must never be renumbered.
void TestBlock::testEnumValuesPinned()
{
    QCOMPARE(static_cast<int>(Block::Paragraph), 0);
    QCOMPARE(static_cast<int>(Block::Heading1), 1);
    QCOMPARE(static_cast<int>(Block::Heading2), 2);
    QCOMPARE(static_cast<int>(Block::Heading3), 3);
    QCOMPARE(static_cast<int>(Block::BulletList), 4);
    QCOMPARE(static_cast<int>(Block::NumberedList), 5);
    QCOMPARE(static_cast<int>(Block::Todo), 6);
    QCOMPARE(static_cast<int>(Block::Quote), 7);
    QCOMPARE(static_cast<int>(Block::CodeBlock), 8);
    QCOMPARE(static_cast<int>(Block::Divider), 9);
    // Appended in Phase 5; heading levels 1-3 keep their historic values
    QCOMPARE(static_cast<int>(Block::Heading4), 10);
}

void TestBlock::testIsListFamily()
{
    QVERIFY(Block::isListFamily(Block::BulletList));
    QVERIFY(Block::isListFamily(Block::NumberedList));
    QVERIFY(Block::isListFamily(Block::Todo));

    QVERIFY(!Block::isListFamily(Block::Paragraph));
    QVERIFY(!Block::isListFamily(Block::Heading1));
    QVERIFY(!Block::isListFamily(Block::Quote));
    QVERIFY(!Block::isListFamily(Block::CodeBlock));
    QVERIFY(!Block::isListFamily(Block::Divider));
}

void TestBlock::testCheckedProperty()
{
    Block block(Block::Todo, "task");
    QCOMPARE(block.checked(), false);

    QSignalSpy spy(&block, &Block::checkedChanged);
    block.setChecked(true);
    QCOMPARE(block.checked(), true);
    QCOMPARE(spy.count(), 1);

    // Setting same value should not emit signal
    block.setChecked(true);
    QCOMPARE(spy.count(), 1);

    block.setChecked(false);
    QCOMPARE(block.checked(), false);
    QCOMPARE(spy.count(), 2);
}

void TestBlock::testLanguageProperty()
{
    Block block(Block::CodeBlock, "print(1)");
    QCOMPARE(block.language(), QString());

    QSignalSpy spy(&block, &Block::languageChanged);
    block.setLanguage("python");
    QCOMPARE(block.language(), QString("python"));
    QCOMPARE(spy.count(), 1);

    // Setting same value should not emit signal
    block.setLanguage("python");
    QCOMPARE(spy.count(), 1);
}

void TestBlock::testStateRoundTrip()
{
    Block original(Block::Todo, "task text");
    original.setIndentLevel(3);
    original.setChecked(true);
    original.setLanguage("js");

    Block::State state = original.state();
    QCOMPARE(state.type, Block::Todo);
    QCOMPARE(state.content, QString("task text"));
    QCOMPARE(state.indentLevel, 3);
    QCOMPARE(state.checked, true);
    QCOMPARE(state.language, QString("js"));

    Block restored;
    restored.setState(state);
    QCOMPARE(restored.blockType(), Block::Todo);
    QCOMPARE(restored.content(), QString("task text"));
    QCOMPARE(restored.indentLevel(), 3);
    QCOMPARE(restored.checked(), true);
    QCOMPARE(restored.language(), QString("js"));
}

void TestBlock::testDisplayTextAndCountsCache()
{
    Block block(Block::Paragraph, "**bold** and *italic*");
    const QString display = QStringLiteral("bold and italic");

    QCOMPARE(block.displayText(), display);
    QCOMPARE(block.wordCount(), 3);
    QCOMPARE(block.charCount(true), int(display.length()));
    QCOMPARE(block.charCount(false), int(QStringLiteral("boldanditalic").length()));

    // Re-reading the derived values must stay stable once cached.
    QCOMPARE(block.displayText(), display);
    QCOMPARE(block.wordCount(), 3);
}

void TestBlock::testCacheInvalidatesOnContentAndType()
{
    Block block(Block::Paragraph, "**bold**");
    QCOMPARE(block.displayText(), QStringLiteral("bold"));

    block.setContent("*italic* now");
    QCOMPARE(block.displayText(), QStringLiteral("italic now"));
    QCOMPARE(block.wordCount(), 2);

    block.setBlockType(Block::CodeBlock);
    block.setContent("**literal** text");
    QCOMPARE(block.displayText(), QStringLiteral("**literal** text"));
    QCOMPARE(block.wordCount(), 2);

    block.setBlockType(Block::Paragraph);
    QCOMPARE(block.displayText(), QStringLiteral("literal text"));
}

QTEST_MAIN(TestBlock)
#include "test_block.moc"
