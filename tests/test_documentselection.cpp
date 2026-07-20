// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "documentselection.h"
#include "blockmodel.h"
#include "block.h"

// The document-level selection object (phase6-plan.md step 1): block
// selection (committed set + anchor/head range), cross-block text
// selection (anchor extent + raw head with granularity snapping), id
// stability, pruning, and the revision contract.
class TestDocumentSelection : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Block selection gestures
    void testSelectBlock();
    void testSelectBlockReplacesPrevious();
    void testToggleBlockBuildsNonContiguousSet();
    void testToggleOffRemovesFromRange();
    void testExtendAfterToggleAnchorsAtToggle();
    void testExtendTo();
    void testExtendToBackward();
    void testExtendToShrinks();
    void testExtendByDelta();
    void testExtendByDeltaClamps();
    void testSelectAllBlocks();
    void testCollapseDown();
    void testCollapseUp();
    void testCollapseClampsAtEdges();
    void testLastActiveIndex();

    // Mutual exclusion
    void testTextSelectionClearsBlockSelection();
    void testBlockSelectionClearsTextSelection();

    // Text selection
    void testForwardRangePortions();
    void testBackwardRangePortions();
    void testSameBlockForwardAndBackward();
    void testDividerInsideRangeIsFull();
    void testZeroWidthEdgePortion();
    void testCollapsedRangeIsNoSelection();
    void testOrderedTextRange();
    void testPositionsClampToContent();

    // Granularity
    void testWordBoundaries_data();
    void testWordBoundaries();
    void testWordGranularitySnapsBothEnds();
    void testWordGranularityBackward();
    void testBlockGranularity();
    void testSameBlockWordSelectionKeepsAnchorExtent();

    // Clipboard markdown of the range
    void testRangeMarkdownFragmentsAndStructure();
    void testRangeMarkdownSpansStaySelfContained();
    void testRangeMarkdownTightLists();

    // Structure changes
    void testIdsSurviveMoves();
    void testPruneOnRemoval();
    void testPruneAnchorKeepsCommittedSet();
    void testPruneClearsTextSelection();
    void testModelResetClears();
    void testPruneTouchesOnlyRemovedRange();

    // Revision contract
    void testRevisionBumpsExactlyOnChange();

private:
    // Blocks: 0..5 = P0, P1, P2, Divider, P4, P5 (contents below)
    BlockModel *m_model = nullptr;
    DocumentSelection *m_sel = nullptr;
};

void TestDocumentSelection::init()
{
    m_model = new BlockModel(this);
    m_model->insertBlock(0, Block::Paragraph, "alpha beta");
    m_model->insertBlock(1, Block::Paragraph, "second block");
    m_model->insertBlock(2, Block::Paragraph, "third");
    m_model->insertBlock(3, Block::Divider, "");
    m_model->insertBlock(4, Block::Paragraph, "fifth block here");
    m_model->insertBlock(5, Block::Paragraph, "last");

    m_sel = new DocumentSelection(this);
    m_sel->setModel(m_model);
}

void TestDocumentSelection::cleanup()
{
    delete m_sel;
    m_sel = nullptr;
    delete m_model;
    m_model = nullptr;
}

// ---- block selection ----

void TestDocumentSelection::testSelectBlock()
{
    QVERIFY(!m_sel->hasBlockSelection());
    m_sel->selectBlock(2);
    QVERIFY(m_sel->hasBlockSelection());
    QVERIFY(m_sel->isBlockSelected(2));
    QVERIFY(!m_sel->isBlockSelected(1));
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{2}));
}

void TestDocumentSelection::testSelectBlockReplacesPrevious()
{
    m_sel->selectBlock(0);
    m_sel->selectBlock(4);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{4}));
}

void TestDocumentSelection::testToggleBlockBuildsNonContiguousSet()
{
    m_sel->toggleBlock(0);
    m_sel->toggleBlock(2);
    m_sel->toggleBlock(5);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 2, 5}));
    m_sel->toggleBlock(2);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 5}));
}

