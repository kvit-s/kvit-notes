// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "textdiagram.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <climits>
#include <cmath>

#include "textcanvas.h"

namespace Diagram {
namespace {

// Cell metrics: one cell per character advance horizontally, one per text
// line vertically (~2:1 aspect), chosen against the layout's 14px font.
// Fixed rather than measured so the output is deterministic everywhere.
constexpr qreal kCellW = 9.0;
constexpr qreal kCellH = 18.0;
// Global margin so routes can bend outside the leftmost/topmost node.
constexpr int kColOffset = 2;
constexpr int kRowOffset = 1;

int colOf(qreal x) { return int(std::lround(x / kCellW)) + kColOffset; }
int rowOf(qreal y) { return int(std::lround(y / kCellH)) + kRowOffset; }

struct Node {
    const Shape *shape = nullptr;
    QStringList lines;   // label lines, top to bottom
    int row = 0, col = 0, w = 3, h = 3; // cell rect (col..col+w-1)
    int right() const { return col + w - 1; }
    int bottom() const { return row + h - 1; }
};

// The dominant axis direction of a unit-ish vector.
TextCanvas::Direction directionOf(QPointF v)
{
    if (std::abs(v.x()) >= std::abs(v.y()))
        return v.x() >= 0 ? TextCanvas::Right : TextCanvas::Left;
    return v.y() >= 0 ? TextCanvas::Down : TextCanvas::Up;
}

bool isVertical(TextCanvas::Direction d)
{
    return d == TextCanvas::Up || d == TextCanvas::Down;
}

// Deliberate marker degradation (pre-launch-plan.md §2.2): UML heads
// become △ ◇ o x, crow's feet become < > ^ v by travel direction; the
// plain Arrow keeps the directional ▲▼◄► the repair vocabulary knows.
QChar markerGlyph(Marker marker, TextCanvas::Direction dir)
{
    switch (marker) {
    case Marker::None: return QChar();
    case Marker::Arrow:
    case Marker::OpenArrow:
        switch (dir) {
        case TextCanvas::Up: return QChar(u'▲');
        case TextCanvas::Down: return QChar(u'▼');
        case TextCanvas::Left: return QChar(u'◄');
        case TextCanvas::Right: return QChar(u'►');
        }
        return QChar();
    case Marker::Cross: return QChar(u'x');
    case Marker::Dot:
    case Marker::CircleOpen: return QChar(u'o');
    case Marker::TriangleOpen: return QChar(u'△');
    case Marker::DiamondFilled:
    case Marker::DiamondOpen: return QChar(u'◇');
    case Marker::ErMany:
    case Marker::ErZeroMany:
        switch (dir) {
        case TextCanvas::Up: return QChar(u'^');
        case TextCanvas::Down: return QChar(u'v');
        case TextCanvas::Left: return QChar(u'<');
        case TextCanvas::Right: return QChar(u'>');
        }
        return QChar();
    case Marker::ErOne:
    case Marker::ErZeroOne:
        return isVertical(dir) ? QChar(u'─') : QChar(u'|');
    }
    return QChar();
}

// Occupied-segment bookkeeping so parallel edges get distinct channels: a
// horizontal segment is (row, colA, colB), a vertical one (col, rowA, rowB).
struct Channels {
    QHash<int, QList<QPair<int, int>>> horizontal; // row -> spans
    QHash<int, QList<QPair<int, int>>> vertical;   // col -> spans

