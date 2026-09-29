package textdiagram

import (
	"slices"
	"strings"
)

// This file is the app's diagramrepair.cpp. Repair works in two phases:
//
//  1. Boxes are found (a top edge, lines with a side bar near each of its
// corners, and a bottom edge whose corners are near the top's), and each
// side of each box is moved onto the column most of its corners and bars
// already use.
//  2. Connectors are found (a tee or arrowhead on a box's edge, or a free
// vertical bar, followed down the rows while each next cell is within
// two columns), and each is moved onto the column most of its cells use.
//
// A side or a connector is moved whole or not at all: when one of its cells
// cannot be moved without disturbing something else, it is left as it was.

// RepairCapChars is the largest fence body Repair changes, in UTF-16 code
// units as the app counts them; a larger body is returned unchanged. It
// is the same size as InspectionCapChars.
const RepairCapChars = 256 * 1024

// edgeTolerance is how far a side bar or corner may be from the column of
// its box's corner and still count as that box's. The flaws in the app's
// corpus of model-written diagrams are one to three columns; anything
// farther is taken to be drawn that way on purpose.
const edgeTolerance = 3

// runTolerance is how far a connector may jog sideways from one row to the
// next and still count as one connector.
const runTolerance = 2

// runeAt is line[i], or 0 when i is outside the line. The code reads one
// past the end of a line in slideAlongEdge, when a connector's column is
// beyond the end of the edge's line: string::at then returns the string's
// terminating 0 in the app's release builds (a debug build stops on an
// assertion), which is not fill, so the slide is refused. runeAt gives the
// same 0, and the same answer.
func runeAt(line []rune, i int) rune {
	if i < 0 || i >= len(line) {
		return 0
	}
	return line[i]
}

// The edits below each move one character without moving any other column
// on the line: the character swaps places with spaces or with edge fill.
// Each returns false, and leaves the line as it was, when the cells it
// would pass hold something it must not overwrite.

// moveThroughSpacesRight moves the character at pos right by k over spaces,
// growing the line when the new column is past its end.
func moveThroughSpacesRight(line *[]rune, pos, k int) bool {
	l := *line
	for i := 1; i <= k; i++ {
		if p := pos + i; p < len(l) && l[p] != ' ' {
			return false
		}
	}
	ch := l[pos]
	for len(l) <= pos+k {
		l = append(l, ' ')
	}
	l[pos] = ' '
	l[pos+k] = ch
	*line = l
	return true
}

// moveThroughSpacesLeft moves the character at pos left by k over spaces.
func moveThroughSpacesLeft(line []rune, pos, k int) bool {
	for i := 1; i <= k; i++ {
		if runeAt(line, pos-i) != ' ' {
			return false
		}
	}
	ch := line[pos]
	line[pos] = ' '
	line[pos-k] = ch
	return true
}

// extendEdgeRight moves the corner at pos right by k over spaces and fills
// the gap it leaves with fill, making the edge longer.
func extendEdgeRight(line *[]rune, pos, k int, fill rune) bool {
	l := *line
	for i := 1; i <= k; i++ {
		if p := pos + i; p < len(l) && l[p] != ' ' {
			return false
		}
	}
	corner := l[pos]
	for len(l) <= pos+k {
		l = append(l, ' ')
	}
	for p := pos; p < pos+k; p++ {
		l[p] = fill
	}
	l[pos+k] = corner
	*line = l
	return true
}

// trimEdgeLeft moves the corner at pos left by k over edge fill, never over
// a junction, and leaves spaces behind it so the columns after it stay put.
func trimEdgeLeft(line []rune, pos, k int) bool {
	for i := 1; i <= k; i++ {
		if !IsHFill(runeAt(line, pos-i)) {
			return false
		}
	}
	corner := line[pos]
	line[pos-k] = corner
	for p := pos - k + 1; p <= pos; p++ {
		line[p] = ' '
	}
	return true
}

// slideAlongEdge moves the junction or arrowhead at pos to target along its
// edge, swapping it with the fill there.
func slideAlongEdge(line []rune, pos, target int) bool {
	lo, hi := min(pos, target), max(pos, target)
	for p := lo; p <= hi; p++ {
		if p != pos && !IsHFill(runeAt(line, p)) {
			return false
		}
	}
	ch, fill := line[pos], line[target]
	line[pos] = fill
	line[target] = ch
	return true
}

// edge is a run of a box's top or bottom edge: its row and the columns of
// its two corners.
type edge struct{ row, left, right int }

