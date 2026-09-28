package editor

// Horizontal scrolling for code blocks (features.md 1.2.7, Kvit's
// EditableBlock.qml codeChrome and CodeBlockChrome.qml): long lines do not
// wrap, the caret follows along them, and the scrollbar and the wheel move
// the text.

import (
	"strings"
	"testing"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

const codeLongLine = "result = compute(alpha, beta, gamma, delta, epsilon, zeta, eta, theta, iota, kappa, lambda, mu, nu, xi, omicron, pi, rho, sigma, tau)  # a deliberately long single line that exceeds the panel width by a wide margin"

var codeScrollNote = "```python\nshort = 1\n" + codeLongLine + "\nend = 2\n```\n"

// A long line in a code block does not wrap: one visual line per source
// line, with content past the viewport and a scrollbar under the panel.
func TestCodeLongLinesDoNotWrap(t *testing.T) {
	s, e := openEditor(t, codeScrollNote)
	s.Sync()
	s.Do(func() {
		b := &e.Doc.Blocks[0]
		if b.Kind != Code {
			t.Fatalf("kind %v, want Code", b.Kind)
		}
		if !e.codeNoWrap(b) {
			t.Fatal("a code block should lay out without wrapping")
		}
		l := e.layout(0)
		if got := l.lines(); got != 3 {
			t.Fatalf("a 3-line fence should draw 3 lines, drew %d", got)
		}
		view, content := e.codeViewport(0), e.codeContent(0)
		if content <= view {
			t.Fatalf("the long line should exceed the viewport: content %g, viewport %g", content, view)
		}
		if mx := e.codeMaxScroll(0); mx <= 0 {
			t.Fatalf("nothing to scroll: max %g", mx)
		}
		if r, ok := e.codeBarRect(0); !ok || r.Width <= 0 || r.Height <= 0 {
			t.Fatalf("no scrollbar track: %+v ok=%v", r, ok)
		}
		if r := e.PartRect(0, "codebar"); r.Width <= 0 {
			t.Fatalf("no codebar probe rect: %+v", r)
		}
		// A paragraph with the same long line wraps instead.
		p := &Block{Kind: Paragraph, Text: codeLongLine}
		if e.codeNoWrap(p) {
			t.Fatal("a paragraph should still wrap")
		}
	})
}

// The caret at the end of a long line scrolls it into view; Home and End
// stay on the source line.
func TestCodeCaretScrollsIntoView(t *testing.T) {
	s, e := openEditor(t, codeScrollNote)
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	longLen := len([]rune(codeLongLine))
	s.Do(func() { e.FocusBlock(0, len([]rune("short = 1\n"))+longLen) })
	s.Sync()
	s.Do(func() {
		if got := e.codeScrollOf(id); got <= 0 {
			t.Fatalf("the caret at the end of a long line should scroll it: %g", got)
		}
		r, ok := e.CaretRect()
		if !ok {
			t.Fatal("no caret")
		}
		view := e.codeViewportRect(0)
		if r.X < view.X || r.X > view.Right() {
			t.Fatalf("the caret should be in the viewport: caret %g, viewport %+v", r.X, view)
		}
	})
	// Home goes to the start of the source line and scrolls home.
	s.Screen.KeyPress(unison.KeyHome, mod.None)
	s.Sync()
	s.Do(func() {
		lines := strings.Split(e.Doc.Blocks[0].Text, "\n")
		off := len([]rune(lines[0])) + 1
		if e.Doc.Caret.Off != off {
			t.Fatalf("Home should reach the start of the long line: off %d, want %d", e.Doc.Caret.Off, off)
		}
		if got := e.codeScrollOf(id); got != 0 {
			t.Fatalf("Home should scroll home: %g", got)
		}
	})
	// End goes to the end of the source line and scrolls back out.
	s.Screen.KeyPress(unison.KeyEnd, mod.None)
	s.Sync()
	s.Do(func() {
		lines := strings.Split(e.Doc.Blocks[0].Text, "\n")
		off := len([]rune(lines[0])) + 1 + len([]rune(lines[1]))
		if e.Doc.Caret.Off != off {
			t.Fatalf("End should reach the end of the long line: off %d, want %d", e.Doc.Caret.Off, off)
		}
		if got := e.codeScrollOf(id); got <= 0 {
			t.Fatalf("End should scroll out: %g", got)
		}
	})
}

// The scrollbar moves the text: a press jumps the thumb to the pointer and
// a drag carries it, clamped to the ends.
func TestCodeBarDragScrolls(t *testing.T) {
	s, e := openEditor(t, codeScrollNote)
	s.Sync()
	var bar geom.Point
	s.Do(func() {
		r := e.PartRect(0, "codebar")
		if r.Width <= 0 {
			t.Fatalf("no scrollbar to press: %+v", r)
		}
		bar = s.Screen.PanelPoint(e, geom.NewPoint(r.Right()-2, r.Y+r.Height/2))
	})
	s.Screen.MouseDown(bar, unison.ButtonLeft, mod.None)
	s.Sync()
	var scrolled float32
	s.Do(func() { scrolled = e.codeScrollOf(e.Doc.Blocks[0].ID) })
	if scrolled <= 0 {
		t.Fatalf("a press at the track's end should scroll: %g", scrolled)
	}
	s.Do(func() {
		r := e.PartRect(0, "codebar")
		bar = s.Screen.PanelPoint(e, geom.NewPoint(r.X+2, r.Y+r.Height/2))
	})
	s.Screen.MouseDown(bar, unison.ButtonLeft, mod.None)
	s.Sync()
	s.Do(func() {
		if got := e.codeScrollOf(e.Doc.Blocks[0].ID); got != 0 {
			t.Fatalf("a press at the track's start should scroll home: %g", got)
		}
	})
	s.Screen.MouseUp(bar, unison.ButtonLeft, mod.None)
	s.Sync()
	// A horizontal wheel turn scrolls; a vertical one without Shift does not.
	s.Do(func() {
		before := e.codeScrollOf(e.Doc.Blocks[0].ID)
		if e.codeWheel(0, geom.NewPoint(0, 1), mod.None) {
			t.Fatal("a vertical wheel turn without Shift should stay with the region")
		}
		if !e.codeWheel(0, geom.NewPoint(-2, 0), mod.None) {
			t.Fatal("a horizontal wheel turn should scroll the code")
		}
		after := e.codeScrollOf(e.Doc.Blocks[0].ID)
		if after <= before {
			t.Fatalf("the wheel should scroll out: %g -> %g", before, after)
		}
		if !e.codeWheel(0, geom.NewPoint(0, 2), mod.Shift) {
			t.Fatal("Shift+wheel should scroll the code")
		}
	})
}

// Printing wraps code again, so a PDF holds every line.
func TestPrintingWrapsCode(t *testing.T) {
	s, e := openEditor(t, codeScrollNote)
	s.Sync()
	var wrapped, unwrapped int
	s.Do(func() { unwrapped = e.layout(0).lines() })
	s.Do(func() {
		e.Printing = true
		clear(e.layouts)
	})
	s.Do(func() { wrapped = e.layout(0).lines() })
	s.Do(func() { e.Printing = false })
	if unwrapped != 3 {
		t.Fatalf("unwrapped: %d lines, want 3", unwrapped)
	}
	if wrapped <= unwrapped {
		t.Fatalf("printing should wrap the long line: %d vs %d", wrapped, unwrapped)
	}
}
