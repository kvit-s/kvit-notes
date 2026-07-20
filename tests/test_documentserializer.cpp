// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "documentserializer.h"
#include "blockmodel.h"
#include "block.h"
#include "undostack.h"

class TestDocumentSerializer : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // Serialization tests (Step 6a)
    void testSerializeEmpty();
    void testSerializeSingleParagraph();
    void testSerializeSingleHeading1();
    void testSerializeSingleHeading2();
    void testSerializeSingleHeading3();
    void testSerializeMultipleParagraphs();
    void testSerializeHeadings();
    void testSerializeMixed();
    void testSerializeInlineFormatting();
    void testSerializeEmptyBlock();
    void testSerializeWithoutTrailingNewline();

    // Parsing tests (Step 6b)
    void testParseEmpty();
    void testParseWhitespaceOnly();
    void testParseSingleParagraph();
    void testParseSingleHeading1();
    void testParseSingleHeading2();
    void testParseSingleHeading3();
    void testParseMultipleParagraphs();
    void testParseHeadings();
    void testParseMixed();
    void testParseInlineFormatting();
    void testParseNoBlankLines();
    void testParseMultipleBlankLines();
    void testParseHeadingWithoutSpace();
    void testParseHeading4();
    void testParseHeading5PlusStaysLiteral();

    // Round-trip tests
    void testRoundTripSimple();
    void testRoundTripMixed();
    void testRoundTripWithFormatting();
    void testRoundTripNewInlineTypes();

    // loadIntoModel tests
    void testLoadIntoModelEmpty();
    void testLoadIntoModelSimple();
    void testLoadIntoModelClearsExisting();

    // Block types wave 1
    void testParseBulletList();
    void testParseNumberedList();
    void testParseTodo();
    void testParseQuote();
    void testParseCodeBlock();
    void testIngestTagsCharacterDiagram();
    void testIngestTaggingIsIdempotent();
    void testIngestLeavesExplicitLanguages();
    void testIngestLeavesOrdinaryCode();
    void testIngestStraightensDiagramFences();
    void testDiagramFenceRoundTrips();
    void testParseDivider();
    void testParseImageAndMediaBlocks();
    void testImageRoundTrip();
    void testParseCalloutBlocks();
    void testCalloutRoundTrip();
    void testParseTableBlock();
    void testTableRoundTripAndNormalize();
    void testParseMathBlock();
    void testMathBlockRoundTrip();
    void testNestedQuotes();
    void testParseIndentation();
    void testSerializeBlockTypes();
    void testSerializeTightLists();
    void testSerializeNestedNumbering();
    void testRoundTripBlockTypes();
    void testRoundTripCodeEdgeCases();
    void testParseIndentedFence();
    void testParseTildeFence();
    void testEmojiRoundTripsInAllBlockTypes();
    void testNormalizations();
    void testEdgeSyntaxStaysLiteral();
    void testLoadIntoModelBlockTypes();

    // Block-selection clipboard
    void testSerializeBlocksSubset();
    void testSerializeBlocksTightness();
    void testSerializeBlocksOrdinalsFromDocument();
    void testInsertMarkdownAt();
    void testInsertMarkdownAtSingleUndoStep();
    void testInsertPlainTextAt();
    void testInsertPlainTextAtSingleUndoStep();
    void testTocFenceRoundTripAndDerivedKind();
    void testWikiLinksRoundTripByteIdentical();
    void testQueryFenceRoundTripAndDerivedKind();

private:
    // Serialize -> parse -> serialize must reproduce the exact bytes for
    // anything the editor itself writes.
    void verifyByteIdentical(const QString &markdown)
    {
        BlockModel model;
        m_serializer->loadIntoModel(&model, markdown);
        QCOMPARE(m_serializer->serialize(&model), markdown);
    }

    BlockModel *m_model = nullptr;
    DocumentSerializer *m_serializer = nullptr;
};

void TestDocumentSerializer::initTestCase()
{
}

void TestDocumentSerializer::cleanupTestCase()
{
}

void TestDocumentSerializer::init()
{
    m_model = new BlockModel();
    m_serializer = new DocumentSerializer();
}

void TestDocumentSerializer::cleanup()
{
    delete m_serializer;
    delete m_model;
    m_serializer = nullptr;
    m_model = nullptr;
}

// ============================================================================
// Serialization tests (Step 6a)
// ============================================================================

void TestDocumentSerializer::testSerializeEmpty()
{
    // Empty model should produce empty string
    QString result = m_serializer->serialize(m_model);
    QCOMPARE(result, QString());
}

void TestDocumentSerializer::testSerializeSingleParagraph()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Hello world.");

    QString result = m_serializer->serialize(m_model);
    QCOMPARE(result, QString("Hello world.\n"));
}

void TestDocumentSerializer::testSerializeSingleHeading1()
{
    m_model->insertBlockInternal(0, Block::Heading1, "Title");

    QString result = m_serializer->serialize(m_model);
    QCOMPARE(result, QString("# Title\n"));
}

void TestDocumentSerializer::testSerializeSingleHeading2()
{
    m_model->insertBlockInternal(0, Block::Heading2, "Section");

    QString result = m_serializer->serialize(m_model);
    QCOMPARE(result, QString("## Section\n"));
}

void TestDocumentSerializer::testSerializeSingleHeading3()
{
    m_model->insertBlockInternal(0, Block::Heading3, "Subsection");

    QString result = m_serializer->serialize(m_model);
    QCOMPARE(result, QString("### Subsection\n"));
}

void TestDocumentSerializer::testSerializeMultipleParagraphs()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "First paragraph.");
    m_model->insertBlockInternal(1, Block::Paragraph, "Second paragraph.");

    QString result = m_serializer->serialize(m_model);
    QString expected = "First paragraph.\n\nSecond paragraph.\n";
    QCOMPARE(result, expected);
}

void TestDocumentSerializer::testSerializeHeadings()
{
    m_model->insertBlockInternal(0, Block::Heading1, "Title");
    m_model->insertBlockInternal(1, Block::Heading2, "Section");
    m_model->insertBlockInternal(2, Block::Heading3, "Subsection");
    m_model->insertBlockInternal(3, Block::Heading4, "Minor");

    QString result = m_serializer->serialize(m_model);
    QString expected = "# Title\n\n## Section\n\n### Subsection\n\n#### Minor\n";
    QCOMPARE(result, expected);
}

void TestDocumentSerializer::testSerializeMixed()
{
    m_model->insertBlockInternal(0, Block::Heading1, "Welcome");
    m_model->insertBlockInternal(1, Block::Paragraph, "Hello **world**.");
    m_model->insertBlockInternal(2, Block::Heading2, "Details");
    m_model->insertBlockInternal(3, Block::Paragraph, "More *italic* text.");

    QString result = m_serializer->serialize(m_model);
    QString expected =
        "# Welcome\n\n"
        "Hello **world**.\n\n"
        "## Details\n\n"
        "More *italic* text.\n";
    QCOMPARE(result, expected);
}

