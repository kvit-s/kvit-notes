package editor

import (
	"testing"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// Dragging the handle of a selected block moves the whole selection to the
// drop gap in one undo step, with the selection following it.
func TestMultiBlockDragMovesTheSelection(t *testing.T) {
	s, e := openEditor(t, "one\n\ntwo\n\nthree\n\nfour\n")
	var from, to geom.Point
	s.Do(func() {
		r := e.PartRect(0, "handle")
		from = s.Screen.PanelPoint(e, geom.NewPoint(r.X+r.Width/2, r.Y+r.Height/2))
		// Below the last row: the gap at the end.
		last := e.RowRect(3)
		to = s.Screen.PanelPoint(e, geom.NewPoint(r.X+r.Width/2, last.Y+last.Height+e.px(4)))
		e.SetBlockSelection([]int64{e.Doc.Blocks[0].ID, e.Doc.Blocks[1].ID})
		e.RequestFocus()
	})
	s.Sync()
	s.Screen.MouseDown(from, unison.ButtonLeft, mod.None)
	for k := 1; k <= 6; k++ {
		s.Screen.MouseMove(from.Add(to.Sub(from).Mul(float32(k)/6)), mod.None)
	}
	var gap int
	s.Do(func() {
		if e.drag != nil {
			gap = e.drag.gap
		} else {
			gap = -2
		}
	})
	if gap != 4 {
		t.Fatalf("drop gap: %d", gap)
	}
	s.Screen.MouseUp(to, unison.ButtonLeft, mod.None)
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:three | Paragraph:four | Paragraph:one | Paragraph:two" {
			t.Fatalf("after multi-drag: %s", got)
		}
		if len(e.SelectedBlocks()) != 2 {
			t.Errorf("the selection should follow the moved blocks: %v", e.SelectedBlocks())
		}
	})
	s.Do(func() { e.Doc.Undo() })
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:one | Paragraph:two | Paragraph:three | Paragraph:four" {
			t.Errorf("after one undo: %s", got)
		}
	})
}
