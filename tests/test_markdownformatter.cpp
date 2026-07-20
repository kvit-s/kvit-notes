// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include "markdownformatter.h"

class TestMarkdownFormatter : public QObject
{
    Q_OBJECT

private slots:
    // Basic HTML conversion tests
    void testPlainText();
    void testBasicBold();
    void testBasicItalic();
    void testBoldItalic();
    void testMultipleBoldRegions();
    void testMultipleItalicRegions();
    void testMixedFormatting();
    void testNestedBoldItalic();

    // Edge cases
    void testUnmatchedBoldMarkers();
    void testUnmatchedItalicMarkers();
    void testEmptyBoldMarkers();
    void testEmptyItalicMarkers();
    void testAdjacentFormatting();
    void testFormattingAtStartEnd();
    void testHtmlEscaping();

    // Span parsing tests
    void testGetSpansEmpty();
    void testGetSpansBasicBold();
    void testGetSpansBasicItalic();
    void testGetSpansMultiple();

    // Registry span types beyond bold/italic
    void testParseSpansNewTypes_data();
    void testParseSpansNewTypes();
    void testParseSpansNewTypeEdges();
    void testParseSpansAdjacentNewTypes();
    void testParseSpansCodeVerbatim();

    // "_" variants with the word-boundary rule
    void testParseSpansUnderscoreVariants();
    void testUnderscoreWordBoundaryRule();
    void testToggleRemovesUnderscoreVariant();

    // Nested spans
    void testParseSpansNestedChildren();

    // Generic span-type commands
    void testToggleSpanTypePerType_data();
    void testToggleSpanTypePerType();

    // Phase 9 sup/sub rules
    void testSupSubSpaceAndFamilyRules();
    void testToggleSpanTypeNestedDeepestWins();
    void testRemoveSpanTypeComposite();

    // Links and autolinks
    void testParseSpansLink();
    void testParseSpansLinkEdges();
    void testParseSpansAutolink();
    void testRemoveSpanTypeUnlinks();

    // Wiki-links
    void testParseSpansWikiLink();
    void testParseSpansWikiLinkAliasAndHeading();
    void testWikiLinkPrecedenceAndEdges();

    // Text color span
    void testParseSpansColor();
    void testColorNearMissesStayLiteral();
    void testColorSpanNesting();
    void testColorSpanRoundTrip();
    void testApplyColor();
    void testRecolorInPlace();
    void testRemoveColor();
    void testColorSpanAt();

    // Inline math (phase11 decision 10)
    void testParseSpansInlineMath();
    void testInlineMathCurrencyCorpus_data();
    void testInlineMathCurrencyCorpus();

    // Emoji as opaque span content
    void testEmojiInsideAndBetweenSpans();

    // Backslash escapes
    void testEscapedDelimitersStayLiteral();
    void testEscapeSpansConcealBackslash();
    void testDoubleBackslashConsumesEscape();
    void testEscapedClosersAreSkipped();
    void testEscapedDollarBehavior();

    // Cursor position tests
    void testIsInsideFormattedRegionOutside();
    void testIsInsideFormattedRegionInside();
    void testIsInsideFormattedRegionAtBoundary();
    void testGetSpanAtCursorNone();
    void testGetSpanAtCursorBold();
    void testGetSpanAtCursorItalic();

    // Position mapping tests
    void testMarkdownToDisplayPositionPlain();
    void testMarkdownToDisplayPositionWithBold();
    void testMarkdownToDisplayPositionWithItalic();
    void testMarkdownToDisplayPositionMultipleFormats();
    void testDisplayToMarkdownPositionPlain();
    void testDisplayToMarkdownPositionWithBold();
    void testDisplayToMarkdownPositionWithItalic();

    // HTML with reveal tests

    // Formatting application tests
    void testApplyBoldToSelection();
    void testApplyBoldAtCursor();
    void testApplyItalicToSelection();
    void testApplyItalicAtCursor();
    void testToggleBoldAdd();
    void testToggleBoldRemove();
    void testToggleItalicAdd();
    void testToggleItalicRemove();
};

// Basic HTML conversion tests

void TestMarkdownFormatter::testPlainText()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("Hello World"), QString("Hello World"));
    QCOMPARE(formatter.toHtml(""), QString(""));
    QCOMPARE(formatter.toHtml("Just plain text"), QString("Just plain text"));
}

void TestMarkdownFormatter::testBasicBold()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("**bold**"), QString("<b>bold</b>"));
    QCOMPARE(formatter.toHtml("text **bold** more"), QString("text <b>bold</b> more"));
    QCOMPARE(formatter.toHtml("**start** of line"), QString("<b>start</b> of line"));
    QCOMPARE(formatter.toHtml("end of **line**"), QString("end of <b>line</b>"));
}

void TestMarkdownFormatter::testBasicItalic()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("*italic*"), QString("<i>italic</i>"));
    QCOMPARE(formatter.toHtml("text *italic* more"), QString("text <i>italic</i> more"));
    QCOMPARE(formatter.toHtml("*start* of line"), QString("<i>start</i> of line"));
    QCOMPARE(formatter.toHtml("end of *line*"), QString("end of <i>line</i>"));
}

void TestMarkdownFormatter::testBoldItalic()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("***both***"), QString("<b><i>both</i></b>"));
    QCOMPARE(formatter.toHtml("text ***both*** more"), QString("text <b><i>both</i></b> more"));
}

void TestMarkdownFormatter::testMultipleBoldRegions()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("**one** and **two**"), QString("<b>one</b> and <b>two</b>"));
    QCOMPARE(formatter.toHtml("**a** **b** **c**"), QString("<b>a</b> <b>b</b> <b>c</b>"));
}

void TestMarkdownFormatter::testMultipleItalicRegions()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("*one* and *two*"), QString("<i>one</i> and <i>two</i>"));
    QCOMPARE(formatter.toHtml("*a* *b* *c*"), QString("<i>a</i> <i>b</i> <i>c</i>"));
}

void TestMarkdownFormatter::testMixedFormatting()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("**bold** and *italic*"), QString("<b>bold</b> and <i>italic</i>"));
    QCOMPARE(formatter.toHtml("*italic* then **bold**"), QString("<i>italic</i> then <b>bold</b>"));
}

void TestMarkdownFormatter::testNestedBoldItalic()
{
    MarkdownFormatter formatter;
    // Bold containing italic: **bold *and italic* text**
    QCOMPARE(formatter.toHtml("**bold *and italic* text**"), QString("<b>bold <i>and italic</i> text</b>"));
    // Italic containing bold: *italic **and bold** text*
    QCOMPARE(formatter.toHtml("*italic **and bold** text*"), QString("<i>italic <b>and bold</b> text</i>"));
}

// Edge cases

