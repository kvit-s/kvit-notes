package editor

// Sideways scrolling for a table wider than its room
// (Embedding.ScrollTables): a table whose columns cannot be narrowed to fit,
// each being at least its longest word (tablefit.go), keeps those widths and
// scrolls inside its block as a code block's long lines do (codescroll.go).
// Its view starts where the grid starts and is the table's room wide; the
// grid is drawn through it, shifted left by the block's scroll offset, with a
// scrollbar under it. The offset is kept by block id with the code blocks'
// offsets, and the scrollbar, the wheel and the scrollbar drag are theirs.
//
// Everything that maps a point to a cell goes through gridOrigin, which
// follows the offset, and refuses a point outside the view (inTableView), so
// presses, column grips, the swept rectangle of cells and the live cell's
// field all land on the cell drawn under them.

import "github.com/richardwilkes/toolbox/v2/geom"

// tableBarGap is the space between a scrolling grid and its scrollbar, in
// design pixels.
const tableBarGap = 4

// tableRoom is the width block i's grid has: the text's width, or more under
// an embedding's TextWidth.
func (e *Editor) tableRoom(i int) float32 {
	return e.wideWidth(&e.Doc.Blocks[i])
}

// tableScrolls is block i's grid when it is shown, the embedding lets tables
// scroll, and the grid is wider than its room.
func (e *Editor) tableScrolls(i int) (*grid, bool) {
	if !e.seams.scrollTables() || e.Printing || i < 0 || i >= len(e.Doc.Blocks) || e.Doc.Blocks[i].Kind != Table {
		return nil, false
	}
	g, ok := e.tableShowsGrid(i)
	if !ok || g.width() <= e.tableRoom(i)+0.5 {
		return nil, false
	}
	return g, true
}

// tableScrollDX is how far block i's grid is shifted left: its scroll
// offset, or 0 for a grid that fits.
func (e *Editor) tableScrollDX(i int) float32 {
	if _, ok := e.tableScrolls(i); !ok {
		return 0
	}
	return e.codeScrollOf(e.Doc.Blocks[i].ID)
}

// tableView is the part of the editor block i's grid is seen through: from
// the grid's unscrolled start, its room wide and the grid tall. The right
// rule of a grid scrolled to its end lies just past the room, so the view
// takes that rule too.
func (e *Editor) tableView(i int, g *grid) geom.Rect {
	o := e.gridHome(i)
	return geom.NewRect(o.X, o.Y, e.tableRoom(i)+e.px(gridLineSize), g.height)
}

// inTableView reports whether where is in block i's grid's view, as it is
// anywhere for a grid that fits.
func (e *Editor) inTableView(i int, where geom.Point) bool {
	g, ok := e.tableScrolls(i)
	if !ok {
		return true
	}
	v := e.tableView(i, g)
	return where.X >= v.X && where.X < v.Right()
}

// tableClip is the rectangle a scrolling grid is drawn in: its view's width
// and the whole row's height, which holds the + Row and + Column controls
// and the hint under a live cell. False for a grid that fits.
func (e *Editor) tableClip(i int) (geom.Rect, bool) {
	g, ok := e.tableScrolls(i)
	if !ok {
		return geom.Rect{}, false
	}
	v := e.tableView(i, g)
	return geom.NewRect(v.X, e.tops[i], v.Width, e.heights[i]), true
}

// tableBarRoom is the height the scrollbar under a scrolling grid adds to
// its row, 0 for a grid that fits.
func (e *Editor) tableBarRoom(i int) float32 {
	if _, ok := e.tableScrolls(i); !ok {
		return 0
	}
	return e.px(tableBarGap + codeBarH)
}

// tableBarRect is the scrollbar track under block i's grid, or false when
// the grid fits.
func (e *Editor) tableBarRect(i int) (geom.Rect, bool) {
	g, ok := e.tableScrolls(i)
	if !ok {
		return geom.Rect{}, false
	}
	o := e.gridHome(i)
	return geom.NewRect(o.X, o.Y+g.height+e.px(tableBarGap), e.tableRoom(i), e.px(codeBarH)), true
}

// revealTableCell scrolls block i's grid so column col shows: its left edge
// at the view's left when it is wider than the view, otherwise the whole
// column, moving the grid as little as it can.
func (e *Editor) revealTableCell(i, col int) {
	g, ok := e.tableScrolls(i)
	if !ok || col < 0 || col >= len(g.cols) {
		return
	}
	var left float32
	for c := range col {
		left += g.cols[c]
	}
	right := left + g.cols[col] + e.px(gridLineSize)
	view := e.tableRoom(i)
	id := e.Doc.Blocks[i].ID
	scroll := e.codeScrollOf(id)
	switch {
	case left < scroll:
		scroll = left
	case right > scroll+view:
		scroll = min(left, right-view)
	default:
		return
	}
	e.setCodeScroll(id, scroll)
}

// tableDragPoint is a pointer dragging a swept rectangle across block i's
// grid: past either side of a scrolling grid's view it scrolls the grid by
// as much, and the point is kept inside the view so the rectangle reaches
// the column now at its edge.
func (e *Editor) tableDragPoint(i int, where geom.Point) geom.Point {
	g, ok := e.tableScrolls(i)
	if !ok {
		return where
	}
	v := e.tableView(i, g)
	id := e.Doc.Blocks[i].ID
	switch {
	case where.X < v.X:
		e.setCodeScroll(id, e.codeScrollOf(id)-(v.X-where.X))
		where.X = v.X
	case where.X >= v.Right():
		e.setCodeScroll(id, e.codeScrollOf(id)+where.X-v.Right()+1)
		where.X = v.Right() - 1
	}
	return where
}

// clipToTableView is the part of r, a cell of block i's grid, inside the
// grid's view: where the live cell's field goes.
func (e *Editor) clipToTableView(i int, r geom.Rect) geom.Rect {
	g, ok := e.tableScrolls(i)
	if !ok {
		return r
	}
	v := e.tableView(i, g)
	left, right := max(r.X, v.X), min(r.Right(), v.Right()-e.px(gridLineSize))
	if right <= left {
		return r
	}
	return geom.NewRect(left, r.Y, right-left, r.Height)
}
