package diagram

// These tests check the sequence-diagram layout and renderer: lifelines,
// headers and messages, activation bars and notes, self-messages, frames
// for blocks, autonumbering, columns widened by a long label, and that the
// same source lays out the same way twice. The sequence-diagram parser is
// tested in package mermaid.

import (
	"math"
	"strings"
	"testing"
)

func TestSequenceLayoutProducesLifelinesHeadersAndMessage(t *testing.T) {
	r := parse("sequenceDiagram\n  Alice->>Bob: Hello Bob")
	s := LayoutSequence(&r.Sequence, testOpts())
	if s.Bounds.W <= 0 || s.Bounds.H <= 0 {
		t.Errorf("bounds = %v", s.Bounds)
	}
	// Two lifelines and one message.
	if len(s.Paths) != 3 {
		t.Errorf("paths = %d, want 3", len(s.Paths))
	}
	// A header box at the top of each lifeline and one at the bottom.
	headers := 0
	for _, sh := range s.Shapes {
		if sh.NodeID != "" {
			headers++
		}
	}
	if headers != 4 {
		t.Errorf("headers = %d, want 4", headers)
	}
	if !strings.Contains(s.Summary, "2 participants") || !strings.Contains(s.Summary, "1 message") {
		t.Errorf("summary = %q", s.Summary)
	}
}

func TestSequenceLayoutMessageArrowSpansLifelines(t *testing.T) {
	r := parse("sequenceDiagram\n  A->>B: go\n  B-->>A: back")
	s := LayoutSequence(&r.Sequence, testOpts())
	// The messages are the paths with markers.
	var messages []Path
	for _, p := range s.Paths {
		if p.EndMarker != MarkerNone {
			messages = append(messages, p)
		}
	}
	if len(messages) != 2 {
		t.Fatalf("messages = %d, want 2", len(messages))
	}
	if messages[0].EndPoint.X <= messages[0].StartPoint.X {
		t.Error("the first message does not run to the right")
	}
	if messages[1].EndPoint.X >= messages[1].StartPoint.X {
		t.Error("the reply does not run to the left")
	}
	if messages[1].Style != LineDashed {
		t.Errorf("the reply's style = %d, want dashed", messages[1].Style)
	}
}

func TestSequenceLayoutActivationBarsAndNotes(t *testing.T) {
	r := parse("sequenceDiagram\n" +
		"  Alice->>+John: up\n" +
		"  Note right of John: thinking\n" +
		"  John-->>-Alice: down\n")
	s := LayoutSequence(&r.Sequence, testOpts())
	activation, note := false, false
	for _, sh := range s.Shapes {
		activation = activation || sh.FillRole == RoleActivation
		note = note || sh.FillRole == RoleNoteFill
	}
	if !activation {
		t.Error("no activation bar")
	}
	if !note {
		t.Error("no note")
	}
}

func TestSequenceLayoutSelfMessageLoops(t *testing.T) {
	r := parse("sequenceDiagram\n  A->>A: think")
	s := LayoutSequence(&r.Sequence, testOpts())
	loop := false
	for _, p := range s.Paths {
		// A message to oneself comes back on a lower row.
		if p.EndMarker != MarkerNone && math.Abs(p.EndPoint.Y-p.StartPoint.Y) > 4 {
			loop = true
		}
	}
	if !loop {
		t.Error("no loop")
	}
}

func TestSequenceLayoutFramesForBlocks(t *testing.T) {
	r := parse("sequenceDiagram\n" +
		"  alt happy\n" +
		"    A->>B: yes\n" +
		"  else sad\n" +
		"    A->>B: no\n" +
		"  end\n")
	s := LayoutSequence(&r.Sequence, testOpts())
	chip, condition, divider := false, false, false
	for _, tx := range s.Texts {
		chip = chip || tx.Text == "alt"
		condition = condition || tx.Text == "[happy]"
		divider = divider || tx.Text == "[sad]"
	}
	if !chip || !condition || !divider {
		t.Errorf("chip, condition, divider = %t, %t, %t", chip, condition, divider)
	}
}

func TestSequenceLayoutAutonumberPrefixesLabels(t *testing.T) {
	r := parse("sequenceDiagram\n  autonumber\n  A->>B: first\n  B->>A: second")
	s := LayoutSequence(&r.Sequence, testOpts())
	first, second := false, false
	for _, tx := range s.Texts {
		first = first || tx.Text == "1. first"
		second = second || tx.Text == "2. second"
	}
	if !first || !second {
		t.Errorf("first, second = %t, %t", first, second)
	}
}

func TestSequenceLayoutDeterministic(t *testing.T) {
	src := "sequenceDiagram\n" +
		"  participant A\n  participant B\n  participant C\n" +
		"  A->>B: one\n  B->>C: two\n  C-->>A: three\n" +
		"  Note over A,C: wide note\n"
	r1, r2 := parse(src), parse(src)
	s1 := LayoutSequence(&r1.Sequence, testOpts())
	s2 := LayoutSequence(&r2.Sequence, testOpts())
	if len(s1.Paths) != len(s2.Paths) || !sameShapePositions(s1, s2) {
		t.Error("two layouts of the same source differ")
	}
}

func TestSequenceLayoutWideLabelExpandsColumns(t *testing.T) {
	rn := parse("sequenceDiagram\n  A->>B: hi")
	rw := parse("sequenceDiagram\n  A->>B: a very very very long message label that needs room")
	narrow := LayoutSequence(&rn.Sequence, testOpts())
	wide := LayoutSequence(&rw.Sequence, testOpts())
	if wide.Bounds.W <= narrow.Bounds.W+50 {
		t.Errorf("wide %g is not 50 more than narrow %g", wide.Bounds.W, narrow.Bounds.W)
	}
}

func TestSequenceRendererRendersSequence(t *testing.T) {
	ClearCache()
	r := Render("sequenceDiagram\n  Alice->>Bob: Hi", testOpts())
	if !r.Valid || r.UnsupportedFamily || r.HasError || r.Scene.Empty() {
		t.Errorf("valid %t, unsupported %t, error %t, empty %t", r.Valid, r.UnsupportedFamily, r.HasError, r.Scene.Empty())
	}
}
