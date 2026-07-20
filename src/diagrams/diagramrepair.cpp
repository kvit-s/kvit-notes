// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagramrepair.h"
#include "diagramglyphs.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace DiagramRepair {
namespace {

// How far a wall bar / corner may sit from its edge's dominant column and
// still be treated as part of that edge. LLM flaws in the corpus are off by
// one to three columns; anything farther is assumed intentional.
constexpr int kTol = 3;
// A connector run may jog sideways by at most this many columns per row.
constexpr int kRunTol = 2;

// ---- character classes ----
// The vocabulary lives in diagramglyphs.h (pre-launch-plan.md §2.1) so the
// ASCII exporter emits exactly what repair accepts; these thin wrappers
// keep every call site below unchanged.

bool isTopLeft(QChar c) { return DiagramGlyphs::isTopLeft(c); }
bool isTopRight(QChar c) { return DiagramGlyphs::isTopRight(c); }
bool isBottomLeft(QChar c) { return DiagramGlyphs::isBottomLeft(c); }
bool isBottomRight(QChar c) { return DiagramGlyphs::isBottomRight(c); }
bool isHFill(QChar c) { return DiagramGlyphs::isHFill(c); }
bool isEdgeJunction(QChar c) { return DiagramGlyphs::isEdgeJunction(c); }
bool isWall(QChar c) { return DiagramGlyphs::isWall(c); }
bool isConnector(QChar c) { return DiagramGlyphs::isConnector(c); }

QChar at(const QStringList &lines, int row, int col)
{
    if (row < 0 || row >= lines.size())
        return QChar();
    const QString &l = lines.at(row);
    if (col < 0 || col >= l.size())
        return QChar();
    return l.at(col);
}

// ---- zero-shift edits ----
//
// Every mutation swaps a character with adjacent spaces or edge fill, so no
// other column on the line moves. Each returns false (leaving the line
// unchanged) when the swap span holds anything it must not consume.

// The cells a character moves through are all spaces (or past the end of
// the line), so a move is pure cell assignment: no other column shifts.
bool moveThroughSpacesRight(QString &line, int pos, int k)
{
    for (int i = 1; i <= k; ++i) {
        const int p = pos + i;
        if (p < line.size() && line.at(p) != u' ')
            return false;
    }
    const QChar ch = line.at(pos);
    while (line.size() <= pos + k)
        line.append(u' ');
    line[pos] = u' ';
    line[pos + k] = ch;
    return true;
}

bool moveThroughSpacesLeft(QString &line, int pos, int k)
{
    for (int i = 1; i <= k; ++i) {
        if (line.at(pos - i) != u' ')
            return false;
    }
    const QChar ch = line.at(pos);
    line[pos] = u' ';
    line[pos - k] = ch;
    return true;
}

// Extend an edge: the corner at `pos` moves right by k over spaces, the gap
// filled with `fill`.
bool extendEdgeRight(QString &line, int pos, int k, QChar fill)
{
    for (int i = 1; i <= k; ++i) {
        const int p = pos + i;
        if (p < line.size() && line.at(p) != u' ')
            return false;
    }
    const QChar corner = line.at(pos);
    while (line.size() <= pos + k)
        line.append(u' ');
    for (int p = pos; p < pos + k; ++p)
        line[p] = fill;
    line[pos + k] = corner;
    return true;
}

// Trim an edge: the corner at `pos` moves left by k over pure fill (never
// over a junction), leaving spaces behind so later columns stay put.
bool trimEdgeLeft(QString &line, int pos, int k)
{
    for (int i = 1; i <= k; ++i) {
        if (!isHFill(line.at(pos - i)))
            return false;
    }
    const QChar corner = line.at(pos);
    line[pos - k] = corner;
    for (int p = pos - k + 1; p <= pos; ++p)
        line[p] = u' ';
    return true;
}

// Slide an edge junction (tee/arrowhead) along its edge by swapping with
// fill characters.
bool slideAlongEdge(QString &line, int pos, int target)
{
    const int lo = qMin(pos, target), hi = qMax(pos, target);
    for (int p = lo; p <= hi; ++p) {
        if (p != pos && !isHFill(line.at(p)))
            return false;
    }
    const QChar ch = line.at(pos);
    const QChar fill = line.at(target);
    line[pos] = fill;
    line[target] = ch;
    return true;
}

// ---- box detection ----

struct Edge {
    int row = 0;
    int left = 0;   // corner column
    int right = 0;  // corner column
};

struct Wall {
    int row = 0;
    int left = 0;
    int right = 0;
};

struct Box {
    Edge top, bottom;
    QVector<Wall> walls;
};

// Corner-bounded horizontal runs on one line: corner, fill/junction span,
// corner. The closing corner of one run may open the next.
QVector<Edge> edgeRunsOn(const QString &line, int row, bool topEdges)
{
    QVector<Edge> runs;
    const auto opens = topEdges ? isTopLeft : isBottomLeft;
    const auto closes = topEdges ? isTopRight : isBottomRight;
    int i = 0;
    while (i < line.size()) {
        const QChar c = line.at(i);
        if (!(opens(c) || c == u'+')) {
            ++i;
            continue;
        }
        int j = i + 1;
        int span = 0;
        while (j < line.size()
               && (isHFill(line.at(j)) || isEdgeJunction(line.at(j)))) {
            // A '+' inside the span may close the run (ASCII corner).
            if (line.at(j) == u'+' && span >= 1)
                break;
            ++span;
            ++j;
        }
        const bool closed = j < line.size()
            && (closes(line.at(j)) || line.at(j) == u'+') && span >= 1;
        if (closed) {
            runs.append({row, i, j});
            i = j;      // shared corner may open the next run
        } else {
            i = j + 1;
        }
    }
    return runs;
}

// Nearest wall character to `ref` within kTol; -1 when absent.
int wallNear(const QString &line, int ref)
{
    int best = -1;
    for (int d = 0; d <= kTol; ++d) {
        for (int col : {ref - d, ref + d}) {
            if (col >= 0 && col < line.size() && isWall(line.at(col))) {
                best = col;
                break;
            }
        }
        if (best >= 0)
            break;
    }
    return best;
}

// Grow a box downward from a top edge: wall rows, then a bottom edge whose
// corners land within tolerance of the top corners.
bool growBox(const QStringList &lines, const Edge &top, Box *out)
{
    Box box;
    box.top = top;
    for (int row = top.row + 1; row < lines.size(); ++row) {
        const QString &line = lines.at(row);
        // Bottom edge?
        const QVector<Edge> bottoms = edgeRunsOn(line, row, false);
        bool closed = false;
        for (const Edge &b : bottoms) {
            if (qAbs(b.left - top.left) <= kTol
                && qAbs(b.right - top.right) <= kTol) {
                box.bottom = b;
                closed = true;
                break;
            }
        }
        if (closed) {
            if (box.walls.isEmpty())
                return false;      // a frame with no interior is not a box
            *out = box;
            return true;
        }
        // Otherwise both walls must be present near the top corners.
        const int wl = wallNear(line, top.left);
        const int wr = wallNear(line, top.right);
        if (wl < 0 || wr < 0 || wl >= wr)
            return false;
        box.walls.append({row, wl, wr});
    }
    return false;
}

QVector<Box> detectBoxes(const QStringList &lines)
{
    QVector<Box> boxes;
    for (int row = 0; row < lines.size(); ++row) {
        const QVector<Edge> tops = edgeRunsOn(lines.at(row), row, true);
        for (const Edge &top : tops) {
            Box box;
            if (growBox(lines, top, &box))
                boxes.append(box);
        }
    }
    return boxes;
}

// Dominant column: the most frequent value; ties resolved toward `preferHigh`.
int dominantColumn(const QVector<int> &cols, bool preferHigh)
{
    QHash<int, int> counts;
    for (int c : cols)
        ++counts[c];
    int best = cols.first(), bestCount = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount
            || (it.value() == bestCount
                && (preferHigh ? it.key() > best : it.key() < best))) {
            best = it.key();
            bestCount = it.value();
        }
    }
    return best;
}

