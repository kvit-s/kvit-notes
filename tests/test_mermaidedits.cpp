// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QRegularExpression>

#include "diagrams/diagramlayout.h"
#include "diagrams/mermaidedits.h"
#include "diagrams/mermaidparser.h"

using namespace Mermaid;
using namespace Diagram;

// §20.3 manual arrangement and its §20.7 gates: pos-line parsing, arranged-
// mode layout, full-snapshot writes confined to the pos line, reset, and the
// cross-tool grammar fixtures (obsidian-mermaid-flow interop).
class TestMermaidEdits : public QObject
{
    Q_OBJECT

    static ParseResult parse(const QString &src)
    {
        MermaidParser p;
        return p.parse(src);
    }
    static LayoutOptions opts()
    {
        LayoutOptions o;
        o.fontFamily = QStringLiteral("sans-serif");
        o.fontPixelSize = 14;
        return o;
    }
    // §20.7 byte-preservation: with pos lines removed from both, the sources
    // are identical — the diff was confined to the pos line.
    static bool differsOnlyInPosLine(const QString &before,
                                     const QString &after)
    {
        auto strip = [](const QString &s) {
            QStringList kept;
            for (const QString &line : s.split(u'\n'))
                if (!line.trimmed().startsWith(
                        QLatin1String("%% mermaid-flow:pos")))
                    kept << line;
            return kept;
        };
        return strip(before) == strip(after);
    }

private slots:
    // ---- pos-line parsing ----

