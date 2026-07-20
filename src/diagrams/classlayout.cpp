// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagramlayout.h"

#include <QFont>
#include <QFontMetricsF>
#include <QHash>
#include <QtMath>

#include <cmath>

using namespace Mermaid;

// Class-diagram layout (diagrams-prd.md §8.4): UML compartment boxes placed by
// the shared layered core, relations routed as straight border-to-border lines
// with UML end markers, cardinalities near the endpoints, namespaces as
// compound bounds, and notes below the graph connected by dashed lines.
namespace Diagram {

namespace {

constexpr double kPadX = 12.0;
constexpr double kMinW = 84.0;
constexpr double kRankGap = 64.0;
constexpr double kNodeGap = 44.0;
constexpr double kMargin = 16.0;
constexpr double kCompartmentPad = 5.0;

Marker markerForEnd(ClassRelEnd end)
{
    switch (end) {
    case ClassRelEnd::None: return Marker::None;
    case ClassRelEnd::Extension: return Marker::TriangleOpen;
    case ClassRelEnd::Composition: return Marker::DiamondFilled;
    case ClassRelEnd::Aggregation: return Marker::DiamondOpen;
    case ClassRelEnd::Dependency: return Marker::OpenArrow;
    case ClassRelEnd::Lollipop: return Marker::CircleOpen;
    }
    return Marker::None;
}

// How far the connecting line stops short of a hollow marker's tip so the
// stroke does not show through it.
double markerInset(ClassRelEnd end)
{
    switch (end) {
    case ClassRelEnd::Extension: return 12.0;
    case ClassRelEnd::Aggregation: return 12.0;
    case ClassRelEnd::Lollipop: return 10.0;
    default: return 0.0;
    }
}

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
    return c + d * qMin(tx, ty);
}

struct ResolvedStyle {
    QColor fill, stroke;
    qreal strokeWidth = 0;
    bool bold = false;
};
ResolvedStyle resolveStyle(const ClassNode &c, const ClassAst &ast)
{
    ResolvedStyle r;
    for (const QString &cls : c.cssClasses) {
        const ClassDef d = ast.classDefs.value(cls);
        if (d.hasFill) r.fill = d.fill;
        if (d.hasStroke) r.stroke = d.stroke;
        if (d.strokeWidth > 0) r.strokeWidth = d.strokeWidth;
        if (d.bold) r.bold = true;
    }
    return r;
}

} // namespace

