package editor

// Where things are, for tests and tools that drive the editor from outside:
// the scenarios press gutter buttons and drag across text by these points,
// as a person would, rather than calling the operations directly.

import "github.com/richardwilkes/toolbox/v2/geom"

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
// gutter's "add", "handle", "delete" and "menu" (shown while the pointer is
// on the row), a to-do's "check" box, a code block's "copy" button, and a
// callout's "fold" arrow, "type", "title" and "color" dot, and an embed
// card's "load" button and "open" title.
func (e *Editor) PartRect(i int, part string) geom.Rect {
	switch part {
	case "add":
		return e.gutterCellRect(i, partAdd)
	case "handle":
		return e.gutterCellRect(i, partHandle)
	case "delete":
		return e.gutterCellRect(i, partDelete)
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
	case "fold", "type", "title", "color":
		chevron, icon, title, dot := e.calloutParts(i)
		return map[string]geom.Rect{"fold": chevron, "type": icon, "title": title, "color": dot}[part]
	}
	return geom.Rect{}
}

// UndoSteps is how many steps Undo can take back.
func (d *Doc) UndoSteps() int { return len(d.undo) }

// CaretLineColumn is the caret's 1-based line and column within its block's
// display text (features.md 9.7, Kvit's cursorLineColumn): the line breaks
// are the display text's own newlines, as the status bar shows them.
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
