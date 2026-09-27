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
// on the row), a to-do's "check" box and a code block's "copy" button.
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
	}
	return geom.Rect{}
}

// UndoSteps is how many steps Undo can take back.
func (d *Doc) UndoSteps() int { return len(d.undo) }
