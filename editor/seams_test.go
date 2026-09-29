package editor

import "testing"

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
