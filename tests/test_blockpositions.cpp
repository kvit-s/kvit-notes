// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "blockpositions.h"
#include "blockmodel.h"
#include "block.h"
#include "undostack.h"

// Translating between a block's two coordinate systems: the markdown it
// holds, `This is **bold** text`, and the display text a reader sees once
// the inline markers are hidden, `This is bold text`.
//
// The mapping itself belongs to InlineMarkdown and is tested there. What
// is tested here is the block-addressed layer over it: resolving an index
// through the model, the identity mapping a verbatim block gets, and the
// answers given for the inputs that fall outside a block — a position past
// the end of the text, a negative one, an index the document does not
// hold, and no document at all. Those answers are the contract a caller
// storing a range against a passage of a note depends on, so they are
// written down here rather than left to whatever the callers happen to do.
class TestBlockPositions : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Display -> markdown
    void testMarkdownPositionSkipsHiddenMarkers();
    void testMarkdownPositionInVerbatimBlockIsIdentity();
    void testMarkdownPositionPastEndClampsToContent();
    void testMarkdownPositionNegativeMapsToStartOfText();
    void testMarkdownPositionOutOfRangeBlockIsZero();
    void testMarkdownPositionNullModelIsZero();

    // Markdown -> display
    void testDisplayPositionSkipsHiddenMarkers();
    void testDisplayPositionInsideMarkerClampsToContentEdge();
    void testDisplayPositionInVerbatimBlockIsIdentity();
    void testDisplayPositionPastEndClampsToDisplayText();
    void testDisplayPositionNegativeIsZero();
    void testDisplayPositionOutOfRangeBlockIsZero();
    void testDisplayPositionNullModelIsZero();

    // Both directions
    void testEmptyBlockAnswersZero();
    void testRoundTripOverEveryDisplayPosition();

private:
    // Blocks: 0 formatted paragraph, 1 code block, 2 divider (no text),
    // 3 paragraph whose first character is a marker.
    BlockModel *m_model = nullptr;
    UndoStack *m_stack = nullptr;
};

// Block 0: markdown "This is **bold** text" (21 characters),
//          display  "This is bold text"     (17 characters).
static const QString kParagraphMarkdown = QStringLiteral("This is **bold** text");
static const int kParagraphMarkdownLength = 21;
static const int kParagraphDisplayLength = 17;
// Block 1: a code block, where the display text IS the content.
static const QString kCodeContent = QStringLiteral("let x = **not markdown**\nf()");

void TestBlockPositions::init()
{
    m_stack = new UndoStack(this);
    m_model = new BlockModel(this);
    m_model->setUndoStack(m_stack);
    m_model->insertBlock(0, Block::Paragraph, kParagraphMarkdown);
    m_model->insertBlock(1, Block::CodeBlock, kCodeContent);
    m_model->insertBlock(2, Block::Divider, QString());
    m_model->insertBlock(3, Block::Paragraph, QStringLiteral("**bold** tail"));
    m_stack->clear();
}

void TestBlockPositions::cleanup()
{
    delete m_model;
    m_model = nullptr;
    delete m_stack;
    m_stack = nullptr;
}

// ---- display -> markdown ----

void TestBlockPositions::testMarkdownPositionSkipsHiddenMarkers()
{
    // Before the span the two coordinate systems agree.
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, 0), 0);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, 5), 5);
    // The 'b' of "bold" is display 8 and markdown 10: the two opening
    // asterisks sit between them.
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, 8), 10);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, 11), 13);
    // Just after "bold": past the closing asterisks, four markers back.
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, 12), 16);
    // The end of the display text is the end of the markdown.
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, kParagraphDisplayLength),
             kParagraphMarkdownLength);
}

void TestBlockPositions::testMarkdownPositionInVerbatimBlockIsIdentity()
{
    // A code block's asterisks are content, not markers, so nothing is
    // hidden and every position maps to itself.
    QCOMPARE(BlockPositions::markdownPosition(m_model, 1, 0), 0);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 1, 12), 12);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 1, kCodeContent.length()),
             int(kCodeContent.length()));
}

void TestBlockPositions::testMarkdownPositionPastEndClampsToContent()
{
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, kParagraphDisplayLength + 1),
             kParagraphMarkdownLength);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, 500),
             kParagraphMarkdownLength);
    // The verbatim path clamps to the same place.
    QCOMPARE(BlockPositions::markdownPosition(m_model, 1, 500),
             int(kCodeContent.length()));
}

