// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "documentsearch.h"
#include "blockmodel.h"
#include "block.h"
#include "undostack.h"

// The document-level search object: match computation over display
// text with the case/word/regex options,
// document-ordered navigation with wrap and cursor seeding, the
// in-selection domain, the replace computations (cut-contract semantics,
// capture groups, preserve case), recompute on model changes, and the
// revision contract.
class TestDocumentSearch : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Matching
    void testPlainMatchAcrossBlocks();
    void testMatchSpansMarkerBoundary();
    void testMarkersAreNotSearchable();
    void testCodeBlockSearchesVerbatimContent();
    void testCaseSensitiveOption();
    void testScanTextOptions_data();
    void testScanTextOptions();
    void testInvalidRegexIsErrorState();
    void testZeroLengthMatchesSkipped();
    void testInactiveOrEmptyQueryYieldsNothing();
    void testEmojiContentIsSearchable();

    // Navigation and seeding
    void testDocumentOrderAndCurrentNumber();
    void testNextPreviousWrap();
    void testSeedToFirstMatchAfterCursor();
    void testSeedWrapsToFirstWhenPastLastMatch();
    void testMatchesForBlockMarksCurrent();
    void testCurrentMatchInfo();

    // In-selection domain
    void testBlockDomainFilters();
    void testTextDomainEdgeFiltering();
    void testDomainSurvivesMoves();
    void testBlockDomainPrunesOnRemoval();
    void testTextDomainClearsWhenEdgeRemoved();

    // Recompute on model changes
    void testRecomputeOnContentChange();
    void testRecomputeOnStructureChange();

    // Replace
    void testReplaceCurrentAdvancesAndIsOneUndoStep();
    void testReplaceFullyCoveredSpanFollowsCutContract();
    void testReplacePartialSpanKeepsMarkers();
    void testReplaceAcrossMarkerBoundary();
    void testReplaceInCodeBlockSplicesVerbatim();
    void testReplacementTextIsMarkdown();
    void testReplaceAllRightToLeftWithinBlock();
    void testReplaceAllAcrossBlocksIsOneUndoStep();
    void testReplaceAllRespectsDomain();
    void testCaptureGroupSubstitution_data();
    void testCaptureGroupSubstitution();
    void testRegexReplaceWithCaptures();
    void testPreserveCase_data();
    void testPreserveCase();
    void testPreserveCaseEndToEnd();
    void testPreviewMatchesApply();
    void testPreviewLineContext();

    // Revision contract
    void testRevisionBumpsExactlyOnChange();

    // Display-semantics changes (code <-> formatted text)
    void testCodeToParagraphRecomputesMatches();
    void testParagraphToCodeReplacesTheRightSpan();
    // In-selection find must not be O(matches x blocks)
    void testDenseInSelectionFindStaysLinear();
    // Model lifetime
    void testModelDestroyedWithRecomputePending();

private:
    // Blocks: 0 formatted paragraph, 1 plain paragraph, 2 bullet,
    // 3 divider, 4 multi-line code block, 5 multi-line quote.
    BlockModel *m_model = nullptr;
    UndoStack *m_stack = nullptr;
    DocumentSearch *m_search = nullptr;
};

void TestDocumentSearch::init()
{
    m_stack = new UndoStack(this);
    m_model = new BlockModel(this);
    m_model->setUndoStack(m_stack);
    m_model->insertBlock(0, Block::Paragraph, "This is **bold** text");
    m_model->insertBlock(1, Block::Paragraph, "second Fox block fox FOX");
    m_model->insertBlock(2, Block::BulletList, "fox item");
    m_model->insertBlock(3, Block::Divider, "");
    m_model->insertBlock(4, Block::CodeBlock, "let fox = **not markdown**\nfox()");
    m_model->insertBlock(5, Block::Quote, "quote line one\nfox two");
    m_stack->clear();

    m_search = new DocumentSearch(this);
    m_search->setModel(m_model);
    m_search->setActive(true);
}

void TestDocumentSearch::cleanup()
{
    delete m_search;
    m_search = nullptr;
    delete m_model;
    m_model = nullptr;
    delete m_stack;
    m_stack = nullptr;
}

// ---- matching ----