void TestDocumentSerializer::testSerializeInlineFormatting()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Text with **bold** and *italic* formatting.");

    QString result = m_serializer->serialize(m_model);
    QCOMPARE(result, QString("Text with **bold** and *italic* formatting.\n"));
}

void TestDocumentSerializer::testSerializeEmptyBlock()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Before");
    m_model->insertBlockInternal(1, Block::Paragraph, "");
    m_model->insertBlockInternal(2, Block::Paragraph, "After");

    QString result = m_serializer->serialize(m_model);
    // Empty block produces empty string between separators
    QString expected = "Before\n\n\n\nAfter\n";
    QCOMPARE(result, expected);
}

void TestDocumentSerializer::testSerializeWithoutTrailingNewline()
{
    m_serializer->setTrailingNewline(false);
    m_model->insertBlockInternal(0, Block::Paragraph, "Hello world.");

    QString result = m_serializer->serialize(m_model);
    QCOMPARE(result, QString("Hello world."));
}

// ============================================================================
// Parsing tests (Step 6b)
// ============================================================================

void TestDocumentSerializer::testParseEmpty()
{
    auto blocks = m_serializer->parse("");
    QCOMPARE(blocks.size(), 0);
}

void TestDocumentSerializer::testParseWhitespaceOnly()
{
    auto blocks = m_serializer->parse("   \n\n  ");
    QCOMPARE(blocks.size(), 0);
}

void TestDocumentSerializer::testParseSingleParagraph()
{
    auto blocks = m_serializer->parse("Hello world.\n");

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[0].content, QString("Hello world."));
}

void TestDocumentSerializer::testParseSingleHeading1()
{
    auto blocks = m_serializer->parse("# Title\n");

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Heading1);
    QCOMPARE(blocks[0].content, QString("Title"));
}

void TestDocumentSerializer::testParseSingleHeading2()
{
    auto blocks = m_serializer->parse("## Section\n");

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Heading2);
    QCOMPARE(blocks[0].content, QString("Section"));
}

void TestDocumentSerializer::testParseSingleHeading3()
{
    auto blocks = m_serializer->parse("### Subsection\n");

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Heading3);
    QCOMPARE(blocks[0].content, QString("Subsection"));
}

void TestDocumentSerializer::testParseMultipleParagraphs()
{
    QString markdown = "First paragraph.\n\nSecond paragraph.\n";
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[0].content, QString("First paragraph."));
    QCOMPARE(blocks[1].type, Block::Paragraph);
    QCOMPARE(blocks[1].content, QString("Second paragraph."));
}

void TestDocumentSerializer::testParseHeadings()
{
    QString markdown = "# Title\n\n## Section\n\n### Subsection\n\n#### Minor\n";
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 4);
    QCOMPARE(blocks[0].type, Block::Heading1);
    QCOMPARE(blocks[0].content, QString("Title"));
    QCOMPARE(blocks[1].type, Block::Heading2);
    QCOMPARE(blocks[1].content, QString("Section"));
    QCOMPARE(blocks[2].type, Block::Heading3);
    QCOMPARE(blocks[2].content, QString("Subsection"));
    QCOMPARE(blocks[3].type, Block::Heading4);
    QCOMPARE(blocks[3].content, QString("Minor"));
}

void TestDocumentSerializer::testParseMixed()
{
    QString markdown =
        "# Welcome\n\n"
        "This is a paragraph with **bold** text.\n\n"
        "## Section Title\n\n"
        "Another paragraph here.\n";

    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 4);
    QCOMPARE(blocks[0].type, Block::Heading1);
    QCOMPARE(blocks[0].content, QString("Welcome"));
    QCOMPARE(blocks[1].type, Block::Paragraph);
    QCOMPARE(blocks[1].content, QString("This is a paragraph with **bold** text."));
    QCOMPARE(blocks[2].type, Block::Heading2);
    QCOMPARE(blocks[2].content, QString("Section Title"));
    QCOMPARE(blocks[3].type, Block::Paragraph);
    QCOMPARE(blocks[3].content, QString("Another paragraph here."));
}

void TestDocumentSerializer::testParseInlineFormatting()
{
    QString markdown = "Text with **bold** and *italic*.\n";
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 1);
    // Inline formatting should be preserved as-is
    QCOMPARE(blocks[0].content, QString("Text with **bold** and *italic*."));
}

void TestDocumentSerializer::testParseNoBlankLines()
{
    // Content without blank lines should be treated as single paragraph
    QString markdown = "Line one\nLine two\nLine three";
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[0].content, QString("Line one\nLine two\nLine three"));
}

void TestDocumentSerializer::testParseMultipleBlankLines()
{
    // Multiple blank lines should be treated same as single blank line
    QString markdown = "First\n\n\n\nSecond";
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].content, QString("First"));
    QCOMPARE(blocks[1].content, QString("Second"));
}

void TestDocumentSerializer::testParseHeadingWithoutSpace()
{
    // #text without space should be treated as paragraph
    QString markdown = "#NoSpace";
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[0].content, QString("#NoSpace"));
}

void TestDocumentSerializer::testParseHeading4()
{
    // "#### " parses as the fourth heading level of features.md
    // §1.2.2 (this flips the earlier "H4+ not supported" pin — the §4.2
    // block menu lists Heading 1-4, so the type now exists).
    QString markdown = "#### Minor Heading";
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Heading4);
    QCOMPARE(blocks[0].content, QString("Minor Heading"));

    verifyByteIdentical(QStringLiteral("#### Minor Heading\n"));
}

void TestDocumentSerializer::testParseHeading5PlusStaysLiteral()
{
    // Five and six hashes map to Heading4, superseding the earlier
    // stays-literal pin: LLMs use deep heading levels, and visible hashes
    // are worse than a demoted heading. Lossy (a reload demotes "#####"
    // to "####"), accepted.
    auto blocks = m_serializer->parse(QStringLiteral("##### Was Too Deep"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Heading4);
    QCOMPARE(blocks[0].content, QString("Was Too Deep"));

    blocks = m_serializer->parse(QStringLiteral("###### Deeper Still"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Heading4);
    QCOMPARE(blocks[0].content, QString("Deeper Still"));

    // Seven or more hashes still stay literal.
    blocks = m_serializer->parse(QStringLiteral("####### too deep"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[0].content, QString("####### too deep"));
    verifyByteIdentical(QStringLiteral("####### too deep\n"));
}

// ============================================================================
// Round-trip tests
// ============================================================================

void TestDocumentSerializer::testRoundTripSimple()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Hello world.");

    QString markdown = m_serializer->serialize(m_model);
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[0].content, QString("Hello world."));
}

void TestDocumentSerializer::testRoundTripMixed()
{
    m_model->insertBlockInternal(0, Block::Heading1, "Title");
    m_model->insertBlockInternal(1, Block::Paragraph, "Content");
    m_model->insertBlockInternal(2, Block::Heading2, "Section");
    m_model->insertBlockInternal(3, Block::Paragraph, "More content");

    QString markdown = m_serializer->serialize(m_model);

    BlockModel model2;
    m_serializer->loadIntoModel(&model2, markdown);

    QCOMPARE(model2.count(), m_model->count());
    for (int i = 0; i < m_model->count(); ++i) {
        QCOMPARE(model2.blockAt(i)->blockType(), m_model->blockAt(i)->blockType());
        QCOMPARE(model2.blockAt(i)->content(), m_model->blockAt(i)->content());
    }
}

void TestDocumentSerializer::testRoundTripWithFormatting()
{
    m_model->insertBlockInternal(0, Block::Paragraph, "Text with **bold** and *italic* formatting.");

    QString markdown = m_serializer->serialize(m_model);
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].content, QString("Text with **bold** and *italic* formatting."));
}

