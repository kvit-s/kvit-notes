package editor

import "testing"

// A printed code panel has no header or footer strip: its code sits as far
// from the panel's top and bottom as from its side, and the row is shorter
// than on screen by what the two strips took.
func TestPrintedCodePanel(t *testing.T) {
	s, e := openEditor(t, "```go\nq := 7\n```\n")
	s.Sync()
	s.Do(func() {
		screen := e.heights[0]
		e.Paginate(500, 700)
		defer func() { e.Printing = false }()
		p, o, h := e.codePanel(0), e.textOrigin(0), e.layout(0).height()
		near := func(a, b float32) bool { return a-b < 0.01 && b-a < 0.01 }
		if top, bottom := o.Y-p.Y, p.Bottom()-(o.Y+h); !near(top, e.px(codePadSide)) || !near(bottom, e.px(codePadSide)) {
			t.Errorf("the code is %g from the panel's top and %g from its bottom, want %g", top, bottom, e.px(codePadSide))
		}
		if want := screen - e.px(codeHeader+codeFooter-2*(codePadSide-codeTextPad)); !near(e.heights[0], want) {
			t.Errorf("the printed row is %g high, want %g (%g on screen)", e.heights[0], want, screen)
		}
	})
}