void TestDocumentSearch::testPlainMatchAcrossBlocks()
{
    m_search->setQuery("fox");
    // Case-insensitive default: block 1 has Fox/fox/FOX, block 2 one,
    // block 4 (code) two, block 5 one.
    QCOMPARE(m_search->matchCount(), 7);
    QCOMPARE(m_search->matchesForBlock(0).size(), 0);
    QCOMPARE(m_search->matchesForBlock(1).size(), 3);
    QCOMPARE(m_search->matchesForBlock(2).size(), 1);
    QCOMPARE(m_search->matchesForBlock(3).size(), 0);
    QCOMPARE(m_search->matchesForBlock(4).size(), 2);
    QCOMPARE(m_search->matchesForBlock(5).size(), 1);

    const QVariantMap first = m_search->matchesForBlock(1).first().toMap();
    QCOMPARE(first.value("start").toInt(), 7);
    QCOMPARE(first.value("length").toInt(), 3);
}

void TestDocumentSearch::testMatchSpansMarkerBoundary()
{
    // Display text of block 0 is "This is bold text": the query exists
    // only there, never in the raw markdown.
    m_search->setQuery("is bold");
    QCOMPARE(m_search->matchCount(), 1);
    const QVariantMap m = m_search->matchesForBlock(0).first().toMap();
    QCOMPARE(m.value("start").toInt(), 5);
    QCOMPARE(m.value("length").toInt(), 7);
}

void TestDocumentSearch::testMarkersAreNotSearchable()
{
    m_search->setQuery("**");
    // Block 0's asterisks are markers (stripped from display text); the
    // code block's are verbatim content and DO match.
    QCOMPARE(m_search->matchesForBlock(0).size(), 0);
    QCOMPARE(m_search->matchesForBlock(4).size(), 2);
    QCOMPARE(m_search->matchCount(), 2);
}

void TestDocumentSearch::testCodeBlockSearchesVerbatimContent()
{
    m_search->setQuery("not markdown");
    QCOMPARE(m_search->matchCount(), 1);
    const QVariantMap m = m_search->matchesForBlock(4).first().toMap();
    QCOMPARE(m.value("start").toInt(), 12); // after "let fox = **"
}

void TestDocumentSearch::testCaseSensitiveOption()
{
    m_search->setQuery("fox");
    m_search->setCaseSensitive(true);
    // "Fox" and "FOX" in block 1 drop out.
    QCOMPARE(m_search->matchCount(), 5);
    QCOMPARE(m_search->matchesForBlock(1).size(), 1);
}

void TestDocumentSearch::testScanTextOptions_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("query");
    QTest::addColumn<bool>("caseSensitive");
    QTest::addColumn<bool>("wholeWord");
    QTest::addColumn<bool>("useRegex");
    QTest::addColumn<QList<int>>("starts");

    QTest::newRow("plain insensitive")
        << "Fox fox FOX" << "fox" << false << false << false
        << QList<int>{0, 4, 8};
    QTest::newRow("plain sensitive")
        << "Fox fox FOX" << "fox" << true << false << false
        << QList<int>{4};
    QTest::newRow("whole word rejects substrings")
        << "fox foxes 'fox' fox_y" << "fox" << false << true << false
        << QList<int>{0, 11};
    QTest::newRow("whole word with non-word query edges")
        << "a+b c+d" << "+" << false << true << false
        << QList<int>{1, 5};
    QTest::newRow("non-overlapping scan")
        << "aaaa" << "aa" << false << false << false
        << QList<int>{0, 2};
    QTest::newRow("regex")
        << "fax fix fox" << "f.x" << false << false << true
        << QList<int>{0, 4, 8};
    QTest::newRow("regex case sensitive")
        << "Fox fox" << "f.x" << true << false << true
        << QList<int>{4};
    QTest::newRow("regex composed with whole word")
        << "fix prefix fixes" << "f.x" << false << true << true
        << QList<int>{0};
    QTest::newRow("regex alternation whole word")
        << "cat cats dog" << "cat|dog" << false << true << true
        << QList<int>{0, 9};
    QTest::newRow("dot does not cross lines")
        << "a\nb axb" << "a.b" << false << false << true
        << QList<int>{4};
}