void TestDocumentSerializer::testRoundTripNewInlineTypes()
{
    // The new inline syntax — and edge syntax that never parses to a
    // span — survives save/parse verbatim.
    const QString formatted =
        QStringLiteral("Mix ~~strike~~ `code` ==mark== ++under++ [a](http://x) http://y.io done.");
    const QString edges =
        QStringLiteral("Edges ~~open `tick ==== ++++ a~b [a](b c) [x]y stay literal");
    m_model->insertBlockInternal(0, Block::Paragraph, formatted);
    m_model->insertBlockInternal(1, Block::Paragraph, edges);

    QString markdown = m_serializer->serialize(m_model);
    auto blocks = m_serializer->parse(markdown);

    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].content, formatted);
    QCOMPARE(blocks[1].content, edges);
}

// ============================================================================
// loadIntoModel tests
// ============================================================================

void TestDocumentSerializer::testLoadIntoModelEmpty()
{
    // Loading empty should create single empty paragraph
    m_serializer->loadIntoModel(m_model, "");

    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Paragraph);
    QCOMPARE(m_model->blockAt(0)->content(), QString(""));
}

void TestDocumentSerializer::testLoadIntoModelSimple()
{
    QString markdown = "# Title\n\nParagraph content.\n";
    m_serializer->loadIntoModel(m_model, markdown);

    QCOMPARE(m_model->count(), 2);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Heading1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("Title"));
    QCOMPARE(m_model->blockAt(1)->blockType(), Block::Paragraph);
    QCOMPARE(m_model->blockAt(1)->content(), QString("Paragraph content."));
}

void TestDocumentSerializer::testLoadIntoModelClearsExisting()
{
    // Add some existing blocks
    m_model->insertBlockInternal(0, Block::Paragraph, "Existing content");
    m_model->insertBlockInternal(1, Block::Paragraph, "More existing");
    QCOMPARE(m_model->count(), 2);

    // Load new content
    QString markdown = "# New Title\n";
    m_serializer->loadIntoModel(m_model, markdown);

    // Should have replaced existing blocks
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Heading1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("New Title"));
}

// ============================================================================
// Block types wave 1
// ============================================================================

void TestDocumentSerializer::testParseBulletList()
{
    // Consecutive marker lines are separate blocks (a tight list); both
    // bullet characters parse, "- " is canonical
    auto blocks = m_serializer->parse("- first\n* second\n- ");

    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[0].type, Block::BulletList);
    QCOMPARE(blocks[0].content, QString("first"));
    QCOMPARE(blocks[1].type, Block::BulletList);
    QCOMPARE(blocks[1].content, QString("second"));
    QCOMPARE(blocks[2].content, QString(""));
}

void TestDocumentSerializer::testParseNumberedList()
{
    auto blocks = m_serializer->parse("1. one\n2. two\n17. seventeen");

    QCOMPARE(blocks.size(), 3);
    for (const auto &b : blocks)
        QCOMPARE(b.type, Block::NumberedList);
    QCOMPARE(blocks[0].content, QString("one"));
    QCOMPARE(blocks[2].content, QString("seventeen"));  // ordinal is computed, not stored
}

void TestDocumentSerializer::testParseTodo()
{
    auto blocks = m_serializer->parse("- [ ] open\n- [x] done\n- [X] also done\n* [ ] star");

    QCOMPARE(blocks.size(), 4);
    for (const auto &b : blocks)
        QCOMPARE(b.type, Block::Todo);
    QCOMPARE(blocks[0].content, QString("open"));
    QCOMPARE(blocks[0].checked, false);
    QCOMPARE(blocks[1].checked, true);
    QCOMPARE(blocks[2].checked, true);
    QCOMPARE(blocks[3].checked, false);
}

void TestDocumentSerializer::testParseQuote()
{
    // A contiguous run of quote lines is ONE block; a bare ">" is an
    // empty content line; a blank line starts a new quote block
    auto blocks = m_serializer->parse("> line one\n> line two\n>\n> after gap\n\n> separate");

    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].type, Block::Quote);
    QCOMPARE(blocks[0].content, QString("line one\nline two\n\nafter gap"));
    QCOMPARE(blocks[1].type, Block::Quote);
    QCOMPARE(blocks[1].content, QString("separate"));
}

void TestDocumentSerializer::testParseCodeBlock()
{
    // Fence content is verbatim: blank lines, marker-shaped lines, and
    // leading whitespace all survive; the info string is the language
    auto blocks = m_serializer->parse(
        "```python\ndef f():\n    return 1\n\n# not a heading\n- not a bullet\n```\n");

    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::CodeBlock);
    QCOMPARE(blocks[0].language, QString("python"));
    QCOMPARE(blocks[0].content,
             QString("def f():\n    return 1\n\n# not a heading\n- not a bullet"));

    // Empty code block
    auto empty = m_serializer->parse("```\n```");
    QCOMPARE(empty.size(), 1);
    QCOMPARE(empty[0].type, Block::CodeBlock);
    QCOMPARE(empty[0].content, QString());

    // Unclosed fence runs to end of file (CommonMark behavior)
    auto open = m_serializer->parse("```js\nlet x = 1");
    QCOMPARE(open.size(), 1);
    QCOMPARE(open[0].type, Block::CodeBlock);
    QCOMPARE(open[0].language, QString("js"));
    QCOMPARE(open[0].content, QString("let x = 1"));
}

// A compact two-box character diagram the classifier accepts (two framed
// regions joined by a connector). Used across the ingest-tagging tests.
static const char *kDiagramBody =
    "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90\n"  // ┌─────────┐
    "\xE2\x94\x82  START  \xE2\x94\x82\n"                                                                                       // │  START  │
    "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98\n"  // └────┬────┘
    "     \xE2\x94\x82\n"                                                                                                       //      │
    "     \xE2\x96\xBC\n"                                                                                                       //      ▼
    "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90\n"  // ┌─────────┐
    "\xE2\x94\x82   END   \xE2\x94\x82\n"                                                                                       // │   END   │
    "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98";   // └─────────┘

