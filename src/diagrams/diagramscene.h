// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMSCENE_H
#define DIAGRAMSCENE_H

#include <QColor>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>

// The stable seam between syntax/layout and rendering.
// A Scene holds logical-coordinate primitives with semantic color *roles* rather
// than resolved colors or QQuickItem objects, so the same scene paints to the
// screen (DiagramCanvas + QPainter) and to a PDF raster, with colors resolved
// from the active Theme at paint time. Layout produces a Scene; painting
// consumes one; HTML export does not use it (the browser renders the original
// Mermaid source through Mermaid.js).
namespace Diagram {

// Semantic color roles resolved against Theme at paint/export time.
enum class Role {
    Background,
    NodeFill,
    NodeStroke,
    EdgeStroke,
    Label,
    EdgeLabel,
    SubgraphFill,
    SubgraphStroke,
    NoteFill,        // sequence-diagram notes
    NoteStroke,
    Activation,      // sequence-diagram activation bar fill
};

// Line-end markers (arrowheads and friends) drawn at a Path's endpoints.
enum class Marker {
    None,
    Arrow,         // filled triangle (`-->`, `->>`)
    OpenArrow,     // two stroked lines (`->` open head)
    Cross,         // `-x`
    Dot,           // `-)` async point
    TriangleOpen,  // UML extension `<|--`
    DiamondFilled, // UML composition `*--`
    DiamondOpen,   // UML aggregation `o--`
    CircleOpen,    // UML lollipop `()--`
    ErOne,         // crow's-foot `||` (double bar)
    ErZeroOne,     // crow's-foot `|o` (circle + bar)
    ErMany,        // crow's-foot `}|` (fork + bar)
    ErZeroMany,    // crow's-foot `}o` (fork + circle)
};

struct Shape {
    enum Kind {
        Rect, RoundRect, Stadium, Circle, DoubleCircle, Ellipse, Rhombus,
        Hexagon, Cylinder, Subroutine, Parallelogram, ParallelogramAlt,
        Trapezoid, TrapezoidAlt, Odd,
        Actor,   // sequence-diagram stick figure (rect is its bounding box)
    };
    Kind kind = Rect;
    QRectF rect;
    Role fillRole = Role::NodeFill;
    Role strokeRole = Role::NodeStroke;
    qreal strokeWidth = 1.5;
    // Optional explicit overrides from classDef/style; invalid => use the role.
    QColor fillOverride;
    QColor strokeOverride;
    QString nodeId;      // for accessibility / hit-testing / selection (§20.1)
    // §20.5 preview↔source linking: the element's defining span in the fence.
    int srcStart = -1;
    int srcLen = 0;
};

struct Path {
    QPainterPath path;
    Role strokeRole = Role::EdgeStroke;
    qreal strokeWidth = 1.5;
    Qt::PenStyle penStyle = Qt::SolidLine;
    Marker startMarker = Marker::None;
    Marker endMarker = Marker::None;
    QPointF startPoint;   // marker anchors and orientation
    QPointF endPoint;
    QPointF startDir;     // unit vector pointing into the start node
    QPointF endDir;       // unit vector pointing into the end node
    // §20.1 selection / §20.5 linking: which AST edge (family-specific index)
    // this path draws, and its source span. -1 = not a selectable element.
    int edgeIndex = -1;
    int srcStart = -1;
    int srcLen = 0;
};

struct Text {
    QRectF rect;          // bounding box; alignment applies within it
    QString text;
    Role role = Role::Label;
    qreal fontSize = 14;
    bool bold = false;
    bool italic = false;
    int align = Qt::AlignCenter;  // Qt::Alignment flags
    bool hasBackground = false;   // edge labels sit on a small backdrop
};

struct Group {            // a subgraph frame / background band
    QRectF rect;
    QString title;
    QColor fillOverride;  // invalid => SubgraphFill role
    bool noBorder = false;
};

struct Scene {
    QList<Group> groups;  // painted first (behind nodes)
    QList<Shape> shapes;
    QList<Path> paths;
    QList<Text> texts;
    QRectF bounds;
    // Accessibility metadata.
    QString accTitle;
    QString accDescr;
    QString summary;      // generated, e.g. "Mermaid flowchart, 8 nodes, 9 edges"

    bool isEmpty() const { return shapes.isEmpty() && paths.isEmpty(); }
};

} // namespace Diagram

#endif // DIAGRAMSCENE_H