void TestMarkdownFormatter::testUnmatchedBoldMarkers()
{
    MarkdownFormatter formatter;
    // Unmatched markers should be treated as literal text
    QCOMPARE(formatter.toHtml("**unclosed"), QString("**unclosed"));
    QCOMPARE(formatter.toHtml("unclosed**"), QString("unclosed**"));
    QCOMPARE(formatter.toHtml("**one** and **unclosed"), QString("<b>one</b> and **unclosed"));
}

void TestMarkdownFormatter::testUnmatchedItalicMarkers()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("*unclosed"), QString("*unclosed"));
    QCOMPARE(formatter.toHtml("unclosed*"), QString("unclosed*"));
    QCOMPARE(formatter.toHtml("*one* and *unclosed"), QString("<i>one</i> and *unclosed"));
}

void TestMarkdownFormatter::testEmptyBoldMarkers()
{
    MarkdownFormatter formatter;
    // Empty markers should be treated as literal
    QCOMPARE(formatter.toHtml("****"), QString("****"));
    QCOMPARE(formatter.toHtml("text **** more"), QString("text **** more"));
}

void TestMarkdownFormatter::testEmptyItalicMarkers()
{
    MarkdownFormatter formatter;
    // Single asterisk pair with nothing inside - but we need at least 2 for bold
    // ** is not italic, it's an empty bold marker
    // For single asterisks: ** is actually a failed bold (treated as literal)
    QCOMPARE(formatter.toHtml("**"), QString("**"));
}

void TestMarkdownFormatter::testAdjacentFormatting()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("**bold***italic*"), QString("<b>bold</b><i>italic</i>"));
    QCOMPARE(formatter.toHtml("*italic***bold**"), QString("<i>italic</i><b>bold</b>"));
}

void TestMarkdownFormatter::testFormattingAtStartEnd()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.toHtml("**bold**"), QString("<b>bold</b>"));
    QCOMPARE(formatter.toHtml("*italic*"), QString("<i>italic</i>"));
}

void TestMarkdownFormatter::testHtmlEscaping()
{
    MarkdownFormatter formatter;
    // HTML entities should be escaped
    QCOMPARE(formatter.toHtml("<script>alert('xss')</script>"),
             QString("&lt;script&gt;alert('xss')&lt;/script&gt;"));
    QCOMPARE(formatter.toHtml("**<b>nested</b>**"), QString("<b>&lt;b&gt;nested&lt;/b&gt;</b>"));
    QCOMPARE(formatter.toHtml("a & b"), QString("a &amp; b"));
    QCOMPARE(formatter.toHtml("\"quotes\""), QString("&quot;quotes&quot;"));
}

// Span parsing tests

void TestMarkdownFormatter::testGetSpansEmpty()
{
    MarkdownFormatter formatter;
    auto spans = formatter.getFormattedSpans("");
    QCOMPARE(spans.size(), 0);

    spans = formatter.getFormattedSpans("plain text");
    QCOMPARE(spans.size(), 0);
}

void TestMarkdownFormatter::testGetSpansBasicBold()
{
    MarkdownFormatter formatter;
    auto spans = formatter.getFormattedSpans("Hello **world**");
    QCOMPARE(spans.size(), 1);

    auto span = spans[0].toMap();
    QCOMPARE(span["type"].toString(), QString("bold"));
    QCOMPARE(span["start"].toInt(), 6);
    QCOMPARE(span["end"].toInt(), 15);
    QCOMPARE(span["displayStart"].toInt(), 6);
    QCOMPARE(span["displayEnd"].toInt(), 11);
}

void TestMarkdownFormatter::testGetSpansBasicItalic()
{
    MarkdownFormatter formatter;
    auto spans = formatter.getFormattedSpans("Hello *world*");
    QCOMPARE(spans.size(), 1);

    auto span = spans[0].toMap();
    QCOMPARE(span["type"].toString(), QString("italic"));
    QCOMPARE(span["start"].toInt(), 6);
    QCOMPARE(span["end"].toInt(), 13);
    QCOMPARE(span["displayStart"].toInt(), 6);
    QCOMPARE(span["displayEnd"].toInt(), 11);
}

void TestMarkdownFormatter::testGetSpansMultiple()
{
    MarkdownFormatter formatter;
    auto spans = formatter.getFormattedSpans("**bold** and *italic*");
    QCOMPARE(spans.size(), 2);

    auto span1 = spans[0].toMap();
    QCOMPARE(span1["type"].toString(), QString("bold"));

    auto span2 = spans[1].toMap();
    QCOMPARE(span2["type"].toString(), QString("italic"));
}

// Registry span types beyond bold/italic. Each row exercises the
// generic delimiter matcher for one new type over "x <m>ab<m> y".

void TestMarkdownFormatter::testParseSpansNewTypes_data()
{
    QTest::addColumn<QString>("marker");
    QTest::addColumn<QString>("type");

    QTest::newRow("strike") << "~~" << "strike";
    QTest::newRow("highlight") << "==" << "highlight";
    QTest::newRow("underline") << "++" << "underline";
    QTest::newRow("code") << "`" << "code";
    // Phase 9: Pandoc sup/sub.
    QTest::newRow("superscript") << "^" << "superscript";
    QTest::newRow("subscript") << "~" << "subscript";
}

void TestMarkdownFormatter::testParseSpansNewTypes()
{
    QFETCH(QString, marker);
    QFETCH(QString, type);

    MarkdownFormatter formatter;
    const int L = marker.length();
    const QString md = "x " + marker + "ab" + marker + " y";
    const auto spans = formatter.parseSpans(md);

    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, type);
    QCOMPARE(spans[0].start, 2);
    QCOMPARE(spans[0].end, 2 + 2 * L + 2);
    QCOMPARE(spans[0].openLen, L);
    QCOMPARE(spans[0].closeLen, L);
    QCOMPARE(spans[0].displayStart, 2);
    QCOMPARE(spans[0].displayEnd, 4);
    QCOMPARE(spans[0].rawText, marker + "ab" + marker);
}

void TestMarkdownFormatter::testParseSpansNewTypeEdges()
{
    MarkdownFormatter formatter;
    // Unclosed markers stay literal.
    QCOMPARE(formatter.parseSpans("~~open").size(), 0);
    QCOMPARE(formatter.parseSpans("==open").size(), 0);
    QCOMPARE(formatter.parseSpans("++open").size(), 0);
    QCOMPARE(formatter.parseSpans("`open").size(), 0);
    // Empty spans stay literal (content must be non-empty).
    QCOMPARE(formatter.parseSpans("~~~~").size(), 0);
    QCOMPARE(formatter.parseSpans("====").size(), 0);
    QCOMPARE(formatter.parseSpans("++++").size(), 0);
    QCOMPARE(formatter.parseSpans("``").size(), 0);
}