void TestDocumentSelection::testToggleOffRemovesFromRange()
{
    m_sel->selectBlock(0);
    m_sel->extendBlockSelectionTo(2);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 1, 2}));
    m_sel->toggleBlock(1);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 2}));
}

void TestDocumentSelection::testExtendAfterToggleAnchorsAtToggle()
{
    m_sel->selectBlock(0);
    m_sel->toggleBlock(4); // anchor moves to 4, set = {0, 4}
    m_sel->extendBlockSelectionTo(2); // range 2..4 joins the set
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 2, 3, 4}));
}

void TestDocumentSelection::testExtendTo()
{
    m_sel->selectBlock(1);
    m_sel->extendBlockSelectionTo(3);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({1, 2, 3}));
}

void TestDocumentSelection::testExtendToBackward()
{
    m_sel->selectBlock(3);
    m_sel->extendBlockSelectionTo(1);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({1, 2, 3}));
}

void TestDocumentSelection::testExtendToShrinks()
{
    m_sel->selectBlock(0);
    m_sel->extendBlockSelectionTo(4);
    m_sel->extendBlockSelectionTo(2); // head moves back; anchor holds
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 1, 2}));
}

void TestDocumentSelection::testExtendByDelta()
{
    m_sel->selectBlock(2);
    m_sel->extendBlockSelection(1);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({2, 3}));
    m_sel->extendBlockSelection(1);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({2, 3, 4}));
    m_sel->extendBlockSelection(-1);
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({2, 3}));
}

void TestDocumentSelection::testExtendByDeltaClamps()
{
    m_sel->selectBlock(5);
    m_sel->extendBlockSelection(1);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{5}));
    m_sel->selectBlock(0);
    m_sel->extendBlockSelection(-1);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{0}));
}

void TestDocumentSelection::testSelectAllBlocks()
{
    m_sel->selectAllBlocks();
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 1, 2, 3, 4, 5}));
}

void TestDocumentSelection::testCollapseDown()
{
    m_sel->selectBlock(1);
    m_sel->extendBlockSelectionTo(3);
    m_sel->collapseBlockSelection(1);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{4}));
}

void TestDocumentSelection::testCollapseUp()
{
    m_sel->selectBlock(2);
    m_sel->extendBlockSelectionTo(3);
    m_sel->collapseBlockSelection(-1);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{1}));
}

void TestDocumentSelection::testCollapseClampsAtEdges()
{
    m_sel->selectBlock(0);
    m_sel->collapseBlockSelection(-1);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{0}));
    m_sel->selectBlock(5);
    m_sel->collapseBlockSelection(1);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{5}));
}

void TestDocumentSelection::testLastActiveIndex()
{
    QCOMPARE(m_sel->lastActiveIndex(), -1);
    m_sel->selectBlock(1);
    QCOMPARE(m_sel->lastActiveIndex(), 1);
    m_sel->extendBlockSelectionTo(4);
    QCOMPARE(m_sel->lastActiveIndex(), 4);
    m_sel->toggleBlock(0);
    QCOMPARE(m_sel->lastActiveIndex(), 0);
}

// ---- mutual exclusion ----

