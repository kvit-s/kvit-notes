package editor

// These tests are the diagram-block functions of the Qt app's
// tests/tst_integration.qml, in that file's order, with the same notes and
// expectations: test_zx0h_ctrlEnterLeavesNoGapUnderAFoldingBlock (its
// diagram case), test_zx0i_pastedFenceBecomesItsBlock,
// test_zx0m_pastedDiagramLeavesTheRowsBelowInPlace,
// test_zzy2_diagramFitFitsTallFlowchartAndShowsZoom,
// test_zzy2b_diagramRightWhitespaceOpensEditor,
// test_zzy3_mermaidSourceEnterKeepsIndent and
// test_zzy5_mermaidSourceCtrlEnterLeavesTheBlock. The Qt tests look for
// objects by name in the QML tree (diagramReadCanvas, mermaidSourceArea,
// mermaidExitHint, diagramZoomText); here the same things are the read
// canvas, the caret in the block, the preview's key hint and the zoom label.
// Where the Qt tests measure the gaps between the list's delegates, these
// check that each row starts one block gap below the one before it.
// test_zx0i's last check, that two lines of plain prose paste as two
// blocks, is left out: the Go editor pastes them into one block with a
// line break, which is the paste's behaviour rather than the diagram's. Three
// of them skip in the Qt app's headless run, for want of a display, and run
// here.

import (
	"fmt"
	"math"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// rowsAdjacent checks that every row starts one block gap below the row
// before it.
func rowsAdjacent(t *testing.T, s *uitest.Session, e *Editor) {
	t.Helper()
	s.Do(func() {
		for k := 0; k+1 < len(e.tops); k++ {
			if gap := e.tops[k+1] - (e.tops[k] + e.heights[k]); gap < e.gap()-1 || gap > e.gap()+1 {
				t.Errorf("row %d follows row %d with a gap of %g, not %g", k+1, k, gap, e.gap())
			}
		}
	})
}

func TestZx0hCtrlEnterLeavesNoGapUnderADiagram(t *testing.T) {
	s, e := openEditor(t, "above\n\n```mermaid\nflowchart TD\n  A[Start] --> B[End]\n```\n")
	i := diagramIndex(e)
	waitRendered(s, e, i)
	var resting float32
	s.Do(func() { resting = e.heights[i] })
	s.Do(func() { e.FocusBlock(i, len([]rune(e.Doc.Blocks[i].Text))) })
	s.WaitFor("the preview", func() bool { return e.diagramFor(&e.Doc.Blocks[i]).preview.hasScene })
	s.Do(func() {
		if d := e.heights[i] - resting; d < 1 && d > -1 {
			t.Errorf("opening the source did not change the row's height: %g", e.heights[i])
		}
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		if len(e.Doc.Blocks) != 3 {
			t.Errorf("blocks = %d, want 3", len(e.Doc.Blocks))
		}
		if e.diagramEditing(i) {
			t.Error("the diagram is still open")
		}
	})
	rowsAdjacent(t, s, e)
}

func TestZx0iPastedFenceBecomesItsBlock(t *testing.T) {
	fence := "```mermaid\nflowchart LR\n    A([Start]) --> B{Vault set?}\n    B -- yes --> C[Open collection]\n```"
	s, e := openEditor(t, "\n")
	s.Do(func() {
		e.FocusBlock(0, 0)
		unison.ClipboardSetText(fence)
	})
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		b := &e.Doc.Blocks[0]
		if !isMermaid(b) {
			t.Errorf("the paste is a %v block, language %q", b.Kind, b.Lang)
		}
		if strings.Contains(b.Text, "```") || !strings.HasPrefix(b.Text, "flowchart LR") {
			t.Errorf("content = %q", b.Text)
		}
	})

	// Inside a code block the same paste is text, markers included.
	s.Do(func() {
		e.SetDoc(NewDoc(ParseMarkdown("```\n\n```\n")))
		e.FocusBlock(0, 0)
		unison.ClipboardSetText(fence)
	})
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		if !strings.Contains(e.Doc.Blocks[0].Text, "```mermaid") || len(e.Doc.Blocks) != 1 {
			t.Errorf("a fence pasted into a listing: %d blocks, %q", len(e.Doc.Blocks), e.Doc.Blocks[0].Text)
		}
	})
}

func TestZx0mPastedDiagramLeavesTheRowsBelowInPlace(t *testing.T) {
	s, e := openEditor(t, "above the paste\n\nbelow one\n\nbelow two\n")
	s.Do(func() { e.FocusBlock(0, len([]rune("above the paste"))) })
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		unison.ClipboardSetText("```mermaid\nflowchart LR\n    A([Start]) --> B{Vault set?}\n    B -- yes --> C[Open collection]\n```")
	})
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		if !isMermaid(&e.Doc.Blocks[1]) {
			t.Errorf("block 1 is %v %q", e.Doc.Blocks[1].Kind, e.Doc.Blocks[1].Lang)
		}
		e.ClearFocus()
	})
	waitRendered(s, e, 1)
	s.Sync()
	rowsAdjacent(t, s, e)
}