void TestDocumentSearch::testScanTextOptions()
{
    QFETCH(QString, text);
    QFETCH(QString, query);
    QFETCH(bool, caseSensitive);
    QFETCH(bool, wholeWord);
    QFETCH(bool, useRegex);
    QFETCH(QList<int>, starts);

    bool error = false;
    const auto matches = DocumentSearch::scanText(text, query, caseSensitive,
                                                  wholeWord, useRegex, &error);
    QVERIFY(!error);
    QList<int> actual;
    for (const auto &m : matches)
        actual.append(m.start);
    QCOMPARE(actual, starts);
}

void TestDocumentSearch::testInvalidRegexIsErrorState()
{
    m_search->setUseRegex(true);
    m_search->setQuery("(unclosed");
    QVERIFY(m_search->patternError());
    QCOMPARE(m_search->matchCount(), 0);

    // Recovers the moment the pattern compiles.
    m_search->setQuery("(fox)");
    QVERIFY(!m_search->patternError());
    QCOMPARE(m_search->matchCount(), 7);
}

void TestDocumentSearch::testZeroLengthMatchesSkipped()
{
    bool error = false;
    auto matches = DocumentSearch::scanText("abc", "x*", false, false, true,
                                            &error);
    QVERIFY(!error);
    QVERIFY(matches.isEmpty());

    matches = DocumentSearch::scanText("aaab", "a*", false, false, true);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().start, 0);
    QCOMPARE(matches.first().length, 3);
}

void TestDocumentSearch::testEmojiContentIsSearchable()
{
    // Emoji flow through search as opaque text: both a query containing an
    // emoji and plain text next to one hit, with UTF-16 offsets that map
    // back into the display text.
    m_model->insertBlock(6, Block::Paragraph,
                         QStringLiteral("launch 🚀 checklist"));

    m_search->setQuery(QStringLiteral("🚀"));
    QCOMPARE(m_search->matchCount(), 1);
    const QVariantMap hit = m_search->matchesForBlock(6).first().toMap();
    QCOMPARE(hit.value("start").toInt(), 7);
    QCOMPARE(hit.value("length").toInt(), 2);  // surrogate pair

    m_search->setQuery(QStringLiteral("🚀 check"));
    QCOMPARE(m_search->matchCount(), 1);
}

void TestDocumentSearch::testInactiveOrEmptyQueryYieldsNothing()
{
    m_search->setQuery("fox");
    QCOMPARE(m_search->matchCount(), 7);

    m_search->setQuery("");
    QCOMPARE(m_search->matchCount(), 0);
    QCOMPARE(m_search->currentNumber(), 0);

    m_search->setQuery("fox");
    m_search->setActive(false);
    QCOMPARE(m_search->matchCount(), 0);

    m_search->setActive(true);
    QCOMPARE(m_search->matchCount(), 7);
}

// ---- navigation and seeding ----

void TestDocumentSearch::testDocumentOrderAndCurrentNumber()
{
    m_search->setQuery("fox");
    QCOMPARE(m_search->matchCount(), 7);
    // No cursor seed set: current starts at the document's first match.
    QCOMPARE(m_search->currentNumber(), 1);
    const QVariantMap info = m_search->currentMatchInfo();
    QCOMPARE(info.value("blockIndex").toInt(), 1);
    QCOMPARE(info.value("start").toInt(), 7);
}

void TestDocumentSearch::testNextPreviousWrap()
{
    m_search->setQuery("fox");
    QCOMPARE(m_search->currentNumber(), 1);
    for (int i = 0; i < 6; ++i)
        m_search->next();
    QCOMPARE(m_search->currentNumber(), 7);
    m_search->next();
    QCOMPARE(m_search->currentNumber(), 1); // wraps forward
    m_search->previous();
    QCOMPARE(m_search->currentNumber(), 7); // wraps backward
}

void TestDocumentSearch::testSeedToFirstMatchAfterCursor()
{
    m_search->setActiveCursor(4, 0);
    m_search->setQuery("fox");
    // First match at/after block 4 position 0: the code block's first
    // "fox" — flat index 4 (three in block 1, one in block 2 before it).
    QCOMPARE(m_search->currentNumber(), 5);
    QCOMPARE(m_search->currentMatchInfo().value("blockIndex").toInt(), 4);
}

void TestDocumentSearch::testSeedWrapsToFirstWhenPastLastMatch()
{
    m_search->setActiveCursor(5, 20); // beyond "fox" at display 15
    m_search->setQuery("fox");
    QCOMPARE(m_search->currentNumber(), 1);
    QCOMPARE(m_search->currentMatchInfo().value("blockIndex").toInt(), 1);
}