void TestBlockPositions::testMarkdownPositionNegativeMapsToStartOfText()
{
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, -1), 0);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 0, -500), 0);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 1, -1), 0);
    // "Start of the text" is not always markdown 0. Block 3 opens with a
    // marker, so its first visible character is at markdown 2, and that
    // is where display position 0 — and anything below it — lands.
    QCOMPARE(BlockPositions::markdownPosition(m_model, 3, 0), 2);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 3, -4), 2);
}

void TestBlockPositions::testMarkdownPositionOutOfRangeBlockIsZero()
{
    QCOMPARE(BlockPositions::markdownPosition(m_model, m_model->count(), 5), 0);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 99, 5), 0);
    QCOMPARE(BlockPositions::markdownPosition(m_model, -1, 5), 0);
}

void TestBlockPositions::testMarkdownPositionNullModelIsZero()
{
    QCOMPARE(BlockPositions::markdownPosition(nullptr, 0, 5), 0);
}

// ---- markdown -> display ----

void TestBlockPositions::testDisplayPositionSkipsHiddenMarkers()
{
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 0), 0);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 5), 5);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 10), 8);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 13), 11);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 16), 12);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, kParagraphMarkdownLength),
             kParagraphDisplayLength);
}

void TestBlockPositions::testDisplayPositionInsideMarkerClampsToContentEdge()
{
    // Markdown 8 and 9 are the opening asterisks and 14 and 15 the
    // closing pair. Neither pair is on screen, so each offset inside them
    // answers the nearest edge of the content the markers wrap.
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 8), 8);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 9), 8);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 14), 12);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 15), 12);
}

void TestBlockPositions::testDisplayPositionInVerbatimBlockIsIdentity()
{
    QCOMPARE(BlockPositions::displayPosition(m_model, 1, 0), 0);
    QCOMPARE(BlockPositions::displayPosition(m_model, 1, 12), 12);
    QCOMPARE(BlockPositions::displayPosition(m_model, 1, kCodeContent.length()),
             int(kCodeContent.length()));
}

void TestBlockPositions::testDisplayPositionPastEndClampsToDisplayText()
{
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, kParagraphMarkdownLength + 1),
             kParagraphDisplayLength);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, 500),
             kParagraphDisplayLength);
    QCOMPARE(BlockPositions::displayPosition(m_model, 1, 500),
             int(kCodeContent.length()));
}

void TestBlockPositions::testDisplayPositionNegativeIsZero()
{
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, -1), 0);
    QCOMPARE(BlockPositions::displayPosition(m_model, 0, -500), 0);
    QCOMPARE(BlockPositions::displayPosition(m_model, 1, -1), 0);
    // Markdown 0 of a block opening with a marker is inside that marker,
    // which clamps forward to the content edge — display 0 all the same,
    // because nothing before "bold" is visible.
    QCOMPARE(BlockPositions::displayPosition(m_model, 3, -1), 0);
}

void TestBlockPositions::testDisplayPositionOutOfRangeBlockIsZero()
{
    QCOMPARE(BlockPositions::displayPosition(m_model, m_model->count(), 5), 0);
    QCOMPARE(BlockPositions::displayPosition(m_model, 99, 5), 0);
    QCOMPARE(BlockPositions::displayPosition(m_model, -1, 5), 0);
}

void TestBlockPositions::testDisplayPositionNullModelIsZero()
{
    QCOMPARE(BlockPositions::displayPosition(nullptr, 0, 5), 0);
}

// ---- both directions ----

void TestBlockPositions::testEmptyBlockAnswersZero()
{
    // A divider holds no text at all: there is one position in it, and
    // every input maps to it.
    QCOMPARE(BlockPositions::markdownPosition(m_model, 2, 0), 0);
    QCOMPARE(BlockPositions::markdownPosition(m_model, 2, 7), 0);
    QCOMPARE(BlockPositions::displayPosition(m_model, 2, 0), 0);
    QCOMPARE(BlockPositions::displayPosition(m_model, 2, 7), 0);
}

void TestBlockPositions::testRoundTripOverEveryDisplayPosition()
{
    // Every display position has a markdown offset that maps back to it,
    // which is what a caller storing a range and redrawing it later
    // depends on. The reverse round trip does not hold everywhere and is
    // not claimed: a marker offset has no display position of its own.
    for (int display = 0; display <= kParagraphDisplayLength; ++display) {
        const int md = BlockPositions::markdownPosition(m_model, 0, display);
        QCOMPARE(BlockPositions::displayPosition(m_model, 0, md), display);
    }
    for (int display = 0; display <= kCodeContent.length(); ++display) {
        const int md = BlockPositions::markdownPosition(m_model, 1, display);
        QCOMPARE(BlockPositions::displayPosition(m_model, 1, md), display);
    }
}

QTEST_MAIN(TestBlockPositions)
#include "test_blockpositions.moc"