void TestDocumentSelection::testTextSelectionClearsBlockSelection()
{
    m_sel->selectBlock(1);
    m_sel->extendBlockSelectionTo(3);
    m_sel->beginTextSelection(0, 2, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(1, 3);
    QVERIFY(m_sel->hasTextSelection());
    QVERIFY(!m_sel->hasBlockSelection());
    QCOMPARE(m_sel->selectedIndexes(), QVariantList());
}

void TestDocumentSelection::testBlockSelectionClearsTextSelection()
{
    m_sel->beginTextSelection(0, 2, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(1, 3);
    QVERIFY(m_sel->hasTextSelection());
    m_sel->selectBlock(2);
    QVERIFY(!m_sel->hasTextSelection());
    QVERIFY(m_sel->hasBlockSelection());
}

// ---- text selection ----

void TestDocumentSelection::testForwardRangePortions()
{
    // From "alpha |beta" (pos 6 in block 0) to "fifth| block here"
    // (pos 5 in block 4): tail of 0, all of 1..3, head of 4.
    m_sel->beginTextSelection(0, 6, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(4, 5);
    QVERIFY(m_sel->hasTextSelection());

    QVariantMap p0 = m_sel->portionForBlock(0);
    QCOMPARE(p0.value("selected").toBool(), true);
    QCOMPARE(p0.value("full").toBool(), false);
    QCOMPARE(p0.value("start").toInt(), 6);
    QCOMPARE(p0.value("end").toInt(), 10); // "alpha beta".length()

    QVariantMap p1 = m_sel->portionForBlock(1);
    QCOMPARE(p1.value("selected").toBool(), true);
    QCOMPARE(p1.value("full").toBool(), true);
    QCOMPARE(p1.value("start").toInt(), 0);
    QCOMPARE(p1.value("end").toInt(), 12); // "second block".length()

    QVariantMap p4 = m_sel->portionForBlock(4);
    QCOMPARE(p4.value("selected").toBool(), true);
    QCOMPARE(p4.value("full").toBool(), false);
    QCOMPARE(p4.value("start").toInt(), 0);
    QCOMPARE(p4.value("end").toInt(), 5);

    QCOMPARE(m_sel->portionForBlock(5).value("selected").toBool(), false);
}

void TestDocumentSelection::testBackwardRangePortions()
{
    // Anchor in block 4, head dragged up into block 1: same ordered
    // range as a forward drag between the same points.
    m_sel->beginTextSelection(4, 5, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(1, 7);

    QVariantMap p1 = m_sel->portionForBlock(1);
    QCOMPARE(p1.value("selected").toBool(), true);
    QCOMPARE(p1.value("start").toInt(), 7);
    QCOMPARE(p1.value("end").toInt(), 12);

    QVariantMap p2 = m_sel->portionForBlock(2);
    QCOMPARE(p2.value("full").toBool(), true);

    QVariantMap p4 = m_sel->portionForBlock(4);
    QCOMPARE(p4.value("start").toInt(), 0);
    QCOMPARE(p4.value("end").toInt(), 5);

    QCOMPARE(m_sel->portionForBlock(0).value("selected").toBool(), false);
}

void TestDocumentSelection::testSameBlockForwardAndBackward()
{
    m_sel->beginTextSelection(1, 3, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(1, 8);
    QVariantMap fwd = m_sel->portionForBlock(1);
    QCOMPARE(fwd.value("start").toInt(), 3);
    QCOMPARE(fwd.value("end").toInt(), 8);

    m_sel->updateTextSelectionHead(1, 1);
    QVariantMap back = m_sel->portionForBlock(1);
    QCOMPARE(back.value("start").toInt(), 1);
    QCOMPARE(back.value("end").toInt(), 3);
}

void TestDocumentSelection::testDividerInsideRangeIsFull()
{
    m_sel->beginTextSelection(2, 1, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(4, 2);
    QVariantMap divider = m_sel->portionForBlock(3);
    QCOMPARE(divider.value("selected").toBool(), true);
    QCOMPARE(divider.value("full").toBool(), true);
}

void TestDocumentSelection::testZeroWidthEdgePortion()
{
    // Head stops exactly at block 2's start: block 2 renders nothing.
    m_sel->beginTextSelection(1, 4, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(2, 0);
    QVERIFY(m_sel->hasTextSelection());
    QCOMPARE(m_sel->portionForBlock(2).value("selected").toBool(), false);
    QCOMPARE(m_sel->portionForBlock(1).value("selected").toBool(), true);
}

void TestDocumentSelection::testCollapsedRangeIsNoSelection()
{
    m_sel->beginTextSelection(1, 4, DocumentSelection::CharacterGranularity);
    QVERIFY(!m_sel->hasTextSelection());
    m_sel->updateTextSelectionHead(1, 4);
    QVERIFY(!m_sel->hasTextSelection());
    QCOMPARE(m_sel->portionForBlock(1).value("selected").toBool(), false);
}

void TestDocumentSelection::testOrderedTextRange()
{
    m_sel->beginTextSelection(4, 5, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(1, 7);
    QVariantMap range = m_sel->orderedTextRange();
    QCOMPARE(range.value("startIndex").toInt(), 1);
    QCOMPARE(range.value("startPos").toInt(), 7);
    QCOMPARE(range.value("endIndex").toInt(), 4);
    QCOMPARE(range.value("endPos").toInt(), 5);
    QCOMPARE(m_sel->textAnchorIndex(), 4);
    QCOMPARE(m_sel->textHeadIndex(), 1);
    QCOMPARE(m_sel->textHeadPosition(), 7);
}

void TestDocumentSelection::testPositionsClampToContent()
{
    m_sel->beginTextSelection(0, 999, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(2, 999);
    // The anchor clamped to block 0's end, so block 0's portion is zero
    // width; the head clamped to block 2's end.
    QCOMPARE(m_sel->portionForBlock(0).value("selected").toBool(), false);
    QCOMPARE(m_sel->portionForBlock(1).value("full").toBool(), true);
    QVariantMap p2 = m_sel->portionForBlock(2);
    QCOMPARE(p2.value("end").toInt(), 5); // "third".length()
    QCOMPARE(p2.value("full").toBool(), true);
}

// ---- granularity ----

void TestDocumentSelection::testWordBoundaries_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("pos");
    QTest::addColumn<int>("expectedStart");
    QTest::addColumn<int>("expectedEnd");

    QTest::newRow("mid word") << "alpha beta" << 2 << 0 << 5;
    // At a class boundary the two functions classify differently by
    // design: wordStart looks left (the space run), wordEnd looks right
    // (the word) — a boundary click still snaps outward over the word.
    QTest::newRow("word start boundary") << "alpha beta" << 6 << 5 << 10;
    QTest::newRow("in space run") << "a   b" << 2 << 1 << 4;
    QTest::newRow("snake_case is one word") << "snake_case x" << 3 << 0 << 10;
    QTest::newRow("punctuation run") << "a...b" << 2 << 1 << 4;
    QTest::newRow("at end") << "word" << 4 << 0 << 4;
    QTest::newRow("at zero") << "word" << 0 << 0 << 4;
    QTest::newRow("empty text") << "" << 0 << 0 << 0;
}

void TestDocumentSelection::testWordBoundaries()
{
    QFETCH(QString, text);
    QFETCH(int, pos);
    QFETCH(int, expectedStart);
    QFETCH(int, expectedEnd);

    // wordStart classifies by the char BEFORE pos, wordEnd by the char AT
    // pos; for a position inside one class run they agree on the run.
    QCOMPARE(DocumentSelection::wordStart(text, pos), expectedStart);
    QCOMPARE(DocumentSelection::wordEnd(text, pos), expectedEnd);
}

void TestDocumentSelection::testWordGranularitySnapsBothEnds()
{
    // Double-click on "alpha" (block 0), drag into "second" (block 1):
    // anchor extends to the whole first word, head snaps to a word end.
    m_sel->beginTextSelection(0, 2, DocumentSelection::WordGranularity);
    m_sel->updateTextSelectionHead(1, 2); // inside "second"

    QVariantMap p0 = m_sel->portionForBlock(0);
    QCOMPARE(p0.value("start").toInt(), 0); // "alpha" start
    QVariantMap p1 = m_sel->portionForBlock(1);
    QCOMPARE(p1.value("end").toInt(), 6); // "second" end
}

void TestDocumentSelection::testWordGranularityBackward()
{
    // Double-click in block 1's "block" (pos 8), drag up into block 0's
    // "beta": head snaps to a word START going backward, anchor keeps
    // its word END.
    m_sel->beginTextSelection(1, 8, DocumentSelection::WordGranularity);
    m_sel->updateTextSelectionHead(0, 8); // inside "beta"

    QVariantMap p0 = m_sel->portionForBlock(0);
    QCOMPARE(p0.value("start").toInt(), 6); // "beta" start
    QVariantMap p1 = m_sel->portionForBlock(1);
    QCOMPARE(p1.value("end").toInt(), 12); // "second block" end ("block")
}

void TestDocumentSelection::testBlockGranularity()
{
    // Triple-click block 1, drag into block 2: whole blocks select.
    m_sel->beginTextSelection(1, 5, DocumentSelection::BlockGranularity);
    QVERIFY(m_sel->hasTextSelection()); // whole block already selected
    QCOMPARE(m_sel->portionForBlock(1).value("full").toBool(), true);

    m_sel->updateTextSelectionHead(2, 2);
    QCOMPARE(m_sel->portionForBlock(1).value("full").toBool(), true);
    QCOMPARE(m_sel->portionForBlock(2).value("full").toBool(), true);
}

void TestDocumentSelection::testSameBlockWordSelectionKeepsAnchorExtent()
{
    // Double-click "beta": both words of the extent stay selected even
    // when the head wanders back inside the anchor word.
    m_sel->beginTextSelection(0, 7, DocumentSelection::WordGranularity);
    QCOMPARE(m_sel->portionForBlock(0).value("start").toInt(), 6);
    QCOMPARE(m_sel->portionForBlock(0).value("end").toInt(), 10);

    m_sel->updateTextSelectionHead(0, 8); // still inside "beta"
    QCOMPARE(m_sel->portionForBlock(0).value("start").toInt(), 6);
    QCOMPARE(m_sel->portionForBlock(0).value("end").toInt(), 10);
}

// ---- clipboard markdown ----

void TestDocumentSelection::testRangeMarkdownFragmentsAndStructure()
{
    // From "alpha |beta" through the divider into "fifth| block here":
    // partial edges as bare fragments, full middles with structure.
    m_sel->beginTextSelection(0, 6, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(4, 5);
    QCOMPARE(m_sel->rangeMarkdown(),
             QString("beta\n\nsecond block\n\nthird\n\n---\n\nfifth"));

    // Backward drag between the same points yields the same markdown
    m_sel->beginTextSelection(4, 5, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(0, 6);
    QCOMPARE(m_sel->rangeMarkdown(),
             QString("beta\n\nsecond block\n\nthird\n\n---\n\nfifth"));

    QVERIFY(m_sel->hasTextSelection());
    m_sel->clearTextSelection();
    QCOMPARE(m_sel->rangeMarkdown(), QString());
}

void TestDocumentSelection::testRangeMarkdownSpansStaySelfContained()
{
    m_model->insertBlock(0, Block::Paragraph, "go **bold word** on");
    // Anchor at markdown 6 — inside the bold span ("b|old word") — to
    // "alpha|" in the next block: the partially selected span re-wraps
    // as a self-contained fragment, like the in-block copy does.
    m_sel->beginTextSelection(0, 6, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(1, 5);
    QCOMPARE(m_sel->rangeMarkdown(), QString("**old word** on\n\nalpha"));
}

void TestDocumentSelection::testRangeMarkdownTightLists()
{
    BlockModel model;
    model.insertBlock(0, Block::Paragraph, "intro text");
    model.insertBlock(1, Block::BulletList, "one");
    model.insertBlock(2, Block::NumberedList, "first");
    model.insertBlock(3, Block::NumberedList, "second");
    model.insertBlock(4, Block::Paragraph, "outro");
    DocumentSelection sel;
    sel.setModel(&model);

    // Full list blocks keep their prefixes, document ordinals, and
    // tight separators; the partial edges stay fragments.
    sel.beginTextSelection(0, 6, DocumentSelection::CharacterGranularity);
    sel.updateTextSelectionHead(4, 2);
    QCOMPARE(sel.rangeMarkdown(),
             QString("text\n\n- one\n1. first\n2. second\n\nou"));
}

// ---- structure changes ----

void TestDocumentSelection::testIdsSurviveMoves()
{
    m_sel->selectBlock(1);
    m_sel->extendBlockSelectionTo(2); // blocks "second block", "third"
    m_model->moveBlock(0, 5);         // shift everything up by one
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 1}));
    QCOMPARE(m_model->blockAt(0)->content(), QString("second block"));
}

void TestDocumentSelection::testPruneOnRemoval()
{
    m_sel->toggleBlock(0);
    m_sel->toggleBlock(2);
    m_model->removeBlock(2);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{0}));
    QVERIFY(m_sel->hasBlockSelection());
}

void TestDocumentSelection::testPruneAnchorKeepsCommittedSet()
{
    m_sel->toggleBlock(4);            // committed {4}
    m_sel->toggleBlock(0);            // committed {0, 4}, anchor at 0
    m_sel->extendBlockSelectionTo(2); // + range 0..2 -> {0, 1, 2, 4}
    QCOMPARE(m_sel->selectedIndexes(), QVariantList({0, 1, 2, 4}));

    // Removing the range's anchor drops the range but keeps the
    // committed set ("fifth block here" shifts from index 4 to 3).
    m_model->removeBlock(0);
    QCOMPARE(m_sel->selectedIndexes(), (QVariantList{3}));
    QCOMPARE(m_model->blockAt(3)->content(), QString("fifth block here"));
}

void TestDocumentSelection::testPruneClearsTextSelection()
{
    m_sel->beginTextSelection(1, 2, DocumentSelection::CharacterGranularity);
    m_sel->updateTextSelectionHead(4, 3);
    QVERIFY(m_sel->hasTextSelection());
    m_model->removeBlock(4); // head block gone
    QVERIFY(!m_sel->hasTextSelection());
}

void TestDocumentSelection::testModelResetClears()
{
    m_sel->selectAllBlocks();
    QVERIFY(m_sel->hasBlockSelection());
    m_model->clear();
    QVERIFY(!m_sel->hasBlockSelection());
}

// ---- revision contract ----

void TestDocumentSelection::testRevisionBumpsExactlyOnChange()
{
    QSignalSpy spy(m_sel, &DocumentSelection::revisionChanged);

    m_sel->selectBlock(1);
    QCOMPARE(spy.count(), 1);
    m_sel->selectBlock(1); // no observable change
    QCOMPARE(spy.count(), 1);
    m_sel->extendBlockSelectionTo(3);
    QCOMPARE(spy.count(), 2);
    m_sel->extendBlockSelectionTo(3); // same head
    QCOMPARE(spy.count(), 2);
    m_sel->clear();
    QCOMPARE(spy.count(), 3);
    m_sel->clear(); // already clear
    QCOMPARE(spy.count(), 3);

    m_sel->beginTextSelection(0, 2, DocumentSelection::CharacterGranularity);
    QCOMPARE(spy.count(), 4);
    m_sel->updateTextSelectionHead(1, 3);
    QCOMPARE(spy.count(), 5);
    m_sel->updateTextSelectionHead(1, 3); // same head
    QCOMPARE(spy.count(), 5);
}

// Phase 6 range-based pruning: only ids in the removed range leave the
// selection, and removing an unselected block changes nothing — no
// revision bump, no membership churn.
void TestDocumentSelection::testPruneTouchesOnlyRemovedRange()
{
    m_sel->toggleBlock(0);
    m_sel->toggleBlock(2);
    QVERIFY(m_sel->isBlockSelected(0));
    QVERIFY(m_sel->isBlockSelected(2));

    QSignalSpy spy(m_sel, &DocumentSelection::revisionChanged);

    // Removing an unselected block keeps the selection and stays quiet.
    m_model->removeBlockInternal(4);
    QCOMPARE(spy.count(), 0);
    QVERIFY(m_sel->isBlockSelected(0));
    QVERIFY(m_sel->isBlockSelected(2));

    // Removing a selected block prunes exactly that id and bumps once.
    m_model->removeBlockInternal(2);
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_sel->isBlockSelected(0));
    QCOMPARE(m_sel->selectedIndexes().size(), 1);
}

QTEST_MAIN(TestDocumentSelection)
#include "test_documentselection.moc"
