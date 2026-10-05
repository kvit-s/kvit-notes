package editor

import (
	"testing"

	"github.com/richardwilkes/toolbox/v2/geom"
)

// An editor nobody embeds is laid out as a note: the gutter's strip, the
// page margin above the first row, and a third of the window below the last.
func TestANoteIsLaidOutAsANote(t *testing.T) {
	s, e := openEditor(t, accessNote)
	s.Do(func() {
		if _, ok := e.Embedded(); ok {
			t.Error("a note reports an embedding")
		}
		if e.bodyLeft() != e.side()+e.px(gutterWidth+focusBar) || e.tops[0] != e.px(pageMargin) || e.tail() <= e.px(pageMargin) {
			t.Errorf("text from %v, first row at %v, %v below the last", e.bodyLeft(), e.tops[0], e.tail())
		}
		if e.focusBarX() != e.side()+e.px(gutterWidth) || !e.showsCaret() || e.copiedSelection() != e.Doc.SelectedMarkdown() {
			t.Error("a note draws or copies as an embedded editor")
		}
	})
}

// An embedding lays the editor out by its own margin and spacing, and is told
// each time the rows are measured.
func TestAnEmbeddingSetsTheMarginsAndIsToldOfEachLayout(t *testing.T) {
	s, e := openEditor(t, "One\n\nTwo\n")
	laid := 0
	s.Do(func() {
		e.Embed(Embedding{NoGutter: true, Margin: 6, BlockSpacing: 30, Laid: func() { laid++ }})
		laid = 0
		e.Refresh()
		if laid == 0 {
			t.Error("the embedding was not told of the layout")
		}
		if e.tops[0] != e.px(6) || e.tops[1] != e.tops[0]+e.heights[0]+e.px(30) {
			t.Errorf("rows at %v", e.tops)
		}
		if e.tail() != e.px(6) || e.ContentHeight() != e.total()-e.px(6) {
			t.Errorf("%v below the last row, content %v", e.tail(), e.ContentHeight())
		}
		if b, off := e.BlockAtY(e.tops[1] - 1); b != 1 || off != 0 {
			t.Errorf("the space between two rows reads as block %d at %v", b, off)
		}
	})
}

// Tight rows are as tall as their text: the embedding's block spacing is all
// the space between two blocks and its list spacing all the space between
// two items of a list, and a seam lies in the middle of each.
func TestTightRowsAreAsTallAsTheirText(t *testing.T) {
	s, e := openEditor(t, "One\n\n- first\n- second\n\n> Quoted\n")
	s.Do(func() {
		e.Embed(Embedding{NoGutter: true, BlockSpacing: 10, TightRows: true, ListSpacing: 2})
		e.Refresh()
		for i := range e.Doc.Blocks {
			if e.heights[i] != e.layout(i).height() || e.textOrigin(i).Y != e.tops[i] {
				t.Errorf("row %d is %v tall with text %v tall from %v down", i, e.heights[i], e.layout(i).height(), e.textOrigin(i).Y-e.tops[i])
			}
		}
		for i, want := range []float32{10, 2, 10} {
			if got := e.tops[i+1] - e.tops[i] - e.heights[i]; got != e.px(want) {
				t.Errorf("%v between rows %d and %d, want %v", got, i, i+1, e.px(want))
			}
			if y := e.tops[i+1] - e.px(want)/2; e.gapLineY(i+1) != y || e.gapAt(geom.NewPoint(e.width()/2, y)) != i+1 {
				t.Errorf("seam %d at %v, want %v", i+1, e.gapLineY(i+1), y)
			}
		}
	})
}

// A document passes tight rows and its list spacing on to its editor.
func TestADocumentPassesTightRowsOn(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{TightRows: true, ListSpacing: 3}, "- a\n- b\n", 0)
	s.Do(func() {
		e := d.Editor
		if emb, _ := e.Embedded(); !emb.TightRows || emb.ListSpacing != 3 || e.tops[1]-e.tops[0]-e.heights[0] != e.px(3) {
			t.Errorf("embedding %+v, rows at %v and %v tall", emb, e.tops, e.heights)
		}
	})
}
