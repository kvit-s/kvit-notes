// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef DIAGRAMBUDGET_H
#define DIAGRAMBUDGET_H

#include <QtGlobal>

// Ceilings for everything a diagram or formula can ask the process to
// allocate. A note is untrusted input: it arrives by import, paste, or sync,
// and its Mermaid source, arrangement comments and TeX all feed sizes straight
// into backing stores. Without a ceiling, a manual arrangement comment reading
// `%% mermaid-flow:pos B=1e12,1e12` produces a scene 10^12 logical pixels
// across, and a 400,000-character formula produces a 3.9M x 9 raster (134 MiB,
// six seconds) — both from a file that looks unremarkable in a text editor.
//
// The numbers are deliberately far above any real diagram. The largest
// checked-in fixture lays out under 4,000 logical pixels across, and the
// canvas comment about fit-to-window scaling cites a 15,000 px flowchart as the
// long case, so kMaxSceneSpan leaves more than an order of magnitude of room.
namespace Diagram {

// Largest magnitude accepted for a manually pinned node center, in logical
// pixels. Coordinates outside this clamp to it rather than being dropped, so a
// node stays visible at the edge instead of vanishing.
constexpr double kMaxPinnedCoordinate = 200000.0;

// Largest scene extent, in logical pixels, along either axis.
constexpr double kMaxSceneSpan = 500000.0;

// Largest raster a single diagram or formula may allocate, in pixels. At 4
// bytes per pixel this is a 256 MiB backing store.
constexpr qint64 kMaxRasterPixels = 64LL * 1024 * 1024;

// Largest dimension, in pixels, for a rasterized diagram or formula.
constexpr int kMaxRasterEdge = 32768;

// Largest character-art export. Text export walks scene coordinates into a
// row/column grid, so scene bounds bound this too, but the grid is
// materialized as real QString and QList storage per row and needs its own
// ceiling.
//
// The per-axis caps alone do not bound the storage: two pinned nodes far apart
// on both axes make a box whose side walls touch every row out to the far
// column, and every touched row grows three dense per-column arrays. At the
// 20000 x 20000 limits that is 4 x 10^8 cells, roughly 1.6 GiB. kMaxTextCanvas
// Cells is therefore the binding limit — the total number of cells the grid may
// materialize across all rows, about 8 MiB of backing store at four bytes per
// cell. Drawing past it is clipped, which is the same degradation the per-axis
// caps already apply.
constexpr int kMaxTextCanvasRows = 20000;
constexpr int kMaxTextCanvasCols = 20000;
constexpr qint64 kMaxTextCanvasCells = 2LL * 1000 * 1000;

// Largest TeX source accepted for layout or rasterization, in characters.
// Real formulas are a line or two; this admits a very generous one while
// keeping layout cost bounded.
constexpr int kMaxTexChars = 8192;

// Bounds on the presentation knobs that multiply a formula's raster size.
constexpr int kMaxTextSizePx = 512;
constexpr qreal kMaxDevicePixelRatio = 8.0;

} // namespace Diagram

#endif // DIAGRAMBUDGET_H
