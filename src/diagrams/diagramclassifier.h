// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMCLASSIFIER_H
#define DIAGRAMCLASSIFIER_H

#include <QString>
#include <QStringList>

// Conservative character-cell-diagram detector (diagrams-prd.md §7). Given the
// verbatim body of a fenced block, it decides whether that body is an
// ASCII/Unicode "character diagram" — a drawing made of box-drawing glyphs,
// arrows, and boxed labels, the kind LLMs routinely emit — as opposed to
// ordinary code, a table, a directory listing, a stack trace, or prose.
//
// It is a pure, unit-tested component with no Qt-GUI or QML dependency. The
// ingest pass in DocumentSerializer::parse calls it at exactly the boundaries
// where Markdown enters a document (file open, paste), and rewrites a
// high-confidence fence's info string to `diagram`; it never runs on a block's
// content edits. QML holds no detection logic.
//
// The design is deliberately biased toward false negatives: a missed diagram
// merely renders as code (still editable, still correct text), whereas a false
// positive persists a wrong tag in the user's file. The gates below therefore
// demand real diagram geometry — framed regions and inter-frame connectors —
// not merely the presence of box-drawing strokes, which a directory tree also
// has.
namespace DiagramClassifier {

// Only the first this-many UTF-16 code units are inspected (diagrams-prd.md
// §7.4). A larger fence is preserved verbatim but never tagged.
constexpr int kInspectionCapChars = 256 * 1024;

struct Result {
    bool isDiagram = false;
    // A monotonic confidence built from the weighted evidence; compared against
    // a fixed documented threshold. Exposed for tests and diagnostics.
    double score = 0.0;
    // Human-readable evidence and gate outcomes, in evaluation order.
    QStringList reasons;
};

// Classify a fence body. Linear in the inspected length.
Result classify(const QString &content);

// Convenience: the boolean decision only.
bool looksLikeDiagram(const QString &content);

} // namespace DiagramClassifier

#endif // DIAGRAMCLASSIFIER_H
