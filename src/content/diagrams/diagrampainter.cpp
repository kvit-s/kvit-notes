// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagrampainter.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QFont>
#include <QtMath>

namespace Diagram {

namespace {

QColor roleColor(Role role, const SceneColors &c)
{
    switch (role) {
    case Role::Background: return c.background;
    case Role::NodeFill: return c.nodeFill;
    case Role::NodeStroke: return c.nodeStroke;
    case Role::EdgeStroke: return c.edge;
    case Role::Label: return c.label;
    case Role::EdgeLabel: return c.edgeLabel;
    case Role::SubgraphFill: return c.subgraphFill;
    case Role::SubgraphStroke: return c.subgraphStroke;
    case Role::NoteFill: return c.noteFill;
    case Role::NoteStroke: return c.noteStroke;
    case Role::Activation: return c.activationFill;
    }
    return c.label;
}

QPainterPath shapeOutline(const Shape &s)
{
    QPainterPath path;
    const QRectF r = s.rect;
    const double cx = r.center().x();
    const double cy = r.center().y();
    switch (s.kind) {
    case Shape::Rect:
        path.addRect(r);
        break;
    case Shape::RoundRect:
        path.addRoundedRect(r, 8, 8);
        break;
    case Shape::Stadium:
        path.addRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
        break;
    case Shape::Subroutine:
        path.addRect(r);
        break;
    case Shape::Circle:
    case Shape::DoubleCircle:
        path.addEllipse(r);
        break;
    case Shape::Ellipse:
        path.addEllipse(r);
        break;
    case Shape::Rhombus: {
        QPolygonF poly;
        poly << QPointF(cx, r.top()) << QPointF(r.right(), cy)
             << QPointF(cx, r.bottom()) << QPointF(r.left(), cy);
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case Shape::Hexagon: {
        const double inset = qMin(r.width() * 0.25, r.height());
        QPolygonF poly;
        poly << QPointF(r.left(), cy)
             << QPointF(r.left() + inset, r.top())
             << QPointF(r.right() - inset, r.top())
             << QPointF(r.right(), cy)
             << QPointF(r.right() - inset, r.bottom())
             << QPointF(r.left() + inset, r.bottom());
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case Shape::Parallelogram: {
        const double skew = r.height() * 0.3;
        QPolygonF poly;
        poly << QPointF(r.left() + skew, r.top()) << QPointF(r.right(), r.top())
             << QPointF(r.right() - skew, r.bottom()) << QPointF(r.left(), r.bottom());
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case Shape::ParallelogramAlt: {
        const double skew = r.height() * 0.3;
        QPolygonF poly;
        poly << QPointF(r.left(), r.top()) << QPointF(r.right() - skew, r.top())
             << QPointF(r.right(), r.bottom()) << QPointF(r.left() + skew, r.bottom());
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case Shape::Trapezoid: {
        const double inset = r.height() * 0.3;
        QPolygonF poly;
        poly << QPointF(r.left() + inset, r.top())
             << QPointF(r.right() - inset, r.top())
             << QPointF(r.right(), r.bottom()) << QPointF(r.left(), r.bottom());
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case Shape::TrapezoidAlt: {
        const double inset = r.height() * 0.3;
        QPolygonF poly;
        poly << QPointF(r.left(), r.top()) << QPointF(r.right(), r.top())
             << QPointF(r.right() - inset, r.bottom())
             << QPointF(r.left() + inset, r.bottom());
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case Shape::Odd: {
        // The `>text]` flag shape: a rectangle whose left edge notches in to a
        // point at mid-height.
        const double inset = qMin(r.height() * 0.4, r.width() * 0.3);
        QPolygonF poly;
        poly << QPointF(r.left() + inset, r.top()) << QPointF(r.right(), r.top())
             << QPointF(r.right(), r.bottom())
             << QPointF(r.left() + inset, r.bottom())
             << QPointF(r.left(), cy);
        path.addPolygon(poly);
        path.closeSubpath();
        break;
    }
    case Shape::Cylinder: {
        const double ry = qMin(r.height() * 0.14, 12.0);
        path.moveTo(r.left(), r.top() + ry);
        path.arcTo(QRectF(r.left(), r.top(), r.width(), 2 * ry), 180, -180);
        path.lineTo(r.right(), r.bottom() - ry);
        path.arcTo(QRectF(r.left(), r.bottom() - 2 * ry, r.width(), 2 * ry), 0, -180);
        path.closeSubpath();
        break;
    }
    case Shape::Actor:
        break;   // drawn as a stick figure, not a filled outline
    }
    return path;
}

// A sequence-diagram actor: stick figure filling the given bounding box.
void drawActorFigure(QPainter *p, const QRectF &r, const QColor &stroke)
{
    p->save();
    p->setPen(QPen(stroke, 1.6, Qt::SolidLine, Qt::RoundCap));
    p->setBrush(Qt::NoBrush);
    const double cx = r.center().x();
    const double headR = r.height() * 0.18;
    const QPointF headC(cx, r.top() + headR + 1);
    p->drawEllipse(headC, headR, headR);
    const double neckY = headC.y() + headR;
    const double hipY = r.top() + r.height() * 0.68;
    p->drawLine(QPointF(cx, neckY), QPointF(cx, hipY));                 // body
    const double armY = neckY + (hipY - neckY) * 0.3;
    p->drawLine(QPointF(cx - r.width() * 0.32, armY),
                QPointF(cx + r.width() * 0.32, armY));                  // arms
    p->drawLine(QPointF(cx, hipY),
                QPointF(cx - r.width() * 0.28, r.bottom()));            // legs
    p->drawLine(QPointF(cx, hipY), QPointF(cx + r.width() * 0.28, r.bottom()));
    p->restore();
}

void drawMarker(QPainter *p, Marker kind, const QPointF &tip, const QPointF &dir,
                const QColor &color)
{
    if (kind == Marker::None)
        return;
    const double len = std::hypot(dir.x(), dir.y());
    if (len < 0.001)
        return;
    const QPointF d = dir / len;
    const QPointF perp(-d.y(), d.x());
    p->save();
    switch (kind) {
    case Marker::Arrow: {
        const double size = 9.0;
        const double half = 3.6;
        const QPointF base = tip - d * size;
        QPolygonF head;
        head << tip << (base + perp * half) << (base - perp * half);
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawPolygon(head);
        break;
    }
    case Marker::OpenArrow: {
        const double size = 9.0;
        const double half = 4.2;
        const QPointF base = tip - d * size;
        p->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
        p->setBrush(Qt::NoBrush);
        p->drawLine(tip, base + perp * half);
        p->drawLine(tip, base - perp * half);
        break;
    }
    case Marker::Cross: {
        // The `-x` head: an X drawn just short of the endpoint.
        const double size = 4.2;
        const QPointF c = tip - d * 6.0;
        p->setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(c + (d + perp) * size, c - (d + perp) * size);
        p->drawLine(c + (d - perp) * size, c - (d - perp) * size);
        break;
    }
    case Marker::Dot: {
        // The `-)` async point: a small filled circle at the endpoint.
        p->setPen(Qt::NoPen);
        p->setBrush(color);
        p->drawEllipse(tip - d * 3.5, 3.5, 3.5);
        break;
    }
    case Marker::TriangleOpen: {
        // UML extension: hollow triangle. The path stops at its base.
        const double size = 13.0;
        const double half = 6.5;
        const QPointF base = tip - d * size;
        QPolygonF head;
        head << tip << (base + perp * half) << (base - perp * half);
        p->setPen(QPen(color, 1.4));
        p->setBrush(Qt::NoBrush);
        p->drawPolygon(head);
        break;
    }
    case Marker::DiamondFilled:
    case Marker::DiamondOpen: {
        const double size = 6.0;
        const QPointF mid = tip - d * size;
        QPolygonF diamond;
        diamond << tip << (mid + perp * 4.0) << (tip - d * 2 * size)
                << (mid - perp * 4.0);
        p->setPen(QPen(color, 1.4));
        p->setBrush(kind == Marker::DiamondFilled ? QBrush(color)
                                                  : QBrush(Qt::NoBrush));
        p->drawPolygon(diamond);
        break;
    }
    case Marker::CircleOpen: {
        p->setPen(QPen(color, 1.4));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(tip - d * 5.0, 5.0, 5.0);
        break;
    }
    case Marker::ErOne:
    case Marker::ErZeroOne:
    case Marker::ErMany:
    case Marker::ErZeroMany: {
        // Crow's-foot notation. `tip` sits on the entity border; strokes are
        // drawn back along the line direction.
        p->setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap));
        p->setBrush(Qt::NoBrush);
        auto bar = [&](double dist) {
            const QPointF c = tip - d * dist;
            p->drawLine(c + perp * 5.0, c - perp * 5.0);
        };
        auto circle = [&](double dist) {
            p->drawEllipse(tip - d * dist, 4.0, 4.0);
        };
        auto crow = [&]() {
            const QPointF base = tip - d * 11.0;
            p->drawLine(base, tip + perp * 5.0);
            p->drawLine(base, tip - perp * 5.0);
            p->drawLine(base, tip);
        };
        switch (kind) {
        case Marker::ErOne:
            bar(6.0);
            bar(11.0);
            break;
        case Marker::ErZeroOne:
            bar(6.0);
            circle(15.0);
            break;
        case Marker::ErMany:
            crow();
            bar(15.0);
            break;
        case Marker::ErZeroMany:
            crow();
            circle(16.0);
            break;
        default:
            break;
        }
        break;
    }
    case Marker::None:
        break;
    }
    p->restore();
}

} // namespace

void paintScene(QPainter *painter, const Scene &scene, const SceneColors &colors,
                const QString &fontFamily)
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    // Subgraph frames and background bands (behind nodes).
    for (const Group &g : scene.groups) {
        painter->setPen(g.noBorder ? QPen(Qt::NoPen)
                                   : QPen(colors.subgraphStroke, 1.0, Qt::DashLine));
        painter->setBrush(g.fillOverride.isValid() ? g.fillOverride
                                                   : colors.subgraphFill);
        painter->drawRoundedRect(g.rect, 6, 6);
        if (!g.title.isEmpty()) {
            QFont f(fontFamily);
            f.setPixelSize(12);
            f.setBold(true);
            painter->setFont(f);
            painter->setPen(colors.subgraphStroke.darker(140));
            painter->drawText(QRectF(g.rect.left() + 8, g.rect.top() + 2,
                                     g.rect.width() - 16, 16),
                              Qt::AlignLeft | Qt::AlignVCenter, g.title);
        }
    }

    // Edges (under nodes so arrowheads meet borders cleanly).
    for (const Path &e : scene.paths) {
        const QColor stroke = roleColor(e.strokeRole, colors);
        QPen pen(stroke, e.strokeWidth, e.penStyle);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(e.path);
        drawMarker(painter, e.endMarker, e.endPoint, e.endDir, stroke);
        drawMarker(painter, e.startMarker, e.startPoint, e.startDir, stroke);
    }

    // Nodes.
    for (const Shape &s : scene.shapes) {
        const QColor fill = s.fillOverride.isValid()
            ? s.fillOverride : roleColor(s.fillRole, colors);
        const QColor stroke = s.strokeOverride.isValid()
            ? s.strokeOverride : roleColor(s.strokeRole, colors);
        if (s.kind == Shape::Actor) {
            drawActorFigure(painter, s.rect, stroke);
            continue;
        }
        const QPainterPath path = shapeOutline(s);
        painter->setPen(QPen(stroke, s.strokeWidth));
        painter->setBrush(fill);
        painter->drawPath(path);
        // Subroutine: inner vertical bars.
        if (s.kind == Shape::Subroutine) {
            const double inset = 6;
            painter->drawLine(QPointF(s.rect.left() + inset, s.rect.top()),
                              QPointF(s.rect.left() + inset, s.rect.bottom()));
            painter->drawLine(QPointF(s.rect.right() - inset, s.rect.top()),
                              QPointF(s.rect.right() - inset, s.rect.bottom()));
        }
        // Double circle: an inner concentric ring.
        if (s.kind == Shape::DoubleCircle) {
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(s.rect.adjusted(4, 4, -4, -4));
        }
    }

    // Text (node labels + edge labels).
    for (const Text &t : scene.texts) {
        if (t.hasBackground) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(colors.edgeLabelBackground);
            painter->drawRoundedRect(t.rect, 3, 3);
        }
        QFont f(fontFamily);
        f.setPixelSize(int(t.fontSize));
        f.setBold(t.bold);
        f.setItalic(t.italic);
        painter->setFont(f);
        painter->setPen(roleColor(t.role, colors));
        painter->drawText(t.rect, t.align | Qt::TextWordWrap, t.text);
    }
}

} // namespace Diagram
