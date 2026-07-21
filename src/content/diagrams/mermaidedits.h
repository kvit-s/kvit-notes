// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef MERMAIDEDITS_H
#define MERMAIDEDITS_H

#include <QPointF>
#include <QString>

#include "mermaidast.h"

// Gesture-to-edit engine: every on-diagram gesture becomes a targeted,
// formatting-preserving edit of the fence source, computed from AST source
// offsets. The fence is never reserialized from the model; bytes
// outside the specified span are preserved exactly, which the
// byte-preservation property tests assert. Each function returns the complete
// new source (applied through the undo-aware blockModel.updateContent as one
// step) or an error explaining the refusal.
namespace Mermaid {
namespace Edits {

struct Result {
    bool ok = false;
    QString source;   // the full new fence source when ok
    QString error;    // human-readable refusal otherwise
    QString newId;    // quickAddNode: the generated node id
};

// Manual arrangement: write a full pos-line snapshot. `positions` holds
// every live node center (source order, logical pixels); entries for deleted
// or renamed ids disappear because the line is rewritten from live nodes
// only. The write changes only the pos line (replaced in place, or appended
// as the last line of the body). Idempotent: identical positions produce an
// identical line.
Result writeArrangement(const QString &source,
                        const QList<QPair<QString, QPointF>> &positions);

// Reset layout: delete the pos line (and its newline) as one edit.
// A source with no recognized pos line is returned unchanged.
Result resetArrangement(const QString &source);

// ---- Flowchart semantic gestures ----
// Every function refuses (ok = false) rather than applying approximately when
// the edit cannot be guaranteed to reparse without new diagnostics.

// Inline label edit: replace the raw text between the node's brackets;
// quoting is added only when the new text requires it. A node without
// brackets gains `[label]` after its declaring id.
Result setNodeLabel(const QString &source, const QString &nodeId,
                    const QString &newLabel);

// Change shape: rewrite the bracket delimiters around the (raw) label.
Result setNodeShape(const QString &source, const QString &nodeId,
                    NodeShape shape);

// Rename id: replace every AST-known reference span; comments and string
// labels are untouched.
Result renameNode(const QString &source, const QString &oldId,
                  const QString &newId);

// Delete node: remove the statements declaring the node and every edge
// statement referencing it — whole statements only.
Result deleteNode(const QString &source, const QString &nodeId);

// Delete edge: remove that edge statement; a chained statement is split so
// unaffected links survive.
Result deleteEdge(const QString &source, int edgeIndex);

// Change edge style: rewrite the arrow token (inline labels preserved).
Result setEdgeStroke(const QString &source, int edgeIndex, EdgeStroke stroke);

// Draw edge: insert one edge statement after the last statement mentioning
// the source node, before the pos line, indentation matched.
Result insertEdge(const QString &source, const QString &fromId,
                  const QString &toId);

// Quick-add: insert a declaration-plus-edge statement; the generated id
// avoids collisions and is returned in Result::newId.
Result quickAddNode(const QString &source, const QString &fromId);

// Style/color: insert or update a Kvit-named classDef plus a class
// statement; existing classDefs are never rewritten.
Result setNodeStyle(const QString &source, const QString &nodeId,
                    const QColor &fill, const QColor &stroke);

// Reparent: move the node's standalone declaration into the named subgraph
// (empty subgraphId = out to top level). Refused when membership comes from
// edge statements inside the subgraph.
Result reparentNode(const QString &source, const QString &nodeId,
                    const QString &subgraphId);

// Reorder among siblings (auto mode only): swap the node's standalone
// declaration statement with its previous/next statement.
Result reorderNode(const QString &source, const QString &nodeId, int delta);

// ---- Sequence-diagram reordering ----
// Vertical geometry is statement order: moving a message up or down swaps its
// line with the adjacent message's line, one-for-one. `eventIndex` addresses
// SequenceAst::events; the event must be a Message.
Result moveSequenceMessage(const QString &source, int eventIndex, int delta);

// Horizontal order is declaration order: moving a participant swaps its
// `participant`/`actor` declaration statement with its neighbour's. Refused
// for auto-declared participants (no declaration statement to move).
Result moveSequenceParticipant(const QString &source, const QString &id,
                               int delta);

} // namespace Edits
} // namespace Mermaid

#endif // MERMAIDEDITS_H
