// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QTemporaryDir>
#include <QImage>

#include "diagrams/diagrampainter.h"
#include <QPainter>
#include <cmath>
#include "diagrams/diagramcanvas.h"
#include "diagrams/diagramlayout.h"
#include "diagrams/diagramtext.h"
#include "diagrams/mermaidparser.h"
#include "diagrams/mermaidrenderer.h"

using namespace Mermaid;
using namespace Diagram;

// Deterministic layered layout: identical source produces identical scene
// primitives, node boxes do not overlap, edge endpoints sit on node borders,
// and the renderer cache/round-trip behave.
class TestDiagramLayout : public QObject
{
    Q_OBJECT

    static FlowchartAst parseFlow(const QString &src)
    {
        MermaidParser p;
        return p.parse(src).flowchart;
    }
    static LayoutOptions opts()
    {
        LayoutOptions o;
        o.fontFamily = QStringLiteral("sans-serif");
        o.fontPixelSize = 14;
        return o;
    }
    // Lay out whichever family the source names, so a fixture can be written
    // exactly as a user would type it.
    static Scene layoutAny(const QString &src)
    {
        MermaidParser p;
        const ParseResult r = p.parse(src);
        LayoutOptions o = opts();
        switch (r.type) {
        case DiagramType::Flowchart:
            o.direction = r.flowchart.direction;
            return layoutFlowchart(r.flowchart, o);
        case DiagramType::Sequence: return layoutSequence(r.sequence, o);
        case DiagramType::Class:    return layoutClassDiagram(r.classDiagram, o);
        case DiagramType::State:    return layoutStateDiagram(r.stateDiagram, o);
        case DiagramType::Er:       return layoutErDiagram(r.er, o);
        default:                    return Scene();
        }
    }

private slots:
    void producesShapesAndPaths()
    {
        const FlowchartAst a = parseFlow(
            "flowchart TD\nA[Start]-->B{Choice}\nB-->C([End])\nB-->D((Stop))");
        const Scene s = layoutFlowchart(a, opts());
        QCOMPARE(s.shapes.size(), 4);
        QCOMPARE(s.paths.size(), 3);
        QVERIFY(s.texts.size() >= 4);   // one label per node (+ any edge labels)
        QVERIFY(s.bounds.width() > 0 && s.bounds.height() > 0);
        // Shape kinds reflect the source.
        for (const Shape &sh : s.shapes) {
            if (sh.nodeId == "B") QCOMPARE(sh.kind, Shape::Rhombus);
            if (sh.nodeId == "C") QCOMPARE(sh.kind, Shape::Stadium);
            if (sh.nodeId == "D") QCOMPARE(sh.kind, Shape::Circle);
        }
    }

