// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QSignalSpy>
#include <QtMath>

#include <QTemporaryDir>

#include "blockeditorengine.h"
#include "mathrenderer.h"
#include "documentoutline.h"
#include "notecollection.h"
#include "blockmodel.h"
#include "block.h"

using FormatRange = BlockEditorEngine::FormatRange;

// Shorthand builders for expected ranges
static FormatRange marker(int start, int len)
{
    return {start, len, FormatRange::Marker};
}
static FormatRange bold(int start, int len)
{
    return {start, len, FormatRange::Bold};
}
static FormatRange italic(int start, int len)
{
    return {start, len, FormatRange::Italic};
}
static FormatRange boldItalic(int start, int len)
{
    return {start, len, FormatRange::BoldItalic};
}
static FormatRange codeRange(int start, int len)
{
    return {start, len, FormatRange::Code};
}
static FormatRange colorRange(int start, int len, const QString &value)
{
    return {start, len, FormatRange::Color, value};
}
// A Link range carries its per-instance target url (phase11 decision 3), so
// the highlighter can distinguish resolved from unresolved #slug links.
static FormatRange linkRange(int start, int len, const QString &url)
{
    return {start, len, FormatRange::Link, QString(), url};
}

using HighlightRange = BlockEditorEngine::HighlightRange;

static HighlightRange hr(int start, int len, bool current = false)
{
    return {start, len, current};
}

// The char format the layout actually renders at a position (the ranges
// QSyntaxHighlighter produced are non-overlapping).
static QTextCharFormat layoutFormatAt(QTextDocument &doc, int pos)
{
    const auto formats = doc.firstBlock().layout()->formats();
    for (const auto &fr : formats) {
        if (pos >= fr.start && pos < fr.start + fr.length)
            return fr.format;
    }
    return QTextCharFormat();
}

// Reveal-state evaluation is deferred to the event loop (the engine never
// mutates the document inside QQuickTextEdit's signal dispatch); tests
// drain the queue after cursor/focus/typing changes.
static void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

// Reference markdown used across mapping tests:
//   "This is **bold** and *it* end"
//    0123456789...
//   bold span: md [8,16), content [10,14); italic span: md [21,25)
//   display: "This is bold and it end" (bold 8..12, it 17..19)
static const QString M = QStringLiteral("This is **bold** and *it* end");

class TestBlockEditorEngine : public QObject
{
    Q_OBJECT

private slots:
    // --- displayText / documentText ---
    void testDisplayText_data();
    void testDisplayText();
    void testDocumentTextStates();

    // --- position mapping tables ---
    void testDocumentToMarkdownHidden_data();
    void testDocumentToMarkdownHidden();
    void testDocumentToMarkdownRevealed();
    void testMarkdownToDocumentHidden_data();
    void testMarkdownToDocumentHidden();
    void testMappingRoundTripThroughContent();

    // --- reveal decision (inclusive boundaries) ---
    void testSpanToReveal_data();
    void testSpanToReveal();

    // --- format ranges per state (ports the Stage A corpus) ---
    void testFormatRangesHidden();
    void testFormatRangesRevealed();
    void testFormatRangesMultipleAndAdjacent();
    void testFormatRangesNestedAndDegenerate();

    // --- document-edit mapping (applyDocumentEdit) ---
    void testEditTypeInPlainText();
    void testEditTypeInsideHiddenSpan();
    void testEditTypeInsideRevealedSpan();
    void testEditDeleteWholeHiddenWordRemovesMarkers();
    void testEditPartialDeleteKeepsMarkers();
    void testEditStraddlingDelete();
    void testEditReplaceSelection();
    void testEditDeleteRevealedMarkerChar();
    void testEditInsertIntoEmptyMarkdown();

    // --- engine attached to a document ---
    void testSetMarkdownFillsDocumentWithDisplayText();
    void testModelRebuildEmitsNoUserEdit();
    void testInitialBindCoalescesRebuilds();
    void testUserEditEmitsMarkdownEdited();
    void testSetSameMarkdownIsQuiet();
    void testHighlighterStylesHiddenContent();
    void testDocumentUndoDisabled();
    void testReattachToNewDocument();
    void testInactiveCursorNeverReveals();
    void testRevealWalkthrough_2_2_3();
    void testRevealTransitionEmitsNoUserEdit();
    void testTypingNewSpanStaysRawUntilCursorLeaves();
    void testDeletingMarkerCharDestroysSpan();
    void testFocusLossHidesRevealedSpan();
    void testSelectionRevealsAllTouchedSpans();
    void testPartialSelectionRevealsTouchedSpan();
    void testModelRebuildUsesMinimalDiff();

    // --- copy/cut markdown mapping ---
    void testMarkdownForRangeCopy();
    void testMarkdownForRangeRevealedState();
    void testCutRangeResult();

    // --- New symmetric types (~~ / == / ++ / `) ---
    void testSymmetricTypeLifecycle_data();
    void testSymmetricTypeLifecycle();
    void testCodeSpanContentStaysVerbatim();

    // --- Nested spans ---
    void testNestedMappingRoundTrip();
    void testNestedEditMapping();
    void testNestedCopyAndCut();
    void testNestedRevealWalkthrough();

    // --- Links and autolinks ---
    void testLinkStatesAndMapping();
    void testColorSpanStatesAndMapping();
    void testLinkEditCopyCut();
    void testLinkRevealWalkthrough();
    void testAutolinkBehavior();
    void testLinkAtDocumentPosition();
    void testInternalLinkUnresolvedStyle();
    void testWikiLinkRangesAndUnresolvedStyle();
    void testInlineMathReserveAndReveal();
    void testInlineMathReservationUsesRenderedWidth();
    void testInlineMathTallFormulaReservesLineHeight();

    // --- Verbatim mode (code blocks) ---
    void testVerbatimDocumentIsMarkdown();
    void testVerbatimNeverReveals();
    void testVerbatimEditsAndMapping();

    // --- search-match highlighting ---
    void testSearchHighlightRangesPlain();
    void testSearchHighlightRangesHiddenSpans();
    void testSearchHighlightRangesRevealedSpan();
    void testSearchHighlightRangesVerbatim();
    void testSearchHighlightRangesClampStale();
    void testSearchMatchesPaintAndMergeFormats();
    void testSearchHighlightFollowsRevealTransition();

    // --- Phase 9: sup/sub rendering, caret format flags ---
    void testSupSubVerticalAlignment();
    void testFormatFlagsAtDocumentPosition();

    // --- Math-entry gates ---
    void testMathSpanRangeIn();
    void testMathSpanRangeAtDocument();
    void testShouldAutoPairDollarRules();
    void testShouldAutoPairDollarVerbatim();
};

void TestBlockEditorEngine::testDisplayText_data()
{
    QTest::addColumn<QString>("markdown");
    QTest::addColumn<QString>("display");

    QTest::newRow("empty") << "" << "";
    QTest::newRow("plain") << "plain text" << "plain text";
    QTest::newRow("bold") << "**b**" << "b";
    QTest::newRow("italic") << "*i*" << "i";
    QTest::newRow("bolditalic") << "***x***" << "x";
    QTest::newRow("sentence") << "Hello **world** end" << "Hello world end";
    QTest::newRow("multiple") << "a **b** c *d* e" << "a b c d e";
    QTest::newRow("adjacent") << "**a***b*" << "ab";
    // Nested markers strip at every level (features.md §2.2.7; step 4).
    QTest::newRow("nested") << "**bo *it* ld**" << "bo it ld";
    // The nested code span hides its backticks but keeps its content
    // verbatim — the asterisks inside are code text, not a bold span.
    QTest::newRow("nested-code-verbatim") << "*i `**x**` z*" << "i **x** z";
    QTest::newRow("unclosed") << "**abc" << "**abc";
    QTest::newRow("empty-markers") << "****" << "****";
    QTest::newRow("strike") << "~~gone~~" << "gone";
    QTest::newRow("highlight") << "==note==" << "note";
    QTest::newRow("underline") << "++line++" << "line";
    QTest::newRow("code") << "`x = 1`" << "x = 1";
    QTest::newRow("all-types-sentence")
        << "a **b** *i* ~~s~~ ==h== ++u++ `c` z" << "a b i s h u c z";
    QTest::newRow("strike-unclosed") << "~~abc" << "~~abc";
    QTest::newRow("highlight-empty") << "====" << "====";
    QTest::newRow("underline-empty") << "++++" << "++++";
    QTest::newRow("backtick-unclosed") << "`abc" << "`abc";
    QTest::newRow("underscore-bold") << "__b__" << "b";
    QTest::newRow("underscore-italic") << "_i_" << "i";
    QTest::newRow("underscore-bolditalic") << "___x___" << "x";
    QTest::newRow("snake-case-literal") << "snake_case_name" << "snake_case_name";
    QTest::newRow("intra-word-underscores") << "x_y_z" << "x_y_z";
    QTest::newRow("mixed-variants") << "**a** and __b__" << "a and b";
}

void TestBlockEditorEngine::testDisplayText()
{
    QFETCH(QString, markdown);
    QFETCH(QString, display);
    QCOMPARE(BlockEditorEngine::displayText(markdown), display);
}

void TestBlockEditorEngine::testDocumentTextStates()
{
    QCOMPARE(BlockEditorEngine::documentText(M, -1),
             QString("This is bold and it end"));
    QCOMPARE(BlockEditorEngine::documentText(M, 0),
             QString("This is **bold** and it end"));
    QCOMPARE(BlockEditorEngine::documentText(M, 1),
             QString("This is bold and *it* end"));
}