void TestDocumentSerializer::testIngestTagsCharacterDiagram()
{
    // An untagged fence whose body is a character diagram is rewritten to
    // `diagram` at ingest; the body is byte-identical.
    const QString body = QString::fromUtf8(kDiagramBody);
    const QString md = "```\n" + body + "\n```\n";
    auto blocks = m_serializer->parse(md);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::CodeBlock);
    QCOMPARE(blocks[0].language, QString("diagram"));
    QCOMPARE(blocks[0].content, body);

    // `text`, `plaintext`, and `ascii` wrappers are equally eligible.
    for (const QString &lang : {QStringLiteral("text"), QStringLiteral("plaintext"),
                                QStringLiteral("ascii")}) {
        auto b = m_serializer->parse("```" + lang + "\n" + body + "\n```\n");
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].language, QString("diagram"));
    }
}

void TestDocumentSerializer::testIngestTaggingIsIdempotent()
{
    // Reparsing already-tagged output never re-examines the fence, so the
    // language stays `diagram` and the pass is a fixed point.
    const QString body = QString::fromUtf8(kDiagramBody);
    auto once = m_serializer->parse("```\n" + body + "\n```\n");
    QCOMPARE(once[0].language, QString("diagram"));
    auto twice = m_serializer->parse("```diagram\n" + body + "\n```\n");
    QCOMPARE(twice.size(), 1);
    QCOMPARE(twice[0].language, QString("diagram"));
    QCOMPARE(twice[0].content, body);
}

void TestDocumentSerializer::testIngestLeavesExplicitLanguages()
{
    // A `plain` fence is the code opt-out and is never classified, even when
    // its body would otherwise look like a diagram.
    const QString body = QString::fromUtf8(kDiagramBody);
    auto plain = m_serializer->parse("```plain\n" + body + "\n```\n");
    QCOMPARE(plain[0].language, QString("plain"));
    // An unrelated explicit language is untouched too.
    auto py = m_serializer->parse("```python\n" + body + "\n```\n");
    QCOMPARE(py[0].language, QString("python"));
}

void TestDocumentSerializer::testIngestLeavesOrdinaryCode()
{
    // A `tree`-listing-shaped untagged fence stays code (no framed regions).
    auto tree = m_serializer->parse(
        "```\nproject/\n"
        "\xE2\x94\x9C\xE2\x94\x80\xE2\x94\x80 src/\n"       // ├── src/
        "\xE2\x94\x82   \xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 main.cpp\n"  // │   └── main.cpp
        "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80 README.md\n```\n");
    QCOMPARE(tree.size(), 1);
    QCOMPARE(tree[0].language, QString());
}

void TestDocumentSerializer::testIngestStraightensDiagramFences()
{
    // Ingest straightening: a diagram fence with a ragged edge — the
    // top-right corner two columns short of its walls — is repaired
    // during parse, whether it arrives pre-tagged or gets tagged by the
    // classifier. A `plain` fence opts out of the whole diagram pass
    // family, repair included.
    const QString flawed = QString::fromUtf8(
        "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90\n"          // ┌─────┐
        "\xE2\x94\x82  A     \xE2\x94\x82\n"                                                              // │  A     │
        "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\xAC\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98\n" // └────┬───┘
        "     \xE2\x94\x82\n"                                                                             //      │
        "     \xE2\x96\xBC\n"                                                                             //      ▼
        "\xE2\x94\x8C\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x90\n" // ┌────────┐
        "\xE2\x94\x82  B     \xE2\x94\x82\n"                                                              // │  B     │
        "\xE2\x94\x94\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x80\xE2\x94\x98");  // └────────┘
    auto tagged = m_serializer->parse("```diagram\n" + flawed + "\n```\n");
    QCOMPARE(tagged.size(), 1);
    QVERIFY(tagged[0].content != flawed);
    // The first box's top-right corner now sits at the wall column (9).
    QCOMPARE(tagged[0].content.split(u'\n').first().indexOf(QChar(0x2510)), 9);
    // Repaired output is a fixed point of another parse.
    auto again = m_serializer->parse("```diagram\n" + tagged[0].content + "\n```\n");
    QCOMPARE(again[0].content, tagged[0].content);
    // The `plain` opt-out skips repair entirely.
    auto plain = m_serializer->parse("```plain\n" + flawed + "\n```\n");
    QCOMPARE(plain[0].content, flawed);
}

void TestDocumentSerializer::testDiagramFenceRoundTrips()
{
    // The tagged fence round-trips through the model verbatim.
    const QString body = QString::fromUtf8(kDiagramBody);
    BlockModel model;
    m_serializer->loadIntoModel(&model, "```\n" + body + "\n```\n");
    QCOMPARE(model.count(), 1);
    const QString out = m_serializer->serialize(&model);
    QVERIFY(out.contains(QLatin1String("```diagram")));
    // A second load of the serialized output is stable.
    BlockModel model2;
    m_serializer->loadIntoModel(&model2, out);
    QCOMPARE(m_serializer->serialize(&model2), out);
}

void TestDocumentSerializer::testParseDivider()
{
    auto blocks = m_serializer->parse("above\n\n---\n\n***\n\nbelow");

    QCOMPARE(blocks.size(), 4);
    QCOMPARE(blocks[1].type, Block::Divider);
    QCOMPARE(blocks[2].type, Block::Divider);
    QCOMPARE(blocks[1].content, QString());
}

void TestDocumentSerializer::testParseImageAndMediaBlocks()
{
    // A lone image expression on its own line becomes an Image block; a
    // media extension becomes a Media block; ![…] mid-prose stays literal
    // in a paragraph.
    auto blocks = m_serializer->parse(
        "intro\n\n![a cat|300](cats/tom.png \"My cat\")\n\n"
        "![clip](vid.mp4)\n\nsee ![inline](x.png) here");
    QCOMPARE(blocks.size(), 4);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[1].type, Block::Image);
    QCOMPARE(blocks[1].content, QString("![a cat|300](cats/tom.png \"My cat\")"));
    QCOMPARE(blocks[2].type, Block::Media);
    QCOMPARE(blocks[2].content, QString("![clip](vid.mp4)"));
    // The inline-image paragraph is a plain paragraph, literal expression.
    QCOMPARE(blocks[3].type, Block::Paragraph);
    QCOMPARE(blocks[3].content, QString("see ![inline](x.png) here"));
}

void TestDocumentSerializer::testImageRoundTrip()
{
    // An Image block serializes to exactly its content (the markdown line),
    // so save→reopen is byte-identical.
    m_model->insertBlockInternal(0, Block::Image,
        "![alt|200](assets/pic.png \"cap\")");
    const QString out = m_serializer->serialize(m_model);
    QCOMPARE(out, QString("![alt|200](assets/pic.png \"cap\")\n"));
    // Reparsing yields the same Image block.
    auto reparsed = m_serializer->parse(out);
    QCOMPARE(reparsed.size(), 1);
    QCOMPARE(reparsed[0].type, Block::Image);
    QCOMPARE(reparsed[0].content, QString("![alt|200](assets/pic.png \"cap\")"));
}

