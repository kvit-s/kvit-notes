// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QFile>
#include <QTextStream>

#include "diagrams/diagramclassifier.h"

// Character-diagram classifier corpus. The positive set is the checked-in
// llm-diagram.md fixture (read from disk so the test proves the committed
// file classifies as a diagram, misalignments included) plus a
// deliberately-worse second fixture with heavier column shifts and mixed
// ASCII/Unicode strokes. The negative set holds the lookalikes that must stay
// code: source, shell transcripts, Markdown tables, ASCII console tables,
// stack traces, `tree` listings, and prose art.
class TestDiagramClassifier : public QObject
{
    Q_OBJECT

private:
    // The body of the first fenced block in a Markdown file — what the ingest
    // pass hands the classifier.
    static QString fenceBody(const QString &path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        QTextStream in(&f);
        const QStringList lines = in.readAll().split(u'\n');
        int open = -1, close = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines.at(i).trimmed().startsWith(QLatin1String("```"))) {
                if (open < 0) open = i;
                else { close = i; break; }
            }
        }
        if (open < 0 || close < 0)
            return QString();
        return lines.mid(open + 1, close - open - 1).join(u'\n');
    }

private slots:
    // ---- positive: the canonical checked-in fixture ----
    void classifiesCanonicalFixture()
    {
        const QString body =
            fenceBody(QStringLiteral(KVIT_SOURCE_ROOT "/tests/fixtures/llm-diagram.md"));
        QVERIFY2(!body.isEmpty(), "could not read llm-diagram.md fence body");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    // ---- positive: a deliberately-worse second fixture ----
    void classifiesMisalignedFixture()
    {
        // Heavier column shifts, mixed ASCII '+' corners with Unicode strokes,
        // ragged widths. Still two framed regions joined by an arrow.
        const QString body = QStringLiteral(
            "   +-----------------+\n"
            "   |   INGEST  node  |\n"
            "   |  parse · repair |\n"
            "   +--------+--------+\n"
            "            |  hands off\n"
            "            v\n"
            "      ┌────────────────────┐\n"
            "      │   RENDER  (stage)    │\n"
            "      │ layout → paint       │\n"
            "      └──────────────────────┘\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    // ---- negatives ----
    void rejectsTreeListing()
    {
        const QString body = QStringLiteral(
            "project/\n"
            "├── src/\n"
            "│   ├── main.cpp\n"
            "│   └── util.cpp\n"
            "├── tests/\n"
            "│   └── test_main.cpp\n"
            "└── README.md\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void rejectsPsqlConsoleTable()
    {
        const QString body = QStringLiteral(
            "+----+-------+---------+\n"
            "| id | name  | balance |\n"
            "+----+-------+---------+\n"
            "|  1 | Alice |   10.00 |\n"
            "|  2 | Bob   |    5.50 |\n"
            "|  3 | Carol |    0.00 |\n"
            "+----+-------+---------+\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void rejectsMarkdownTable()
    {
        const QString body = QStringLiteral(
            "| Name  | Role      | Notes |\n"
            "|-------|-----------|-------|\n"
            "| Alice | Lead      | on    |\n"
            "| Bob   | Reviewer  | off   |\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void rejectsSourceCode()
    {
        const QString body = QStringLiteral(
            "int main(int argc, char **argv) {\n"
            "    auto x = compute(argc);\n"
            "    for (int i = 0; i < x; ++i) {\n"
            "        process(i);\n"
            "    }\n"
            "    return 0;\n"
            "}\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void rejectsShellTranscript()
    {
        const QString body = QStringLiteral(
            "$ git status\n"
            "On branch main\n"
            "nothing to commit, working tree clean\n"
            "$ ls -la\n"
            "total 24\n"
            "drwxr-xr-x  3 sk sk 4096 build\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void rejectsStackTrace()
    {
        const QString body = QStringLiteral(
            "Traceback (most recent call last):\n"
            "  File \"app.py\", line 42, in <module>\n"
            "    main()\n"
            "  File \"app.py\", line 30, in main\n"
            "    raise ValueError(\"boom\")\n"
            "ValueError: boom\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void rejectsProse()
    {
        const QString body = QStringLiteral(
            "The quick brown fox jumps over the lazy dog.\n"
            "It was the best of times, it was the worst of times.\n"
            "All that glitters is not gold.\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void rejectsSingleBoxWithArrow()
    {
        // One boxed line and a lone arrow is insufficient (§7.3).
        const QString body = QStringLiteral(
            "┌─────────────┐\n"
            "│   only box  │\n"
            "└──────┬──────┘\n"
            "       ▼\n"
            "   dangling label\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void rejectsDecorativeRule()
    {
        const QString body = QStringLiteral(
            "Section A\n"
            "──────────────────────────\n"
            "Some body text goes here.\n"
            "──────────────────────────\n"
            "Section B\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(body);
        QVERIFY2(!r.isDiagram, qPrintable(r.reasons.join(QStringLiteral("; "))));
    }

    void oversizedFenceNeverTagged()
    {
        QString big;
        big.reserve(DiagramClassifier::kInspectionCapChars + 32);
        while (big.size() <= DiagramClassifier::kInspectionCapChars)
            big += QStringLiteral("┌──┐\n│ x│\n└──┘\n");
        const DiagramClassifier::Result r = DiagramClassifier::classify(big);
        QVERIFY(!r.isDiagram);
    }

    // Two dense rows of vertical strokes whose columns never come within the
    // +/- 2 proximity window, which is the worst case for the recurring-column
    // scan: no early exit, every column on one row compared against every
    // column on the other. Both rows fit inside the inspection cap, so this is
    // reachable from an ordinary paste or note open.
    void adversarialVerticalColumnsStayCheap()
    {
        // Box-drawing strokes, so each row also counts as a base-signal line
        // and the scan is actually reached.
        const int n = 50000;
        const QChar bar(0x2502);
        QString content = QString(n, bar);
        content += u'\n';
        content += QString(n + 3, u' ');
        content += QString(n, bar);
        content += QStringLiteral("\n┌──┐\n");
        QVERIFY(content.size() <= DiagramClassifier::kInspectionCapChars);

        QElapsedTimer t;
        t.start();
        DiagramClassifier::classify(content);
        const qint64 ms = t.elapsed();
        qInfo("adversarial classify took %lld ms", ms);
        QVERIFY2(ms < 250, qPrintable(QStringLiteral("classify took %1 ms")
                                          .arg(ms)));
    }

    // A full-cap input of dense box-drawing, to catch any other path in
    // classify() that is worse than linear in the accepted input size.
    void capSizedInputStaysCheap()
    {
        QString content;
        const QString row = QStringLiteral("┌─┬─┐ ├─┼─┤ └─┴─┘ │ │ │ ──> A1\n");
        while (content.size() + row.size()
               <= DiagramClassifier::kInspectionCapChars)
            content += row;

        QElapsedTimer t;
        t.start();
        DiagramClassifier::classify(content);
        const qint64 ms = t.elapsed();
        qInfo("cap-sized classify took %lld ms", ms);
        QVERIFY2(ms < 250, qPrintable(QStringLiteral("classify took %1 ms")
                                          .arg(ms)));
    }

    void emptyContentRejected()
    {
        QVERIFY(!DiagramClassifier::looksLikeDiagram(QString()));
        QVERIFY(!DiagramClassifier::looksLikeDiagram(QStringLiteral("\n\n")));
    }
};

QTEST_APPLESS_MAIN(TestDiagramClassifier)
#include "test_diagramclassifier.moc"