// wall is a line inside a box: its row and the columns of its two side bars.
type wall struct{ row, left, right int }

type box struct {
	top, bottom edge
	walls       []wall
}

// edgeRunsOn finds the runs of top edges (or bottom edges) on a line: a
// corner, fill and junctions, then a corner. The closing corner of one run
// may open the next.
func edgeRunsOn(line []rune, row int, topEdges bool) []edge {
	opens, closes := IsBottomLeft, IsBottomRight
	if topEdges {
		opens, closes = IsTopLeft, IsTopRight
	}
	var runs []edge
	i := 0
	for i < len(line) {
		c := line[i]
		if !(opens(c) || c == '+') {
			i++
			continue
		}
		j := i + 1
		span := 0
		for j < len(line) && (IsHFill(line[j]) || IsEdgeJunction(line[j])) {
			// A '+' inside the run may close it, as an ASCII corner.
			if line[j] == '+' && span >= 1 {
				break
			}
			span++
			j++
		}
		closed := j < len(line) && (closes(line[j]) || line[j] == '+') && span >= 1
		if closed {
			runs = append(runs, edge{row, i, j})
			i = j // the shared corner may open the next run
		} else {
			i = j + 1
		}
	}
	return runs
}

// wallNear is the column of the side bar nearest to ref, looking at most
// edgeTolerance columns either way and to the left first, or -1 when there
// is none.
func wallNear(line []rune, ref int) int {
	for d := 0; d <= edgeTolerance; d++ {
		for _, col := range [2]int{ref - d, ref + d} {
			if col >= 0 && col < len(line) && IsWall(line[col]) {
				return col
			}
		}
	}
	return -1
}

// growBox follows a top edge down: lines with a side bar near each of its
// corners, then a bottom edge whose corners are near the top's.
func growBox(lines [][]rune, top edge) (box, bool) {
	b := box{top: top}
	for row := top.row + 1; row < len(lines); row++ {
		line := lines[row]
		for _, bottom := range edgeRunsOn(line, row, false) {
			if abs(bottom.left-top.left) <= edgeTolerance &&
				abs(bottom.right-top.right) <= edgeTolerance {
				if len(b.walls) == 0 {
					return box{}, false // a frame with nothing inside is not a box
				}
				b.bottom = bottom
				return b, true
			}
		}
		// Otherwise both side bars have to be there, near the top's corners.
		wl, wr := wallNear(line, top.left), wallNear(line, top.right)
		if wl < 0 || wr < 0 || wl >= wr {
			return box{}, false
		}
		b.walls = append(b.walls, wall{row, wl, wr})
	}
	return box{}, false
}

func abs(n int) int {
	if n < 0 {
		return -n
	}
	return n
}

func detectBoxes(lines [][]rune) []box {
	var boxes []box
	for row, line := range lines {
		for _, top := range edgeRunsOn(line, row, true) {
			if b, ok := growBox(lines, top); ok {
				boxes = append(boxes, b)
			}
		}
	}
	return boxes
}

// dominantColumn is the column that occurs most often in cols; of columns
// occurring equally often, the highest when preferHigh and the lowest
// otherwise.
func dominantColumn(cols []int, preferHigh bool) int {
	counts := map[int]int{}
	for _, c := range cols {
		counts[c]++
	}
	best, bestCount := cols[0], 0
	for col, n := range counts {
		if n > bestCount || (n == bestCount && (preferHigh && col > best || !preferHigh && col < best)) {
			best, bestCount = col, n
		}
	}
	return best
}

// allAt reports whether every column in cols is target.
func allAt(cols []int, target int) bool {
	for _, c := range cols {
		if c != target {
			return false
		}
	}
	return true
}

// editRow applies f to a copy of work[row] and keeps the copy when f
// succeeds, so that lines shared with the caller are never changed by an
// attempt that is later abandoned.
func editRow(work [][]rune, row int, f func(line *[]rune) bool) bool {
	line := slices.Clone(work[row])
	if !f(&line) {
		return false
	}
	work[row] = line
	return true
}