Scene layoutClassDiagram(const ClassAst &ast, const LayoutOptions &opts)
{
    Scene scene;
    scene.accTitle = !ast.accTitle.isEmpty() ? ast.accTitle : ast.title;
    scene.accDescr = ast.accDescr;

    const int N = ast.classes.size();
    scene.summary = QStringLiteral("Mermaid class diagram with %1 class%2 and "
                                   "%3 relationship%4")
                        .arg(N).arg(N == 1 ? "" : "es")
                        .arg(ast.relations.size())
                        .arg(ast.relations.size() == 1 ? "" : "s");
    if (N == 0)
        return scene;

    QFont font(opts.fontFamily);
    font.setPixelSize(opts.fontPixelSize);
    const QFontMetricsF fm(font);
    const double lineH = fm.height();
    const double memberSize = qMax(10, opts.fontPixelSize - 1);

    QHash<QString, int> idx;
    for (int i = 0; i < N; ++i)
        idx.insert(ast.classes.at(i).id, i);

    // ---- measure compartment boxes ----
    struct BoxMetrics {
        double w = 0;
        double titleH = 0;    // annotation + name band
        double attrH = 0;
        double methH = 0;
    };
    QList<BoxMetrics> metrics(N);
    QList<QSizeF> size(N);
    for (int i = 0; i < N; ++i) {
        const ClassNode &c = ast.classes.at(i);
        BoxMetrics m;
        double w = fm.horizontalAdvance(c.label) + 2 * kPadX;
        m.titleH = lineH + 2 * kCompartmentPad;
        if (!c.annotation.isEmpty()) {
            w = qMax(w, fm.horizontalAdvance(
                            QStringLiteral("«%1»").arg(c.annotation))
                            + 2 * kPadX);
            m.titleH += lineH;
        }
        for (const QString &t : c.attributes)
            w = qMax(w, fm.horizontalAdvance(t) + 2 * kPadX);
        for (const QString &t : c.methods)
            w = qMax(w, fm.horizontalAdvance(t) + 2 * kPadX);
        m.attrH = c.attributes.size() * lineH + 2 * kCompartmentPad;
        m.methH = c.methods.size() * lineH + 2 * kCompartmentPad;
        m.w = qMax(kMinW, w);
        metrics[i] = m;
        size[i] = QSizeF(m.w, m.titleH + m.attrH + m.methH);
    }

    // ---- place with the shared layered core ----
    QList<LayeredEdge> ledges;
    ledges.reserve(ast.relations.size());
    for (const ClassRelation &r : ast.relations) {
        const int u = idx.value(r.from, -1);
        const int v = idx.value(r.to, -1);
        if (u < 0 || v < 0)
            continue;
        ledges.append({ u, v, 1 });
    }
    const QList<QPointF> center =
        layeredCenters(size, ledges, ast.direction, kRankGap, kNodeGap);

    // ---- emit class boxes ----
    QList<QRectF> rects(N);
    for (int i = 0; i < N; ++i) {
        const ClassNode &c = ast.classes.at(i);
        const BoxMetrics &m = metrics.at(i);
        const QRectF r(center[i].x() - size[i].width() / 2.0,
                       center[i].y() - size[i].height() / 2.0,
                       size[i].width(), size[i].height());
        rects[i] = r;
        const ResolvedStyle st = resolveStyle(c, ast);

        Shape box;
        box.kind = Shape::Rect;
        box.rect = r;
        box.nodeId = c.id;
        box.srcStart = c.srcSpan.start;
        box.srcLen = c.srcSpan.length;
        box.strokeWidth = st.strokeWidth > 0 ? st.strokeWidth : 1.5;
        if (st.fill.isValid()) box.fillOverride = st.fill;
        if (st.stroke.isValid()) box.strokeOverride = st.stroke;
        scene.shapes.append(box);

        // Compartment separators (always drawn — empty compartments too).
        for (const double yLine : { r.top() + m.titleH,
                                    r.top() + m.titleH + m.attrH }) {
            Path sep;
            QPainterPath pp(QPointF(r.left(), yLine));
            pp.lineTo(r.right(), yLine);
            sep.path = pp;
            sep.strokeRole = Role::NodeStroke;
            sep.strokeWidth = 1.0;
            scene.paths.append(sep);
        }

        double y = r.top() + kCompartmentPad;
        if (!c.annotation.isEmpty()) {
            Text ann;
            ann.text = QStringLiteral("«%1»").arg(c.annotation);
            ann.role = Role::Label;
            ann.italic = true;
            ann.fontSize = memberSize;
            ann.rect = QRectF(r.left(), y, r.width(), lineH);
            scene.texts.append(ann);
            y += lineH;
        }
        Text title;
        title.text = c.label;
        title.role = Role::Label;
        title.bold = true;
        title.fontSize = opts.fontPixelSize;
        title.rect = QRectF(r.left(), y, r.width(), lineH);
        scene.texts.append(title);

        y = r.top() + m.titleH + kCompartmentPad;
        for (const QString &t : c.attributes) {
            Text tx;
            tx.text = t;
            tx.role = Role::Label;
            tx.fontSize = memberSize;
            tx.align = Qt::AlignLeft | Qt::AlignVCenter;
            tx.rect = QRectF(r.left() + kPadX, y, r.width() - 2 * kPadX, lineH);
            scene.texts.append(tx);
            y += lineH;
        }
        y = r.top() + m.titleH + m.attrH + kCompartmentPad;
        for (const QString &t : c.methods) {
            Text tx;
            tx.text = t;
            tx.role = Role::Label;
            tx.fontSize = memberSize;
            tx.align = Qt::AlignLeft | Qt::AlignVCenter;
            tx.rect = QRectF(r.left() + kPadX, y, r.width() - 2 * kPadX, lineH);
            scene.texts.append(tx);
            y += lineH;
        }
    }

    // ---- relations ----
    for (int ri = 0; ri < ast.relations.size(); ++ri) {
        const ClassRelation &rel = ast.relations.at(ri);
        const int u = idx.value(rel.from, -1);
        const int v = idx.value(rel.to, -1);
        if (u < 0 || v < 0)
            continue;
        QPointF a, b;
        QPointF dirA, dirB;
        if (u == v) {
            const QRectF r = rects[u];
            a = QPointF(r.right(), r.center().y() - r.height() * 0.2);
            b = QPointF(r.right(), r.center().y() + r.height() * 0.2);
        } else {
            a = borderPoint(rects[u], rects[v].center());
            b = borderPoint(rects[v], rects[u].center());
        }
        QPointF d = b - a;
        const double len = std::hypot(d.x(), d.y());
        if (len > 0.001)
            d /= len;
        dirA = -d;
        dirB = d;

        Path p;
        p.edgeIndex = ri;
        p.srcStart = rel.srcSpan.start;
        p.srcLen = rel.srcSpan.length;
        p.penStyle = rel.dotted ? Qt::DashLine : Qt::SolidLine;
        p.strokeWidth = 1.4;
        p.startMarker = markerForEnd(rel.fromEnd);
        p.endMarker = markerForEnd(rel.toEnd);
        const QPointF lineStart = a + d * markerInset(rel.fromEnd);
        const QPointF lineEnd = b - d * markerInset(rel.toEnd);
        if (u == v) {
            const QRectF r = rects[u];
            const double bulge = qMax(28.0, r.width() * 0.35);
            QPainterPath pp(a);
            pp.cubicTo(a + QPointF(bulge, -bulge * 0.3),
                       b + QPointF(bulge, bulge * 0.3), b);
            p.path = pp;
            p.startDir = QPointF(-1, 0);
            p.endDir = QPointF(-1, 0);
        } else {
            QPainterPath pp(lineStart);
            pp.lineTo(lineEnd);
            p.path = pp;
            p.startDir = dirA;
            p.endDir = dirB;
        }
        p.startPoint = a;
        p.endPoint = b;
        scene.paths.append(p);

        const double cardSize = qMax(9, opts.fontPixelSize - 3);
        if (!rel.fromCard.isEmpty()) {
            Text t;
            t.text = rel.fromCard;
            t.role = Role::EdgeLabel;
            t.fontSize = cardSize;
            t.hasBackground = true;
            const QPointF pos = a + d * 16.0;
            const double w = fm.horizontalAdvance(rel.fromCard) + 6;
            t.rect = QRectF(pos.x() - w / 2, pos.y() - lineH / 2 - 6, w, lineH);
            scene.texts.append(t);
        }
        if (!rel.toCard.isEmpty()) {
            Text t;
            t.text = rel.toCard;
            t.role = Role::EdgeLabel;
            t.fontSize = cardSize;
            t.hasBackground = true;
            const QPointF pos = b - d * 16.0;
            const double w = fm.horizontalAdvance(rel.toCard) + 6;
            t.rect = QRectF(pos.x() - w / 2, pos.y() - lineH / 2 - 6, w, lineH);
            scene.texts.append(t);
        }
        if (!rel.label.isEmpty()) {
            Text t;
            t.text = rel.label;
            t.role = Role::EdgeLabel;
            t.fontSize = qMax(10, opts.fontPixelSize - 1);
            t.hasBackground = true;
            const QPointF mid = (a + b) / 2.0;
            const double w = fm.horizontalAdvance(rel.label) + 8;
            t.rect = QRectF(mid.x() - w / 2, mid.y() - lineH / 2, w, lineH);
            scene.texts.append(t);
        }
    }

    // ---- namespaces as compound bounds ----
    for (const ClassNamespace &ns : ast.namespaces) {
        QRectF box;
        bool any = false;
        for (const QString &id : ns.classIds) {
            const int i = idx.value(id, -1);
            if (i < 0)
                continue;
            box = any ? box.united(rects[i]) : rects[i];
            any = true;
        }
        if (!any)
            continue;
        box.adjust(-14, -14 - lineH, 14, 14);
        Group g;
        g.rect = box;
        g.title = ns.name;
        scene.groups.append(g);
    }

    // ---- notes: below the graph, dashed line to their class ----
    if (!ast.notes.isEmpty()) {
        double bottom = 0;
        for (const QRectF &r : rects)
            bottom = qMax(bottom, r.bottom());
        double x = 0;
        const double noteY = bottom + 42;
        for (const ClassNote &note : ast.notes) {
            const QStringList ls = labelLines(note.text);
            double tw = 0;
            for (const QString &l : ls)
                tw = qMax(tw, fm.horizontalAdvance(l));
            const double w = qMin(tw, 280.0) + 20;
            const double h = ls.size() * lineH + 12;
            const QRectF r(x, noteY, w, h);
            Shape s;
            s.kind = Shape::Rect;
            s.rect = r;
            s.fillRole = Role::NoteFill;
            s.strokeRole = Role::NoteStroke;
            s.strokeWidth = 1.0;
            scene.shapes.append(s);
            Text t;
            t.text = note.text;
            t.role = Role::Label;
            t.fontSize = memberSize;
            t.rect = r;
            scene.texts.append(t);
            const int target = idx.value(note.forClass, -1);
            if (target >= 0) {
                Path link;
                const QPointF from(r.center().x(), r.top());
                const QPointF to = borderPoint(rects[target], from);
                QPainterPath pp(from);
                pp.lineTo(to);
                link.path = pp;
                link.penStyle = Qt::DashLine;
                link.strokeRole = Role::NoteStroke;
                link.strokeWidth = 1.0;
                link.startPoint = from;
                link.endPoint = to;
                scene.paths.append(link);
            }
            x += w + 24;
        }
    }

    finalizeSceneBounds(scene, kMargin);
    return scene;
}

} // namespace Diagram
