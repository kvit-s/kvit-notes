package diagram

// Not a port of one Qt test: this is the half of canvasSelectionAndLinking
// in the Qt app's tests/test_diagramlayout.cpp that the scene answers
// itself, with the same source. The canvas's selection, revision gating and
// text export move to the editor with the rest of that test.

import (
	"slices"
	"strings"
	"testing"
)

func TestSceneHitTesting(t *testing.T) {
	src := "flowchart LR\n  A[Start] --> B"
	r := Render(src, testOpts())
	if !r.Valid {
		t.Fatal("not valid")
	}
	s := r.Scene
	a, b := shapeRect(s, "A").Center(), shapeRect(s, "B").Center()
	if got := s.NodeAt(a); got != "A" {
		t.Errorf("node at A's centre = %q", got)
	}
	if got := s.NodeAt(b); got != "B" {
		t.Errorf("node at B's centre = %q", got)
	}
	// Halfway between the centres is on edge 0; the corner is empty.
	if got := s.EdgeAt(a.Add(b).Div(2)); got != 0 {
		t.Errorf("edge at the middle = %d, want 0", got)
	}
	if got := s.NodeAt(Point{2, 2}); got != "" {
		t.Errorf("node at (2, 2) = %q", got)
	}
	if got := s.EdgeAt(Point{2, 2}); got != -1 {
		t.Errorf("edge at (2, 2) = %d", got)
	}

	off := s.NodeSource("A")
	if off < 0 || string([]rune(src)[off]) != "A" {
		t.Fatalf("A's source offset = %d", off)
	}
	if line := strings.Count(string([]rune(src)[:off]), "\n") + 1; line != 2 {
		t.Errorf("A is on line %d, want 2", line)
	}
	if got := s.SourceOffsetAt(a); got != off {
		t.Errorf("offset at A's centre = %d, want %d", got, off)
	}
	if !slices.Equal(s.NodeIDs(), []string{"A", "B"}) {
		t.Errorf("node ids = %q", s.NodeIDs())
	}
	if node, edge := s.ElementAtOffset(off); node != "A" || edge != -1 {
		t.Errorf("element at A's offset = %q, %d", node, edge)
	}
	arrow := strings.Index(src, "-->")
	if node, edge := s.ElementAtOffset(len([]rune(src[:arrow])) + 1); node != "" || edge != 0 {
		t.Errorf("element in the arrow = %q, %d", node, edge)
	}
}