void TestBlockEditorEngine::testDocumentToMarkdownHidden_data()
{
    QTest::addColumn<int>("docPos");
    QTest::addColumn<int>("mdPos");

    QTest::newRow("start") << 0 << 0;
    QTest::newRow("plain") << 7 << 7;
    // Left content edge maps inside the span (after the opening marker) —
    // matches where a QTextCursor lands when markers are inserted there.
    QTest::newRow("content-left-edge") << 8 << 10;
    QTest::newRow("inside-content") << 10 << 12;
    // Right content edge maps after the closing marker.
    QTest::newRow("content-right-edge") << 12 << 16;
    QTest::newRow("plain-between") << 13 << 17;
    QTest::newRow("italic-left-edge") << 17 << 22;
    QTest::newRow("italic-right-edge") << 19 << 25;
    QTest::newRow("end") << 23 << 29;
}

void TestBlockEditorEngine::testDocumentToMarkdownHidden()
{
    QFETCH(int, docPos);
    QFETCH(int, mdPos);
    QCOMPARE(BlockEditorEngine::documentToMarkdown(M, -1, docPos), mdPos);
}

void TestBlockEditorEngine::testDocumentToMarkdownRevealed()
{
    // documentText(M, 0) == "This is **bold** and it end"
    QCOMPARE(BlockEditorEngine::documentToMarkdown(M, 0, 9), 9);   // inside raw markers
    QCOMPARE(BlockEditorEngine::documentToMarkdown(M, 0, 12), 12); // inside raw content
    QCOMPARE(BlockEditorEngine::documentToMarkdown(M, 0, 16), 16); // after raw
    QCOMPARE(BlockEditorEngine::documentToMarkdown(M, 0, 21), 22); // hidden italic content edge
}

void TestBlockEditorEngine::testMarkdownToDocumentHidden_data()
{
    QTest::addColumn<int>("mdPos");
    QTest::addColumn<int>("docPos");

    QTest::newRow("start") << 0 << 0;
    QTest::newRow("span-start") << 8 << 8;
    QTest::newRow("in-opening-marker") << 9 << 8;   // clamps to content edge
    QTest::newRow("content-start") << 10 << 8;
    QTest::newRow("inside-content") << 12 << 10;
    QTest::newRow("in-closing-marker") << 15 << 12; // clamps to content edge
    QTest::newRow("span-end") << 16 << 12;
    QTest::newRow("plain-between") << 17 << 13;
    QTest::newRow("end") << 29 << 23;
}

void TestBlockEditorEngine::testMarkdownToDocumentHidden()
{
    QFETCH(int, mdPos);
    QFETCH(int, docPos);
    QCOMPARE(BlockEditorEngine::markdownToDocument(M, -1, mdPos), docPos);
}

void TestBlockEditorEngine::testMappingRoundTripThroughContent()
{
    // Every position strictly inside hidden content round-trips exactly.
    for (int docPos = 9; docPos < 12; ++docPos) {
        const int mdPos = BlockEditorEngine::documentToMarkdown(M, -1, docPos);
        QCOMPARE(BlockEditorEngine::markdownToDocument(M, -1, mdPos), docPos);
    }
    // Every revealed-state position round-trips exactly (1:1 raw segment).
    for (int docPos = 8; docPos <= 16; ++docPos) {
        const int mdPos = BlockEditorEngine::documentToMarkdown(M, 0, docPos);
        QCOMPARE(BlockEditorEngine::markdownToDocument(M, 0, mdPos), docPos);
    }
}

void TestBlockEditorEngine::testSpanToReveal_data()
{
    QTest::addColumn<int>("mdPos");
    QTest::addColumn<int>("spanIndex");

    QTest::newRow("before") << 7 << -1;
    QTest::newRow("at-span-start") << 8 << 0;     // inclusive left edge
    QTest::newRow("inside-markers") << 9 << 0;
    QTest::newRow("inside-content") << 12 << 0;
    QTest::newRow("at-span-end") << 16 << 0;      // inclusive right edge
    QTest::newRow("between") << 18 << -1;
    QTest::newRow("italic") << 23 << 1;
    QTest::newRow("after-all") << 27 << -1;
}

void TestBlockEditorEngine::testSpanToReveal()
{
    QFETCH(int, mdPos);
    QFETCH(int, spanIndex);
    QCOMPARE(BlockEditorEngine::spanToReveal(M, mdPos), spanIndex);
}

