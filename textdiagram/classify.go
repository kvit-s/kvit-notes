package textdiagram

import (
	"strconv"
	"strings"
	"unicode"
	"unicode/utf8"
)

// Classify works in four steps:
//
//  1. Each line is read for box-drawing strokes, arrows, runs of edge
//     between two corners, vertical strokes and label text.
//  2. Two kinds of lookalike are rejected outright: tables (a Markdown
//     `|---|` separator, or a grid made only of `+--+` rules and `|` rows,
//     as psql and MySQL print) and source code.
//  3. Framed regions are counted: a top edge, one or more lines of wall or
//     label, then a bottom edge. The edges need not line up with each other
//     or with the walls, and boxes side by side on one edge line each count.
//  4. The body is a diagram when it has three or more non-empty lines, two
//     or more lines with some diagram evidence, two or more framed regions,
//     and a score above scoreThreshold.
//
// No step asks for columns to line up exactly, because the diagrams language
// models write often do not: in one diagram of the test corpus a box closes
// at a different column from its walls.

// InspectionCapChars is the largest fence body Classify reads, in UTF-16
// code units. A larger body is never a diagram.
const InspectionCapChars = 256 * 1024

// scoreThreshold is the score a diagram has to exceed. The rejection steps
// and the count of framed regions decide most bodies; the score keeps a lone
// box or arrow from being enough.
const scoreThreshold = 4.0

// Classification is what Classify decided about a fence body.
type Classification struct {
	// IsDiagram is the decision.
	IsDiagram bool
	// Score is the weighted evidence compared against the threshold, 4. It
	// is 0 when the body was rejected before it was scored.
	Score float64
	// Reasons are the evidence found and the outcome of each step, in the
	// order they were decided, for tests and diagnostics.
	Reasons []string
}

// isBoxDrawing reports whether u is in the Unicode box-drawing block.
func isBoxDrawing(u rune) bool { return u >= 0x2500 && u <= 0x257F }

// isHStroke reports whether u is a horizontal stroke with no junction: the
// light, heavy, dashed and double horizontals and their halves, and ASCII '-'
// and '='.
func isHStroke(u rune) bool {
	switch u {
	case 0x2500, 0x2501, 0x2504, 0x2505,
		0x2508, 0x2509, 0x254C, 0x254D,
		0x2550, 0x2574, 0x2576, 0x2578, 0x257A:
		return true
	}
	return u == '-' || u == '='
}

// isVStroke reports whether u is a vertical stroke with no junction, or
// ASCII '|'.
func isVStroke(u rune) bool {
	switch u {
	case 0x2502, 0x2503, 0x2506, 0x2507,
		0x250A, 0x250B, 0x2551, 0x254E, 0x254F,
		0x2575, 0x2577, 0x2579, 0x257B:
		return true
	}
	return u == '|'
}

// isBoundary reports whether u can end a horizontal run: every box-drawing
// character that is neither a plain horizontal nor a plain vertical
// (corners, tees and crosses in every weight), and ASCII '+'.
func isBoundary(u rune) bool {
	if u == '+' {
		return true
	}
	if !isBoxDrawing(u) {
		return false
	}
	return !isHStroke(u) && !isVStroke(u)
}

// isTopCorner reports whether u is a top-left or top-right corner, light,
// heavy, double or rounded.
func isTopCorner(u rune) bool {
	switch u {
	case 0x250C, 0x250D, 0x250E, 0x250F, // ┌┍┎┏
		0x2510, 0x2511, 0x2512, 0x2513, // ┐┑┒┓
		0x2552, 0x2553, 0x2554, // ╒╓╔
		0x2555, 0x2556, 0x2557, // ╕╖╗
		0x256D, 0x256E: // ╭╮
		return true
	}
	return false
}

// isBottomCorner reports whether u is a bottom-left or bottom-right corner.
func isBottomCorner(u rune) bool {
	switch u {
	case 0x2514, 0x2515, 0x2516, 0x2517, // └┕┖┗
		0x2518, 0x2519, 0x251A, 0x251B, // ┘┙┚┛
		0x2558, 0x2559, 0x255A, // ╘╙╚
		0x255B, 0x255C, 0x255D, // ╛╜╝
		0x2570, 0x256F: // ╰╯
		return true
	}
	return false
}

// isArrowGlyph reports whether u is an arrowhead used as a connector: the
// geometric triangles and pointers, and the Unicode arrow blocks.
func isArrowGlyph(u rune) bool {
	switch {
	case u >= 0x2190 && u <= 0x21FF: // Arrows
		return true
	case u >= 0x2794 && u <= 0x27BF: // Dingbat arrows
		return true
	case u >= 0x27F0 && u <= 0x27FF: // Supplemental Arrows-A
		return true
	case u >= 0x2900 && u <= 0x297F: // Supplemental Arrows-B
		return true
	}
	switch u {
	case 0x25B2, 0x25B3, 0x25B4, 0x25B6, 0x25B7,
		0x25B8, 0x25BA, 0x25BC, 0x25BD, 0x25BE,
		0x25C0, 0x25C1, 0x25C2, 0x25C4:
		return true
	}
	return false
}

