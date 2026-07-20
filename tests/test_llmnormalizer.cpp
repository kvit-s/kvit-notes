// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "llmnormalizer.h"
#include "documentserializer.h"
#include "blockmodel.h"
#include "block.h"
#include "tabledata.h"

// The LLM-markdown normalization pass: pure-text rewrites (fixes 1, 3, 4,
// 7), each fixture asserted twice — once for the rewrite, once for
// idempotence — plus the canonical-corpus fixed-point test over documents
// produced by serialize. Fixtures are ported from
// ~/chat/frontend/tests/components/ReplyDisplay.spec.ts so both projects
// test the same real-world shapes.

class TestLlmNormalizer : public QObject
{
    Q_OBJECT

private slots:
    // Fix 1: code fence inside a table cell
    void testTableCodeFenceRepaired();
    void testTableCodeFencePipesEscaped();
    void testBareFenceAfterTableIsCodeBlock();
    void testUnclosedFenceWithoutTableRunsToEof();
    void testProsePipeBeforeFenceIsNotTableContext();
    void testCompleteTableRowBeforeFenceIsCodeBlock();
    void testLongerFenceKeepsInnerFencesIntact();
    void testFenceContentMentioningBackticksSurvives();

    // Fix 3: \(...\) and \[...\] math delimiters
    void testInlineParenMath();
    void testMultiLineBracketMath();
    void testMultiMatrixBracketMath();
    void testIncompleteLatexKeepsStructure();
    void testMixedCodeAndMath();
    void testUnmatchedDelimitersStayLiteral();
    void testBracketMathInTableRowBecomesInline();

    // Fix 4: Unicode spaces inside math
    void testUnicodeSpacesInInlineMath();
    void testUnicodeSpacesInDisplayMath();
    void testUnicodeSpacesInDollarSpans();
    void testProseUnicodeSpaceSurvives();

    // Fix 7: HTML entities
    void testEntitiesDecodeInProse();
    void testNumericEntities();
    void testDoubleEscapedAmpStaysLiteral();
    void testEntitiesInVerbatimRegionsSurvive();

    // Load-bearing properties
    void testIdempotenceOnAllFixtures();
    void testCanonicalCorpusFixedPoint();
    void testValidMarkdownCorpusUnchanged();
    void testLookaheadBoundKeepsLinearTime();

private:
    // Every fixture that exercises a rewrite also lands here for the
    // idempotence sweep.
    QStringList allFixtures() const;

    // Documents that are already valid Markdown holding none of the LLM
    // constructs. The normalizer must return each byte-identically.
    QStringList validMarkdownCorpus() const;

    static QString tableCodeFenceFixture()
    {
        return QStringLiteral(
            "### How it works\n"
            "\n"
            "| Step | What happens | Why |\n"
            "|------|--------------|-----|\n"
            "| **Dimension check** | Some explanation | Reason here |\n"
            "| **Triple loop** |\n"
            "```\n"
            "for i  in rows of A\n"
            "    for j in cols of B\n"
            "        for t in shared dimension\n"
            "            C[i][j] += A[i][t] * B[t][j];\n"
            "``` | This is the classic dot-product implementation. |\n"
            "| **Return** | The fully populated matrix. | Caller can use it. |\n"
            "\n"
            "Done.\n");
    }

    static QString multiLineBracketFixture()
    {
        return QStringLiteral(
            "The matrix is:\n"
            "\n"
            "\\[\n"
            "C = \\begin{bmatrix}\n"
            "1 & 2 \\\\\n"
            "3 & 4\n"
            "\\end{bmatrix}\n"
            "\\]\n"
            "\n"
            "That's the result.\n");
    }

    static QString multiMatrixFixture()
    {
        return QStringLiteral(
            "Result:\n"
            "\n"
            "\\[\n"
            "C = \\begin{bmatrix}\n"
            "1 & 2 & 3 \\\\\n"
            "4 & 5 & 6\n"
            "\\end{bmatrix}\n"
            "=\n"
            "\\begin{bmatrix}\n"
            "7 & 8 \\\\\n"
            "9 & 10\n"
            "\\end{bmatrix}\n"
            "\\]\n"
            "\n"
            "Done.\n");
    }

