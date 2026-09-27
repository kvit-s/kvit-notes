package editor

// Printing a note to pages, for PDF export: the note laid out at the width
// of a page's text, cut into pages between rows, each page drawn as the
// editor draws the note, without the caret, the gutter or the page's side
// margins. A row taller than a page is cut where the page ends and goes on
// on the next.

import (
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/pathop"
)

// Page is one page's part of the note, from one y to another in the laid
// out note.
type Page struct{ From, To float32 }

// Paginate lays the note out at a width and cuts it into pages of a height.
func (e *Editor) Paginate(width, height float32) []Page {
	e.Printing = true
	e.Doc.Focused = false
	e.measureAt(width)
	var pages []Page
	from := float32(0)
	for i := range e.tops {
		top, bottom := e.tops[i], e.tops[i]+e.heights[i]
		for bottom-from > height {
			if top > from {
				// The row starts a new page.
				pages = append(pages, Page{from, top})
				from = top
				continue
			}
			// A row taller than a page is cut.
			pages = append(pages, Page{from, from + height})
			from += height
		}
	}
	if len(e.tops) == 0 || e.total() > from {
		pages = append(pages, Page{from, max(e.total(), from)})
	}
	return pages
}

// DrawPage draws one page's part of the note with its top at the canvas's
// origin.
func (e *Editor) DrawPage(gc *unison.Canvas, p Page) {
	gc.Save()
	defer gc.Restore()
	gc.ClipRect(geom.NewRect(0, 0, e.width(), p.To-p.From), pathop.Intersect, true)
	gc.Translate(geom.NewPoint(0, -p.From))
	for i := range e.tops {
		if e.tops[i] >= p.To || e.tops[i]+e.heights[i] <= p.From {
			continue
		}
		e.drawRow(gc, i)
	}
}