    static bool overlaps(const QList<QPair<int, int>> &spans, int a, int b)
    {
        const int lo = std::min(a, b), hi = std::max(a, b);
        for (const auto &span : spans) {
            if (lo <= span.second && span.first <= hi)
                return true;
        }
        return false;
    }
    bool hFree(int row, int a, int b) const
    { return !overlaps(horizontal.value(row), a, b); }
    bool vFree(int col, int a, int b) const
    { return !overlaps(vertical.value(col), a, b); }
    void takeH(int row, int a, int b)
    { horizontal[row].append({std::min(a, b), std::max(a, b)}); }
    void takeV(int col, int a, int b)
    { vertical[col].append({std::min(a, b), std::max(a, b)}); }
    // Node boxes block both orientations so no channel ever runs through
    // a box; drawn edges only block their own orientation (a crossing
    // renders as ┼, an overlap would be ambiguous).
    void blockRect(int top, int left, int bottom, int right)
    {
        for (int row = top; row <= bottom; ++row)
            takeH(row, left, right);
        for (int col = left; col <= right; ++col)
            takeV(col, top, bottom);
    }
};

struct Builder {
    const Scene &scene;
    TextCanvas canvas;
    QList<Node> nodes;
    QSet<int> nodeTexts;     // indexes into scene.texts consumed as labels
    Channels channels;
    // Per-path drawn geometry — start cell, longest-segment midpoint, end
    // cell — so an edge label (or cardinality) follows its edge even when
    // routing moved it away from the pixel layout (outer-lane detours).
    struct PathDraw {
        QPoint start = QPoint(-1, -1); // (col,row)
        QPoint mid = QPoint(-1, -1);
        QPoint end = QPoint(-1, -1);
        bool valid = false;
    };
    QList<PathDraw> pathDraws;

    explicit Builder(const Scene &s) : scene(s) {}

    // ---- nodes ----------------------------------------------------------

    // A DoubleCircle (and friends) arrives as two concentric shapes; only
    // the outer one becomes a box. Containment alone is not enough — a
    // composite state legitimately contains much smaller children — so
    // the inner shape must also fill most of the outer one.
    bool isConcentricInner(const Shape &shape) const
    {
        const qreal area = shape.rect.width() * shape.rect.height();
        for (const Shape &other : scene.shapes) {
            if (&other == &shape)
                continue;
            const qreal otherArea =
                other.rect.width() * other.rect.height();
            if (otherArea <= area)
                continue;
            const QPointF delta =
                other.rect.center() - shape.rect.center();
            if (other.rect.adjusted(-1, -1, 1, 1).contains(shape.rect)
                && delta.manhattanLength() < 4.0)
                return true;
        }
        return false;
    }

    void collectNodes()
    {
        for (const Shape &shape : scene.shapes) {
            if (isConcentricInner(shape))
                continue;
            Node node;
            node.shape = &shape;

            // Label lines: every text whose center falls inside the shape,
            // top to bottom. (Class compartments contribute several.)
            QList<QPair<qreal, QString>> inside;
            for (int i = 0; i < scene.texts.size(); ++i) {
                const Text &text = scene.texts.at(i);
                if (text.hasBackground)
                    continue; // edge labels place separately
                if (shape.rect.contains(text.rect.center())) {
                    inside.append({text.rect.center().y(), text.text});
                    nodeTexts.insert(i);
                }
            }
            std::stable_sort(inside.begin(), inside.end(),
                             [](const auto &a, const auto &b)
                             { return a.first < b.first; });
            for (const auto &pair : inside) {
                const QStringList split = pair.second.split(QLatin1Char('\n'));
                for (const QString &line : split)
                    node.lines.append(line);
            }
            decorateLabel(&node);

            int maxLine = 0;
            for (const QString &line : node.lines)
                maxLine = std::max(maxLine, int(line.size()));

            node.col = colOf(shape.rect.left());
            node.row = rowOf(shape.rect.top());
            node.w = std::max({int(std::lround(shape.rect.width() / kCellW)),
                               maxLine + 4, 5});
            node.h = std::max({int(std::lround(shape.rect.height() / kCellH)),
                               int(node.lines.size()) + 2, 3});
            nodes.append(node);
        }
        relaxOverlaps();
        for (const Node &node : std::as_const(nodes))
            channels.blockRect(node.row, node.col,
                               node.bottom(), node.right());
    }