void TestDocumentSerializer::testParseCalloutBlocks()
{
    // A quote whose first line is a callout header becomes a Callout block;
    // the type reuses `language`, the fold state `checked`, the title its own
    // field, and the remaining lines are the body. A plain quote stays a
    // quote.
    auto blocks = m_serializer->parse(
        "> [!warning]- Heads up\n> body line one\n> body line two\n\n"
        "> [!info] Title only\n\n> just a quote\n\n> [!note]\n> untitled body");
    QCOMPARE(blocks.size(), 4);

    QCOMPARE(blocks[0].type, Block::Callout);
    QCOMPARE(blocks[0].language, QString("warning"));
    QCOMPARE(blocks[0].checked, true);                    // folded
    QCOMPARE(blocks[0].calloutTitle, QString("Heads up"));
    QCOMPARE(blocks[0].content, QString("body line one\nbody line two"));

    QCOMPARE(blocks[1].type, Block::Callout);
    QCOMPARE(blocks[1].language, QString("info"));
    QCOMPARE(blocks[1].checked, false);                   // expanded
    QCOMPARE(blocks[1].calloutTitle, QString("Title only"));
    QCOMPARE(blocks[1].content, QString());               // no body

    QCOMPARE(blocks[2].type, Block::Quote);               // plain quote

    QCOMPARE(blocks[3].type, Block::Callout);
    QCOMPARE(blocks[3].language, QString("note"));
    QCOMPARE(blocks[3].calloutTitle, QString());          // untitled
    QCOMPARE(blocks[3].content, QString("untitled body"));
}

void TestDocumentSerializer::testCalloutRoundTrip()
{
    // Byte-identical round-trip for the canonical forms.
    const QStringList docs = {
        "> [!warning]- Heads up\n> body one\n> body two",
        "> [!info] Title only",
        "> [!note]\n> untitled body",
        "> [!tip]-",   // folded, no title, no body
    };
    for (const QString &doc : docs) {
        auto blocks = m_serializer->parse(doc);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].type, Block::Callout);
        m_model->clear();
        Block::State s;
        s.type = blocks[0].type;
        s.content = blocks[0].content;
        s.checked = blocks[0].checked;
        s.language = blocks[0].language;
        s.calloutTitle = blocks[0].calloutTitle;
        m_model->insertBlockInternal(0, s);
        QCOMPARE(m_serializer->serialize(m_model), doc + "\n");
    }

    // A hand-authored '+' expanded marker normalizes to no marker.
    auto b = m_serializer->parse("> [!info]+ Expandable");
    m_model->clear();
    Block::State s;
    s.type = b[0].type; s.content = b[0].content; s.checked = b[0].checked;
    s.language = b[0].language; s.calloutTitle = b[0].calloutTitle;
    m_model->insertBlockInternal(0, s);
    QCOMPARE(m_serializer->serialize(m_model), QString("> [!info] Expandable\n"));
}

void TestDocumentSerializer::testParseTableBlock()
{
    auto blocks = m_serializer->parse(
        "before\n\n| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |\n\nafter");
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[0].type, Block::Paragraph);
    QCOMPARE(blocks[1].type, Block::Table);
    QCOMPARE(blocks[1].content, QString("| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |"));
    QCOMPARE(blocks[2].type, Block::Paragraph);
    QCOMPARE(blocks[2].content, QString("after"));
}

void TestDocumentSerializer::testTableRoundTripAndNormalize()
{
    // A canonical table round-trips byte-identically.
    const QString canonical = "| A | B |\n| --- | --- |\n| 1 | 2 |";
    m_model->clear();
    m_model->insertBlockInternal(0, Block::Table, canonical);
    QCOMPARE(m_serializer->serialize(m_model), canonical + "\n");

    // A padded/ragged hand-authored table squares up on save.
    m_model->clear();
    m_model->insertBlockInternal(0, Block::Table,
        "|  A  | B |\n|:----|---|\n| 1 |\n| 2 | 3 | 4 |");
    QCOMPARE(m_serializer->serialize(m_model),
             QString("| A | B |\n| :--- | --- |\n| 1 |  |\n| 2 | 3 |\n"));
}

void TestDocumentSerializer::testParseMathBlock()
{
    // A $$ fence on its own lines becomes a MathBlock with verbatim content.
    auto blocks = m_serializer->parse("$$\nE = mc^2\n$$");
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::MathBlock);
    QCOMPARE(blocks[0].content, QString("E = mc^2"));

    // The single-line $$x$$ form is recognized too.
    blocks = m_serializer->parse("$$a+b$$");
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::MathBlock);
    QCOMPARE(blocks[0].content, QString("a+b"));

    // Multi-line content is verbatim: blank lines and marker-shaped rows
    // survive (an aligned environment with '\\' and '&').
    blocks = m_serializer->parse(
        "$$\n\\begin{aligned}\na &= b \\\\\n\nc &= d\n\\end{aligned}\n$$");
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::MathBlock);
    QVERIFY(blocks[0].content.contains(QLatin1String("aligned")));
    QVERIFY(blocks[0].content.contains(QLatin1String("a &= b")));

    // A prose line carrying an inline dollar is not a math block.
    blocks = m_serializer->parse("it costs $5 today");
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::Paragraph);
}

void TestDocumentSerializer::testMathBlockRoundTrip()
{
    // The canonical multi-line form round-trips byte-identically.
    const QString canonical = "$$\nE = mc^2\n$$";
    m_model->clear();
    m_model->insertBlockInternal(0, Block::MathBlock, "E = mc^2");
    QCOMPARE(m_serializer->serialize(m_model), canonical + "\n");

    // A hand-authored single-line $$x^2$$ normalizes to the multi-line form.
    m_model->clear();
    auto blocks = m_serializer->parse("$$x^2$$");
    m_model->insertBlockInternal(0, blocks[0].type, blocks[0].content);
    QCOMPARE(m_serializer->serialize(m_model), QString("$$\nx^2\n$$\n"));
}

void TestDocumentSerializer::testNestedQuotes()
{
    // Depth rides indentLevel; a depth change starts a new quote block.
    // A same-depth run still joins.
    auto blocks = m_serializer->parse("> outer\n> still outer\n> > inner\n> outer again");
    QCOMPARE(blocks.size(), 3);
    QCOMPARE(blocks[0].type, Block::Quote);
    QCOMPARE(blocks[0].indentLevel, 0);
    QCOMPARE(blocks[0].content, QString("outer\nstill outer"));
    QCOMPARE(blocks[1].type, Block::Quote);
    QCOMPARE(blocks[1].indentLevel, 1);
    QCOMPARE(blocks[1].content, QString("inner"));
    QCOMPARE(blocks[2].indentLevel, 0);

    // Round-trip a depth-2 quote byte-identically.
    m_model->clear();
    Block::State s;
    s.type = Block::Quote;
    s.content = "deep";
    s.indentLevel = 1;
    m_model->insertBlockInternal(0, s);
    QCOMPARE(m_serializer->serialize(m_model), QString("> > deep\n"));
}