void TestDocumentSearch::testMatchesForBlockMarksCurrent()
{
    m_search->setQuery("fox");
    m_search->next(); // second match: block 1, start 17
    const QVariantList list = m_search->matchesForBlock(1);
    QCOMPARE(list.size(), 3);
    QCOMPARE(list.at(0).toMap().value("current").toBool(), false);
    QCOMPARE(list.at(1).toMap().value("current").toBool(), true);
    QCOMPARE(list.at(2).toMap().value("current").toBool(), false);
}

void TestDocumentSearch::testCurrentMatchInfo()
{
    QCOMPARE(m_search->currentMatchInfo().value("found").toBool(), false);
    m_search->setQuery("quote");
    const QVariantMap info = m_search->currentMatchInfo();
    QCOMPARE(info.value("found").toBool(), true);
    QCOMPARE(info.value("blockIndex").toInt(), 5);
    QCOMPARE(info.value("start").toInt(), 0);
    QCOMPARE(info.value("length").toInt(), 5);
    QCOMPARE(info.value("mdStart").toInt(), 0);

    // mdStart compensates for hidden markers: "bold" sits at display 8
    // but markdown 10 in "This is **bold** text".
    m_search->setQuery("bold");
    const QVariantMap formatted = m_search->currentMatchInfo();
    QCOMPARE(formatted.value("blockIndex").toInt(), 0);
    QCOMPARE(formatted.value("start").toInt(), 8);
    QCOMPARE(formatted.value("mdStart").toInt(), 10);
}

// ---- in-selection domain ----

void TestDocumentSearch::testBlockDomainFilters()
{
    m_search->setQuery("fox");
    m_search->setBlockDomain({1, 2});
    QVERIFY(m_search->hasDomain());
    // Domain armed but toggle off: everything still matches.
    QCOMPARE(m_search->matchCount(), 7);

    m_search->setInSelectionOnly(true);
    QCOMPARE(m_search->matchCount(), 4);
    QCOMPARE(m_search->matchesForBlock(4).size(), 0);

    m_search->setInSelectionOnly(false);
    QCOMPARE(m_search->matchCount(), 7);

    m_search->clearDomain();
    QVERIFY(!m_search->hasDomain());
}

void TestDocumentSearch::testTextDomainEdgeFiltering()
{
    m_search->setQuery("fox");
    m_search->setInSelectionOnly(true);
    // Markdown positions: from block 1 md 10 (after "Fox") to block 4
    // md 10 (before the second code "fox" but after the first at 4..7).
    m_search->setTextDomain(1, 10, 4, 10);
    QVERIFY(m_search->hasDomain());
    // Block 1: matches at 17 and 21 remain (7 is before the edge);
    // block 2: 1; block 4: only the match at 4 (ends 7 <= 10).
    QCOMPARE(m_search->matchCount(), 4);
    QCOMPARE(m_search->matchesForBlock(1).size(), 2);
    QCOMPARE(m_search->matchesForBlock(2).size(), 1);
    QCOMPARE(m_search->matchesForBlock(4).size(), 1);
    QCOMPARE(m_search->matchesForBlock(5).size(), 0);
}

void TestDocumentSearch::testDomainSurvivesMoves()
{
    m_search->setQuery("fox");
    m_search->setInSelectionOnly(true);
    m_search->setBlockDomain({2}); // "fox item"
    QCOMPARE(m_search->matchCount(), 1);

    m_model->moveBlock(2, 5); // bullet moves to the end
    m_search->recomputeNow();
    QCOMPARE(m_search->matchCount(), 1);
    QCOMPARE(m_search->currentMatchInfo().value("blockIndex").toInt(), 5);
}

void TestDocumentSearch::testBlockDomainPrunesOnRemoval()
{
    m_search->setQuery("fox");
    m_search->setInSelectionOnly(true);
    m_search->setBlockDomain({1, 2});
    QCOMPARE(m_search->matchCount(), 4);

    m_model->removeBlock(1);
    m_search->recomputeNow();
    QVERIFY(m_search->hasDomain()); // block 2's id survives
    QCOMPARE(m_search->matchCount(), 1);

    m_model->removeBlock(1); // the former block 2
    m_search->recomputeNow();
    QVERIFY(!m_search->hasDomain()); // all ids gone: domain dissolves
    QCOMPARE(m_search->matchCount(), 3); // whole document again
}