    // Shape-specific dress-up: a decision shows as < label >, state
    // start/end circles as (*) / ((*)) — every shape still draws as a box
    // rather than failing (pre-launch-plan.md §2.2).
    void decorateLabel(Node *node)
    {
        const Shape::Kind kind = node->shape->kind;
        if (kind == Shape::Rhombus) {
            for (QString &line : node->lines)
                line = QStringLiteral("< ") + line + QStringLiteral(" >");
            if (node->lines.isEmpty())
                node->lines.append(QStringLiteral("< >"));
        } else if (kind == Shape::Circle && node->lines.isEmpty()) {
            node->lines.append(QStringLiteral("(*)"));
        } else if (kind == Shape::DoubleCircle && node->lines.isEmpty()) {
            node->lines.append(QStringLiteral("((*))"));
        }
    }

    // 1-D push-apart along the axis with the smaller overlap; the pixel
    // layout guarantees an order, so a bounded number of passes settles.
    void relaxOverlaps()
    {
        for (int pass = 0; pass < 32; ++pass) {
            bool moved = false;
            for (int i = 0; i < nodes.size(); ++i) {
                for (int j = i + 1; j < nodes.size(); ++j) {
                    Node &a = nodes[i];
                    Node &b = nodes[j];
                    const int overlapW =
                        std::min(a.right(), b.right())
                        - std::max(a.col, b.col) + 2; // +1 gap each side
                    const int overlapH =
                        std::min(a.bottom(), b.bottom())
                        - std::max(a.row, b.row) + 2;
                    if (overlapW <= 0 || overlapH <= 0)
                        continue;
                    const QPointF ca = a.shape->rect.center();
                    const QPointF cb = b.shape->rect.center();
                    // Push along the axis the pixel layout separates them
                    // on most (relative to their sizes), preserving order.
                    const bool pushX =
                        std::abs(ca.x() - cb.x()) * kCellH
                        >= std::abs(ca.y() - cb.y()) * kCellW;
                    if (pushX) {
                        Node &later = ca.x() <= cb.x() ? b : a;
                        later.col += overlapW;
                    } else {
                        Node &later = ca.y() <= cb.y() ? b : a;
                        later.row += overlapH;
                    }
                    moved = true;
                }
            }
            if (!moved)
                break;
        }
    }

    const Node *nodeAt(QPointF point) const
    {
        const Node *best = nullptr;
        qreal bestDistance = 0;
        for (const Node &node : nodes) {
            const QRectF grown =
                node.shape->rect.adjusted(-6, -6, 6, 6);
            if (!grown.contains(point))
                continue;
            const qreal d =
                (node.shape->rect.center() - point).manhattanLength();
            if (!best || d < bestDistance) {
                best = &node;
                bestDistance = d;
            }
        }
        return best;
    }

    void drawNodes()
    {
        for (const Node &node : nodes) {
            const bool doubles = node.shape->kind == Shape::Subroutine;
            canvas.drawBox(node.row, node.col, node.bottom(), node.right(),
                           doubles);
            if (node.shape->kind == Shape::Cylinder) {
                for (int c = node.col + 1; c < node.right(); ++c)
                    canvas.put(node.row, c, QChar(u'~'));
            }
        }
    }

    void drawNodeLabels()
    {
        for (const Node &node : nodes) {
            const int innerW = node.w - 2;
            const int firstRow =
                node.row + std::max(1, (node.h - int(node.lines.size())) / 2);
            for (int i = 0; i < node.lines.size(); ++i) {
                const QString line = node.lines.at(i).left(innerW);
                const int row = firstRow + i;
                if (row >= node.bottom())
                    break;
                const int col =
                    node.col + 1 + std::max(0, (innerW - int(line.size())) / 2);
                canvas.drawText(row, col, line);
            }
        }
    }

    // ---- groups ----------------------------------------------------------