    static QString incompleteLatexFixture()
    {
        return QStringLiteral(
            "Result:\n"
            "\n"
            "\\[\n"
            "C = \\begin{bmatrix}\n"
            "1 & 2 \\\\\n"
            "3 & 4\n"
            "\\]\n"
            "\n"
            "Text after.\n");
    }

    static QString mixedCodeAndMathFixture()
    {
        return QStringLiteral(
            "Here's some code:\n"
            "\n"
            "```python\n"
            "def solve():\n"
            "    return 42\n"
            "```\n"
            "\n"
            "And a formula:\n"
            "\n"
            "\\[\n"
            "f(x) = x^2\n"
            "\\]\n"
            "\n"
            "Done.\n");
    }

    static QString unicodeInlineFixture()
    {
        return QStringLiteral(
            "The algorithm complexity is \\(O(n · m · p)\\).\n"
            "\n"
            "Another example with non-breaking space: \\(a + b = c\\).\n"
            "\n"
            "Thin space example: \\(x = 5\\).\n"
            "\n"
            "Hair space example: \\(y = 10\\).\n");
    }

    static QString unicodeDisplayFixture()
    {
        return QStringLiteral(
            "The matrix equation:\n"
            "\n"
            "\\[\n"
            "C = A × B\n"
            "\\]\n"
            "\n"
            "Where \\(A \\in \\mathbb{R}^{n×m}\\).\n");
    }

    QList<DocumentSerializer::BlockData> parsed(const QString &markdown)
    {
        DocumentSerializer serializer;
        return serializer.parse(markdown);
    }
};

// ---------------------------------------------------------------- fix 1

void TestLlmNormalizer::testTableCodeFenceRepaired()
{
    const auto blocks = parsed(tableCodeFenceFixture());

    // Exactly one table plus the surrounding heading/paragraph blocks;
    // nothing after the table is absorbed into a code block.
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[0].type, Block::Heading3);
    QCOMPARE(blocks[1].type, Block::Table);
    QCOMPARE(blocks[2].type, Block::Paragraph);
    QCOMPARE(blocks[2].content, QStringLiteral("Done."));

    const TableData::Table table = TableData::parse(blocks[1].content);
    QVERIFY(table.valid);
    QCOMPARE(table.headers,
             QStringList({"Step", "What happens", "Why"}));
    QCOMPARE(table.rowCount(), 3);
    QCOMPARE(table.rows[0][0], QStringLiteral("**Dimension check**"));
    QCOMPARE(table.rows[1][0], QStringLiteral("**Triple loop**"));
    // The code sits in the middle cell as an inline code span, lines
    // joined with single spaces (table integrity wins over line breaks).
    QCOMPARE(table.rows[1][1], QStringLiteral(
        "`for i  in rows of A for j in cols of B "
        "for t in shared dimension C[i][j] += A[i][t] * B[t][j];`"));
    QCOMPARE(table.rows[1][2],
             QStringLiteral("This is the classic dot-product implementation."));
    QCOMPARE(table.rows[2][0], QStringLiteral("**Return**"));
}

void TestLlmNormalizer::testTableCodeFencePipesEscaped()
{
    // A pipe inside the embedded code must not split the repaired row.
    const QString md = QStringLiteral(
        "| a | b |\n"
        "|---|---|\n"
        "| x |\n"
        "```\n"
        "grep foo | wc -l\n"
        "``` | count |\n");
    const auto blocks = parsed(md);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Table);
    const TableData::Table table = TableData::parse(blocks[0].content);
    QCOMPARE(table.rowCount(), 1);
    QCOMPARE(table.rows[0][1], QStringLiteral("`grep foo | wc -l`"));
}

void TestLlmNormalizer::testBareFenceAfterTableIsCodeBlock()
{
    // A properly closed fence directly under a table row is an ordinary
    // code block, not the broken-table shape — no repair.
    const QString md = QStringLiteral(
        "| a | b |\n"
        "|---|---|\n"
        "| 1 | 2 |\n"
        "```\n"
        "code\n"
        "```\n");
    const auto blocks = parsed(md);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].type, Block::Table);
    QCOMPARE(blocks[1].type, Block::CodeBlock);
    QCOMPARE(blocks[1].content, QStringLiteral("code"));
}

