// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "diagrams/diagrambudget.h"
#include "diagrams/diagramclassifier.h"
#include "diagrams/diagramrepair.h"
#include "diagrams/mermaidrenderer.h"
#include "diagrams/textdiagram.h"

using namespace Diagram;

// Unit suite for the Scene → text serializer, in the established
// repair-test style: assert glyphs and structural facts,
// no golden images. One canonical fixture per family, determinism, and
// the two closure properties that pin exporter and repair to one
// canonical form: the classifier accepts the output as a character
// diagram, and repair leaves it byte-identical (already straight).
class TestTextDiagram : public QObject
{
    Q_OBJECT

    static QString renderSource(const QString &source)
    {
        LayoutOptions opts;
        opts.fontFamily = QStringLiteral("sans-serif");
        opts.fontPixelSize = 14;
        const RenderResult result = render(source, opts);
        if (!result.valid)
            return QString();
        return renderText(result.scene);
    }

    // Both closure properties plus non-emptiness, for every family.
    void verifyClosure(const QString &output)
    {
        QVERIFY(!output.isEmpty());
        QVERIFY2(DiagramClassifier::looksLikeDiagram(output),
                 qPrintable(QStringLiteral("classifier rejects:\n") + output));
        QCOMPARE(DiagramRepair::repair(output), output);
        verifyNoDanglingArms(output);
    }

    // The arms a box-drawing glyph reaches out with, as U|D|L|R bits.
    static int armsOf(QChar c)
    {
        switch (c.unicode()) {
        case u'─': return Left | Right;
        case u'│': case u'║': return Up | Down;
        case u'┌': return Down | Right;
        case u'┐': return Down | Left;
        case u'└': return Up | Right;
        case u'┘': return Up | Left;
        case u'├': return Up | Down | Right;
        case u'┤': return Up | Down | Left;
        case u'┬': return Down | Left | Right;
        case u'┴': return Up | Left | Right;
        case u'┼': return Up | Down | Left | Right;
        default:   return 0;
        }
    }

    // A glyph that legitimately ends a line: an arrowhead, one of the
    // deliberately degraded UML/ER markers, or a label character (a label
    // is allowed to displace the line it rides on).
    static bool terminates(QChar c)
    {
        static const QString markers = QStringLiteral("▲▼◄►△◇~");
        return !c.isNull() && c != QLatin1Char(' ')
            && (markers.contains(c) || c.isLetterOrNumber()
                || c.isPunct() || c.isSymbol());
    }

    // Structural invariant across every fixture: no line arm points at
    // nothing. Each arm must meet a neighbour that continues the line or
    // one that terminates it. A route that stops in mid-air — the stray
    // dash a collapsed Z route used to leave behind, a self-loop that
    // never comes back, a detour drawn across a box — is a dangling arm,
    // and reads as a diagram whose edges do not connect.
    void verifyNoDanglingArms(const QString &output)
    {
        const QStringList lines = output.split(QLatin1Char('\n'));
        const auto at = [&](int row, int col) -> QChar {
            if (row < 0 || row >= lines.size())
                return QChar();
            const QString &line = lines.at(row);
            if (col < 0 || col >= line.size())
                return QChar();
            return line.at(col);
        };
        struct Step { int arm; int dRow; int dCol; int back; };
        static const Step steps[] = {
            {Up, -1, 0, Down}, {Down, 1, 0, Up},
            {Left, 0, -1, Right}, {Right, 0, 1, Left},
        };
        for (int row = 0; row < lines.size(); ++row) {
            for (int col = 0; col < lines.at(row).size(); ++col) {
                const int arms = armsOf(at(row, col));
                for (const Step &step : steps) {
                    if (!(arms & step.arm))
                        continue;
                    const QChar next = at(row + step.dRow, col + step.dCol);
                    if (armsOf(next) & step.back)
                        continue;
                    if (terminates(next))
                        continue;
                    // A label the line runs into keeps its padding: a
                    // subgraph's title sits in the frame as `┌─ Name ─┐`,
                    // and an edge label displaces the line it rides on
                    // with a space either side.
                    if (next == QLatin1Char(' ')
                        && terminates(at(row + 2 * step.dRow,
                                         col + 2 * step.dCol)))
                        continue;
                    QFAIL(qPrintable(
                        QStringLiteral("dangling arm at row %1 col %2:\n%3")
                            .arg(row).arg(col).arg(output)));
                }
            }
        }
    }

    enum Arm { Up = 1, Down = 2, Left = 4, Right = 8 };

private slots:
    void testEmptyScene();
    void testFlowchartFixture();
    void testFlowchartBackEdgeAvoidsBoxes();
    void testFlowchartFanOutReachesEveryTarget();
    void testFlowchartFanInReachesTheTarget();
    void testFlowchartSelfLoopReturns();
    void testFlowchartSubgraph();
    void testSequenceFixture();
    void testClassFixture();
    void testStateFixture();
    void testErFixture();
    void testDeterminism();
    void testExtremePinnedCoordinatesStayBounded();
};