// straightenSide moves one side of a box (its left or its right corners and
// side bars) onto the column most of them already use. When any one of them
// cannot be moved, the side is left as it was. It reports whether it changed
// lines.
func straightenSide(lines [][]rune, b box, rightSide bool) bool {
	side := func(left, right int) int {
		if rightSide {
			return right
		}
		return left
	}
	cols := []int{side(b.top.left, b.top.right)}
	for _, w := range b.walls {
		cols = append(cols, side(w.left, w.right))
	}
	cols = append(cols, side(b.bottom.left, b.bottom.right))

	target := dominantColumn(cols, rightSide)
	if allAt(cols, target) {
		return false
	}

	// Every move is made on a copy, which replaces lines only when all of
	// them succeed.
	work := slices.Clone(lines)
	moveCorner := func(e edge) bool {
		pos := side(e.left, e.right)
		if pos == target {
			return true
		}
		return editRow(work, e.row, func(line *[]rune) bool {
			l := *line
			if rightSide {
				fill := runeAt(l, pos-1)
				if !IsHFill(fill) {
					return false
				}
				if pos < target {
					return extendEdgeRight(line, pos, target-pos, fill)
				}
				return trimEdgeLeft(l, pos, pos-target)
			}
			// A left corner has its fill to the right: moving left turns
			// the spaces it passes into fill, and moving right trims fill.
			if pos > target {
				for i := 1; i <= pos-target; i++ {
					if runeAt(l, pos-i) != ' ' {
						return false
					}
				}
				fill := runeAt(l, pos+1)
				if !IsHFill(fill) {
					return false
				}
				corner := l[pos]
				for p := target + 1; p <= pos; p++ {
					l[p] = fill
				}
				l[target] = corner
				return true
			}
			for i := 1; i <= target-pos; i++ {
				if !IsHFill(runeAt(l, pos+i)) {
					return false
				}
			}
			corner := l[pos]
			for p := pos; p < target; p++ {
				l[p] = ' '
			}
			l[target] = corner
			return true
		})
	}
	moveWall := func(w wall) bool {
		pos := side(w.left, w.right)
		if pos == target {
			return true
		}
		return editRow(work, w.row, func(line *[]rune) bool {
			if pos < target {
				return moveThroughSpacesRight(line, pos, target-pos)
			}
			return moveThroughSpacesLeft(*line, pos, pos-target)
		})
	}

	if !moveCorner(b.top) || !moveCorner(b.bottom) {
		return false
	}
	for _, w := range b.walls {
		if !moveWall(w) {
			return false
		}
	}
	copy(lines, work)
	return true
}

// runCell is one cell of a connector.
type runCell struct {
	row, col int
	// onEdge is whether the cell is a junction inside a box's edge, which
	// slides along the edge, rather than a free bar, which swaps with spaces.
	onEdge bool
}

// edgeMap is where the boxes' edges and side bars are, to tell a free
// connector cell from a box's side bar and to find the junctions a
// connector may slide.
type edgeMap struct {
	topByRow, bottomByRow map[int][]edge
	wallsByRow            map[int][]wall
}

func (m *edgeMap) onEdgeSpan(row, col int) bool {
	for _, e := range m.topByRow[row] {
		if col >= e.left && col <= e.right {
			return true
		}
	}
	for _, e := range m.bottomByRow[row] {
		if col >= e.left && col <= e.right {
			return true
		}
	}
	return false
}

func (m *edgeMap) isBoxWall(row, col int) bool {
	for _, w := range m.wallsByRow[row] {
		if w.left == col || w.right == col {
			return true
		}
	}
	return false
}

// buildEdgeMap finds the boxes in lines. The app searches every box's
// side bars for each cell; this keeps them by row, which gives the same
// answers without reading every box for every cell.
func buildEdgeMap(lines [][]rune) *edgeMap {
	m := &edgeMap{
		topByRow:    map[int][]edge{},
		bottomByRow: map[int][]edge{},
		wallsByRow:  map[int][]wall{},
	}
	for _, b := range detectBoxes(lines) {
		m.topByRow[b.top.row] = append(m.topByRow[b.top.row], b.top)
		m.bottomByRow[b.bottom.row] = append(m.bottomByRow[b.bottom.row], b.bottom)
		for _, w := range b.walls {
			m.wallsByRow[w.row] = append(m.wallsByRow[w.row], w)
		}
	}
	return m
}

// connectorNear finds the connector cell on row within runTolerance columns
// of col, nearest first and to the left first: a junction or arrowhead on a
// box's edge, or a connector character outside every box's edge and side
// bars.
func connectorNear(lines [][]rune, m *edgeMap, row, col int) (runCell, bool) {
	var line []rune
	if row >= 0 && row < len(lines) {
		line = lines[row]
	}
	for d := 0; d <= runTolerance; d++ {
		for _, c := range [2]int{col - d, col + d} {
			if c < 0 || c >= len(line) {
				continue
			}
			ch := line[c]
			if m.onEdgeSpan(row, c) {
				if IsEdgeJunction(ch) {
					return runCell{row, c, true}, true
				}
				continue // plain fill on an edge: the connector meets it here
			}
			if m.isBoxWall(row, c) {
				continue
			}
			if IsConnector(ch) {
				return runCell{row, c, false}, true
			}
		}
	}
	return runCell{}, false
}