void TestLlmNormalizer::testUnclosedFenceWithoutTableRunsToEof()
{
    // CommonMark-sanctioned: an unclosed fence not preceded by a table row
    // keeps today's run-to-EOF behavior.
    const QString md = QStringLiteral("prose\n\n```\ncode\nmore code");
    const auto blocks = parsed(md);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[1].type, Block::CodeBlock);
    QCOMPARE(blocks[1].content, QStringLiteral("code\nmore code"));
}

void TestLlmNormalizer::testProsePipeBeforeFenceIsNotTableContext()
{
    // A pipe in prose is not a table. Without a delimiter row establishing
    // one, a fence on the next line is an ordinary code block however its
    // content is punctuated.
    const QString md = QStringLiteral(
        "Columns: name | value\n"
        "```\n"
        "Write ``` to open a fence.\n"
        "```\n");
    QCOMPARE(LlmNormalizer::normalize(md), md);

    const auto blocks = parsed(md);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[1].type, Block::CodeBlock);
    QCOMPARE(blocks[1].content,
             QStringLiteral("Write ``` to open a fence."));

    // A single pipe-bearing row with no delimiter row above is not context
    // either, even when the fence content does look like a broken closer.
    const QString lone = QStringLiteral(
        "| just a line with pipes |\n```\n``` | y |\n```\n");
    QCOMPARE(LlmNormalizer::normalize(lone), lone);
}

void TestLlmNormalizer::testCompleteTableRowBeforeFenceIsCodeBlock()
{
    // The broken shape always cuts off mid-row, so the row above the fence
    // has fewer cells than the header. A complete row means the table
    // finished and the fence is an ordinary code block — even when its
    // content happens to read like a malformed closer.
    const QString md = QStringLiteral(
        "| a | b |\n|---|---|\n| 1 | 2 |\n"
        "```\n"
        "``` | y |\n"
        "```\n");
    QCOMPARE(LlmNormalizer::normalize(md), md);

    const auto blocks = parsed(md);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].type, Block::Table);
    QCOMPARE(blocks[1].type, Block::CodeBlock);
    QCOMPARE(blocks[1].content, QStringLiteral("``` | y |"));
}

void TestLlmNormalizer::testLongerFenceKeepsInnerFencesIntact()
{
    // A four-backtick fence wrapping a three-backtick one is how Markdown
    // documents Markdown. The inner "```python" is content, not a closer:
    // its run is shorter than the opener's and it resumes no table row.
    const QString md = QStringLiteral(
        "Use the a | b syntax to chain.\n"
        "````\n"
        "```python\n"
        "print(1)\n"
        "```\n"
        "````\n"
        "\n"
        "After.\n");
    QCOMPARE(LlmNormalizer::normalize(md), md);

    const auto blocks = parsed(md);
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].type, Block::CodeBlock);
    QCOMPARE(blocks[1].content,
             QStringLiteral("```python\nprint(1)\n```"));
    QCOMPARE(blocks[2].content, QStringLiteral("After."));

    // The same nesting directly under a real table.
    const QString table = QStringLiteral(
        "| a | b |\n|---|---|\n| 1 | 2 |\n"
        "````\n```js\nx\n```\n````\n");
    QCOMPARE(LlmNormalizer::normalize(table), table);
}

void TestLlmNormalizer::testFenceContentMentioningBackticksSurvives()
{
    // Backtick runs away from the line start are code content, whatever
    // follows them.
    const QString md = QStringLiteral(
        "| a | b |\n|---|---|\n| x |\n"
        "```\n"
        "printf('%s', s); ``` | not a cell |\n"
        "```\n");
    // The row IS truncated here, so the trigger's other two conditions hold
    // — only the closer definition keeps this document intact.
    QCOMPARE(LlmNormalizer::normalize(md), md);

    // A tilde fence is never the broken shape: fix 1 repairs backtick fences.
    const QString tilde = QStringLiteral(
        "| a | b |\n|---|---|\n| x |\n~~~\ncode ``` | y |\n~~~\n");
    QCOMPARE(LlmNormalizer::normalize(tilde), tilde);
}