void TestDocumentSearch::testTextDomainClearsWhenEdgeRemoved()
{
    m_search->setQuery("fox");
    m_search->setInSelectionOnly(true);
    m_search->setTextDomain(1, 0, 2, 8);
    QCOMPARE(m_search->matchCount(), 4);

    m_model->removeBlock(2);
    m_search->recomputeNow();
    QVERIFY(!m_search->hasDomain());
    QCOMPARE(m_search->matchCount(), 6); // 7 minus the removed block's match
}

// ---- recompute on model changes ----

void TestDocumentSearch::testRecomputeOnContentChange()
{
    m_search->setQuery("fox");
    QCOMPARE(m_search->matchCount(), 7);
    m_model->updateContent(2, "cat item");
    // The model signal recomputes through a queued call.
    QTRY_COMPARE(m_search->matchCount(), 6);
}

void TestDocumentSearch::testRecomputeOnStructureChange()
{
    m_search->setQuery("fox");
    m_model->insertBlock(0, Block::Paragraph, "fox fox");
    QTRY_COMPARE(m_search->matchCount(), 9);
    m_model->removeBlock(0);
    QTRY_COMPARE(m_search->matchCount(), 7);
    m_model->moveBlock(1, 5);
    QTRY_COMPARE(m_search->currentMatchInfo().value("blockIndex").toInt(), 1);
}

// ---- replace ----

void TestDocumentSearch::testReplaceCurrentAdvancesAndIsOneUndoStep()
{
    m_search->setQuery("fox");
    const int before = m_stack->count();
    QVERIFY(m_search->replaceCurrent("cat"));
    QCOMPARE(m_model->getContent(1), QString("second cat block fox FOX"));
    QCOMPARE(m_search->matchCount(), 6);
    // Advanced to the next remaining match in the same block.
    const QVariantMap info = m_search->currentMatchInfo();
    QCOMPARE(info.value("blockIndex").toInt(), 1);
    QCOMPARE(info.value("start").toInt(), 17);

    QCOMPARE(m_stack->count(), before + 1);
    m_stack->undo();
    QCOMPARE(m_model->getContent(1), QString("second Fox block fox FOX"));
}

void TestDocumentSearch::testReplaceFullyCoveredSpanFollowsCutContract()
{
    m_model->updateContent(0, "x **bold** y");
    m_search->setQuery("bold");
    m_search->recomputeNow();
    QVERIFY(m_search->replaceCurrent("brave"));
    // The formatting belonged to the replaced text.
    QCOMPARE(m_model->getContent(0), QString("x brave y"));
}

void TestDocumentSearch::testReplacePartialSpanKeepsMarkers()
{
    m_model->updateContent(0, "x **bold** y");
    m_search->setQuery("bo");
    m_search->recomputeNow();
    QVERIFY(m_search->replaceCurrent("zz"));
    QCOMPARE(m_model->getContent(0), QString("x **zzld** y"));
}

void TestDocumentSearch::testReplaceAcrossMarkerBoundary()
{
    m_search->setQuery("is bo");
    QCOMPARE(m_search->matchCount(), 1);
    QVERIFY(m_search->replaceCurrent("X"));
    // Plain part and partial span content removed, remainder keeps its
    // formatting.
    QCOMPARE(m_model->getContent(0), QString("This X**ld** text"));
}

void TestDocumentSearch::testReplaceInCodeBlockSplicesVerbatim()
{
    m_search->setQuery("not markdown");
    QVERIFY(m_search->replaceCurrent("still code"));
    QCOMPARE(m_model->getContent(4),
             QString("let fox = **still code**\nfox()"));
}

void TestDocumentSearch::testReplacementTextIsMarkdown()
{
    m_search->setQuery("second");
    QVERIFY(m_search->replaceCurrent("**loud**"));
    QCOMPARE(m_model->getContent(1), QString("**loud** Fox block fox FOX"));
}

