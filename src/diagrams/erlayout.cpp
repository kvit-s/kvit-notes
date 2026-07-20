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

// ER-diagram layout (diagrams-prd.md §8.4): entity tables (title band plus
// type/name/keys/comment columns) placed by the shared layered core;
// relationships drawn border-to-border with crow's-foot end markers, dashed
// when non-identifying, labeled at the midpoint.
namespace Diagram {

namespace {

constexpr double kRankGap = 70.0;
constexpr double kNodeGap = 48.0;
constexpr double kMargin = 16.0;
constexpr double kCellPadX = 8.0;

Marker markerForCard(ErCardinality c)
{
    switch (c) {
    case ErCardinality::ZeroOrOne: return Marker::ErZeroOne;
    case ErCardinality::ZeroOrMore: return Marker::ErZeroMany;
    case ErCardinality::OneOrMore: return Marker::ErMany;
    case ErCardinality::OnlyOne: return Marker::ErOne;
    case ErCardinality::MdParent: return Marker::ErOne;
    }
    return Marker::ErOne;
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

} // namespace

Scene layoutErDiagram(const ErAst &ast, const LayoutOptions &opts)
{
    Scene scene;
    scene.accTitle = !ast.accTitle.isEmpty() ? ast.accTitle : ast.title;
    scene.accDescr = ast.accDescr;

    const int N = ast.entities.size();
    scene.summary = QStringLiteral("Mermaid ER diagram with %1 entit%2 and %3 "
                                   "relationship%4")
                        .arg(N).arg(N == 1 ? "y" : "ies")
                        .arg(ast.relationships.size())
                        .arg(ast.relationships.size() == 1 ? "" : "s");
    if (N == 0)
        return scene;

    QFont font(opts.fontFamily);
    font.setPixelSize(opts.fontPixelSize);
    const QFontMetricsF fm(font);
    const double lineH = fm.height();
    const double rowH = lineH + 6;
    const double titleH = lineH + 12;
    const double cellSize = qMax(10, opts.fontPixelSize - 1);

    QHash<QString, int> idx;
    for (int i = 0; i < N; ++i)
        idx.insert(ast.entities.at(i).id, i);

    // ---- measure entity tables ----
    struct Cols {
        double type = 0, name = 0, keys = 0, comment = 0;
        bool hasKeys = false, hasComment = false;
    };
    QList<Cols> cols(N);
    QList<QSizeF> size(N);
    for (int i = 0; i < N; ++i) {
        const ErEntity &e = ast.entities.at(i);
        Cols c;
        for (const ErAttribute &a : e.attributes) {
            c.type = qMax(c.type, fm.horizontalAdvance(a.type));
            c.name = qMax(c.name, fm.horizontalAdvance(a.name));
            if (!a.keys.isEmpty()) {
                c.hasKeys = true;
                c.keys = qMax(c.keys, fm.horizontalAdvance(
                                          a.keys.join(QLatin1String(","))));
            }
            if (!a.comment.isEmpty()) {
                c.hasComment = true;
                c.comment = qMax(c.comment, fm.horizontalAdvance(a.comment));
            }
        }
        double w = c.type + c.name + 4 * kCellPadX;
        if (c.hasKeys)
            w += c.keys + 2 * kCellPadX;
        if (c.hasComment)
            w += c.comment + 2 * kCellPadX;
        w = qMax(w, fm.horizontalAdvance(e.label) + 2 * kCellPadX);
        w = qMax(w, 90.0);
        const double h = titleH + e.attributes.size() * rowH;
        cols[i] = c;
        size[i] = QSizeF(w, h);
    }

    // ---- place with the shared layered core ----
    QList<LayeredEdge> ledges;
    for (const ErRelationship &r : ast.relationships) {
        const int u = idx.value(r.from, -1);
        const int v = idx.value(r.to, -1);
        if (u < 0 || v < 0)
            continue;
        ledges.append({ u, v, 1 });
    }
    const QList<QPointF> center =
        layeredCenters(size, ledges, ast.direction, kRankGap, kNodeGap);

    // ---- emit entity tables ----
    QList<QRectF> rects(N);
    for (int i = 0; i < N; ++i) {
        const ErEntity &e = ast.entities.at(i);
        const Cols &c = cols.at(i);
        const QRectF r(center[i].x() - size[i].width() / 2.0,
                       center[i].y() - size[i].height() / 2.0,
                       size[i].width(), size[i].height());
        rects[i] = r;

        QColor fillOverride, strokeOverride;
        for (const QString &cls : e.cssClasses) {
            const ClassDef d = ast.classDefs.value(cls);
            if (d.hasFill) fillOverride = d.fill;
            if (d.hasStroke) strokeOverride = d.stroke;
        }

        Shape box;
        box.kind = Shape::Rect;
        box.rect = r;
        box.nodeId = e.id;
        box.srcStart = e.srcSpan.start;
        box.srcLen = e.srcSpan.length;
        if (fillOverride.isValid()) box.fillOverride = fillOverride;
        if (strokeOverride.isValid()) box.strokeOverride = strokeOverride;
        scene.shapes.append(box);

        Text title;
        title.text = e.label;
        title.role = Role::Label;
        title.bold = true;
        title.fontSize = opts.fontPixelSize;
        title.rect = QRectF(r.left(), r.top(), r.width(), titleH);
        scene.texts.append(title);

        if (e.attributes.isEmpty())
            continue;

        // Title separator and row grid.
        auto hline = [&](double y) {
            Path sep;
            QPainterPath pp(QPointF(r.left(), y));
            pp.lineTo(r.right(), y);
            sep.path = pp;
            sep.strokeRole = Role::NodeStroke;
            sep.strokeWidth = 0.8;
            scene.paths.append(sep);
        };
        hline(r.top() + titleH);
        for (int row = 1; row < e.attributes.size(); ++row)
            hline(r.top() + titleH + row * rowH);

        // Column separators.
        double colX = r.left() + c.type + 2 * kCellPadX;
        QList<double> colStarts{ r.left(), colX };
        {
            Path sep;
            QPainterPath pp(QPointF(colX, r.top() + titleH));
            pp.lineTo(colX, r.bottom());
            sep.path = pp;
            sep.strokeRole = Role::NodeStroke;
            sep.strokeWidth = 0.8;
            scene.paths.append(sep);
        }
        if (c.hasKeys) {
            colX += c.name + 2 * kCellPadX;
            colStarts << colX;
            Path sep;
            QPainterPath pp(QPointF(colX, r.top() + titleH));
            pp.lineTo(colX, r.bottom());
            sep.path = pp;
            sep.strokeRole = Role::NodeStroke;
            sep.strokeWidth = 0.8;
            scene.paths.append(sep);
        }
        if (c.hasComment) {
            colX += (c.hasKeys ? c.keys : c.name) + 2 * kCellPadX;
            if (!c.hasKeys)
                colStarts << colX;
            else
                colStarts << colX;
            Path sep;
            QPainterPath pp(QPointF(colX, r.top() + titleH));
            pp.lineTo(colX, r.bottom());
            sep.path = pp;
            sep.strokeRole = Role::NodeStroke;
            sep.strokeWidth = 0.8;
            scene.paths.append(sep);
        }

        for (int row = 0; row < e.attributes.size(); ++row) {
            const ErAttribute &a = e.attributes.at(row);
            const double y = r.top() + titleH + row * rowH;
            auto cell = [&](double x, double w, const QString &text,
                            bool italic = false) {
                if (text.isEmpty())
                    return;
                Text t;
                t.text = text;
                t.role = Role::Label;
                t.fontSize = cellSize;
                t.italic = italic;
                t.align = Qt::AlignLeft | Qt::AlignVCenter;
                t.rect = QRectF(x + kCellPadX, y, w, rowH);
                scene.texts.append(t);
            };
            int col = 0;
            cell(colStarts.at(col++), c.type, a.type);
            cell(colStarts.at(col++), c.name, a.name);
            if (c.hasKeys)
                cell(colStarts.at(col++), c.keys,
                     a.keys.join(QLatin1String(",")));
            if (c.hasComment && col < colStarts.size())
                cell(colStarts.at(col), c.comment, a.comment, true);
        }
    }

    // ---- relationships ----
    for (int ri = 0; ri < ast.relationships.size(); ++ri) {
        const ErRelationship &rel = ast.relationships.at(ri);
        const int u = idx.value(rel.from, -1);
        const int v = idx.value(rel.to, -1);
        if (u < 0 || v < 0)
            continue;
        QPointF a, b;
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

        Path p;
        p.edgeIndex = ri;
        p.srcStart = rel.srcSpan.start;
        p.srcLen = rel.srcSpan.length;
        p.penStyle = rel.identifying ? Qt::SolidLine : Qt::DashLine;
        p.strokeWidth = 1.4;
        p.startMarker = markerForCard(rel.fromCard);
        p.endMarker = markerForCard(rel.toCard);
        // Crow's-foot markers are hollow: the line stops short of them.
        const QPointF lineStart = a + d * 16.0;
        const QPointF lineEnd = b - d * 16.0;
        if (u == v) {
            const QRectF r = rects[u];
            const double bulge = qMax(28.0, r.width() * 0.35);
            QPainterPath pp(a);
            pp.cubicTo(a + QPointF(bulge, -bulge * 0.3),
                       b + QPointF(bulge, bulge * 0.3), b);
            p.path = pp;
            p.startDir = QPointF(-1, 0);
            p.endDir = QPointF(-1, 0);
        } else if (len > 40.0) {
            QPainterPath pp(lineStart);
            pp.lineTo(lineEnd);
            p.path = pp;
            p.startDir = -d;
            p.endDir = d;
        } else {
            QPainterPath pp(a);
            pp.lineTo(b);
            p.path = pp;
            p.startDir = -d;
            p.endDir = d;
        }
        p.startPoint = a;
        p.endPoint = b;
        scene.paths.append(p);

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

    finalizeSceneBounds(scene, kMargin);
    return scene;
}

} // namespace Diagram
