// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMLAYOUT_H
#define DIAGRAMLAYOUT_H

#include <QString>
#include <QStringList>

#include "diagramscene.h"
#include "mermaidast.h"

// Deterministic layered layout for the flowchart family (diagrams-prd.md §8.4).
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
// establish vertical bands, labels expand columns before final placement
// (diagrams-prd.md §8.4). Implemented in sequencelayout.cpp.
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
};
QList<QPointF> layeredCenters(const QList<QSizeF> &sizes,
                              const QList<LayeredEdge> &edges,
                              Mermaid::Direction direction,
                              double rankGap, double nodeGap);

// Split a label on `<br>` tags and `\n` escapes (shared label convention).
QStringList labelLines(const QString &label);

} // namespace Diagram

#endif // DIAGRAMLAYOUT_H