    void drawGroups()
    {
        for (const Group &group : scene.groups) {
            const int top = rowOf(group.rect.top());
            const int left = colOf(group.rect.left());
            const int bottom =
                std::max(top + 2, rowOf(group.rect.bottom()));
            const int right =
                std::max(left + 4, colOf(group.rect.right()));
            if (!group.noBorder)
                canvas.drawBox(top, left, bottom, right);
            if (!group.title.isEmpty())
                canvas.drawText(top, left + 2,
                                QLatin1Char(' ') + group.title
                                    + QLatin1Char(' '));
        }
    }

    // ---- edges -----------------------------------------------------------

    struct Anchor {
        int row = 0;
        int col = 0;               // the cell just OUTSIDE the border
        TextCanvas::Direction out = TextCanvas::Down; // travel direction away
    };

    // Border-adjacent anchor for an edge leaving `node` toward `travel`.
    Anchor anchorFor(const Node &node, QPointF pixel,
                     TextCanvas::Direction travel) const
    {
        Anchor anchor;
        anchor.out = travel;
        switch (travel) {
        case TextCanvas::Down:
            anchor.row = node.bottom() + 1;
            anchor.col = std::clamp(colOf(pixel.x()),
                                    node.col + 1, node.right() - 1);
            break;
        case TextCanvas::Up:
            anchor.row = node.row - 1;
            anchor.col = std::clamp(colOf(pixel.x()),
                                    node.col + 1, node.right() - 1);
            break;
        case TextCanvas::Right:
            anchor.col = node.right() + 1;
            anchor.row = std::clamp(rowOf(pixel.y()),
                                    node.row + 1, node.bottom() - 1);
            break;
        case TextCanvas::Left:
            anchor.col = node.col - 1;
            anchor.row = std::clamp(rowOf(pixel.y()),
                                    node.row + 1, node.bottom() - 1);
            break;
        }
        return anchor;
    }

    void drawEdges()
    {
        for (const Path &path : scene.paths) {
            m_currentDraw = PathDraw();
            const Node *from = nodeAt(path.startPoint);
            const Node *to = nodeAt(path.endPoint);
            if (from && to && from != to)
                routeNodeEdge(path, *from, *to);
            else if (from && to)
                routeSelfLoop(path, *from);
            else
                routeFreePath(path);
            pathDraws.append(m_currentDraw);
        }
    }

    // Filled by drawPolyline as it draws the current path's segments.
    PathDraw m_currentDraw;
    int m_currentAnchorLength = 0;