void TestBlockEditorEngine::testFormatRangesHidden()
{
    QCOMPARE(BlockEditorEngine::formatRangesForState("", -1), QList<FormatRange>{});
    QCOMPARE(BlockEditorEngine::formatRangesForState("plain", -1), QList<FormatRange>{});
    QCOMPARE(BlockEditorEngine::formatRangesForState("**b**", -1),
             (QList<FormatRange>{bold(0, 1)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("*i*", -1),
             (QList<FormatRange>{italic(0, 1)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("***x***", -1),
             (QList<FormatRange>{boldItalic(0, 1)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("Hello **world** end", -1),
             (QList<FormatRange>{bold(6, 5)}));
}

void TestBlockEditorEngine::testFormatRangesRevealed()
{
    QCOMPARE(BlockEditorEngine::formatRangesForState("**b**", 0),
             (QList<FormatRange>{marker(0, 2), bold(2, 1), marker(3, 2)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("*i*", 0),
             (QList<FormatRange>{marker(0, 1), italic(1, 1), marker(2, 1)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("***x***", 0),
             (QList<FormatRange>{marker(0, 3), boldItalic(3, 1), marker(4, 3)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("Hello **world** end", 0),
             (QList<FormatRange>{marker(6, 2), bold(8, 5), marker(13, 2)}));
}

void TestBlockEditorEngine::testFormatRangesMultipleAndAdjacent()
{
    // "a **b** c *d* e" — display "a b c d e"
    QCOMPARE(BlockEditorEngine::formatRangesForState("a **b** c *d* e", -1),
             (QList<FormatRange>{bold(2, 1), italic(6, 1)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("a **b** c *d* e", 0),
             (QList<FormatRange>{marker(2, 2), bold(4, 1), marker(5, 2), italic(10, 1)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("a **b** c *d* e", 1),
             (QList<FormatRange>{bold(2, 1), marker(6, 1), italic(7, 1), marker(8, 1)}));

    // adjacent "**a***b*" — display "ab"
    QCOMPARE(BlockEditorEngine::formatRangesForState("**a***b*", -1),
             (QList<FormatRange>{bold(0, 1), italic(1, 1)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("**a***b*", 0),
             (QList<FormatRange>{marker(0, 2), bold(2, 1), marker(3, 2), italic(5, 1)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState("**a***b*", 1),
             (QList<FormatRange>{bold(0, 1), marker(1, 1), italic(2, 1), marker(3, 1)}));
}

void TestBlockEditorEngine::testFormatRangesNestedAndDegenerate()
{
    // Nested content combines ancestor flags (step 4); hidden state shows
    // "bo it ld" with the inner word bold+italic.
    QCOMPARE(BlockEditorEngine::formatRangesForState("**bo *it* ld**", -1),
             (QList<FormatRange>{bold(0, 3), boldItalic(3, 2), bold(5, 3)}));
    // Revealed, the whole top-level span shows every marker in its
    // subtree, muted; content styling is unchanged.
    QCOMPARE(BlockEditorEngine::formatRangesForState("**bo *it* ld**", 0),
             (QList<FormatRange>{marker(0, 2), bold(2, 3), marker(5, 1),
                                 boldItalic(6, 2), marker(8, 1), bold(9, 3),
                                 marker(12, 2)}));

    QCOMPARE(BlockEditorEngine::formatRangesForState("**abc", -1), QList<FormatRange>{});
    QCOMPARE(BlockEditorEngine::formatRangesForState("****", -1), QList<FormatRange>{});
}

void TestBlockEditorEngine::testEditTypeInPlainText()
{
    const auto r = BlockEditorEngine::applyDocumentEdit(M, -1, 4, 0, "X");
    QCOMPARE(r.markdown, QString("ThisX is **bold** and *it* end"));
    QCOMPARE(r.mdEditEnd, 5);
}

void TestBlockEditorEngine::testEditTypeInsideHiddenSpan()
{
    // Insert 'x' between 'o' and 'l' of hidden "bold" (doc pos 10)
    const auto r = BlockEditorEngine::applyDocumentEdit(M, -1, 10, 0, "x");
    QCOMPARE(r.markdown, QString("This is **boxld** and *it* end"));
    QCOMPARE(r.mdEditEnd, 13);
}

void TestBlockEditorEngine::testEditTypeInsideRevealedSpan()
{
    // documentText(M, 0) == "This is **bold** and it end"; insert at raw pos 12
    const auto r = BlockEditorEngine::applyDocumentEdit(M, 0, 12, 0, "x");
    QCOMPARE(r.markdown, QString("This is **boxld** and *it* end"));
    QCOMPARE(r.mdEditEnd, 13);
}

void TestBlockEditorEngine::testEditDeleteWholeHiddenWordRemovesMarkers()
{
    // Deleting all of "bold" (doc [8,12)) must delete the markers too.
    const auto r = BlockEditorEngine::applyDocumentEdit(M, -1, 8, 4, QString());
    QCOMPARE(r.markdown, QString("This is  and *it* end"));
    QCOMPARE(r.mdEditEnd, 8);
}

void TestBlockEditorEngine::testEditPartialDeleteKeepsMarkers()
{
    // Deleting "bo" (doc [8,10)) keeps the span with the remaining content.
    const auto r = BlockEditorEngine::applyDocumentEdit(M, -1, 8, 2, QString());
    QCOMPARE(r.markdown, QString("This is **ld** and *it* end"));
}

void TestBlockEditorEngine::testEditStraddlingDelete()
{
    // Deleting "s bo" (doc [6,10)): plain "s " plus content "bo"; the
    // closing marker survives between the two removed ranges.
    const auto r = BlockEditorEngine::applyDocumentEdit(M, -1, 6, 4, QString());
    QCOMPARE(r.markdown, QString("This i**ld** and *it* end"));
}

void TestBlockEditorEngine::testEditReplaceSelection()
{
    // Replace doc [4,10) ("s is" + " bo"... plain [4,8) + content [8,10)) with "Z"
    const auto r = BlockEditorEngine::applyDocumentEdit(M, -1, 4, 6, "Z");
    QCOMPARE(r.markdown, QString("ThisZ**ld** and *it* end"));
    QCOMPARE(r.mdEditEnd, 5);
}

void TestBlockEditorEngine::testEditDeleteRevealedMarkerChar()
{
    // Revealed state; delete the last '*' of the closing marker (raw is 1:1).
    const auto r = BlockEditorEngine::applyDocumentEdit(M, 0, 15, 1, QString());
    QCOMPARE(r.markdown, QString("This is **bold* and *it* end"));
}

void TestBlockEditorEngine::testEditInsertIntoEmptyMarkdown()
{
    const auto r = BlockEditorEngine::applyDocumentEdit(QString(), -1, 0, 0, "a");
    QCOMPARE(r.markdown, QString("a"));
    QCOMPARE(r.mdEditEnd, 1);
}

void TestBlockEditorEngine::testSetMarkdownFillsDocumentWithDisplayText()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello **world**");

    QCOMPARE(doc.toPlainText(), QString("Hello world"));
    QCOMPARE(engine.markdown(), QString("Hello **world**"));
    QCOMPARE(engine.revealedSpan(), -1);
}

void TestBlockEditorEngine::testModelRebuildEmitsNoUserEdit()
{
    // Programmatic (model-driven) edits must emit no model updates —
    // only real user edits may emit markdownEdited.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);

    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);
    engine.setMarkdown("first");
    engine.setMarkdown("second **b** version");
    engine.setMarkdown("");

    QCOMPARE(editedSpy.count(), 0);
    QCOMPARE(doc.toPlainText(), QString(""));
}

void TestBlockEditorEngine::testInitialBindCoalescesRebuilds()
{
    QTextDocument doc;
    BlockEditorEngine engine;

    engine.classBegin();
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello **world**");
    engine.setLineHeight(1.25);
    engine.setMonoFontFamily("Fira Mono");
    engine.setContentFontPixelSize(17);
    engine.setContentFontFamily("Serif");
    engine.setContentFontWeight(QFont::Bold);
    QVariantList matches;
    matches.append(QVariantMap{{QStringLiteral("start"), 0},
                               {QStringLiteral("length"), 5},
                               {QStringLiteral("current"), true}});
    engine.setSearchMatches(matches);

    QCOMPARE(engine.rebuildCountForTesting(), 1);
    QCOMPARE(engine.rehighlightCountForTesting(), 0);
    QCOMPARE(doc.toPlainText(), QString("Hello world"));

    engine.componentComplete();

    QCOMPARE(doc.toPlainText(), QString("Hello world"));
    QCOMPARE(engine.rebuildCountForTesting(), 1);
    QCOMPARE(engine.rehighlightCountForTesting(), 1);
    QCOMPARE(engine.markdown(), QString("Hello **world**"));
    QCOMPARE(engine.revealedSpan(), -1);
}

void TestBlockEditorEngine::testUserEditEmitsMarkdownEdited()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello");

    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);

    // Simulate the user typing: an edit on the document from outside the
    // engine (exactly what TextArea key handling does).
    QTextCursor cursor(&doc);
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(" world");

    QCOMPARE(editedSpy.count(), 1);
    QCOMPARE(editedSpy.takeFirst().at(0).toString(), QString("Hello world"));
    QCOMPARE(engine.markdown(), QString("Hello world"));
}

void TestBlockEditorEngine::testSetSameMarkdownIsQuiet()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("stable");

    QSignalSpy changedSpy(&engine, &BlockEditorEngine::markdownChanged);
    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);
    engine.setMarkdown("stable");

    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(editedSpy.count(), 0);
    QCOMPARE(doc.toPlainText(), QString("stable"));
}

void TestBlockEditorEngine::testHighlighterStylesHiddenContent()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello **world**");
    QCoreApplication::processEvents();

    // Hidden state: document shows "Hello world"; content 6..11 is bold.
    const auto formats = doc.firstBlock().layout()->formats();
    QVERIFY(!formats.isEmpty());

    bool contentIsBold = false;
    for (const auto &fr : formats) {
        if (fr.start <= 6 && fr.start + fr.length >= 11
            && fr.format.fontWeight() == QFont::Bold) {
            contentIsBold = true;
        }
    }
    QVERIFY2(contentIsBold, "hidden span content should carry a bold char format");
}

void TestBlockEditorEngine::testDocumentUndoDisabled()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    QVERIFY(!doc.isUndoRedoEnabled());
}

void TestBlockEditorEngine::testReattachToNewDocument()
{
    // Delegate pooling detaches and reattaches engines; the new document
    // must pick up the engine's markdown and the old one must go silent.
    QTextDocument docA;
    QTextDocument docB;
    BlockEditorEngine engine;
    engine.attachDocument(&docA);
    engine.setMarkdown("content A");
    QCOMPARE(docA.toPlainText(), QString("content A"));

    engine.attachDocument(&docB);
    QCOMPARE(docB.toPlainText(), QString("content A"));

    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);
    QTextCursor cursorA(&docA);
    cursorA.insertText("stray edit on the old document");
    QCOMPARE(editedSpy.count(), 0);

    QTextCursor cursorB(&docB);
    cursorB.movePosition(QTextCursor::End);
    cursorB.insertText("!");
    QCOMPARE(editedSpy.count(), 1);
    QCOMPARE(engine.markdown(), QString("content A!"));
}

void TestBlockEditorEngine::testInactiveCursorNeverReveals()
{
    // A block whose content STARTS with a span must not reveal it just
    // because the idle cursor position is 0.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("**lead** text");

    QCOMPARE(doc.toPlainText(), QString("lead text"));
    QCOMPARE(engine.revealedSpan(), -1);

    engine.setCursorPosition(1);
    settle();
    QCOMPARE(engine.revealedSpan(), -1); // still inactive

    engine.setCursorActive(true);
    settle();
    QCOMPARE(engine.revealedSpan(), 0);
    QCOMPARE(doc.toPlainText(), QString("**lead** text"));
}

void TestBlockEditorEngine::testRevealWalkthrough_2_2_3()
{
    // features.md §2.2.3 example 1, against the real document.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("This is **important** *information* here");
    engine.setCursorActive(true);

    // Cursor at line start: everything rendered.
    engine.setCursorPosition(0);
    settle();
    QCOMPARE(doc.toPlainText(), QString("This is important information here"));
    QCOMPARE(engine.revealedSpan(), -1);

    // Cursor into "important": its syntax visible, rest rendered.
    engine.setCursorPosition(10);
    settle();
    QCOMPARE(doc.toPlainText(), QString("This is **important** information here"));
    QCOMPARE(engine.revealedSpan(), 0);

    // Cursor onward into "information": bold re-hidden, italic revealed.
    engine.setCursorPosition(25);
    settle();
    QCOMPARE(doc.toPlainText(), QString("This is important *information* here"));
    QCOMPARE(engine.revealedSpan(), 1);

    // Cursor elsewhere: everything rendered again; markdown never changed.
    engine.setCursorPosition(0);
    settle();
    QCOMPARE(doc.toPlainText(), QString("This is important information here"));
    QCOMPARE(engine.revealedSpan(), -1);
    QCOMPARE(engine.markdown(), QString("This is **important** *information* here"));
}

void TestBlockEditorEngine::testRevealTransitionEmitsNoUserEdit()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello **world** end");
    engine.setCursorActive(true);

    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);
    QSignalSpy changedSpy(&engine, &BlockEditorEngine::markdownChanged);

    engine.setCursorPosition(8);  // reveal
    settle();
    engine.setCursorPosition(0);  // hide
    settle();

    QCOMPARE(editedSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(engine.markdown(), QString("Hello **world** end"));
}

void TestBlockEditorEngine::testTypingNewSpanStaysRawUntilCursorLeaves()
{
    // features.md §2.2.3 example 2: type "**hi**" — asterisks stay visible
    // while typing and while the cursor is still at the closing marker;
    // rendering happens when the cursor moves away.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello ");
    engine.setCursorActive(true);
    engine.setCursorPosition(6);

    const QString typed = QStringLiteral("**hi**");
    for (int i = 0; i < typed.length(); ++i) {
        QTextCursor tc(&doc);
        tc.setPosition(6 + i);
        tc.insertText(QString(typed.at(i)));
        engine.setCursorPosition(7 + i);
        settle();
    }

    // Closing marker just typed: span complete, still revealed.
    QCOMPARE(engine.markdown(), QString("Hello **hi**"));
    QCOMPARE(doc.toPlainText(), QString("Hello **hi**"));
    QCOMPARE(engine.revealedSpan(), 0);

    // Cursor leaves: rendered.
    engine.setCursorPosition(0);
    settle();
    QCOMPARE(doc.toPlainText(), QString("Hello hi"));
    QCOMPARE(engine.revealedSpan(), -1);
    QCOMPARE(engine.markdown(), QString("Hello **hi**"));
}

void TestBlockEditorEngine::testDeletingMarkerCharDestroysSpan()
{
    // Backspacing a revealed marker char edits the markdown and the span
    // stops existing (manual unformat, §2.2.3 example 3).
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello **hi**");
    engine.setCursorActive(true);
    engine.setCursorPosition(8); // inside content -> reveal
    settle();
    QCOMPARE(doc.toPlainText(), QString("Hello **hi**"));

    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);
    QTextCursor tc(&doc);
    tc.setPosition(11);
    tc.setPosition(12, QTextCursor::KeepAnchor);
    tc.removeSelectedText(); // delete last '*'
    settle();

    QCOMPARE(editedSpy.count(), 1);
    QCOMPARE(engine.markdown(), QString("Hello **hi*"));
    QCOMPARE(doc.toPlainText(), QString("Hello **hi*"));
}

void TestBlockEditorEngine::testFocusLossHidesRevealedSpan()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello **world**");
    engine.setCursorActive(true);
    engine.setCursorPosition(8);
    settle();
    QCOMPARE(doc.toPlainText(), QString("Hello **world**"));

    engine.setCursorActive(false);
    settle();
    QCOMPARE(doc.toPlainText(), QString("Hello world"));
    QCOMPARE(engine.revealedSpan(), -1);
}

void TestBlockEditorEngine::testSelectionRevealsAllTouchedSpans()
{
    // §2.2.4: a selection that includes spans reveals every one it touches.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("This is **important** *information* here");
    engine.setCursorActive(true);
    settle();

    // Display: "This is important information here" — select "important
    // information" (display 8..29): both spans touched.
    engine.setSelectionStart(8);
    engine.setSelectionEnd(29);
    settle();
    QCOMPARE(engine.revealedSpans(), (QList<int>{0, 1}));
    QCOMPARE(doc.toPlainText(), QString("This is **important** *information* here"));
    QCOMPARE(engine.markdown(), QString("This is **important** *information* here"));

    // Collapse the selection outside: everything rendered again.
    engine.setSelectionStart(0);
    engine.setSelectionEnd(0);
    engine.setCursorPosition(0);
    settle();
    QCOMPARE(engine.revealedSpans(), QList<int>{});
    QCOMPARE(doc.toPlainText(), QString("This is important information here"));
}

void TestBlockEditorEngine::testPartialSelectionRevealsTouchedSpan()
{
    // §2.2.7: a selection that starts/ends within a formatted region
    // reveals that region's syntax; untouched spans stay rendered.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("This is **important** *information* here");
    engine.setCursorActive(true);
    settle();

    // Select "is imp" (display 5..11): touches only the bold span.
    engine.setSelectionStart(5);
    engine.setSelectionEnd(11);
    settle();
    QCOMPARE(engine.revealedSpans(), QList<int>{0});
    QCOMPARE(doc.toPlainText(), QString("This is **important** information here"));
}

void TestBlockEditorEngine::testModelRebuildUsesMinimalDiff()
{
    // Undo/redo arrives as a model-driven setMarkdown; the rebuild must
    // only touch the differing middle (risk table: cursor-preserving).
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello brave world");

    int lastRemoved = -1;
    int lastAdded = -1;
    connect(&doc, &QTextDocument::contentsChange,
            [&](int, int removed, int added) {
                if (removed + added > 0) {
                    lastRemoved = removed;
                    lastAdded = added;
                }
            });

    engine.setMarkdown("Hello world"); // simulated undo of typing "brave "
    QCOMPARE(doc.toPlainText(), QString("Hello world"));
    QCOMPARE(lastRemoved, 6);
    QCOMPARE(lastAdded, 0);
}

void TestBlockEditorEngine::testMarkdownForRangeCopy()
{
    const QList<int> none;
    // Whole document reproduces the full markdown.
    QCOMPARE(BlockEditorEngine::markdownForRange(M, none, 0, 23), M);
    // Plain-only slice is literal.
    QCOMPARE(BlockEditorEngine::markdownForRange(M, none, 0, 7), QString("This is"));
    // Exactly the rendered bold word: markers come along.
    QCOMPARE(BlockEditorEngine::markdownForRange(M, none, 8, 12), QString("**bold**"));
    // Partial span content keeps its formatting.
    QCOMPARE(BlockEditorEngine::markdownForRange(M, none, 9, 11), QString("**ol**"));
    // Across spans and plain text.
    QCOMPARE(BlockEditorEngine::markdownForRange(M, none, 8, 19),
             QString("**bold** and *it*"));
}

void TestBlockEditorEngine::testMarkdownForRangeRevealedState()
{
    // documentText(M, 0) == "This is **bold** and it end"
    const QList<int> revealed{0};
    // Content only (double-click on the revealed word): markers included once.
    QCOMPARE(BlockEditorEngine::markdownForRange(M, revealed, 10, 14), QString("**bold**"));
    // Content plus both visible markers: not doubled.
    QCOMPARE(BlockEditorEngine::markdownForRange(M, revealed, 8, 16), QString("**bold**"));
    // Half a marker plus content: marker chars ignored, wrap supplies them.
    QCOMPARE(BlockEditorEngine::markdownForRange(M, revealed, 9, 14), QString("**bold**"));
}

void TestBlockEditorEngine::testCutRangeResult()
{
    const QList<int> none;
    // Cutting the whole rendered word removes markers too (cut+paste
    // round-trips, unlike plain deletion which keeps empty markers).
    auto r = BlockEditorEngine::cutRangeResult(M, none, 8, 12);
    QCOMPARE(r.markdown, QString("This is  and *it* end"));
    QCOMPARE(r.mdEditEnd, 8);

    // Partial cut keeps the span with the remaining content.
    r = BlockEditorEngine::cutRangeResult(M, none, 9, 11);
    QCOMPARE(r.markdown, QString("This is **bd** and *it* end"));
    QCOMPARE(r.mdEditEnd, 11);

    // Across spans: both fully-covered spans disappear with their markers.
    r = BlockEditorEngine::cutRangeResult(M, none, 8, 19);
    QCOMPARE(r.markdown, QString("This is  end"));
    QCOMPARE(r.mdEditEnd, 8);

    // Revealed state, content selected: whole span removed.
    r = BlockEditorEngine::cutRangeResult(M, QList<int>{0}, 10, 14);
    QCOMPARE(r.markdown, QString("This is  and *it* end"));
    QCOMPARE(r.mdEditEnd, 8);
}

// One full lifecycle per new symmetric span type, over the markdown
// "Hi <m>word<m> end": display, reveal decision at the inclusive edges,
// format ranges in both states, edit mapping, copy wrap, and the live
// engine's reveal/hide transitions. The table is the registry contract:
// a future type passes by adding a row here and in the registry.
void TestBlockEditorEngine::testSymmetricTypeLifecycle_data()
{
    QTest::addColumn<QString>("marker");
    QTest::addColumn<quint32>("flags");

    QTest::newRow("strike") << "~~" << quint32(FormatRange::Strike);
    QTest::newRow("highlight") << "==" << quint32(FormatRange::Highlight);
    QTest::newRow("underline") << "++" << quint32(FormatRange::Underline);
    QTest::newRow("code") << "`" << quint32(FormatRange::Code);
    // "_" variants (step 3) run the same lifecycle; "Hi _word_ end" has
    // the word boundaries the family requires.
    QTest::newRow("underscore-bold") << "__" << quint32(FormatRange::Bold);
    QTest::newRow("underscore-italic") << "_" << quint32(FormatRange::Italic);
    QTest::newRow("underscore-bolditalic") << "___" << quint32(FormatRange::BoldItalic);
}

void TestBlockEditorEngine::testSymmetricTypeLifecycle()
{
    QFETCH(QString, marker);
    QFETCH(quint32, flags);

    const int L = marker.length();
    const QString md = "Hi " + marker + "word" + marker + " end";
    const int spanStart = 3;
    const int spanEnd = 3 + 2 * L + 4;

    // Display / document text per state.
    QCOMPARE(BlockEditorEngine::displayText(md), QString("Hi word end"));
    QCOMPARE(BlockEditorEngine::documentText(md, 0), md);

    // Reveal decision, inclusive at both edges.
    QCOMPARE(BlockEditorEngine::spanToReveal(md, spanStart - 1), -1);
    QCOMPARE(BlockEditorEngine::spanToReveal(md, spanStart), 0);
    QCOMPARE(BlockEditorEngine::spanToReveal(md, spanEnd), 0);
    QCOMPARE(BlockEditorEngine::spanToReveal(md, spanEnd + 1), -1);

    // Format ranges hidden and revealed.
    QCOMPARE(BlockEditorEngine::formatRangesForState(md, -1),
             (QList<FormatRange>{{3, 4, flags}}));
    QCOMPARE(BlockEditorEngine::formatRangesForState(md, 0),
             (QList<FormatRange>{{3, L, FormatRange::Marker},
                                 {3 + L, 4, flags},
                                 {3 + L + 4, L, FormatRange::Marker}}));

    // Edit mapping: typing inside hidden content lands in the content.
    auto r = BlockEditorEngine::applyDocumentEdit(md, -1, 5, 0, "x");
    QCOMPARE(r.markdown, "Hi " + marker + "woxrd" + marker + " end");
    // Deleting all of the hidden content removes the markers too.
    r = BlockEditorEngine::applyDocumentEdit(md, -1, 3, 4, QString());
    QCOMPARE(r.markdown, QString("Hi  end"));
    // Copy of exactly the rendered word carries the markers.
    QCOMPARE(BlockEditorEngine::markdownForRange(md, QList<int>{}, 3, 7),
             marker + "word" + marker);

    // Live engine: reveal on cursor entry, hide on exit, model untouched.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown(md);
    engine.setCursorActive(true);
    QCOMPARE(doc.toPlainText(), QString("Hi word end"));

    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);
    engine.setCursorPosition(5);
    settle();
    QCOMPARE(doc.toPlainText(), md);
    QCOMPARE(engine.revealedSpan(), 0);

    engine.setCursorPosition(0);
    settle();
    QCOMPARE(doc.toPlainText(), QString("Hi word end"));
    QCOMPARE(engine.revealedSpan(), -1);
    QCOMPARE(editedSpy.count(), 0);
    QCOMPARE(engine.markdown(), md);
}

void TestBlockEditorEngine::testCodeSpanContentStaysVerbatim()
{
    // Inline code content is literal: markers of other types inside it
    // never parse, in display or in format ranges (features.md §2.1's
    // "monospace" region; the nesting model excludes code).
    const QString md = QStringLiteral("`a **b** c`");
    QCOMPARE(BlockEditorEngine::displayText(md), QString("a **b** c"));
    QCOMPARE(BlockEditorEngine::formatRangesForState(md, -1),
             (QList<FormatRange>{codeRange(0, 9)}));
    QCOMPARE(BlockEditorEngine::formatRangesForState(md, 0),
             (QList<FormatRange>{marker(0, 1), codeRange(1, 9), marker(10, 1)}));

    // Editing inside the code content edits literal characters; the new
    // '*' does not start a span because the code span still wraps it.
    const auto r = BlockEditorEngine::applyDocumentEdit(md, -1, 2, 0, "*");
    QCOMPARE(r.markdown, QString("`a ***b** c`"));
    QCOMPARE(BlockEditorEngine::displayText(r.markdown), QString("a ***b** c"));
}

// Nested-span reference markdown (step 4):
//   "**bo *it* ld**"  — bold [0,14), child italic md [5,9)
//   display "bo it ld" — "bo " bold, "it" bold+italic, " ld" bold
static const QString N = QStringLiteral("**bo *it* ld**");

void TestBlockEditorEngine::testNestedMappingRoundTrip()
{
    QCOMPARE(BlockEditorEngine::documentText(N, -1), QString("bo it ld"));
    QCOMPARE(BlockEditorEngine::documentText(N, 0), N);

    // Hidden: inner content maps through both marker levels.
    QCOMPARE(BlockEditorEngine::documentToMarkdown(N, -1, 4), 7);  // between i,t
    QCOMPARE(BlockEditorEngine::markdownToDocument(N, -1, 7), 4);
    // The child's left content edge maps inside the child span.
    QCOMPARE(BlockEditorEngine::documentToMarkdown(N, -1, 3), 6);
    // Every hidden-content position round-trips.
    for (int docPos = 1; docPos < 8; ++docPos) {
        const int mdPos = BlockEditorEngine::documentToMarkdown(N, -1, docPos);
        QCOMPARE(BlockEditorEngine::markdownToDocument(N, -1, mdPos), docPos);
    }
    // Revealed: the whole top-level span is 1:1.
    for (int docPos = 0; docPos <= 14; ++docPos)
        QCOMPARE(BlockEditorEngine::documentToMarkdown(N, 0, docPos), docPos);
}

void TestBlockEditorEngine::testNestedEditMapping()
{
    // Typing inside the nested content lands between both marker levels.
    auto r = BlockEditorEngine::applyDocumentEdit(N, -1, 4, 0, "x");
    QCOMPARE(r.markdown, QString("**bo *ixt* ld**"));

    // Deleting exactly the nested word removes the child span whole,
    // markers included; the outer span survives.
    r = BlockEditorEngine::applyDocumentEdit(N, -1, 3, 2, QString());
    QCOMPARE(r.markdown, QString("**bo  ld**"));

    // A straddling deletion ("o i") removes content characters only; the
    // child's markers survive around its remaining content.
    r = BlockEditorEngine::applyDocumentEdit(N, -1, 1, 3, QString());
    QCOMPARE(r.markdown, QString("**b*t* ld**"));

    // Deleting the entire visible content removes the whole span tree.
    r = BlockEditorEngine::applyDocumentEdit(N, -1, 0, 8, QString());
    QCOMPARE(r.markdown, QString(""));
}

void TestBlockEditorEngine::testNestedCopyAndCut()
{
    const QList<int> none;
    // Whole visible content: byte-faithful raw markdown.
    QCOMPARE(BlockEditorEngine::markdownForRange(N, none, 0, 8), N);
    // Exactly the nested word: its own span, wrapped in the outer chain.
    QCOMPARE(BlockEditorEngine::markdownForRange(N, none, 3, 5), QString("***it***"));
    // A straddle ("o i") reconstructs self-contained fragments that
    // render exactly like the selected text.
    const QString frag = BlockEditorEngine::markdownForRange(N, none, 1, 4);
    QCOMPARE(frag, QString("**o *****i***"));
    QCOMPARE(BlockEditorEngine::displayText(frag), QString("o i"));

    // Cut of the nested word removes the child span whole.
    auto r = BlockEditorEngine::cutRangeResult(N, none, 3, 5);
    QCOMPARE(r.markdown, QString("**bo  ld**"));
    // Cut of everything removes the whole tree.
    r = BlockEditorEngine::cutRangeResult(N, none, 0, 8);
    QCOMPARE(r.markdown, QString(""));
}

void TestBlockEditorEngine::testNestedRevealWalkthrough()
{
    // features.md §2.2.7: cursor in the region reveals ALL applicable
    // syntax — the top-level span is the reveal unit, nested markers
    // show and hide with it.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("A " + N + " z");
    engine.setCursorActive(true);
    QCOMPARE(doc.toPlainText(), QString("A bo it ld z"));

    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);

    // Cursor into the nested word ("it", display 5..7).
    engine.setCursorPosition(6);
    settle();
    QCOMPARE(doc.toPlainText(), QString("A **bo *it* ld** z"));
    QCOMPARE(engine.revealedSpan(), 0);

    // Cursor into the outer word ("bo") keeps the same reveal unit.
    engine.setCursorPosition(3);
    settle();
    QCOMPARE(doc.toPlainText(), QString("A **bo *it* ld** z"));

    // Cursor out: everything hides in one step, model untouched.
    engine.setCursorPosition(0);
    settle();
    QCOMPARE(doc.toPlainText(), QString("A bo it ld z"));
    QCOMPARE(engine.revealedSpan(), -1);
    QCOMPARE(editedSpy.count(), 0);
    QCOMPARE(engine.markdown(), "A " + N + " z");
}

// Link reference markdown (step 6):
//   "go [ab](http://x) on" — link md [3,17), text "ab", display "go ab on"
static const QString L = QStringLiteral("go [ab](http://x) on");

void TestBlockEditorEngine::testLinkStatesAndMapping()
{
    QCOMPARE(BlockEditorEngine::displayText(L), QString("go ab on"));
    QCOMPARE(BlockEditorEngine::documentText(L, 0), L);

    // Format ranges: link accent on the text; revealed markers muted.
    QCOMPARE(BlockEditorEngine::formatRangesForState(L, -1),
             (QList<FormatRange>{linkRange(3, 2, "http://x")}));
    QCOMPARE(BlockEditorEngine::formatRangesForState(L, 0),
             (QList<FormatRange>{marker(3, 1),
                                 linkRange(4, 2, "http://x"),
                                 marker(6, 11)}));

    // The reveal decision is inclusive over the whole [text](url) range.
    QCOMPARE(BlockEditorEngine::spanToReveal(L, 2), -1);
    QCOMPARE(BlockEditorEngine::spanToReveal(L, 3), 0);
    QCOMPARE(BlockEditorEngine::spanToReveal(L, 10), 0); // inside the URL
    QCOMPARE(BlockEditorEngine::spanToReveal(L, 17), 0);
    QCOMPARE(BlockEditorEngine::spanToReveal(L, 18), -1);

    // Asymmetric mapping: doc left content edge is inside the span; the
    // right edge maps past the whole "](url)" closing marker.
    QCOMPARE(BlockEditorEngine::documentToMarkdown(L, -1, 3), 4);
    QCOMPARE(BlockEditorEngine::documentToMarkdown(L, -1, 5), 17);
    QCOMPARE(BlockEditorEngine::markdownToDocument(L, -1, 10), 5); // URL interior clamps
    for (int docPos = 4; docPos < 5; ++docPos) {
        const int mdPos = BlockEditorEngine::documentToMarkdown(L, -1, docPos);
        QCOMPARE(BlockEditorEngine::markdownToDocument(L, -1, mdPos), docPos);
    }
}

void TestBlockEditorEngine::testColorSpanStatesAndMapping()
{
    // <span style="color:red">hi</span> — openLen 24, content "hi", close 7.
    const QString C = QStringLiteral("<span style=\"color:red\">hi</span>");
    QCOMPARE(BlockEditorEngine::displayText(C), QString("hi"));
    QCOMPARE(BlockEditorEngine::documentText(C, 0), C);

    // Hidden: the content carries a Color range with the per-instance value.
    QCOMPARE(BlockEditorEngine::formatRangesForState(C, -1),
             (QList<FormatRange>{colorRange(0, 2, QStringLiteral("red"))}));
    // Revealed: markers muted, content still colored.
    QCOMPARE(BlockEditorEngine::formatRangesForState(C, 0),
             (QList<FormatRange>{marker(0, 24),
                                 colorRange(24, 2, QStringLiteral("red")),
                                 marker(26, 7)}));

    // Nested color spans: inner value overrides for its content (innermost
    // wins), outer value resumes after it.
    const QString N = QStringLiteral(
        "<span style=\"color:red\">a <span style=\"color:blue\">b</span> c</span>");
    QCOMPARE(BlockEditorEngine::displayText(N), QString("a b c"));
    const auto ranges = BlockEditorEngine::formatRangesForState(N, -1);
    QCOMPARE(ranges, (QList<FormatRange>{
                 colorRange(0, 2, QStringLiteral("red")),   // "a "
                 colorRange(2, 1, QStringLiteral("blue")),  // "b"
                 colorRange(3, 2, QStringLiteral("red")),   // " c"
             }));
}

void TestBlockEditorEngine::testLinkEditCopyCut()
{
    // Typing inside the hidden link text edits the text, not the URL.
    auto r = BlockEditorEngine::applyDocumentEdit(L, -1, 4, 0, "x");
    QCOMPARE(r.markdown, QString("go [axb](http://x) on"));

    // Deleting the whole link text removes the link, URL included.
    r = BlockEditorEngine::applyDocumentEdit(L, -1, 3, 2, QString());
    QCOMPARE(r.markdown, QString("go  on"));

    // Copy: full text captures the raw link; partial keeps the URL.
    const QList<int> none;
    QCOMPARE(BlockEditorEngine::markdownForRange(L, none, 3, 5),
             QString("[ab](http://x)"));
    QCOMPARE(BlockEditorEngine::markdownForRange(L, none, 3, 4),
             QString("[a](http://x)"));

    // Cut mirrors copy.
    auto cut = BlockEditorEngine::cutRangeResult(L, none, 3, 5);
    QCOMPARE(cut.markdown, QString("go  on"));
    cut = BlockEditorEngine::cutRangeResult(L, none, 3, 4);
    QCOMPARE(cut.markdown, QString("go [b](http://x) on"));

    // Paste-plain semantics: the URL is dropped, the text stays.
    QCOMPARE(BlockEditorEngine::displayText(QStringLiteral("[a](http://x)")),
             QString("a"));
}

void TestBlockEditorEngine::testLinkRevealWalkthrough()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown(L);
    engine.setCursorActive(true);
    QCOMPARE(doc.toPlainText(), QString("go ab on"));

    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);

    // Cursor into the link text reveals the whole [text](url) syntax.
    engine.setCursorPosition(4);
    settle();
    QCOMPARE(doc.toPlainText(), L);
    QCOMPARE(engine.revealedSpan(), 0);

    // Cursor away re-renders; the model never changed.
    engine.setCursorPosition(0);
    settle();
    QCOMPARE(doc.toPlainText(), QString("go ab on"));
    QCOMPARE(editedSpy.count(), 0);
    QCOMPARE(engine.markdown(), L);
}

void TestBlockEditorEngine::testAutolinkBehavior()
{
    const QString md = QStringLiteral("go http://a.com on");

    // Zero-length markers: display equals markdown in every state.
    QCOMPARE(BlockEditorEngine::displayText(md), md);
    QCOMPARE(BlockEditorEngine::documentText(md, 0), md);
    QCOMPARE(BlockEditorEngine::formatRangesForState(md, -1),
             (QList<FormatRange>{linkRange(3, 12, "http://a.com")}));

    // Copy across the autolink is the literal text.
    QCOMPARE(BlockEditorEngine::markdownForRange(md, QList<int>{}, 3, 15),
             QString("http://a.com"));

    // Editing the URL text is a plain 1:1 edit.
    const auto r = BlockEditorEngine::applyDocumentEdit(md, -1, 15, 0, "x");
    QCOMPARE(r.markdown, QString("go http://a.comx on"));

    // A live engine never mutates the document for autolink reveals.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown(md);
    engine.setCursorActive(true);
    engine.setCursorPosition(5);
    settle();
    QCOMPARE(doc.toPlainText(), md);
}

void TestBlockEditorEngine::testLinkAtDocumentPosition()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown(L);

    // Hidden state, display "go ab on": the link text carries the URL.
    QCOMPARE(engine.linkAtDocumentPosition(3), QString("http://x"));
    QCOMPARE(engine.linkAtDocumentPosition(4), QString("http://x"));
    QCOMPARE(engine.linkAtDocumentPosition(0), QString());
    QCOMPARE(engine.linkAtDocumentPosition(6), QString());

    // Autolinks answer too.
    engine.setMarkdown("go http://a.com on");
    QCOMPARE(engine.linkAtDocumentPosition(5), QString("http://a.com"));

    // Formatted link text still reports the deepest URL in the chain.
    engine.setMarkdown("[**b**](http://y)");
    QCOMPARE(engine.linkAtDocumentPosition(0), QString("http://y"));
}

void TestBlockEditorEngine::testInternalLinkUnresolvedStyle()
{
    // A DocumentOutline with one heading "Alpha" (slug "alpha").
    BlockModel model;
    model.insertBlock(0, Block::Heading1, "Alpha");
    model.insertBlock(1, Block::Paragraph, "body");
    DocumentOutline outline;
    outline.setModel(&model);

    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setLinkResolver(&outline);
    // Two internal links: one resolves (#alpha), one dangles (#missing).
    // Display: "see A and B" — A at 4, B at 10.
    engine.setMarkdown("see [A](#alpha) and [B](#missing)");

    // The resolved link text renders in the link accent; the unresolved one
    // renders muted (kUnresolvedLinkColor, no theme set).
    QCOMPARE(layoutFormatAt(doc, 4).foreground().color(), QColor("#2970c8"));
    QCOMPARE(layoutFormatAt(doc, 10).foreground().color(), QColor("#9a9a9a"));

    // An external link is never muted, even with a resolver set.
    engine.setMarkdown("go [x](http://z) on");
    QCOMPARE(layoutFormatAt(doc, 3).foreground().color(), QColor("#2970c8"));

    // Adding the missing heading resolves the second link on the next
    // highlight (the outline's slugsChanged drives a rehighlight).
    engine.setMarkdown("see [A](#alpha) and [B](#missing)");
    model.insertBlock(2, Block::Heading2, "missing");
    outline.rebuildNow();
    QCOMPARE(layoutFormatAt(doc, 10).foreground().color(), QColor("#2970c8"));
}

void TestBlockEditorEngine::testWikiLinkRangesAndUnresolvedStyle()
{
    // The hidden state carries Link|WikiLink flags and the kvit-note: url.
    const auto ranges = BlockEditorEngine::formatRangesForState("[[Note]]", -1);
    QCOMPARE(ranges.size(), 1);
    QVERIFY(ranges[0].kind & FormatRange::Link);
    QVERIFY(ranges[0].kind & FormatRange::WikiLink);
    QCOMPARE(ranges[0].url, QString("kvit-note:Note"));

    // A collection with one note: [[Exists]] resolves, [[Missing]] muted.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        QFile f(dir.filePath("Exists.md"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("hi\n");
    }
    NoteCollection collection;
    QVERIFY(collection.openRoot(dir.path()));

    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setWikiResolver(&collection);
    // Display: "a Exists and Missing" — E at 2, M at 13.
    engine.setMarkdown("a [[Exists]] and [[Missing]]");
    QCOMPARE(layoutFormatAt(doc, 2).foreground().color(), QColor("#2970c8"));
    QCOMPARE(layoutFormatAt(doc, 13).foreground().color(), QColor("#9a9a9a"));

    // Click-through: the wiki url answers at the link text.
    QCOMPARE(engine.linkAtDocumentPosition(2), QString("kvit-note:Exists"));

    // Creating the missing note flips the style on the revision bump.
    {
        QFile f(dir.filePath("Missing.md"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        f.write("now\n");
    }
    collection.refresh();
    QCOMPARE(layoutFormatAt(doc, 13).foreground().color(), QColor("#2970c8"));

    // Without a resolver every wiki-link styles as an ordinary link.
    BlockEditorEngine bare;
    QTextDocument doc2;
    bare.attachDocument(&doc2);
    bare.setMarkdown("a [[Missing]]");
    QCOMPARE(layoutFormatAt(doc2, 2).foreground().color(), QColor("#2970c8"));

    // Ctrl+K's span query skips wiki-links (it must never rewrite one
    // into [text](url) form).
    QCOMPARE(engine.linkSpanAtCursor(3).value("found").toBool(), false);
}

void TestBlockEditorEngine::testInlineMathReserveAndReveal()
{
    // Hidden ($…$ not revealed): the content carries the Math flag, which the
    // highlighter renders transparent at renderer-measured width while the
    // delegate overlays the equation.
    const auto hidden = BlockEditorEngine::formatRangesForState("$x^2$", -1);
    bool hiddenMath = false;
    for (const auto &r : hidden)
        if (r.kind & FormatRange::Math)
            hiddenMath = true;
    QVERIFY(hiddenMath);

    // Revealed: the Math flag is stripped so the raw $…$ source shows and is
    // editable, exactly like inline code.
    const auto shown = BlockEditorEngine::formatRangesForState("$x^2$", 0);
    for (const auto &r : shown)
        QVERIFY(!(r.kind & FormatRange::Math));

    // A live engine reports the hidden math box with its TeX and doc range.
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("energy $E=mc^2$ here");
    const auto boxes = engine.inlineMathBoxes();
    QCOMPARE(boxes.size(), 1);
    const QVariantMap box = boxes.at(0).toMap();
    QCOMPARE(box.value("tex").toString(), QString("E=mc^2"));
    QVERIFY(box.value("docEnd").toInt() > box.value("docStart").toInt());
    QVERIFY(box.value("valid").toBool());
    const MathRenderer::Metrics liveMetrics =
        MathRenderer::measure("E=mc^2", engine.mathFontPixelSize());
    QVERIFY(liveMetrics.valid);
    QVERIFY(qAbs(box.value("width").toReal() - liveMetrics.width) <= 1.0);
    QVERIFY(qAbs(box.value("height").toReal() - liveMetrics.height) <= 1.0);
    QVERIFY(qAbs(box.value("baseline").toReal() - liveMetrics.baseline) <= 1.0);

    // Prose dollars stay literal — no math box for "$5 and $6".
    engine.setMarkdown("it costs $5 and $6 total");
    QCOMPARE(engine.inlineMathBoxes().size(), 0);
}

void TestBlockEditorEngine::testInlineMathReservationUsesRenderedWidth()
{
    QTextDocument doc;
    doc.setTextWidth(1000);
    QFont font = doc.defaultFont();
    font.setPixelSize(15);
    doc.setDefaultFont(font);
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setContentFontPixelSize(15);
    engine.setMarkdown("Text before $x^2$ text after");
    settle();

    const auto boxes = engine.inlineMathBoxes();
    QCOMPARE(boxes.size(), 1);
    const QVariantMap box = boxes.first().toMap();
    const int start = box.value("docStart").toInt();
    const int end = box.value("docEnd").toInt();
    QCOMPARE(doc.toPlainText().mid(start, end - start), QString("x^2"));

    const QTextLine line = doc.firstBlock().layout()->lineForTextPosition(start);
    QVERIFY(line.isValid());
    const qreal reserved = line.cursorToX(end) - line.cursorToX(start);
    const MathRenderer::Metrics metrics =
        MathRenderer::measure("x^2", engine.mathFontPixelSize());
    QVERIFY2(metrics.valid, qPrintable(metrics.error));

    QVERIFY2(qAbs(reserved - metrics.width) <= 2.0,
             qPrintable(QStringLiteral("reserved=%1 rendered=%2")
                            .arg(reserved, 0, 'f', 2)
                            .arg(metrics.width, 0, 'f', 2)));
}

void TestBlockEditorEngine::testInlineMathTallFormulaReservesLineHeight()
{
    QTextDocument doc;
    doc.setTextWidth(1000);
    QFont font = doc.defaultFont();
    font.setPixelSize(15);
    doc.setDefaultFont(font);

    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setContentFontPixelSize(15);
    const QString tex = QStringLiteral("\\frac{a}{b}");
    engine.setMarkdown(QStringLiteral("Text before $%1$ text after").arg(tex));
    settle();

    const auto boxes = engine.inlineMathBoxes();
    QCOMPARE(boxes.size(), 1);
    const QVariantMap box = boxes.first().toMap();
    QVERIFY(box.value(QStringLiteral("reservationValid")).toBool());
    const int start = box.value(QStringLiteral("docStart")).toInt();
    const int end = box.value(QStringLiteral("docEnd")).toInt();
    QCOMPARE(doc.toPlainText().mid(start, end - start), tex);

    const int mathSize = engine.mathFontPixelSize();
    const MathRenderer::Metrics metrics = MathRenderer::measure(tex, mathSize);
    QVERIFY2(metrics.valid, qPrintable(metrics.error));
    const qreal targetHeight =
        metrics.height + 2 * qMax(2, qCeil(mathSize * 0.12));
    const QFontMetricsF baseMetrics(font);
    QVERIFY2(targetHeight > baseMetrics.height(),
             qPrintable(QStringLiteral("target=%1 base=%2")
                            .arg(targetHeight, 0, 'f', 2)
                            .arg(baseMetrics.height(), 0, 'f', 2)));

    const QTextBlock block = doc.findBlock(start);
    QVERIFY(block.isValid());
    const int relStart = start - block.position();
    const int relEnd = end - block.position();
    const QTextLine line = block.layout()->lineForTextPosition(relStart);
    QVERIFY(line.isValid());
    QVERIFY2(line.height() + 1.0 >= targetHeight,
             qPrintable(QStringLiteral("line=%1 target=%2")
                            .arg(line.height(), 0, 'f', 2)
                            .arg(targetHeight, 0, 'f', 2)));

    const qreal reservedWidth =
        line.cursorToX(relEnd) - line.cursorToX(relStart);
    QVERIFY2(qAbs(reservedWidth - metrics.width) <= 2.0,
             qPrintable(QStringLiteral("reserved=%1 rendered=%2")
                            .arg(reservedWidth, 0, 'f', 2)
                            .arg(metrics.width, 0, 'f', 2)));
}

// ============================================================================
// Verbatim mode (code blocks)
// ============================================================================

void TestBlockEditorEngine::testVerbatimDocumentIsMarkdown()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.setVerbatim(true);
    engine.attachDocument(&doc);

    // Marker syntax and multi-line content stay literal — nothing parses
    engine.setMarkdown("x = 1  # **not bold**\n    indented `not code`");
    QCOMPARE(doc.toPlainText(),
             QString("x = 1  # **not bold**\n    indented `not code`"));
}

void TestBlockEditorEngine::testVerbatimNeverReveals()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.setVerbatim(true);
    engine.attachDocument(&doc);
    engine.setMarkdown("a **b** c");
    engine.setCursorActive(true);

    // The cursor lands where a span would be; nothing changes
    engine.setCursorPosition(4);
    settle();
    QCOMPARE(doc.toPlainText(), QString("a **b** c"));
    QCOMPARE(engine.revealedSpan(), -1);
}

void TestBlockEditorEngine::testVerbatimEditsAndMapping()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.setVerbatim(true);
    engine.attachDocument(&doc);
    engine.setMarkdown("line one");

    // Positions map 1:1
    QCOMPARE(engine.toMarkdownPosition(5), 5);
    QCOMPARE(engine.toDocumentPosition(5), 5);
    QCOMPARE(engine.toMarkdownPosition(999), 8);

    // Ranges are literal slices; stripFormatting is the identity
    QCOMPARE(engine.markdownForRange(0, 4), QString("line"));
    QCOMPARE(engine.stripFormatting("**x**"), QString("**x**"));
    QCOMPARE(engine.linkAtDocumentPosition(0), QString());
    QCOMPARE(engine.linkSpanAtCursor(0).value("found").toBool(), false);

    // A user edit (a newline, as the code block's Enter inserts) flows to
    // markdownEdited verbatim
    QSignalSpy editedSpy(&engine, &BlockEditorEngine::markdownEdited);
    QTextCursor cursor(&doc);
    cursor.movePosition(QTextCursor::End);
    cursor.insertText("\nline two");
    QCOMPARE(editedSpy.count(), 1);
    QCOMPARE(engine.markdown(), QString("line one\nline two"));

    // Cut removes the literal slice
    const QVariantMap cut = engine.cutRange(0, 5);
    QCOMPARE(cut.value("markdown").toString(), QString("one\nline two"));
    QCOMPARE(cut.value("cursor").toInt(), 0);
}

// ---- search-match highlighting ----

void TestBlockEditorEngine::testSearchHighlightRangesPlain()
{
    // No spans: document text equals display text in every reveal state,
    // so ranges pass through unchanged; the current flag rides along.
    const QString md = QStringLiteral("plain text here");
    const auto out = BlockEditorEngine::searchHighlightRanges(
        md, {}, {hr(2, 4), hr(6, 4, true)}, false);
    QCOMPARE(out, (QList<HighlightRange>{hr(2, 4), hr(6, 4, true)}));
}

void TestBlockEditorEngine::testSearchHighlightRangesHiddenSpans()
{
    // With nothing revealed the document IS the display text, even when
    // the match crosses a hidden marker boundary or covers span content.
    const auto out = BlockEditorEngine::searchHighlightRanges(
        M, {}, {hr(8, 4), hr(5, 5), hr(17, 2, true)}, false);
    QCOMPARE(out, (QList<HighlightRange>{hr(8, 4), hr(5, 5), hr(17, 2, true)}));
}

void TestBlockEditorEngine::testSearchHighlightRangesRevealedSpan()
{
    // Bold span revealed: document "This is **bold** and it end".
    // "bold" (display 8..12) sits at document 10..14; "is bo" (display
    // 5..10) tints document 5..12 — the opening marker between matched
    // characters tints with them; "and" (display 13..16) shifts past the
    // revealed markers to 17..20.
    const auto out = BlockEditorEngine::searchHighlightRanges(
        M, {0}, {hr(8, 4), hr(5, 5, true), hr(13, 3)}, false);
    QCOMPARE(out, (QList<HighlightRange>{hr(10, 4), hr(5, 7, true), hr(17, 3)}));
}

void TestBlockEditorEngine::testSearchHighlightRangesVerbatim()
{
    // Verbatim: identity, marker-shaped text and newlines included.
    const QString code = QStringLiteral("let x = **y**\nline");
    const auto out = BlockEditorEngine::searchHighlightRanges(
        code, {}, {hr(8, 5), hr(14, 4, true)}, true);
    QCOMPARE(out, (QList<HighlightRange>{hr(8, 5), hr(14, 4, true)}));
}

void TestBlockEditorEngine::testSearchHighlightRangesClampStale()
{
    // Stale matches (an edit outran the queued search recompute) clamp
    // to the display bounds or drop instead of mispainting.
    const QString md = QStringLiteral("short");
    const auto out = BlockEditorEngine::searchHighlightRanges(
        md, {}, {hr(3, 10), hr(7, 2), hr(0, 0)}, false);
    QCOMPARE(out, (QList<HighlightRange>{hr(3, 2)}));
}

void TestBlockEditorEngine::testSearchMatchesPaintAndMergeFormats()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello **world**");
    settle();

    engine.setSearchMatches({
        QVariantMap{{"start", 0}, {"length", 5}, {"current", true}},
        QVariantMap{{"start", 6}, {"length", 5}, {"current", false}},
    });

    // Both matches carry a background tint; the current match's differs.
    const QTextCharFormat current = layoutFormatAt(doc, 2);
    const QTextCharFormat normal = layoutFormatAt(doc, 8);
    QVERIFY(current.background().style() != Qt::NoBrush);
    QVERIFY(normal.background().style() != Qt::NoBrush);
    QVERIFY(current.background().color() != normal.background().color());

    // Merged, not replaced: the bold weight survives under the tint.
    QCOMPARE(normal.fontWeight(), int(QFont::Bold));

    // Between matches: untinted.
    QCOMPARE(layoutFormatAt(doc, 5).background().style(), Qt::NoBrush);

    // Clearing the matches clears the tint but keeps the span styling.
    engine.setSearchMatches({});
    QCOMPARE(layoutFormatAt(doc, 2).background().style(), Qt::NoBrush);
    QCOMPARE(layoutFormatAt(doc, 8).background().style(), Qt::NoBrush);
    QCOMPARE(layoutFormatAt(doc, 8).fontWeight(), int(QFont::Bold));
}

