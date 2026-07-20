// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include "diagrams/mermaidlexer.h"
#include "diagrams/mermaidparser.h"

using namespace Mermaid;

// Lexer offsets and flowchart-parser ASTs (diagrams-prd.md §8.2, §15). The
// parser recognizes the v1 flowchart subset (§9) and returns the
// unsupported-family diagnostic for every other family without discarding
// source.
class TestMermaidParser : public QObject
{
    Q_OBJECT

    static const Node *node(const FlowchartAst &a, const QString &id)
    {
        const int i = a.indexOfNode(id);
        return i >= 0 ? &a.nodes.at(i) : nullptr;
    }

private slots:
    // ---- lexer ----
    void lexerReportsOffsets()
    {
        MermaidLexer lex(QStringLiteral("flowchart LR\n  A --> B"));
        const QList<Token> &t = lex.tokens();
        // flowchart(1:1) LR(1:11) Sep A(2:3) Edge(2:5) B(2:9)
        QVERIFY(t.size() >= 6);
        QCOMPARE(t.at(0).kind, Token::Word);
        QCOMPARE(t.at(0).text, QString("flowchart"));
        QCOMPARE(t.at(0).line, 1);
        QCOMPARE(t.at(0).column, 1);
        QCOMPARE(t.at(1).text, QString("LR"));
        QCOMPARE(t.at(1).column, 11);
        // The node on line 2 starts at column 3 (two-space indent).
        const Token *a = nullptr;
        for (const Token &tk : t)
            if (tk.kind == Token::Word && tk.text == QLatin1String("A")) { a = &tk; break; }
        QVERIFY(a);
        QCOMPARE(a->line, 2);
        QCOMPARE(a->column, 3);
    }

    void lexerParsesEdgeVariants()
    {
        auto edge = [](const QString &s) {
            MermaidLexer lex(s);
            for (const Token &t : lex.tokens())
                if (t.kind == Token::Edge)
                    return t;
            return Token{};
        };
        QCOMPARE(edge("A --> B").stroke, EdgeStroke::Solid);
        QVERIFY(edge("A --> B").arrowEnd);
        QCOMPARE(edge("A -.-> B").stroke, EdgeStroke::Dotted);
        QCOMPARE(edge("A ==> B").stroke, EdgeStroke::Thick);
        QVERIFY(!edge("A --- B").arrowEnd);        // open link
        QVERIFY(edge("A <--> B").arrowStart);
        QCOMPARE(edge("A ---> B").minLen, 2);      // extra dash lengthens rank
        QCOMPARE(edge("A -- yes --> B").edgeLabel, QString("yes"));
        QCOMPARE(edge("A == big text ==> B").edgeLabel, QString("big text"));
    }

    // ---- header / family detection ----
    void detectsFlowchart()
    {
        MermaidParser p;
        for (const QString &src : {QStringLiteral("flowchart TD\nA-->B"),
                                   QStringLiteral("graph LR\nA-->B")}) {
            const ParseResult r = p.parse(src);
            QCOMPARE(r.type, DiagramType::Flowchart);
            QVERIFY(r.supported);
            QVERIFY(!r.hasErrors());
        }
    }