void TestDocumentSerializer::testParseIndentation()
{
    // Two spaces per level; tabs count one level each; depth clamps at 4
    auto blocks = m_serializer->parse(
        "- zero\n  - one\n    - two\n\t- tab one\n            - clamped\n  1. num\n  - [x] todo");

    QCOMPARE(blocks.size(), 7);
    QCOMPARE(blocks[0].indentLevel, 0);
    QCOMPARE(blocks[1].indentLevel, 1);
    QCOMPARE(blocks[2].indentLevel, 2);
    QCOMPARE(blocks[3].indentLevel, 1);
    QCOMPARE(blocks[4].indentLevel, 4);
    QCOMPARE(blocks[5].type, Block::NumberedList);
    QCOMPARE(blocks[5].indentLevel, 1);
    QCOMPARE(blocks[6].type, Block::Todo);
    QCOMPARE(blocks[6].indentLevel, 1);
    QCOMPARE(blocks[6].checked, true);
}

void TestDocumentSerializer::testSerializeBlockTypes()
{
    m_model->insertBlockInternal(0, Block::BulletList, "item");
    QCOMPARE(m_serializer->serialize(m_model), QString("- item\n"));

    m_model->clear();
    m_model->insertBlockInternal(0, Block::Todo, "task");
    QCOMPARE(m_serializer->serialize(m_model), QString("- [ ] task\n"));
    m_model->blockAt(0)->setChecked(true);
    QCOMPARE(m_serializer->serialize(m_model), QString("- [x] task\n"));

    m_model->clear();
    m_model->insertBlockInternal(0, Block::Quote, "wise\n\nwords");
    QCOMPARE(m_serializer->serialize(m_model), QString("> wise\n>\n> words\n"));

    m_model->clear();
    m_model->insertBlockInternal(0, Block::CodeBlock, "x = 1");
    m_model->blockAt(0)->setLanguage("python");
    QCOMPARE(m_serializer->serialize(m_model), QString("```python\nx = 1\n```\n"));

    m_model->clear();
    m_model->insertBlockInternal(0, Block::Divider, "");
    QCOMPARE(m_serializer->serialize(m_model), QString("---\n"));
}

void TestDocumentSerializer::testSerializeTightLists()
{
    // Consecutive list-family blocks join with a single newline; any
    // other neighbor keeps the blank-line separator
    m_model->insertBlockInternal(0, Block::Paragraph, "before");
    m_model->insertBlockInternal(1, Block::BulletList, "a");
    m_model->insertBlockInternal(2, Block::Todo, "b");
    m_model->insertBlockInternal(3, Block::NumberedList, "c");
    m_model->insertBlockInternal(4, Block::Paragraph, "after");

    QCOMPARE(m_serializer->serialize(m_model),
             QString("before\n\n- a\n- [ ] b\n1. c\n\nafter\n"));
}

void TestDocumentSerializer::testSerializeNestedNumbering()
{
    // Ordinals are computed per indent level with nested restart
    Block::State item;
    item.type = Block::NumberedList;
    item.content = "one";
    m_model->insertBlockInternal(0, item);
    item.content = "one.a"; item.indentLevel = 1;
    m_model->insertBlockInternal(1, item);
    item.content = "one.b";
    m_model->insertBlockInternal(2, item);
    item.content = "two"; item.indentLevel = 0;
    m_model->insertBlockInternal(3, item);

    QCOMPARE(m_serializer->serialize(m_model),
             QString("1. one\n  1. one.a\n  2. one.b\n2. two\n"));
}

void TestDocumentSerializer::testRoundTripBlockTypes()
{
    // Everything the editor can write reloads byte-identically
    verifyByteIdentical("- alpha\n- beta\n  - beta.child\n- gamma\n");
    verifyByteIdentical("1. one\n2. two\n  1. two.a\n3. three\n");
    verifyByteIdentical("- [ ] open task\n- [x] done task\n  - [ ] subtask\n");
    verifyByteIdentical("> a quote\n> continues\n>\n> second paragraph\n");
    verifyByteIdentical("```python\ndef f():\n    return 1\n```\n");
    verifyByteIdentical("---\n");
    verifyByteIdentical("# Title\n\n- item **bold**\n1. numbered *italic*\n"
                        "- [ ] task with [link](http://x)\n\n> quoted `code`\n\n"
                        "```\nliteral **not bold**\n```\n\n---\n\ndone\n");
    // Mixed list families stay tight
    verifyByteIdentical("- bullet\n1. number\n- [ ] todo\n- bullet again\n");
    // Empty list items and an empty code block
    verifyByteIdentical("- \n- [ ] \n");
    verifyByteIdentical("```\n```\n");
}

void TestDocumentSerializer::testRoundTripCodeEdgeCases()
{
    // Content containing a three-backtick line forces a longer fence
    m_model->insertBlockInternal(0, Block::CodeBlock, "text\n```\nmore");
    QString markdown = m_serializer->serialize(m_model);
    QCOMPARE(markdown, QString("````\ntext\n```\nmore\n````\n"));
    auto blocks = m_serializer->parse(markdown);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].content, QString("text\n```\nmore"));

    // And such a document round-trips byte-identically
    verifyByteIdentical("````\ntext\n```\nmore\n````\n");

    // Code block content keeps leading and trailing blank lines
    verifyByteIdentical("```\n\nspaced\n\n```\n");
}

void TestDocumentSerializer::testParseIndentedFence()
{
    // A fence indented under a list item — the most common shape in LLM
    // answers — is recognized; the opener's leading whitespace is
    // stripped from the content. The CodeBlock is top-level: the block
    // model is flat, so the code appears after the list item rather than
    // visually nested inside it.
    const QString md = QStringLiteral(
        "1. Do X:\n"
        "    ```python\n"
        "    print(42)\n"
        "    ```\n");
    auto blocks = m_serializer->parse(md);
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks[0].type, Block::NumberedList);
    QCOMPARE(blocks[0].content, QString("Do X:"));
    QCOMPARE(blocks[1].type, Block::CodeBlock);
    QCOMPARE(blocks[1].language, QString("python"));
    QCOMPARE(blocks[1].content, QString("print(42)"));

    // A shorter content indent strips only the characters present
    // (CommonMark-style); deeper indentation keeps its extra columns.
    blocks = m_serializer->parse(QStringLiteral(
        "    ```\n  two\n      six\n    ```\n"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].content, QString("two\n  six"));

    // Serialize emits the fence at column 0 (canonical), and the canonical
    // form is byte-stable.
    BlockModel model;
    m_serializer->loadIntoModel(&model, md);
    QCOMPARE(m_serializer->serialize(&model),
             QString("1. Do X:\n\n```python\nprint(42)\n```\n"));
    verifyByteIdentical(QStringLiteral(
        "1. Do X:\n\n```python\nprint(42)\n```\n"));
}

