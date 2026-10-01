package textdiagram

// FromScene: "Copy as text" on a rendered Mermaid diagram. It reads the same
// laid-out diagram.Scene the editor draws, so what is copied matches what is
// shown, and draws it on a Canvas, which uses only the characters Repair
// recognizes, so Repair leaves the text unchanged and Classify takes it for a
// character diagram.
// Flowcharts and sequence diagrams come out best. Class, state and ER
// diagrams go through the same drawing with simpler markers: a UML head is
// △, ◇ or o, and a crow's foot is < > ^ or v by the way its line runs.
//
// The pixel scene is cut into cells 9 pixels wide and 18 high, about the
// shape of a character, chosen against the layout's 14-pixel font. The cell
// size is fixed rather than measured, so the text is the same everywhere.
// Each shape becomes a box of cells holding the labels inside it, the boxes
// are pushed apart where rounding made them overlap, and each edge is routed
// again between the boxes along rows and columns, because a pixel route
// does not survive rounding to cells.

import (
	"math"
	"sort"
	"strings"
	"unicode/utf8"

	"github.com/kvit-s/kvit-notes/diagram"
)

const (
	cellW = 9.0
	cellH = 18.0
	// A margin on the top and left, so a route can bend beyond the first
	// node.
	colOffset = 2
	rowOffset = 1
)

func sceneCol(x float64) int { return int(math.Round(x/cellW)) + colOffset }
func sceneRow(y float64) int { return int(math.Round(y/cellH)) + rowOffset }

// cell is a column and a row.
type cell struct{ col, row int }

// noCell is a cell not yet known.
var noCell = cell{-1, -1}

// textNode is a shape as a box of cells.
type textNode struct {
	shape          int      // index into the scene's shapes
	lines          []string // the label, top to bottom
	row, col, w, h int
}

func (n *textNode) right() int  { return n.col + n.w - 1 }
func (n *textNode) bottom() int { return n.row + n.h - 1 }

// directionOf is the axis direction closest to v.
func directionOf(v diagram.Point) Direction {
	if math.Abs(v.X) >= math.Abs(v.Y) {
		if v.X >= 0 {
			return Right
		}
		return Left
	}
	if v.Y >= 0 {
		return Down
	}
	return Up
}

func isVertical(d Direction) bool { return d == Up || d == Down }

func opposite(d Direction) Direction {
	switch d {
	case Up:
		return Down
	case Down:
		return Up
	case Left:
		return Right
	}
	return Left
}

// markerGlyph is the character a marker is drawn as. An arrow keeps the
// arrowheads Repair knows; the UML heads become △ ◇ o x and a crow's foot
// < > ^ v by the way its line runs, on purpose.
func markerGlyph(m diagram.Marker, dir Direction) rune {
	switch m {
	case diagram.MarkerArrow, diagram.MarkerOpenArrow:
		return [...]rune{Up: '▲', Down: '▼', Left: '◄', Right: '►'}[dir]
	case diagram.MarkerCross:
		return 'x'
	case diagram.MarkerDot, diagram.MarkerCircleOpen:
		return 'o'
	case diagram.MarkerTriangleOpen:
		return '△'
	case diagram.MarkerDiamondFilled, diagram.MarkerDiamondOpen:
		return '◇'
	case diagram.MarkerErMany, diagram.MarkerErZeroMany:
		return [...]rune{Up: '^', Down: 'v', Left: '<', Right: '>'}[dir]
	case diagram.MarkerErOne, diagram.MarkerErZeroOne:
		if isVertical(dir) {
			return '─'
		}
		return '|'
	}
	return 0
}

// ends is the two cells beside the borders an edge leaves from and arrives
// at. Edges sharing one are the branches of a fan-out, or the arms of a
// fan-in: a node three edges leave has one usable cell on that wall, so
// their first segments have to coincide, and the junctions draw that as one
// trunk splitting rather than as a collision. Edges sharing neither keep
// out of each other's way.
type ends struct{ from, to cell }

var noEnds = ends{noCell, noCell}

func known(c cell) bool { return c.col >= 0 }

func (e ends) shares(o ends) bool {
	return known(e.from) && (e.from == o.from || e.from == o.to) ||
		known(e.to) && (e.to == o.from || e.to == o.to)
}