void TestMarkdownFormatter::testParseSpansAdjacentNewTypes()
{
    MarkdownFormatter formatter;
    const auto spans = formatter.parseSpans("~~a~~==b==++c++`d`**e**");
    QCOMPARE(spans.size(), 5);
    QCOMPARE(spans[0].type, QString("strike"));
    QCOMPARE(spans[1].type, QString("highlight"));
    QCOMPARE(spans[2].type, QString("underline"));
    QCOMPARE(spans[3].type, QString("code"));
    QCOMPARE(spans[4].type, QString("bold"));
    // Display positions pack the stripped contents tightly: "abcde".
    for (int i = 0; i < spans.size(); ++i) {
        QCOMPARE(spans[i].displayStart, i);
        QCOMPARE(spans[i].displayEnd, i + 1);
    }
}

void TestMarkdownFormatter::testParseSpansCodeVerbatim()
{
    MarkdownFormatter formatter;
    // A code span opening first swallows other markers as literal content.
    auto spans = formatter.parseSpans("`a **b** c`");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("code"));
    QCOMPARE(spans[0].rawText, QString("`a **b** c`"));

    // An earlier-starting span wins over a later code span — the scan is
    // left-to-right. The code span nests as a child of the bold content.
    spans = formatter.parseSpans("**a `b` c**");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("bold"));
    QCOMPARE(spans[0].children.size(), 1);
    QCOMPARE(spans[0].children[0].type, QString("code"));
}

// Nested spans: children carry absolute markdown and display
// coordinates; code content never produces children.
void TestMarkdownFormatter::testParseSpansNestedChildren()
{
    MarkdownFormatter formatter;

    // "**bo *it* ld**" — child italic at md [5,9), display [3,5).
    auto spans = formatter.parseSpans("**bo *it* ld**");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("bold"));
    QCOMPARE(spans[0].displayStart, 0);
    QCOMPARE(spans[0].displayEnd, 8); // "bo it ld"
    QCOMPARE(spans[0].children.size(), 1);
    const auto &child = spans[0].children[0];
    QCOMPARE(child.type, QString("italic"));
    QCOMPARE(child.start, 5);
    QCOMPARE(child.end, 9);
    QCOMPARE(child.displayStart, 3);
    QCOMPARE(child.displayEnd, 5);

    // Grandchildren: "*a **b `c` d** e*".
    spans = formatter.parseSpans("*a **b `c` d** e*");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].children.size(), 1);
    QCOMPARE(spans[0].children[0].type, QString("bold"));
    QCOMPARE(spans[0].children[0].children.size(), 1);
    QCOMPARE(spans[0].children[0].children[0].type, QString("code"));

    // Code content is verbatim: no children, ever.
    spans = formatter.parseSpans("`a **b** c`");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].children.size(), 0);

    // Spans after a nested span keep correct display coordinates (the
    // nested markers count toward the stripped offset).
    spans = formatter.parseSpans("**a *b* c** *d*");
    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans[1].type, QString("italic"));
    // display "a b c d": second span's content is the trailing "d".
    QCOMPARE(spans[1].displayStart, 6);
    QCOMPARE(spans[1].displayEnd, 7);
}

// "_" variants: same type names and flags as the "*" family, matched
// only at word boundaries.

void TestMarkdownFormatter::testParseSpansUnderscoreVariants()
{
    MarkdownFormatter formatter;

    auto spans = formatter.parseSpans("a __bold__ z");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("bold"));
    QCOMPARE(spans[0].openLen, 2);
    QCOMPARE(spans[0].rawText, QString("__bold__"));

    spans = formatter.parseSpans("a _it_ z");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("italic"));

    spans = formatter.parseSpans("a ___bi___ z");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("bolditalic"));

    // Both variants of the same type coexist and parse identically.
    spans = formatter.parseSpans("**a** __b__");
    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans[0].type, QString("bold"));
    QCOMPARE(spans[1].type, QString("bold"));
    QCOMPARE(spans[1].displayStart, 2);
    QCOMPARE(spans[1].displayEnd, 3);
}

void TestMarkdownFormatter::testUnderscoreWordBoundaryRule()
{
    MarkdownFormatter formatter;
    // Intra-word underscores never open or close a span — the rule that
    // makes the "_" variants safe to enable at all.
    QCOMPARE(formatter.parseSpans("snake_case_name").size(), 0);
    QCOMPARE(formatter.parseSpans("one_two three_four").size(), 0);
    QCOMPARE(formatter.parseSpans("x_y_z").size(), 0);
    // A closing candidate followed by a word character is skipped...
    QCOMPARE(formatter.parseSpans("__a__b").size(), 0);
    // ...while a later valid closing still matches; inner "_" stays
    // literal content.
    const auto spans = formatter.parseSpans("_snake_case ok_");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].rawText, QString("_snake_case ok_"));
    // Unclosed variants stay literal.
    QCOMPARE(formatter.parseSpans("_lead").size(), 0);
    QCOMPARE(formatter.parseSpans("trail_").size(), 0);
    // The "*" family is exempt: intra-word asterisks still match.
    QCOMPARE(formatter.parseSpans("a*b*c").size(), 1);
}

void TestMarkdownFormatter::testToggleRemovesUnderscoreVariant()
{
    MarkdownFormatter formatter;
    // Toggling bold inside a "__" span removes it (same type), while
    // applying bold to plain text emits the canonical "**" markers.
    QCOMPARE(formatter.toggleBold("Hello __world__", 8, 13), QString("Hello world"));
    QCOMPARE(formatter.toggleBold("Hello world", 6, 11), QString("Hello **world**"));
    QCOMPARE(formatter.toggleItalic("Hello _world_", 7, 12), QString("Hello world"));
}

// Cursor position tests

void TestMarkdownFormatter::testIsInsideFormattedRegionOutside()
{
    MarkdownFormatter formatter;
    // "Hello **world**"
    //  01234567...
    // Positions 0-5 are outside the bold region
    QVERIFY(!formatter.isInsideFormattedRegion("Hello **world**", 0));
    QVERIFY(!formatter.isInsideFormattedRegion("Hello **world**", 3));
    QVERIFY(!formatter.isInsideFormattedRegion("Hello **world**", 5));
}

void TestMarkdownFormatter::testIsInsideFormattedRegionInside()
{
    MarkdownFormatter formatter;
    // "Hello **world**"
    //  0123456789...
    // Bold span is at positions 6-14 (including markers)
    QVERIFY(formatter.isInsideFormattedRegion("Hello **world**", 7));  // Inside ** markers
    QVERIFY(formatter.isInsideFormattedRegion("Hello **world**", 10)); // Middle of "world"
    QVERIFY(formatter.isInsideFormattedRegion("Hello **world**", 13)); // Before closing **
}

void TestMarkdownFormatter::testIsInsideFormattedRegionAtBoundary()
{
    MarkdownFormatter formatter;
    // "Hello **world**"
    // Position 6 is the first *, 14 is the last *
    QVERIFY(formatter.isInsideFormattedRegion("Hello **world**", 6));  // At opening **
    QVERIFY(formatter.isInsideFormattedRegion("Hello **world**", 14)); // At closing *
}

