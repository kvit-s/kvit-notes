// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest/QtTest>

#include "diagrams/diagramclassifier.h"
#include "diagrams/diagramrepair.h"
#include "diagrams/mermaidrenderer.h"
#include "diagrams/textdiagram.h"

using namespace Diagram;

// Unit suite for the Scene → text serializer (pre-launch-plan.md §2.2), in
// the established repair-test style: assert glyphs and structural facts,
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
    }

private slots:
    void testEmptyScene();
    void testFlowchartFixture();
    void testFlowchartBackEdgeAvoidsBoxes();
    void testFlowchartSubgraph();
    void testSequenceFixture();
    void testClassFixture();
    void testStateFixture();
    void testErFixture();
    void testDeterminism();
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

QTEST_MAIN(TestTextDiagram)
#include "test_textdiagram.moc"
