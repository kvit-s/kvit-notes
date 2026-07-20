// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>
#include <QTemporaryDir>
#include <QImage>

#include "diagrams/diagramcanvas.h"
#include "diagrams/diagramlayout.h"
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
};

QTEST_MAIN(TestDiagramLayout)
#include "test_diagramlayout.moc"