// span is a stretch of a row or column a line or a box takes, with the ends
// of the edge that drew it.
type span struct {
	lo, hi int
	ends   ends
}

// channels is what is taken on each row and column, so unrelated edges run
// in different ones. A span remembers its edge's ends, so a sibling branch
// can run along a shared trunk while every other edge finds it taken.
type channels struct {
	horizontal map[int][]span // row to spans
	vertical   map[int][]span // column to spans
}

func overlaps(spans []span, a, b int, e ends) bool {
	lo, hi := min(a, b), max(a, b)
	for _, s := range spans {
		if hi < s.lo || s.hi < lo {
			continue
		}
		if s.ends.shares(e) {
			continue
		}
		return true
	}
	return false
}

func (c *channels) hFree(row, a, b int, e ends) bool { return !overlaps(c.horizontal[row], a, b, e) }
func (c *channels) vFree(col, a, b int, e ends) bool { return !overlaps(c.vertical[col], a, b, e) }

func (c *channels) takeH(row, a, b int, e ends) {
	c.horizontal[row] = append(c.horizontal[row], span{min(a, b), max(a, b), e})
}

func (c *channels) takeV(col, a, b int, e ends) {
	c.vertical[col] = append(c.vertical[col], span{min(a, b), max(a, b), e})
}

// shared is the rows or columns a sibling branch already runs along, in
// order. Reusing one turns a fan-out's several turning columns into one
// spine.
func shared(lanes map[int][]span, e ends) []int {
	var out []int
	for key, spans := range lanes {
		for _, s := range spans {
			if s.ends.shares(e) {
				out = append(out, key)
				break
			}
		}
	}
	sort.Ints(out)
	return out
}

// blockRect takes a node's box in both directions, so no line runs through
// it; a drawn line takes only its own direction, because a crossing is
// drawn as ┼ while an overlap would be ambiguous. A box has no ends, so no
// edge counts as its sibling.
func (c *channels) blockRect(top, left, bottom, right int) {
	for row := top; row <= bottom; row++ {
		c.takeH(row, left, right, noEnds)
	}
	for col := left; col <= right; col++ {
		c.takeV(col, top, bottom, noEnds)
	}
}

// pathDraw is where an edge was drawn in cells: its first cell, the middle
// of its longest segment, and its last cell. An edge label follows its edge
// by these even when routing moved the edge away from the pixel layout.
type pathDraw struct {
	start, mid, end cell
	valid           bool
}

var noDraw = pathDraw{noCell, noCell, noCell, false}

// builder draws one scene.
type builder struct {
	scene     *diagram.Scene
	canvas    Canvas
	nodes     []textNode
	nodeTexts map[int]bool // scene texts used as node labels
	channels  channels
	pathDraws []pathDraw

	// The edge being drawn: where drawPolyline has put it so far, the
	// length of its longest segment, and its ends, which keep the spans it
	// takes open to its siblings on the same wall cell.
	current       pathDraw
	currentLength int
	currentEnds   ends
}

// ---- nodes ----

// containsRect reports whether r holds every point of o, neither of them
// empty.
func containsRect(r, o diagram.Rect) bool {
	r, o = r.Normalized(), o.Normalized()
	if r.W == 0 || r.H == 0 || o.W == 0 || o.H == 0 {
		return false
	}
	return o.X >= r.X && o.Right() <= r.Right() && o.Y >= r.Y && o.Bottom() <= r.Bottom()
}

func manhattan(p diagram.Point) float64 { return math.Abs(p.X) + math.Abs(p.Y) }

// isConcentricInner reports a shape drawn inside a larger one around the
// same centre, as the end state's inner disc is: only the outer one becomes
// a box. Being inside is not enough, since a composite state holds much
// smaller states; the two must also share a centre.
func (b *builder) isConcentricInner(i int) bool {
	sh := b.scene.Shapes[i].Rect
	area := sh.W * sh.H
	for j, other := range b.scene.Shapes {
		if j == i {
			continue
		}
		if other.Rect.W*other.Rect.H <= area {
			continue
		}
		delta := other.Rect.Center().Sub(sh.Center())
		if containsRect(other.Rect.Adjusted(-1, -1, 1, 1), sh) && manhattan(delta) < 4 {
			return true
		}
	}
	return false
}