    void routeNodeEdge(const Path &path, const Node &from, const Node &to)
    {
        // startDir/endDir point INTO their nodes: the edge leaves the
        // start against startDir and enters the end along endDir. Paths
        // without direction hints (sequence lifelines) travel along their
        // endpoint delta.
        const TextCanvas::Direction travel =
            directionOf(path.endPoint - path.startPoint);
        const TextCanvas::Direction out = path.startDir.isNull()
            ? travel
            : directionOf(QPointF(-path.startDir.x(), -path.startDir.y()));
        const TextCanvas::Direction in =
            path.endDir.isNull() ? travel : directionOf(path.endDir);

        const Anchor s = anchorFor(from, path.startPoint, out);
        // The end anchor sits outside the border the edge crosses, i.e.
        // on the side the edge travels FROM: opposite `in`.
        const Anchor t = anchorFor(to, path.endPoint, opposite(in));

        QList<QPoint> waypoints; // (col,row) polyline through cell space
        waypoints.append(QPoint(s.col, s.row));
        if (isVertical(out) && isVertical(in)) {
            if (s.col == t.col && std::abs(t.row - s.row) > 1
                && !channels.vFree(s.col, std::min(s.row, t.row) + 1,
                                   std::max(s.row, t.row) - 1)) {
                // Aligned but the column is blocked (a chain layout puts
                // other ranks' boxes between): detour instead of
                // clipping straight through them.
                routeOuterLane(path, from, to, true);
                return;
            }
            if (s.col != t.col) {
                // A valid Z needs all three segments clear: the crossbar
                // AND both vertical stubs (a back edge's stub would cut
                // straight through the rank between).
                bool found = false;
                const int midRow = zChannel(
                    s.row, t.row, (s.row + t.row) / 2, &found,
                    [&](int row) {
                        return channels.hFree(row, s.col, t.col)
                            && channels.vFree(s.col, s.row, row)
                            && channels.vFree(t.col, row, t.row);
                    });
                if (!found) {
                    // Go around via an outer vertical lane and enter
                    // through the target's side wall.
                    routeOuterLane(path, from, to, true);
                    return;
                }
                waypoints.append(QPoint(s.col, midRow));
                waypoints.append(QPoint(t.col, midRow));
            }
        } else if (!isVertical(out) && !isVertical(in)) {
            if (s.row == t.row && std::abs(t.col - s.col) > 1
                && !channels.hFree(s.row, std::min(s.col, t.col) + 1,
                                   std::max(s.col, t.col) - 1)) {
                routeOuterLane(path, from, to, false);
                return;
            }
            if (s.row != t.row) {
                bool found = false;
                const int midCol = zChannel(
                    s.col, t.col, (s.col + t.col) / 2, &found,
                    [&](int col) {
                        return channels.vFree(col, s.row, t.row)
                            && channels.hFree(s.row, s.col, col)
                            && channels.hFree(t.row, col, t.col);
                    });
                if (!found) {
                    routeOuterLane(path, from, to, false);
                    return;
                }
                waypoints.append(QPoint(midCol, s.row));
                waypoints.append(QPoint(midCol, t.row));
            }
        } else if (isVertical(out)) {
            waypoints.append(QPoint(s.col, t.row)); // L: vertical then horizontal
        } else {
            waypoints.append(QPoint(t.col, s.row)); // L: horizontal then vertical
        }
        waypoints.append(QPoint(t.col, t.row));

        drawPolyline(waypoints);
        placeMarker(path.endMarker, t.row, t.col, in);
        placeMarker(path.startMarker, s.row, s.col, opposite(out));
    }

    // Detour route for edges whose direct channel is blocked: out through
    // the start node's wall facing an outer vertical (or horizontal) lane
    // beyond the diagram's right (or bottom) flank, along the lane, and
    // into the target's wall on the same side — so neither end disturbs
    // the shared top/bottom anchors forward edges use.
    void routeOuterLane(const Path &path, const Node &from, const Node &to,
                        bool verticalLane)
    {
        if (verticalLane) {
            int flank = 0;
            for (const Node &node : std::as_const(nodes))
                flank = std::max(flank, node.right());
            const int sRow = std::clamp(rowOf(path.startPoint.y()),
                                        from.row + 1, from.bottom() - 1);
            const int tRow = std::clamp(rowOf(path.endPoint.y()),
                                        to.row + 1, to.bottom() - 1);
            int lane = flank + 3;
            for (int extra = 0; extra < 6; ++extra) {
                if (channels.vFree(lane, std::min(sRow, tRow),
                                   std::max(sRow, tRow)))
                    break;
                ++lane;
            }
            drawPolyline({QPoint(from.right() + 1, sRow), QPoint(lane, sRow),
                          QPoint(lane, tRow), QPoint(to.right() + 1, tRow)});
            placeMarker(path.endMarker, tRow, to.right() + 1,
                        TextCanvas::Left);
            placeMarker(path.startMarker, sRow, from.right() + 1,
                        TextCanvas::Right);
        } else {
            int flank = 0;
            for (const Node &node : std::as_const(nodes))
                flank = std::max(flank, node.bottom());
            const int sCol = std::clamp(colOf(path.startPoint.x()),
                                        from.col + 1, from.right() - 1);
            const int tCol = std::clamp(colOf(path.endPoint.x()),
                                        to.col + 1, to.right() - 1);
            int lane = flank + 2;
            for (int extra = 0; extra < 6; ++extra) {
                if (channels.hFree(lane, std::min(sCol, tCol),
                                   std::max(sCol, tCol)))
                    break;
                ++lane;
            }
            drawPolyline({QPoint(sCol, from.bottom() + 1), QPoint(sCol, lane),
                          QPoint(tCol, lane), QPoint(tCol, to.bottom() + 1)});
            placeMarker(path.endMarker, to.bottom() + 1, tCol,
                        TextCanvas::Up);
            placeMarker(path.startMarker, from.bottom() + 1, sCol,
                        TextCanvas::Down);
        }
    }

