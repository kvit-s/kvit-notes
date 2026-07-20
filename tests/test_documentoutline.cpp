// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "documentoutline.h"
#include "blockmodel.h"
#include "block.h"

// The document outline: the shared slug function with its corpus, the
// heading-tree shape and nesting, collision-disambiguated
// slugs, resolution (slug<->block), the current-section query, collapse, the
// level filter, the TOC body, and the revision/slugs contract.
class TestDocumentOutline : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testBaseSlug_data();
    void testBaseSlug();
    void testSlugFromDisplayTextStripsMarkers();

    void testTreeLevelsAndText();
    void testNestingDepthWithSkips();
    void testHasChildren();
    void testCollisionSuffixesInDocumentOrder();

    void testResolveSlugToBlock();
    void testUnresolvedSlug();
    void testSlugForBlockIndex();

    void testRowForBlockIsPrecedingSection();
    void testRowForBlockBeforeFirstHeadingIsNone();
    void testCurrentRowTracksCaret();

    void testCollapseHidesChildren();
    void testRowForBlockUnderCollapsedMapsToCollapser();
    void testCollapseStatePrunedOnRemoval();

    void testLevelFilterHidesRows();
    void testLevelFilterKeepsChildDepth();

    void testHeadingsList();
    void testTocMarkdownNested();
    void testTocLinksResolveToOutlineNodes();

    void testRebuildOnStructuralChangeIsQueued();
    void testRevisionBumpsOnChange();
    void testSlugsChangedOnlyWhenSetChanges();
    void testIgnoresNonHeadingDataChanges();
    void testSkipsNoOpStructuralRebuild();
    void testHeadingEditRebuildsOnce();

private:
    BlockModel *m_model = nullptr;
    DocumentOutline *m_outline = nullptr;

    void addHeading(int level, const QString &text)
    {
        Block::BlockType t = Block::Heading1;
        switch (level) {
        case 1: t = Block::Heading1; break;
        case 2: t = Block::Heading2; break;
        case 3: t = Block::Heading3; break;
        case 4: t = Block::Heading4; break;
        }
        m_model->insertBlock(m_model->count(), t, text);
    }
    void addPara(const QString &text)
    {
        m_model->insertBlock(m_model->count(), Block::Paragraph, text);
    }
};

void TestDocumentOutline::init()
{
    m_model = new BlockModel(this);
    m_outline = new DocumentOutline(this);
    // Build the model first, then attach: setModel's initial rebuild is
    // synchronous, so the fixtures below are visible without spinning the loop.
    // Tests that mutate after attach call rebuildNow() themselves.
}

void TestDocumentOutline::cleanup()
{
    delete m_outline; m_outline = nullptr;
    delete m_model; m_model = nullptr;
}

void TestDocumentOutline::testBaseSlug_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("slug");
    QTest::newRow("simple") << "Introduction" << "introduction";
    QTest::newRow("spaces") << "Getting Started" << "getting-started";
    QTest::newRow("case") << "HELLO World" << "hello-world";
    QTest::newRow("punctuation") << "What's new?" << "whats-new";
    QTest::newRow("colon+parens") << "Section 2: Details (v1)" << "section-2-details-v1";
    QTest::newRow("underscores") << "snake_case_name" << "snake-case-name";
    QTest::newRow("collapse-hyphens") << "a  --  b" << "a-b";
    QTest::newRow("trim-edges") << "  spaced  " << "spaced";
    QTest::newRow("leading-punct") << "!!!bang" << "bang";
    QTest::newRow("digits") << "Chapter 12" << "chapter-12";
    QTest::newRow("unicode") << QString::fromUtf8("Café Ünïcode")
                             << QString::fromUtf8("café-ünïcode");
    QTest::newRow("all-punct") << "***" << "";
}

void TestDocumentOutline::testBaseSlug()
{
    QFETCH(QString, text);
    QFETCH(QString, slug);
    QCOMPARE(DocumentOutline::baseSlug(text), slug);
}

void TestDocumentOutline::testSlugFromDisplayTextStripsMarkers()
{
    addHeading(1, "The **bold** and *italic* title");
    m_outline->setModel(m_model);
    // Node text is display text (markers stripped), so the slug drops markers.
    QCOMPARE(m_outline->slugForBlockIndex(0),
             QString("the-bold-and-italic-title"));
}