// ---- phase 1: box edge straightening ----

// Straighten one side (left or right) of a box: align every corner and wall
// bar of that side to the dominant column. All-or-nothing per side — if any
// single move cannot be made with zero-shift swaps, the side stays as is.
bool straightenSide(QStringList &lines, const Box &box, bool rightSide)
{
    QVector<int> cols;
    cols.append(rightSide ? box.top.right : box.top.left);
    for (const Wall &w : box.walls)
        cols.append(rightSide ? w.right : w.left);
    cols.append(rightSide ? box.bottom.right : box.bottom.left);

    const int target = dominantColumn(cols, rightSide);
    bool allEqual = true;
    for (int c : cols)
        allEqual = allEqual && c == target;
    if (allEqual)
        return false;

    // Dry-run on copies, commit only if every move succeeds.
    QStringList work = lines;
    auto moveCorner = [&](const Edge &e) {
        const int pos = rightSide ? e.right : e.left;
        if (pos == target)
            return true;
        QString &line = work[e.row];
        if (rightSide) {
            const QChar fill = line.at(pos - 1);
            if (!isHFill(fill))
                return false;
            return pos < target ? extendEdgeRight(line, pos, target - pos, fill)
                                : trimEdgeLeft(line, pos, pos - target);
        }
        // Left corners: the fill sits to the right of the corner; moving
        // left swallows spaces, moving right trims fill.
        if (pos > target) {
            // Extend leftward: spaces to the left become fill.
            for (int i = 1; i <= pos - target; ++i)
                if (line.at(pos - i) != u' ')
                    return false;
            const QChar fill = line.at(pos + 1);
            if (!isHFill(fill))
                return false;
            const QChar corner = line.at(pos);
            for (int p = target + 1; p <= pos; ++p)
                line[p] = fill;
            line[target] = corner;
            return true;
        }
        // Trim rightward over pure fill.
        for (int i = 1; i <= target - pos; ++i)
            if (!isHFill(line.at(pos + i)))
                return false;
        const QChar corner = line.at(pos);
        for (int p = pos; p < target; ++p)
            line[p] = u' ';
        line[target] = corner;
        return true;
    };
    auto moveWall = [&](const Wall &w) {
        const int pos = rightSide ? w.right : w.left;
        if (pos == target)
            return true;
        QString &line = work[w.row];
        return pos < target ? moveThroughSpacesRight(line, pos, target - pos)
                            : moveThroughSpacesLeft(line, pos, pos - target);
    };

    if (!moveCorner(box.top) || !moveCorner(box.bottom))
        return false;
    for (const Wall &w : box.walls) {
        if (!moveWall(w))
            return false;
    }
    lines = work;
    return true;
}