void TestBlockEditorEngine::testSearchHighlightFollowsRevealTransition()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("Hello **world**");
    settle();

    engine.setSearchMatches({
        QVariantMap{{"start", 6}, {"length", 5}, {"current", false}},
    });
    // Hidden: display "Hello world", tint on 6..10.
    QVERIFY(layoutFormatAt(doc, 6).background().style() != Qt::NoBrush);
    QVERIFY(layoutFormatAt(doc, 10).background().style() != Qt::NoBrush);
    QCOMPARE(layoutFormatAt(doc, 5).background().style(), Qt::NoBrush);

    // Reveal the span: the tint moves with the matched characters —
    // document "Hello **world**", "world" now at 8..12, markers bare.
    engine.setCursorActive(true);
    engine.setCursorPosition(8);
    settle();
    QCOMPARE(doc.toPlainText(), QString("Hello **world**"));
    QVERIFY(layoutFormatAt(doc, 8).background().style() != Qt::NoBrush);
    QVERIFY(layoutFormatAt(doc, 12).background().style() != Qt::NoBrush);
    QCOMPARE(layoutFormatAt(doc, 7).background().style(), Qt::NoBrush);
    QCOMPARE(layoutFormatAt(doc, 13).background().style(), Qt::NoBrush);

    // And back.
    engine.setCursorActive(false);
    settle();
    QCOMPARE(doc.toPlainText(), QString("Hello world"));
    QVERIFY(layoutFormatAt(doc, 6).background().style() != Qt::NoBrush);
    QCOMPARE(layoutFormatAt(doc, 11).background().style(), Qt::NoBrush);
}