void TestDocumentOutline::testTreeLevelsAndText()
{
    addHeading(1, "One");
    addHeading(2, "Two");
    addHeading(4, "Four");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->rowCount(), 3);
    QCOMPARE(m_outline->data(m_outline->index(0), DocumentOutline::LevelRole).toInt(), 1);
    QCOMPARE(m_outline->data(m_outline->index(2), DocumentOutline::LevelRole).toInt(), 4);
    QCOMPARE(m_outline->data(m_outline->index(1), DocumentOutline::TextRole).toString(),
             QString("Two"));
}

void TestDocumentOutline::testNestingDepthWithSkips()
{
    addHeading(1, "H1");   // depth 0
    addHeading(3, "H3");   // depth 1 (nested under H1 even though level skips)
    addHeading(2, "H2b");  // depth 1 (sibling under H1)
    addHeading(3, "H3b");  // depth 2 (under H2b)
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->data(m_outline->index(0), DocumentOutline::DepthRole).toInt(), 0);
    QCOMPARE(m_outline->data(m_outline->index(1), DocumentOutline::DepthRole).toInt(), 1);
    QCOMPARE(m_outline->data(m_outline->index(2), DocumentOutline::DepthRole).toInt(), 1);
    QCOMPARE(m_outline->data(m_outline->index(3), DocumentOutline::DepthRole).toInt(), 2);
}

void TestDocumentOutline::testHasChildren()
{
    addHeading(1, "Parent");
    addHeading(2, "Child");
    addHeading(1, "Lonely");
    m_outline->setModel(m_model);
    QVERIFY(m_outline->data(m_outline->index(0), DocumentOutline::HasChildrenRole).toBool());
    QVERIFY(!m_outline->data(m_outline->index(1), DocumentOutline::HasChildrenRole).toBool());
    QVERIFY(!m_outline->data(m_outline->index(2), DocumentOutline::HasChildrenRole).toBool());
}

void TestDocumentOutline::testCollisionSuffixesInDocumentOrder()
{
    addHeading(1, "Overview");
    addHeading(2, "Overview");
    addHeading(2, "Overview");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->slugForBlockIndex(0), QString("overview"));
    QCOMPARE(m_outline->slugForBlockIndex(1), QString("overview-1"));
    QCOMPARE(m_outline->slugForBlockIndex(2), QString("overview-2"));
}

void TestDocumentOutline::testResolveSlugToBlock()
{
    addPara("intro");
    addHeading(1, "Alpha");
    addPara("body");
    addHeading(2, "Beta");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->blockIndexForSlug("alpha"), 1);
    QCOMPARE(m_outline->blockIndexForSlug("beta"), 3);
    QVERIFY(m_outline->hasSlug("beta"));
}

void TestDocumentOutline::testUnresolvedSlug()
{
    addHeading(1, "Alpha");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->blockIndexForSlug("nope"), -1);
    QVERIFY(!m_outline->hasSlug("nope"));
}

void TestDocumentOutline::testSlugForBlockIndex()
{
    addHeading(1, "First Heading");
    addPara("x");
    addHeading(2, "Second Heading");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->slugForBlockIndex(0), QString("first-heading"));
    QCOMPARE(m_outline->slugForBlockIndex(2), QString("second-heading"));
    QCOMPARE(m_outline->slugForBlockIndex(1), QString()); // not a heading
}

void TestDocumentOutline::testRowForBlockIsPrecedingSection()
{
    addHeading(1, "A");   // block 0
    addPara("a1");        // block 1
    addPara("a2");        // block 2
    addHeading(1, "B");   // block 3
    addPara("b1");        // block 4
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->rowForBlock(0), 0);
    QCOMPARE(m_outline->rowForBlock(2), 0); // still in section A
    QCOMPARE(m_outline->rowForBlock(3), 1);
    QCOMPARE(m_outline->rowForBlock(4), 1); // in section B
}

void TestDocumentOutline::testRowForBlockBeforeFirstHeadingIsNone()
{
    addPara("preamble");
    addHeading(1, "A");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->rowForBlock(0), -1);
}

void TestDocumentOutline::testCurrentRowTracksCaret()
{
    addHeading(1, "A");
    addPara("a");
    addHeading(1, "B");
    m_outline->setModel(m_model);
    QSignalSpy spy(m_outline, &DocumentOutline::currentRowChanged);
    m_outline->setCurrentBlock(1);
    QCOMPARE(m_outline->currentRow(), 0);
    m_outline->setCurrentBlock(2);
    QCOMPARE(m_outline->currentRow(), 1);
    QVERIFY(spy.count() >= 2);
    QVERIFY(m_outline->data(m_outline->index(1), DocumentOutline::IsCurrentRole).toBool());
    QVERIFY(!m_outline->data(m_outline->index(0), DocumentOutline::IsCurrentRole).toBool());
}

