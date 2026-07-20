// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "diagramclassifier.h"

#include <QChar>
#include <QStringList>

// The algorithm:
//
//   1. Per-line features: box-drawing strokes, arrow tokens, corner-bounded
//      horizontal runs (box "edges"), vertical boundaries, and label text.
//   2. Reject gates that hold the two closest lookalikes:
//        - Markdown / ASCII console tables (a `|---|` separator, or a contiguous
//          `+--+`/`|` grid) — the psql/MySQL shape;
//        - strong source-code signatures.
//   3. A framed region = a top edge, one or more wall lines, and a bottom edge
//      in loose vertical sequence; edges need not close into an exact rectangle
//      and may disagree in width. Side-by-side boxes on one edge line count once
//      per corner-bounded run.
//   4. Minimum gates: >= 3 non-empty lines, >= 2 lines carrying base signals,
//      the deciding signal (>= 2 separate framed regions), and a score above a
//      fixed threshold.
//
// Cross-line geometry is a tolerant bonus, never a requirement: real LLM
// diagrams misalign their box edges (in one model-generated diagram from the
// test corpus, an OPERATOR box closes at a different column than its walls),
// so exact column alignment is never demanded anywhere.

namespace {

// The confidence a positive classification must exceed. Documented constant
// so the model-generated corpus (including the deliberately-worse second
// fixture) can be recalibrated against one number. The reject/deciding gates
// carry the real discriminating power; the score is a secondary evidence
// filter that keeps a single stray box or arrow from tagging.
constexpr double kScoreThreshold = 4.0;

// ---- character classes ----

// The Unicode box-drawing block.
bool isBoxDrawing(char16_t u) { return u >= 0x2500 && u <= 0x257F; }

// A pure horizontal stroke (no junction): the light/heavy/dashed/double
// horizontals, plus ASCII '-' and '='.
bool isHStroke(char16_t u)
{
    switch (u) {
    case 0x2500: case 0x2501: case 0x2504: case 0x2505:
    case 0x2508: case 0x2509: case 0x254C: case 0x254D:
    case 0x2550: case 0x2574: case 0x2576: case 0x2578: case 0x257A:
        return true;
    default:
        return u == u'-' || u == u'=';
    }
}

// A pure vertical stroke, plus ASCII '|'.
bool isVStroke(char16_t u)
{
    switch (u) {
    case 0x2502: case 0x2503: case 0x2506: case 0x2507:
    case 0x250A: case 0x250B: case 0x2551: case 0x254E: case 0x254F:
    case 0x2575: case 0x2577: case 0x2579: case 0x257B:
        return true;
    default:
        return u == u'|';
    }
}

// A corner or junction that can bound a horizontal run: every box-drawing glyph
// that is neither a pure horizontal nor a pure vertical (corners, tees, crosses,
// in light/heavy/double/rounded weights), plus ASCII '+'.
bool isBoundary(char16_t u)
{
    if (u == u'+')
        return true;
    if (!isBoxDrawing(u))
        return false;
    return !isHStroke(u) && !isVStroke(u);
}

// Top corners (upper-left / upper-right, all weights + rounded + double).
bool isTopCorner(char16_t u)
{
    switch (u) {
    case 0x250C: case 0x250D: case 0x250E: case 0x250F:  // ┌┍┎┏
    case 0x2510: case 0x2511: case 0x2512: case 0x2513:  // ┐┑┒┓
    case 0x2552: case 0x2553: case 0x2554:               // ╒╓╔
    case 0x2555: case 0x2556: case 0x2557:               // ╕╖╗
    case 0x256D: case 0x256E:                            // ╭╮
        return true;
    default:
        return false;
    }
}

// Bottom corners (lower-left / lower-right).
bool isBottomCorner(char16_t u)
{
    switch (u) {
    case 0x2514: case 0x2515: case 0x2516: case 0x2517:  // └┕┖┗
    case 0x2518: case 0x2519: case 0x251A: case 0x251B:  // ┘┙┚┛
    case 0x2558: case 0x2559: case 0x255A:               // ╘╙╚
    case 0x255B: case 0x255C: case 0x255D:               // ╛╜╝
    case 0x2570: case 0x256F:                            // ╰╯
        return true;
    default:
        return false;
    }
}

// Arrowhead glyphs used as connectors: the geometric triangles/pointers plus
// the Unicode arrow blocks.
bool isArrowGlyph(char16_t u)
{
    if (u >= 0x2190 && u <= 0x21FF) return true;   // Arrows
    if (u >= 0x2794 && u <= 0x27BF) return true;   // Dingbat arrows
    if (u >= 0x27F0 && u <= 0x27FF) return true;   // Supplemental Arrows-A
    if (u >= 0x2900 && u <= 0x297F) return true;   // Supplemental Arrows-B
    switch (u) {
    case 0x25B2: case 0x25B3: case 0x25B4: case 0x25B6: case 0x25B7:
    case 0x25B8: case 0x25BA: case 0x25BC: case 0x25BD: case 0x25BE:
    case 0x25C0: case 0x25C1: case 0x25C2: case 0x25C4:
        return true;
    default:
        return false;
    }
}

bool isLetterOrDigit(char16_t u)
{
    return QChar::isLetterOrNumber(u);
}

// ---- per-line features ----

struct LineInfo {
    QString trimmed;
    bool nonEmpty = false;
    int boxCount = 0;             // box-drawing strokes on this line
    bool hasArrow = false;        // an arrow token/glyph
    bool hasCornerHRun = false;   // a corner/junction joined to a >=3 h-run
    int boxSegments = 0;          // corner-bounded horizontal runs (>=1 => edge)
    bool topEdge = false;         // an edge carrying top corners
    bool bottomEdge = false;      // an edge carrying bottom corners
    bool asciiEdge = false;       // a corner-bounded run drawn only with + - =
    bool hasLabel = false;        // letters/digits present
    bool isPipeWall = false;      // |...| table-wall shape
    bool isAsciiGridEdge = false; // +---+---+ table-rule shape
    bool isMarkdownSep = false;   // a |---|---| markdown separator row
    QList<int> vColumns;          // columns of vertical boundaries
};

// An ASCII arrow token anywhere in the line (kept narrow so a lone '-' or '|'
// never reads as an arrow).
bool hasAsciiArrow(const QString &s)
{
    static const char *tokens[] = { "-->", "<--", "->", "<-", "==>", "<==",
                                    "=>", "<=>", "<->", "──►", ">|", nullptr };
    for (int t = 0; tokens[t]; ++t) {
        if (s.contains(QLatin1String(tokens[t])))
            return true;
    }
    return false;
}

// A corner that can start or end a box: top/bottom corners, or ASCII '+'.
bool isCorner(char16_t u)
{
    return u == u'+' || isTopCorner(u) || isBottomCorner(u);
}

// Count corner-bounded horizontal runs on a line: an opening corner, a span of
// >=2 frame strokes (horizontals, arrowheads, and interior tees/junctions —
// which mark connection points, not box edges), then a closing corner. The
// closing corner of one run may open the next (`+---+---+`, side-by-side
// boxes), so a shared rule counts every cell; a space or label between two
// boxes breaks the run into separate segments.
int scanBoxSegments(const QString &line, bool *anyTop, bool *anyBottom,
                    bool *sawUnicodeCorner)
{
    int segments = 0;
    const int n = line.size();
    int i = 0;
    while (i < n) {
        const char16_t c = line.at(i).unicode();
        if (!isCorner(c)) {
            ++i;
            continue;
        }
        // An opening corner: extend a run of interior strokes to a closing
        // corner. Interior tees/junctions do not terminate the run.
        int j = i + 1;
        int strokeCount = 0;
        while (j < n) {
            const char16_t d = line.at(j).unicode();
            if (isCorner(d))
                break;
            if (isHStroke(d) || isArrowGlyph(d) || (isBoxDrawing(d) && !isVStroke(d)))
                ++strokeCount;
            else
                break;
            ++j;
        }
        if (j < n && strokeCount >= 2 && isCorner(line.at(j).unicode())) {
            const char16_t open = c;
            const char16_t close = line.at(j).unicode();
            ++segments;
            if (isTopCorner(open) || isTopCorner(close)) *anyTop = true;
            if (isBottomCorner(open) || isBottomCorner(close)) *anyBottom = true;
            if (isBoxDrawing(open) || isBoxDrawing(close)) *sawUnicodeCorner = true;
            // Restart from the shared closing corner so it can open the next.
            i = j;
            continue;
        }
        ++i;
    }
    return segments;
}

// A corner/junction immediately joined to a >=3 horizontal run (┌──, ├──, +--).
bool scanCornerHRun(const QString &line)
{
    const int n = line.size();
    for (int i = 0; i + 1 < n; ++i) {
        if (!isBoundary(line.at(i).unicode()))
            continue;
        int run = 0;
        int j = i + 1;
        while (j < n && isHStroke(line.at(j).unicode())) { ++run; ++j; }
        if (run >= 3)
            return true;
    }
    return false;
}

bool looksLikePipeWall(const QString &t)
{
    if (!t.startsWith(u'|') || !t.endsWith(u'|'))
        return false;
    return t.count(u'|') >= 2;
}

bool looksLikeAsciiGridEdge(const QString &t)
{
    if (t.isEmpty() || !t.startsWith(u'+') || !t.endsWith(u'+'))
        return false;
    if (t.count(u'+') < 2)
        return false;
    for (const QChar &ch : t) {
        const char16_t u = ch.unicode();
        if (u != u'+' && u != u'-' && u != u'=' && u != u':' && u != u' ')
            return false;
    }
    return true;
}

bool looksLikeMarkdownSeparator(const QString &t)
{
    if (t.count(u'-') < 3 || !t.contains(u'|'))
        return false;
    for (const QChar &ch : t) {
        const char16_t u = ch.unicode();
        if (u != u'|' && u != u'-' && u != u':' && u != u' ')
            return false;
    }
    return true;
}

LineInfo analyzeLine(const QString &raw)
{
    LineInfo info;
    info.trimmed = raw.trimmed();
    info.nonEmpty = !info.trimmed.isEmpty();
    if (!info.nonEmpty)
        return info;

    for (int i = 0; i < raw.size(); ++i) {
        const char16_t u = raw.at(i).unicode();
        if (isBoxDrawing(u))
            ++info.boxCount;
        if (isArrowGlyph(u))
            info.hasArrow = true;
        if (isLetterOrDigit(u))
            info.hasLabel = true;
        if (isVStroke(u))
            info.vColumns.append(i);
    }
    if (!info.hasArrow)
        info.hasArrow = hasAsciiArrow(raw);

    bool anyTop = false, anyBottom = false, sawUnicodeCorner = false;
    info.boxSegments = scanBoxSegments(raw, &anyTop, &anyBottom, &sawUnicodeCorner);
    info.topEdge = info.boxSegments > 0 && anyTop;
    info.bottomEdge = info.boxSegments > 0 && anyBottom;
    // An edge drawn only with + - = (no unicode corner) is ambiguous between a
    // box and a table rule; the table gate resolves it.
    info.asciiEdge = info.boxSegments > 0 && !sawUnicodeCorner;
    info.hasCornerHRun = scanCornerHRun(raw);

    info.isPipeWall = looksLikePipeWall(info.trimmed);
    info.isAsciiGridEdge = looksLikeAsciiGridEdge(info.trimmed);
    info.isMarkdownSep = looksLikeMarkdownSeparator(info.trimmed);
    return info;
}

// ---- reject gates ----

// A contiguous +--+/| grid with no gaps between framed rows (the psql/MySQL
// console shape) or a Markdown pipe-table separator.
bool looksLikeTable(const QList<LineInfo> &lines)
{
    int nonEmpty = 0;
    int gridEdges = 0;
    int gridLike = 0;
    for (const LineInfo &l : lines) {
        if (!l.nonEmpty)
            continue;
        ++nonEmpty;
        if (l.isMarkdownSep)
            return true;
        if (l.isAsciiGridEdge) {
            ++gridEdges;
            ++gridLike;
        } else if (l.isPipeWall) {
            ++gridLike;
        }
    }
    // Every non-empty line is a wall or a rule, and there are at least two
    // rules: a grid, not a drawing.
    return nonEmpty > 0 && gridLike == nonEmpty && gridEdges >= 2;
}

// A strong source-code signature: a shebang, JSON object structure, or a large
// fraction of lines ending in code punctuation. Deliberately narrow — the frame
// gate already excludes ordinary code; this is a second guard for text that
// happens to carry a few frame strokes.
bool looksLikeCode(const QList<LineInfo> &lines, const QString &content)
{
    const QString t = content.trimmed();
    if (t.startsWith(QLatin1String("#!")))
        return true;
    if ((t.startsWith(u'{') && t.endsWith(u'}'))
        || (t.startsWith(u'[') && t.endsWith(u']'))) {
        if (t.contains(QLatin1String("\":")) || t.contains(QLatin1String("\": ")))
            return true;
    }
    int codey = 0, nonEmpty = 0;
    for (const LineInfo &l : lines) {
        if (!l.nonEmpty)
            continue;
        ++nonEmpty;
        const QChar last = l.trimmed.at(l.trimmed.size() - 1);
        if (last == u';' || last == u'{' || last == u'}')
            ++codey;
    }
    return nonEmpty >= 3 && codey * 2 >= nonEmpty;  // >= half the lines
}

// ---- framed-region counting ----

// Count separate framed regions (a top edge, >= 1 wall line, a bottom edge),
// summing side-by-side boxes by their corner-bounded run count. Table-shaped
// shared rules are excluded upstream by looksLikeTable, so an ascii edge here
// simply acts as a bottom that does not immediately reopen a top.
int countFramedRegions(const QList<LineInfo> &lines)
{
    int regions = 0;
    bool openTop = false;
    int wallSince = 0;
    for (const LineInfo &l : lines) {
        if (!l.nonEmpty)
            continue;
        const bool isTop = l.topEdge || (l.asciiEdge && !openTop);
        const bool isBottom = l.bottomEdge || (l.asciiEdge && openTop);
        if (isBottom && openTop && wallSince >= 1) {
            regions += qMax(1, l.boxSegments);
            openTop = false;
            wallSince = 0;
            continue;
        }
        if (isTop && !openTop) {
            openTop = true;
            wallSince = 0;
            continue;
        }
        // A wall/content line between the open top and its bottom.
        if (openTop && (!l.vColumns.isEmpty() || l.hasLabel))
            ++wallSince;
    }
    return regions;
}

// Vertical boundaries recurring in roughly the same column region (+/- 2) across
// consecutive rows — a tolerant base signal.
int countRecurringVerticalRows(const QList<LineInfo> &lines)
{
    int rows = 0;
    QList<int> prev;
    for (const LineInfo &l : lines) {
        if (!l.nonEmpty) {
            prev.clear();
            continue;
        }
        bool matched = false;
        for (int c : l.vColumns) {
            for (int p : prev) {
                if (qAbs(c - p) <= 2) { matched = true; break; }
            }
            if (matched) break;
        }
        if (matched)
            ++rows;
        prev = l.vColumns;
    }
    return rows;
}

} // namespace