void TestDocumentSerializer::testParseTildeFence()
{
    // ~~~ fences are valid CommonMark, used notably for
    // markdown-inside-markdown where the inner block uses backticks. The
    // closing fence must use the same character.
    const QString md = QStringLiteral(
        "~~~\n"
        "```\n"
        "inner\n"
        "```\n"
        "~~~\n");
    auto blocks = m_serializer->parse(md);
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::CodeBlock);
    QCOMPARE(blocks[0].content, QString("```\ninner\n```"));

    // A tilde info string may contain backticks (CommonMark); they are
    // dropped from the stored language, since the canonical backtick
    // serialization could not re-parse them.
    blocks = m_serializer->parse(QStringLiteral("~~~md `x`\ncode\n~~~\n"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].type, Block::CodeBlock);
    QCOMPARE(blocks[0].language, QString("md x"));

    // Serialize canonicalizes to backticks, picking a fence longer than
    // any backtick run in the content; that form round-trips byte-stable.
    BlockModel model;
    m_serializer->loadIntoModel(&model, md);
    QCOMPARE(m_serializer->serialize(&model),
             QString("````\n```\ninner\n```\n````\n"));
    verifyByteIdentical(QStringLiteral("````\n```\ninner\n```\n````\n"));

    // A backtick closer does not close a tilde fence.
    blocks = m_serializer->parse(QStringLiteral("~~~\ncode\n```"));
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks[0].content, QString("code\n```"));
}

void TestDocumentSerializer::testEmojiRoundTripsInAllBlockTypes()
{
    // Emoji flow through parse/serialize as opaque text: a UTF-16
    // surrogate half can never equal an ASCII delimiter, so byte-identical
    // round-trips hold in every block type.
    const QString md = QStringLiteral(
        "# Title 🎉\n"
        "\n"
        "Prose with 🙂 and a family 👨‍👩‍👧 inline.\n"
        "\n"
        "- [ ] task 🚀 with rocket\n"
        "\n"
        "| plan 📋 | done ✅ |\n"
        "| --- | --- |\n"
        "| a 🙂 | b |\n");
    verifyByteIdentical(md);

    const auto blocks = m_serializer->parse(md);
    QCOMPARE(blocks.size(), 4);
    QCOMPARE(blocks[0].content, QStringLiteral("Title 🎉"));
    QCOMPARE(blocks[2].type, Block::Todo);
    QCOMPARE(blocks[2].content, QStringLiteral("task 🚀 with rocket"));
    QCOMPARE(blocks[3].type, Block::Table);
}

void TestDocumentSerializer::testNormalizations()
{
    // The documented set:
    // hand-authored variants normalize to canonical form on save.

    // Out-of-sequence ordinals renumber
    BlockModel model;
    m_serializer->loadIntoModel(&model, "7. first\n9. second\n");
    QCOMPARE(m_serializer->serialize(&model), QString("1. first\n2. second\n"));

    // Loose lists tighten
    m_serializer->loadIntoModel(&model, "- a\n\n- b\n");
    QCOMPARE(m_serializer->serialize(&model), QString("- a\n- b\n"));

    // "* " bullets, "[X]" checks, and "***" dividers canonicalize
    m_serializer->loadIntoModel(&model, "* a\n* [X] b\n\n***\n");
    QCOMPARE(m_serializer->serialize(&model), QString("- a\n- [x] b\n\n---\n"));

    // An unclosed fence gains its closing fence
    m_serializer->loadIntoModel(&model, "```\ncode");
    QCOMPARE(m_serializer->serialize(&model), QString("```\ncode\n```\n"));

    // Tab indentation becomes two-space indentation
    m_serializer->loadIntoModel(&model, "- a\n\t- b\n");
    QCOMPARE(m_serializer->serialize(&model), QString("- a\n  - b\n"));

    // H5/H6 demote to H4
    m_serializer->loadIntoModel(&model, "##### five\n\n###### six\n");
    QCOMPARE(m_serializer->serialize(&model),
             QString("#### five\n\n#### six\n"));

    // A tilde fence canonicalizes to backticks
    m_serializer->loadIntoModel(&model, "~~~js\ncode\n~~~\n");
    QCOMPARE(m_serializer->serialize(&model), QString("```js\ncode\n```\n"));
}

void TestDocumentSerializer::testEdgeSyntaxStaysLiteral()
{
    // Near-miss prefixes never convert and round-trip verbatim
    const QStringList literals = {
        "-no space",
        "1.no space",
        "----",
        "**not a divider**",
        ">no space quote",
        "####### too deep",  // H5/H6 demote to H4; seven+ stay literal
        "####no space",
        "use ``` mid-line",
        "-",
        "*",
        "1.",
    };
    for (const QString &text : literals) {
        auto blocks = m_serializer->parse(text);
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0].type, Block::Paragraph);
        QCOMPARE(blocks[0].content, text);
        verifyByteIdentical(text + "\n");
    }
}

void TestDocumentSerializer::testLoadIntoModelBlockTypes()
{
    m_serializer->loadIntoModel(m_model,
        "- [x] done\n  - child\n\n```js\ncode\n```\n\n---\n");

    QCOMPARE(m_model->count(), 4);
    QCOMPARE(m_model->blockAt(0)->blockType(), Block::Todo);
    QCOMPARE(m_model->blockAt(0)->checked(), true);
    QCOMPARE(m_model->blockAt(1)->blockType(), Block::BulletList);
    QCOMPARE(m_model->blockAt(1)->indentLevel(), 1);
    QCOMPARE(m_model->blockAt(2)->blockType(), Block::CodeBlock);
    QCOMPARE(m_model->blockAt(2)->language(), QString("js"));
    QCOMPARE(m_model->blockAt(2)->content(), QString("code"));
    QCOMPARE(m_model->blockAt(3)->blockType(), Block::Divider);
}

// ---- Block-selection clipboard ----

void TestDocumentSerializer::testSerializeBlocksSubset()
{
    m_model->insertBlock(0, Block::Heading1, "Title");
    m_model->insertBlock(1, Block::Paragraph, "prose");
    m_model->insertBlock(2, Block::CodeBlock, "x = 1");
    m_model->blockAt(2)->setLanguage("python");
    m_model->insertBlock(3, Block::Divider, "");
    m_model->insertBlock(4, Block::Todo, "task");
    m_model->blockAt(4)->setChecked(true);

    // Structure is preserved (§5.1); no trailing newline on clipboard text
    QCOMPARE(m_serializer->serializeBlocks(m_model, {0, 2, 4}),
             QString("# Title\n\n```python\nx = 1\n```\n\n- [x] task"));
    QCOMPARE(m_serializer->serializeBlocks(m_model, {3}), QString("---"));
    QCOMPARE(m_serializer->serializeBlocks(m_model, QVariantList()), QString());
}

