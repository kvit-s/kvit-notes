// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>
#include "documentstats.h"
#include "blockmodel.h"
#include "block.h"

// Document statistics (phase11 decision 7): the six counts and reading time
// over display text, for the whole document and for a selection string,
// agreeing with the status bar's counting rules.
class TestDocumentStats : public QObject
{
    Q_OBJECT

private slots:
    void testWordCount_data();
    void testWordCount();
    void testCharCountWithAndWithoutSpaces();
    void testReadingMinutes_data();
    void testReadingMinutes();

    void testDocumentStatsOverModel();
    void testMarkersStrippedFromCounts();
    void testCodeBlockCountsVerbatim();
    void testParagraphAndBlockCounts();
    void testStatsForSelectionText();
    void testEmptyDocument();
};

void TestDocumentStats::testWordCount_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("words");
    QTest::newRow("empty") << "" << 0;
    QTest::newRow("one") << "hello" << 1;
    QTest::newRow("three") << "one two three" << 3;
    QTest::newRow("leading-trailing") << "  padded  words  " << 2;
    QTest::newRow("newlines") << "a\nb\nc" << 3;
    QTest::newRow("tabs") << "a\tb" << 2;
}

void TestDocumentStats::testWordCount()
{
    QFETCH(QString, text);
    QFETCH(int, words);
    QCOMPARE(DocumentStats::wordCount(text), words);
}

void TestDocumentStats::testCharCountWithAndWithoutSpaces()
{
    QCOMPARE(DocumentStats::charCount("ab cd", true), 5);
    QCOMPARE(DocumentStats::charCount("ab cd", false), 4);
    QCOMPARE(DocumentStats::charCount("  a b  ", true), 7);
    QCOMPARE(DocumentStats::charCount("  a b  ", false), 2);

    // Code points, not UTF-16 code units:
    // 🙂 is one character even though it is two code units.
    QCOMPARE(DocumentStats::charCount(QStringLiteral("🙂"), true), 1);
    QCOMPARE(DocumentStats::charCount(QStringLiteral("a🙂b"), true), 3);
    QCOMPARE(DocumentStats::charCount(QStringLiteral("a 🙂 b"), false), 3);
    // A ZWJ family counts its visible people — 3, not 8 code units. Full
    // grapheme accuracy (counting it as 1) is deliberately not attempted;
    // a stats readout does not justify grapheme iteration on every recount.
    QCOMPARE(DocumentStats::charCount(QStringLiteral("👨‍👩‍👧"), true), 3);
    // Word count is unaffected (splits on whitespace).
    QCOMPARE(DocumentStats::wordCount(QStringLiteral("hi 🙂 there")), 3);
}

void TestDocumentStats::testReadingMinutes_data()
{
    QTest::addColumn<int>("words");
    QTest::addColumn<int>("minutes");
    QTest::newRow("zero") << 0 << 0;
    QTest::newRow("few") << 10 << 1;      // never rounds a real read to 0
    QTest::newRow("half") << 100 << 1;    // round(0.5) up
    QTest::newRow("exact") << 200 << 1;
    QTest::newRow("two-and-a-bit") << 450 << 2;  // round(2.25)
    QTest::newRow("three") << 500 << 3;   // round(2.5) up
}

void TestDocumentStats::testReadingMinutes()
{
    QFETCH(int, words);
    QFETCH(int, minutes);
    QCOMPARE(DocumentStats::readingMinutes(words), minutes);
}

void TestDocumentStats::testDocumentStatsOverModel()
{
    BlockModel model;
    model.insertBlock(0, Block::Heading1, "Title Here");        // 2 words
    model.insertBlock(1, Block::Paragraph, "one two three four"); // 4 words
    DocumentStats stats;
    stats.setModel(&model);
    const QVariantMap s = stats.documentStats();
    QCOMPARE(s.value("words").toInt(), 6);
    QCOMPARE(s.value("blocks").toInt(), 2);
    QCOMPARE(s.value("paragraphs").toInt(), 2);
    QCOMPARE(s.value("readingMinutes").toInt(), 1);
}

void TestDocumentStats::testMarkersStrippedFromCounts()
{
    BlockModel model;
    // Display text is "bold and italic" — 3 words, 15 chars-with-spaces.
    model.insertBlock(0, Block::Paragraph, "**bold** and *italic*");
    DocumentStats stats;
    stats.setModel(&model);
    const QVariantMap s = stats.documentStats();
    QCOMPARE(s.value("words").toInt(), 3);
    QCOMPARE(s.value("charsWithSpaces").toInt(),
             int(QString("bold and italic").length()));
}

void TestDocumentStats::testCodeBlockCountsVerbatim()
{
    BlockModel model;
    // A code block counts its literal content (markers are not stripped).
    model.insertBlock(0, Block::CodeBlock, "int x = **1**;");
    DocumentStats stats;
    stats.setModel(&model);
    const QVariantMap s = stats.documentStats();
    QCOMPARE(s.value("charsWithSpaces").toInt(),
             int(QString("int x = **1**;").length()));
}

void TestDocumentStats::testParagraphAndBlockCounts()
{
    BlockModel model;
    model.insertBlock(0, Block::Paragraph, "prose one");
    model.insertBlock(1, Block::Divider, "");          // not prose
    model.insertBlock(2, Block::Paragraph, "");        // empty, not prose
    model.insertBlock(3, Block::Paragraph, "prose two");
    DocumentStats stats;
    stats.setModel(&model);
    const QVariantMap s = stats.documentStats();
    QCOMPARE(s.value("blocks").toInt(), 4);
    QCOMPARE(s.value("paragraphs").toInt(), 2); // only the two prose blocks
}

void TestDocumentStats::testStatsForSelectionText()
{
    DocumentStats stats;
    const QVariantMap s = stats.statsForText("one two\n\nthree");
    QCOMPARE(s.value("words").toInt(), 3);
    QCOMPARE(s.value("paragraphs").toInt(), 2); // two non-empty lines
    QCOMPARE(s.value("charsNoSpaces").toInt(),
             int(QString("onetwothree").length()));
}

void TestDocumentStats::testEmptyDocument()
{
    BlockModel model;
    DocumentStats stats;
    stats.setModel(&model);
    const QVariantMap s = stats.documentStats();
    QCOMPARE(s.value("words").toInt(), 0);
    QCOMPARE(s.value("blocks").toInt(), 0);
    QCOMPARE(s.value("readingMinutes").toInt(), 0);
}

QTEST_MAIN(TestDocumentStats)
#include "test_documentstats.moc"
