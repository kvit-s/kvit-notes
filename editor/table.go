package editor

// Tables: a run of lines starting with "|" is a table block, kept as written.
// It is drawn as a grid, the header row set apart, each cell's inline
// Markdown drawn and its column's alignment kept; a press in a cell makes it
// live for editing in place. Column widths the reader drags live in the
// block's own cols attribute, so they follow the file.

import (
	"math"
	"strconv"
	"strings"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/pathop"
)

// The grid in design pixels: the padding in each cell and the narrowest a
// column is made.
const (
	cellPadX     = 8
	cellPadY     = 5
	minColumn    = 48
	gridLineSize = 1
)

// grid is a table laid out at a width: each column's width, each row's
// height, and every cell's text.
type grid struct {
	cols  []float32
	rowsH []float32
	cells [][]*text.Layout // [row][col], the header first
	projs [][]projection   // what each cell draws, for its typeset math
	// spans are where each cell's characters are in the block's Markdown
	// (tabletext.go), nil when they cannot be told.
	spans  [][]cellSpan
	align  []TableAlign
	height float32
}

// width is the grid's width: its columns side by side.
func (g *grid) width() float32 {
	var w float32
	for _, c := range g.cols {
		w += c
	}
	return w
}

// cellLayout lays a cell's inline Markdown out, markers hidden, "<br>" as a
// line break.
func (e *Editor) cellLayout(src string, header bool, width float32) *text.Layout {
	l, _ := e.cellText(src, header, width)
	return l
}

// cellText is cellLayout with the projection the cell was laid out from,
// whose typeset math the grid draws over it.
func (e *Editor) cellText(src string, header bool, width float32) (*text.Layout, projection) {
	src = strings.ReplaceAll(strings.ReplaceAll(src, "<br>", "\n"), "<br/>", "\n")
	r := []rune(src)
	base := e.blockStyle(&Block{Kind: Paragraph})
	if header {
		base.Weight = text.Bold
	}
	proj := e.typesetInline(project(r, parseInline(r), nil), base, true)
	return e.ui.Fonts.Layout(e.runs(proj, proj.flags, base), text.Options{MaxWidth: width, Pitch: e.pitch(base)}), proj
}

// gridFor lays a table block out at its width, from a cache. Its room is
// the text's width, or more under an embedding's TextWidth (wideblocks.go);
// an unsized table that is narrower spans the text's width.
func (e *Editor) gridFor(i int) (*grid, bool) {
	b := &e.Doc.Blocks[i]
	width, fill := e.wideWidth(b), e.textWidth(b)
	key := layoutKey{text: b.Text, attrs: b.Attrs, kind: b.Kind, width: width, fill: fill, caret: -2, generation: e.generation}
	if e.tableResize != nil && e.tableResize.blockID == b.ID {
		// A drag follows the pointer before anything is written, so the
		// preview width joins the key.
		key.width += e.tableResize.width
	}
	if c, ok := e.grids[b.ID]; ok && c.key == key {
		return c.grid, c.grid != nil
	}
	t := ParseTable(b.Text)
	var g *grid
	if t.Valid && len(t.Headers) > 0 {
		g = e.layOutGrid(t, width, fill, storedTableWidths(b), e.resizeOverride(b.ID))
		g.spans = matchedCellSpans(b.Text, t)
	}
	e.grids[b.ID] = cachedGrid{key, g}
	return g, g != nil
}

type cachedGrid struct {
	key  layoutKey
	grid *grid
}

