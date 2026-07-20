// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMREPAIR_H
#define DIAGRAMREPAIR_H

#include <QString>

// Ingest straightening for character-cell diagrams (diagrams-prd.md §7.5).
// LLM-emitted diagrams routinely carry small geometric flaws — a box's
// top-right corner one or two columns short of its side bars, a connector
// that jogs sideways between rows. This pass repairs those flaws in the
// stored text, in the same family as the LLM markdown normalizations: it
// runs only inside DocumentSerializer::parse on fences tagged as diagrams,
// is idempotent, arms the same one-time .bak backup through the load-time
// divergence test, and is undoable when it arrives via paste.
//
// Every repair is a zero-shift edit: a wall bar swaps places with adjacent
// spaces, an edge corner extends or trims through fill characters, a tee
// slides along its edge by swapping with fill — so no character to the
// right of a repair ever moves, and label text is never modified. Anything
// the conservative rules cannot fix cleanly is left exactly as written.
namespace DiagramRepair {

// Bodies larger than this are returned unchanged (mirrors the classifier's
// inspection cap; a huge fence is preserved verbatim, never repaired).
constexpr int kRepairCapChars = 256 * 1024;

// Straighten `body` (the verbatim fence content). Returns the repaired
// text; when nothing needs fixing the input is returned unchanged.
QString repair(const QString &body);

} // namespace DiagramRepair

#endif // DIAGRAMREPAIR_H