void TestMarkdownFormatter::testGetSpanAtCursorNone()
{
    MarkdownFormatter formatter;
    auto span = formatter.getSpanAtCursor("Hello **world**", 3);
    QVERIFY(span.isEmpty());

    span = formatter.getSpanAtCursor("plain text", 5);
    QVERIFY(span.isEmpty());
}

void TestMarkdownFormatter::testGetSpanAtCursorBold()
{
    MarkdownFormatter formatter;
    auto span = formatter.getSpanAtCursor("Hello **world**", 10);
    QVERIFY(!span.isEmpty());
    QCOMPARE(span["type"].toString(), QString("bold"));
    QCOMPARE(span["start"].toInt(), 6);
    QCOMPARE(span["end"].toInt(), 15);
}

void TestMarkdownFormatter::testGetSpanAtCursorItalic()
{
    MarkdownFormatter formatter;
    auto span = formatter.getSpanAtCursor("Hello *world*", 9);
    QVERIFY(!span.isEmpty());
    QCOMPARE(span["type"].toString(), QString("italic"));
}

// Position mapping tests

void TestMarkdownFormatter::testMarkdownToDisplayPositionPlain()
{
    MarkdownFormatter formatter;
    // Plain text: positions map 1:1
    QCOMPARE(formatter.markdownToDisplayPosition("Hello World", 0), 0);
    QCOMPARE(formatter.markdownToDisplayPosition("Hello World", 5), 5);
    QCOMPARE(formatter.markdownToDisplayPosition("Hello World", 11), 11);
}

void TestMarkdownFormatter::testMarkdownToDisplayPositionWithBold()
{
    MarkdownFormatter formatter;
    // "Hello **world** end"
    //  MD:     0123456789...
    // Display: "Hello world end"
    //          0123456789...

    // Before bold: 1:1 mapping
    QCOMPARE(formatter.markdownToDisplayPosition("Hello **world** end", 0), 0);
    QCOMPARE(formatter.markdownToDisplayPosition("Hello **world** end", 5), 5);

    // Inside bold markers (positions 6-7 are "**"): map to display pos 6
    QCOMPARE(formatter.markdownToDisplayPosition("Hello **world** end", 6), 6);
    QCOMPARE(formatter.markdownToDisplayPosition("Hello **world** end", 7), 6);

    // Inside "world" (md positions 8-12): subtract 2 for opening **
    QCOMPARE(formatter.markdownToDisplayPosition("Hello **world** end", 8), 6);  // 'w'
    QCOMPARE(formatter.markdownToDisplayPosition("Hello **world** end", 12), 10); // 'd'

    // After bold (md pos 15+): subtract 4 for both ** markers
    QCOMPARE(formatter.markdownToDisplayPosition("Hello **world** end", 15), 11);
    QCOMPARE(formatter.markdownToDisplayPosition("Hello **world** end", 19), 15);
}

void TestMarkdownFormatter::testMarkdownToDisplayPositionWithItalic()
{
    MarkdownFormatter formatter;
    // "Hello *world* end"
    // MD positions: 0-5 "Hello ", 6 "*", 7-11 "world", 12 "*", 13-16 " end"
    // Display: "Hello world end" (without asterisks)

    QCOMPARE(formatter.markdownToDisplayPosition("Hello *world* end", 0), 0);
    QCOMPARE(formatter.markdownToDisplayPosition("Hello *world* end", 5), 5);
    QCOMPARE(formatter.markdownToDisplayPosition("Hello *world* end", 6), 6);  // At *
    QCOMPARE(formatter.markdownToDisplayPosition("Hello *world* end", 7), 6);  // 'w'
    QCOMPARE(formatter.markdownToDisplayPosition("Hello *world* end", 11), 10); // 'd'
    QCOMPARE(formatter.markdownToDisplayPosition("Hello *world* end", 13), 11); // After *
}

void TestMarkdownFormatter::testMarkdownToDisplayPositionMultipleFormats()
{
    MarkdownFormatter formatter;
    // "**bold** and *italic*"
    // MD: 0-7 "**bold**", 8-12 " and ", 13-20 "*italic*"
    // Display: "bold and italic"

    QCOMPARE(formatter.markdownToDisplayPosition("**bold** and *italic*", 0), 0);  // At **
    QCOMPARE(formatter.markdownToDisplayPosition("**bold** and *italic*", 2), 0);  // 'b'
    QCOMPARE(formatter.markdownToDisplayPosition("**bold** and *italic*", 8), 4);  // After **bold**
    QCOMPARE(formatter.markdownToDisplayPosition("**bold** and *italic*", 13), 9); // After " and "
}

void TestMarkdownFormatter::testDisplayToMarkdownPositionPlain()
{
    MarkdownFormatter formatter;
    QCOMPARE(formatter.displayToMarkdownPosition("Hello World", 0), 0);
    QCOMPARE(formatter.displayToMarkdownPosition("Hello World", 5), 5);
    QCOMPARE(formatter.displayToMarkdownPosition("Hello World", 11), 11);
}

void TestMarkdownFormatter::testDisplayToMarkdownPositionWithBold()
{
    MarkdownFormatter formatter;
    // Display "Hello world end" -> MD "Hello **world** end"

    QCOMPARE(formatter.displayToMarkdownPosition("Hello **world** end", 0), 0);
    QCOMPARE(formatter.displayToMarkdownPosition("Hello **world** end", 5), 5);
    QCOMPARE(formatter.displayToMarkdownPosition("Hello **world** end", 6), 8);   // 'w' in display maps to pos 8 in MD
    QCOMPARE(formatter.displayToMarkdownPosition("Hello **world** end", 10), 12); // 'd' in display
    QCOMPARE(formatter.displayToMarkdownPosition("Hello **world** end", 11), 15); // After "world" in display
}

void TestMarkdownFormatter::testDisplayToMarkdownPositionWithItalic()
{
    MarkdownFormatter formatter;
    // Display "Hello world end" -> MD "Hello *world* end"

    QCOMPARE(formatter.displayToMarkdownPosition("Hello *world* end", 0), 0);
    QCOMPARE(formatter.displayToMarkdownPosition("Hello *world* end", 6), 7);  // 'w' in display -> pos 7 in MD
    QCOMPARE(formatter.displayToMarkdownPosition("Hello *world* end", 11), 13); // After "world"
}

// Links and autolinks