void TestDocumentSearch::testReplaceAllRightToLeftWithinBlock()
{
    m_model->updateContent(1, "fox fox fox");
    m_search->setQuery("fox");
    m_search->recomputeNow();
    const int replaced = m_search->replaceAll("foxy");
    // Length-changing replacements must not corrupt earlier positions.
    QCOMPARE(m_model->getContent(1), QString("foxy foxy foxy"));
    QCOMPARE(replaced, 7);
    QCOMPARE(m_search->matchCount(), 7); // "foxy" still contains "fox"
}

void TestDocumentSearch::testReplaceAllAcrossBlocksIsOneUndoStep()
{
    m_search->setQuery("fox");
    m_search->setCaseSensitive(true);
    QCOMPARE(m_search->matchCount(), 5);
    const int before = m_stack->count();
    QCOMPARE(m_search->replaceAll("cat"), 5);
    QCOMPARE(m_model->getContent(1), QString("second Fox block cat FOX"));
    QCOMPARE(m_model->getContent(2), QString("cat item"));
    QCOMPARE(m_model->getContent(4), QString("let cat = **not markdown**\ncat()"));
    QCOMPARE(m_model->getContent(5), QString("quote line one\ncat two"));
    QCOMPARE(m_search->matchCount(), 0);

    QCOMPARE(m_stack->count(), before + 1);
    m_stack->undo();
    QCOMPARE(m_model->getContent(1), QString("second Fox block fox FOX"));
    QCOMPARE(m_model->getContent(2), QString("fox item"));
    QCOMPARE(m_model->getContent(4), QString("let fox = **not markdown**\nfox()"));
    QCOMPARE(m_model->getContent(5), QString("quote line one\nfox two"));
}

void TestDocumentSearch::testReplaceAllRespectsDomain()
{
    m_search->setQuery("fox");
    m_search->setInSelectionOnly(true);
    m_search->setBlockDomain({2});
    QCOMPARE(m_search->replaceAll("cat"), 1);
    QCOMPARE(m_model->getContent(2), QString("cat item"));
    QCOMPARE(m_model->getContent(1), QString("second Fox block fox FOX"));
}

void TestDocumentSearch::testCaptureGroupSubstitution_data()
{
    QTest::addColumn<QString>("replacement");
    QTest::addColumn<QStringList>("captures");
    QTest::addColumn<QString>("expected");

    QTest::newRow("group") << "$1!" << QStringList{"ab", "a"} << "a!";
    QTest::newRow("two groups reordered")
        << "$2$1" << QStringList{"ab", "a", "b"} << "ba";
    QTest::newRow("whole match") << "[$&]" << QStringList{"ab"} << "[ab]";
    QTest::newRow("literal dollar") << "$$5" << QStringList{"ab"} << "$5";
    QTest::newRow("absent group empty") << "$3" << QStringList{"ab", "a"} << "";
    QTest::newRow("trailing dollar literal") << "x$" << QStringList{"ab"} << "x$";
    QTest::newRow("dollar zero literal") << "$0" << QStringList{"ab"} << "$0";
}

void TestDocumentSearch::testCaptureGroupSubstitution()
{
    QFETCH(QString, replacement);
    QFETCH(QStringList, captures);
    QFETCH(QString, expected);
    QCOMPARE(DocumentSearch::substituteCaptures(replacement, captures), expected);
}

void TestDocumentSearch::testRegexReplaceWithCaptures()
{
    m_model->updateContent(1, "name: value");
    m_search->setUseRegex(true);
    m_search->setQuery("(\\w+): (\\w+)");
    m_search->recomputeNow();
    QVERIFY(m_search->replaceCurrent("$2 = $1"));
    QCOMPARE(m_model->getContent(1), QString("value = name"));
}

void TestDocumentSearch::testPreserveCase_data()
{
    QTest::addColumn<QString>("replacement");
    QTest::addColumn<QString>("matched");
    QTest::addColumn<QString>("expected");

    QTest::newRow("upper") << "cat" << "FOX" << "CAT";
    QTest::newRow("lower") << "Cat" << "fox" << "cat";
    QTest::newRow("capitalized") << "cAT" << "Fox" << "Cat";
    QTest::newRow("mixed as typed") << "cat" << "fOx" << "cat";
    QTest::newRow("no letters as typed") << "Cat" << "123" << "Cat";
    QTest::newRow("single upper letter capitalizes") << "cat" << "F" << "Cat";
}

