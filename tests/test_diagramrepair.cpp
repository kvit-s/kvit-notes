// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QFile>

#include "diagrams/diagramrepair.h"
#include "diagrams/diagramclassifier.h"

using DiagramRepair::repair;

// Ingest straightening for character diagrams (diagrams-prd.md §7.5): LLM
// alignment flaws — ragged box edges, jogged connectors — are repaired in
// the stored text with zero-shift edits; anything ambiguous is preserved
// byte-exact. The checked-in llm-diagram.md is the canonical flawed corpus.
class TestDiagramRepair : public QObject
{
    Q_OBJECT

    static QString fixtureBody()
    {
        QFile f(QStringLiteral(KVIT_SOURCE_ROOT "/tests/fixtures/llm-diagram.md"));
        if (!f.open(QIODevice::ReadOnly))
            return QString();
        QString text = QString::fromUtf8(f.readAll());
        // Strip the surrounding markdown fence: the body is what the
        // serializer hands to repair().
        QStringList lines = text.split(u'\n');
        while (!lines.isEmpty()
               && (lines.first().startsWith(QLatin1String("```"))
                   || lines.first().trimmed().isEmpty()))
            lines.removeFirst();
        while (!lines.isEmpty()
               && (lines.last().startsWith(QLatin1String("```"))
                   || lines.last().trimmed().isEmpty()))
            lines.removeLast();
        return lines.join(u'\n');
    }

    // Column of the first occurrence of `ch` on line `row` of `text`.
    static int colOf(const QString &text, int row, QChar ch)
    {
        return text.split(u'\n').value(row).indexOf(ch);
    }

private slots:
    void shortTopEdgeExtendsToWallColumn()
    {
        // The top-right corner sits two columns short of the side bars —
        // the OPERATOR-box flaw from the corpus, minimized.
        const QString in = QStringLiteral("┌─────┐\n"
                                          "│ ab    │\n"
                                          "│ cd    │\n"
                                          "└───────┘");
        const QString out = repair(in);
        QCOMPARE(colOf(out, 0, u'┐'), 8);
        // Interior text untouched.
        QVERIFY(out.contains(QStringLiteral("│ ab    │")));
        QVERIFY(out.contains(QStringLiteral("│ cd    │")));
    }

    void raggedWallBarsAlignWithoutShifting()
    {
        // One wall bar juts two columns past the frame; text right of the
        // bar must keep its column when the bar moves.
        const QString in = QStringLiteral("┌─────┐\n"
                                          "│ ab  │   note\n"
                                          "│ cd    │ note\n"
                                          "└─────┘");
        const QString out = repair(in);
        const QStringList lines = out.split(u'\n');
        QCOMPARE(lines.at(2).indexOf(u'│', 1), 6);      // bar pulled in
        QCOMPARE(lines.at(2).indexOf(QLatin1String("note")), 10); // text fixed
    }

    void bottomEdgeTrimsToMatch()
    {
        const QString in = QStringLiteral("┌─────┐\n"
                                          "│ ab  │\n"
                                          "└───────┘");
        const QString out = repair(in);
        QCOMPARE(colOf(out, 2, u'┘'), 6);
    }

    void joggedConnectorStraightens()
    {
        // The tee sits two columns left of the bar and arrowhead below it —
        // the TRUSTED-CORE-to-MEMORY flaw from the corpus, minimized.
        const QString in = QStringLiteral("┌────────┐\n"
                                          "│ top    │\n"
                                          "└──┬─────┘\n"
                                          "    │\n"
                                          "┌───▼────┐\n"
                                          "│ bottom │\n"
                                          "└────────┘");
        const QString out = repair(in);
        QCOMPARE(colOf(out, 2, u'┬'), 4);
        QCOMPARE(colOf(out, 3, u'│'), 4);
        QCOMPARE(colOf(out, 4, u'▼'), 4);
    }

    void labelBlocksBarMoveBoxStaysUntouched()
    {
        // The ragged bar cannot move left through label text: the whole
        // side is left alone rather than half-repaired.
        const QString in = QStringLiteral("┌─────┐\n"
                                          "│ abcdef│\n"
                                          "│ cd  │\n"
                                          "└─────┘");
        QCOMPARE(repair(in), in);
    }

    void asciiBoxStraightens()
    {
        const QString in = QStringLiteral("+-----+\n"
                                          "| ab    |\n"
                                          "| cd    |\n"
                                          "+-------+");
        const QString out = repair(in);
        QCOMPARE(out.split(u'\n').at(0).lastIndexOf(u'+'), 8);
    }

    void straightDiagramUntouched()
    {
        const QString in = QStringLiteral("┌─────┐   ┌─────┐\n"
                                          "│ ab  │ ─►│ cd  │\n"
                                          "└──┬──┘   └─────┘\n"
                                          "   │\n"
                                          "   ▼");
        QCOMPARE(repair(in), in);
    }

    void tabsDisableRepair()
    {
        const QString in = QStringLiteral("┌─────┐\n"
                                          "│\tab    │\n"
                                          "└───────┘");
        QCOMPARE(repair(in), in);
    }

    void idempotentOnCorpus()
    {
        const QString body = fixtureBody();
        QVERIFY(!body.isEmpty());
        const QString once = repair(body);
        QVERIFY(once != body);            // the corpus is known-flawed
        QCOMPARE(repair(once), once);     // and the repair converges
    }

    void corpusEdgesAlignAfterRepair()
    {
        const QString out = repair(fixtureBody());
        const QStringList lines = out.split(u'\n');
        // OPERATOR box (rows 0-4): the top-right corner reaches the wall
        // column 67, and the connector tee/bar column agrees (38).
        QCOMPARE(lines.at(0).indexOf(u'┐'), 67);
        QCOMPARE(lines.at(4).indexOf(u'┘'), 67);
        // TRUSTED CORE box (rows 6-10): right edge unified at 72.
        QCOMPARE(lines.at(6).indexOf(u'┐'), 72);
        QCOMPARE(lines.at(10).indexOf(u'┘'), 72);
        // The jogged connector into MEMORY: tee, bar, and arrowhead in one
        // column.
        const int tee = lines.at(10).lastIndexOf(u'┬');
        QCOMPARE(lines.at(11).lastIndexOf(u'│'), tee);
        QCOMPARE(lines.at(12).lastIndexOf(u'▼'), tee);
    }

    void repairedCorpusStillClassifiesAsDiagram()
    {
        QVERIFY(DiagramClassifier::looksLikeDiagram(repair(fixtureBody())));
    }

    void oversizedBodyUntouched()
    {
        QString big = QStringLiteral("┌─────┐\n│ ab    │\n└───────┘\n");
        big += QString(DiagramRepair::kRepairCapChars, u'x');
        QCOMPARE(repair(big), big);
    }

    // Not an assertion — writes the repaired corpus for eyeball checks when
    // KVIT_REPAIR_DUMP is set to a writable path.
    void corpusDump()
    {
        const QByteArray path = qgetenv("KVIT_REPAIR_DUMP");
        if (path.isEmpty())
            QSKIP("set KVIT_REPAIR_DUMP=<path.txt> to write the repaired corpus");
        QFile f(QString::fromLocal8Bit(path));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(repair(fixtureBody()).toUtf8());
    }
};

QTEST_MAIN(TestDiagramRepair)
#include "test_diagramrepair.moc"
