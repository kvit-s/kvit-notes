package editor

import (
	"testing"

	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// features.md 2.6: Ctrl+Home and Ctrl+End go to the start and the end of
// the note, and with Shift they select to there.
func TestCtrlHomeAndEndReachTheEndsOfTheNote(t *testing.T) {
	s, e := openEditor(t, accessNote)
	s.Do(func() { e.FocusBlock(1, 5) })
	s.Sync()
	s.Screen.KeyPress(unison.KeyEnd, mod.Control)
	s.Do(func() {
		d := e.Doc
		last := d.Blocks[len(d.Blocks)-1]
		if d.Caret.Block != last.ID || d.Caret.Off != len([]rune(last.Text)) {
			t.Errorf("Ctrl+End should reach the end of the last block: %v", d.Caret)
		}
	})
	s.Screen.KeyPress(unison.KeyHome, mod.Control|mod.Shift)
	s.Do(func() {
		d := e.Doc
		if d.Caret.Block != d.Blocks[0].ID || d.Caret.Off != 0 || !d.CrossBlock() {
			t.Errorf("Ctrl+Shift+Home should select back to the start of the note: %v to %v", d.Anchor, d.Caret)
		}
	})
}

// A read-only note moves its caret and copies, and changes nothing.
func TestReadOnlyRefusesEveryChange(t *testing.T) {
	s, e := openEditor(t, accessNote)
	s.Do(func() {
		e.Doc.ReadOnly = true
		e.FocusBlock(1, 0)
	})
	s.Sync()
	before := ""
	s.Do(func() { before = Serialize(e.Doc.Blocks) })
	s.Screen.Type("typed")
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Screen.KeyPress(unison.KeyBackspace, mod.None)
	s.Screen.KeyPress(unison.KeyD, mod.Control)
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	s.Screen.KeyPress(unison.KeyRight, mod.Shift)
	s.Screen.KeyPress(unison.KeyC, mod.Control)
	s.Do(func() {
		if after := Serialize(e.Doc.Blocks); after != before {
			t.Errorf("a read-only note changed:\n%s", after)
		}
		if e.Doc.Caret.Off != 1 || e.Doc.Anchor.Off != 0 {
			t.Errorf("the caret should still move and select: %v %v", e.Doc.Anchor, e.Doc.Caret)
		}
		if got := unison.ClipboardGetText(); got != "T" {
			t.Errorf("copy should still work: %q", got)
		}
	})
}

// Ctrl+Enter on selected blocks puts the caret in a new paragraph after
// them, the keyboard's way below a table or a board.
func TestCtrlEnterAfterSelectedBlocks(t *testing.T) {
	s, e := openEditor(t, "| a | b |\n| --- | --- |\n| 1 | 2 |\n\nAfter\n")
	s.Do(func() {
		e.FocusBlock(1, 0)
		e.blockSel[e.Doc.Blocks[0].ID] = true
		e.RequestFocus()
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		d := e.Doc
		if len(d.Blocks) != 3 || d.Blocks[1].Kind != Paragraph || d.Blocks[1].Text != "" || d.Caret.Block != d.Blocks[1].ID {
			t.Errorf("blocks %d, the second %v %q, caret in %d", len(d.Blocks), d.Blocks[1].Kind, d.Blocks[1].Text, d.Caret.Block)
		}
	})
}
