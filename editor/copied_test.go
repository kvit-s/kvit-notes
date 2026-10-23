package editor

// The copy controls say "Copied" for a moment after a copy: a code block's
// Copy, pressed with the pointer or by a screen reader, and a diagram's
// Copy and Copy as text. The tests read the clipboard, the word each control
// is drawn with, where the controls are, what a screen reader is told, what
// the application's status line is given, and how long the word stays.

import (
	"cmp"
	"slices"
	"testing"
	"time"

	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
)

// waitForWord waits until word says want, at most copiedFor and two seconds
// after since, and is how long after since that was.
func waitForWord(t *testing.T, s *uitest.Session, want string, since time.Time, word func() string) time.Duration {
	t.Helper()
	for time.Since(since) < copiedFor+2*time.Second {
		var got string
		s.Do(func() { got = word() })
		if got == want {
			return time.Since(since)
		}
		time.Sleep(20 * time.Millisecond)
		s.Sync()
	}
	t.Fatalf("waited %v for the control to say %q", copiedFor+2*time.Second, want)
	return 0
}

// A code block's Copy puts the block's text on the clipboard and says
// "Copied" until copiedFor has passed, and a screen reader hears "Copied"
// once; a screen reader's Press does the same and moves the word to its
// block; a press on the part of "Copied" left of where "Copy" is drawn
// copies again and starts the time again. The button stays where it is
// throughout, and the status line is told nothing.
func TestCodeCopySaysCopied(t *testing.T) {
	s, e := openEditor(t, "```go\nfmt.Println(\"one\")\n```\n\n```\nthe second block\n```\n")
	var statuses []string
	s.Do(func() { e.OnStatus = func(m string) { statuses = append(statuses, m) } })
	word := func(i int) func() string {
		return func() string { return e.copyWord(e.Doc.Blocks[i].ID, partCopy, "Copy") }
	}
	says := func(i int) (w string) {
		s.Do(func() { w = word(i)() })
		return w
	}
	clipboard := func(want string) {
		t.Helper()
		s.Do(func() {
			if got := unison.ClipboardGetText(); got != want {
				t.Errorf("the clipboard holds %q, want %q", got, want)
			}
		})
	}
	heard := func(want ...string) {
		t.Helper()
		if got := s.Screen.Announcements(); !slices.Equal(got, want) {
			t.Errorf("a screen reader was told %q, want %q", got, want)
		}
	}
	var btn geom.Rect
	s.Do(func() { btn = e.PartRect(0, "copy") })
	s.Screen.Announcements()

	s.Screen.Click(s.Screen.PanelPoint(e, btn.Center()))
	clipboard("fmt.Println(\"one\")")
	if w := says(0); w != "Copied" {
		t.Errorf("after a press the button says %q", w)
	}
	if w := says(1); w != "Copy" {
		t.Errorf("the other block's button says %q", w)
	}
	heard("Copied")
	s.Do(func() {
		if r := e.PartRect(0, "copy"); r != btn {
			t.Errorf("the button moved from %v to %v", btn, r)
		}
	})

	var nodes []*accessibility.Node
	for _, n := range s.Tree().Nodes {
		if n.Name == "Copy code" {
			nodes = append(nodes, n)
		}
	}
	if len(nodes) != 2 {
		t.Fatalf("%d Copy code buttons, want 2", len(nodes))
	}
	slices.SortFunc(nodes, func(a, b *accessibility.Node) int { return cmp.Compare(a.Bounds.Y, b.Bounds.Y) })
	if !s.Screen.PerformAccessibilityAction(accessibility.ActionRequest{Node: nodes[1].ID, Action: accessibility.Press}) {
		t.Fatal("the second block's Copy code refused a Press")
	}
	s.Sync()
	clipboard("the second block")
	if a, b := says(0), says(1); a != "Copy" || b != "Copied" {
		t.Errorf("after a Press on the second block the buttons say %q and %q", a, b)
	}
	heard("Copied")

	time.Sleep(copiedFor / 2)
	var left geom.Point
	s.Do(func() {
		unison.ClipboardSetText("")
		r := e.PartRect(1, "copy")
		left = geom.NewPoint(r.X+e.px(codeCopyPad)+1, r.Center().Y)
	})
	again := time.Now()
	s.Screen.Click(s.Screen.PanelPoint(e, left))
	clipboard("the second block")
	if took := waitForWord(t, s, "Copy", again, word(1)); took < copiedFor {
		t.Errorf("the button said Copied for %v after the copy that started the time again, want %v", took, copiedFor)
	}
	if len(statuses) != 0 {
		t.Errorf("the status line was given %q", statuses)
	}
}

// chipOf is diagram block i's control part, as it is laid out and what it
// says.
func chipOf(e *Editor, i int, part gutterPart) diagramChip {
	chips, _ := e.diagramChips(i)
	for _, c := range chips {
		if c.part == part {
			return c
		}
	}
	return diagramChip{}
}

// A diagram's Copy puts the source on the clipboard and says "Copied" until
// copiedFor has passed, without moving any control, and a screen reader
// hears "Copied" while the status line is told nothing. Copy as text says
// "Copied" too and keeps its message.
func TestDiagramCopySaysCopied(t *testing.T) {
	src := "flowchart LR\n  A[Start] --> B[End]"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	waitRendered(s, e, i)
	var statuses []string
	s.Do(func() { e.OnStatus = func(m string) { statuses = append(statuses, m) } })
	rects := func() (out []geom.Rect) {
		s.Do(func() {
			chips, _ := e.diagramChips(i)
			for _, c := range chips {
				out = append(out, c.rect)
			}
		})
		return out
	}
	words := func() (copyWord, textWord string) {
		s.Do(func() { copyWord, textWord = chipOf(e, i, partDiagramCopy).label, chipOf(e, i, partDiagramText).label })
		return copyWord, textWord
	}
	before := rects()
	s.Screen.Announcements()

	pressed := time.Now()
	clickChip(t, s, e, i, partDiagramCopy)
	s.Do(func() {
		if got := unison.ClipboardGetText(); got != src {
			t.Errorf("Copy put %q on the clipboard", got)
		}
	})
	if c, x := words(); c != "Copied" || x != "Copy as text" {
		t.Errorf("after Copy the controls say %q and %q", c, x)
	}
	if after := rects(); !slices.Equal(after, before) {
		t.Errorf("the controls moved from %v to %v", before, after)
	}
	if got := s.Screen.Announcements(); !slices.Equal(got, []string{"Copied"}) {
		t.Errorf("a screen reader was told %q", got)
	}
	if len(statuses) != 0 {
		t.Errorf("the status line was given %q", statuses)
	}
	if took := waitForWord(t, s, "Copy", pressed, func() string { return chipOf(e, i, partDiagramCopy).label }); took < copiedFor {
		t.Errorf("Copy said Copied for %v, want %v", took, copiedFor)
	}

	clickChip(t, s, e, i, partDiagramText)
	if c, x := words(); c != "Copy" || x != "Copied" {
		t.Errorf("after Copy as text the controls say %q and %q", c, x)
	}
	if after := rects(); !slices.Equal(after, before) {
		t.Errorf("the controls moved from %v to %v", before, after)
	}
	if !slices.Equal(statuses, []string{"Copied the diagram as text"}) {
		t.Errorf("the status line was given %q", statuses)
	}
	if got := s.Screen.Announcements(); !slices.Equal(got, []string{"Copied the diagram as text"}) {
		t.Errorf("a screen reader was told %q", got)
	}
}