func (b *builder) collectNodes() {
	for i := range b.scene.Shapes {
		if b.isConcentricInner(i) {
			continue
		}
		shape := &b.scene.Shapes[i]
		node := textNode{shape: i}

		// The label is every text whose centre is inside the shape, top to
		// bottom; a class box has several.
		type labelText struct {
			y    float64
			text string
		}
		var inside []labelText
		for ti, text := range b.scene.Texts {
			if text.HasBackground {
				continue // an edge label, placed with its edge
			}
			if shape.Rect.Contains(text.Rect.Center()) {
				inside = append(inside, labelText{text.Rect.Center().Y, text.Text})
				b.nodeTexts[ti] = true
			}
		}
		sort.SliceStable(inside, func(a, c int) bool { return inside[a].y < inside[c].y })
		for _, lt := range inside {
			node.lines = append(node.lines, strings.Split(lt.text, "\n")...)
		}
		decorateLabel(&node, shape.Kind)

		maxLine := 0
		for _, l := range node.lines {
			maxLine = max(maxLine, utf8.RuneCountInString(l))
		}
		node.col = sceneCol(shape.Rect.Left())
		node.row = sceneRow(shape.Rect.Top())
		node.w = max(int(math.Round(shape.Rect.W/cellW)), maxLine+4, 5)
		node.h = max(int(math.Round(shape.Rect.H/cellH)), len(node.lines)+2, 3)
		b.nodes = append(b.nodes, node)
	}
	b.relaxOverlaps()
	for _, n := range b.nodes {
		b.channels.blockRect(n.row, n.col, n.bottom(), n.right())
	}
}

// decorateLabel dresses a label for its shape: a decision shows as
// < label >, and a state's start and end circles as (*) and ((*)). Every
// shape is still drawn as a box.
func decorateLabel(n *textNode, kind diagram.ShapeKind) {
	switch {
	case kind == diagram.KindRhombus:
		for i, l := range n.lines {
			n.lines[i] = "< " + l + " >"
		}
		if len(n.lines) == 0 {
			n.lines = append(n.lines, "< >")
		}
	case kind == diagram.KindCircle && len(n.lines) == 0:
		n.lines = append(n.lines, "(*)")
	case kind == diagram.KindDoubleCircle && len(n.lines) == 0:
		n.lines = append(n.lines, "((*))")
	}
}

// relaxOverlaps pushes overlapping boxes apart along one axis, the one the
// pixel layout separates them on most, keeping their order. The pixel
// layout already orders them, so a few passes settle.
func (b *builder) relaxOverlaps() {
	for range 32 {
		moved := false
		for i := range b.nodes {
			for j := i + 1; j < len(b.nodes); j++ {
				a, c := &b.nodes[i], &b.nodes[j]
				// A cell is about twice as tall as it is wide, so the two
				// axes need different clearances. Side by side, two boxes
				// need a blank column between them, or their walls sit in
				// neighbouring cells and read as one thick wall. Stacked,
				// `└──┘` right above `┌────┐` already reads as two boxes,
				// and asking for a blank row there pushes a whole rank out
				// of line with the pixel layout it came from.
				overlapW := min(a.right(), c.right()) - max(a.col, c.col) + 2
				overlapH := min(a.bottom(), c.bottom()) - max(a.row, c.row) + 1
				if overlapW <= 0 || overlapH <= 0 {
					continue
				}
				ca := b.scene.Shapes[a.shape].Rect.Center()
				cb := b.scene.Shapes[c.shape].Rect.Center()
				// Along the axis the pixel layout separates them on most,
				// relative to the cell's shape, keeping their order.
				if math.Abs(ca.X-cb.X)*cellH >= math.Abs(ca.Y-cb.Y)*cellW {
					later := c
					if ca.X > cb.X {
						later = a
					}
					later.col += overlapW
				} else {
					later := c
					if ca.Y > cb.Y {
						later = a
					}
					later.row += overlapH
				}
				moved = true
			}
		}
		if !moved {
			break
		}
	}
}

// nodeAt is the node nearest p among those whose shape, grown by 6 pixels,
// holds p, or -1.
func (b *builder) nodeAt(p diagram.Point) int {
	best := -1
	var bestDistance float64
	for i, n := range b.nodes {
		r := b.scene.Shapes[n.shape].Rect
		if !r.Adjusted(-6, -6, 6, 6).Contains(p) {
			continue
		}
		d := manhattan(r.Center().Sub(p))
		if best < 0 || d < bestDistance {
			best, bestDistance = i, d
		}
	}
	return best
}