void TestMarkdownFormatter::testParseSpansLink()
{
    MarkdownFormatter formatter;

    // "[text](http://a)" — open "[", close "](http://a)".
    auto spans = formatter.parseSpans("[text](http://a)");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("link"));
    QCOMPARE(spans[0].start, 0);
    QCOMPARE(spans[0].end, 16);
    QCOMPARE(spans[0].openLen, 1);
    QCOMPARE(spans[0].closeLen, 11);
    QCOMPARE(spans[0].url, QString("http://a"));
    QCOMPARE(spans[0].displayStart, 0);
    QCOMPARE(spans[0].displayEnd, 4); // display "text"
    QCOMPARE(spans[0].openMarker(), QString("["));
    QCOMPARE(spans[0].closeMarker(), QString("](http://a)"));

    // Position math with surrounding text.
    spans = formatter.parseSpans("go [a](u) now");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].start, 3);
    QCOMPARE(spans[0].end, 9);
    QCOMPARE(spans[0].displayStart, 3);
    QCOMPARE(spans[0].displayEnd, 4);

    // Formatted link text nests as children.
    spans = formatter.parseSpans("[**b**](u)");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].children.size(), 1);
    QCOMPARE(spans[0].children[0].type, QString("bold"));
    QCOMPARE(spans[0].displayEnd, 1); // display "b"
}

void TestMarkdownFormatter::testParseSpansLinkEdges()
{
    MarkdownFormatter formatter;
    // Unclosed / malformed forms stay literal.
    QCOMPARE(formatter.parseSpans("[a](b").size(), 0);
    QCOMPARE(formatter.parseSpans("[a]b").size(), 0);
    QCOMPARE(formatter.parseSpans("[a] (b)").size(), 0);
    // Empty text would be an invisible, unrevealable span.
    QCOMPARE(formatter.parseSpans("[](u)").size(), 0);
    // URLs with spaces or parens stay literal.
    QCOMPARE(formatter.parseSpans("[a](b c)").size(), 0);
    // Brackets inside the text stay literal.
    QCOMPARE(formatter.parseSpans("[a[x]](u)").size(), 0);
    // Empty URL parses: visible, editable via the link dialog.
    const auto spans = formatter.parseSpans("[a]()");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].url, QString(""));
    // Two adjacent links.
    QCOMPARE(formatter.parseSpans("[a](x)[b](y)").size(), 2);
}

void TestMarkdownFormatter::testParseSpansAutolink()
{
    MarkdownFormatter formatter;

    auto spans = formatter.parseSpans("see http://a.com now");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("autolink"));
    QCOMPARE(spans[0].start, 4);
    QCOMPARE(spans[0].end, 16);
    QCOMPARE(spans[0].openLen, 0);
    QCOMPARE(spans[0].closeLen, 0);
    QCOMPARE(spans[0].url, QString("http://a.com"));
    // Zero-length markers: display == markdown positions.
    QCOMPARE(spans[0].displayStart, 4);
    QCOMPARE(spans[0].displayEnd, 16);

    // Trailing sentence punctuation stays text.
    spans = formatter.parseSpans("Visit https://x.io.");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].url, QString("https://x.io"));

    // Stops at closing bracket; boundary rule; bare scheme is no link.
    QCOMPARE(formatter.parseSpans("(http://a)")[0].url, QString("http://a"));
    QCOMPARE(formatter.parseSpans("xhttp://a").size(), 0);
    QCOMPARE(formatter.parseSpans("http://").size(), 0);

    // Autolink content is verbatim: markers in the URL never parse.
    spans = formatter.parseSpans("http://a.com/**b**");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].children.size(), 0);
    QCOMPARE(spans[0].url, QString("http://a.com/**b**"));
}

void TestMarkdownFormatter::testRemoveSpanTypeUnlinks()
{
    MarkdownFormatter formatter;
    // Remove link formatting while keeping the text (features.md §2.4).
    QCOMPARE(formatter.removeSpanType("go [a](u) now", 4, 5, "link"),
             QString("go a now"));
    // Applying "link" via the generic wrap is not defined — no-op.
    QCOMPARE(formatter.applySpanType("word", 0, 4, "link"), QString("word"));
}

// Pandoc sup/sub: space-free content, the "~" family sharing with "~~"
// strike, intra-word matching.
void TestMarkdownFormatter::testSupSubSpaceAndFamilyRules()
{
    MarkdownFormatter formatter;

    // Intra-word: the whole point of superscript (no word boundary).
    {
        const auto spans = formatter.parseSpans("E=mc^2^ energy");
        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans[0].type, QString("superscript"));
        QCOMPARE(spans[0].rawText, QString("^2^"));
    }
    {
        const auto spans = formatter.parseSpans("H~2~O is water");
        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans[0].type, QString("subscript"));
        QCOMPARE(spans[0].rawText, QString("~2~"));
    }

    // Space-free content: prose tildes and carets stay literal.
    QCOMPARE(formatter.parseSpans("either ~5 or ~3 of them").size(), 0);
    QCOMPARE(formatter.parseSpans("a ^b c^ d").size(), 0);

    // The "~" family: strike still wins its double marker, and the two
    // coexist in one line.
    {
        const auto spans = formatter.parseSpans("~~gone~~ and H~2~O");
        QCOMPARE(spans.size(), 2);
        QCOMPARE(spans[0].type, QString("strike"));
        QCOMPARE(spans[1].type, QString("subscript"));
    }
    {
        const auto spans = formatter.parseSpans("~~x~~");
        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans[0].type, QString("strike"));
    }

    // Nesting inside other spans.
    {
        const auto spans = formatter.parseSpans("**H~2~O**");
        QCOMPARE(spans.size(), 1);
        QCOMPARE(spans[0].type, QString("bold"));
        QCOMPARE(spans[0].children.size(), 1);
        QCOMPARE(spans[0].children[0].type, QString("subscript"));
    }

    // Unclosed and empty stay literal.
    QCOMPARE(formatter.parseSpans("x^2 open").size(), 0);
    QCOMPARE(formatter.parseSpans("^^").size(), 0);
}

// Generic span-type commands

void TestMarkdownFormatter::testToggleSpanTypePerType_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QString>("marker");

    QTest::newRow("bold") << "bold" << "**";
    QTest::newRow("italic") << "italic" << "*";
    QTest::newRow("strike") << "strike" << "~~";
    QTest::newRow("highlight") << "highlight" << "==";
    QTest::newRow("underline") << "underline" << "++";
    QTest::newRow("code") << "code" << "`";
    QTest::newRow("superscript") << "superscript" << "^";
    QTest::newRow("subscript") << "subscript" << "~";
}

void TestMarkdownFormatter::testToggleSpanTypePerType()
{
    QFETCH(QString, type);
    QFETCH(QString, marker);

    MarkdownFormatter formatter;
    // Toggle on wraps in the canonical markers...
    QCOMPARE(formatter.toggleSpanType("Hello world", 6, 11, type),
             "Hello " + marker + "world" + marker);
    // ...toggle off unwraps (selection on the content, markdown coords).
    const QString formatted = "Hello " + marker + "world" + marker;
    const int contentStart = 6 + marker.length();
    QCOMPARE(formatter.toggleSpanType(formatted, contentStart, contentStart + 5, type),
             QString("Hello world"));
    // Collapsed cursor inserts an empty marker pair (format-then-type).
    QCOMPARE(formatter.toggleSpanType("Hello ", 6, 6, type),
             "Hello " + marker + marker);
}

