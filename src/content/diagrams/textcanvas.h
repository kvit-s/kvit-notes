// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TEXTCANVAS_H
#define TEXTCANVAS_H

#include <QChar>
#include <QList>
#include <QString>
#include <QStringList>

// Character-grid draw target for the Mermaid → text exporter.
// DiagramRepair analyzes character diagrams;
// this is the missing constructive half: a dynamically grown grid of
// QChar cells with box/line primitives whose crossings resolve to the
// proper junction glyphs (drawing ─ over │ yields ┼; over a box's top
// edge yields ┬; …). Junctions are computed from per-cell line-arm sets,
// so any drawing order produces the same glyph. Emits only the light
// box-drawing vocabulary diagramglyphs.h recognizes, which is what makes
// the exporter's output a DiagramRepair fixed point.
class TextCanvas
{
public:
    enum Direction { Up, Down, Left, Right };

    TextCanvas() = default;

    int rows() const { return m_lines.size(); }
    int cols() const;
    // Cells actually materialized across all rows. The grid clips rather than
    // grows once this reaches Diagram::kMaxTextCanvasCells, so callers and
    // tests can check the export stayed inside its storage budget.
    qint64 cells() const { return m_cells; }

    QChar at(int row, int col) const; // QChar() outside the grid

    // Raw cell write (text, arrowheads): overwrites and clears the cell's
    // line-arm state.
    void put(int row, int col, QChar c);
    void drawText(int row, int col, const QString &text);

    // Line primitives with junction resolution. Coordinates are inclusive
    // and may be given in either order.
    void drawHLine(int row, int col1, int col2);
    void drawVLine(int col, int row1, int row2);
    // Box outline: four lines whose corners land as ┌┐└┘ (or richer
    // junctions where boxes touch). `doubleWalls` renders the side walls
    // as ║ (the Subroutine shape).
    void drawBox(int top, int left, int bottom, int right,
                 bool doubleWalls = false);

    // Arrowhead pointing along `dir`, e.g. Down = ▼. Overwrites the cell.
    void drawArrowhead(int row, int col, Direction dir);

    // The grid as text: '\n'-joined lines, trailing spaces trimmed.
    QString toString() const;

private:
    // Line arms per cell (bitmask of ArmUp..ArmRight); 0 = not a line cell.
    enum Arm { ArmUp = 1, ArmDown = 2, ArmLeft = 4, ArmRight = 8 };
    // Grow the grid to cover (row, col). False when the cell lies outside
    // the export budget and the caller must skip it.
    bool ensure(int row, int col);
    // False when the cell lay outside the budget and was not drawn. Within one
    // row that answer only ever goes from true to false — a further column
    // costs strictly more storage — so a horizontal run can stop at the first
    // refusal instead of walking the rest of a clipped span cell by cell.
    bool mergeArms(int row, int col, int arms, bool doubleVertical = false);
    static QChar charForArms(int arms, bool doubleVertical);

    QStringList m_lines;
    QList<QList<quint8>> m_arms;      // parallel to m_lines
    QList<QList<bool>> m_doubles;     // cell renders with double vertical
    qint64 m_cells = 0;               // sum of the row lengths above
};

#endif // TEXTCANVAS_H