func (b *builder) drawNodes() {
	for _, n := range b.nodes {
		kind := b.scene.Shapes[n.shape].Kind
		b.canvas.DrawBox(n.row, n.col, n.bottom(), n.right(), kind == diagram.KindSubroutine)
		if kind == diagram.KindCylinder {
			for c := n.col + 1; c < n.right(); c++ {
				b.canvas.Put(n.row, c, '~')
			}
		}
	}
}

func (b *builder) drawNodeLabels() {
	for _, n := range b.nodes {
		innerW := n.w - 2
		firstRow := n.row + max(1, (n.h-len(n.lines))/2)
		for i, l := range n.lines {
			line := []rune(l)
			if len(line) > innerW {
				line = line[:max(0, innerW)]
			}
			row := firstRow + i
			if row >= n.bottom() {
				break
			}
			col := n.col + 1 + max(0, (innerW-len(line))/2)
			b.canvas.DrawText(row, col, string(line))
		}
	}
}

// ---- groups ----

func (b *builder) drawGroups() {
	for _, g := range b.scene.Groups {
		top, left := sceneRow(g.Rect.Top()), sceneCol(g.Rect.Left())
		bottom := max(top+2, sceneRow(g.Rect.Bottom()))
		right := max(left+4, sceneCol(g.Rect.Right()))
		if !g.NoBorder {
			b.canvas.DrawBox(top, left, bottom, right, false)
		}
		if g.Title != "" {
			b.canvas.DrawText(top, left+2, " "+g.Title+" ")
		}
	}
}

// ---- edges ----

// anchor is the cell just outside a node's border that an edge leaves or
// arrives through, and the way it travels away from the node.
type anchor struct {
	row, col int
	out      Direction
}

// anchorFor is the anchor of an edge leaving n towards travel, as near the
// pixel point as the wall allows.
func (b *builder) anchorFor(n *textNode, pixel diagram.Point, travel Direction) anchor {
	a := anchor{out: travel}
	switch travel {
	case Down:
		a.row = n.bottom() + 1
		a.col = clamp(sceneCol(pixel.X), n.col+1, n.right()-1)
	case Up:
		a.row = n.row - 1
		a.col = clamp(sceneCol(pixel.X), n.col+1, n.right()-1)
	case Right:
		a.col = n.right() + 1
		a.row = clamp(sceneRow(pixel.Y), n.row+1, n.bottom()-1)
	case Left:
		a.col = n.col - 1
		a.row = clamp(sceneRow(pixel.Y), n.row+1, n.bottom()-1)
	}
	return a
}

// clamp is v kept between lo and hi: lo when v is below lo, otherwise hi
// when v is above hi.
func clamp(v, lo, hi int) int {
	if v < lo {
		return lo
	}
	if hi < v {
		return hi
	}
	return v
}

func (b *builder) drawEdges() {
	for i := range b.scene.Paths {
		path := &b.scene.Paths[i]
		b.current = noDraw
		b.currentEnds = noEnds
		// A path whose two ends are the same point is not an edge but part
		// of a box: a class box's compartment lines leave both ends at the
		// scene's origin. Routing one would hang a loop off the box's side,
		// and a box has no spare row inside to draw the line on, so it is
		// left out. A loop from a node to itself has two different points
		// on the border and is still routed.
		if path.StartPoint.Equal(path.EndPoint) {
			b.pathDraws = append(b.pathDraws, b.current)
			continue
		}
		from, to := b.nodeAt(path.StartPoint), b.nodeAt(path.EndPoint)
		switch {
		case from >= 0 && to >= 0 && from != to:
			b.routeNodeEdge(path, &b.nodes[from], &b.nodes[to])
		case from >= 0 && to >= 0:
			b.routeSelfLoop(path, &b.nodes[from])
		default:
			b.routeFreePath(path)
		}
		b.pathDraws = append(b.pathDraws, b.current)
	}
}

