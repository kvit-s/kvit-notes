package diagram

// These tests are the layout and render functions of the app's
// tests/test_mermaidstate.cpp, one Go test per test function and in the
// same order, with the same sources and expectations; the parser's
// functions of that file are ported in package mermaid. Each name has
// "State" added, because every family's test file has a
// layoutDeterministic. The test tells a note's line by ::DashLine; here
// it is LineDashed.

import (
	"strings"
	"testing"
)

func TestStateLayoutStartEndAndTransition(t *testing.T) {
	r := parse("stateDiagram-v2\n  [*] --> Still\n  Still --> [*]")
	s := LayoutState(&r.State, testOpts())
	if s.Bounds.W <= 0 {
		t.Errorf("bounds = %v", s.Bounds)
	}
	// The start disc, the end's two circles, and the state's box.
	if len(s.Shapes) < 4 {
		t.Errorf("shapes = %d, want at least 4", len(s.Shapes))
	}
	arrows := 0
	for _, p := range s.Paths {
		if p.EndMarker == MarkerArrow {
			arrows++
		}
	}
	if arrows != 2 {
		t.Errorf("arrows = %d, want 2", arrows)
	}
	if !strings.Contains(s.Summary, "1 state") {
		t.Errorf("summary = %q", s.Summary)
	}
}

func TestStateLayoutCompositeGroupContainsMembers(t *testing.T) {
	r := parse("stateDiagram-v2\n" +
		"  [*] --> First\n" +
		"  state First {\n" +
		"    [*] --> second\n" +
		"  }\n")
	s := LayoutState(&r.State, testOpts())
	if len(s.Groups) != 1 {
		t.Fatalf("groups = %d, want 1", len(s.Groups))
	}
	if s.Groups[0].Title != "First" {
		t.Errorf("title = %q, want First", s.Groups[0].Title)
	}
	// The member's box lies inside the composite's frame.
	inside := false
	for _, sh := range s.Shapes {
		inside = inside || sh.NodeID == "second" && s.Groups[0].Rect.Contains(sh.Rect.Center())
	}
	if !inside {
		t.Error("the member is not inside the composite")
	}
}

func TestStateLayoutNoteAndTether(t *testing.T) {
	r := parse("stateDiagram-v2\n" +
		"  Active --> Idle\n" +
		"  note right of Active : remember me\n")
	s := LayoutState(&r.State, testOpts())
	note, tether := false, false
	for _, sh := range s.Shapes {
		note = note || sh.FillRole == RoleNoteFill
	}
	for _, p := range s.Paths {
		tether = tether || p.Style == LineDashed && p.StrokeRole == RoleNoteStroke
	}
	if !note || !tether {
		t.Errorf("note, tether = %t, %t", note, tether)
	}
}

func TestStateLayoutDeterministic(t *testing.T) {
	src := "stateDiagram-v2\n" +
		"  [*] --> A\n  A --> B : go\n  B --> C\n  C --> A : loop\n"
	r1, r2 := parse(src), parse(src)
	if !sameShapePositions(LayoutState(&r1.State, testOpts()), LayoutState(&r2.State, testOpts())) {
		t.Error("two layouts of the same source differ")
	}
}

func TestStateRendererRendersStateDiagram(t *testing.T) {
	ClearCache()
	r := Render("stateDiagram-v2\n  [*] --> Working", testOpts())
	if !r.Valid || r.UnsupportedFamily || r.HasError {
		t.Errorf("valid %t, unsupported %t, error %t", r.Valid, r.UnsupportedFamily, r.HasError)
	}
}
