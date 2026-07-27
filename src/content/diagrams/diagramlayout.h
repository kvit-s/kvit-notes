// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMLAYOUT_H
#define DIAGRAMLAYOUT_H

#include <QString>
#include <QStringList>

#include "diagramscene.h"
#include "mermaidast.h"

// Deterministic layered layout for the flowchart family.
// It measures node labels, breaks cycles, assigns ranks by longest path, reduces
// crossings with barycenter sweeps, places nodes on a layered grid, routes
// edges, and wraps subgraph members in compound bounds. Identical source, font,
// and direction always produce an identical Scene (node id and source order are
// the tie-breakers), so edits never reorder unrelated nodes. Layout is
// width-independent: the canvas scales the finished scene to fit.
namespace Diagram {

struct LayoutOptions {
    QString fontFamily = QStringLiteral("sans-serif");
    int fontPixelSize = 14;
    Mermaid::Direction direction = Mermaid::Direction::TB;
};

Scene layoutFlowchart(const Mermaid::FlowchartAst &ast, const LayoutOptions &opts);

// Sequence family: lifelines establish columns, messages and fragments
// establish vertical bands, labels expand columns before final placement.
// Implemented in sequencelayout.cpp.
Scene layoutSequence(const Mermaid::SequenceAst &ast, const LayoutOptions &opts);

// Class family: UML compartment boxes over the shared layered core, relations
// with UML end markers and cardinalities (classlayout.cpp).
Scene layoutClassDiagram(const Mermaid::ClassAst &ast, const LayoutOptions &opts);

// State family: recursive compound layout — composites place their members
// locally then join the parent scope as one node (statelayout.cpp).
Scene layoutStateDiagram(const Mermaid::StateAst &ast, const LayoutOptions &opts);

// ER family: entity tables with crow's-foot relationship markers
// (erlayout.cpp).
Scene layoutErDiagram(const Mermaid::ErAst &ast, const LayoutOptions &opts);

// Translate every primitive so the scene starts at (margin, margin) and set
// scene.bounds. Shared by the family layout engines.
void finalizeSceneBounds(Scene &scene, qreal margin);

// The shared layered core (§8.4): cycle breaking, longest-path ranking,
// barycenter crossing reduction, and coordinate assignment. Returns one center
// per node. Deterministic: node index and edge order are the tie-breakers.
struct LayeredEdge {
    int u = 0;
    int v = 0;
    int minLen = 1;
    // Room along the flow axis this edge's label needs. The gap between two
    // ranks grows to fit the widest label crossing it, so a label never has
    // to be squeezed between two boxes that are closer together than it is
    // wide. Zero for an unlabelled edge.
    double labelMain = 0.0;
};

// One placement pass over the layered core.
//
// `centers` holds one center per input node, in input order.
//
// `edgeBends` holds, per input edge in input order, the waypoints an edge
// spanning more than one rank must pass through: one per rank between its
// ends, ordered from `u` towards `v`, and empty for every edge whose ends are
// neighbours. Those waypoints are real positions in the layout — each is a
// zero-thickness placeholder node that takes part in ordering and takes its
// own slot in the rank it sits in — so an edge routed through them passes
// between the boxes of that rank instead of across them, and its label has
// somewhere clear to sit. Without them, an edge is a straight line from one
// end to the other and anything standing in between is drawn over.
struct LayeredLayout {
    QList<QPointF> centers;
    QList<QList<QPointF>> edgeBends;
};
LayeredLayout layeredLayout(const QList<QSizeF> &sizes,
                            const QList<LayeredEdge> &edges,
                            Mermaid::Direction direction,
                            double rankGap, double nodeGap);

// Centers only, for callers that route their own edges.
QList<QPointF> layeredCenters(const QList<QSizeF> &sizes,
                              const QList<LayeredEdge> &edges,
                              Mermaid::Direction direction,
                              double rankGap, double nodeGap);

// Where to put an edge label: on `path`, in open space.
//
// The preferred spot is the middle of the path, which is what a reader looks
// for. When something is already there the rect slides along the path, and
// then steps sideways off it, until it clears every rect in `obstacles` —
// pass the node boxes plus the labels already placed, so labels do not stack
// on each other either. `size` is the label's rendered size. When nothing
// clears (a dense diagram with no room anywhere), the middle of the path is
// returned, which is no worse than placing it blindly.
QRectF placeEdgeLabel(const QPainterPath &path, const QSizeF &size,
                      const QList<QRectF> &obstacles);

// Where to put a label belonging to one end of an edge rather than to the
// edge as a whole — a UML cardinality, say, which means nothing unless the
// reader can tell which end it is on.
//
// `tip` is the endpoint the label belongs to and `dir` points from there
// along the edge. The label goes beside the line, past the `markerLength`
// the end marker occupies, so it sits neither on the arrowhead nor on the
// line itself, and it is walked further out until it clears `obstacles`.
QRectF placeEndLabel(const QPointF &tip, const QPointF &dir,
                     double markerLength, const QSizeF &size,
                     const QList<QRectF> &obstacles);

// Split a label on `<br>` tags and `\n` escapes (shared label convention).
QStringList labelLines(const QString &label);

} // namespace Diagram

#endif // DIAGRAMLAYOUT_H