void TestDocumentOutline::testCollapseHidesChildren()
{
    addHeading(1, "Parent");
    addHeading(2, "Child A");
    addHeading(2, "Child B");
    addHeading(1, "Sibling");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->rowCount(), 4);
    m_outline->toggleCollapsed(0); // collapse Parent
    QCOMPARE(m_outline->rowCount(), 2); // Parent + Sibling only
    QCOMPARE(m_outline->data(m_outline->index(1), DocumentOutline::TextRole).toString(),
             QString("Sibling"));
    QVERIFY(m_outline->data(m_outline->index(0), DocumentOutline::CollapsedRole).toBool());
    m_outline->toggleCollapsed(0); // expand
    QCOMPARE(m_outline->rowCount(), 4);
}

void TestDocumentOutline::testRowForBlockUnderCollapsedMapsToCollapser()
{
    addHeading(1, "Parent");  // block 0, row 0
    addHeading(2, "Child");   // block 1
    addPara("under child");   // block 2
    m_outline->setModel(m_model);
    m_outline->toggleCollapsed(0);
    // The child and the paragraph beneath it now map to the collapsed Parent.
    QCOMPARE(m_outline->rowForBlock(1), 0);
    QCOMPARE(m_outline->rowForBlock(2), 0);
}

void TestDocumentOutline::testCollapseStatePrunedOnRemoval()
{
    addHeading(1, "Parent");
    addHeading(2, "Child");
    m_outline->setModel(m_model);
    m_outline->toggleCollapsed(0);
    QVERIFY(m_outline->data(m_outline->index(0), DocumentOutline::CollapsedRole).toBool());
    // Remove the parent heading; its collapse state must not leak onto a
    // later heading that happens to reuse the row.
    m_model->removeBlock(0);
    m_outline->rebuildNow();
    QCOMPARE(m_outline->rowCount(), 1);
    QVERIFY(!m_outline->data(m_outline->index(0), DocumentOutline::CollapsedRole).toBool());
}

void TestDocumentOutline::testLevelFilterHidesRows()
{
    addHeading(1, "H1");
    addHeading(2, "H2");
    addHeading(3, "H3");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->rowCount(), 3);
    // Show only levels 1 and 2 (bits 0 and 1).
    m_outline->setLevelMask(0x3);
    QCOMPARE(m_outline->rowCount(), 2);
    QCOMPARE(m_outline->data(m_outline->index(1), DocumentOutline::LevelRole).toInt(), 2);
}

void TestDocumentOutline::testLevelFilterKeepsChildDepth()
{
    addHeading(1, "H1");   // shown
    addHeading(2, "H2");   // filtered out
    addHeading(3, "H3");   // shown, nests under H1 visually
    m_outline->setModel(m_model);
    m_outline->setLevelMask(0x1 | 0x4); // levels 1 and 3
    QCOMPARE(m_outline->rowCount(), 2);
    // H3's visible depth counts only the shown ancestor (H1), so depth 1.
    QCOMPARE(m_outline->data(m_outline->index(1), DocumentOutline::LevelRole).toInt(), 3);
    QCOMPARE(m_outline->data(m_outline->index(1), DocumentOutline::DepthRole).toInt(), 1);
}

void TestDocumentOutline::testHeadingsList()
{
    addHeading(1, "Alpha");
    addPara("x");
    addHeading(2, "Beta");
    m_outline->setModel(m_model);
    const QVariantList h = m_outline->headings();
    QCOMPARE(h.size(), 2);
    QCOMPARE(h.at(0).toMap().value("text").toString(), QString("Alpha"));
    QCOMPARE(h.at(0).toMap().value("slug").toString(), QString("alpha"));
    QCOMPARE(h.at(1).toMap().value("blockIndex").toInt(), 2);
}

void TestDocumentOutline::testTocMarkdownNested()
{
    addHeading(1, "Intro");
    addHeading(2, "Details");
    addHeading(2, "More");
    m_outline->setModel(m_model);
    const QString toc = m_outline->tocMarkdown();
    const QStringList lines = toc.split('\n');
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0), QString("- [Intro](#intro)"));
    QCOMPARE(lines.at(1), QString("  - [Details](#details)"));
    QCOMPARE(lines.at(2), QString("  - [More](#more)"));
}