void TestDocumentSearch::testPreserveCase()
{
    QFETCH(QString, replacement);
    QFETCH(QString, matched);
    QFETCH(QString, expected);
    QCOMPARE(DocumentSearch::applyPreserveCase(replacement, matched), expected);
}

void TestDocumentSearch::testPreserveCaseEndToEnd()
{
    m_search->setQuery("fox");
    m_search->setPreserveCase(true);
    m_search->setBlockDomain({1});
    m_search->setInSelectionOnly(true);
    QCOMPARE(m_search->replaceAll("cat"), 3);
    QCOMPARE(m_model->getContent(1), QString("second Cat block cat CAT"));
}

void TestDocumentSearch::testPreviewMatchesApply()
{
    m_search->setQuery("fox");
    const QVariantList rows = m_search->previewReplacements("cat");
    QCOMPARE(rows.size(), 7);
    QCOMPARE(m_search->replaceAll("cat"), 7);

    const QVariantMap row = rows.first().toMap();
    QCOMPARE(row.value("blockIndex").toInt(), 1);
    QCOMPARE(row.value("prefix").toString(), QString("second "));
    QCOMPARE(row.value("matched").toString(), QString("Fox"));
    QCOMPARE(row.value("replacement").toString(), QString("cat"));
    QCOMPARE(row.value("suffix").toString(), QString(" block fox FOX"));
}

void TestDocumentSearch::testPreviewLineContext()
{
    // Context clips to the match's line and to 30 characters with an
    // ellipsis marker.
    m_model->updateContent(5,
        QString("a").repeated(40) + " fox " + QString("b").repeated(40)
        + "\nsecond line");
    m_search->setQuery("fox");
    m_search->recomputeNow();
    const QVariantMap row = m_search->previewReplacements("cat").last().toMap();
    QCOMPARE(row.value("blockIndex").toInt(), 5);
    const QString prefix = row.value("prefix").toString();
    const QString suffix = row.value("suffix").toString();
    QVERIFY(prefix.startsWith(QStringLiteral("…")));
    QVERIFY(suffix.endsWith(QStringLiteral("…")));
    QVERIFY(!prefix.contains('\n'));
    QVERIFY(!suffix.contains('\n'));
    QCOMPARE(prefix.length(), 31); // 30 chars + ellipsis
    QCOMPARE(suffix.length(), 31);
}

// ---- revision contract ----

void TestDocumentSearch::testRevisionBumpsExactlyOnChange()
{
    QSignalSpy spy(m_search, &DocumentSearch::revisionChanged);

    m_search->setQuery("fox");
    QCOMPARE(spy.count(), 1);

    m_search->setQuery("fox"); // no-op
    QCOMPARE(spy.count(), 1);

    m_search->next();
    QCOMPARE(spy.count(), 2);

    m_search->setCaseSensitive(false); // already false: no-op
    QCOMPARE(spy.count(), 2);

    // Two queries with identical (empty) results and no current match:
    // nothing observable changed between them.
    m_search->setQuery("zzz-one");
    const int afterFirstMiss = spy.count();
    m_search->setQuery("zzz-two");
    QCOMPARE(spy.count(), afterFirstMiss);

    // recomputeNow with nothing changed: no bump.
    m_search->setQuery("fox");
    const int base = spy.count();
    m_search->recomputeNow();
    QCOMPARE(spy.count(), base);
}

// ---- display-semantics changes ----

// Matches are computed over DISPLAY text, so a block flipping between
// verbatim code and formatted text changes both what matches and what a
// match's coordinates mean — while its content stays byte-identical.
// Recompute used to listen for ContentRole alone, so nothing fired and the
// highlights stayed on the old coordinates.
void TestDocumentSearch::testCodeToParagraphRecomputesMatches()
{
    auto *model = new BlockModel(this);
    model->insertBlock(0, Block::CodeBlock, QStringLiteral("a*b*c"));
    auto *search = new DocumentSearch(this);
    search->setModel(model);
    search->setActive(true);
    search->setQuery(QStringLiteral("b"));

    // Verbatim: the markers are ordinary characters, so "b" sits at 2.
    QCOMPARE(search->matchCount(), 1);
    QCOMPARE(search->currentMatchInfo().value(QStringLiteral("start")).toInt(), 2);

    model->updateType(0, Block::Paragraph);
    QCoreApplication::processEvents();

    // Formatted: *b* is italic, so the display text is "abc" and "b" is at 1.
    QCOMPARE(search->matchCount(), 1);
    QCOMPARE(search->currentMatchInfo().value(QStringLiteral("start")).toInt(), 1);

    delete search;
    delete model;
}