// Sup/sub render through QTextCharFormat vertical alignment; Qt derives
// the smaller size itself.
void TestBlockEditorEngine::testSupSubVerticalAlignment()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("E=mc^2^ and H~3~O");

    const int supPos = doc.toPlainText().indexOf("2");
    const int subPos = doc.toPlainText().indexOf("3");
    QCOMPARE(layoutFormatAt(doc, supPos).verticalAlignment(),
             QTextCharFormat::AlignSuperScript);
    QCOMPARE(layoutFormatAt(doc, subPos).verticalAlignment(),
             QTextCharFormat::AlignSubScript);
    // Ordinary text stays on the baseline.
    QCOMPARE(layoutFormatAt(doc, doc.toPlainText().indexOf("and"))
                 .verticalAlignment(),
             QTextCharFormat::AlignNormal);
}

// The toolbar's state source: combined flags of the span chain at a
// document position, ends inclusive.
void TestBlockEditorEngine::testFormatFlagsAtDocumentPosition()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown("a **bo *it* ld** z");
    // Display: "a bo it ld z"; bold spans display 2..10, italic 5..7.

    QCOMPARE(engine.formatFlagsAtDocumentPosition(0), 0);
    QCOMPARE(engine.formatFlagsAtDocumentPosition(3),
             int(SpanFormat::Bold));
    QCOMPARE(engine.formatFlagsAtDocumentPosition(6),
             int(SpanFormat::Bold | SpanFormat::Italic));
    // Span edges are inclusive, matching the reveal rule: at the
    // bold span's very start the state already reads bold.
    QCOMPARE(engine.formatFlagsAtDocumentPosition(2),
             int(SpanFormat::Bold));

    // Verbatim mode: everything is literal, nothing reports flags.
    BlockEditorEngine verbatim;
    QTextDocument codeDoc;
    verbatim.attachDocument(&codeDoc);
    verbatim.setVerbatim(true);
    verbatim.setMarkdown("x = a ** b");
    QCOMPARE(verbatim.formatFlagsAtDocumentPosition(7), 0);
}

