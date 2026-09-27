package editor

// Tables (features.md 1.2.11): a run of lines starting with "|" is a table
// block, kept as written. Away from the caret it is drawn as a grid, the
// header row set apart, each cell's inline Markdown drawn and its column's
// alignment kept (Kvit's src/content/tabledata.cpp reads the same pipe
// table); with the caret in it, it shows its Markdown to edit, as a code
// block does.

import (
	"math"
	"strings"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// Column alignments.
const (
	alignNone = iota
	alignLeft
	alignCenter
	alignRight
)

// table is a pipe table read into cells.
type table struct {
	header []string
	align  []int
	rows   [][]string
}

// splitRow splits a table line into its cells at the pipes that are not
// escaped, dropping the border pipes, as TableData::parse does.
func splitRow(line string) []string {
	line = strings.TrimSpace(line)
	line = strings.TrimPrefix(line, "|")
	if strings.HasSuffix(line, "|") && !strings.HasSuffix(line, `\|`) {
		line = line[:len(line)-1]
	}
	var cells []string
	var b strings.Builder
	for i := 0; i < len(line); i++ {
		if line[i] == '\\' && i+1 < len(line) && line[i+1] == '|' {
			b.WriteByte('|')
			i++
			continue
		}
		if line[i] == '|' {
			cells = append(cells, strings.TrimSpace(b.String()))
			b.Reset()
			continue
		}
		b.WriteByte(line[i])
	}
	return append(cells, strings.TrimSpace(b.String()))
}

func alignOf(delim string) int {
	d := strings.TrimSpace(delim)
	left, right := strings.HasPrefix(d, ":"), strings.HasSuffix(d, ":")
	switch {
	case left && right:
		return alignCenter
	case right:
		return alignRight
	case left:
		return alignLeft
	}
	return alignNone
}

// parseTable reads a pipe table; ok is false when its second line is not a
// delimiter row, which leaves the block drawn as its Markdown.
func parseTable(src string) (t table, ok bool) {
	lines := strings.Split(src, "\n")
	if len(lines) < 2 {
		return t, false
	}
	t.header = splitRow(lines[0])
	delim := splitRow(lines[1])
	for _, d := range delim {
		if strings.Trim(strings.TrimSpace(d), ":-") != "" || !strings.Contains(d, "-") {
			return t, false
		}
	}
	for c := range t.header {
		a := alignNone
		if c < len(delim) {
			a = alignOf(delim[c])
		}
		t.align = append(t.align, a)
	}
	for _, l := range lines[2:] {
		cells := splitRow(l)
		// Rows are squared up to the header's width, as Kvit squares them.
		for len(cells) < len(t.header) {
			cells = append(cells, "")
		}
		t.rows = append(t.rows, cells[:len(t.header)])
	}
	return t, true
}

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
	align  []int
	height float32
}

// cellLayout lays a cell's inline Markdown out, markers hidden, "<br>" as a
// line break.
func (e *Editor) cellLayout(src string, header bool, width float32) *text.Layout {
	src = strings.ReplaceAll(strings.ReplaceAll(src, "<br>", "\n"), "<br/>", "\n")
	r := []rune(src)
	proj := project(r, parseInline(r), nil)
	base := e.blockStyle(&Block{Kind: Paragraph})
	if header {
		base.Weight = text.Bold
	}
	return e.ui.Fonts.Layout(e.runs(proj, proj.flags, base), text.Options{MaxWidth: width, Pitch: e.pitch(base)})
}

// gridFor lays a table block out at its width, from a cache.
func (e *Editor) gridFor(i int) (*grid, bool) {
	b := &e.Doc.Blocks[i]
	width := e.textWidth(b)
	key := layoutKey{text: b.Text, kind: b.Kind, width: width, caret: -2, generation: e.generation}
	if c, ok := e.grids[b.ID]; ok && c.key == key {
		return c.grid, c.grid != nil
	}
	t, ok := parseTable(b.Text)
	var g *grid
	if ok && len(t.header) > 0 {
		g = e.layOutGrid(t, width)
	}
	e.grids[b.ID] = cachedGrid{key, g}
	return g, g != nil
}

type cachedGrid struct {
	key  layoutKey
	grid *grid
}

func (e *Editor) layOutGrid(t table, width float32) *grid {
	n := len(t.header)
	all := append([][]string{t.header}, t.rows...)
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
	var sum float32
	for _, w := range natural {
		sum += w
	}
	cols := natural
	if sum > width {
		// Too wide: every column gives up room in proportion to its width,
		// down to the narrowest, and its text wraps.
		cols = make([]float32, n)
		for c := range natural {
			cols[c] = max(e.px(minColumn), natural[c]*width/sum)
		}
	}
	g := &grid{cols: cols, align: t.align}
	for r, row := range all {
		var h float32
		var cells []*text.Layout
		for c, cell := range row {
			l := e.cellLayout(cell, r == 0, max(1, cols[c]-pad))
			_, lh := l.Size()
			h = max(h, lh)
			cells = append(cells, l)
		}
		h += 2 * e.px(cellPadY)
		g.rowsH = append(g.rowsH, h)
		g.cells = append(g.cells, cells)
		g.height += h
	}
	return g
}

// tableShowsGrid reports whether a table block is drawn as its grid: away
// from the caret, and when it reads as a table.
func (e *Editor) tableShowsGrid(i int) (*grid, bool) {
	b := &e.Doc.Blocks[i]
	if b.Kind != Table || (e.Doc.Focused && e.Doc.Caret.Block == b.ID) {
		return nil, false
	}
	return e.gridFor(i)
}

func (e *Editor) drawGrid(gc *unison.Canvas, i int, g *grid) {
	t := e.tok()
	o := geom.NewPoint(e.bodyLeft()+e.px(codeInset), e.tops[i]+e.px(codeRowTop))
	var total float32
	for _, w := range g.cols {
		total += w
	}
	e.fill(gc, geom.NewRect(o.X, o.Y, total, g.rowsH[0]), t.ChipBackground)
	y := o.Y
	for r, h := range g.rowsH {
		x := o.X
		for c, w := range g.cols {
			l := g.cells[r][c]
			lw, _ := l.Size()
			tx := x + e.px(cellPadX)
			switch g.align[c] {
			case alignCenter:
				tx = x + (w-lw)/2
			case alignRight:
				tx = x + w - e.px(cellPadX) - lw
			}
			l.Draw(gc, tx, y+e.px(cellPadY))
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
}