func (b *builder) routeNodeEdge(path *diagram.Path, from, to *textNode) {
	// StartDir and EndDir point into their nodes: the edge leaves the start
	// against StartDir and enters the end along EndDir. A path without
	// them, such as a lifeline, travels along the line between its ends.
	travel := directionOf(path.EndPoint.Sub(path.StartPoint))
	out, in := travel, travel
	if path.StartDir != (diagram.Point{}) {
		out = directionOf(path.StartDir.Neg())
	}
	if path.EndDir != (diagram.Point{}) {
		in = directionOf(path.EndDir)
	}

	s := b.anchorFor(from, path.StartPoint, out)
	// The end's anchor is outside the border the edge crosses, on the side
	// it comes from: opposite in.
	t := b.anchorFor(to, path.EndPoint, opposite(in))
	b.currentEnds = ends{cell{s.col, s.row}, cell{t.col, t.row}}
	e := b.currentEnds

	way := []cell{{s.col, s.row}}
	switch {
	case isVertical(out) && isVertical(in):
		if s.col == t.col && abs(t.row-s.row) > 1 &&
			!b.channels.vFree(s.col, min(s.row, t.row)+1, max(s.row, t.row)-1, e) {
			// In line, but the column is taken by the boxes of the ranks
			// between: go around rather than through them.
			b.routeOuterLane(path, from, to, true)
			return
		}
		if s.col != t.col {
			// A Z route needs all three segments clear, the crossbar and
			// both upright stubs; a back edge's stub would cut straight
			// through the rank between.
			midRow, found := zChannel(s.row, t.row, (s.row+t.row)/2, shared(b.channels.horizontal, e),
				func(row int) bool {
					return b.channels.hFree(row, s.col, t.col, e) &&
						b.channels.vFree(s.col, s.row, row, e) &&
						b.channels.vFree(t.col, row, t.row, e)
				})
			if !found {
				// Round by a lane beyond the diagram, into the target's
				// side wall.
				b.routeOuterLane(path, from, to, true)
				return
			}
			way = append(way, cell{s.col, midRow}, cell{t.col, midRow})
		}
	case !isVertical(out) && !isVertical(in):
		if s.row == t.row && abs(t.col-s.col) > 1 &&
			!b.channels.hFree(s.row, min(s.col, t.col)+1, max(s.col, t.col)-1, e) {
			b.routeOuterLane(path, from, to, false)
			return
		}
		if s.row != t.row {
			midCol, found := zChannel(s.col, t.col, (s.col+t.col)/2, shared(b.channels.vertical, e),
				func(col int) bool {
					return b.channels.vFree(col, s.row, t.row, e) &&
						b.channels.hFree(s.row, s.col, col, e) &&
						b.channels.hFree(t.row, col, t.col, e)
				})
			if !found {
				b.routeOuterLane(path, from, to, false)
				return
			}
			way = append(way, cell{midCol, s.row}, cell{midCol, t.row})
		}
	case isVertical(out):
		way = append(way, cell{s.col, t.row}) // an L: down or up, then across
	default:
		way = append(way, cell{t.col, s.row}) // an L: across, then down or up
	}
	way = append(way, cell{t.col, t.row})

	b.drawPolyline(way)
	// Tie each anchor to the wall it is against. Where the route already
	// leaves the anchor along that axis, this adds an arm the cell has;
	// where a stub that came to nothing left the anchor turning sideways,
	// it adds the arm that makes it a corner.
	b.canvas.DrawStub(s.row, s.col, opposite(out))
	b.canvas.DrawStub(t.row, t.col, in)
	b.placeMarker(path.EndMarker, t.row, t.col, in)
	b.placeMarker(path.StartMarker, s.row, s.col, opposite(out))
}

