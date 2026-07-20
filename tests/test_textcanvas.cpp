// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "diagrams/textcanvas.h"

// Unit suite for the character-grid draw target: junction resolution for
// every crossing pair, box overlap, text clipping behavior, dynamic growth,
// and the trimmed string form.
class TestTextCanvas : public QObject
{
    Q_OBJECT

private slots:
    void testGrowthAndPut();
    void testHLineAndVLineEndpoints();
    void testJunctionTable_data();
    void testJunctionTable();
    void testBoxCornersAndWalls();
    void testTouchingBoxesShareJunctions();
    void testDoubleWalls();
    void testLinesNeverEatText();
    void testArrowheads();
    void testToStringTrims();
};

void TestTextCanvas::testGrowthAndPut()
{
    TextCanvas canvas;
    QCOMPARE(canvas.rows(), 0);
    canvas.put(2, 5, QChar(u'x'));
    QCOMPARE(canvas.rows(), 3);
    QCOMPARE(canvas.at(2, 5), QChar(u'x'));
    QCOMPARE(canvas.at(2, 4), QChar(u' '));
    QCOMPARE(canvas.at(9, 9), QChar()); // outside stays null
}

void TestTextCanvas::testHLineAndVLineEndpoints()
{
    TextCanvas canvas;
    canvas.drawHLine(0, 2, 6);
    for (int col = 2; col <= 6; ++col)
        QCOMPARE(canvas.at(0, col), QChar(u'─'));
    canvas.drawVLine(0, 2, 5);
    for (int row = 2; row <= 5; ++row)
        QCOMPARE(canvas.at(row, 0), QChar(u'│'));
    // Reversed coordinates draw the same line.
    TextCanvas reversed;
    reversed.drawHLine(0, 6, 2);
    QCOMPARE(reversed.at(0, 4), QChar(u'─'));
}

void TestTextCanvas::testJunctionTable_data()
{
    QTest::addColumn<int>("hCol1");
    QTest::addColumn<int>("hCol2");
    QTest::addColumn<int>("vRow1");
    QTest::addColumn<int>("vRow2");
    QTest::addColumn<QChar>("expected");

    // All meetings of an H line (row 2, varying span) and a V line
    // (col 2, varying span) at cell (2,2).
    QTest::newRow("cross")    << 0 << 4 << 0 << 4 << QChar(u'┼');
    QTest::newRow("tee-down") << 0 << 4 << 2 << 4 << QChar(u'┬');
    QTest::newRow("tee-up")   << 0 << 4 << 0 << 2 << QChar(u'┴');
    QTest::newRow("tee-right")<< 2 << 4 << 0 << 4 << QChar(u'├');
    QTest::newRow("tee-left") << 0 << 2 << 0 << 4 << QChar(u'┤');
    QTest::newRow("corner-tl")<< 2 << 4 << 2 << 4 << QChar(u'┌');
    QTest::newRow("corner-tr")<< 0 << 2 << 2 << 4 << QChar(u'┐');
    QTest::newRow("corner-bl")<< 2 << 4 << 0 << 2 << QChar(u'└');
    QTest::newRow("corner-br")<< 0 << 2 << 0 << 2 << QChar(u'┘');
}

void TestTextCanvas::testJunctionTable()
{
    QFETCH(int, hCol1);
    QFETCH(int, hCol2);
    QFETCH(int, vRow1);
    QFETCH(int, vRow2);
    QFETCH(QChar, expected);

    // Either drawing order produces the same junction.
    TextCanvas hFirst;
    hFirst.drawHLine(2, hCol1, hCol2);
    hFirst.drawVLine(2, vRow1, vRow2);
    QCOMPARE(hFirst.at(2, 2), expected);

    TextCanvas vFirst;
    vFirst.drawVLine(2, vRow1, vRow2);
    vFirst.drawHLine(2, hCol1, hCol2);
    QCOMPARE(vFirst.at(2, 2), expected);
}