// ---- phase 2: connector straightening ----

struct RunCell {
    int row = 0;
    int col = 0;
    bool onEdge = false;   // junction embedded in a box edge (slides), vs a
                           // free bar (swaps through spaces)
};

// Rows belonging to box edges, for distinguishing free connector cells from
// box walls and finding slideable junctions.
struct EdgeMap {
    QHash<int, QVector<Edge>> topByRow, bottomByRow;
    QVector<Box> boxes;

    bool onEdgeSpan(int row, int col) const
    {
        for (const Edge &e : topByRow.value(row))
            if (col >= e.left && col <= e.right)
                return true;
        for (const Edge &e : bottomByRow.value(row))
            if (col >= e.left && col <= e.right)
                return true;
        return false;
    }
    bool isBoxWall(int row, int col) const
    {
        for (const Box &b : boxes)
            for (const Wall &w : b.walls)
                if (w.row == row && (w.left == col || w.right == col))
                    return true;
        return false;
    }
};

EdgeMap buildEdgeMap(const QStringList &lines)
{
    EdgeMap map;
    map.boxes = detectBoxes(lines);
    for (const Box &b : map.boxes) {
        map.topByRow[b.top.row].append(b.top);
        map.bottomByRow[b.bottom.row].append(b.bottom);
    }
    return map;
}

