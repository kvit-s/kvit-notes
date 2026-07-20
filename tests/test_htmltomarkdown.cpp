// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "htmltomarkdown.h"
#include "markdownformatter.h"

#include <QUrl>

// Covers the HTML arm of the smart-paste matrix (features.md §5.3): pasting a
// browser or word-processor payload converts to Kvit markdown rather than
// landing as either raw tags or structure-stripped text.
class TestHtmlToMarkdown : public QObject
{
    Q_OBJECT

private slots:
    void testHeadings();
    void testInlineEmphasis();
    void testInlineCodeAndPre();
    void testLinks();
    void testUnorderedList();
    void testOrderedList();
    void testNestedList();
    void testBlockquote();
    void testTable();
    void testParagraphSeparation();
    void testEntitiesDecode();
    void testEmptyAndPlainInput();
    void testHasStructure_data();
    void testHasStructure();
    void testMarkdownCharactersInTextAreEscaped();

    // M9: delimiters must be chosen to fit the content they wrap.
    void testInlineCodeChoosesFenceLongerThanContent_data();
    void testInlineCodeChoosesFenceLongerThanContent();
    void testCodeFenceChoosesFenceLongerThanContent();
    void testLinkDestinationWithParenthesesSurvives();

private:
    HtmlToMarkdown converter;

    // Read the produced markdown back with the app's own inline parser, so
    // the assertions are about what Kvit will see, not about spelling.
    MarkdownFormatter reader;
};

void TestHtmlToMarkdown::testHeadings()
{
    QCOMPARE(converter.convert("<h1>Title</h1>"), QString("# Title"));
    QCOMPARE(converter.convert("<h2>Section</h2>"), QString("## Section"));
    QCOMPARE(converter.convert("<h3>Sub</h3>"), QString("### Sub"));
    QCOMPARE(converter.convert("<h6>Deep</h6>"), QString("###### Deep"));
}

void TestHtmlToMarkdown::testInlineEmphasis()
{
    QCOMPARE(converter.convert("<p><b>bold</b></p>"), QString("**bold**"));
    QCOMPARE(converter.convert("<p><strong>bold</strong></p>"), QString("**bold**"));
    QCOMPARE(converter.convert("<p><i>it</i></p>"), QString("*it*"));
    QCOMPARE(converter.convert("<p><em>it</em></p>"), QString("*it*"));
    QCOMPARE(converter.convert("<p><s>gone</s></p>"), QString("~~gone~~"));
    // Emphasis markers must hug the text, with the space outside them —
    // "a ** b **" would not be emphasis when read back.
    QCOMPARE(converter.convert("<p>a <b>b</b> c</p>"), QString("a **b** c"));
}

void TestHtmlToMarkdown::testInlineCodeAndPre()
{
    QCOMPARE(converter.convert("<p>run <code>ls -l</code> now</p>"),
             QString("run `ls -l` now"));
    // A wholly monospace block is a fence, not an inline span.
    const QString fenced = converter.convert("<pre>int main() {}</pre>");
    QVERIFY2(fenced.startsWith("```"), qPrintable(fenced));
    QVERIFY2(fenced.contains("int main() {}"), qPrintable(fenced));
    QVERIFY2(fenced.endsWith("```"), qPrintable(fenced));
}

void TestHtmlToMarkdown::testLinks()
{
    QCOMPARE(converter.convert("<p><a href=\"https://example.com\">site</a></p>"),
             QString("[site](https://example.com)"));
    // A link carrying emphasis keeps both, with the link outermost.
    QCOMPARE(converter.convert(
                 "<p><a href=\"https://e.com\"><b>bold link</b></a></p>"),
             QString("[**bold link**](https://e.com)"));
}

void TestHtmlToMarkdown::testUnorderedList()
{
    QCOMPARE(converter.convert("<ul><li>one</li><li>two</li></ul>"),
             QString("- one\n\n- two"));
}

void TestHtmlToMarkdown::testOrderedList()
{
    const QString md = converter.convert("<ol><li>one</li><li>two</li></ol>");
    QVERIFY2(md.contains("1. one"), qPrintable(md));
    QVERIFY2(md.contains("2. two"), qPrintable(md));
}

void TestHtmlToMarkdown::testNestedList()
{
    const QString md = converter.convert(
        "<ul><li>outer</li><ul><li>inner</li></ul></ul>");
    QVERIFY2(md.contains("- outer"), qPrintable(md));
    // The nested level is indented rather than flattened.
    QVERIFY2(md.contains("  - inner"), qPrintable(md));
}

void TestHtmlToMarkdown::testBlockquote()
{
    QCOMPARE(converter.convert("<blockquote>quoted</blockquote>"),
             QString("> quoted"));
}

void TestHtmlToMarkdown::testTable()
{
    const QString md = converter.convert(
        "<table><tr><th>A</th><th>B</th></tr>"
        "<tr><td>1</td><td>2</td></tr></table>");
    QVERIFY2(md.contains("| A | B |"), qPrintable(md));
    QVERIFY2(md.contains("| --- | --- |"), qPrintable(md));
    QVERIFY2(md.contains("| 1 | 2 |"), qPrintable(md));
}

