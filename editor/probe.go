package editor

// Where things are, for tests and tools that drive the editor from outside:
// the scenarios press gutter buttons and drag across text by these points,
// as a person would, rather than calling the operations directly.

import (
	"strconv"
	"strings"

	"github.com/richardwilkes/toolbox/v2/geom"
)

// RowRect is block i's whole row, gutter included, in the editor's
// coordinates.
func (e *Editor) RowRect(i int) geom.Rect { return e.rowRect(i) }

// TextPoint is the point in the middle of the line of block i where a caret
// before source offset off is drawn, just right of it, in the editor's
// coordinates.
func (e *Editor) TextPoint(i, off int) geom.Point {
	l := e.layout(i)
	x, top := l.caretAt(l.drawn(off))
	o := e.textOrigin(i)
	return geom.NewPoint(o.X+x+1, o.Y+top+l.pitch/2)
}

// CaretRect is the caret's rectangle in the editor's coordinates, and false
// when no block holds the caret.
func (e *Editor) CaretRect() (geom.Rect, bool) { return e.caretRect() }

// PartRect is one of block i's controls in the editor's coordinates: the
// gutter's "menu" and "handle" (shown while the pointer is on the row), a to-do's "check" box, a code block's "copy" button, and a
// card's "load" button and "open" title, and a table's "cell" (or
// "cell:<row>:<col>" for another data cell), "header" (or "header:<col>"
// for another header column), "grip", and "addrow"/"addcol" (the + Row /
// + Column buttons under a live cell), and a code block's "codebar" (its
// horizontal scrollbar, when a long line runs past the panel).
func (e *Editor) PartRect(i int, part string) geom.Rect {
	switch part {
	case "handle":
		return e.gutterCellRect(i, partHandle)
	case "menu":
		return e.gutterCellRect(i, partMenu)
	case "check":
		return e.checkBox(i)
	case "copy":
		return e.copyButton(i)
	case "language":
		return e.languageButton(i)
	case "load", "open":
		card, _, ok := e.embedCard(i)
		if !ok {
			return geom.Rect{}
		}
		title, load := e.embedParts(card)
		if part == "load" {
			return load
		}
		return title
	case "cell":
		if g, ok := e.gridFor(i); ok && len(g.cols) > 0 && len(g.rowsH) > 1 {
			return e.cellRect(i, g, 0, 0)
		}
		return geom.Rect{}
	case "header":
		if g, ok := e.gridFor(i); ok && len(g.cols) > 0 {
			return e.cellRect(i, g, -1, 0)
		}
		return geom.Rect{}
	case "grip":
		if g, ok := e.gridFor(i); ok && len(g.cols) > 1 {
			o := e.gridOrigin(i)
			x := o.X + g.cols[0]
			return geom.NewRect(x-4, o.Y, 8, min(g.rowsH[0], 24))
		}
		return geom.Rect{}
	case "addrow", "addcol":
		if g, ok := e.gridFor(i); ok {
			o := e.gridOrigin(i)
			rowR, colR, ok := e.tableAddRects(i, g, o)
			if !ok {
				return geom.Rect{}
			}
			if part == "addrow" {
				return rowR
			}
			return colR
		}
		return geom.Rect{}
	case "codebar":
		if r, ok := e.codeBarRect(i); ok {
			return r
		}
		return geom.Rect{}
	case "fold", "type", "title", "color":
		chevron, icon, title, dot := e.calloutParts(i)
		return map[string]geom.Rect{"fold": chevron, "type": icon, "title": title, "color": dot}[part]
	}
	if col, ok := headerColumn(part); ok {
		if g, ok := e.gridFor(i); ok && col >= 0 && col < len(g.cols) {
			return e.cellRect(i, g, -1, col)
		}
		return geom.Rect{}
	}
	if row, col, ok := tableCellPart(part); ok {
		if g, ok := e.gridFor(i); ok && col >= 0 && col < len(g.cols) && row+1 >= 0 && row+1 < len(g.rowsH) {
			return e.cellRect(i, g, row, col)
		}
		return geom.Rect{}
	}
	return geom.Rect{}
}

// tableCellPart parses "cell:<row>:<col>" for a data cell beyond (0,0).
func tableCellPart(part string) (row, col int, ok bool) {
	rest, found := strings.CutPrefix(part, "cell:")
	if !found {
		return 0, 0, false
	}
	rs, cs, _ := strings.Cut(rest, ":")
	r, err := strconv.Atoi(rs)
	if err != nil || r < 0 {
		return 0, 0, false
	}
	c, err := strconv.Atoi(cs)
	if err != nil || c < 0 {
		return 0, 0, false
	}
	return r, c, true
}

// headerColumn parses "header:<col>" for a header cell beyond the first.
func headerColumn(part string) (int, bool) {
	col, found := strings.CutPrefix(part, "header:")
	if !found {
		return 0, false
	}
	n, err := strconv.Atoi(col)
	if err != nil || n < 0 {
		return 0, false
	}
	return n, true
}

// UndoSteps is how many steps Undo can take back.
func (d *Doc) UndoSteps() int { return len(d.undo) }

// CaretLineColumn is the caret's 1-based line and column within its block's
// display text: the line breaks are the display text's own newlines, as the
// status bar shows them.
func (e *Editor) CaretLineColumn() (line, col int) {
	d := e.Doc
	b := d.CaretBlock()
	if b == nil || !d.Focused {
		return 1, 1
	}
	i := d.Index(b.ID)
	if i < 0 {
		return 1, 1
	}
	l := e.layout(i)
	at := l.drawn(d.Caret.Off)
	line, col = 1, at+1
	for k := 0; k < at && k < len(l.proj.Disp); k++ {
		if l.proj.Disp[k] == '\n' {
			line++
			col = at - k
		}
	}
	return line, col
}

// PreviewTitle is the title an embed card shows from its page, "" before
// the page is read.
func (e *Editor) PreviewTitle(address string) string {
	if p := e.previews[address]; p != nil {
		return p.Title
	}
	return ""
}
