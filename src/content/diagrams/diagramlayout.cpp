// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagramlayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>

#include <algorithm>

using namespace Mermaid;

namespace Diagram {

namespace {

constexpr double kPadX = 14.0;
constexpr double kPadY = 9.0;
constexpr double kMinW = 46.0;
constexpr double kMinH = 30.0;
constexpr double kRankGap = 58.0;   // gap between ranks along the main axis
constexpr double kNodeGap = 30.0;   // gap between siblings along the cross axis
constexpr double kMargin = 16.0;
constexpr double kSubgraphPad = 16.0;

Shape::Kind sceneShape(NodeShape s)
{
    switch (s) {
    case NodeShape::Rect: return Shape::Rect;
    case NodeShape::RoundRect: return Shape::RoundRect;
    case NodeShape::Stadium: return Shape::Stadium;
    case NodeShape::Subroutine: return Shape::Subroutine;
    case NodeShape::Cylinder: return Shape::Cylinder;
    case NodeShape::Circle: return Shape::Circle;
    case NodeShape::DoubleCircle: return Shape::DoubleCircle;
    case NodeShape::Ellipse: return Shape::Ellipse;
    case NodeShape::Rhombus: return Shape::Rhombus;
    case NodeShape::Hexagon: return Shape::Hexagon;
    case NodeShape::Parallelogram: return Shape::Parallelogram;
    case NodeShape::ParallelogramAlt: return Shape::ParallelogramAlt;
    case NodeShape::Trapezoid: return Shape::Trapezoid;
    case NodeShape::TrapezoidAlt: return Shape::TrapezoidAlt;
    case NodeShape::Odd: return Shape::Odd;
    }
    return Shape::Rect;
}

// Merge the classDef/style settings a node inherits, last write winning.
struct ResolvedStyle {
    QColor fill, stroke;
    qreal strokeWidth = 0;
    bool dashed = false, bold = false;
};
ResolvedStyle resolveStyle(const Node &n, const FlowchartAst &ast)
{
    ResolvedStyle r;
    for (const QString &cls : n.classes) {
        const ClassDef d = ast.classDefs.value(cls);
        if (d.hasFill) r.fill = d.fill;
        if (d.hasStroke) r.stroke = d.stroke;
        if (d.strokeWidth > 0) r.strokeWidth = d.strokeWidth;
        if (d.dashed) r.dashed = true;
        if (d.bold) r.bold = true;
    }
    return r;
}

// Clip the segment from the center of rect `r` toward `toward` to the rect's
// border; a robust approximation for every shape's outline.
QPointF borderPoint(const QRectF &r, const QPointF &toward)
{
    const QPointF c = r.center();
    QPointF d = toward - c;
    if (qFuzzyIsNull(d.x()) && qFuzzyIsNull(d.y()))
        return c;
    const double hw = r.width() / 2.0;
    const double hh = r.height() / 2.0;
    const double tx = qFuzzyIsNull(d.x()) ? 1e9 : hw / qAbs(d.x());
    const double ty = qFuzzyIsNull(d.y()) ? 1e9 : hh / qAbs(d.y());
    const double t = qMin(tx, ty);
    return c + d * t;
}

} // namespace

QStringList labelLines(const QString &label)
{
    QString s = label;
    static const QRegularExpression br(QStringLiteral("<br\\s*/?>"),
                                       QRegularExpression::CaseInsensitiveOption);
    s.replace(br, QStringLiteral("\n"));
    s.replace(QLatin1String("\\n"), QStringLiteral("\n"));
    return s.split(u'\n');
}

QList<QPointF> layeredCenters(const QList<QSizeF> &size,
                              const QList<LayeredEdge> &edges,
                              Direction direction,
                              double rankGap, double nodeGap)
{
    const int N = size.size();
    QList<QPointF> center(N);
    if (N == 0)
        return center;

    // ---- adjacency, cycle breaking, ranking ----
    QList<QList<QPair<int, int>>> adj(N);   // (target, minLen), forward candidates
    for (const LayeredEdge &e : edges) {
        if (e.u < 0 || e.v < 0 || e.u >= N || e.v >= N || e.u == e.v)
            continue;
        adj[e.u].append({ e.v, qMax(1, e.minLen) });
    }

    // DFS classifies edges to a gray (on-stack) node as back edges (a cycle);
    // those are excluded from ranking so longest-path terminates.
    QList<int> color(N, 0);   // 0 white, 1 gray, 2 black
    QSet<QPair<int, int>> backEdges;
    QList<int> stack;
    QList<int> iterPos(N, 0);
    for (int s = 0; s < N; ++s) {
        if (color[s] != 0)
            continue;
        stack.append(s);
        color[s] = 1;
        while (!stack.isEmpty()) {
            const int u = stack.last();
            if (iterPos[u] < adj[u].size()) {
                const int v = adj[u].at(iterPos[u]).first;
                ++iterPos[u];
                if (color[v] == 1)
                    backEdges.insert({ u, v });
                else if (color[v] == 0) {
                    color[v] = 1;
                    stack.append(v);
                }
            } else {
                color[u] = 2;
                stack.removeLast();
            }
        }
    }

    // Longest-path ranking over the forward DAG (Kahn).
    QList<int> rank(N, 0);
    QList<int> indeg(N, 0);
    QList<QList<QPair<int, int>>> fadj(N);
    for (int u = 0; u < N; ++u)
        for (const auto &pr : adj[u])
            if (!backEdges.contains({ u, pr.first })) {
                fadj[u].append(pr);
                ++indeg[pr.first];
            }
    QList<int> queue;
    for (int i = 0; i < N; ++i)
        if (indeg[i] == 0)
            queue.append(i);
    int qh = 0;
    while (qh < queue.size()) {
        const int u = queue.at(qh++);
        for (const auto &pr : fadj[u]) {
            rank[pr.first] = qMax(rank[pr.first], rank[u] + pr.second);
            if (--indeg[pr.first] == 0)
                queue.append(pr.first);
        }
    }

    int maxRank = 0;
    for (int i = 0; i < N; ++i)
        maxRank = qMax(maxRank, rank[i]);

    // ---- order within ranks (barycenter crossing reduction) ----
    QList<QList<int>> layers(maxRank + 1);
    for (int i = 0; i < N; ++i)
        layers[rank[i]].append(i);   // encounter order within a layer

    QList<QList<int>> undirected(N);
    for (const LayeredEdge &e : edges) {
        if (e.u < 0 || e.v < 0 || e.u >= N || e.v >= N || e.u == e.v)
            continue;
        undirected[e.u].append(e.v);
        undirected[e.v].append(e.u);
    }
    QList<int> posInLayer(N, 0);
    auto refreshPos = [&]() {
        for (const QList<int> &layer : layers)
            for (int k = 0; k < layer.size(); ++k)
                posInLayer[layer.at(k)] = k;
    };
    refreshPos();
    auto barycenter = [&](int node) {
        const QList<int> &nb = undirected[node];
        if (nb.isEmpty())
            return double(posInLayer[node]);
        double sum = 0;
        for (int m : nb)
            sum += posInLayer[m];
        return sum / nb.size();
    };
    for (int sweep = 0; sweep < 4; ++sweep) {
        for (int l = 0; l <= maxRank; ++l) {
            std::stable_sort(layers[l].begin(), layers[l].end(),
                             [&](int a, int b) {
                                 const double ba = barycenter(a);
                                 const double bb = barycenter(b);
                                 if (!qFuzzyCompare(ba, bb))
                                     return ba < bb;
                                 return a < b;   // deterministic tie-break
                             });
        }
        refreshPos();
    }

    // ---- coordinate assignment ----
    const bool horizontal = direction == Direction::LR
                            || direction == Direction::RL;
    auto mainSize = [&](int i) {
        return horizontal ? size[i].width() : size[i].height();
    };
    auto crossSize = [&](int i) {
        return horizontal ? size[i].height() : size[i].width();
    };

    // Main-axis band positions.
    QList<double> layerMainStart(maxRank + 1, 0.0);
    QList<double> layerMainExtent(maxRank + 1, 0.0);
    double mainCursor = 0;
    for (int l = 0; l <= maxRank; ++l) {
        double extent = 0;
        for (int i : layers[l])
            extent = qMax(extent, mainSize(i));
        layerMainStart[l] = mainCursor;
        layerMainExtent[l] = extent;
        mainCursor += extent + rankGap;
    }
    const double totalMain = mainCursor > 0 ? mainCursor - rankGap : 0;

    // Cross-axis positions, each layer centered against the widest.
    QList<double> layerCrossTotal(maxRank + 1, 0.0);
    double maxCross = 0;
    for (int l = 0; l <= maxRank; ++l) {
        double c = 0;
        for (int k = 0; k < layers[l].size(); ++k) {
            c += crossSize(layers[l].at(k));
            if (k + 1 < layers[l].size())
                c += nodeGap;
        }
        layerCrossTotal[l] = c;
        maxCross = qMax(maxCross, c);
    }

    for (int l = 0; l <= maxRank; ++l) {
        double cross = (maxCross - layerCrossTotal[l]) / 2.0;
        for (int i : layers[l]) {
            const double crossCenter = cross + crossSize(i) / 2.0;
            const double mainCenter = layerMainStart[l] + layerMainExtent[l] / 2.0;
            double x, y;
            switch (direction) {
            case Direction::TB: x = crossCenter; y = mainCenter; break;
            case Direction::BT: x = crossCenter; y = totalMain - mainCenter; break;
            case Direction::LR: x = mainCenter; y = crossCenter; break;
            case Direction::RL: x = totalMain - mainCenter; y = crossCenter; break;
            }
            center[i] = QPointF(x, y);
            cross += crossSize(i) + nodeGap;
        }
    }
    return center;
}

void finalizeSceneBounds(Scene &scene, qreal margin)
{
    QRectF bounds;
    auto grow = [&](const QRectF &r) {
        bounds = bounds.isNull() ? r : bounds.united(r);
    };
    for (const Group &g : scene.groups) grow(g.rect);
    for (const Shape &s : scene.shapes) grow(s.rect);
    for (const Text &t : scene.texts) grow(t.rect);
    for (const Path &p : scene.paths) grow(p.path.boundingRect());

    const QPointF shift(margin - bounds.left(), margin - bounds.top());
    for (Group &g : scene.groups) g.rect.translate(shift);
    for (Shape &s : scene.shapes) s.rect.translate(shift);
    for (Text &t : scene.texts) t.rect.translate(shift);
    for (Path &p : scene.paths) {
        p.path.translate(shift);
        p.startPoint += shift;
        p.endPoint += shift;
    }
    scene.bounds = QRectF(0, 0, bounds.width() + 2 * margin,
                          bounds.height() + 2 * margin);
}

Scene layoutFlowchart(const FlowchartAst &ast, const LayoutOptions &opts)
{
    Scene scene;
    scene.accTitle = ast.accTitle;
    scene.accDescr = ast.accDescr;

    const int N = ast.nodes.size();
    scene.summary = QStringLiteral("Mermaid flowchart with %1 node%2 and %3 "
                                   "connection%4")
                        .arg(N).arg(N == 1 ? "" : "s")
                        .arg(ast.edges.size())
                        .arg(ast.edges.size() == 1 ? "" : "s");
    if (N == 0)
        return scene;

    QFont font(opts.fontFamily);
    font.setPixelSize(opts.fontPixelSize);
    const QFontMetricsF fm(font);
    const double lineH = fm.height();

    QHash<QString, int> idx;
    for (int i = 0; i < N; ++i)
        idx.insert(ast.nodes.at(i).id, i);

    // ---- measure node boxes ----
    QList<QSizeF> size;
    QList<QStringList> lines;
    size.reserve(N);
    lines.reserve(N);
    for (int i = 0; i < N; ++i) {
        const QStringList ls = labelLines(ast.nodes.at(i).label);
        double tw = 0;
        for (const QString &l : ls)
            tw = qMax(tw, fm.horizontalAdvance(l));
        const double th = qMax<double>(1, ls.size()) * lineH;
        double w = qMax(kMinW, tw + 2 * kPadX);
        double h = qMax(kMinH, th + 2 * kPadY);
        switch (ast.nodes.at(i).shape) {
        case NodeShape::Circle: {
            const double d = qMax(w, h) * 1.15;
            w = h = d;
            break;
        }
        case NodeShape::DoubleCircle: {
            const double d = qMax(w, h) * 1.3;
            w = h = d;
            break;
        }
        case NodeShape::Ellipse:
            w *= 1.35; h *= 1.25;
            break;
        case NodeShape::Rhombus:
            w *= 1.4; h *= 1.55;
            break;
        case NodeShape::Hexagon:
            w += h;
            break;
        case NodeShape::Cylinder:
            h += 14;
            break;
        case NodeShape::Subroutine:
            w += 16;
            break;
        case NodeShape::Parallelogram:
        case NodeShape::ParallelogramAlt:
        case NodeShape::Trapezoid:
        case NodeShape::TrapezoidAlt:
            w += h * 0.6;
            break;
        case NodeShape::Odd:
            w += h * 0.4;
            break;
        default:
            break;
        }
        size.append(QSizeF(w, h));
        lines.append(ls);
    }

    // ---- coordinates: arranged mode or the shared layered core ----
    const bool horizontal = opts.direction == Direction::LR
                            || opts.direction == Direction::RL;
    QList<QPointF> center;
    bool arranged = false;
    if (ast.hasPosLine) {
        QHash<QString, QPointF> pinned;
        for (const PosEntry &pe : ast.posEntries)
            if (idx.contains(pe.id))
                pinned.insert(pe.id, QPointF(pe.x, pe.y));
        if (!pinned.isEmpty()) {
            arranged = true;
            center = QList<QPointF>(N);
            double maxMain = 0;
            for (int i = 0; i < N; ++i) {
                const auto it = pinned.constFind(ast.nodes.at(i).id);
                if (it == pinned.constEnd())
                    continue;
                center[i] = it.value();
                maxMain = qMax(maxMain, horizontal
                                   ? it.value().x() + size[i].width() / 2
                                   : it.value().y() + size[i].height() / 2);
            }
            // Nodes without an entry (typed in later) are placed beyond the
            // existing content along the flow direction, in source order,
            // without moving pinned nodes.
            double crossCursor = 0;
            for (int i = 0; i < N; ++i) {
                if (pinned.contains(ast.nodes.at(i).id))
                    continue;
                if (horizontal) {
                    center[i] = QPointF(maxMain + kRankGap
                                            + size[i].width() / 2,
                                        crossCursor + size[i].height() / 2);
                    crossCursor += size[i].height() + kNodeGap;
                } else {
                    center[i] = QPointF(crossCursor + size[i].width() / 2,
                                        maxMain + kRankGap
                                            + size[i].height() / 2);
                    crossCursor += size[i].width() + kNodeGap;
                }
            }
        }
    }
    if (!arranged) {
        QList<LayeredEdge> ledges;
        ledges.reserve(ast.edges.size());
        for (const Edge &e : ast.edges) {
            const int u = idx.value(e.from, -1);
            const int v = idx.value(e.to, -1);
            if (u < 0 || v < 0)
                continue;
            ledges.append({ u, v, qMax(1, e.minLen) });
        }
        center = layeredCenters(size, ledges, opts.direction, kRankGap,
                                kNodeGap);
    }

    // ---- emit node shapes and labels ----
    QList<QRectF> rects(N);
    for (int i = 0; i < N; ++i) {
        QRectF r(center[i].x() - size[i].width() / 2.0,
                 center[i].y() - size[i].height() / 2.0,
                 size[i].width(), size[i].height());
        rects[i] = r;
        const ResolvedStyle st = resolveStyle(ast.nodes.at(i), ast);
        Shape sh;
        sh.kind = sceneShape(ast.nodes.at(i).shape);
        sh.rect = r;
        sh.nodeId = ast.nodes.at(i).id;
        {
            const Node &n = ast.nodes.at(i);
            if (n.idSpan.valid()) {
                sh.srcStart = n.idSpan.start;
                sh.srcLen = n.idSpan.length;
                // Include an adjacent bracket construct in the span.
                if (n.shapeSpan.valid() && n.shapeSpan.start >= n.idSpan.end()
                    && n.shapeSpan.start <= n.idSpan.end() + 4)
                    sh.srcLen = n.shapeSpan.end() - n.idSpan.start;
            }
        }
        sh.strokeWidth = st.strokeWidth > 0 ? st.strokeWidth : 1.5;
        if (st.fill.isValid()) sh.fillOverride = st.fill;
        if (st.stroke.isValid()) sh.strokeOverride = st.stroke;
        scene.shapes.append(sh);

        Text tx;
        tx.rect = r;
        tx.text = lines[i].join(u'\n');
        tx.role = Role::Label;
        tx.fontSize = opts.fontPixelSize;
        tx.bold = st.bold;
        scene.texts.append(tx);
    }

    // ---- route edges ----
    // Arranged mode bows parallel edges apart; count pair multiplicity.
    QHash<QPair<int, int>, int> pairCount;
    QHash<QPair<int, int>, int> pairSeen;
    if (arranged) {
        for (const Edge &e : ast.edges) {
            const int u = idx.value(e.from, -1);
            const int v = idx.value(e.to, -1);
            if (u < 0 || v < 0 || u == v || e.invisible)
                continue;
            ++pairCount[qMakePair(qMin(u, v), qMax(u, v))];
        }
    }
    for (int ei = 0; ei < ast.edges.size(); ++ei) {
        const Edge &e = ast.edges.at(ei);
        const int u = idx.value(e.from, -1);
        const int v = idx.value(e.to, -1);
        if (u < 0 || v < 0)
            continue;
        if (e.invisible)
            continue;   // `~~~`: ranks like an edge, draws nothing
        Path p;
        p.edgeIndex = ei;
        if (e.opSpan.valid()) {
            p.srcStart = e.opSpan.start;
            p.srcLen = e.opSpan.length;
        }
        p.strokeWidth = e.stroke == EdgeStroke::Thick ? 3.0 : 1.5;
        p.penStyle = e.stroke == EdgeStroke::Dotted ? Qt::DotLine : Qt::SolidLine;
        p.startMarker = e.arrowStart ? Marker::Arrow : Marker::None;
        p.endMarker = e.arrowEnd ? Marker::Arrow : Marker::None;

        QPointF a, b;
        if (u == v) {
            // Self-loop: a small arc off the right side of the node.
            const QRectF r = rects[u];
            a = QPointF(r.right(), r.center().y() - r.height() * 0.2);
            b = QPointF(r.right(), r.center().y() + r.height() * 0.2);
            QPainterPath path(a);
            const double bulge = qMax(24.0, r.width() * 0.4);
            path.cubicTo(a + QPointF(bulge, -bulge * 0.3),
                         b + QPointF(bulge, bulge * 0.3), b);
            p.path = path;
            p.startPoint = a;
            p.endPoint = b;
            p.endDir = QPointF(-1, 0);
            p.startDir = QPointF(-1, 0);
        } else if (arranged) {
            // Cubic Béziers leave and enter perpendicular to the node
            // sides along the flow axis; parallel edges bow apart.
            const QPair<int, int> key(qMin(u, v), qMax(u, v));
            const int n = pairCount.value(key, 1);
            const int k = pairSeen[key]++;
            const double bow = (k - (n - 1) / 2.0) * 16.0;
            QPointF flow, perp;
            if (horizontal) {
                const bool forward = center[v].x() >= center[u].x();
                flow = QPointF(forward ? 1 : -1, 0);
                perp = QPointF(0, 1);
                a = QPointF(forward ? rects[u].right() : rects[u].left(),
                            rects[u].center().y() + bow * 0.4);
                b = QPointF(forward ? rects[v].left() : rects[v].right(),
                            rects[v].center().y() + bow * 0.4);
            } else {
                const bool forward = center[v].y() >= center[u].y();
                flow = QPointF(0, forward ? 1 : -1);
                perp = QPointF(1, 0);
                a = QPointF(rects[u].center().x() + bow * 0.4,
                            forward ? rects[u].bottom() : rects[u].top());
                b = QPointF(rects[v].center().x() + bow * 0.4,
                            forward ? rects[v].top() : rects[v].bottom());
            }
            const double dist = qMax(24.0, std::hypot((b - a).x(),
                                                      (b - a).y()));
            const QPointF c1 = a + flow * (dist * 0.4) + perp * bow;
            const QPointF c2 = b - flow * (dist * 0.4) + perp * bow;
            QPainterPath path(a);
            path.cubicTo(c1, c2, b);
            p.path = path;
            p.startPoint = a;
            p.endPoint = b;
            QPointF endDir = b - c2;
            const double el = std::hypot(endDir.x(), endDir.y());
            if (el > 0.001)
                endDir /= el;
            QPointF startDir = a - c1;
            const double sl = std::hypot(startDir.x(), startDir.y());
            if (sl > 0.001)
                startDir /= sl;
            p.endDir = endDir;
            p.startDir = startDir;
        } else {
            a = borderPoint(rects[u], center[v]);
            b = borderPoint(rects[v], center[u]);
            QPainterPath path(a);
            path.lineTo(b);
            p.path = path;
            p.startPoint = a;
            p.endPoint = b;
            QPointF dir = b - a;
            const double len = std::hypot(dir.x(), dir.y());
            if (len > 0.001)
                dir /= len;
            p.endDir = dir;
            p.startDir = -dir;
        }
        scene.paths.append(p);

        if (!e.label.isEmpty()) {
            const QPointF mid = (a + b) / 2.0;
            const double lw = fm.horizontalAdvance(e.label) + 8;
            Text tx;
            tx.rect = QRectF(mid.x() - lw / 2.0, mid.y() - lineH / 2.0,
                             lw, lineH);
            tx.text = e.label;
            tx.role = Role::EdgeLabel;
            tx.fontSize = qMax(10, opts.fontPixelSize - 1);
            tx.hasBackground = true;
            scene.texts.append(tx);
        }
    }

    // ---- subgraph compound bounds ----
    for (const Subgraph &sg : ast.subgraphs) {
        QRectF box;
        bool any = false;
        for (const QString &id : sg.nodeIds) {
            const int i = idx.value(id, -1);
            if (i < 0)
                continue;
            box = any ? box.united(rects[i]) : rects[i];
            any = true;
        }
        if (!any)
            continue;
        box.adjust(-kSubgraphPad, -kSubgraphPad - lineH, kSubgraphPad, kSubgraphPad);
        Group g;
        g.rect = box;
        g.title = sg.title;
        scene.groups.append(g);
    }

    // ---- normalize to a non-negative origin and compute bounds ----
    finalizeSceneBounds(scene, kMargin);
    return scene;
}

} // namespace Diagram