// ---------------------------------------------------------------- fix 3

void TestLlmNormalizer::testInlineParenMath()
{
    const auto blocks =
        parsed(QStringLiteral("The value \\(x = 5\\) is correct.\n"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[0].content,
             QStringLiteral("The value $x = 5$ is correct."));

    // Trimming matters: the inline matcher requires a non-space
    // immediately inside both dollars.
    const auto padded = parsed(QStringLiteral("A \\( x = 5 \\) B\n"));
    QCOMPARE(padded[0].content, QStringLiteral("A $x = 5$ B"));
}

void TestLlmNormalizer::testMultiLineBracketMath()
{
    const auto blocks = parsed(multiLineBracketFixture());
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[1].type, Block::MathBlock);
    QCOMPARE(blocks[1].content, QStringLiteral(
        "C = \\begin{bmatrix}\n1 & 2 \\\\\n3 & 4\n\\end{bmatrix}"));
    QCOMPARE(blocks[2].type, Block::Paragraph);
    QVERIFY(!blocks[0].content.contains(QLatin1String("\\[")));
    QVERIFY(!blocks[2].content.contains(QLatin1String("\\]")));
}

void TestLlmNormalizer::testMultiMatrixBracketMath()
{
    const auto blocks = parsed(multiMatrixFixture());
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].type, Block::MathBlock);
    QVERIFY(blocks[1].content.contains(QLatin1String("7 & 8")));
    QVERIFY(blocks[1].content.contains(QLatin1String("9 & 10")));
    for (const auto &b : blocks) {
        QVERIFY(!b.content.contains(QLatin1String("\\[")));
        QVERIFY(!b.content.contains(QLatin1String("\\]")));
    }
}

void TestLlmNormalizer::testIncompleteLatexKeepsStructure()
{
    // Deliberately malformed TeX (missing \end) still becomes a MathBlock
    // — MicroTeX reports the error, the document structure survives.
    const auto blocks = parsed(incompleteLatexFixture());
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].type, Block::MathBlock);
    QCOMPARE(blocks[2].type, Block::Paragraph);
    QCOMPARE(blocks[2].content, QStringLiteral("Text after."));
}

void TestLlmNormalizer::testMixedCodeAndMath()
{
    const auto blocks = parsed(mixedCodeAndMathFixture());
    QCOMPARE(blocks.size(), 5);
    QCOMPARE(blocks[1].type, Block::CodeBlock);
    QCOMPARE(blocks[1].language, QStringLiteral("python"));
    QCOMPARE(blocks[1].content,
             QStringLiteral("def solve():\n    return 42"));
    QCOMPARE(blocks[3].type, Block::MathBlock);
    QCOMPARE(blocks[3].content, QStringLiteral("f(x) = x^2"));
}

void TestLlmNormalizer::testUnmatchedDelimitersStayLiteral()
{
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("open \\( only\n")),
             QStringLiteral("open \\( only\n"));
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("open \\[ only\n")),
             QStringLiteral("open \\[ only\n"));
    // Empty pairs stay literal too: an invisible span cannot exist.
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("a \\(\\) b\n")),
             QStringLiteral("a \\(\\) b\n"));
}

void TestLlmNormalizer::testBracketMathInTableRowBecomesInline()
{
    // Splitting a table row into a $$ fence would destroy the row, so a
    // same-line \[...\] inside one converts to an inline span instead.
    const QString md = QStringLiteral(
        "| formula | note |\n"
        "|---|---|\n"
        "| \\[x = 2\\] | fine |\n");
    const auto blocks = parsed(md);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Table);
    const TableData::Table table = TableData::parse(blocks[0].content);
    QCOMPARE(table.rows[0][0], QStringLiteral("$x = 2$"));
}

// ---------------------------------------------------------------- fix 4

void TestLlmNormalizer::testUnicodeSpacesInInlineMath()
{
    const auto blocks = parsed(unicodeInlineFixture());
    QCOMPARE(blocks.size(), 4);
    QCOMPARE(blocks[0].content, QStringLiteral(
        "The algorithm complexity is $O(n · m · p)$."));
    QCOMPARE(blocks[1].content, QStringLiteral(
        "Another example with non-breaking space: $a + b = c$."));
    QCOMPARE(blocks[2].content,
             QStringLiteral("Thin space example: $x = 5$."));
    QCOMPARE(blocks[3].content,
             QStringLiteral("Hair space example: $y = 10$."));
}