    void unsupportedFamilyDiagnosesWithoutDiscarding()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "gantt\n  title A deferred family"));
        QCOMPARE(r.type, DiagramType::Unsupported);
        QVERIFY(!r.supported);
        QVERIFY(r.hasErrors());
        QVERIFY(r.firstError().message.contains("Unsupported"));
    }

    // ---- directions ----
    void parsesDirections()
    {
        MermaidParser p;
        QCOMPARE(p.parse("flowchart TB\nA-->B").flowchart.direction, Direction::TB);
        QCOMPARE(p.parse("graph TD\nA-->B").flowchart.direction, Direction::TB);
        QCOMPARE(p.parse("flowchart LR\nA-->B").flowchart.direction, Direction::LR);
        QCOMPARE(p.parse("flowchart RL\nA-->B").flowchart.direction, Direction::RL);
        QCOMPARE(p.parse("flowchart BT\nA-->B").flowchart.direction, Direction::BT);
    }

    // ---- nodes, shapes, chains ----
    void parsesNodesShapesAndChain()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\n"
            "  A[Start] --> B{Choice}\n"
            "  B -->|yes| C([Done])\n"
            "  B -->|no| D((Stop))\n"));
        const FlowchartAst &a = r.flowchart;
        QCOMPARE(a.nodes.size(), 4);
        QCOMPARE(node(a, "A")->label, QString("Start"));
        QCOMPARE(node(a, "A")->shape, NodeShape::Rect);
        QCOMPARE(node(a, "B")->shape, NodeShape::Rhombus);
        QCOMPARE(node(a, "C")->shape, NodeShape::Stadium);
        QCOMPARE(node(a, "D")->shape, NodeShape::Circle);
        QCOMPARE(a.edges.size(), 3);
        // The pipe labels attach to their edges.
        bool sawYes = false, sawNo = false;
        for (const Edge &e : a.edges) {
            if (e.from == "B" && e.to == "C") { QCOMPARE(e.label, QString("yes")); sawYes = true; }
            if (e.from == "B" && e.to == "D") { QCOMPARE(e.label, QString("no")); sawNo = true; }
        }
        QVERIFY(sawYes && sawNo);
    }

    void chainedEdges()
    {
        MermaidParser p;
        const ParseResult r = p.parse("flowchart LR\nA --> B --> C --> D");
        QCOMPARE(r.flowchart.nodes.size(), 4);
        QCOMPARE(r.flowchart.edges.size(), 3);
    }

    void ampNodeLists()
    {
        MermaidParser p;
        const ParseResult r = p.parse("flowchart LR\nA & B --> C & D");
        // Cartesian product: A->C, A->D, B->C, B->D.
        QCOMPARE(r.flowchart.edges.size(), 4);
    }

    // ---- subgraphs ----
    void subgraphsGroupMembers()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart TB\n"
            "  subgraph one [Group One]\n"
            "    A --> B\n"
            "  end\n"
            "  B --> C\n"));
        QCOMPARE(r.flowchart.subgraphs.size(), 1);
        QCOMPARE(r.flowchart.subgraphs.first().title, QString("Group One"));
        const QStringList members = r.flowchart.subgraphs.first().nodeIds;
        QVERIFY(members.contains("A"));
        QVERIFY(members.contains("B"));
        QVERIFY(!members.contains("C"));   // declared after `end`
    }

    // ---- classes / styles ----
    void classDefAndClassApply()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\n"
            "  classDef hot fill:#f96,stroke:#333,stroke-width:2px\n"
            "  A --> B\n"
            "  class A,B hot\n"));
        QVERIFY(r.flowchart.classDefs.contains("hot"));
        const ClassDef &d = r.flowchart.classDefs.value("hot");
        QVERIFY(d.hasFill);
        QCOMPARE(d.fill, QColor("#f96"));
        QVERIFY(d.hasStroke);
        QCOMPARE(d.strokeWidth, 2.0);
        QVERIFY(node(r.flowchart, "A")->classes.contains("hot"));
        QVERIFY(node(r.flowchart, "B")->classes.contains("hot"));
    }

    void tripleColonClass()
    {
        MermaidParser p;
        const ParseResult r = p.parse("flowchart LR\nA:::hot --> B");
        QVERIFY(node(r.flowchart, "A")->classes.contains("hot"));
    }

    // ---- comments / accessibility / restricted ----
    void commentsIgnored()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\n"
            "%% this is a comment\n"
            "A --> B %% trailing\n"));
        QCOMPARE(r.flowchart.nodes.size(), 2);
        QCOMPARE(r.flowchart.edges.size(), 1);
    }

    void accessibilityDirectives()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\n"
            "  accTitle: My flow\n"
            "  accDescr: Two steps\n"
            "  A --> B\n"));
        QCOMPARE(r.flowchart.accTitle, QString("My flow"));
        QCOMPARE(r.flowchart.accDescr, QString("Two steps"));
    }

    void clickIsWarnedAndIgnored()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\nA --> B\nclick A \"https://x\"\n"));
        QVERIFY(!r.hasErrors());
        bool warned = false;
        for (const Diagnostic &d : r.diagnostics)
            if (d.severity == Diagnostic::Warning && d.message.contains("click"))
                warned = true;
        QVERIFY(warned);
    }

    void frontmatterStripped()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "---\ntitle: My Chart\n---\nflowchart LR\nA --> B\n"));
        QCOMPARE(r.type, DiagramType::Flowchart);
        QCOMPARE(r.flowchart.accTitle, QString("My Chart"));
        QCOMPARE(r.flowchart.nodes.size(), 2);
    }

    // ---- limits / robustness ----
    void deterministicNodeOrder()
    {
        MermaidParser p;
        const ParseResult r1 = p.parse("flowchart LR\nA-->B-->C\nZ-->A");
        const ParseResult r2 = p.parse("flowchart LR\nA-->B-->C\nZ-->A");
        QCOMPARE(r1.flowchart.nodes.size(), r2.flowchart.nodes.size());
        for (int i = 0; i < r1.flowchart.nodes.size(); ++i)
            QCOMPARE(r1.flowchart.nodes.at(i).id, r2.flowchart.nodes.at(i).id);
        // First-encounter order: A, B, C, Z.
        QCOMPARE(r1.flowchart.nodes.at(0).id, QString("A"));
        QCOMPARE(r1.flowchart.nodes.at(3).id, QString("Z"));
    }

    void nodeLimitEnforced()
    {
        QString src = QStringLiteral("flowchart LR\n");
        for (int i = 0; i < kMaxNodes + 50; ++i)
            src += QStringLiteral("n%1 --> n%2\n").arg(i).arg(i + 1);
        MermaidParser p;
        const ParseResult r = p.parse(src);
        QVERIFY(r.flowchart.nodes.size() <= kMaxNodes);
        bool capped = false;
        for (const Diagnostic &d : r.diagnostics)
            if (d.message.contains("Too many nodes"))
                capped = true;
        QVERIFY(capped);
    }

    void unterminatedShapeRecovers()
    {
        MermaidParser p;
        // A missing close bracket must not hang or crash; the source is kept.
        const ParseResult r = p.parse("flowchart LR\nA[unterminated --> B");
        Q_UNUSED(r);
        QVERIFY(true);
    }

    // ---- flow.jison@11.16.0 retro-audit (diagrams-prd.md §9.5) ----

    static bool hasWarning(const ParseResult &r, const char *needle)
    {
        for (const Diagnostic &d : r.diagnostics)
            if (d.severity == Diagnostic::Warning
                && d.message.contains(QLatin1String(needle)))
                return true;
        return false;
    }

    void auditVertexShapes()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\n"
            "  A(((double)))\n"
            "  B(-ellipse-)\n"
            "  C>odd flag]\n"
            "  D[/trap\\]\n"
            "  E[\\invtrap/]\n"
            "  F[\\leanleft\\]\n"
            "  G[/leanright/]\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(node(r.flowchart, "A")->shape, NodeShape::DoubleCircle);
        QCOMPARE(node(r.flowchart, "A")->label, QString("double"));
        QCOMPARE(node(r.flowchart, "B")->shape, NodeShape::Ellipse);
        QCOMPARE(node(r.flowchart, "C")->shape, NodeShape::Odd);
        QCOMPARE(node(r.flowchart, "C")->label, QString("odd flag"));
        QCOMPARE(node(r.flowchart, "D")->shape, NodeShape::Trapezoid);
        QCOMPARE(node(r.flowchart, "E")->shape, NodeShape::TrapezoidAlt);
        QCOMPARE(node(r.flowchart, "F")->shape, NodeShape::ParallelogramAlt);
        QCOMPARE(node(r.flowchart, "G")->shape, NodeShape::Parallelogram);
    }

    void auditShapeDataBlocks()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart TD\n"
            "  A@{ shape: hexagon, label: \"Prep step\" }\n"
            "  B@{ shape: cyl }\n"
            "  A --> B\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(node(r.flowchart, "A")->shape, NodeShape::Hexagon);
        QCOMPARE(node(r.flowchart, "A")->label, QString("Prep step"));
        QCOMPARE(node(r.flowchart, "B")->shape, NodeShape::Cylinder);
        QCOMPARE(r.flowchart.edges.size(), 1);
    }

    void auditShapeDataMultilineAndUnknown()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart TD\n"
            "  A@{\n"
            "    shape: cloud\n"
            "    icon: \"fa:fa-user\"\n"
            "  }\n"
            "  A --> B\n"));
        QVERIFY(!r.hasErrors());
        // Unknown shape falls back to Rect with a warning; unknown keys warn.
        QCOMPARE(node(r.flowchart, "A")->shape, NodeShape::Rect);
        QVERIFY(hasWarning(r, "Unknown shape"));
        QVERIFY(hasWarning(r, "icon"));
        QCOMPARE(r.flowchart.edges.size(), 1);
    }

    void auditEdgeIdsAndInvisibleLinks()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\n"
            "  A e1@--> B\n"
            "  B ~~~ C\n"));
        QVERIFY(!r.hasErrors());
        QCOMPARE(r.flowchart.edges.size(), 2);
        QCOMPARE(r.flowchart.edges.at(0).id, QString("e1"));
        QCOMPARE(r.flowchart.edges.at(0).from, QString("A"));
        QCOMPARE(r.flowchart.edges.at(0).to, QString("B"));
        QVERIFY(r.flowchart.edges.at(1).invisible);
        QVERIFY(!r.flowchart.edges.at(1).arrowEnd);
    }

    void auditHeaderVariants()
    {
        MermaidParser p;
        const ParseResult elk = p.parse("flowchart-elk TD\nA-->B");
        QCOMPARE(elk.type, DiagramType::Flowchart);
        QVERIFY(elk.supported);
        QVERIFY(hasWarning(elk, "layout engine"));

        QCOMPARE(p.parse("graph >\nA-->B").flowchart.direction, Direction::LR);
        QCOMPARE(p.parse("graph <\nA-->B").flowchart.direction, Direction::RL);
        QCOMPARE(p.parse("graph ^\nA-->B").flowchart.direction, Direction::BT);
        QCOMPARE(p.parse("graph v\nA-->B").flowchart.direction, Direction::TB);
    }

    void auditClassDefCommaNames()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\n"
            "  classDef hot,cold fill:#f96\n"
            "  A:::hot --> B:::cold\n"));
        QVERIFY(r.flowchart.classDefs.contains("hot"));
        QVERIFY(r.flowchart.classDefs.contains("cold"));
        QVERIFY(r.flowchart.classDefs.value("cold").hasFill);
    }

    void auditSubgraphMultiWordTitle()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart TB\n"
            "  subgraph Main Processing Stage\n"
            "    A --> B\n"
            "  end\n"));
        QCOMPARE(r.flowchart.subgraphs.size(), 1);
        QCOMPARE(r.flowchart.subgraphs.first().title,
                 QString("Main Processing Stage"));
    }

    void auditMarkdownStringLabel()
    {
        MermaidParser p;
        const ParseResult r = p.parse(
            "flowchart LR\n  A[\"`emphasised label`\"] --> B");
        QCOMPARE(node(r.flowchart, "A")->label, QString("emphasised label"));
    }

    void auditAccDescrMultiline()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\n"
            "  accDescr {\n"
            "    A description across\n"
            "    two lines\n"
            "  }\n"
            "  A --> B\n"));
        QVERIFY(!r.hasErrors());
        QVERIFY(r.flowchart.accDescr.contains("A description across"));
        QVERIFY(r.flowchart.accDescr.contains("two lines"));
        QCOMPARE(r.flowchart.nodes.size(), 2);
    }

    void auditVertexWithProps()
    {
        MermaidParser p;
        const ParseResult r = p.parse(
            "flowchart LR\n  A[|borders:none|the text] --> B");
        QVERIFY(!r.hasErrors());
        QCOMPARE(node(r.flowchart, "A")->label, QString("the text"));
        QVERIFY(hasWarning(r, "properties"));
    }

    void auditLinkStyleWarns()
    {
        MermaidParser p;
        const ParseResult r = p.parse(QStringLiteral(
            "flowchart LR\nA --> B\nlinkStyle 0 stroke:#f00\n"));
        QVERIFY(!r.hasErrors());
        QVERIFY(hasWarning(r, "linkStyle"));
    }

    // ---- §20 source spans ----

    void sourceSpansMapOntoSource()
    {
        const QString src = QStringLiteral(
            "flowchart LR\n  Alpha[Start here] --> Beta\n  Alpha --> Gamma\n");
        MermaidParser p;
        const ParseResult r = p.parse(src);
        QVERIFY(!r.hasErrors());
        const Node *a = node(r.flowchart, "Alpha");
        QVERIFY(a);
        // The declaration span points at the first `Alpha`.
        QVERIFY(a->idSpan.valid());
        QCOMPARE(src.mid(a->idSpan.start, a->idSpan.length), QString("Alpha"));
        QCOMPARE(a->idSpan.start, src.indexOf("Alpha"));
        // The raw label span sits between the brackets.
        QVERIFY(a->labelSpan.valid());
        QCOMPARE(src.mid(a->labelSpan.start, a->labelSpan.length),
                 QString("Start here"));
        // The bracket construct span covers `[Start here]`.
        QCOMPARE(src.mid(a->shapeSpan.start, a->shapeSpan.length),
                 QString("[Start here]"));
        // Every reference is recorded (declaration + second statement).
        QCOMPARE(a->refSpans.size(), 2);
        QCOMPARE(src.mid(a->refSpans.at(1).start, a->refSpans.at(1).length),
                 QString("Alpha"));
        // Edge spans: the arrow token and the statement.
        const Edge &e = r.flowchart.edges.first();
        QCOMPARE(src.mid(e.opSpan.start, e.opSpan.length), QString("-->"));
        QVERIFY(e.stmtSpan.valid());
        QCOMPARE(src.mid(e.stmtSpan.start, e.stmtSpan.length),
                 QString("Alpha[Start here] --> Beta"));
    }

    void sourceSpansShiftPastFrontmatter()
    {
        const QString src = QStringLiteral(
            "---\ntitle: T\n---\nflowchart LR\n  A --> B\n");
        MermaidParser p;
        const ParseResult r = p.parse(src);
        QVERIFY(!r.hasErrors());
        const Node *a = node(r.flowchart, "A");
        QVERIFY(a && a->idSpan.valid());
        QCOMPARE(src.mid(a->idSpan.start, a->idSpan.length), QString("A"));
        QCOMPARE(a->idSpan.start, src.indexOf("A --> B"));
    }
};

QTEST_APPLESS_MAIN(TestMermaidParser)
#include "test_mermaidparser.moc"