void TestTextDiagram::testEmptyScene()
{
    QCOMPARE(renderText(Scene()), QString());
}

void TestTextDiagram::testFlowchartFixture()
{
    const QString out = renderSource(
        "flowchart TD\n"
        "  A[Start] --> B{Decision}\n"
        "  B -->|yes| C[Done]\n"
        "  B -->|no| D[Retry]\n");
    verifyClosure(out);

    // Every label lands inside a box; the decision shows as < … >.
    QVERIFY(out.contains("│ Start │"));
    QVERIFY(out.contains("< Decision >"));
    QVERIFY(out.contains("│ Done │"));
    QVERIFY(out.contains("│ Retry │"));
    // Edges arrow downward and the labels ride along.
    QVERIFY(out.count(QChar(u'▼')) >= 3);
    QVERIFY(out.contains("yes"));
    QVERIFY(out.contains("no"));
    // Box vocabulary only (light corners).
    QVERIFY(out.contains(QChar(u'┌')));
    QVERIFY(out.contains(QChar(u'└')));
}

void TestTextDiagram::testFlowchartBackEdgeAvoidsBoxes()
{
    const QString out = renderSource(
        "flowchart TD\n"
        "  A[Start] --> B{Decision}\n"
        "  B -->|yes| C[Done]\n"
        "  B -->|no| D[Retry]\n"
        "  D --> A\n");
    verifyClosure(out);

    // The back edge routes around the flank and enters a side wall.
    QVERIFY(out.contains(QChar(u'◄')));
    // No line ever cuts through a label: every label still sits intact
    // between its walls.
    QVERIFY(out.contains("│ Start │"));
    QVERIFY(out.contains("< Decision >"));
    QVERIFY(out.contains("│ Retry │"));
}

// One node fanning out to three is the shape the gallery's own diagram
// has, and it is the shape the channel bookkeeping used to get wrong. A
// three-row box has one usable cell on each wall, so all three edges leave
// through the same one; treating the first edge's stub as occupied
// territory made the other two give up on a direct route and detour below
// the whole drawing, where they arrived at the wrong boxes from
// underneath. The branches share the stub and split at one spine instead.
void TestTextDiagram::testFlowchartFanOutReachesEveryTarget()
{
    const QString out = renderSource(
        "flowchart LR\n"
        "  A[Write markdown] --> B{Rendered live}\n"
        "  B --> C[Math]\n"
        "  B --> D[Diagrams]\n"
        "  B --> E[Tables]\n");
    verifyClosure(out);

    // Every edge arrives at its own target's left wall.
    QVERIFY2(out.contains(QStringLiteral("►│ < Rendered live > │")),
             qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("►│ Math │")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("►│ Diagrams │")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("►│ Tables │")), qPrintable(out));
    // Four edges, four arrowheads, all of them pointing right: nothing
    // took the detour below the diagram, which arrives pointing up.
    QCOMPARE(out.count(QChar(u'►')), 4);
    QCOMPARE(out.count(QChar(u'▲')), 0);
    QCOMPARE(out.count(QChar(u'▼')), 0);
}

// The mirror case: three edges converging on one wall cell.
void TestTextDiagram::testFlowchartFanInReachesTheTarget()
{
    const QString out = renderSource(
        "flowchart LR\n"
        "  A[One] --> D[Sink]\n"
        "  B[Two] --> D\n"
        "  C[Three] --> D\n");
    verifyClosure(out);

    QVERIFY2(out.contains(QStringLiteral("►│ Sink │")), qPrintable(out));
    // Each source has a line leaving its right wall.
    QVERIFY2(out.contains(QStringLiteral("│ One │─")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("│ Two │─")), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("│ Three │─")), qPrintable(out));
    QCOMPARE(out.count(QChar(u'▲')), 0);
}

// A three-row box has a single interior row, so a self-loop cannot leave
// and return through the same wall. It leaves the side, drops past the box
// and comes back up into the floor; returning along the row it left on
// drew a stub that went nowhere.
void TestTextDiagram::testFlowchartSelfLoopReturns()
{
    const QString out = renderSource(
        "flowchart TD\n"
        "  A[Loop] --> A\n"
        "  A --> B[Next]\n");
    verifyClosure(out);

    QVERIFY2(out.contains(QStringLiteral("│ Loop │─")), qPrintable(out));
    // The loop comes back into the box's floor, and the forward edge still
    // drops into Next.
    QVERIFY2(out.contains(QChar(u'▲')), qPrintable(out));
    QVERIFY2(out.contains(QStringLiteral("│ Next │")), qPrintable(out));
    QCOMPARE(out.count(QChar(u'▼')), 1);
}

void TestTextDiagram::testFlowchartSubgraph()
{
    const QString out = renderSource(
        "flowchart TD\n"
        "  subgraph Backend\n"
        "    S[Server] --> Q[Queue]\n"
        "  end\n"
        "  U[User] --> S\n");
    verifyClosure(out);
    QVERIFY(out.contains("Backend"));   // group title on the frame
    QVERIFY(out.contains("│ Server │"));
    QVERIFY(out.contains("│ Queue │"));
}