void TestMarkdownFormatter::testToggleSpanTypeNestedDeepestWins()
{
    MarkdownFormatter formatter;
    // Selection on the nested word: toggling italic removes the child
    // span, not the outer bold.
    QCOMPARE(formatter.toggleSpanType("**bo *it* ld**", 6, 8, "italic"),
             QString("**bo it ld**"));
    // Toggling bold there removes the outer span (the only bold in the
    // chain), keeping the nested italic.
    QCOMPARE(formatter.toggleSpanType("**bo *it* ld**", 6, 8, "bold"),
             QString("bo *it* ld"));
    // Toggling a type absent from the chain nests a new span inside.
    QCOMPARE(formatter.toggleSpanType("**bold**", 2, 6, "strike"),
             QString("**~~bold~~**"));
}

void TestMarkdownFormatter::testRemoveSpanTypeComposite()
{
    MarkdownFormatter formatter;
    // A composite span loses one aspect and converts to the remainder.
    QCOMPARE(formatter.removeSpanType("***both***", 3, 7, "bold"), QString("*both*"));
    QCOMPARE(formatter.removeSpanType("***both***", 3, 7, "italic"), QString("**both**"));
    // Underscore variants respond to the same commands.
    QCOMPARE(formatter.removeSpanType("__word__", 2, 6, "bold"), QString("word"));
}

/// Formatting application tests

void TestMarkdownFormatter::testApplyBoldToSelection()
{
    MarkdownFormatter formatter;
    // Select "world" (positions 6-11) and apply bold
    QString result = formatter.applyBold("Hello world", 6, 11);
    QCOMPARE(result, QString("Hello **world**"));
}

void TestMarkdownFormatter::testApplyBoldAtCursor()
{
    MarkdownFormatter formatter;
    // No selection (cursor at position 6)
    QString result = formatter.applyBold("Hello world", 6, 6);
    QCOMPARE(result, QString("Hello ****world"));
}

void TestMarkdownFormatter::testApplyItalicToSelection()
{
    MarkdownFormatter formatter;
    QString result = formatter.applyItalic("Hello world", 6, 11);
    QCOMPARE(result, QString("Hello *world*"));
}

void TestMarkdownFormatter::testApplyItalicAtCursor()
{
    MarkdownFormatter formatter;
    QString result = formatter.applyItalic("Hello world", 6, 6);
    QCOMPARE(result, QString("Hello **world"));
}

void TestMarkdownFormatter::testToggleBoldAdd()
{
    MarkdownFormatter formatter;
    // Text not bold, should add bold
    QString result = formatter.toggleBold("Hello world", 6, 11);
    QCOMPARE(result, QString("Hello **world**"));
}

void TestMarkdownFormatter::testToggleBoldRemove()
{
    MarkdownFormatter formatter;
    // Text already bold, should remove bold
    // Select inside the bold markers (display positions 6-11 map to "world" in "Hello **world**")
    QString result = formatter.toggleBold("Hello **world**", 8, 13);
    QCOMPARE(result, QString("Hello world"));
}

void TestMarkdownFormatter::testToggleItalicAdd()
{
    MarkdownFormatter formatter;
    QString result = formatter.toggleItalic("Hello world", 6, 11);
    QCOMPARE(result, QString("Hello *world*"));
}

void TestMarkdownFormatter::testToggleItalicRemove()
{
    MarkdownFormatter formatter;
    // Text already italic, should remove italic
    QString result = formatter.toggleItalic("Hello *world*", 7, 12);
    QCOMPARE(result, QString("Hello world"));
}

void TestMarkdownFormatter::testParseSpansColor()
{
    MarkdownFormatter formatter;

    // Named color, double-quoted.
    auto spans = formatter.parseSpans("<span style=\"color:red\">hi</span>");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("color"));
    QCOMPARE(spans[0].color, QString("red"));
    QCOMPARE(spans[0].start, 0);
    QCOMPARE(spans[0].openLen, 24);            // <span style="color:red">
    QCOMPARE(spans[0].closeLen, 7);            // </span>
    QCOMPARE(spans[0].end, 33);
    QCOMPARE(spans[0].displayStart, 0);
    QCOMPARE(spans[0].displayEnd, 2);          // "hi"
    QVERIFY(spans[0].formatFlags & SpanFormat::Color);

    // #rrggbb, single-quoted, surrounding text; position math.
    spans = formatter.parseSpans("a <span style='color:#ff0000'>b</span> c");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].color, QString("#ff0000"));
    QCOMPARE(spans[0].start, 2);
    QCOMPARE(spans[0].displayStart, 2);
    QCOMPARE(spans[0].displayEnd, 3);          // "b"

    // #rgb short hex, and optional whitespace around the value.
    spans = formatter.parseSpans("<span style=\"color: #abc \">x</span>");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].color, QString("#abc"));

    // Formatted content nests as children.
    spans = formatter.parseSpans("<span style=\"color:blue\">**b**</span>");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].children.size(), 1);
    QCOMPARE(spans[0].children[0].type, QString("bold"));
    QCOMPARE(spans[0].displayEnd, 1);          // display "b"
}

void TestMarkdownFormatter::testColorNearMissesStayLiteral()
{
    MarkdownFormatter formatter;
    // The doctrine's "nothing more": every near-miss stays literal text,
    // pinned here so the exact grammar is enforced by tests, not intent.
    const char *literals[] = {
        "<span style=\"color:red\" class=\"x\">hi</span>",  // extra attribute
        "<span style=\"color:red;font-weight:bold\">hi</span>", // 2 properties
        "<span style=\"background:red\">hi</span>",          // other property
        "<span class=\"c\">hi</span>",                        // no style
        "<div style=\"color:red\">hi</div>",                 // block element
        "<span style=color:red>hi</span>",                   // unquoted value
        "<span style=\"color:123xyz\">hi</span>",            // invalid value
        "<span style=\"color:#gg0000\">hi</span>",           // non-hex
        "<span style=\"color:#ff00\">hi</span>",             // 4 hex (not 3/6)
        "<span style=\"color:red\">hi",                      // unclosed
        "<span style=\"color:red\"></span>",                 // empty content
        "<SPAN style=\"color:red\">hi</SPAN>",               // wrong case tag
    };
    for (const char *s : literals)
        QVERIFY2(formatter.parseSpans(QString::fromUtf8(s)).isEmpty(),
                 s);
}

void TestMarkdownFormatter::testColorSpanNesting()
{
    MarkdownFormatter formatter;
    // Nested color spans: the matching </span> is found by balancing, so the
    // outer span spans the whole string and the inner is its child.
    const QString md =
        "<span style=\"color:red\">a <span style=\"color:blue\">b</span> c</span>";
    auto spans = formatter.parseSpans(md);
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].color, QString("red"));
    QCOMPARE(spans[0].end, md.length());
    QCOMPARE(spans[0].children.size(), 1);
    QCOMPARE(spans[0].children[0].type, QString("color"));
    QCOMPARE(spans[0].children[0].color, QString("blue"));
}