void TestBlockEditorEngine::testMathSpanRangeIn()
{
    // "see $x^2$ here": math span markdown 4..9, content 5..8.
    const QString md = QStringLiteral("see $x^2$ here");

    // Outside: prose, the opening marker, and after the closing marker.
    QVERIFY(!BlockEditorEngine::mathSpanRangeIn(md, 0)
                 .value("found").toBool());
    QVERIFY(!BlockEditorEngine::mathSpanRangeIn(md, 4)
                 .value("found").toBool());
    QVERIFY(!BlockEditorEngine::mathSpanRangeIn(md, 9)
                 .value("found").toBool());
    QVERIFY(!BlockEditorEngine::mathSpanRangeIn(md, 12)
                 .value("found").toBool());

    // Inside, both content edges inclusive: right after the opening $
    // (5) and right before the closing $ (8) both count — the trigger
    // must fire for a \ typed at either edge.
    for (int pos : { 5, 7, 8 }) {
        const QVariantMap map = BlockEditorEngine::mathSpanRangeIn(md, pos);
        QVERIFY2(map.value("found").toBool(),
                 qPrintable(QString::number(pos)));
        QCOMPARE(map.value("mdStart").toInt(), 4);
        QCOMPARE(map.value("mdEnd").toInt(), 9);
        QCOMPARE(map.value("contentStart").toInt(), 5);
        QCOMPARE(map.value("contentEnd").toInt(), 8);
        QCOMPARE(map.value("tex").toString(), QStringLiteral("x^2"));
    }

    // The parser's own rules govern what is a span: "$5 and $6" is prose
    // (digit-after rule), an unclosed $ is prose, and — the transitional
    // state the trigger pre-checks around — "$\$" reads its closing
    // dollar as escaped, so it is not a span either.
    QVERIFY(!BlockEditorEngine::mathSpanRangeIn(
                 QStringLiteral("$5 and $6"), 1).value("found").toBool());
    QVERIFY(!BlockEditorEngine::mathSpanRangeIn(
                 QStringLiteral("a $x + y"), 4).value("found").toBool());
    QVERIFY(!BlockEditorEngine::mathSpanRangeIn(
                 QStringLiteral("$\\$"), 1).value("found").toBool());
}

