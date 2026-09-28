package editor

import (
	"testing"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// A press in the seam between two blocks arms a caret there; typing makes a
// paragraph in that space holding what was typed.
func TestGapCaretInsertsOnTyping(t *testing.T) {
	s, e := openEditor(t, "first\n\nsecond\n")
	var pt geom.Point
	s.Do(func() {
		y := e.gapLineY(1)
		x := e.side() + e.px(gapLeftInset+10)
		pt = s.Screen.PanelPoint(e, geom.NewPoint(x, y))
	})
	s.Screen.MouseMove(pt, mod.None)
	var hover int
	s.Do(func() { hover = e.gapHover })
	if hover != 1 {
		t.Fatalf("hover seam: %d", hover)
	}
	s.Screen.Click(pt)
	s.Do(func() {
		if e.gapArmed != 1 {
			t.Fatalf("armed seam: %d", e.gapArmed)
		}
	})
	s.Screen.Type("hi")
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:first | Paragraph:hi | Paragraph:second" {
			t.Errorf("after typing in the seam: %s", got)
		}
		if e.gapArmed >= 0 {
			t.Errorf("the seam should disarm after typing")
		}
	})
}

// Enter in a seam leaves an empty paragraph.
func TestGapCaretKeys(t *testing.T) {
	s, e := openEditor(t, "first\n\nsecond\n")
	var pt geom.Point
	s.Do(func() {
		y := e.gapLineY(1)
		x := e.side() + e.px(gapLeftInset+10)
		pt = s.Screen.PanelPoint(e, geom.NewPoint(x, y))
	})
	s.Screen.Click(pt)
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:first | Paragraph: | Paragraph:second" {
			t.Errorf("Enter in the seam: %s", got)
		}
	})
}

// Up/Down moves the armed seam; Escape leaves for the end of the block above.
func TestGapCaretMovesAndLeaves(t *testing.T) {
	s, e := openEditor(t, "one\n\ntwo\n\nthree\n")
	var pt geom.Point
	s.Do(func() {
		y := e.gapLineY(1)
		x := e.side() + e.px(gapLeftInset+10)
		pt = s.Screen.PanelPoint(e, geom.NewPoint(x, y))
	})
	s.Screen.Click(pt)
	s.Screen.KeyPress(unison.KeyDown, mod.None)
	s.Do(func() {
		if e.gapArmed != 2 {
			t.Errorf("Down should move the seam: %d", e.gapArmed)
		}
	})
	s.Screen.KeyPress(unison.KeyEscape, mod.None)
	s.Do(func() {
		if e.gapArmed >= 0 {
			t.Errorf("Escape should leave the seam")
		}
		if e.Doc.Caret.Block != e.Doc.Blocks[1].ID {
			t.Errorf("Escape leaves for the end of the block above")
		}
	})
}

// "/" in a seam makes the paragraph and opens the block menu.
func TestGapCaretSlashOpensTheMenu(t *testing.T) {
	s, e := openEditor(t, "first\n\nsecond\n")
	var pt geom.Point
	s.Do(func() {
		y := e.gapLineY(1)
		x := e.side() + e.px(gapLeftInset+10)
		pt = s.Screen.PanelPoint(e, geom.NewPoint(x, y))
	})
	s.Screen.Click(pt)
	s.Screen.Type("/")
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:first | Paragraph:/ | Paragraph:second" {
			t.Errorf("slash in the seam: %s", got)
		}
		if e.menu == nil {
			t.Errorf("the block menu should open")
		}
	})
}

// Ctrl+V pastes at the armed seam: flat text as paragraphs, structured as
// blocks, plain stripped.
func TestGapCaretPaste(t *testing.T) {
	s, e := openEditor(t, "first\n\nsecond\n")
	var pt geom.Point
	s.Do(func() {
		y := e.gapLineY(1)
		x := e.side() + e.px(gapLeftInset+10)
		pt = s.Screen.PanelPoint(e, geom.NewPoint(x, y))
		unison.ClipboardSetText("one\ntwo")
	})
	s.Screen.Click(pt)
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:first | Paragraph:one | Paragraph:two | Paragraph:second" {
			t.Errorf("paste in the seam: %s", got)
		}
	})
}