// Find the connector cell on `row` within kRunTol of `col`: an edge junction
// on a box edge, or a free connector char outside any box span.
bool connectorNear(const QStringList &lines, const EdgeMap &map, int row,
                   int col, RunCell *out)
{
    const QString &line = lines.value(row);
    for (int d = 0; d <= kRunTol; ++d) {
        for (int c : {col - d, col + d}) {
            if (c < 0 || c >= line.size())
                continue;
            const QChar ch = line.at(c);
            if (map.onEdgeSpan(row, c)) {
                if (isEdgeJunction(ch)) {
                    *out = {row, c, true};
                    return true;
                }
                continue;      // plain fill on an edge: the run abuts here
            }
            if (map.isBoxWall(row, c))
                continue;
            if (isConnector(ch)) {
                *out = {row, c, false};
                return true;
            }
        }
    }
    return false;
}

void straightenConnectors(QStringList &lines)
{
    const EdgeMap map = buildEdgeMap(lines);
    QSet<QPair<int, int>> used;

    for (int row = 0; row < lines.size(); ++row) {
        const QString &line = lines.at(row);
        for (int col = 0; col < line.size(); ++col) {
            // A run starts at a junction on an edge, or at a free connector
            // with no connector above it.
            RunCell start{row, col, map.onEdgeSpan(row, col)};
            if (used.contains({row, col}))
                continue;
            if (start.onEdge) {
                if (!isEdgeJunction(line.at(col)))
                    continue;
            } else {
                if (!isConnector(line.at(col)) || map.isBoxWall(row, col))
                    continue;
                RunCell above;
                if (connectorNear(lines, map, row - 1, col, &above))
                    continue;   // not the top of a run
            }

            // Walk downward collecting the run.
            QVector<RunCell> run{start};
            int cur = col;
            for (int r = row + 1; r < lines.size(); ++r) {
                RunCell next;
                if (!connectorNear(lines, map, r, cur, &next))
                    break;
                run.append(next);
                cur = next.col;
                if (next.onEdge)
                    break;      // anchored into a box edge: run ends
            }
            for (const RunCell &c : run)
                used.insert({c.row, c.col});
            if (run.size() < 2)
                continue;

            QVector<int> cols;
            for (const RunCell &c : run)
                cols.append(c.col);
            const int target = dominantColumn(cols, false);
            bool allEqual = true;
            for (int c : cols)
                allEqual = allEqual && c == target;
            if (allEqual)
                continue;

            // All-or-nothing, on copies.
            QStringList work = lines;
            bool ok = true;
            for (const RunCell &c : run) {
                if (c.col == target)
                    continue;
                QString &l = work[c.row];
                if (c.onEdge)
                    ok = slideAlongEdge(l, c.col, target);
                else
                    ok = c.col < target
                        ? moveThroughSpacesRight(l, c.col, target - c.col)
                        : moveThroughSpacesLeft(l, c.col, c.col - target);
                if (!ok)
                    break;
            }
            if (ok)
                lines = work;
        }
    }
}

} // namespace

QString repair(const QString &body)
{
    if (body.size() > kRepairCapChars)
        return body;
    // Tabs shift every column after them; a body using tabs for layout is
    // left alone rather than risk repairs against a misread grid.
    if (body.contains(u'\t'))
        return body;

    QStringList lines = body.split(u'\n');

    // Phase 1: straighten box sides. Repairs are zero-shift, so detection
    // stays valid across boxes; still, detect once and repair per box.
    const QVector<Box> boxes = detectBoxes(lines);
    for (const Box &box : boxes) {
        straightenSide(lines, box, /*rightSide=*/true);
        straightenSide(lines, box, /*rightSide=*/false);
    }

    // Phase 2: straighten connector runs against the repaired geometry.
    straightenConnectors(lines);

    // Trim trailing spaces only on lines a repair touched (moves leave
    // spaces behind); untouched lines keep their bytes.
    const QStringList original = body.split(u'\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i) != original.at(i)) {
            QString &l = lines[i];
            while (l.endsWith(u' '))
                l.chop(1);
        }
    }

    const QString repaired = lines.join(u'\n');
    return repaired == body ? body : repaired;
}

} // namespace DiagramRepair