void TestBlockEditorEngine::testMathSpanRangeAtDocument()
{
    QTextDocument doc;
    BlockEditorEngine engine;
    engine.attachDocument(&doc);
    engine.setMarkdown(QStringLiteral("see $x^2$ here"));

    // Reveal transitions are event-loop-deferred, so without one the span
    // stays hidden: document text is "see x^2 here" and the content maps
    // to display 4..7. The invokable tracks whichever reveal state is
    // current — under QML the revealed coordinates come out instead.
    const QVariantMap map = engine.mathSpanRangeAt(6);
    QVERIFY(map.value("found").toBool());
    QCOMPARE(map.value("tex").toString(), QStringLiteral("x^2"));
    QCOMPARE(map.value("docContentStart").toInt(), 4);
    QCOMPARE(map.value("docContentEnd").toInt(), 7);

    // Verbatim mode never reports a span.
    BlockEditorEngine verbatim;
    QTextDocument codeDoc;
    verbatim.attachDocument(&codeDoc);
    verbatim.setVerbatim(true);
    verbatim.setMarkdown(QStringLiteral("$x^2$"));
    QVERIFY(!verbatim.mathSpanRangeAt(2).value("found").toBool());
}

void TestBlockEditorEngine::testShouldAutoPairDollarRules()
{
    using E = BlockEditorEngine;

    // Plain prose, end of line: pair.
    QVERIFY(E::shouldAutoPairDollarIn(QStringLiteral("It costs "), 9));
    QVERIFY(E::shouldAutoPairDollarIn(QString(), 0));

    // Unmatched unescaped $ left of the caret: this $ closes it.
    QVERIFY(!E::shouldAutoPairDollarIn(QStringLiteral("a $x + y "), 9));
    // Balanced dollars to the left pair again.
    QVERIFY(E::shouldAutoPairDollarIn(QStringLiteral("$x$ and "), 8));
    // An escaped \$ is prose, not an opener.
    QVERIFY(E::shouldAutoPairDollarIn(QStringLiteral("costs \\$5 "), 10));

    // Escape: right after a backslash the user is typing the literal \$.
    QVERIFY(!E::shouldAutoPairDollarIn(QStringLiteral("a \\"), 3));
    // A double backslash does not escape (parity rule).
    QVERIFY(E::shouldAutoPairDollarIn(QStringLiteral("a \\\\"), 4));

    // Inside an inline code span, $ is always literal.
    QVERIFY(!E::shouldAutoPairDollarIn(QStringLiteral("`code` x"), 3));
    QVERIFY(E::shouldAutoPairDollarIn(QStringLiteral("`code` "), 7));

    // A letter, digit, or $ right of the caret suppresses: a dollar in
    // front of existing text is a price ("costs $|5").
    QVERIFY(!E::shouldAutoPairDollarIn(QStringLiteral("costs 5"), 6));
    QVERIFY(!E::shouldAutoPairDollarIn(QStringLiteral("word"), 2));
    // Punctuation or space after the caret still pairs.
    QVERIFY(E::shouldAutoPairDollarIn(QStringLiteral("x , y"), 2));

    // Selection wrap skips the following-character rule (the selection is
    // what follows) but keeps the others.
    QVERIFY(E::shouldAutoPairDollarIn(QStringLiteral("costs 5"), 6, true));
    QVERIFY(!E::shouldAutoPairDollarIn(QStringLiteral("a $x y z"), 5, true));
}

void TestBlockEditorEngine::testShouldAutoPairDollarVerbatim()
{
    BlockEditorEngine verbatim;
    QTextDocument doc;
    verbatim.attachDocument(&doc);
    verbatim.setVerbatim(true);
    verbatim.setMarkdown(QStringLiteral("price = "));
    QVERIFY(!verbatim.shouldAutoPairDollar(8));
}

QTEST_MAIN(TestBlockEditorEngine)
#include "test_blockeditorengine.moc"