// isLetterOrDigit reports a letter or a number of the Basic Multilingual
// Plane. A character beyond that plane counts as neither.
func isLetterOrDigit(u rune) bool {
	return u <= 0xFFFF && (unicode.IsLetter(u) || unicode.IsNumber(u))
}

// lineInfo is what one line of the body shows.
type lineInfo struct {
	trimmed         string
	nonEmpty        bool
	boxCount        int   // box-drawing characters on the line
	hasArrow        bool  // an arrow character or an ASCII arrow
	hasCornerHRun   bool  // a corner or junction followed by 3 or more horizontals
	boxSegments     int   // runs of edge between two corners; 1 or more makes an edge
	topEdge         bool  // an edge with a top corner
	bottomEdge      bool  // an edge with a bottom corner
	asciiEdge       bool  // an edge drawn only with + - =
	hasLabel        bool  // letters or digits
	isPipeWall      bool  // the |...| shape of a table row
	isAsciiGridEdge bool  // the +---+---+ shape of a table rule
	isMarkdownSep   bool  // a |---|---| Markdown table separator
	vColumns        []int // columns of the vertical strokes, ascending
}

// asciiArrows are the ASCII arrows a line may hold. None of them is a single
// character, so a lone '-' or '|' is never an arrow.
var asciiArrows = []string{"-->", "<--", "->", "<-", "==>", "<==",
	"=>", "<=>", "<->", "──►", ">|"}

func hasASCIIArrow(s string) bool {
	for _, a := range asciiArrows {
		if strings.Contains(s, a) {
			return true
		}
	}
	return false
}

// isCorner reports whether u can start or end a box's edge: a top or bottom
// corner, or ASCII '+'.
func isCorner(u rune) bool { return u == '+' || isTopCorner(u) || isBottomCorner(u) }

// scanBoxSegments counts the runs of edge on a line: a corner, two or more
// frame strokes (horizontals, arrowheads, and tees and crosses, which mark
// where a connector joins rather than end the edge), then a corner. The
// closing corner of one run may open the next, so a rule shared by boxes side
// by side (`+---+---+`) counts once per box; a space or a label between two
// boxes separates them. It also reports whether any run had a top corner, a
// bottom corner, or a box-drawing corner at either end.
func scanBoxSegments(line []rune) (segments int, anyTop, anyBottom, sawUnicodeCorner bool) {
	n := len(line)
	i := 0
	for i < n {
		c := line[i]
		if !isCorner(c) {
			i++
			continue
		}
		// An opening corner: extend a run of strokes to a closing corner.
		// Tees and crosses inside do not end the run.
		j := i + 1
		strokes := 0
		for j < n {
			d := line[j]
			if isCorner(d) {
				break
			}
			if isHStroke(d) || isArrowGlyph(d) || (isBoxDrawing(d) && !isVStroke(d)) {
				strokes++
			} else {
				break
			}
			j++
		}
		if j < n && strokes >= 2 && isCorner(line[j]) {
			open, close := c, line[j]
			segments++
			if isTopCorner(open) || isTopCorner(close) {
				anyTop = true
			}
			if isBottomCorner(open) || isBottomCorner(close) {
				anyBottom = true
			}
			if isBoxDrawing(open) || isBoxDrawing(close) {
				sawUnicodeCorner = true
			}
			// The closing corner may open the next run.
			i = j
			continue
		}
		i++
	}
	return segments, anyTop, anyBottom, sawUnicodeCorner
}

// scanCornerHRun reports whether a corner or junction on the line is
// followed straight away by three or more horizontals (┌──, ├──, +--).
func scanCornerHRun(line []rune) bool {
	n := len(line)
	for i := 0; i+1 < n; i++ {
		if !isBoundary(line[i]) {
			continue
		}
		run := 0
		for j := i + 1; j < n && isHStroke(line[j]); j++ {
			run++
		}
		if run >= 3 {
			return true
		}
	}
	return false
}

func looksLikePipeWall(t string) bool {
	if !strings.HasPrefix(t, "|") || !strings.HasSuffix(t, "|") {
		return false
	}
	return strings.Count(t, "|") >= 2
}

func looksLikeASCIIGridEdge(t string) bool {
	if t == "" || !strings.HasPrefix(t, "+") || !strings.HasSuffix(t, "+") {
		return false
	}
	if strings.Count(t, "+") < 2 {
		return false
	}
	for _, u := range t {
		if u != '+' && u != '-' && u != '=' && u != ':' && u != ' ' {
			return false
		}
	}
	return true
}