void TestMarkdownFormatter::testColorSpanRoundTrip()
{
    MarkdownFormatter formatter;
    // rawText is byte-identical to the input slice (round-trip fidelity).
    const QString md = "<span style='color:#00ff88'>text</span>";
    auto spans = formatter.parseSpans(md);
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].rawText, md);
}

void TestMarkdownFormatter::testApplyColor()
{
    MarkdownFormatter formatter;
    // Wrap a selection.
    QCOMPARE(formatter.applyColor("hello world", 6, 11, "red"),
             QString("hello <span style=\"color:red\">world</span>"));
    // Collapsed cursor still wraps (empty content) — the format-then-type
    // parity with other spans; the caller normally has a selection.
    QCOMPARE(formatter.applyColor("ab", 1, 1, "blue"),
             QString("a<span style=\"color:blue\"></span>b"));
}

void TestMarkdownFormatter::testRecolorInPlace()
{
    MarkdownFormatter formatter;
    // Selecting exactly the content of an existing color span and applying a
    // new color rewrites the value in place, not nests.
    const QString md = "x <span style=\"color:red\">w</span> y";
    // Content "w" is at markdown [26, 27].
    const int contentStart = md.indexOf("w");
    QString out = formatter.applyColor(md, contentStart, contentStart + 1, "green");
    QCOMPARE(out, QString("x <span style=\"color:green\">w</span> y"));
    // Exactly one color span remains (no nesting).
    QCOMPARE(formatter.parseSpans(out).size(), 1);
}

void TestMarkdownFormatter::testRemoveColor()
{
    MarkdownFormatter formatter;
    const QString md = "a <span style=\"color:red\">mid</span> b";
    const int contentStart = md.indexOf("mid");
    QString out = formatter.removeColor(md, contentStart, contentStart + 3);
    QCOMPARE(out, QString("a mid b"));
    QVERIFY(formatter.parseSpans(out).isEmpty());
}

void TestMarkdownFormatter::testColorSpanAt()
{
    MarkdownFormatter formatter;
    const QString md = "a <span style=\"color:red\">mid</span> b";
    const int contentStart = md.indexOf("mid");
    QVariantMap found = formatter.colorSpanAt(md, contentStart, contentStart + 3);
    QVERIFY(found.value("found").toBool());
    QCOMPARE(found.value("color").toString(), QString("red"));

    // Outside any color span.
    QVariantMap none = formatter.colorSpanAt(md, 0, 1);
    QVERIFY(!none.value("found").toBool());
}

void TestMarkdownFormatter::testParseSpansInlineMath()
{
    MarkdownFormatter formatter;
    // A basic inline-math span carries the Math flag and verbatim raw text.
    auto spans = formatter.parseSpans("$x^2$");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("math"));
    QCOMPARE(spans[0].rawText, QString("$x^2$"));
    QVERIFY(spans[0].formatFlags & SpanFormat::Math);
    // Internal spaces are allowed (unlike sup/sub) — only the chars adjacent
    // to the delimiters matter (Pandoc rule).
    spans = formatter.parseSpans("$a + b$");
    QCOMPARE(spans[0].type, QString("math"));
    QCOMPARE(spans[0].rawText, QString("$a + b$"));
    // Content is verbatim: inner ** does not parse.
    spans = formatter.parseSpans("$a **b** c$");
    QCOMPARE(spans[0].type, QString("math"));
    QVERIFY(spans[0].children.isEmpty());
    // Two separate inline-math spans on one line.
    spans = formatter.parseSpans("$x$ and $y$");
    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans[0].type, QString("math"));
    QCOMPARE(spans[1].type, QString("math"));
    // Inline math nests as a child inside another span (bold here).
    spans = formatter.parseSpans("**see $x$ now**");
    QCOMPARE(spans[0].type, QString("bold"));
    QCOMPARE(spans[0].children[0].type, QString("math"));
}

void TestMarkdownFormatter::testInlineMathCurrencyCorpus_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("mathSpans");
    // Currency stays literal (the adjacency rule).
    QTest::newRow("two prices")     << "$5 and $6"        << 0;
    QTest::newRow("one price")      << "it costs $5"      << 0;
    QTest::newRow("prices pair")    << "$100 and $200"    << 0;
    // Adjacency failures — each keeps the dollars literal.
    QTest::newRow("space after open")  << "$ x$"          << 0;
    QTest::newRow("space before close")<< "$x $"          << 0;
    QTest::newRow("digit after close") << "$x$2"          << 0;
    QTest::newRow("empty/block $$")    << "$$"            << 0;
    QTest::newRow("escaped dollar")    << "price a\\$b"   << 0;
    QTest::newRow("lone dollar")       << "a $ b"         << 0;
    // Genuine inline math still parses.
    QTest::newRow("power")          << "$x^2$"            << 1;
    QTest::newRow("frac")           << "$\\frac{a}{b}$"   << 1;
    QTest::newRow("price then math")<< "$5 costs $x$"     << 1;
    QTest::newRow("digits inside")  << "$2+2=4$"          << 1;
}

void TestMarkdownFormatter::testInlineMathCurrencyCorpus()
{
    QFETCH(QString, text);
    QFETCH(int, mathSpans);
    MarkdownFormatter formatter;
    const auto spans = formatter.parseSpans(text);
    int count = 0;
    for (const auto &s : spans)
        if (s.type == QLatin1String("math"))
            ++count;
    QCOMPARE(count, mathSpans);
}

// ---- Emoji as opaque span content ----

void TestMarkdownFormatter::testEmojiInsideAndBetweenSpans()
{
    MarkdownFormatter formatter;
    // Inside a span: offsets are UTF-16 positions, markers still line up.
    const QString bold = QStringLiteral("**bold 🙂**");
    auto spans = formatter.parseSpans(bold);
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("bold"));
    QCOMPARE(spans[0].start, 0);
    QCOMPARE(spans[0].end, int(bold.length()));

    // Adjacent to markers: the surrogate pair neither opens nor closes
    // anything, so both italics parse around it.
    const QString between = QStringLiteral("*a*🙂*b*");
    spans = formatter.parseSpans(between);
    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans[0].type, QString("italic"));
    QCOMPARE(spans[1].type, QString("italic"));
    QCOMPARE(spans[1].start, 5);  // after "*a*" (3) + 🙂 (2 code units)
}

// ---- Backslash escapes ----