// layOutGrid lays a table out in width. An unsized table narrower than fill
// is widened to span it, unless its columns are fitted to their content
// (tablefit.go).
func (e *Editor) layOutGrid(t PipeTable, width, fill float32, stored []int, override map[int]float32) *grid {
	n := len(t.Headers)
	all := append([][]string{t.Headers}, t.Rows...)
	pad := 2 * e.px(cellPadX)
	natural := make([]float32, n)
	var least []float32
	if e.measuresLeast() {
		least = make([]float32, n)
	}
	for r, row := range all {
		for c, cell := range row {
			l := e.cellLayout(cell, r == 0, 0)
			w, _ := l.Size()
			// The text layer wraps at a whole pixel, so a cell is given a
			// pixel more than its text needs.
			natural[c] = max(natural[c], float32(math.Ceil(float64(w)))+1+pad, e.px(minColumn))
			if least != nil {
				least[c] = max(least[c], float32(math.Ceil(float64(longestPiece(l))))+1+pad, e.px(minColumn))
			}
		}
	}

	// A column the reader has sized keeps that width; the rest measure
	// themselves from content. Only the dragged column is pinned: the rest
	// stay automatic.
	cols := make([]float32, n)
	pinned := make([]bool, n)
	anyStored := false
	for c := range cols {
		if w, ok := override[c]; ok {
			cols[c] = max(e.px(minColumn), w)
			anyStored, pinned[c] = true, true
			continue
		}
		if c < len(stored) && stored[c] > 0 {
			cols[c] = e.px(float32(stored[c]))
			anyStored, pinned[c] = true, true
			continue
		}
		cols[c] = natural[c]
	}
	fitted := least != nil && e.fitColumns(cols, pinned, natural, least, width)
	var sum float32
	for _, w := range cols {
		sum += w
	}
	if !fitted && sum > width && sum > 0 {
		// Widths sized in a wide window would run off a narrow one, so the
		// row scales down together to fit. It never scales up: a table
		// deliberately made narrow stays narrow.
		scale := width / sum
		for c := range cols {
			cols[c] = max(e.px(minColumn), float32(math.Round(float64(cols[c]*scale))))
		}
		sum = 0
		for _, w := range cols {
			sum += w
		}
	}
	if !fitted && !anyStored && n > 0 && sum < fill {
		// Unsized tables finish flush with the blocks around them: the
		// last column takes the rounding difference.
		cols[n-1] += fill - sum
	}
	g := &grid{cols: cols, align: t.Alignments}
	for r, row := range all {
		var h float32
		var cells []*text.Layout
		var projs []projection
		for c, cell := range row {
			l, p := e.cellText(cell, r == 0, max(1, cols[c]-pad))
			_, lh := l.Size()
			h = max(h, lh)
			cells = append(cells, l)
			projs = append(projs, p)
		}
		h += 2 * e.px(cellPadY)
		g.rowsH = append(g.rowsH, h)
		g.cells = append(g.cells, cells)
		g.projs = append(g.projs, projs)
		g.height += h
	}
	return g
}

// tableShowsGrid reports whether a table block is drawn as its grid: when it
// reads as a table, away from the caret, with a live cell in it (a press
// in the grid keeps the grid while the cell is edited), or with a swept
// rectangle in it. A read-only document always draws it as its grid, since
// its Markdown could not be edited there.
func (e *Editor) tableShowsGrid(i int) (*grid, bool) {
	b := &e.Doc.Blocks[i]
	if b.Kind != Table {
		return nil, false
	}
	if e.tableHold[b.ID] || e.Doc.ReadOnly {
		return e.gridFor(i)
	}
	if e.Doc.Focused && e.Doc.Caret.Block == b.ID && !e.tableCellIn(b.ID) {
		if id, _, _, _, _, ok := e.TableSelection(); !ok || id != b.ID {
			return nil, false
		}
	}
	return e.gridFor(i)
}

// tableCellIn reports whether block id holds the live cell.
func (e *Editor) tableCellIn(id int64) bool {
	return e.tableActive != nil && e.tableActive.blockID == id
}

// gridOrigin is where a table block's grid starts, as drawGrid draws it:
// its home, shifted left by how far a grid wider than its room is scrolled.
func (e *Editor) gridOrigin(i int) geom.Point {
	o := e.gridHome(i)
	o.X -= e.tableScrollDX(i)
	return o
}

// gridHome is where a table block's grid starts unscrolled: the code panel's
// text origin. A scrolling grid's view starts here.
func (e *Editor) gridHome(i int) geom.Point {
	return geom.NewPoint(e.bodyLeft()+e.px(codeInset), e.tops[i]+e.px(codeRowTop))
}

// cellRect is cell (row, col) of block i's grid: row -1 is the header, 0..
// the data rows.
func (e *Editor) cellRect(i int, g *grid, row, col int) geom.Rect {
	o := e.gridOrigin(i)
	x := o.X
	for c := 0; c < col && c < len(g.cols); c++ {
		x += g.cols[c]
	}
	y := o.Y
	r := row + 1
	for k := 0; k < r && k < len(g.rowsH); k++ {
		y += g.rowsH[k]
	}
	var w, h float32
	if col >= 0 && col < len(g.cols) {
		w = g.cols[col]
	}
	if r >= 0 && r < len(g.rowsH) {
		h = g.rowsH[r]
	}
	return geom.NewRect(x, y, w, h)
}