// routeOuterLane routes an edge whose direct way is taken: out through the
// start node's wall facing a lane beyond the right (or bottom) of the
// diagram, along the lane, and into the target's wall on the same side, so
// neither end takes the top and bottom anchors forward edges use.
func (b *builder) routeOuterLane(path *diagram.Path, from, to *textNode, verticalLane bool) {
	if verticalLane {
		flank := 0
		for _, n := range b.nodes {
			flank = max(flank, n.right())
		}
		sRow := clamp(sceneRow(path.StartPoint.Y), from.row+1, from.bottom()-1)
		tRow := clamp(sceneRow(path.EndPoint.Y), to.row+1, to.bottom()-1)
		b.currentEnds = ends{cell{from.right() + 1, sRow}, cell{to.right() + 1, tRow}}
		lane := flank + 3
		for range 6 {
			if b.channels.vFree(lane, min(sRow, tRow), max(sRow, tRow), b.currentEnds) {
				break
			}
			lane++
		}
		b.drawPolyline([]cell{{from.right() + 1, sRow}, {lane, sRow}, {lane, tRow}, {to.right() + 1, tRow}})
		b.placeMarker(path.EndMarker, tRow, to.right()+1, Left)
		b.placeMarker(path.StartMarker, sRow, from.right()+1, Right)
		return
	}
	flank := 0
	for _, n := range b.nodes {
		flank = max(flank, n.bottom())
	}
	sCol := clamp(sceneCol(path.StartPoint.X), from.col+1, from.right()-1)
	tCol := clamp(sceneCol(path.EndPoint.X), to.col+1, to.right()-1)
	b.currentEnds = ends{cell{sCol, from.bottom() + 1}, cell{tCol, to.bottom() + 1}}
	lane := flank + 2
	for range 6 {
		if b.channels.hFree(lane, min(sCol, tCol), max(sCol, tCol), b.currentEnds) {
			break
		}
		lane++
	}
	b.drawPolyline([]cell{{sCol, from.bottom() + 1}, {sCol, lane}, {tCol, lane}, {tCol, to.bottom() + 1}})
	b.placeMarker(path.EndMarker, to.bottom()+1, tCol, Up)
	b.placeMarker(path.StartMarker, from.bottom()+1, sCol, Down)
}

// routeSelfLoop draws an edge from a node to itself: out of the right wall,
// two columns over, and back in below.
func (b *builder) routeSelfLoop(path *diagram.Path, n *textNode) {
	rowA := clamp(sceneRow(path.StartPoint.Y), n.row+1, n.bottom()-1)
	rowB := clamp(sceneRow(path.EndPoint.Y), n.row+1, n.bottom()-1)
	if rowB == rowA {
		rowB++
	}
	wall := n.right()
	lane := wall + 3
	if rowB > n.bottom()-1 {
		// A box three rows high has one row inside, so its side wall
		// cannot hold both ends: the loop leaves the side, drops past the
		// box and comes back up into its floor. Returning along the row it
		// left on would draw a stub that goes nowhere.
		below := n.bottom() + 1
		back := clamp(n.right()-2, n.col+1, n.right()-1)
		b.drawPolyline([]cell{{wall + 1, rowA}, {lane, rowA}, {lane, below}, {back, below}})
		b.canvas.DrawStub(rowA, wall+1, Left)
		b.placeMarker(path.EndMarker, below, back, Up)
		return
	}
	b.drawPolyline([]cell{{wall + 1, rowA}, {lane, rowA}, {lane, rowB}, {wall + 1, rowB}})
	b.canvas.DrawStub(rowA, wall+1, Left)
	b.placeMarker(path.EndMarker, rowB, wall+1, Left)
}

// routeFreePath draws a path that belongs to no node, such as a lifeline,
// a message or a note's tie, from end to end: straight when its ends share
// a row or a column, otherwise down or up first and then across. Lines stop
// at the boxes they meet, so a lifeline ends on its header's border as a
// junction rather than cutting through the box.
func (b *builder) routeFreePath(path *diagram.Path) {
	c1, r1 := sceneCol(path.StartPoint.X), sceneRow(path.StartPoint.Y)
	c2, r2 := sceneCol(path.EndPoint.X), sceneRow(path.EndPoint.Y)
	if c1 != c2 && r1 != r2 {
		b.drawPolyline([]cell{{c1, r1}, {c1, r2}, {c2, r2}})
	} else {
		b.drawPolyline([]cell{{c1, r1}, {c2, r2}})
	}
	var in Direction
	switch {
	case c1 == c2 && r2 >= r1:
		in = Down
	case c1 == c2:
		in = Up
	case c2 >= c1:
		in = Right
	default:
		in = Left
	}
	b.placeMarker(path.EndMarker, r2, c2, in)
	b.placeMarker(path.StartMarker, r1, c1, opposite(in))
}

type cellState int

const (
	freeCell cellState = iota
	borderCell
	interiorCell
)

// cellState says whether a cell is outside every node, on a node's border,
// or inside one.
func (b *builder) cellState(row, col int) cellState {
	for _, n := range b.nodes {
		if row < n.row || row > n.bottom() || col < n.col || col > n.right() {
			continue
		}
		if row == n.row || row == n.bottom() || col == n.col || col == n.right() {
			return borderCell
		}
		return interiorCell
	}
	return freeCell
}