void TestMarkdownFormatter::testEscapedDelimitersStayLiteral()
{
    MarkdownFormatter formatter;
    // No italic span: both stars are escaped. Each "\*" is an escape span.
    const auto spans = formatter.parseSpans("2 \\* 3 \\* 4");
    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans[0].type, QString("escape"));
    QCOMPARE(spans[0].start, 2);
    QCOMPARE(spans[0].end, 4);
    QCOMPARE(spans[0].openLen, 1);
    QCOMPARE(spans[0].closeLen, 0);
    QCOMPARE(spans[1].type, QString("escape"));

    // The whole registry honors the escape: underscores, tildes, carets,
    // equals, pluses, backticks, brackets.
    const QStringList literals = {
        "a \\_b\\_ c", "x \\~s\\~ y", "p \\^q\\^ r",
        "h \\=i\\= j", "u \\+v\\+ w", "c \\`d\\` e", "\\[not](a-link)",
    };
    for (const QString &text : literals) {
        const auto parsed = formatter.parseSpans(text);
        for (const auto &s : parsed)
            QCOMPARE(s.type, QString("escape"));
    }
}

void TestMarkdownFormatter::testEscapeSpansConcealBackslash()
{
    MarkdownFormatter formatter;
    // toHtml conceals the backslash (display path for table cells).
    QCOMPARE(formatter.toHtml("2 \\* 3"), QString("2 * 3"));
    QCOMPARE(formatter.toHtml("a \\# b"), QString("a # b"));
    // An inline code span in a cell renders as code, not nothing (the
    // fix-1 table repair depends on this path).
    QCOMPARE(formatter.toHtml("`x | y`"), QString("<code>x | y</code>"));
}

void TestMarkdownFormatter::testDoubleBackslashConsumesEscape()
{
    MarkdownFormatter formatter;
    // "\\" is an escape span for the backslash itself, so the star after
    // it is an ordinary marker free to open a span.
    const auto spans = formatter.parseSpans("\\\\*a*");
    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans[0].type, QString("escape"));
    QCOMPARE(spans[0].rawText, QString("\\\\"));
    QCOMPARE(spans[1].type, QString("italic"));
    QCOMPARE(spans[1].start, 2);
}

void TestMarkdownFormatter::testEscapedClosersAreSkipped()
{
    MarkdownFormatter formatter;
    // The escaped star inside cannot close the italic; the final one does.
    const auto spans = formatter.parseSpans("*a\\*b*");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("italic"));
    QCOMPARE(spans[0].end, 6);
    // The escaped star inside is a nested escape span of the content.
    QCOMPARE(spans[0].children.size(), 1);
    QCOMPARE(spans[0].children[0].type, QString("escape"));
}

void TestMarkdownFormatter::testEscapedDollarBehavior()
{
    MarkdownFormatter formatter;
    // The pre-existing rule holds: \$ never opens math...
    auto spans = formatter.parseSpans("price a\\$5");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("escape"));
    // ...and inside math content an escaped dollar is literal TeX, so the
    // span closes at the final dollar.
    spans = formatter.parseSpans("$a\\$b$");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("math"));
    QCOMPARE(spans[0].end, 6);
}

void TestMarkdownFormatter::testParseSpansWikiLink()
{
    MarkdownFormatter formatter;

    auto spans = formatter.parseSpans("[[My Note]]");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("wikilink"));
    QCOMPARE(spans[0].start, 0);
    QCOMPARE(spans[0].end, 11);
    QCOMPARE(spans[0].openLen, 2);
    QCOMPARE(spans[0].closeLen, 2);
    QCOMPARE(spans[0].url, QString("kvit-note:My Note"));
    QVERIFY(spans[0].formatFlags & SpanFormat::Link);
    QVERIFY(spans[0].formatFlags & SpanFormat::WikiLink);
    QCOMPARE(spans[0].displayStart, 0);
    QCOMPARE(spans[0].displayEnd, 7); // display "My Note"

    // Position math with surrounding text; content is verbatim (no
    // children even with marker characters inside).
    spans = formatter.parseSpans("see [[notes/plan]] soon");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].start, 4);
    QCOMPARE(spans[0].end, 18);
    QCOMPARE(spans[0].url, QString("kvit-note:notes/plan"));
    QVERIFY(spans[0].children.isEmpty());
}

void TestMarkdownFormatter::testParseSpansWikiLinkAliasAndHeading()
{
    MarkdownFormatter formatter;

    // Alias: the opening marker swallows "target|"; display is the alias.
    auto spans = formatter.parseSpans("[[Plan|the plan]]");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].openLen, 7);  // "[[Plan|"
    QCOMPARE(spans[0].closeLen, 2);
    QCOMPARE(spans[0].url, QString("kvit-note:Plan"));
    QCOMPARE(spans[0].displayEnd - spans[0].displayStart, 8); // "the plan"

    // Heading anchor rides in the url.
    spans = formatter.parseSpans("[[Plan#Goals]]");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].url, QString("kvit-note:Plan#Goals"));
    QCOMPARE(spans[0].displayEnd - spans[0].displayStart, 10); // "Plan#Goals"

    // Both: url keeps target#heading, display shows the alias.
    spans = formatter.parseSpans("[[Plan#Goals|goals]]");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].url, QString("kvit-note:Plan#Goals"));
    QCOMPARE(spans[0].displayEnd - spans[0].displayStart, 5);

    // Same-note anchor form.
    spans = formatter.parseSpans("[[#Goals]]");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].url, QString("kvit-note:#Goals"));
}

void TestMarkdownFormatter::testWikiLinkPrecedenceAndEdges()
{
    MarkdownFormatter formatter;

    // "[[" wins over the LinkMatcher's "[" — never a [text](url) parse.
    auto spans = formatter.parseSpans("[[a]](b)");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("wikilink"));
    QCOMPARE(spans[0].end, 5);

    // A plain markdown link still parses.
    spans = formatter.parseSpans("[a](b)");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("link"));

    // Escaped opener stays literal.
    QVERIFY(formatter.parseSpans("\\[[a]]").isEmpty()
            || formatter.parseSpans("\\[[a]]")[0].type == QString("escape"));

    // Unclosed, empty, or malformed forms stay literal text.
    QVERIFY(formatter.parseSpans("[[a").isEmpty());
    QVERIFY(formatter.parseSpans("[[]]").isEmpty());
    QVERIFY(formatter.parseSpans("[[  ]]").isEmpty());
    QVERIFY(formatter.parseSpans("[[a|]]").isEmpty());
    QVERIFY(formatter.parseSpans("[[a#]]").isEmpty());
    QVERIFY(formatter.parseSpans("[[a|b|c]]").isEmpty());
    QVERIFY(formatter.parseSpans("[[a\nb]]").isEmpty());

    // Inside inline math the dollars own the content: no wiki-link span.
    spans = formatter.parseSpans("$[[x]]$");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("math"));

    // "![[note]]" parses as a plain wiki-link preceded by a literal "!"
    // (embeds are post-launch).
    spans = formatter.parseSpans("![[note]]");
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].type, QString("wikilink"));
    QCOMPARE(spans[0].start, 1);
}

QTEST_MAIN(TestMarkdownFormatter)
#include "test_markdownformatter.moc"