void TestTextDiagram::testSequenceFixture()
{
    const QString out = renderSource(
        "sequenceDiagram\n"
        "  participant A as Alice\n"
        "  participant B as Bob\n"
        "  A->>B: Hello\n"
        "  B-->>A: Hi back\n");
    verifyClosure(out);

    // Actor boxes top and bottom, so both names appear twice.
    QCOMPARE(out.count("│ Alice │"), 2);
    QCOMPARE(out.count("│ Bob │"), 2);
    // Messages: one arrow each way with the labels above the lines.
    QVERIFY(out.contains(QChar(u'►')));
    QVERIFY(out.contains(QChar(u'◄')));
    QVERIFY(out.contains("Hello"));
    QVERIFY(out.contains("Hi back"));
    // Lifelines drop vertically between the boxes.
    QVERIFY(out.contains(QChar(u'│')));
}

void TestTextDiagram::testClassFixture()
{
    const QString out = renderSource(
        "classDiagram\n"
        "  class Animal {\n"
        "    +name: string\n"
        "    +speak()\n"
        "  }\n"
        "  Animal <|-- Dog\n");
    verifyClosure(out);
    QVERIFY(out.contains("Animal"));
    QVERIFY(out.contains("+name: string"));
    QVERIFY(out.contains("+speak()"));
    QVERIFY(out.contains("Dog"));
    // The UML extension head degrades to the open triangle, deliberately.
    QVERIFY(out.contains(QChar(u'△')));
    // A compartment separator is a line across the box with no endpoints
    // of its own. Routing it as though it were an edge hung a stub loop
    // off the box's flank; the box's rows now end at their right wall.
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("Animal"))
            || line.contains(QStringLiteral("+name")))
            QVERIFY2(line.endsWith(QChar(u'│')), qPrintable(line));
    }
}

void TestTextDiagram::testStateFixture()
{
    const QString out = renderSource(
        "stateDiagram-v2\n"
        "  [*] --> Idle\n"
        "  Idle --> Busy: start\n"
        "  Busy --> [*]\n");
    verifyClosure(out);
    QVERIFY(out.contains("│ Idle │"));
    QVERIFY(out.contains("│ Busy │"));
    QVERIFY(out.contains("start"));
    // Start and end circles render as (*) boxes — exactly two (the end
    // state's concentric double circle merges into one).
    QCOMPARE(out.count("(*)"), 2);
    QVERIFY(out.count(QChar(u'▼')) >= 3);
}

void TestTextDiagram::testErFixture()
{
    const QString out = renderSource(
        "erDiagram\n"
        "  CUSTOMER ||--o{ ORDER : places\n");
    verifyClosure(out);
    QVERIFY(out.contains("CUSTOMER"));
    QVERIFY(out.contains("ORDER"));
    QVERIFY(out.contains("places"));
    // Crow's-foot degradation: some < > ^ v end reaches the line.
    QVERIFY(out.contains(QChar(u'v')) || out.contains(QChar(u'^'))
            || out.contains(QChar(u'<')) || out.contains(QChar(u'>')));
}

void TestTextDiagram::testDeterminism()
{
    const QString source =
        "flowchart LR\n  A[Input] --> B[Process]\n  B --> C[Output]\n"
        "  B --> D[Log]\n  D --> A\n";
    const QString once = renderSource(source);
    const QString twice = renderSource(source);
    QVERIFY(!once.isEmpty());
    QCOMPARE(once, twice);
}

// A note's own arrangement comment decides where nodes sit, so it decides how
// large a grid "Copy as text" builds. Two nodes pinned far apart on both axes
// are two lines of Mermaid, and they used to produce a canvas of roughly
// 2 x 10^8 cells: about 500 MiB of resident memory, and std::bad_alloc under a
// modest virtual-memory cap. The export now clips to a cell budget, so the
// work and the result are both bounded by the budget rather than by the
// coordinates.
void TestTextDiagram::testExtremePinnedCoordinatesStayBounded()
{
    QElapsedTimer timer;
    timer.start();
    const QString out = renderSource(
        "flowchart TD\n"
        "  A[Start] --> B[End]\n"
        "%% mermaid-flow:pos A=0,0 B=170000,200000\n");
    const qint64 elapsed = timer.elapsed();

    // Bounded work: the ceiling is far above what the clipped export costs and
    // far below the seconds the unclipped one took, so it does not turn on how
    // fast the machine is.
    QVERIFY2(elapsed < 5000,
             qPrintable(QStringLiteral("export took %1 ms").arg(elapsed)));
    // Bounded result: at most one character per budgeted cell, plus its line
    // separators.
    QVERIFY2(out.size() <= 2 * Diagram::kMaxTextCanvasCells,
             qPrintable(QStringLiteral("export produced %1 characters")
                            .arg(out.size())));
    // Still a drawing, not a failure: the first node came out.
    QVERIFY(out.contains(QChar(u'┌')));
}

QTEST_MAIN(TestTextDiagram)
#include "test_textdiagram.moc"
