// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "tabledata.h"

using TableData::Align;

// Corpus for the pipe-table parse/serialize/mutate core: escaped pipes,
// alignment colons, ragged rows, the mutations, and sort — pinned before
// the delegate paints any of it.
class TestTableData : public QObject
{
    Q_OBJECT

private slots:
    void parseBasic()
    {
        const auto t = TableData::parse(
            "| A | B | C |\n| :--- | :---: | ---: |\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |");
        QVERIFY(t.valid);
        QCOMPARE(t.columnCount(), 3);
        QCOMPARE(t.rowCount(), 2);
        QCOMPARE(t.headers, QStringList({"A", "B", "C"}));
        QCOMPARE(t.alignments,
                 (QList<Align>{Align::Left, Align::Center, Align::Right}));
        QCOMPARE(t.rows[0], QStringList({"1", "2", "3"}));
        QCOMPARE(t.rows[1], QStringList({"4", "5", "6"}));
    }

    void parseNoOuterPipesAndEscapedPipe()
    {
        // No leading/trailing border pipes, and an escaped pipe inside a cell.
        const auto t = TableData::parse("a \\| b | c\n--- | ---\nx | y");
        QVERIFY(t.valid);
        QCOMPARE(t.columnCount(), 2);
        QCOMPARE(t.headers, QStringList({"a | b", "c"}));   // \| unescaped
        QCOMPARE(t.rows[0], QStringList({"x", "y"}));
    }

    void raggedRowsSquareUp()
    {
        // Short rows pad, long rows truncate to the header's column count.
        const auto t = TableData::parse("| A | B |\n| --- | --- |\n| 1 |\n| 2 | 3 | 4 |");
        QVERIFY(t.valid);
        QCOMPARE(t.rows[0], QStringList({"1", ""}));
        QCOMPARE(t.rows[1], QStringList({"2", "3"}));
    }

    void invalidTables()
    {
        QVERIFY(!TableData::parse("just text").valid);
        QVERIFY(!TableData::parse("| A | B |").valid);          // no delimiter
        QVERIFY(!TableData::parse("| A |\n| not a delim |").valid);
        // A lone divider line is not a one-column table (needs a pipe header).
        QVERIFY(!TableData::looksLikeTableStart("Heading", "---"));
    }

    void serializeIsCanonicalAndIdempotent()
    {
        const QString canonical =
            "| A | B |\n| :--- | ---: |\n| 1 | 2 |";
        const auto t = TableData::parse(canonical);
        QCOMPARE(TableData::serialize(t), canonical);
        // A padded/ragged table normalizes to the same canonical form.
        const QString padded =
            "|  A  |   B |\n|:----|----:|\n| 1   | 2   |";
        QCOMPARE(TableData::serialize(TableData::parse(padded)), canonical);
    }

    void setCellHeaderAndBody()
    {
        const QString md = "| A | B |\n| --- | --- |\n| 1 | 2 |";
        QCOMPARE(TableData::setCell(md, -1, 0, "Name"),
                 QString("| Name | B |\n| --- | --- |\n| 1 | 2 |"));
        QCOMPARE(TableData::setCell(md, 0, 1, "x"),
                 QString("| A | B |\n| --- | --- |\n| 1 | x |"));
        // A newline in a value is written as <br>, so the row stays one line.
        QCOMPARE(TableData::setCell(md, 0, 0, "a\nb"),
                 QString("| A | B |\n| --- | --- |\n| a<br>b | 2 |"));
        // A pipe in a value is escaped.
        QCOMPARE(TableData::setCell(md, 0, 0, "a|b"),
                 QString("| A | B |\n| --- | --- |\n| a\\|b | 2 |"));
    }

    // Writing a cell is idempotent against the whitespace parse throws away,
    // so the live cell editor is not handed a rewrite for a space it has not
    // finished typing (the space-swallowing round trip).
    void setCellTrimsSurroundingWhitespace()
    {
        const QString md = "| A | B |\n| --- | --- |\n| 1 | 2 |";
        QCOMPARE(TableData::setCell(md, 0, 0, "1 "), md);
        QCOMPARE(TableData::setCell(md, 0, 0, " 1"), md);
        QCOMPARE(TableData::setCell(md, -1, 0, "A  "), md);
        // The trimming is the serializer's, so every mutation shares it and
        // serialize stays the exact inverse of parse.
        QCOMPARE(TableData::setCell(md, 0, 0, "  x  "),
                 QString("| A | B |\n| --- | --- |\n| x | 2 |"));
        TableData::Table t = TableData::parse(md);
        t.rows[0][0] = QStringLiteral(" 1 ");
        QCOMPARE(TableData::serialize(t), md);
    }