void TestHtmlToMarkdown::testParagraphSeparation()
{
    QCOMPARE(converter.convert("<p>one</p><p>two</p>"), QString("one\n\ntwo"));
    // Empty paragraphs must not pile up blank lines.
    QCOMPARE(converter.convert("<p>one</p><p></p><p></p><p>two</p>"),
             QString("one\n\ntwo"));
}

void TestHtmlToMarkdown::testEntitiesDecode()
{
    QCOMPARE(converter.convert("<p>a &amp; b &lt; c</p>"), QString("a & b < c"));
}

void TestHtmlToMarkdown::testEmptyAndPlainInput()
{
    QCOMPARE(converter.convert(QString()), QString());
    QCOMPARE(converter.convert("   "), QString());
    QCOMPARE(converter.convert("just text"), QString("just text"));
}

void TestHtmlToMarkdown::testHasStructure_data()
{
    QTest::addColumn<QString>("html");
    QTest::addColumn<bool>("structured");
    QTest::newRow("heading") << "<h1>x</h1>" << true;
    QTest::newRow("list") << "<ul><li>x</li></ul>" << true;
    QTest::newRow("bold") << "<span><b>x</b></span>" << true;
    QTest::newRow("link") << "<a href=\"u\">x</a>" << true;
    QTest::newRow("bare wrapper") << "<html><body>plain</body></html>" << false;
    QTest::newRow("no tags") << "plain" << false;
}

void TestHtmlToMarkdown::testHasStructure()
{
    QFETCH(QString, html);
    QFETCH(bool, structured);
    QCOMPARE(converter.hasStructure(html), structured);
}

void TestHtmlToMarkdown::testMarkdownCharactersInTextAreEscaped()
{
    // Literal asterisks in HTML text carried no meaning there; they must not
    // silently become emphasis once the payload is markdown.
    QCOMPARE(converter.convert("<p>2 * 3 * 4</p>"), QString("2 \\* 3 \\* 4"));
    QCOMPARE(converter.convert("<p>a_b_c</p>"), QString("a\\_b\\_c"));
}

// ---------- M9: delimiters sized to their content ----------

void TestHtmlToMarkdown::testInlineCodeChoosesFenceLongerThanContent_data()
{
    QTest::addColumn<QString>("codeText");
    QTest::newRow("no backtick")    << "ls -l";
    QTest::newRow("one backtick")   << "a ` b";
    QTest::newRow("two backticks")  << "a `` b";
    QTest::newRow("three backticks")<< "a ``` b";
    QTest::newRow("leading tick")   << "`x";
    QTest::newRow("trailing tick")  << "x`";
    QTest::newRow("only ticks")     << "``";
}

void TestHtmlToMarkdown::testInlineCodeChoosesFenceLongerThanContent()
{
    QFETCH(QString, codeText);
    const QString html = "<p>run <code>" + codeText.toHtmlEscaped()
                       + "</code> now</p>";
    const QString md = converter.convert(html);

    // Reading the result back must recover the original code text exactly.
    const QList<FormattedSpan> spans = reader.parseSpans(md);
    const FormattedSpan *code = nullptr;
    for (const FormattedSpan &s : spans) {
        if (s.type == QLatin1String("code"))
            code = &s;
    }
    QVERIFY2(code, qPrintable("no code span parsed out of: " + md));
    const QString inner =
        md.mid(code->start + code->openLen, code->contentLength());
    // CommonMark strips one space of padding from each end when both are
    // present, which is how a span may start or end with a backtick.
    QString recovered = inner;
    if (recovered.size() >= 2 && recovered.startsWith(QLatin1Char(' '))
        && recovered.endsWith(QLatin1Char(' ')))
        recovered = recovered.mid(1, recovered.size() - 2);
    QVERIFY2(recovered == codeText,
             qPrintable("produced: " + md + " | recovered: " + recovered));
}

void TestHtmlToMarkdown::testCodeFenceChoosesFenceLongerThanContent()
{
    // A <pre> line containing a ``` run must not be closed by it.
    const QString md = converter.convert("<pre>a ``` b</pre>");
    const int firstNewline = md.indexOf(QLatin1Char('\n'));
    QVERIFY(firstNewline > 0);
    const QString opener = md.left(firstNewline);
    QVERIFY2(opener.length() >= 4,
             qPrintable("fence must outrun the ``` inside: " + md));
    QVERIFY2(md.contains("a ``` b"), qPrintable(md));
    // The fence appears exactly twice: opener and closer, nowhere inside.
    QCOMPARE(md.count(opener), 2);
}

void TestHtmlToMarkdown::testLinkDestinationWithParenthesesSurvives()
{
    const QString md = converter.convert(
        "<p><a href=\"http://x/a_(b)_c\">wiki</a></p>");
    const QList<FormattedSpan> spans = reader.parseSpans(md);
    const FormattedSpan *link = nullptr;
    for (const FormattedSpan &s : spans) {
        if (s.type == QLatin1String("link"))
            link = &s;
    }
    QVERIFY2(link, qPrintable("no link span parsed out of: " + md));
    // Percent-encoding is not an escape the reader must undo: it is the same
    // URL, and it fits a destination grammar that admits no parentheses.
    QCOMPARE(link->url, QString("http://x/a_%28b%29_c"));
    QCOMPARE(QUrl::fromPercentEncoding(link->url.toUtf8()),
             QString("http://x/a_(b)_c"));
}

QTEST_MAIN(TestHtmlToMarkdown)
#include "test_htmltomarkdown.moc"