void TestDocumentSerializer::testSerializeBlocksTightness()
{
    m_model->insertBlock(0, Block::BulletList, "one");
    m_model->insertBlock(1, Block::Paragraph, "gap");
    m_model->insertBlock(2, Block::BulletList, "two");

    // Tightness follows adjacency in the OUTPUT: the two bullets join
    // tightly even though a paragraph separates them in the document
    QCOMPARE(m_serializer->serializeBlocks(m_model, {0, 2}),
             QString("- one\n- two"));
    QCOMPARE(m_serializer->serializeBlocks(m_model, {0, 1, 2}),
             QString("- one\n\ngap\n\n- two"));
}

void TestDocumentSerializer::testSerializeBlocksOrdinalsFromDocument()
{
    m_model->insertBlock(0, Block::NumberedList, "first");
    m_model->insertBlock(1, Block::NumberedList, "second");
    m_model->insertBlock(2, Block::NumberedList, "third");

    // Copying a mid-list subset keeps the document's numbering; a paste
    // normalizes it like any hand-authored markdown
    QCOMPARE(m_serializer->serializeBlocks(m_model, {1, 2}),
             QString("2. second\n3. third"));
}

void TestDocumentSerializer::testInsertMarkdownAt()
{
    m_model->insertBlock(0, Block::Paragraph, "existing");

    const int count = m_serializer->insertMarkdownAt(m_model, 1,
        "# Head\n\n- [x] done\n- [ ] open\n\n```js\ncode\n```");
    QCOMPARE(count, 4);
    QCOMPARE(m_model->count(), 5);
    QCOMPARE(m_model->blockAt(1)->blockType(), Block::Heading1);
    QCOMPARE(m_model->blockAt(2)->blockType(), Block::Todo);
    QCOMPARE(m_model->blockAt(2)->checked(), true);
    QCOMPARE(m_model->blockAt(3)->checked(), false);
    QCOMPARE(m_model->blockAt(4)->blockType(), Block::CodeBlock);
    QCOMPARE(m_model->blockAt(4)->language(), QString("js"));

    QCOMPARE(m_serializer->insertMarkdownAt(m_model, 0, QString()), 0);
}

void TestDocumentSerializer::testInsertMarkdownAtSingleUndoStep()
{
    UndoStack stack;
    m_model->setUndoStack(&stack);
    m_model->insertBlock(0, Block::Paragraph, "existing");
    stack.clear();

    QCOMPARE(m_serializer->insertMarkdownAt(m_model, 1, "a\n\nb\n\nc"), 3);
    QCOMPARE(m_model->count(), 4);
    QCOMPARE(stack.count(), 1);
    stack.undo();
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("existing"));
    m_model->setUndoStack(nullptr);
}

void TestDocumentSerializer::testInsertPlainTextAt()
{
    m_model->insertBlock(0, Block::Paragraph, "existing");

    // Paste-as-plain-text (§5.3): structure is NOT interpreted, so markdown
    // syntax in the payload stays literal paragraph text.
    const int count = m_serializer->insertPlainTextAt(
        m_model, 1, "# Not a heading\n- not a list\n> not a quote");
    QCOMPARE(count, 3);
    QCOMPARE(m_model->count(), 4);
    for (int i = 1; i <= 3; ++i)
        QCOMPARE(m_model->blockAt(i)->blockType(), Block::Paragraph);
    QCOMPARE(m_model->blockAt(1)->content(), QString("# Not a heading"));
    QCOMPARE(m_model->blockAt(2)->content(), QString("- not a list"));
    QCOMPARE(m_model->blockAt(3)->content(), QString("> not a quote"));

    QCOMPARE(m_serializer->insertPlainTextAt(m_model, 0, QString()), 0);
}

void TestDocumentSerializer::testInsertPlainTextAtSingleUndoStep()
{
    UndoStack stack;
    m_model->setUndoStack(&stack);
    m_model->insertBlock(0, Block::Paragraph, "existing");
    stack.clear();

    // CRLF normalizes, and the whole paste undoes in one step.
    QCOMPARE(m_serializer->insertPlainTextAt(m_model, 1, "a\r\nb\r\nc"), 3);
    QCOMPARE(m_model->count(), 4);
    QCOMPARE(m_model->blockAt(1)->content(), QString("a"));
    QCOMPARE(stack.count(), 1);
    stack.undo();
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(m_model->blockAt(0)->content(), QString("existing"));
    m_model->setUndoStack(nullptr);
}

void TestDocumentSerializer::testTocFenceRoundTripAndDerivedKind()
{
    // A `toc`-tagged code fence parses as a CodeBlock
    // whose language is "toc" — no new stored type — and its derived delegate
    // kind is TocKind, so the chooser renders the linked TOC.
    const QString md = "# Intro\n\n```toc\n- [Intro](#intro)\n```\n\n## Next";
    m_serializer->loadIntoModel(m_model, md);
    QCOMPARE(m_model->count(), 3);
    QCOMPARE(m_model->blockAt(1)->blockType(), Block::CodeBlock);
    QCOMPARE(m_model->blockAt(1)->language(), QString("toc"));
    QCOMPARE(m_model->delegateKindForBlock(Block::CodeBlock,
                                              QStringLiteral("toc")),
             BlockModel::TocKind);
    // Round-trips byte-identically as a code fence.
    QCOMPARE(m_serializer->serialize(m_model), md + "\n");
}

void TestDocumentSerializer::testWikiLinksRoundTripByteIdentical()
{
    // Wiki-links are plain text to the serializer: nothing normalizes
    // them, in any block type, in any of the four grammar forms —
    // including the literal-! embed form.
    verifyByteIdentical("See [[My Note]] for details.\n");
    verifyByteIdentical("- [[a/b]] and [[Note#Heading]]\n");
    verifyByteIdentical("> quote [[Target|alias]] text\n");
    verifyByteIdentical("## Head with [[T#H|a]]\n");
    verifyByteIdentical("![[embed-stays-literal]]\n");
    verifyByteIdentical("| a | b |\n| --- | --- |\n| [[x]] | y |\n");
    verifyByteIdentical("```\n[[inside a fence stays code]]\n```\n");
}

void TestDocumentSerializer::testQueryFenceRoundTripAndDerivedKind()
{
    // A `query`-tagged code fence parses as a CodeBlock whose language is
    // "query" — no new stored type — and its derived delegate kind is
    // QueryKind. Only the spec is serialized; results are never written,
    // so the fence round-trips byte-for-byte.
    const QString md = "# Tasks\n\n```query\nfrom: projects/\n"
                       "where: status = active\nview: table\n"
                       "columns: title, due\nsort: due asc\n```\n\nAfter";
    m_serializer->loadIntoModel(m_model, md);
    QCOMPARE(m_model->count(), 3);
    QCOMPARE(m_model->blockAt(1)->blockType(), Block::CodeBlock);
    QCOMPARE(m_model->blockAt(1)->language(), QString("query"));
    QCOMPARE(m_model->delegateKindForBlock(Block::CodeBlock,
                                              QStringLiteral("query")),
             BlockModel::QueryKind);
    QCOMPARE(m_serializer->serialize(m_model), md + "\n");
}

QTEST_MAIN(TestDocumentSerializer)
#include "test_documentserializer.moc"