    void routeSelfLoop(const Path &path, const Node &node)
    {
        // Out the right wall, two columns over, back in below.
        const int rowA = std::clamp(rowOf(path.startPoint.y()),
                                    node.row + 1, node.bottom() - 1);
        int rowB = std::clamp(rowOf(path.endPoint.y()),
                              node.row + 1, node.bottom() - 1);
        if (rowB == rowA)
            rowB = std::min(rowA + 1, node.bottom() - 1);
        const int wall = node.right();
        const int lane = wall + 3;
        drawPolyline({QPoint(wall + 1, rowA), QPoint(lane, rowA),
                      QPoint(lane, rowB), QPoint(wall + 1, rowB)});
        placeMarker(path.endMarker, rowB, wall + 1, TextCanvas::Left);
    }

    // No node association (sequence lifelines and messages, free lines):
    // rasterize endpoint-to-endpoint, vertical/horizontal snapping first.
    // Segments clip against node boxes — a lifeline stops at the actor
    // box border (merging into it as a junction) instead of cutting
    // through the box.
    void routeFreePath(const Path &path)
    {
        const int c1 = colOf(path.startPoint.x());
        const int r1 = rowOf(path.startPoint.y());
        const int c2 = colOf(path.endPoint.x());
        const int r2 = rowOf(path.endPoint.y());
        if (c1 != c2 && r1 != r2) {
            drawPolyline({QPoint(c1, r1), QPoint(c1, r2), QPoint(c2, r2)});
        } else {
            drawPolyline({QPoint(c1, r1), QPoint(c2, r2)});
        }

        const TextCanvas::Direction in = c1 == c2
            ? (r2 >= r1 ? TextCanvas::Down : TextCanvas::Up)
            : (c2 >= c1 ? TextCanvas::Right : TextCanvas::Left);
        placeMarker(path.endMarker, r2, c2, in);
        placeMarker(path.startMarker, r1, c1, opposite(in));
    }

    enum CellState { FreeCell, BorderCell, InteriorCell };

    CellState cellState(int row, int col) const
    {
        for (const Node &node : nodes) {
            if (row < node.row || row > node.bottom()
                || col < node.col || col > node.right())
                continue;
            const bool onBorder = row == node.row || row == node.bottom()
                || col == node.col || col == node.right();
            return onBorder ? BorderCell : InteriorCell;
        }
        return FreeCell;
    }

    // Maximal free runs, each end extended onto an abutting border cell so
    // the line merges into the box edge as a junction glyph.
    void drawClippedVLine(int col, int rowA, int rowB)
    {
        const int lo = std::min(rowA, rowB), hi = std::max(rowA, rowB);
        int run = -1;
        for (int row = lo; row <= hi + 1; ++row) {
            const bool free =
                row <= hi && cellState(row, col) == FreeCell;
            if (free && run < 0)
                run = row;
            if (!free && run >= 0) {
                int from = run, to = row - 1;
                // A short stub past the last box (a lifeline's tail below
                // the bottom actor) is quantization noise, not drawing.
                const bool trailingStub =
                    run > lo && to == hi && to - run < 2;
                if (!trailingStub) {
                    if (from > lo && cellState(from - 1, col) == BorderCell)
                        --from;
                    if (to < hi && cellState(to + 1, col) == BorderCell)
                        ++to;
                    canvas.drawVLine(col, from, to);
                    channels.takeV(col, from, to);
                }
                run = -1;
            }
        }
    }
    void drawClippedHLine(int row, int colA, int colB)
    {
        const int lo = std::min(colA, colB), hi = std::max(colA, colB);
        int run = -1;
        for (int col = lo; col <= hi + 1; ++col) {
            const bool free =
                col <= hi && cellState(row, col) == FreeCell;
            if (free && run < 0)
                run = col;
            if (!free && run >= 0) {
                int from = run, to = col - 1;
                if (from > lo && cellState(row, from - 1) == BorderCell)
                    --from;
                if (to < hi && cellState(row, to + 1) == BorderCell)
                    ++to;
                canvas.drawHLine(row, from, to);
                channels.takeH(row, from, to);
                run = -1;
            }
        }
    }