// drawClippedVLine draws the stretches of the line that are outside every
// node, each end reaching onto a border it touches, so the line joins the
// box's edge as a junction.
func (b *builder) drawClippedVLine(col, rowA, rowB int) {
	lo, hi := min(rowA, rowB), max(rowA, rowB)
	run := -1
	for row := lo; row <= hi+1; row++ {
		free := row <= hi && b.cellState(row, col) == freeCell
		if free && run < 0 {
			run = row
		}
		if !free && run >= 0 {
			from, to := run, row-1
			// A short stretch past the last box, such as a lifeline's tail
			// below the bottom header, is an artifact of rounding rather
			// than drawing.
			trailingStub := run > lo && to == hi && to-run < 2
			if !trailingStub {
				if from > lo && b.cellState(from-1, col) == borderCell {
					from--
				}
				if to < hi && b.cellState(to+1, col) == borderCell {
					to++
				}
				b.canvas.DrawVLine(col, from, to)
				b.channels.takeV(col, from, to, b.currentEnds)
			}
			run = -1
		}
	}
}

func (b *builder) drawClippedHLine(row, colA, colB int) {
	lo, hi := min(colA, colB), max(colA, colB)
	run := -1
	for col := lo; col <= hi+1; col++ {
		free := col <= hi && b.cellState(row, col) == freeCell
		if free && run < 0 {
			run = col
		}
		if !free && run >= 0 {
			from, to := run, col-1
			if from > lo && b.cellState(row, from-1) == borderCell {
				from--
			}
			if to < hi && b.cellState(row, to+1) == borderCell {
				to++
			}
			b.canvas.DrawHLine(row, from, to)
			b.channels.takeH(row, from, to, b.currentEnds)
			run = -1
		}
	}
}

// zChannel is the row (or column) a Z route's crossbar runs along: one a
// sibling branch already turns on, if it lies within the ends' span, and
// otherwise the preferred one or the nearest to it within the span, two
// cells either side allowed, that valid accepts. found is false when none
// is accepted.
func zChannel(a, b, preferred int, spine []int, valid func(int) bool) (int, bool) {
	lo, hi := min(a, b)-2, max(a, b)+2
	for _, candidate := range spine {
		if candidate >= lo && candidate <= hi && valid(candidate) {
			return candidate, true
		}
	}
	for delta := 0; delta <= hi-lo; delta++ {
		for _, candidate := range []int{preferred + delta, preferred - delta} {
			if candidate >= lo && candidate <= hi && valid(candidate) {
				return candidate, true
			}
			if delta == 0 {
				break // +0 and -0 are the same
			}
		}
	}
	return preferred, false
}

// drawPolyline draws a route through the cells, each segment clipped to
// the boxes: a well-routed segment meets no box, and a crossing forced on a
// detour in a crowded layout stops at one border and starts again past the
// other rather than spoiling the box.
func (b *builder) drawPolyline(way []cell) {
	b.currentLength = 0
	for i := 1; i < len(way); i++ {
		p, q := way[i-1], way[i]
		// A Z route whose crossbar falls on an end's own row or column
		// leaves that stub with no length; drawing it would leave a
		// one-cell dash pointing at neither neighbour.
		if p == q {
			continue
		}
		if p.row == q.row {
			b.drawClippedHLine(p.row, p.col, q.col)
		} else {
			b.drawClippedVLine(p.col, p.row, q.row)
		}
		if length := abs(q.col-p.col) + abs(q.row-p.row); length >= b.currentLength {
			b.currentLength = length
			b.current.mid = cell{(p.col + q.col) / 2, (p.row + q.row) / 2}
		}
	}
	if len(way) > 0 {
		b.current.start = way[0]
		b.current.end = way[len(way)-1]
		if b.current.mid.col < 0 {
			b.current.mid = b.current.start
		}
		b.current.valid = true
	}
}

func (b *builder) placeMarker(m diagram.Marker, row, col int, dir Direction) {
	if glyph := markerGlyph(m, dir); glyph != 0 {
		b.canvas.Put(row, col, glyph)
	}
}

// ---- texts outside nodes: edge labels, messages, ER roles ----

