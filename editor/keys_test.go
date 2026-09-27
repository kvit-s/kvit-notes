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