    static TextCanvas::Direction opposite(TextCanvas::Direction d)
    {
        switch (d) {
        case TextCanvas::Up: return TextCanvas::Down;
        case TextCanvas::Down: return TextCanvas::Up;
        case TextCanvas::Left: return TextCanvas::Right;
        case TextCanvas::Right: return TextCanvas::Left;
        }
        return d;
    }

    // Z-route channel search: try the preferred crossbar coordinate first,
    // then spiral outward within the endpoints' span (±2 slack), taking
    // the first coordinate the whole-route predicate accepts.
    template <typename Valid>
    int zChannel(int a, int b, int preferred, bool *found, Valid valid)
    {
        const int lo = std::min(a, b) - 2;
        const int hi = std::max(a, b) + 2;
        for (int delta = 0; delta <= hi - lo; ++delta) {
            for (const int candidate :
                 {preferred + delta, preferred - delta}) {
                if (candidate < lo || candidate > hi)
                    continue;
                if (valid(candidate)) {
                    *found = true;
                    return candidate;
                }
                if (delta == 0)
                    break; // +0 and -0 are the same candidate
            }
        }
        *found = false;
        return preferred;
    }

    // Every segment draws box-clipped: a well-routed segment never meets a
    // box (the clip is a no-op), and a forced crossing — outer-lane
    // detours in dense layouts — renders as stopping at one border and
    // resuming past the other instead of corrupting the box interior.
    void drawPolyline(const QList<QPoint> &waypoints)
    {
        m_currentAnchorLength = 0;
        for (int i = 1; i < waypoints.size(); ++i) {
            const QPoint a = waypoints.at(i - 1);
            const QPoint b = waypoints.at(i);
            if (a.y() == b.y())
                drawClippedHLine(a.y(), a.x(), b.x());
            else
                drawClippedVLine(a.x(), a.y(), b.y());
            const int length = std::abs(b.x() - a.x())
                             + std::abs(b.y() - a.y());
            if (length >= m_currentAnchorLength) {
                m_currentAnchorLength = length;
                m_currentDraw.mid = QPoint((a.x() + b.x()) / 2,
                                           (a.y() + b.y()) / 2);
            }
        }
        if (!waypoints.isEmpty()) {
            m_currentDraw.start = waypoints.first();
            m_currentDraw.end = waypoints.last();
            if (m_currentDraw.mid.x() < 0)
                m_currentDraw.mid = m_currentDraw.start;
            m_currentDraw.valid = true;
        }
    }

    void placeMarker(Marker marker, int row, int col,
                     TextCanvas::Direction dir)
    {
        const QChar glyph = markerGlyph(marker, dir);
        if (!glyph.isNull())
            canvas.put(row, col, glyph);
    }

    // ---- loose texts (edge labels, sequence messages, ER roles) ----------

    // A label cell may land on blank space or plain line fill (that is the
    // "label displaces the line" rule) but must not eat arrowheads,
    // corners, or box borders.
    bool labelFits(int row, int col, int length) const
    {
        for (int i = 0; i < length; ++i) {
            const QChar c = canvas.at(row, col + i);
            if (c.isNull() || c == QLatin1Char(' ')
                || c == QChar(u'─') || c == QChar(u'│'))
                continue;
            return false;
        }
        return true;
    }