namespace DiagramClassifier {

Result classify(const QString &content)
{
    Result r;
    if (content.size() > kInspectionCapChars) {
        r.reasons << QStringLiteral("over inspection cap: not tagged");
        return r;
    }

    const QStringList rawLines = content.split(u'\n');
    QList<LineInfo> lines;
    lines.reserve(rawLines.size());
    int nonEmpty = 0;
    for (const QString &raw : rawLines) {
        LineInfo info = analyzeLine(raw);
        if (info.nonEmpty)
            ++nonEmpty;
        lines.append(info);
    }

    if (nonEmpty < 3) {
        r.reasons << QStringLiteral("fewer than 3 non-empty lines");
        return r;
    }

    if (looksLikeTable(lines)) {
        r.reasons << QStringLiteral("table signature (grid/markdown separator)");
        return r;
    }
    if (looksLikeCode(lines, content)) {
        r.reasons << QStringLiteral("source-code signature");
        return r;
    }

    // Base signals: lines that carry diagram evidence on their own.
    int baseSignalLines = 0;
    int arrowLines = 0;
    int edgeLines = 0;
    for (const LineInfo &l : lines) {
        if (!l.nonEmpty)
            continue;
        const bool base = l.boxCount > 0 || l.hasCornerHRun || l.hasArrow
                          || l.boxSegments > 0;
        if (base)
            ++baseSignalLines;
        if (l.hasArrow)
            ++arrowLines;
        if (l.boxSegments > 0)
            ++edgeLines;
    }
    if (baseSignalLines < 2) {
        r.reasons << QStringLiteral("fewer than 2 base-signal lines");
        return r;
    }

    const int regions = countFramedRegions(lines);
    const int recurringVertical = countRecurringVerticalRows(lines);

    r.reasons << QStringLiteral("regions=%1").arg(regions);
    r.reasons << QStringLiteral("edgeLines=%1").arg(edgeLines);
    r.reasons << QStringLiteral("arrowLines=%1").arg(arrowLines);
    r.reasons << QStringLiteral("recurringVertical=%1").arg(recurringVertical);

    // The deciding signal: two or more separate framed regions. A connector
    // between frames only reinforces confidence — it cannot substitute for the
    // second frame, so a single box with one outgoing arrow stays code.
    if (regions < 2) {
        r.reasons << QStringLiteral("no deciding signal (< 2 framed regions)");
        return r;
    }

    r.score = 2.0 * regions + 1.5 * arrowLines + 1.0 * edgeLines
              + 0.5 * recurringVertical;
    r.reasons << QStringLiteral("score=%1 (threshold %2)")
                     .arg(r.score).arg(kScoreThreshold);

    if (r.score <= kScoreThreshold) {
        r.reasons << QStringLiteral("score below threshold");
        return r;
    }

    r.isDiagram = true;
    r.reasons << QStringLiteral("classified as diagram");
    return r;
}

bool looksLikeDiagram(const QString &content)
{
    return classify(content).isDiagram;
}

} // namespace DiagramClassifier
