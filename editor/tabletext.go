package editor

// The characters of a table's cells. A table block holds its whole Markdown,
// and its grid draws each cell's text with its inline markers hidden and
// "<br>" as a line break (table.go). This file says where each character a
// cell draws came from in the block's Markdown, which is what lets a cell's
// characters be selected, copied, quoted, placed and marked as the
// characters of any other block are: the selection is the document's own,
// Doc.Anchor and Doc.Caret in the table block's Markdown offsets.
//
// In a read-only document the pointer selects inside one cell: a press and a
// drag that stays in the cell selects the characters between them, a
// double-click a word and a triple-click the cell's whole text, while a drag
// that reaches another cell becomes the rectangle of cells (tableedit.go),
// and coming back to the first cell turns it back into characters. A
// right-click there offers Copy alone. An editable note opens the cell's
// field on a press instead, and selects in that, and its right-click menu
// edits rows and columns.

import (
	"strings"
	"unicode"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// cellSpan is one cell of a table block as the block's Markdown holds it:
// the value ParseTable reads for it, and for each rune k of the value the
// runes [from[k], to[k]) of the block's text it was read from. An escaped
// pipe is two runes of the text for one of the value, and a "<br>", with the
// spaces before it, is several for one line break.
type cellSpan struct {
	value    []rune
	from, to []int
	// at is where an empty cell's text would be.
	at int
}

// start is the block offset of the cell's first character, end the offset
// just after its last.
func (c *cellSpan) start() int {
	if len(c.from) == 0 {
		return c.at
	}
	return c.from[0]
}

func (c *cellSpan) end() int {
	if len(c.to) == 0 {
		return c.at
	}
	return c.to[len(c.to)-1]
}

// valueAt is the value offset that block offset off stands for: the number
// of the cell's characters wholly before it.
func (c *cellSpan) valueAt(off int) int {
	n := 0
	for n < len(c.to) && c.to[n] <= off {
		n++
	}
	return n
}

// sourceStart is the block offset where value character v begins, the
// cell's end for v past its last character; sourceEnd is the block offset
// just after the value characters before v, the cell's start for v at 0.
func (c *cellSpan) sourceStart(v int) int {
	if v < 0 {
		v = 0
	}
	if v >= len(c.from) {
		return c.end()
	}
	return c.from[v]
}

func (c *cellSpan) sourceEnd(v int) int {
	if v <= 0 {
		return c.start()
	}
	return c.to[min(v, len(c.to))-1]
}

// tableCellSpans reads where every cell of a table block's Markdown is, by
// ParseTable's own rules: [0] is the header row, [1..] the data rows, each
// as wide as the header. It is nil when the Markdown is not a table.
func tableCellSpans(markdown string) [][]cellSpan {
	lines := strings.Split(markdown, "\n")
	if len(lines) < 2 || !LooksLikeTableStart(lines[0], lines[1]) {
		return nil
	}
	starts := make([]int, len(lines))
	off := 0
	for k, l := range lines {
		starts[k] = off
		off += len([]rune(l)) + 1
	}
	header := lineCellSpans([]rune(lines[0]), starts[0])
	cols := len(header)
	out := [][]cellSpan{header}
	for k := 2; k < len(lines); k++ {
		if strings.TrimSpace(lines[k]) == "" {
			continue
		}
		r := []rune(lines[k])
		row := lineCellSpans(r, starts[k])
		for len(row) < cols {
			row = append(row, cellSpan{at: starts[k] + len(r)})
		}
		out = append(out, row[:cols])
	}
	return out
}

// lineCellSpans splits one row of a table as splitTableCells does, keeping
// where each character came from: base is the line's offset in the block.
func lineCellSpans(line []rune, base int) []cellSpan {
	lo, hi := 0, len(line)
	for lo < hi && unicode.IsSpace(line[lo]) {
		lo++
	}
	for hi > lo && unicode.IsSpace(line[hi-1]) {
		hi--
	}
	var cells []cellSpan
	var cur []rune
	var from, to []int
	at := base + lo
	flush := func() {
		cells = append(cells, readCellSpan(cur, from, to, at))
		cur, from, to = nil, nil, nil
	}
	for k := lo; k < hi; k++ {
		switch {
		case line[k] == '\\' && k+1 < hi && line[k+1] == '|':
			cur, from, to = append(cur, '|'), append(from, base+k), append(to, base+k+2)
			k++
		case line[k] == '|':
			flush()
			at = base + k + 1
		default:
			cur, from, to = append(cur, line[k]), append(from, base+k), append(to, base+k+1)
		}
	}
	flush()
	if len(cells) > 1 && len(cells[0].value) == 0 {
		cells = cells[1:]
	}
	if len(cells) > 1 && len(cells[len(cells)-1].value) == 0 {
		cells = cells[:len(cells)-1]
	}
	return cells
}

// readCellSpan trims a cell's runes and turns its "<br>"s into line breaks,
// as splitTableCells does to its text.
func readCellSpan(cur []rune, from, to []int, at int) cellSpan {
	lo, hi := 0, len(cur)
	for lo < hi && unicode.IsSpace(cur[lo]) {
		lo++
	}
	for hi > lo && unicode.IsSpace(cur[hi-1]) {
		hi--
	}
	cur, from, to = cur[lo:hi], from[lo:hi], to[lo:hi]
	c := cellSpan{at: at}
	if len(cur) > 0 {
		c.at = from[0]
	}
	s := string(cur)
	// The regular expression answers byte offsets; the spans count runes.
	runeAt := make([]int, len(s)+1)
	n := 0
	for b := range s {
		runeAt[b] = n
		n++
	}
	runeAt[len(s)] = n
	k := 0
	for _, m := range reBrDelim.FindAllStringIndex(s, -1) {
		a, b := runeAt[m[0]], runeAt[m[1]]
		c.value = append(c.value, cur[k:a]...)
		c.from = append(c.from, from[k:a]...)
		c.to = append(c.to, to[k:a]...)
		c.value = append(c.value, '\n')
		c.from = append(c.from, from[a])
		c.to = append(c.to, to[b-1])
		k = b
	}
	c.value = append(c.value, cur[k:]...)
	c.from = append(c.from, from[k:]...)
	c.to = append(c.to, to[k:]...)
	return c
}

// matchedCellSpans is tableCellSpans for a table already read as t, and nil
// when any cell's value differs from what ParseTable read, so a table this
// file reads otherwise than the grid draws it offers no character of it.
func matchedCellSpans(markdown string, t PipeTable) [][]cellSpan {
	spans := tableCellSpans(markdown)
	if len(spans) != len(t.Rows)+1 {
		return nil
	}
	for r, row := range append([][]string{t.Headers}, t.Rows...) {
		if len(spans[r]) != len(row) {
			return nil
		}
		for c, v := range row {
			if string(spans[r][c].value) != v {
				return nil
			}
		}
	}
	return spans
}

// cellHolding is the cell whose text holds block offset off, its row (-1
// the header) and column, from its first character's offset to just after
// its last.
func cellHolding(spans [][]cellSpan, off int) (row, col int, ok bool) {
	for r := range spans {
		for c := range spans[r] {
			if sp := &spans[r][c]; sp.start() <= off && off <= sp.end() {
				return r - 1, c, true
			}
		}
	}
	return 0, 0, false
}

// cellRange is the cell holding all of block offsets from to to, and false
// when they are not both in one cell's text.
func cellRange(spans [][]cellSpan, from, to int) (*cellSpan, bool) {
	row, col, ok := cellHolding(spans, from)
	if !ok {
		return nil, false
	}
	sp := &spans[row+1][col]
	if to < sp.start() || to > sp.end() {
		return nil, false
	}
	return sp, true
}

// tableCellFragment is block offsets from to to of a table block, when they
// are inside one cell, as Markdown of their own: the inline markers of the
// cell's spans balanced as in a paragraph, and its pipes and line breaks
// written back as the table writes them.
func tableCellFragment(b Block, from, to int) (string, bool) {
	sp, ok := cellRange(tableCellSpans(b.Text), from, to)
	if !ok {
		return "", false
	}
	return escapeTableCell(inlineFragment(sp.value, sp.valueAt(from), sp.valueAt(to))), true
}

// tableCellCopy is the characters a cell draws between block offsets from
// and to, when they are inside one cell: plain text with its markers left
// out and an inline formula as its TeX between $ signs, as copying a
// paragraph's characters gives them.
func tableCellCopy(b Block, from, to int) (string, bool) {
	sp, ok := cellRange(tableCellSpans(b.Text), from, to)
	if !ok {
		return "", false
	}
	p := project(sp.value, parseInline(sp.value), func(s span) bool { return s.Kind == sMath })
	return string(p.Disp[p.S2D[sp.valueAt(from)]:p.S2D[sp.valueAt(to)]]), true
}

// Where a cell's characters are drawn.

// cellTextOrigin is where cell (row, col) of block i's grid draws its text
// from, as drawGrid places it in its column's alignment.
func (e *Editor) cellTextOrigin(i int, g *grid, row, col int) geom.Point {
	r := e.cellRect(i, g, row, col)
	lw, _ := g.cells[row+1][col].Size()
	return geom.NewPoint(e.cellTextX(r.X, r.Width, lw, g.align[col]), r.Y+e.px(cellPadY))
}

// cellTextX is the x a cell's text of width lw starts at in a column at x,
// w wide, in its alignment.
func (e *Editor) cellTextX(x, w, lw float32, align TableAlign) float32 {
	switch align {
	case TableAlignCenter:
		return x + (w-lw)/2
	case TableAlignRight:
		return x + w - e.px(cellPadX) - lw
	}
	return x + e.px(cellPadX)
}

// cellDrawn is the drawn offset in cell (row, col) of block offset off.
func (g *grid) cellDrawn(row, col, off int) int {
	sp := &g.spans[row+1][col]
	p := g.projs[row+1][col]
	return p.S2D[min(sp.valueAt(off), len(p.S2D)-1)]
}

// cellSource is the block offsets of drawn offsets lo to hi of cell (row,
// col): from its first drawn character's source to just after its last's,
// so a run ending at a closing marker does not take the marker in, and a
// formula drawn as one character takes its whole TeX.
func (g *grid) cellSource(row, col, lo, hi int) (from, to int) {
	sp := &g.spans[row+1][col]
	p := g.projs[row+1][col]
	vLo := len(sp.value)
	if lo < len(p.D2S) {
		vLo = p.D2S[lo]
	}
	from = sp.sourceStart(vLo)
	if hi <= lo {
		return from, from
	}
	vHi := len(sp.value)
	for v := range p.S2D {
		if p.S2D[v] >= hi {
			vHi = v
			break
		}
	}
	return from, sp.sourceEnd(vHi)
}

// gridCell is block i's grid with the cell holding block offset off, false
// when the block draws no grid or off is in no cell's text.
func (e *Editor) gridCell(i, off int) (g *grid, row, col int, ok bool) {
	if i < 0 || i >= len(e.Doc.Blocks) {
		return nil, 0, 0, false
	}
	g, ok = e.tableShowsGrid(i)
	if !ok || g.spans == nil {
		return nil, 0, 0, false
	}
	row, col, ok = cellHolding(g.spans, off)
	return g, row, col, ok
}

// cellCaretRect is where a caret before block offset off of block i's grid
// is drawn, one line of its cell tall, and false when off is in no cell.
// A grid scrolled sideways keeps the answer inside its view.
func (e *Editor) cellCaretRect(i, off int) (geom.Rect, bool) {
	g, row, col, ok := e.gridCell(i, off)
	if !ok {
		return geom.Rect{}, false
	}
	l := g.cells[row+1][col]
	x, top, h := l.CaretAt(g.cellDrawn(row, col, off))
	o := e.cellTextOrigin(i, g, row, col)
	x += o.X
	if sg, ok := e.tableScrolls(i); ok {
		v := e.tableView(i, sg)
		x = min(max(x, v.X), v.Right()-1)
	}
	return geom.NewRect(x, o.Y+top, 1, h), true
}

// cellRangeRects is where block offsets from to to of block i's grid are
// drawn: one rectangle for each line of each cell they cross, cut to the
// view of a grid scrolled sideways.
func (e *Editor) cellRangeRects(i int, g *grid, from, to int) []geom.Rect {
	if g.spans == nil || from >= to {
		return nil
	}
	var view geom.Rect
	sg, scrolls := e.tableScrolls(i)
	if scrolls {
		view = e.tableView(i, sg)
	}
	var out []geom.Rect
	for r := range g.spans {
		for c := range g.spans[r] {
			sp := &g.spans[r][c]
			lo, hi := max(from, sp.start()), min(to, sp.end())
			if lo >= hi {
				continue
			}
			a, b := g.cellDrawn(r-1, c, lo), g.cellDrawn(r-1, c, hi)
			for _, rect := range lineRects(g.cells[r][c], a, b, e.cellTextOrigin(i, g, r-1, c)) {
				if scrolls {
					left, right := max(rect.X, view.X), min(rect.Right(), view.Right())
					if right <= left {
						continue
					}
					rect.X, rect.Width = left, right-left
				}
				out = append(out, rect)
			}
		}
	}
	return out
}

// gridPos is the block offset under a point in block i's grid, for a sweep
// that started outside the table: in the cell under it, or the nearest cell
// of the row level with it; the table's start above the grid and its end
// below.
func (e *Editor) gridPos(i int, g *grid, p geom.Point) int {
	n := len(runes(e.Doc.Blocks[i].Text))
	o := e.gridOrigin(i)
	switch {
	case g.spans == nil:
		return 0
	case p.Y < o.Y:
		return 0
	case p.Y >= o.Y+g.height:
		return n
	}
	x0, x1 := o.X, o.X+g.width()-1
	if sg, ok := e.tableScrolls(i); ok {
		v := e.tableView(i, sg)
		x0, x1 = max(x0, v.X), min(x1, v.Right()-1)
	}
	q := geom.NewPoint(min(max(p.X, x0), x1), p.Y)
	row, col, ok := e.tableCellAt(i, g, q)
	if !ok {
		return n
	}
	d := e.cellHit(i, g, row, col, q)
	from, _ := g.cellSource(row, col, d, d)
	return from
}

// cellHit is the drawn offset of cell (row, col) of block i's grid nearest
// to a point, which may be outside the cell.
func (e *Editor) cellHit(i int, g *grid, row, col int, p geom.Point) int {
	l := g.cells[row+1][col]
	o := e.cellTextOrigin(i, g, row, col)
	_, h := l.Size()
	y := min(max(p.Y-o.Y, 0), max(0, h-1))
	return min(l.IndexAt(p.X-o.X, y), len(g.projs[row+1][col].D2S))
}

// Selecting a cell's characters with the pointer, in a read-only document.

// The units a cell's characters are picked in: by character after a press,
// by word after a double-click, and the whole text after a triple-click.
const (
	pickChars = iota
	pickWords
	pickAll
)

// cellPick is a press in a cell of a read-only table, until it is let go:
// the cell, and the drawn offsets the press itself selected, one place for
// a press, a word for a double-click, the whole text for a triple.
type cellPick struct {
	blockID  int64
	row, col int
	a, b     int
	unit     int
}

// pressCellText starts a selection of a cell's characters where a press
// landed in a read-only table, anchoring the rectangle of cells (which only
// shows once the pointer reaches another cell) at the same cell.
func (e *Editor) pressCellText(i int, g *grid, row, col int, where geom.Point, clicks int) {
	e.beginTableSweep(i, row, col)
	if g.spans == nil {
		return
	}
	d := e.cellHit(i, g, row, col, where)
	pk := &cellPick{blockID: e.Doc.Blocks[i].ID, row: row, col: col, a: d, b: d}
	disp := g.projs[row+1][col].Disp
	switch {
	case clicks >= 3:
		pk.a, pk.b, pk.unit = 0, len(disp), pickAll
	case clicks == 2:
		pk.a, pk.b = wordAt(disp, d)
		pk.unit = pickWords
	}
	e.cellPick = pk
	e.selectCellText(i, g, row, col, pk.a, pk.b)
}

// dragCellText follows the pointer of a press in a cell of a read-only
// table: within that cell, or anywhere off the grid while no rectangle has
// been swept, it selects characters of the cell; in another cell it drops
// them, and reports false so the caller sweeps the rectangle.
func (e *Editor) dragCellText(i int, g *grid, p geom.Point, row, col int, inCell bool) bool {
	pk, sw := e.cellPick, e.tableSweep
	if pk == nil || sw == nil || pk.blockID != sw.blockID || g.spans == nil {
		return false
	}
	home := inCell && row == pk.row && col == pk.col
	if inCell && !home {
		e.Doc.Anchor = e.Doc.Caret
		return false
	}
	if !home && (sw.focusRow != sw.anchorRow || sw.focusCol != sw.anchorCol) {
		// Off the grid with a rectangle swept: the rectangle stays.
		return true
	}
	sw.focusRow, sw.focusCol = sw.anchorRow, sw.anchorCol
	d := e.cellHit(i, g, pk.row, pk.col, p)
	a, b := pk.a, d
	switch pk.unit {
	case pickWords:
		w0, w1 := wordAt(g.projs[pk.row+1][pk.col].Disp, d)
		if d >= pk.a {
			b = max(pk.b, w1)
		} else {
			a, b = pk.b, w0
		}
	case pickAll:
		b = pk.b
	}
	e.selectCellText(i, g, pk.row, pk.col, a, b)
	return true
}

// selectCellText makes drawn offsets a to b of cell (row, col) the
// document's selection, anchored at a, in the table block's Markdown
// offsets. a and b equal selects nothing.
func (e *Editor) selectCellText(i int, g *grid, row, col, a, b int) {
	d := e.Doc
	id := d.Blocks[i].ID
	from, to := g.cellSource(row, col, min(a, b), max(a, b))
	if b < a {
		from, to = to, from
	}
	d.Anchor, d.Caret = Pos{id, from}, Pos{id, to}
	d.Focused = true
	e.changed()
}

// readOnlyCellMenuItems is the menu a right-click in cell (row, col) of a
// read-only table opens: Copy, of the selected characters or the rectangle
// of cells, since nothing in the table can be changed. A right-click outside
// what is selected drops it first, as a right-click outside a paragraph's
// selection does, and Copy is then unavailable.
func (e *Editor) readOnlyCellMenuItems(i int, g *grid, row, col int, where geom.Point) []kvitui.MenuItem {
	switch {
	case e.HasTableSelection():
		if !e.TableCellSelected(e.Doc.Blocks[i].ID, row, col) {
			e.clearTableSweep()
		}
	case e.Doc.HasSelection():
		on := false
		if from, to, ok := e.Doc.SelectionIn(i); ok {
			for _, r := range e.cellRangeRects(i, g, from, to) {
				on = on || where.In(r)
			}
		}
		if !on {
			e.Doc.Anchor = e.Doc.Caret
			e.changed()
		}
	}
	return []kvitui.MenuItem{{Text: "Copy", Disabled: !e.HasTableSelection() && !e.Doc.HasSelection(), OnSelect: func() {
		if e.HasTableSelection() {
			e.copyTableSelection()
			return
		}
		e.copyText(e.copiedSelection())
	}}}
}

// drawCellMarks paints, behind block i's cell text, the washes of the
// decorations' spans and the part of the text selection in the block.
func (e *Editor) drawCellMarks(gc *unison.Canvas, i int, g *grid) {
	if e.seams != nil && e.seams.deco != nil {
		for _, s := range e.seams.deco.spans {
			if s.block != i || s.style&SpanWash == 0 {
				continue
			}
			for _, r := range e.cellRangeRects(i, g, s.start, s.start+s.length) {
				gc.DrawRect(r, s.color.Paint(gc, r, paintstyle.Fill))
			}
		}
	}
	if from, to, ok := e.Doc.SelectionIn(i); ok {
		for _, r := range e.cellRangeRects(i, g, from, to) {
			e.fill(gc, r, e.tok().SelectionTint)
		}
	}
}