    // An edge label's home cell: hasBackground texts belong to a path, so
    // find the path (and whether the label sits at its start, middle, or
    // end in pixels) and return the matching DRAWN cell — routing may
    // have moved the edge far from the label's pixel position. Other
    // loose texts keep their pixel-derived cell.
    QPoint labelHome(const Text &text) const
    {
        const QPoint pixelCell(colOf(text.rect.center().x()),
                               rowOf(text.rect.center().y()));
        if (!text.hasBackground)
            return pixelCell;
        int bestPath = -1;
        int bestRef = 1;
        qreal bestDistance = 0;
        const int n = std::min(int(scene.paths.size()),
                               int(pathDraws.size()));
        for (int p = 0; p < n; ++p) {
            if (!pathDraws.at(p).valid)
                continue;
            const Path &path = scene.paths.at(p);
            const QPointF refs[3] = {
                path.startPoint,
                (path.startPoint + path.endPoint) / 2.0,
                path.endPoint,
            };
            for (int r = 0; r < 3; ++r) {
                const qreal d =
                    (refs[r] - text.rect.center()).manhattanLength();
                if (bestPath < 0 || d < bestDistance) {
                    bestPath = p;
                    bestRef = r;
                    bestDistance = d;
                }
            }
        }
        if (bestPath < 0)
            return pixelCell;
        const PathDraw &draw = pathDraws.at(bestPath);
        const QPoint cell = bestRef == 0 ? draw.start
                          : bestRef == 2 ? draw.end : draw.mid;
        return cell;
    }

    void drawLooseTexts()
    {
        for (int i = 0; i < scene.texts.size(); ++i) {
            if (nodeTexts.contains(i))
                continue;
            const Text &text = scene.texts.at(i);
            if (text.text.trimmed().isEmpty())
                continue;
            const QStringList lines = text.text.split(QLatin1Char('\n'));
            const QPoint home = labelHome(text);
            const int firstRow = home.y() - (lines.size() - 1) / 2;
            for (int l = 0; l < lines.size(); ++l) {
                const QString line = lines.at(l);
                const int col =
                    std::max(0, home.x() - int(line.size()) / 2);
                int row = firstRow + l;
                // Nudge off arrowheads/borders when possible — up to a
                // label-length sideways, one row up or down — and place
                // at the natural cell only when nowhere nearby fits.
                bool placed = false;
                const int reach = int(line.size()) + 2;
                for (const int dRow : {0, -1, 1}) {
                    for (int shift = 0; shift <= reach && !placed;
                         ++shift) {
                        for (const int dCol : {shift, -shift}) {
                            if (labelFits(row + dRow, col + dCol,
                                          int(line.size()))) {
                                canvas.drawText(row + dRow, col + dCol,
                                                line);
                                placed = true;
                                break;
                            }
                            if (shift == 0)
                                break;
                        }
                    }
                    if (placed)
                        break;
                }
                if (!placed)
                    canvas.drawText(row, col, line);
            }
        }
    }

    QString build()
    {
        collectNodes();
        drawGroups();
        drawNodes();
        drawEdges();
        drawNodeLabels();
        drawLooseTexts();
        return canvas.toString();
    }
};

} // namespace

QString renderText(const Scene &scene)
{
    if (scene.isEmpty())
        return QString();
    Builder builder(scene);
    QString text = builder.build();

    // Normalize the frame: no leading blank lines, no common left margin
    // (routing offsets and quantization leave both behind).
    QStringList lines = text.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    int indent = INT_MAX;
    for (const QString &line : std::as_const(lines)) {
        if (line.trimmed().isEmpty())
            continue;
        int i = 0;
        while (i < line.size() && line.at(i) == QLatin1Char(' '))
            ++i;
        indent = std::min(indent, i);
    }
    if (indent > 0 && indent != INT_MAX) {
        for (QString &line : lines)
            line = line.mid(indent);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace Diagram