void TestLlmNormalizer::testUnicodeSpacesInDisplayMath()
{
    const auto blocks = parsed(unicodeDisplayFixture());
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[1].type, Block::MathBlock);
    QCOMPARE(blocks[1].content, QStringLiteral("C = A × B"));
    QCOMPARE(blocks[2].content, QStringLiteral(
        "Where $A \\in \\mathbb{R}^{n×m}$."));
}

void TestLlmNormalizer::testUnicodeSpacesInDollarSpans()
{
    // Already-dollar-delimited math is normalized too...
    QCOMPARE(LlmNormalizer::normalize(
                 QStringLiteral("so $a + b$ holds\n")),
             QStringLiteral("so $a + b$ holds\n"));
    // ...and inside a $$ fence.
    QCOMPARE(LlmNormalizer::normalize(
                 QStringLiteral("$$\na + b\n$$\n")),
             QStringLiteral("$$\na + b\n$$\n"));
    // Prose dollars are not math; nothing changes.
    const QString prose = QStringLiteral("$5 and $6\n");
    QCOMPARE(LlmNormalizer::normalize(prose), prose);
}

void TestLlmNormalizer::testProseUnicodeSpaceSurvives()
{
    // A non-breaking space in prose can be intentional; only math content
    // is rewritten.
    const QString prose = QStringLiteral("A B et al.\n");
    QCOMPARE(LlmNormalizer::normalize(prose), prose);
    const auto blocks = parsed(prose);
    QCOMPARE(blocks[0].content, QStringLiteral("A B et al."));
}

// ---------------------------------------------------------------- fix 7

void TestLlmNormalizer::testEntitiesDecodeInProse()
{
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("Fish &amp; Chips\n")),
             QStringLiteral("Fish & Chips\n"));
    QCOMPARE(LlmNormalizer::normalize(
                 QStringLiteral("&lt;tag&gt; &quot;q&quot; &apos;a&apos;\n")),
             QStringLiteral("<tag> \"q\" 'a'\n"));
    // &nbsp; becomes a plain space.
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("a&nbsp;b\n")),
             QStringLiteral("a b\n"));
    // Unknown entities stay literal.
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("&foo; &x\n")),
             QStringLiteral("&foo; &x\n"));
}

void TestLlmNormalizer::testNumericEntities()
{
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("&#65;&#x42;\n")),
             QStringLiteral("AB\n"));
    // Out-of-range and surrogate references stay literal.
    QCOMPARE(LlmNormalizer::normalize(
                 QStringLiteral("&#0; &#1114112; &#55296;\n")),
             QStringLiteral("&#0; &#1114112; &#55296;\n"));
    // Astral code points decode to surrogate pairs.
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("&#x1F642;\n")),
             QStringLiteral("\U0001F642\n"));
}

void TestLlmNormalizer::testDoubleEscapedAmpStaysLiteral()
{
    // Decoding "&amp;lt;" to "&lt;" cannot coexist with idempotence (the
    // next load would decode "&lt;" to "<", dropping one escape level per
    // load/save cycle), and idempotence is the load-bearing property. An
    // entity decoding to '&' therefore stays literal when a decodable tail
    // follows.
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("&amp;lt;\n")),
             QStringLiteral("&amp;lt;\n"));
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("&amp;amp;\n")),
             QStringLiteral("&amp;amp;\n"));
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("&#38;lt;\n")),
             QStringLiteral("&#38;lt;\n"));
    // Without a tail, the ampersand decodes normally.
    QCOMPARE(LlmNormalizer::normalize(QStringLiteral("&amp;-joined\n")),
             QStringLiteral("&-joined\n"));
}