// labelFits reports whether a label of length characters can go at row,
// col: on blank cells or plain line, which a label may displace, but not
// on an arrowhead, a corner or a border.
func (b *builder) labelFits(row, col, length int) bool {
	for i := range length {
		switch b.canvas.At(row, col+i) {
		case 0, ' ', '─', '│':
		default:
			return false
		}
	}
	return true
}

// labelHome is the cell a text outside the nodes is centred on. An edge
// label (a Text with a backdrop) belongs to a path: it goes to the drawn
// cell matching the end or middle of the path its pixel position is
// nearest, since routing may have moved the edge far from its pixel
// place. Any other text keeps the cell of its pixel position.
func (b *builder) labelHome(text *diagram.Text) cell {
	c := text.Rect.Center()
	pixelCell := cell{sceneCol(c.X), sceneRow(c.Y)}
	if !text.HasBackground {
		return pixelCell
	}
	bestPath, bestRef := -1, 1
	var bestDistance float64
	for p := range min(len(b.scene.Paths), len(b.pathDraws)) {
		if !b.pathDraws[p].valid {
			continue
		}
		path := &b.scene.Paths[p]
		refs := [3]diagram.Point{path.StartPoint, path.StartPoint.Add(path.EndPoint).Div(2), path.EndPoint}
		for r, ref := range refs {
			d := manhattan(ref.Sub(c))
			if bestPath < 0 || d < bestDistance {
				bestPath, bestRef, bestDistance = p, r, d
			}
		}
	}
	if bestPath < 0 {
		return pixelCell
	}
	draw := b.pathDraws[bestPath]
	switch bestRef {
	case 0:
		return draw.start
	case 2:
		return draw.end
	}
	return draw.mid
}

func (b *builder) drawLooseTexts() {
	for i := range b.scene.Texts {
		if b.nodeTexts[i] {
			continue
		}
		text := &b.scene.Texts[i]
		if strings.TrimSpace(text.Text) == "" {
			continue
		}
		lines := strings.Split(text.Text, "\n")
		home := b.labelHome(text)
		firstRow := home.row - (len(lines)-1)/2
		for l, line := range lines {
			length := utf8.RuneCountInString(line)
			col := max(0, home.col-length/2)
			row := firstRow + l
			// Move off arrowheads and borders where possible, up to a
			// label's length sideways and a row up or down, and use the
			// natural cell only when nothing near fits.
			placed := false
			reach := length + 2
			for _, dRow := range []int{0, -1, 1} {
				for shift := 0; shift <= reach && !placed; shift++ {
					for _, dCol := range []int{shift, -shift} {
						if b.labelFits(row+dRow, col+dCol, length) {
							b.canvas.DrawText(row+dRow, col+dCol, line)
							placed = true
							break
						}
						if shift == 0 {
							break
						}
					}
				}
				if placed {
					break
				}
			}
			if !placed {
				b.canvas.DrawText(row, col, line)
			}
		}
	}
}

// FromScene is the scene drawn in box-drawing characters, or "" for a scene
// with nothing to draw. The same scene always gives the same text. The
// canvas stays within MaxCanvasCells however far apart a note pins its
// nodes, so the text is cut off rather than the memory unbounded.
func FromScene(scene *diagram.Scene) string {
	if scene == nil || scene.Empty() {
		return ""
	}
	b := builder{
		scene:     scene,
		nodeTexts: map[int]bool{},
		channels:  channels{horizontal: map[int][]span{}, vertical: map[int][]span{}},
	}
	b.collectNodes()
	b.drawGroups()
	b.drawNodes()
	b.drawEdges()
	b.drawNodeLabels()
	b.drawLooseTexts()

	// No blank lines at the top and no left margin every line shares:
	// routing's offsets and rounding leave both behind.
	lines := strings.Split(b.canvas.String(), "\n")
	for len(lines) > 0 && strings.TrimSpace(lines[0]) == "" {
		lines = lines[1:]
	}
	indent := -1
	for _, line := range lines {
		if strings.TrimSpace(line) == "" {
			continue
		}
		n := len(line) - len(strings.TrimLeft(line, " "))
		if indent < 0 || n < indent {
			indent = n
		}
	}
	if indent > 0 {
		for i, line := range lines {
			r := []rune(line)
			lines[i] = string(r[min(indent, len(r)):])
		}
	}
	return strings.Join(lines, "\n")
}