func looksLikeMarkdownSeparator(t string) bool {
	if strings.Count(t, "-") < 3 || !strings.Contains(t, "|") {
		return false
	}
	for _, u := range t {
		if u != '|' && u != '-' && u != ':' && u != ' ' {
			return false
		}
	}
	return true
}

// trimSpace is s without the white space unicode.IsSpace reports at either
// end.
func trimSpace(s string) string { return strings.TrimFunc(s, unicode.IsSpace) }

func analyzeLine(raw string) lineInfo {
	var info lineInfo
	info.trimmed = trimSpace(raw)
	info.nonEmpty = info.trimmed != ""
	if !info.nonEmpty {
		return info
	}

	line := []rune(raw)
	for i, u := range line {
		if isBoxDrawing(u) {
			info.boxCount++
		}
		if isArrowGlyph(u) {
			info.hasArrow = true
		}
		if isLetterOrDigit(u) {
			info.hasLabel = true
		}
		if isVStroke(u) {
			info.vColumns = append(info.vColumns, i)
		}
	}
	if !info.hasArrow {
		info.hasArrow = hasASCIIArrow(raw)
	}

	segments, anyTop, anyBottom, sawUnicodeCorner := scanBoxSegments(line)
	info.boxSegments = segments
	info.topEdge = segments > 0 && anyTop
	info.bottomEdge = segments > 0 && anyBottom
	// An edge drawn only with + - = could be a box or a table rule; the
	// table check decides.
	info.asciiEdge = segments > 0 && !sawUnicodeCorner
	info.hasCornerHRun = scanCornerHRun(line)

	info.isPipeWall = looksLikePipeWall(info.trimmed)
	info.isAsciiGridEdge = looksLikeASCIIGridEdge(info.trimmed)
	info.isMarkdownSep = looksLikeMarkdownSeparator(info.trimmed)
	return info
}

// looksLikeTable reports a Markdown table separator, or a grid of `+--+`
// rules and `|` rows with nothing else between them, the shape psql and
// MySQL print.
func looksLikeTable(lines []lineInfo) bool {
	nonEmpty, gridEdges, gridLike := 0, 0, 0
	for _, l := range lines {
		if !l.nonEmpty {
			continue
		}
		nonEmpty++
		if l.isMarkdownSep {
			return true
		}
		if l.isAsciiGridEdge {
			gridEdges++
			gridLike++
		} else if l.isPipeWall {
			gridLike++
		}
	}
	// Every non-empty line is a table row or a rule, and there are at least
	// two rules: a grid rather than a drawing.
	return nonEmpty > 0 && gridLike == nonEmpty && gridEdges >= 2
}

// looksLikeCode reports a clear sign of source code: a shebang, a JSON
// object or array, or at least half the non-empty lines ending in ';', '{'
// or '}'. It is kept narrow because the count of framed regions already
// rejects ordinary code; this is a second check for text that happens to
// have a few frame strokes.
func looksLikeCode(lines []lineInfo, content string) bool {
	t := trimSpace(content)
	if strings.HasPrefix(t, "#!") {
		return true
	}
	if (strings.HasPrefix(t, "{") && strings.HasSuffix(t, "}")) ||
		(strings.HasPrefix(t, "[") && strings.HasSuffix(t, "]")) {
		if strings.Contains(t, "\":") {
			return true
		}
	}
	codey, nonEmpty := 0, 0
	for _, l := range lines {
		if !l.nonEmpty {
			continue
		}
		nonEmpty++
		last, _ := utf8.DecodeLastRuneInString(l.trimmed)
		if last == ';' || last == '{' || last == '}' {
			codey++
		}
	}
	return nonEmpty >= 3 && codey*2 >= nonEmpty
}

// countFramedRegions counts the separate framed regions: a top edge, one or
// more lines of wall or label, and a bottom edge. A bottom edge with boxes
// side by side counts once per run of edge on it. looksLikeTable has already
// rejected grids of shared rules, so an ASCII edge here opens a region when
// none is open and closes the open one otherwise.
func countFramedRegions(lines []lineInfo) int {
	regions := 0
	openTop := false
	wallSince := 0
	for _, l := range lines {
		if !l.nonEmpty {
			continue
		}
		isTop := l.topEdge || (l.asciiEdge && !openTop)
		isBottom := l.bottomEdge || (l.asciiEdge && openTop)
		if isBottom && openTop && wallSince >= 1 {
			regions += max(1, l.boxSegments)
			openTop = false
			wallSince = 0
			continue
		}
		if isTop && !openTop {
			openTop = true
			wallSince = 0
			continue
		}
		// A wall or label line between the open top and its bottom.
		if openTop && (len(l.vColumns) > 0 || l.hasLabel) {
			wallSince++
		}
	}
	return regions
}