// The dangerous half of the same defect: replace applies the CACHED display
// coordinates to the block's markdown. Stale coordinates from the previous
// display semantics rewrite the wrong span.
void TestDocumentSearch::testParagraphToCodeReplacesTheRightSpan()
{
    auto *model = new BlockModel(this);
    model->insertBlock(0, Block::Paragraph, QStringLiteral("a*b*c"));
    auto *search = new DocumentSearch(this);
    search->setModel(model);
    search->setActive(true);
    search->setQuery(QStringLiteral("b"));
    QCOMPARE(search->currentMatchInfo().value(QStringLiteral("start")).toInt(), 1);

    model->updateType(0, Block::CodeBlock);
    QCoreApplication::processEvents();

    QVERIFY(search->replaceCurrent(QStringLiteral("Z")));
    // Verbatim splice at the recomputed position 2. With the stale position 1
    // this produced "aZb*c" — a marker eaten and the match left behind.
    QCOMPARE(model->getContent(0), QStringLiteral("a*Z*c"));

    delete search;
    delete model;
}

// Every candidate match used to resolve both selection endpoints by scanning
// the model for their block ids and re-mapping their markdown positions into
// display coordinates. On a dense match set that is O(matches x blocks) of
// string comparison plus a markdown mapping per match. Endpoints are now
// resolved once per recompute.
void TestDocumentSearch::testDenseInSelectionFindStaysLinear()
{
    const int blockCount = 2000;
    auto *model = new BlockModel(this);
    QList<Block::State> states;
    states.reserve(blockCount);
    for (int i = 0; i < blockCount; ++i) {
        Block::State state;
        state.type = Block::Paragraph;
        // 40 matches per block, with markers so the mapping is not trivial.
        state.content = QStringLiteral("*x* ").repeated(40);
        states.append(state);
    }
    model->replaceAllBlocksInternal(states);

    auto *search = new DocumentSearch(this);
    search->setModel(model);
    search->setActive(true);
    search->setInSelectionOnly(true);
    search->setTextDomain(0, 0, blockCount - 1,
                          static_cast<int>(model->getContent(blockCount - 1).length()));
    search->setQuery(QStringLiteral("x"));

    QElapsedTimer timer;
    timer.start();
    search->recomputeNow();
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(search->matchCount(), blockCount * 40);
    QVERIFY2(elapsed < 3000,
             qPrintable(QStringLiteral("recomputing %1 in-selection matches "
                                       "over %2 blocks took %3 ms")
                            .arg(search->matchCount())
                            .arg(blockCount)
                            .arg(elapsed)));

    delete search;
    delete model;
}

// QObject auto-disconnection unhooks the signals but leaves the raw pointer
// dangling, and a queued recompute scheduled before the model died still runs
// afterwards.
void TestDocumentSearch::testModelDestroyedWithRecomputePending()
{
    auto *model = new BlockModel(nullptr);
    model->insertBlock(0, Block::Paragraph, QStringLiteral("fox one"));
    model->insertBlock(1, Block::Paragraph, QStringLiteral("fox two"));

    auto *search = new DocumentSearch(this);
    search->setModel(model);
    search->setActive(true);
    search->setQuery(QStringLiteral("fox"));
    search->setBlockDomain(QVariantList{0, 1});
    search->setInSelectionOnly(true);
    QCOMPARE(search->matchCount(), 2);

    // Schedule the compressed recompute, then destroy the model before the
    // event loop gets to it.
    model->updateContent(0, QStringLiteral("fox three"));
    delete model;
    QCoreApplication::processEvents();

    QVERIFY(search->model() == nullptr);
    QCOMPARE(search->matchCount(), 0);
    QVERIFY(!search->hasDomain());
    // Still usable: the queries must answer instead of dereferencing freed
    // memory.
    QVERIFY(search->matchesForBlock(0).isEmpty());
    search->recomputeNow();
    QCOMPARE(search->matchCount(), 0);

    delete search;
}

QTEST_MAIN(TestDocumentSearch)
#include "test_documentsearch.moc"