// straightenConnectors lines every connector up on the column most of its
// cells use. The boxes are found once, after phase 1 has straightened them.
func straightenConnectors(lines [][]rune) {
	m := buildEdgeMap(lines)
	used := map[[2]int]bool{}

	for row := 0; row < len(lines); row++ {
		// lines[row] is read again for every column, because a connector
		// straightened below replaces the row.
		for col := 0; col < len(lines[row]); col++ {
			// A connector starts at a junction on an edge, or at a free
			// connector character with none above it.
			start := runCell{row, col, m.onEdgeSpan(row, col)}
			if used[[2]int{row, col}] {
				continue
			}
			if start.onEdge {
				if !IsEdgeJunction(lines[row][col]) {
					continue
				}
			} else {
				if !IsConnector(lines[row][col]) || m.isBoxWall(row, col) {
					continue
				}
				if _, ok := connectorNear(lines, m, row-1, col); ok {
					continue // not the top of a connector
				}
			}

			// Follow the connector down.
			run := []runCell{start}
			cur := col
			for r := row + 1; r < len(lines); r++ {
				next, ok := connectorNear(lines, m, r, cur)
				if !ok {
					break
				}
				run = append(run, next)
				cur = next.col
				if next.onEdge {
					break // it ends where it meets a box's edge
				}
			}
			for _, c := range run {
				used[[2]int{c.row, c.col}] = true
			}
			if len(run) < 2 {
				continue
			}

			cols := make([]int, len(run))
			for i, c := range run {
				cols[i] = c.col
			}
			target := dominantColumn(cols, false)
			if allAt(cols, target) {
				continue
			}

			// All or nothing, on a copy.
			work := slices.Clone(lines)
			ok := true
			for _, c := range run {
				if c.col == target {
					continue
				}
				ok = editRow(work, c.row, func(line *[]rune) bool {
					switch {
					case c.onEdge:
						return slideAlongEdge(*line, c.col, target)
					case c.col < target:
						return moveThroughSpacesRight(line, c.col, target-c.col)
					default:
						return moveThroughSpacesLeft(*line, c.col, c.col-target)
					}
				})
				if !ok {
					break
				}
			}
			if ok {
				copy(lines, work)
			}
		}
	}
}

// Repair straightens a character diagram: the body of a fence tagged
// `diagram`, `text-diagram` or `ascii-diagram`, as it stands between the
// fence lines. Diagrams written by language models often have small flaws,
// such as a box's top-right corner a column or two short of its side bars,
// or a connector that jogs sideways between rows; Repair moves those
// characters onto the column the rest of their box or connector uses.
//
// Every move swaps a side bar with the spaces next to it, extends or trims
// an edge by moving its corner through fill, or slides a tee along its edge,
// so no character to the right of a repair changes column and no label text
// is changed. Anything the rules cannot fix this way is left as written.
// Repairing a repaired body usually changes nothing, and the tests check
// that on their corpus; where boxes and connectors crowd together, a second
// pass can still move a character the first pass made room for. A body with
// a tab, which moves every column after it, or longer than RepairCapChars is
// returned as it is.
func Repair(body string) string {
	if utf16Len(body) > RepairCapChars {
		return body
	}
	if strings.ContainsRune(body, '\t') {
		return body
	}

	original := strings.Split(body, "\n")
	before := make([][]rune, len(original))
	lines := make([][]rune, len(original))
	for i, l := range original {
		before[i] = []rune(l)
		lines[i] = before[i]
	}

	// Phase 1: the boxes' sides. A repair moves no other column, so the
	// boxes found before the first repair are still where they were.
	for _, b := range detectBoxes(lines) {
		straightenSide(lines, b, true)
		straightenSide(lines, b, false)
	}

	// Phase 2: the connectors, against the straightened boxes.
	straightenConnectors(lines)

	// A line a repair changed loses its trailing spaces, which a move can
	// leave behind; every other line keeps its bytes.
	out := make([]string, len(lines))
	for i, l := range lines {
		if slices.Equal(l, before[i]) {
			out[i] = original[i]
		} else {
			out[i] = strings.TrimRight(string(l), " ")
		}
	}
	return strings.Join(out, "\n")
}