void TestLlmNormalizer::testEntitiesInVerbatimRegionsSurvive()
{
    // Code fences.
    const QString fenced =
        QStringLiteral("```\nif (a &amp;&amp; b)\n```\n");
    QCOMPARE(LlmNormalizer::normalize(fenced), fenced);
    // Tilde fences are verbatim regions too (fix 8).
    const QString tilde =
        QStringLiteral("~~~\nFish &amp; Chips\n~~~\n");
    QCOMPARE(LlmNormalizer::normalize(tilde), tilde);
    // Inline code spans.
    QCOMPARE(LlmNormalizer::normalize(
                 QStringLiteral("use `&amp;` for &amp;\n")),
             QStringLiteral("use `&amp;` for &\n"));
}

// --------------------------------------------------- load-bearing rules

QStringList TestLlmNormalizer::allFixtures() const
{
    return {
        tableCodeFenceFixture(),
        multiLineBracketFixture(),
        multiMatrixFixture(),
        incompleteLatexFixture(),
        mixedCodeAndMathFixture(),
        unicodeInlineFixture(),
        unicodeDisplayFixture(),
        QStringLiteral("The value \\(x = 5\\) is correct.\n"),
        QStringLiteral("open \\( only\n"),
        QStringLiteral("open \\[ only\n"),
        QStringLiteral("so $a + b$ holds\n"),
        QStringLiteral("$$\na + b\n$$\n"),
        QStringLiteral("$5 and $6\n"),
        QStringLiteral("A B et al.\n"),
        QStringLiteral("Fish &amp; Chips\n"),
        QStringLiteral("&lt;tag&gt; &quot;q&quot; &apos;a&apos;\n"),
        QStringLiteral("a&nbsp;b\n"),
        QStringLiteral("&foo; &x\n"),
        QStringLiteral("&#65;&#x42;\n"),
        QStringLiteral("&amp;lt;\n"),
        QStringLiteral("&amp;amp;\n"),
        QStringLiteral("```\nif (a &amp;&amp; b)\n```\n"),
        QStringLiteral("use `&amp;` for &amp;\n"),
        QStringLiteral("| a | b |\n|---|---|\n| x |\n```\n"
                       "grep foo | wc -l\n``` | count |\n"),
        QStringLiteral("| formula | note |\n|---|---|\n"
                       "| \\[x = 2\\] | fine |\n"),
    };
}

void TestLlmNormalizer::testIdempotenceOnAllFixtures()
{
    // parse runs on every load, so a second pass over already-fixed text
    // must change nothing.
    const QStringList fixtures = allFixtures() + validMarkdownCorpus();
    for (const QString &fixture : fixtures) {
        const QString once = LlmNormalizer::normalize(fixture);
        QCOMPARE(LlmNormalizer::normalize(once), once);
    }
}

void TestLlmNormalizer::testCanonicalCorpusFixedPoint()
{
    // For any document produced by DocumentSerializer::serialize (holding
    // none of the LLM constructs), normalize returns its input
    // byte-identically — Kvit-authored files keep their byte-identical
    // round-trip guarantee.
    BlockModel model;
    model.insertBlockInternal(0, Block::Heading1, "Title & more");
    model.insertBlockInternal(1, Block::Paragraph,
                              "Prose with $x^2$ and `code | span`");
    model.insertBlockInternal(2, Block::BulletList, "item one");
    model.insertBlockInternal(3, Block::NumberedList, "item two");
    model.insertBlockInternal(4, Block::Todo, "task");
    model.insertBlockInternal(5, Block::Quote, "quoted line");
    // The trap from the plan review: a code block whose content contains a
    // fence-plus-pipe line directly after a table must not trigger the
    // fix-1 repair.
    model.insertBlockInternal(6, Block::Table,
        "| a | b |\n|---|---|\n| 1 | 2 |");
    model.insertBlockInternal(7, Block::CodeBlock,
        "x ``` | y |\nif (a && b)");
    model.insertBlockInternal(8, Block::MathBlock,
        "C = \\begin{bmatrix}\n1 & 2\n\\end{bmatrix}");
    model.insertBlockInternal(9, Block::Divider, "");
    model.insertBlockInternal(10, Block::Paragraph,
        "tail with \\( unmatched and a $ sign");

    DocumentSerializer serializer;
    const QString canonical = serializer.serialize(&model);
    QCOMPARE(LlmNormalizer::normalize(canonical), canonical);
}