    void deterministicScene()
    {
        const QString src =
            "flowchart LR\nA-->B-->C\nA-->C\nC-->D\nB-->D\nZ-->A";
        const Scene s1 = layoutFlowchart(parseFlow(src), opts());
        const Scene s2 = layoutFlowchart(parseFlow(src), opts());
        QCOMPARE(s1.shapes.size(), s2.shapes.size());
        QCOMPARE(s1.paths.size(), s2.paths.size());
        for (int i = 0; i < s1.shapes.size(); ++i) {
            QCOMPARE(s1.shapes.at(i).nodeId, s2.shapes.at(i).nodeId);
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.x(), s2.shapes.at(i).rect.x()));
            QVERIFY(qFuzzyCompare(s1.shapes.at(i).rect.y(), s2.shapes.at(i).rect.y()));
        }
        for (int i = 0; i < s1.paths.size(); ++i) {
            QVERIFY(qFuzzyCompare(s1.paths.at(i).endPoint.x(),
                                  s2.paths.at(i).endPoint.x()));
            QVERIFY(qFuzzyCompare(s1.paths.at(i).endPoint.y(),
                                  s2.paths.at(i).endPoint.y()));
        }
    }

    void nodesDoNotOverlap()
    {
        const Scene s = layoutFlowchart(
            parseFlow("flowchart TB\nA-->B\nA-->C\nB-->D\nC-->D"), opts());
        for (int i = 0; i < s.shapes.size(); ++i)
            for (int j = i + 1; j < s.shapes.size(); ++j) {
                QRectF a = s.shapes.at(i).rect;
                QRectF b = s.shapes.at(j).rect;
                // Shrink slightly so touching borders are not counted.
                a.adjust(1, 1, -1, -1);
                b.adjust(1, 1, -1, -1);
                QVERIFY2(!a.intersects(b),
                         qPrintable(QStringLiteral("overlap %1/%2")
                                        .arg(s.shapes.at(i).nodeId,
                                             s.shapes.at(j).nodeId)));
            }
    }

    void edgeEndpointsOnBorders()
    {
        const Scene s = layoutFlowchart(parseFlow("flowchart TB\nA-->B"), opts());
        QCOMPARE(s.shapes.size(), 2);
        QCOMPARE(s.paths.size(), 1);
        const QRectF ra = s.shapes.at(0).rect;
        const QRectF rb = s.shapes.at(1).rect;
        const Path &p = s.paths.first();
        // The endpoint lies on B's border (within a small tolerance).
        auto onBorder = [](const QRectF &r, const QPointF &pt) {
            const double eps = 1.5;
            const bool inX = pt.x() >= r.left() - eps && pt.x() <= r.right() + eps;
            const bool inY = pt.y() >= r.top() - eps && pt.y() <= r.bottom() + eps;
            const bool nearEdge =
                qAbs(pt.x() - r.left()) < eps || qAbs(pt.x() - r.right()) < eps
                || qAbs(pt.y() - r.top()) < eps || qAbs(pt.y() - r.bottom()) < eps;
            return inX && inY && nearEdge;
        };
        QVERIFY(onBorder(rb, p.endPoint));
        QVERIFY(onBorder(ra, p.startPoint));
        QCOMPARE(p.endMarker, Marker::Arrow);
    }

    void subgraphProducesGroup()
    {
        const Scene s = layoutFlowchart(parseFlow(
            "flowchart TB\nsubgraph g [Box]\nA-->B\nend\nB-->C"), opts());
        QCOMPARE(s.groups.size(), 1);
        QCOMPARE(s.groups.first().title, QString("Box"));
        // The group encloses its members A and B.
        QRectF a, b;
        for (const Shape &sh : s.shapes) {
            if (sh.nodeId == "A") a = sh.rect;
            if (sh.nodeId == "B") b = sh.rect;
        }
        QVERIFY(s.groups.first().rect.contains(a.center()));
        QVERIFY(s.groups.first().rect.contains(b.center()));
    }

    void cyclesDoNotHang()
    {
        // A 3-cycle must rank and lay out without hanging.
        const Scene s = layoutFlowchart(
            parseFlow("flowchart LR\nA-->B\nB-->C\nC-->A"), opts());
        QCOMPARE(s.shapes.size(), 3);
        QCOMPARE(s.paths.size(), 3);
    }

    void rendererFlagsUnsupportedFamily()
    {
        clearCache();
        const RenderResult r = render(
            QStringLiteral("gantt\n  title Deferred family"), opts());
        QVERIFY(!r.valid);
        QVERIFY(r.unsupportedFamily);
        QVERIFY(r.hasError);
    }

    void rendererCacheHit()
    {
        clearCache();
        const QString src = "flowchart LR\nA-->B-->C";
        const RenderResult r1 = render(src, opts());
        const int afterFirst = cacheCount();
        const RenderResult r2 = render(src, opts());
        QVERIFY(r1.valid && r2.valid);
        QVERIFY(afterFirst >= 1);
        QCOMPARE(r1.scene.shapes.size(), r2.scene.shapes.size());
    }

    void canvasSelectionAndLinking()
    {
        // Hit-testing and linking through the canvas API.
        const QString src = QStringLiteral("flowchart LR\n  A[Start] --> B");
        DiagramCanvas canvas;
        canvas.setSource(src);
        QTRY_VERIFY(canvas.hasScene());
        QVERIFY(canvas.sceneCurrent());

        // Locate node centers from the identically keyed render.
        const RenderResult rr = render(src, opts());
        QPointF centerA, centerB;
        for (const Shape &s : rr.scene.shapes) {
            if (s.nodeId == QLatin1String("A")) centerA = s.rect.center();
            if (s.nodeId == QLatin1String("B")) centerB = s.rect.center();
        }
        QCOMPARE(canvas.nodeAt(centerA.x(), centerA.y()), QString("A"));
        QCOMPARE(canvas.nodeAt(centerB.x(), centerB.y()), QString("B"));
        // The edge midpoint hits edge 0; empty space hits nothing.
        const QPointF mid = (centerA + centerB) / 2.0;
        QCOMPARE(canvas.edgeAt(mid.x(), mid.y()), 0);
        QCOMPARE(canvas.nodeAt(2, 2), QString());

        canvas.setSelectedNodeId(QStringLiteral("A"));
        QVERIFY(canvas.hasSelection());
        QVERIFY(canvas.selectionRect().isValid());
        const int off = canvas.sourceOffsetForSelection();
        QVERIFY(off >= 0);
        QCOMPARE(src.mid(off, 1), QString("A"));
        QCOMPARE(canvas.sourceLineForOffset(off), 2);

        // Keyboard cycling wraps deterministically.
        QCOMPARE(canvas.cycleNode(1), QString("B"));
        QCOMPARE(canvas.cycleNode(1), QString("A"));
        canvas.clearSelection();
        QVERIFY(!canvas.hasSelection());

        // Point→offset linking and offset→element highlighting.
        QCOMPARE(canvas.sourceOffsetAt(centerA.x(), centerA.y()), off);
        canvas.highlightSourceOffset(off);   // must not crash or select
        QVERIFY(!canvas.hasSelection());

        // Revision gating: invalid new source keeps the last-good scene but
        // reports the scene as stale.
        canvas.setSource(QStringLiteral("flowchart LR\n  A --> C"));
        QVERIFY(!canvas.sceneCurrent());
        QVERIFY(canvas.textDiagram().isEmpty()); // pending source is stale too
        QTRY_VERIFY(canvas.sceneCurrent());
        QVERIFY(!canvas.textDiagram().isEmpty());
        canvas.setSource(QStringLiteral("gantt\n  oops"));
        QTRY_VERIFY(canvas.hasError());
        QVERIFY(canvas.hasScene());
        QVERIFY(!canvas.sceneCurrent());
        QVERIFY(canvas.textDiagram().isEmpty());
    }

    // A pooled delegate reused for a different block resets the canvas: the
    // previous block's scene must never survive as the new block's
    // "last valid source" (the reported bug: converting a char-diagram fence
    // to mermaid showed an unrelated diagram from a reused delegate).
    void resetSceneDropsLastGoodAcrossReuse()
    {
        DiagramCanvas canvas;
        canvas.setSource(QStringLiteral("flowchart LR\nA[Start] --> B[End]"));
        QTRY_VERIFY(canvas.hasScene());

        // Reuse for a block whose source is not valid Mermaid: without the
        // reset, the old scene shows with the last-good banner.
        canvas.setSource(QStringLiteral("┌──┐\n│ok│\n└──┘"));
        canvas.resetScene();
        QVERIFY(!canvas.hasScene());
        QVERIFY(canvas.textDiagram().isEmpty());
        QTRY_VERIFY(canvas.hasError());
        QVERIFY(!canvas.hasScene());
        QVERIFY(!canvas.sceneCurrent());

        // Reset with an unchanged valid source re-renders rather than
        // stranding the canvas (setSource short-circuits equal sources).
        canvas.setSource(QStringLiteral("flowchart LR\nA[Start] --> B[End]"));
        QTRY_VERIFY(canvas.hasScene());
        canvas.resetScene();
        QVERIFY(!canvas.hasScene());
        QTRY_VERIFY(canvas.hasScene());
        QVERIFY(canvas.sceneCurrent());
    }

    void savePngWritesImage()
    {
        DiagramCanvas canvas;
        canvas.setSource(QStringLiteral("flowchart LR\nA[Start] --> B[End]"));
        QTRY_VERIFY(canvas.hasScene());
        QTemporaryDir dir;
        const QString path = dir.filePath("diagram.png");
        QVERIFY(canvas.savePng(path, 2.0));
        QImage img(path);
        QVERIFY(!img.isNull());
        QVERIFY(img.width() > 100);
        // An empty path or sceneless canvas refuses.
        QVERIFY(!canvas.savePng(QString(), 2.0));
    }

    void largeFlowchartWithinBudget()
    {
        // A 100-node / ~150-edge flowchart lays out well under budget.
        QString src = QStringLiteral("flowchart TB\n");
        for (int i = 0; i < 100; ++i)
            src += QStringLiteral("n%1[Node %1]\n").arg(i);
        for (int i = 0; i < 99; ++i)
            src += QStringLiteral("n%1 --> n%2\n").arg(i).arg(i + 1);
        for (int i = 0; i < 50; ++i)
            src += QStringLiteral("n%1 --> n%2\n").arg(i).arg((i + 7) % 100);
        const FlowchartAst a = parseFlow(src);
        QCOMPARE(a.nodes.size(), 100);
        QElapsedTimer t;
        t.start();
        const Scene s = layoutFlowchart(a, opts());
        const qint64 ms = t.elapsed();
        QCOMPARE(s.shapes.size(), 100);
        qInfo() << "100-node layout:" << ms << "ms";
        QVERIFY2(ms < 1500, "layout unexpectedly slow");
    }

    // ---- edge labels and the nodes they must keep clear of ----

    // An edge whose ends are more than one rank apart passes over the ranks
    // between them. Both its route and its label have to keep out of whatever
    // occupies those ranks, or the label prints over a box and the line runs
    // through it.
    void rankSkippingEdgeClearsTheNodeBetween()
    {
        // B->C spans two ranks (B, then D, then C), so it passes over D.
        const Scene s = layoutAny(QStringLiteral(
            "flowchart LR\n"
            "A([Start]) --> B{Vault set?}\n"
            "B -- yes --> C[Open collection]\n"
            "B -- no --> D[(Seed Welcome)]\n"
            "D --> C\n"
            "C --> E[/Render note/]"));

        QRectF d;
        for (const Shape &sh : s.shapes)
            if (sh.nodeId == QLatin1String("D"))
                d = sh.rect;
        QVERIFY(!d.isNull());

        for (const Text &t : s.texts) {
            if (t.role != Role::EdgeLabel)
                continue;
            QVERIFY2(!t.rect.intersects(d),
                     qPrintable(QStringLiteral("edge label \"%1\" printed over "
                                               "node D").arg(t.text)));
        }
        // The route itself must not cross D either: it is the same defect
        // seen as a line through a box rather than as text over one.
        for (const Path &p : s.paths)
            QVERIFY2(!p.path.intersects(d.adjusted(2, 2, -2, -2)),
                     "an edge is routed through node D");
    }

    // The five sources of docs/qa-checklist.md item 6, one per family. The
    // checklist asks a human to confirm every edge label sits in open space;
    // this is that check, run on every build.
    void checklistDiagramsKeepLabelsOffBoxes()
    {
        struct Case { const char *name; const char *src; };
        const QList<Case> cases = {
            { "flowchart",
              "flowchart LR\n"
              "    A([Start]) --> B{Vault set?}\n"
              "    B -- yes --> C[Open collection]\n"
              "    B -- no --> D[(Seed Welcome)]\n"
              "    D --> C\n"
              "    C --> E[/Render note/]\n" },
            { "sequence",
              "sequenceDiagram\n"
              "    autonumber\n"
              "    participant U as User\n"
              "    participant E as Editor\n"
              "    participant S as Serializer\n"
              "    U->>E: type a heading\n"
              "    activate E\n"
              "    E->>S: block changed\n"
              "    S-->>E: markdown\n"
              "    deactivate E\n"
              "    Note over S: debounced save\n" },
            { "class",
              "classDiagram\n"
              "    class Block {\n"
              "        +BlockType type\n"
              "        +QString content\n"
              "        +render() void\n"
              "    }\n"
              "    class CodeBlock {\n"
              "        +QString language\n"
              "    }\n"
              "    Block <|-- CodeBlock\n"
              "    Block \"1\" o-- \"0..*\" Attribute\n" },
            { "state",
              "stateDiagram-v2\n"
              "    [*] --> Idle\n"
              "    Idle --> Editing: keypress\n"
              "    Editing --> Saving: debounce\n"
              "    Saving --> Idle: written\n"
              "    Saving --> Conflict: file changed\n"
              "    Conflict --> Idle: resolved\n"
              "    Conflict --> [*]: discarded\n" },
            { "er",
              "erDiagram\n"
              "    COLLECTION ||--o{ NOTE : contains\n"
              "    NOTE ||--o{ BLOCK : \"is made of\"\n"
              "    NOTE }o--o{ NOTE : links-to\n"
              "    NOTE {\n"
              "        string title PK\n"
              "        date created\n"
              "        string tags \"comma separated\"\n"
              "    }\n" },
        };

        for (const Case &c : cases) {
            const Scene s = layoutAny(QString::fromUtf8(c.src));
            QVERIFY2(!s.shapes.isEmpty(), c.name);

            // The ground an arrowhead, diamond or crow's foot covers: from
            // the endpoint it sits on, back along the line it came in on.
            QList<QRectF> markers;
            auto addMarker = [&](Marker kind, const QPointF &tip,
                                 const QPointF &outward) {
                const double reach = markerLength(kind);
                if (reach <= 0)
                    return;
                const double len = std::hypot(outward.x(), outward.y());
                if (len < 0.001)
                    return;
                // Widened a little across the line, since every head has some
                // width; a label brushing that edge is judged by the shrunk
                // rect below rather than by this box.
                const QPointF into = -outward / len;
                markers.append(QRectF(tip, tip + into * reach)
                                   .normalized()
                                   .adjusted(-2, -2, 2, 2));
            };
            for (const Path &p : s.paths) {
                addMarker(p.startMarker, p.startPoint, p.startDir);
                addMarker(p.endMarker, p.endPoint, p.endDir);
            }

            QList<QRectF> seen;
            for (const Text &t : s.texts) {
                if (t.role != Role::EdgeLabel || t.text.isEmpty())
                    continue;
                for (const Shape &sh : s.shapes) {
                    // Two pixels of contact with a border is contact rather
                    // than an overlap.
                    if (t.rect.intersects(sh.rect.adjusted(2, 2, -2, -2)))
                        QFAIL(qPrintable(
                            QStringLiteral("%1: label \"%2\" over node %3")
                                .arg(QLatin1String(c.name), t.text, sh.nodeId)));
                }
                // A text rect carries the font's leading above and below the
                // glyphs, so a label set just clear of a line brushes the
                // head's box without a reader seeing them touch. Judge by the
                // ink rather than by the box.
                const QRectF ink = t.rect.adjusted(2, 3, -2, -3);
                for (const QRectF &m : markers)
                    if (ink.intersects(m))
                        QFAIL(qPrintable(
                            QStringLiteral("%1: label \"%2\" over a line end")
                                .arg(QLatin1String(c.name), t.text)));
                for (const QRectF &other : seen)
                    if (t.rect.intersects(other))
                        QFAIL(qPrintable(
                            QStringLiteral("%1: label \"%2\" over another label")
                                .arg(QLatin1String(c.name), t.text)));
                seen.append(t.rect);
            }
        }
    }

    // ---- mathematics in labels (diagram-math.md) ----

    // Only a whole `$$…$$` label is an expression. Everything else stays
    // text, which is what keeps every diagram written before this rendering
    // exactly as it did.
    void mathLabelRecognizesWholeLabelsOnly()
    {
        QCOMPARE(mathLabel(QStringLiteral("$$x^2$$")), QStringLiteral("x^2"));
        // Line-break markup is normalized before the delimiters are read.
        QCOMPARE(mathLabel(QStringLiteral("$$x^2$$<br>")), QStringLiteral("x^2"));
        QCOMPARE(mathLabel(QStringLiteral("  $$\\frac{a}{b}$$  ")),
                 QStringLiteral("\\frac{a}{b}"));

        // A single dollar is ordinary text: currency and shell variables in
        // existing diagrams must not start typesetting.
        QVERIFY(mathLabel(QStringLiteral("costs $5 and $6")).isEmpty());
        QVERIFY(mathLabel(QStringLiteral("$PATH")).isEmpty());
        // Mixed labels are out of scope, so they stay text.
        QVERIFY(mathLabel(QStringLiteral("Step $$x^2$$ done")).isEmpty());
        // Two expressions are not one label's worth of mathematics.
        QVERIFY(mathLabel(QStringLiteral("$$a$$ $$b$$")).isEmpty());
        QVERIFY(mathLabel(QStringLiteral("$$$$")).isEmpty());
        QVERIFY(mathLabel(QStringLiteral("plain")).isEmpty());
    }

    // An expression that does not parse falls back to its source: no size,
    // so layout measures and the painter draws the label's text.
    void unparseableMathFallsBackToItsSource()
    {
        QFont f(QStringLiteral("sans-serif"));
        f.setPixelSize(14);
        QVERIFY(mathLabelSize(mathLabel(QStringLiteral("$$x^2$$")), f).isValid());
        // An unmatched brace and a bare alignment ampersand are the two
        // shapes MicroTeX rejects outright.
        QVERIFY(!mathLabelSize(QStringLiteral("}"), f).isValid());
        QVERIFY(!mathLabelSize(QStringLiteral("a & b"), f).isValid());
        QVERIFY(!mathLabelSize(QString(), f).isValid());

        // A node whose expression does not parse is laid out and painted from
        // its source, so it carries no TeX into the scene.
        const Scene s = layoutFlowchart(parseFlow("flowchart LR\nA[\"$$}$$\"]"),
                                        opts());
        QVERIFY(!s.texts.isEmpty());
        QVERIFY(s.texts.first().tex.isEmpty());
        QCOMPARE(s.texts.first().text, QStringLiteral("$$}$$"));
    }

    void flowchartTypesetsMathNodeAndEdgeLabels()
    {
        const FlowchartAst a = parseFlow(
            "flowchart LR\n"
            "A[\"$$\\frac{a}{b}$$\"] -->|\"$$x^2$$\"| B[plain]\n");
        const Scene s = layoutFlowchart(a, opts());

        const Text *node = nullptr;
        const Text *edge = nullptr;
        const Text *plain = nullptr;
        for (const Text &t : s.texts) {
            if (t.role == Role::EdgeLabel)
                edge = &t;
            else if (t.text == QLatin1String("plain"))
                plain = &t;
            else if (t.text.contains(QLatin1String("frac")))
                node = &t;
        }
        QVERIFY(node && edge && plain);

        // The expression travels to the painter, and the source stays put
        // beside it for the fallback and for accessibility.
        QCOMPARE(node->tex, QStringLiteral("\\frac{a}{b}"));
        QCOMPARE(node->text, QStringLiteral("$$\\frac{a}{b}$$"));
        QCOMPARE(edge->tex, QStringLiteral("x^2"));
        // A plain label carries no expression at all.
        QVERIFY(plain->tex.isEmpty());

        // The node box was sized from the rendered metrics: a two-level
        // fraction is taller than a single line of text.
        const Text *taller = node->rect.height() > plain->rect.height()
            ? node : nullptr;
        QVERIFY2(taller, "a fraction should make its node taller than a word");
    }

    void sequenceTypesetsMessageParticipantAndNote()
    {
        MermaidParser p;
        const SequenceAst a = p.parse(
            "sequenceDiagram\n"
            "participant A as \"$$\\alpha$$\"\n"
            "participant B\n"
            "A->>B: $$\\int_0^1 f$$\n"
            "Note right of B: $$e^{i\\pi}+1=0$$\n").sequence;
        const Scene s = layoutSequence(a, opts());

        QStringList found;
        for (const Text &t : s.texts) {
            if (!t.tex.isEmpty())
                found << t.tex;
        }
        // The participant label is emitted at the top and the bottom of its
        // lifeline, so its expression appears twice.
        QVERIFY2(found.count(QStringLiteral("\\alpha")) >= 1,
                 "a participant name is typeset");
        QVERIFY2(found.contains(QStringLiteral("\\int_0^1 f")),
                 "a message label is typeset");
        QVERIFY2(found.contains(QStringLiteral("e^{i\\pi}+1=0")),
                 "a note is typeset");

        // The diagram title is not one of the kinds Mermaid supports, so it
        // stays text even when it looks like an expression.
        const SequenceAst titled = p.parse(
            "sequenceDiagram\ntitle $$x^2$$\nA->>B: hi\n").sequence;
        for (const Text &t : layoutSequence(titled, opts()).texts) {
            if (t.text == QLatin1String("$$x^2$$"))
                QVERIFY2(t.tex.isEmpty(), "a title is not typeset");
        }
    }
    // The painter takes the math branch: typesetting a label produces a
    // different image from drawing its source, and it puts ink where the
    // label is. Painting is the one seam every target shares, so this covers
    // the on-screen canvas and the raster that PDF export embeds.
    void painterTypesetsRatherThanDrawingTheSource()
    {
        Scene s = layoutFlowchart(
            parseFlow("flowchart LR\nA[\"$$\\frac{a}{b}$$\"]"), opts());
        QVERIFY(!s.texts.isEmpty());
        QVERIFY(!s.texts.first().tex.isEmpty());

        SceneColors colors;
        colors.background = Qt::white;
        colors.nodeFill = Qt::white;
        colors.nodeStroke = Qt::white;
        colors.edge = Qt::white;
        colors.label = Qt::black;
        colors.edgeLabel = Qt::black;
        colors.edgeLabelBackground = Qt::white;
        colors.subgraphFill = Qt::white;
        colors.subgraphStroke = Qt::white;

        const auto render = [&](const Scene &scene) {
            QImage img(int(std::ceil(scene.bounds.width())) + 4,
                       int(std::ceil(scene.bounds.height())) + 4,
                       QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            QPainter p(&img);
            paintScene(&p, scene, colors, QStringLiteral("sans-serif"));
            p.end();
            return img;
        };
        const auto inkCount = [](const QImage &img) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qGray(img.pixel(x, y)) < 200)
                        ++n;
            return n;
        };

        const QImage typeset = render(s);
        QVERIFY2(inkCount(typeset) > 50, "the formula was drawn");

        // The same scene with the expression removed falls back to drawing
        // the label's source, which is a visibly different image.
        Scene asText = s;
        for (Text &t : asText.texts)
            t.tex.clear();
        const QImage drawn = render(asText);
        QVERIFY2(inkCount(drawn) > 50, "the source was drawn");
        QVERIFY2(typeset != drawn,
                 "typesetting must not produce the same pixels as drawing $$...$$");
    }
};

QTEST_MAIN(TestDiagramLayout)
#include "test_diagramlayout.moc"
