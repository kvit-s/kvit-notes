package editor

// Tables (features.md 1.2.11, Kvit's TableBlock and tabledata.cpp): a
// run of lines starting with "|" is a table block, kept as written. It is
// drawn as a grid, the header row set apart, each cell's inline Markdown
// drawn and its column's alignment kept; a press in a cell makes it live
// for editing in place. Column widths the reader drags live in the block's
// own cols attribute, so they follow the file.

import (
	"math"
	"strconv"
	"strings"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
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
	cols   []float32
	rowsH  []float32
	cells  [][]*text.Layout // [row][col], the header first
	projs  [][]projection   // what each cell draws, for its typeset math
	align  []TableAlign
	height float32
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
	proj := e.typesetInline(project(r, parseInline(r), nil), base)
	return e.ui.Fonts.Layout(e.runs(proj, proj.flags, base), text.Options{MaxWidth: width, Pitch: e.pitch(base)}), proj
}

// gridFor lays a table block out at its width, from a cache.
func (e *Editor) gridFor(i int) (*grid, bool) {
	b := &e.Doc.Blocks[i]
	width := e.textWidth(b)
	key := layoutKey{text: b.Text, attrs: b.Attrs, kind: b.Kind, width: width, caret: -2, generation: e.generation}
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
		g = e.layOutGrid(t, width, storedTableWidths(b), e.resizeOverride(b.ID))
	}
	e.grids[b.ID] = cachedGrid{key, g}
	return g, g != nil
}

type cachedGrid struct {
	key  layoutKey
	grid *grid
}

func (e *Editor) layOutGrid(t PipeTable, width float32, stored []int, override map[int]float32) *grid {
	n := len(t.Headers)
	all := append([][]string{t.Headers}, t.Rows...)
	pad := 2 * e.px(cellPadX)
	natural := make([]float32, n)
	for r, row := range all {
		for c, cell := range row {
			w, _ := e.cellLayout(cell, r == 0, 0).Size()
			// The text layer wraps at a whole pixel, so a cell is given a
			// pixel more than its text needs.
			natural[c] = max(natural[c], float32(math.Ceil(float64(w)))+1+pad, e.px(minColumn))
		}
	}

	// A column the reader has sized keeps that width; the rest measure
	// themselves from content. Only the dragged column is pinned: the rest
	// stay automatic.
	cols := make([]float32, n)
	anyStored := false
	for c := range cols {
		if w, ok := override[c]; ok {
			cols[c] = max(e.px(minColumn), w)
			anyStored = true
			continue
		}
		if c < len(stored) && stored[c] > 0 {
			cols[c] = e.px(float32(stored[c]))
			anyStored = true
			continue
		}
		cols[c] = natural[c]
	}
	var sum float32
	for _, w := range cols {
		sum += w
	}
	if sum > width && sum > 0 {
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
	if !anyStored && n > 0 && sum < width {
		// Unsized tables finish flush with the blocks around them: the
		// last column takes the rounding difference.
		cols[n-1] += width - sum
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
// rectangle in it.
func (e *Editor) tableShowsGrid(i int) (*grid, bool) {
	b := &e.Doc.Blocks[i]
	if b.Kind != Table {
		return nil, false
	}
	if e.tableHold[b.ID] {
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

// gridOrigin is where a table block's grid starts: the code panel's text
// origin, as drawGrid draws it.
func (e *Editor) gridOrigin(i int) geom.Point {
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
	o := geom.NewPoint(e.bodyLeft()+e.px(codeInset), e.tops[i]+e.px(codeRowTop))
	var total float32
	for _, w := range g.cols {
		total += w
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
			l := g.cells[r][c]
			lw, _ := l.Size()
			tx := x + e.px(cellPadX)
			switch g.align[c] {
			case TableAlignCenter:
				tx = x + (w-lw)/2
			case TableAlignRight:
				tx = x + w - e.px(cellPadX) - lw
			}
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