func TestZzy2DiagramFitFitsTallFlowchartAndShowsZoom(t *testing.T) {
	s, e := openEditor(t, "```mermaid\nflowchart TD\n  A[a] --> B[b]\n  B --> C[c]\n"+
		"%% mermaid-flow:pos A=100,40 B=100,900 C=100,1760\n```\n")
	i := diagramIndex(e)
	waitRendered(s, e, i)
	s.Do(func() {
		c := readCanvas(e, i)
		if float32(c.scene.Bounds.H) <= e.px(diagramMaxRead) {
			t.Errorf("the fixture is not taller than the read window: %g", c.scene.Bounds.H)
		}
		p := e.diagramReadParts(i)
		if h := float32(c.scene.Bounds.H) * p.scale; h > e.px(diagramMaxRead)+0.5 {
			t.Errorf("fit did not scale the diagram into the height cap: %g", h)
		}
		if p.scale >= 1 {
			t.Errorf("a tall diagram is shown at %g", p.scale)
		}
		if h := float32(c.scene.Bounds.H) * p.scale; h > p.window.Height+0.5 {
			t.Errorf("fit mode needs no scrolling: drawing %g, window %g", h, p.window.Height)
		}
		// The zoom label shows the scale Fit chose.
		if got, want := zoomText(p.scale), fmt.Sprintf("%d%%", int(math.Round(float64(p.scale)*100))); got != want || got == "100%" {
			t.Errorf("zoom label %q, want %q", got, want)
		}
	})
}

func TestZzy2bDiagramRightWhitespaceOpensEditor(t *testing.T) {
	s, e := openEditor(t, "```mermaid\nflowchart LR\n  A[a]\n```\n")
	i := diagramIndex(e)
	waitRendered(s, e, i)
	var at geom.Point
	s.Do(func() {
		c := readCanvas(e, i)
		p := e.diagramReadParts(i)
		if float32(c.scene.Bounds.W)*p.scale >= p.window.Width-40 {
			t.Error("the fixture leaves no whitespace on the right")
		}
		at = geom.NewPoint(p.window.Right()-12, p.window.Y+p.window.Height/2)
	})
	s.Screen.Click(s.Screen.PanelPoint(e, at))
	s.Do(func() {
		if !e.diagramEditing(i) {
			t.Error("clicking the whitespace on the right did not open the source")
		}
	})
}

func TestZzy3MermaidSourceEnterKeepsIndent(t *testing.T) {
	s, e := openEditor(t, "```mermaid\nflowchart LR\n    A[a] --> B[b]\n```\n")
	i := diagramIndex(e)
	s.Do(func() { e.FocusBlock(i, len([]rune(e.Doc.Blocks[i].Text))) })
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if got := e.Doc.Blocks[i].Text; got != "flowchart LR\n    A[a] --> B[b]\n    " {
			t.Errorf("text = %q", got)
		}
		if e.Doc.Caret.Off != len([]rune(e.Doc.Blocks[i].Text)) {
			t.Errorf("the caret is at %d, not after the indent", e.Doc.Caret.Off)
		}
	})
	s.Screen.Type("c")
	s.Do(func() {
		if got := e.Doc.Blocks[i].Text; got != "flowchart LR\n    A[a] --> B[b]\n    c" {
			t.Errorf("text = %q", got)
		}
	})
}

func TestZzy5MermaidSourceCtrlEnterLeavesTheBlock(t *testing.T) {
	s, e := openEditor(t, "```mermaid\nflowchart LR\n    A[a] --> B[b]\n```\n")
	i := diagramIndex(e)
	var source string
	s.Do(func() {
		source = e.Doc.Blocks[i].Text
		e.FocusBlock(i, len([]rune(source)))
		if !e.diagramEditing(i) {
			t.Error("the source did not open")
			return
		}
		if hint := e.diagramPreviewParts(i).hint.Text(); hint != "Ctrl+Enter: new block" {
			t.Errorf("hint = %q", hint)
		}
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		if len(e.Doc.Blocks) != 2 {
			t.Errorf("blocks = %d, want 2", len(e.Doc.Blocks))
			return
		}
		if e.Doc.Blocks[i].Text != source {
			t.Errorf("the diagram's source became %q", e.Doc.Blocks[i].Text)
		}
		nb := e.Doc.CaretBlock()
		if nb == nil || nb.Kind != Paragraph || e.Doc.Index(nb.ID) != i+1 {
			t.Error("the caret is not in a new paragraph below")
		}
		// With the source closed, the hint is gone with the preview.
		if e.diagramEditing(i) {
			t.Error("the diagram's source is still open")
		}
	})
}