QStringList TestLlmNormalizer::validMarkdownCorpus() const
{
    return {
        // Pipes in prose, with and without a fence following.
        QStringLiteral("The | character separates alternatives: a | b | c.\n"
                       "Regexes use it too, as in /foo|bar/.\n"),
        QStringLiteral("Options: a | b\n```bash\nls\n```\n"),
        QStringLiteral("- shell pipe: `ls | wc`\n```bash\nls\n```\n"),
        QStringLiteral("Columns: name | value\n"
                       "```\nWrite ``` to open a fence.\n```\n"),
        // Fenced code whose content holds backtick runs.
        QStringLiteral("```\na ``` b ```` c\n```\n"),
        QStringLiteral("````\n```python\nprint(1)\n```\n````\n"),
        QStringLiteral("`````\n````md\n```js\n1\n```\n````\n`````\n"),
        QStringLiteral("Pipe | here\n"
                       "`````\n````md\n```js\n1\n```\n````\n`````\n"),
        // Tables next to code blocks, with and without a blank line.
        QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |\n```\ncode\n```\n"),
        QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |\n"
                       "\n```\n``` | y |\n```\n"),
        QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |\n"
                       "```\n``` | y |\n```\n"),
        QStringLiteral("| a | b |\n|---|---|\n| 1 | 2 |\n"
                       "````\n```js\nx\n```\n````\n"),
        QStringLiteral("```\ncode\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n"),
        // A delimiter-shaped line that begins no table.
        QStringLiteral("---\n```\n``` | x |\n```\n"),
        // Tilde fences, indented fences, and ordinary structure.
        QStringLiteral("| a | b |\n|---|---|\n| x |\n"
                       "~~~\ncode ``` | y |\n~~~\n"),
        QStringLiteral("Text | with pipe\n  ```\n  indented ``` | z |\n  ```\n"),
        QStringLiteral("# Title\n\nA paragraph.\n\n"
                       "1. one\n2. two\n\n> quoted\n\n| a |\n|---|\n| 1 |\n"),
    };
}

void TestLlmNormalizer::testValidMarkdownCorpusUnchanged()
{
    // The property the normalizer owes every user who never touched an LLM:
    // valid Markdown holding none of the targeted constructs comes back
    // byte-identical. Anything looser here rewrites files on open.
    const QStringList corpus = validMarkdownCorpus();
    for (const QString &doc : corpus) {
        const QString out = LlmNormalizer::normalize(doc);
        QVERIFY2(out == doc,
                 qPrintable(QStringLiteral("normalize rewrote a valid "
                                           "document.\n--- in ---\n%1"
                                           "--- out ---\n%2")
                                .arg(doc, out)));
    }
}

void TestLlmNormalizer::testLookaheadBoundKeepsLinearTime()
{
    // A pathological unclosed \[ followed by megabytes of text must
    // normalize in linear time and leave the text literal (the multi-line
    // constructs give up after a fixed window).
    QString big;
    big.reserve(2 * 1024 * 1024);
    big += QStringLiteral("\\[\n");
    const QString filler = QStringLiteral(
        "plain filler line with some | pipes and text\n");
    while (big.size() < 2 * 1024 * 1024)
        big += filler;

    QElapsedTimer timer;
    timer.start();
    const QString out = LlmNormalizer::normalize(big);
    const qint64 elapsed = timer.elapsed();

    QCOMPARE(out, big);
    QVERIFY2(elapsed < 5000,
             qPrintable(QStringLiteral("normalize took %1 ms").arg(elapsed)));

    // Same bound for the table-fence repair: a closing line beyond the
    // window leaves the fence alone (run-to-EOF, no repair).
    QString table = QStringLiteral("| a | b |\n|---|---|\n| x |\n```\n");
    for (int i = 0; i < 300; ++i)
        table += QStringLiteral("code line\n");
    table += QStringLiteral("``` | late |\n");
    const QString tableOut = LlmNormalizer::normalize(table);
    QVERIFY(tableOut.contains(QStringLiteral("\n```\n")));
}

QTEST_MAIN(TestLlmNormalizer)
#include "test_llmnormalizer.moc"