void TestTextCanvas::testBoxCornersAndWalls()
{
    TextCanvas canvas;
    canvas.drawBox(1, 2, 4, 8);
    QCOMPARE(canvas.at(1, 2), QChar(u'┌'));
    QCOMPARE(canvas.at(1, 8), QChar(u'┐'));
    QCOMPARE(canvas.at(4, 2), QChar(u'└'));
    QCOMPARE(canvas.at(4, 8), QChar(u'┘'));
    QCOMPARE(canvas.at(1, 5), QChar(u'─'));
    QCOMPARE(canvas.at(2, 2), QChar(u'│'));
    QCOMPARE(canvas.at(3, 8), QChar(u'│'));
    QCOMPARE(canvas.at(2, 5), QChar(u' ')); // interior untouched
}

void TestTextCanvas::testTouchingBoxesShareJunctions()
{
    // Two boxes sharing an edge column produce ┬/┴ junctions, not
    // clobbered corners.
    TextCanvas canvas;
    canvas.drawBox(0, 0, 2, 4);
    canvas.drawBox(0, 4, 2, 8);
    QCOMPARE(canvas.at(0, 4), QChar(u'┬'));
    QCOMPARE(canvas.at(2, 4), QChar(u'┴'));

    // A line ending on a box wall becomes a junction into it.
    TextCanvas withLine;
    withLine.drawBox(0, 4, 2, 8);
    withLine.drawHLine(1, 0, 4);
    QCOMPARE(withLine.at(1, 4), QChar(u'┤'));
}

void TestTextCanvas::testDoubleWalls()
{
    TextCanvas canvas;
    canvas.drawBox(0, 0, 3, 6, true);
    QCOMPARE(canvas.at(1, 0), QChar(u'║'));
    QCOMPARE(canvas.at(2, 6), QChar(u'║'));
    QCOMPARE(canvas.at(0, 0), QChar(u'┌')); // corners stay light
}

void TestTextCanvas::testLinesNeverEatText()
{
    TextCanvas canvas;
    canvas.drawText(1, 3, QStringLiteral("hi"));
    canvas.drawHLine(1, 0, 8);
    QCOMPARE(canvas.at(1, 3), QChar(u'h'));
    QCOMPARE(canvas.at(1, 4), QChar(u'i'));
    QCOMPARE(canvas.at(1, 2), QChar(u'─'));
    QCOMPARE(canvas.at(1, 5), QChar(u'─'));

    // Text drawn later DOES displace line cells (labels sit on edges).
    canvas.drawText(1, 6, QStringLiteral("yo"));
    QCOMPARE(canvas.at(1, 6), QChar(u'y'));
}

void TestTextCanvas::testArrowheads()
{
    TextCanvas canvas;
    canvas.drawArrowhead(0, 0, TextCanvas::Up);
    canvas.drawArrowhead(0, 1, TextCanvas::Down);
    canvas.drawArrowhead(0, 2, TextCanvas::Left);
    canvas.drawArrowhead(0, 3, TextCanvas::Right);
    QCOMPARE(canvas.at(0, 0), QChar(u'▲'));
    QCOMPARE(canvas.at(0, 1), QChar(u'▼'));
    QCOMPARE(canvas.at(0, 2), QChar(u'◄'));
    QCOMPARE(canvas.at(0, 3), QChar(u'►'));
}

void TestTextCanvas::testToStringTrims()
{
    TextCanvas canvas;
    canvas.put(0, 0, QChar(u'a'));
    canvas.put(0, 4, QChar(u'b'));
    canvas.put(1, 0, QChar(u'c'));
    canvas.put(3, 8, QChar(u' ')); // grows rows/cols with only spaces
    QCOMPARE(canvas.toString(), QString("a   b\nc"));
}

QTEST_MAIN(TestTextCanvas)
#include "test_textcanvas.moc"