void TestDocumentOutline::testTocLinksResolveToOutlineNodes()
{
    addHeading(1, "Overview");
    addHeading(2, "Overview"); // collision → overview-1
    m_outline->setModel(m_model);
    const QString toc = m_outline->tocMarkdown();
    // Every generated #slug must resolve back to the same heading the outline
    // holds (the shared-slug guarantee).
    QVERIFY(toc.contains("(#overview)"));
    QVERIFY(toc.contains("(#overview-1)"));
    QCOMPARE(m_outline->blockIndexForSlug("overview"), 0);
    QCOMPARE(m_outline->blockIndexForSlug("overview-1"), 1);
}

void TestDocumentOutline::testRebuildOnStructuralChangeIsQueued()
{
    addHeading(1, "A");
    m_outline->setModel(m_model);
    QCOMPARE(m_outline->rowCount(), 1);
    // Mutating after attach schedules a compressed rebuild; it lands when the
    // event loop spins, not synchronously per keystroke.
    addHeading(1, "B");
    addHeading(1, "C");
    QCOMPARE(m_outline->rowCount(), 1); // not yet rebuilt
    QCoreApplication::processEvents();
    QCOMPARE(m_outline->rowCount(), 3); // one coalesced rebuild
}

void TestDocumentOutline::testRevisionBumpsOnChange()
{
    addHeading(1, "A");
    m_outline->setModel(m_model);
    const int r0 = m_outline->revision();
    addHeading(1, "B");
    m_outline->rebuildNow();
    QVERIFY(m_outline->revision() > r0);
}

void TestDocumentOutline::testSlugsChangedOnlyWhenSetChanges()
{
    addHeading(1, "Title");
    addPara("body");
    m_outline->setModel(m_model);
    QSignalSpy spy(m_outline, &DocumentOutline::slugsChanged);
    // Editing a NON-heading block changes no slug: no slugsChanged.
    m_model->updateContent(1, "body edited");
    m_outline->rebuildNow();
    QCOMPARE(spy.count(), 0);
    // Adding a heading changes the slug set.
    addHeading(2, "New Section");
    m_outline->rebuildNow();
    QCOMPARE(spy.count(), 1);
}

void TestDocumentOutline::testIgnoresNonHeadingDataChanges()
{
    addHeading(1, "Title");
    addPara("body");
    m_model->insertBlock(2, Block::Todo, "task");
    m_model->insertBlock(3, Block::NumberedList, "one");
    m_model->insertBlock(4, Block::NumberedList, "two");
    m_outline->setModel(m_model);

    QSignalSpy resetSpy(m_outline, &QAbstractItemModel::modelReset);
    QSignalSpy revisionSpy(m_outline, &DocumentOutline::revisionChanged);
    const int revision = m_outline->revision();

    m_model->updateContent(1, QStringLiteral("body edited"));
    m_model->setChecked(2, true);
    m_model->changeIndent(4, 1);
    QCoreApplication::processEvents();

    QCOMPARE(m_outline->revision(), revision);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(revisionSpy.count(), 0);
}

void TestDocumentOutline::testSkipsNoOpStructuralRebuild()
{
    addHeading(1, "Title");
    addPara("body");
    m_outline->setModel(m_model);

    QSignalSpy resetSpy(m_outline, &QAbstractItemModel::modelReset);
    QSignalSpy revisionSpy(m_outline, &DocumentOutline::revisionChanged);
    const int revision = m_outline->revision();

    m_model->insertBlock(m_model->count(), Block::Paragraph,
                         QStringLiteral("more body"));
    QCoreApplication::processEvents();

    QCOMPARE(m_outline->revision(), revision);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(revisionSpy.count(), 0);
}

void TestDocumentOutline::testHeadingEditRebuildsOnce()
{
    addHeading(1, "Title");
    addPara("body");
    m_outline->setModel(m_model);

    QSignalSpy resetSpy(m_outline, &QAbstractItemModel::modelReset);
    QSignalSpy revisionSpy(m_outline, &DocumentOutline::revisionChanged);
    QSignalSpy slugsSpy(m_outline, &DocumentOutline::slugsChanged);

    m_model->updateContent(0, QStringLiteral("Renamed"));

    QTRY_COMPARE(resetSpy.count(), 1);
    QCOMPARE(revisionSpy.count(), 1);
    QCOMPARE(slugsSpy.count(), 1);
    QCOMPARE(m_outline->data(m_outline->index(0), DocumentOutline::TextRole).toString(),
             QStringLiteral("Renamed"));
}

QTEST_MAIN(TestDocumentOutline)
#include "test_documentoutline.moc"