    void insertAndRemoveRowsColumns()
    {
        const QString md = "| A | B |\n| --- | --- |\n| 1 | 2 |";
        QCOMPARE(TableData::insertRow(md, 0),
                 QString("| A | B |\n| --- | --- |\n| 1 | 2 |\n|  |  |"));
        QCOMPARE(TableData::insertRow(md, -1),
                 QString("| A | B |\n| --- | --- |\n|  |  |\n| 1 | 2 |"));
        QCOMPARE(TableData::insertColumn(md, 1),
                 QString("| A | B |  |\n| --- | --- | --- |\n| 1 | 2 |  |"));
        QCOMPARE(TableData::removeRow(md, 0),
                 QString("| A | B |\n| --- | --- |"));
        QCOMPARE(TableData::removeColumn(md, 0),
                 QString("| B |\n| --- |\n| 2 |"));
        // The last column cannot be removed.
        QCOMPARE(TableData::removeColumn("| A |\n| --- |\n| 1 |", 0),
                 QString("| A |\n| --- |\n| 1 |"));
    }

    void sortByColumn()
    {
        const QString md =
            "| N | V |\n| --- | --- |\n| b | 3 |\n| a | 10 |\n| c | 2 |";
        // Ascending by text on column 0.
        QCOMPARE(TableData::sortByColumn(md, 0, true),
                 QString("| N | V |\n| --- | --- |\n| a | 10 |\n| b | 3 |\n| c | 2 |"));
        // Numeric column sorts numerically (10 > 3, not lexicographically).
        QCOMPARE(TableData::sortByColumn(md, 1, true),
                 QString("| N | V |\n| --- | --- |\n| c | 2 |\n| b | 3 |\n| a | 10 |"));
        // Descending reverses.
        QCOMPARE(TableData::sortByColumn(md, 1, false),
                 QString("| N | V |\n| --- | --- |\n| a | 10 |\n| b | 3 |\n| c | 2 |"));
    }

    void setAlignmentAndEmptyTable()
    {
        const QString md = "| A |\n| --- |\n| 1 |";
        QCOMPARE(TableData::setAlignment(md, 0, Align::Center),
                 QString("| A |\n| :---: |\n| 1 |"));
        QCOMPARE(TableData::emptyTable(2, 1),
                 QString("|  |  |\n| --- | --- |\n|  |  |"));
    }

    void brTagsBecomeLineBreaks()
    {
        // A cell's line break rides as <br>, which is what keeps the row one
        // line of the file. All three spellings, case-insensitive, and the
        // padding an author writes around the tag goes with the break.
        const auto t = TableData::parse(
            "| a<br>b | c |\n| --- | --- |\n| d<br/>e | f <BR /> g |");
        QVERIFY(t.valid);
        QCOMPARE(t.headers, QStringList({"a\nb", "c"}));
        QCOMPARE(t.rows[0], QStringList({"d\ne", "f\n g"}));

        // The canonicalized table is byte-stable on a second pass, and it
        // still writes the breaks as tags rather than as real newlines.
        const QString canonical = TableData::serialize(t);
        QCOMPARE(TableData::serialize(TableData::parse(canonical)),
                 canonical);
        QCOMPARE(canonical.count(QLatin1Char('\n')), 2);   // three rows
        QVERIFY(canonical.contains("a<br>b"));
    }

    // Indentation inside a cell survives, which is what makes a code listing
    // in a cell readable. The first line is the exception: its leading
    // whitespace and the padding around the pipes are the same characters,
    // so the parser cannot keep one without keeping the other.
    void multiLineCellKeepsIndentation()
    {
        const QString md = TableData::setCell(
            "| Step | Code |\n| --- | --- |\n|  |  |",
            0, 1, "for i in rows\n    for j in cols\n        sum += a * b");
        QCOMPARE(md, QString("| Step | Code |\n| --- | --- |\n"
                             "|  | for i in rows<br>    for j in cols"
                             "<br>        sum += a * b |"));
        const auto t = TableData::parse(md);
        QCOMPARE(t.rows[0][1],
                 QString("for i in rows\n    for j in cols\n        sum += a * b"));

        // A pipe inside a multi-line cell still cannot split the row.
        const QString piped = TableData::setCell(md, 0, 1, "a | b\nc");
        QCOMPARE(TableData::parse(piped).rows[0][1], QString("a | b\nc"));
    }
};

QTEST_MAIN(TestTableData)
#include "test_tabledata.moc"