// tableCellAt is the cell of block i's grid under where, in the editor's
// coordinates: row -1 for the header, 0.. for data rows, and its column.
func (e *Editor) tableCellAt(i int, g *grid, where geom.Point) (row, col int, ok bool) {
	if !e.inTableView(i, where) {
		return 0, 0, false
	}
	o := e.gridOrigin(i)
	rel := where.Sub(o)
	if rel.X < 0 || rel.Y < 0 {
		return 0, 0, false
	}
	x := float32(0)
	col = -1
	for c, w := range g.cols {
		if rel.X < x+w {
			col = c
			break
		}
		x += w
	}
	if col < 0 {
		return 0, 0, false
	}
	y := float32(0)
	for r, h := range g.rowsH {
		if rel.Y < y+h {
			return r - 1, col, true
		}
		y += h
	}
	return 0, 0, false
}

// storedTableWidths reads the dragged column widths from the block's own
// cols attribute: design pixels in column order, 0 for a column that still
// measures itself.
func storedTableWidths(b *Block) []int {
	raw, ok := b.Attr("cols")
	if !ok || raw == "" {
		return nil
	}
	var out []int
	for _, part := range strings.Split(raw, ",") {
		part = strings.TrimSpace(part)
		if part == "" {
			out = append(out, 0)
			continue
		}
		n, err := strconv.Atoi(part)
		if err != nil || n <= 0 {
			out = append(out, 0)
			continue
		}
		out = append(out, max(minColumn, n))
	}
	return out
}

// formatTableWidths writes a width list back: a zero becomes an empty slot
// for a column that measures itself; an all-zero list drops the key, so a
// table with no sized columns is plain Markdown again.
func formatTableWidths(widths []int) (string, bool) {
	parts := make([]string, len(widths))
	any := false
	for i, w := range widths {
		if w > 0 {
			parts[i] = strconv.Itoa(w)
			any = true
		} else {
			parts[i] = ""
		}
	}
	if !any {
		return "", false
	}
	return strings.Join(parts, ","), true
}

// resizeOverride reports a drag preview for block id: the dragged column's
// width, so the grid follows the pointer before anything is written.
func (e *Editor) resizeOverride(id int64) map[int]float32 {
	if e.tableResize != nil && e.tableResize.blockID == id {
		return map[int]float32{e.tableResize.col: e.tableResize.width}
	}
	return nil
}

func (e *Editor) drawGrid(gc *unison.Canvas, i int, g *grid) {
	t := e.tok()
	o := e.gridOrigin(i)
	total := g.width()
	if view, ok := e.tableClip(i); ok {
		// A grid wider than its room is drawn through its view, with the
		// scrollbar under it.
		gc.Save()
		defer e.drawCodeScroll(gc, i)
		defer gc.Restore()
		gc.ClipRect(view, pathop.Intersect, true)
	}
	e.fill(gc, geom.NewRect(o.X, o.Y, total, g.rowsH[0]), t.ChipBackground)
	id := e.Doc.Blocks[i].ID
	y := o.Y
	for r, h := range g.rowsH {
		x := o.X
		for c, w := range g.cols {
			if e.TableCellSelected(id, r-1, c) {
				e.fill(gc, geom.NewRect(x, y, w, h), t.SelectionTint)
			}
			x += w
		}
		y += h
	}
	// Washes and selected characters behind the text they mark.
	e.drawCellMarks(gc, i, g)
	y = o.Y
	for r, h := range g.rowsH {
		x := o.X
		for c, w := range g.cols {
			l := g.cells[r][c]
			lw, _ := l.Size()
			tx := e.cellTextX(x, w, lw, g.align[c])
			l.Draw(gc, tx, y+e.px(cellPadY))
			drawInlineMath(gc, l, g.projs[r][c], tx, y+e.px(cellPadY))
			x += w
		}
		y += h
	}
	// The rules between cells and round the grid.
	line := e.px(gridLineSize)
	y = o.Y
	for r := 0; r <= len(g.rowsH); r++ {
		e.fill(gc, geom.NewRect(o.X, y, total, line), t.Border)
		if r < len(g.rowsH) {
			y += g.rowsH[r]
		}
	}
	x := o.X
	for c := 0; c <= len(g.cols); c++ {
		e.fill(gc, geom.NewRect(x, o.Y, line, g.height), t.Border)
		if c < len(g.cols) {
			x += g.cols[c]
		}
	}
	e.drawTableDecor(gc, i, g, o, total)
}