    void posLineParses()
    {
        const ParseResult r = parse(QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "%% mermaid-flow:pos A=120,40 B=260,180\n"));
        QVERIFY(!r.hasErrors());
        QVERIFY(r.flowchart.hasPosLine);
        QCOMPARE(r.flowchart.posEntries.size(), 2);
        QCOMPARE(r.flowchart.posEntries.at(0).id, QString("A"));
        QCOMPARE(r.flowchart.posEntries.at(0).x, 120.0);
        QCOMPARE(r.flowchart.posEntries.at(1).y, 180.0);
    }

    void posLineToleratesPluginDimensionsAndMalformedEntries()
    {
        // The plugin's optional `,width,height` suffix parses (dimensions
        // ignored); individually malformed entries are skipped.
        const ParseResult r = parse(QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "%% mermaid-flow:pos A=100,200,120,40 broken B=300,400 C=x,y\n"));
        QVERIFY(r.flowchart.hasPosLine);
        QCOMPARE(r.flowchart.posEntries.size(), 2);
        QCOMPARE(r.flowchart.posEntries.at(0).x, 100.0);
        QCOMPARE(r.flowchart.posEntries.at(1).id, QString("B"));
    }

    void secondPosLineIsIgnoredWithDiagnostic()
    {
        const ParseResult r = parse(QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "%% mermaid-flow:pos A=1,2\n"
            "%% mermaid-flow:pos A=9,9\n"));
        QVERIFY(r.flowchart.hasPosLine);
        QCOMPARE(r.flowchart.posEntries.size(), 1);
        QCOMPARE(r.flowchart.posEntries.first().x, 1.0);
        bool warned = false;
        for (const Diagnostic &d : r.diagnostics)
            if (d.severity == Diagnostic::Warning
                && d.message.contains("mermaid-flow:pos"))
                warned = true;
        QVERIFY(warned);
    }

    // ---- arranged-mode layout ----

    void arrangedModePinsCenters()
    {
        const ParseResult r = parse(QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "%% mermaid-flow:pos A=100,50 B=300,200\n"));
        const Scene s = layoutFlowchart(r.flowchart, opts());
        QPointF a, b;
        for (const Shape &sh : s.shapes) {
            if (sh.nodeId == QLatin1String("A")) a = sh.rect.center();
            if (sh.nodeId == QLatin1String("B")) b = sh.rect.center();
        }
        // finalizeSceneBounds translates uniformly; relative geometry holds.
        QCOMPARE(qRound(b.x() - a.x()), 200);
        QCOMPARE(qRound(b.y() - a.y()), 150);
    }

    void arrangedModePlacesUnpinnedBeyondContent()
    {
        const ParseResult r = parse(QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "  B --> C\n"
            "%% mermaid-flow:pos A=100,50 B=300,80\n"));
        const Scene s = layoutFlowchart(r.flowchart, opts());
        QPointF a, b, c;
        for (const Shape &sh : s.shapes) {
            if (sh.nodeId == QLatin1String("A")) a = sh.rect.center();
            if (sh.nodeId == QLatin1String("B")) b = sh.rect.center();
            if (sh.nodeId == QLatin1String("C")) c = sh.rect.center();
        }
        // C (no entry) lands beyond the pinned content along the TB flow
        // without moving A or B.
        QVERIFY(c.y() > qMax(a.y(), b.y()));
        QCOMPARE(qRound(b.x() - a.x()), 200);
    }

    void arrangedModeRoutesBeziers()
    {
        const ParseResult r = parse(QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "%% mermaid-flow:pos A=100,50 B=100,220\n"));
        const Scene s = layoutFlowchart(r.flowchart, opts());
        bool cubic = false;
        for (const Path &p : s.paths) {
            if (p.edgeIndex < 0)
                continue;
            for (int i = 0; i < p.path.elementCount(); ++i)
                if (p.path.elementAt(i).type == QPainterPath::CurveToElement)
                    cubic = true;
        }
        QVERIFY(cubic);
    }

    void arrangedModeBowsParallelEdges()
    {
        const ParseResult r = parse(QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "  A --> B\n"
            "%% mermaid-flow:pos A=100,50 B=100,220\n"));
        const Scene s = layoutFlowchart(r.flowchart, opts());
        QList<QPainterPath> edges;
        for (const Path &p : s.paths)
            if (p.edgeIndex >= 0)
                edges.append(p.path);
        QCOMPARE(edges.size(), 2);
        // The parallel edges bow apart: their midpoints differ.
        const QPointF m1 = edges.at(0).pointAtPercent(0.5);
        const QPointF m2 = edges.at(1).pointAtPercent(0.5);
        QVERIFY(std::hypot((m1 - m2).x(), (m1 - m2).y()) > 8.0);
    }

    // ---- arrangement writes (§20.3, §20.7) ----

    void writeAppendsPosLineAsLastLine()
    {
        const QString src = QStringLiteral(
            "flowchart TD\n  %% a comment worth keeping\n  A --> B\n");
        const auto r = Edits::writeArrangement(
            src, { { "A", QPointF(100, 50) }, { "B", QPointF(300, 200) } });
        QVERIFY(r.ok);
        QVERIFY(r.source.endsWith(
            QStringLiteral("%% mermaid-flow:pos A=100,50 B=300,200\n")));
        QVERIFY(differsOnlyInPosLine(src, r.source));
        // The write reparses cleanly and arms arranged mode.
        const ParseResult back = parse(r.source);
        QVERIFY(!back.hasErrors());
        QVERIFY(back.flowchart.hasPosLine);
        QCOMPARE(back.flowchart.posEntries.size(), 2);
    }

    void writeReplacesExistingLineInPlace()
    {
        const QString src = QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "%% mermaid-flow:pos A=1,1 B=2,2 Ghost=9,9\n"
            "  B --> C\n");
        const auto r = Edits::writeArrangement(
            src, { { "A", QPointF(10, 20) }, { "B", QPointF(30, 40) },
                   { "C", QPointF(50, 60) } });
        QVERIFY(r.ok);
        QVERIFY(differsOnlyInPosLine(src, r.source));
        // Stale-entry garbage collection: Ghost disappears; C appears.
        QVERIFY(!r.source.contains("Ghost"));
        QVERIFY(r.source.contains("C=50,60"));
        // Statements after the old line survive byte-identically.
        QVERIFY(r.source.contains("  B --> C\n"));
    }

    void writeIsDeterministicAndIdempotent()
    {
        const QString src = QStringLiteral("flowchart TD\n  A --> B\n");
        const QList<QPair<QString, QPointF>> pos{
            { "A", QPointF(100, 50) }, { "B", QPointF(300, 200) }
        };
        const auto first = Edits::writeArrangement(src, pos);
        QVERIFY(first.ok);
        const auto second = Edits::writeArrangement(first.source, pos);
        QVERIFY(second.ok);
        QCOMPARE(second.source, first.source);
    }

    void writeDropsPluginDimensionSuffix()
    {
        const QString src = QStringLiteral(
            "flowchart TD\n  A --> B\n"
            "%% mermaid-flow:pos A=100,50,120,40 B=300,200,90,36\n");
        const ParseResult before = parse(src);
        QVERIFY(before.flowchart.hasPosLine);
        const auto r = Edits::writeArrangement(
            src, { { "A", QPointF(100, 50) }, { "B", QPointF(300, 200) } });
        QVERIFY(r.ok);
        QVERIFY(!r.source.contains(",120,40"));
        QVERIFY(r.source.contains("A=100,50 B=300,200"));
    }

    void writeRefusesNonFlowcharts()
    {
        const auto r = Edits::writeArrangement(
            QStringLiteral("sequenceDiagram\n  A->>B: hi"),
            { { "A", QPointF(1, 2) } });
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
    }

    void resetRestoresExactPriorSource()
    {
        const QString original = QStringLiteral(
            "flowchart TD\n  %% keep me\n  A --> B\n");
        const auto written = Edits::writeArrangement(
            original, { { "A", QPointF(1, 2) }, { "B", QPointF(3, 4) } });
        QVERIFY(written.ok);
        const auto reset = Edits::resetArrangement(written.source);
        QVERIFY(reset.ok);
        QCOMPARE(reset.source, original);
        // Resetting a source without a pos line is a no-op.
        const auto again = Edits::resetArrangement(original);
        QVERIFY(again.ok);
        QCOMPARE(again.source, original);
    }

    void resetHandlesLineWithoutTrailingNewline()
    {
        const QString src = QStringLiteral(
            "flowchart TD\n  A --> B\n%% mermaid-flow:pos A=1,2");
        const auto r = Edits::resetArrangement(src);
        QVERIFY(r.ok);
        QCOMPARE(r.source, QStringLiteral("flowchart TD\n  A --> B"));
    }

    // ---- cross-tool fixtures (§20.6) ----

    void pluginWrittenLinesParse()
    {
        // Shapes of line the obsidian-mermaid-flow plugin writes.
        const ParseResult r = parse(QStringLiteral(
            "flowchart LR\n"
            "  Start --> Stop\n"
            "%% mermaid-flow:pos Start=80,120 Stop=240,120,140,44\n"));
        QVERIFY(r.flowchart.hasPosLine);
        QCOMPARE(r.flowchart.posEntries.size(), 2);
        const Scene s = layoutFlowchart(r.flowchart, opts());
        QPointF a, b;
        for (const Shape &sh : s.shapes) {
            if (sh.nodeId == QLatin1String("Start")) a = sh.rect.center();
            if (sh.nodeId == QLatin1String("Stop")) b = sh.rect.center();
        }
        QCOMPARE(qRound(b.x() - a.x()), 160);
        QCOMPARE(qRound(b.y() - a.y()), 0);
    }

    // ---- §20.4 semantic gestures / §20.7 byte preservation ----

    // A fixture with comments, odd spacing, pipes, and restricted syntax:
    // the §20.7 corpus shape.
    static QString gestureFixture()
    {
        return QStringLiteral(
            "flowchart TD\n"
            "  %% keep this comment about Alpha\n"
            "  Alpha[The Alpha node]   -->  Beta{Choice?}\n"
            "  Beta -->|yes| Gamma\n"
            "  Beta -->|no| Delta\n"
            "  linkStyle 0 stroke:#f00\n"
            "  classDef hot fill:#f96\n"
            "  class Gamma hot\n");
    }

    void labelEditReplacesOnlyTheLabelSpan()
    {
        const QString src = gestureFixture();
        const auto r = Edits::setNodeLabel(src, "Alpha", "Fresh text");
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.source, QString(src).replace("The Alpha node",
                                                "Fresh text"));
    }

    void labelEditQuotesOnlyWhenNeeded()
    {
        const QString src = QStringLiteral("flowchart LR\n  A[old] --> B\n");
        const auto plain = Edits::setNodeLabel(src, "A", "no quotes needed");
        QVERIFY(plain.ok);
        QVERIFY(plain.source.contains("A[no quotes needed]"));
        const auto piped = Edits::setNodeLabel(src, "A", "a|b");
        QVERIFY(piped.ok);
        QVERIFY(piped.source.contains("A[\"a|b\"]"));
        const auto quoted = Edits::setNodeLabel(src, "A", "he said \"hi\"");
        QVERIFY(!quoted.ok);
    }

    void labelEditAddsBracketsToBareNodes()
    {
        const auto r = Edits::setNodeLabel(
            QStringLiteral("flowchart LR\n  A --> B\n"), "B", "The end");
        QVERIFY(r.ok);
        QVERIFY(r.source.contains("A --> B[The end]"));
    }

    void shapeChangeRewritesOnlyDelimiters()
    {
        const QString src = gestureFixture();
        const auto r = Edits::setNodeShape(src, "Alpha", NodeShape::Hexagon);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.source, QString(src).replace("Alpha[The Alpha node]",
                                                "Alpha{{The Alpha node}}"));
        // Bare node: brackets are created with the id as label.
        const auto bare = Edits::setNodeShape(src, "Delta",
                                              NodeShape::Stadium);
        QVERIFY(bare.ok);
        QVERIFY(bare.source.contains("Delta([Delta])"));
    }

    void renameReplacesRefsButNeverCommentsOrLabels()
    {
        const QString src = gestureFixture();
        const auto r = Edits::renameNode(src, "Alpha", "Omega");
        QVERIFY2(r.ok, qPrintable(r.error));
        // The comment and the string label still say Alpha (§20.4).
        QVERIFY(r.source.contains("%% keep this comment about Alpha"));
        QVERIFY(r.source.contains("Omega[The Alpha node]"));
        QVERIFY(!r.source.contains("Alpha[") );
        // Styling references follow the rename too.
        const auto g = Edits::renameNode(src, "Gamma", "G2");
        QVERIFY(g.ok);
        QVERIFY(g.source.contains("class G2 hot"));
    }

    void renameValidatesTheNewId()
    {
        const QString src = gestureFixture();
        QVERIFY(!Edits::renameNode(src, "Alpha", "Beta").ok);      // collision
        QVERIFY(!Edits::renameNode(src, "Alpha", "end").ok);       // reserved
        QVERIFY(!Edits::renameNode(src, "Alpha", "has space").ok); // charset
        QVERIFY(!Edits::renameNode(src, "Nope", "Fine").ok);       // unknown
    }

    void deleteNodeRemovesWholeStatements()
    {
        const QString src = gestureFixture();
        const auto r = Edits::deleteNode(src, "Delta");
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!r.source.contains("Delta"));
        // Everything else survives byte-identically.
        QCOMPARE(r.source, QString(src).remove("  Beta -->|no| Delta\n"));
    }

    void deleteNodeRefusedWhenStyledWithOthers()
    {
        const QString src = QStringLiteral(
            "flowchart LR\n  A --> B\n  class A,B hot\n  classDef hot fill:#f00\n");
        const auto r = Edits::deleteNode(src, "A");
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("styled"));
    }

    void deleteEdgeRemovesItsStatement()
    {
        const QString src = gestureFixture();
        // Edge 1 is Beta -->|yes| Gamma.
        const auto r = Edits::deleteEdge(src, 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.source, QString(src).remove("  Beta -->|yes| Gamma\n"));
    }

    void deleteEdgeSplitsChains()
    {
        const QString src = QStringLiteral(
            "flowchart LR\n  A[Start] --> B --> C\n  C --> D\n");
        const auto r = Edits::deleteEdge(src, 0);   // A --> B
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.source.contains("  B --> C\n"));
        QVERIFY(!r.source.contains("A[Start]"));
        QVERIFY(r.source.contains("  C --> D\n"));
        // Chained pipe labels survive the split.
        const QString piped = QStringLiteral(
            "flowchart LR\n  A -->|x| B -->|y| C\n");
        const auto rp = Edits::deleteEdge(piped, 0);
        QVERIFY2(rp.ok, qPrintable(rp.error));
        QVERIFY(rp.source.contains("B -->|y| C"));
    }

    void edgeStyleRewritesTheArrowToken()
    {
        const QString src = gestureFixture();
        // Edge 0: Alpha --> Beta (odd spacing preserved around the token).
        const auto dotted = Edits::setEdgeStroke(src, 0, EdgeStroke::Dotted);
        QVERIFY2(dotted.ok, qPrintable(dotted.error));
        QVERIFY(dotted.source.contains("Alpha[The Alpha node]   -.->  Beta"));
        const auto thick = Edits::setEdgeStroke(src, 1, EdgeStroke::Thick);
        QVERIFY(thick.ok);
        QVERIFY(thick.source.contains("Beta ==>|yes| Gamma"));
        // Inline labels are preserved in the rebuilt token.
        const QString inline1 = QStringLiteral(
            "flowchart LR\n  A -- ride --> B\n");
        const auto r = Edits::setEdgeStroke(inline1, 0, EdgeStroke::Thick);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.source.contains("A == ride ==> B"));
    }

    void insertEdgeLandsAfterLastMentionBeforePosLine()
    {
        const QString src = QStringLiteral(
            "flowchart TD\n"
            "  A --> B\n"
            "  B --> C\n"
            "%% mermaid-flow:pos A=1,2 B=3,4 C=5,6\n");
        const auto r = Edits::insertEdge(src, "A", "C");
        QVERIFY2(r.ok, qPrintable(r.error));
        const int inserted = r.source.indexOf("A --> C");
        const int posLine = r.source.indexOf("%% mermaid-flow:pos");
        QVERIFY(inserted > 0);
        QVERIFY(inserted < posLine);
        // Inserted right after the last statement mentioning A.
        QVERIFY(r.source.indexOf("A --> C") > r.source.indexOf("A --> B"));
        QVERIFY(r.source.indexOf("A --> C") < r.source.indexOf("B --> C"));
    }

    void quickAddGeneratesCollisionFreeIds()
    {
        const QString src = QStringLiteral(
            "flowchart LR\n  node1 --> B\n");
        const auto r = Edits::quickAddNode(src, "B");
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.newId, QString("node2"));
        QVERIFY(r.source.contains("B --> node2[New node]"));
    }

    void styleInsertsAndReusesKvitClassDefs()
    {
        const QString src = gestureFixture();
        const auto first = Edits::setNodeStyle(src, "Alpha",
                                               QColor("#ff0000"), QColor());
        QVERIFY2(first.ok, qPrintable(first.error));
        QVERIFY(first.source.contains("classDef kvit_style_1 fill:#ff0000"));
        QVERIFY(first.source.contains("class Alpha kvit_style_1"));
        // The existing user classDef is never rewritten.
        QVERIFY(first.source.contains("classDef hot fill:#f96"));
        // The same color on another node reuses the definition.
        const auto second = Edits::setNodeStyle(first.source, "Beta",
                                                QColor("#ff0000"), QColor());
        QVERIFY(second.ok);
        QCOMPARE(second.source.count("classDef kvit_style_1"), 1);
        QVERIFY(second.source.contains("class Beta kvit_style_1"));
        // Restyling updates the node's class statement in place.
        const auto third = Edits::setNodeStyle(second.source, "Alpha",
                                               QColor("#00ff00"), QColor());
        QVERIFY(third.ok);
        QVERIFY(third.source.contains("classDef kvit_style_2 fill:#00ff00"));
        QVERIFY(third.source.contains("class Alpha kvit_style_2"));
        QVERIFY(!third.source.contains("class Alpha kvit_style_1"));
    }

    void reparentMovesDeclarationsAcrossSubgraphs()
    {
        const QString src = QStringLiteral(
            "flowchart TD\n"
            "  Solo[On its own]\n"
            "  subgraph grp [Group]\n"
            "    A --> B\n"
            "  end\n");
        const auto in = Edits::reparentNode(src, "Solo", "grp");
        QVERIFY2(in.ok, qPrintable(in.error));
        const int subgraphAt = in.source.indexOf("subgraph grp");
        const int endAt = in.source.indexOf("\n  end");
        const int soloAt = in.source.indexOf("Solo[On its own]");
        QVERIFY(soloAt > subgraphAt);
        QVERIFY(soloAt < endAt);
        // Membership through edges refuses (§20.2).
        const auto out = Edits::reparentNode(src, "A", "");
        QVERIFY(!out.ok);
    }

    void reorderSwapsStandaloneDeclarations()
    {
        const QString src = QStringLiteral(
            "flowchart LR\n  First\n  Second\n  First --> Second\n");
        const auto r = Edits::reorderNode(src, "First", 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.source.indexOf("Second") < r.source.indexOf("First"));
        // Arranged mode refuses reordering (§20.4: auto mode only).
        const auto arranged = Edits::reorderNode(
            src + QStringLiteral("%% mermaid-flow:pos First=1,2\n"),
            "First", 1);
        QVERIFY(!arranged.ok);
    }

    void gestureSequencesStayValid()
    {
        // §20.7 fuzz shape: a deterministic pseudo-random gesture sequence;
        // after every step the source reparses without errors.
        QString src = gestureFixture();
        quint32 seed = 0x5eed;
        auto next = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return seed >> 16;
        };
        MermaidParser parser;
        for (int step = 0; step < 60; ++step) {
            const ParseResult pr = parser.parse(src);
            QVERIFY2(!pr.hasErrors(),
                     qPrintable(QStringLiteral("step %1: %2")
                                    .arg(step)
                                    .arg(pr.firstError().message)));
            const QList<Node> &nodes = pr.flowchart.nodes;
            if (nodes.isEmpty())
                break;
            const QString id = nodes.at(next() % nodes.size()).id;
            Edits::Result r;
            switch (next() % 6) {
            case 0:
                r = Edits::setNodeLabel(src, id,
                                        QStringLiteral("L%1").arg(step));
                break;
            case 1:
                r = Edits::setNodeShape(
                    src, id, next() % 2 ? NodeShape::Hexagon
                                        : NodeShape::RoundRect);
                break;
            case 2:
                r = Edits::renameNode(src, id,
                                      QStringLiteral("n_%1").arg(step));
                break;
            case 3:
                r = Edits::quickAddNode(src, id);
                break;
            case 4:
                if (!pr.flowchart.edges.isEmpty())
                    r = Edits::deleteEdge(
                        src, int(next() % pr.flowchart.edges.size()));
                break;
            case 5:
                r = Edits::setNodeStyle(src, id, QColor("#abcdef"), QColor());
                break;
            }
            if (r.ok)
                src = r.source;
        }
        QVERIFY(!parser.parse(src).hasErrors());
    }

    // ---- Phase 5d: sequence reordering (§20.4) ----

    static QString sequenceFixture()
    {
        return QStringLiteral(
            "sequenceDiagram\n"
            "  participant A as Alice\n"
            "  participant B\n"
            "  participant C\n"
            "  %% a comment that must not move\n"
            "  A->>B: first\n"
            "  loop retries\n"
            "    B->>C: second\n"
            "  end\n"
            "  C-->>A: third\n");
    }

    static int nthMessageEvent(const ParseResult &pr, int n)
    {
        int seen = 0;
        for (int i = 0; i < pr.sequence.events.size(); ++i)
            if (pr.sequence.events.at(i).kind == SeqEvent::Message
                && seen++ == n)
                return i;
        return -1;
    }

    void moveMessageSwapsLinesOneForOne()
    {
        const QString src = sequenceFixture();
        MermaidParser parser;
        const ParseResult pr = parser.parse(src);
        // Move "first" down past "second" — across the loop boundary the
        // message lines swap one-for-one; the loop frame stays put.
        const auto r = Edits::moveSequenceMessage(
            src, nthMessageEvent(pr, 0), 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.source.indexOf("B->>C: second")
                < r.source.indexOf("A->>B: first"));
        // Only the two message lines changed places; everything else is
        // byte-identical (§20.7): swapping back restores the original.
        const ParseResult prAfter = parser.parse(r.source);
        const auto back = Edits::moveSequenceMessage(
            r.source, nthMessageEvent(prAfter, 0), 1);
        QVERIFY(back.ok);
        QCOMPARE(back.source, src);
        // The comment did not move.
        QVERIFY(r.source.contains("  %% a comment that must not move\n"));
    }

    void moveMessageRefusesAtTheEdges()
    {
        const QString src = sequenceFixture();
        MermaidParser parser;
        const ParseResult pr = parser.parse(src);
        QVERIFY(!Edits::moveSequenceMessage(src, nthMessageEvent(pr, 0), -1).ok);
        QVERIFY(!Edits::moveSequenceMessage(src, nthMessageEvent(pr, 2), 1).ok);
        // A non-message event index refuses.
        QVERIFY(!Edits::moveSequenceMessage(src, -1, 1).ok);
    }

    void moveParticipantSwapsDeclarations()
    {
        const QString src = sequenceFixture();
        const auto r = Edits::moveSequenceParticipant(src, "A", 1);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.source.indexOf("participant B")
                < r.source.indexOf("participant A as Alice"));
        // Messages are untouched.
        QVERIFY(r.source.contains("  A->>B: first\n"));
        // Moving back restores the original bytes.
        const auto back = Edits::moveSequenceParticipant(r.source, "A", -1);
        QVERIFY(back.ok);
        QCOMPARE(back.source, src);
    }

    void moveParticipantRefusesAutoDeclared()
    {
        const QString src = QStringLiteral(
            "sequenceDiagram\n  participant A\n  A->>Zed: hi\n");
        const auto r = Edits::moveSequenceParticipant(src, "Zed", -1);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains("declaration"));
    }

    void kvitWrittenLinesMatchPluginPattern()
    {
        const auto r = Edits::writeArrangement(
            QStringLiteral("flowchart TD\n  A --> B\n"),
            { { "A", QPointF(-10, 50.6) }, { "B", QPointF(300.2, 200) } });
        QVERIFY(r.ok);
        QString line;
        for (const QString &l : r.source.split(u'\n'))
            if (l.startsWith(QLatin1String("%% mermaid-flow:pos")))
                line = l;
        // The plugin's published entry pattern: id=int,int space-separated.
        static const QRegularExpression pattern(QStringLiteral(
            "^%% mermaid-flow:pos( \\S+=-?\\d+,-?\\d+)+$"));
        QVERIFY2(pattern.match(line).hasMatch(), qPrintable(line));
        QVERIFY(line.contains("A=-10,51"));
    }
};

QTEST_MAIN(TestMermaidEdits)
#include "test_mermaidedits.moc"