// countRecurringVerticalRows counts the lines with a vertical stroke within
// two columns of one on the line before. Both lines' columns are ascending,
// so one pass merging the two lists finds whether any pair is close enough.
// Comparing every column with every column instead is quadratic: two rows of
// 50,000 strokes with no column in common, which fit inside the inspection
// cap, took 11.5 seconds that way.
func countRecurringVerticalRows(lines []lineInfo) int {
	const columnTolerance = 2
	rows := 0
	var prev []int
	for _, l := range lines {
		if !l.nonEmpty {
			prev = nil
			continue
		}
		matched := false
		i, j := 0, 0
		for i < len(l.vColumns) && j < len(prev) {
			delta := l.vColumns[i] - prev[j]
			if delta >= -columnTolerance && delta <= columnTolerance {
				matched = true
				break
			}
			// Advance whichever side is behind; it cannot pair with anything
			// the other side has already passed.
			if delta > 0 {
				j++
			} else {
				i++
			}
		}
		if matched {
			rows++
		}
		prev = l.vColumns
	}
	return rows
}

// formatNumber writes f with six significant digits, in exponent notation
// when the exponent is below -4 or at least 6 and plainly otherwise.
func formatNumber(f float64) string { return strconv.FormatFloat(f, 'g', 6, 64) }

// Classify decides whether the body of a code fence, as it stands between
// the fence lines, is a character diagram, the kind of drawing language
// models often write, rather than code, a table, a directory listing, a
// stack trace or prose. It is written to miss a diagram rather than to tag
// something that is not one, because a missed diagram still shows as code
// while a wrong tag is written into the note. So it asks for the geometry of
// a diagram, two or more framed regions, and not only for box-drawing
// characters, which a directory listing has too. Its time is linear in the
// length of the body.
func Classify(content string) Classification {
	var r Classification
	if utf16Len(content) > InspectionCapChars {
		r.Reasons = append(r.Reasons, "over inspection cap: not tagged")
		return r
	}

	rawLines := strings.Split(content, "\n")
	lines := make([]lineInfo, 0, len(rawLines))
	nonEmpty := 0
	for _, raw := range rawLines {
		info := analyzeLine(raw)
		if info.nonEmpty {
			nonEmpty++
		}
		lines = append(lines, info)
	}

	if nonEmpty < 3 {
		r.Reasons = append(r.Reasons, "fewer than 3 non-empty lines")
		return r
	}
	if looksLikeTable(lines) {
		r.Reasons = append(r.Reasons, "table signature (grid/markdown separator)")
		return r
	}
	if looksLikeCode(lines, content) {
		r.Reasons = append(r.Reasons, "source-code signature")
		return r
	}

	// Lines that show some diagram evidence on their own.
	baseSignalLines, arrowLines, edgeLines := 0, 0, 0
	for _, l := range lines {
		if !l.nonEmpty {
			continue
		}
		if l.boxCount > 0 || l.hasCornerHRun || l.hasArrow || l.boxSegments > 0 {
			baseSignalLines++
		}
		if l.hasArrow {
			arrowLines++
		}
		if l.boxSegments > 0 {
			edgeLines++
		}
	}
	if baseSignalLines < 2 {
		r.Reasons = append(r.Reasons, "fewer than 2 base-signal lines")
		return r
	}

	regions := countFramedRegions(lines)
	recurringVertical := countRecurringVerticalRows(lines)

	r.Reasons = append(r.Reasons,
		"regions="+strconv.Itoa(regions),
		"edgeLines="+strconv.Itoa(edgeLines),
		"arrowLines="+strconv.Itoa(arrowLines),
		"recurringVertical="+strconv.Itoa(recurringVertical))

	// Two or more framed regions decide it. A connector between frames adds
	// to the score but cannot stand in for the second frame, so one box with
	// an arrow leaving it stays code.
	if regions < 2 {
		r.Reasons = append(r.Reasons, "no deciding signal (< 2 framed regions)")
		return r
	}

	r.Score = 2.0*float64(regions) + 1.5*float64(arrowLines) + 1.0*float64(edgeLines) +
		0.5*float64(recurringVertical)
	r.Reasons = append(r.Reasons,
		"score="+formatNumber(r.Score)+" (threshold "+formatNumber(scoreThreshold)+")")

	if r.Score <= scoreThreshold {
		r.Reasons = append(r.Reasons, "score below threshold")
		return r
	}

	r.IsDiagram = true
	r.Reasons = append(r.Reasons, "classified as diagram")
	return r
}

// LooksLikeDiagram is Classify's decision alone.
func LooksLikeDiagram(content string) bool { return Classify(content).IsDiagram }
