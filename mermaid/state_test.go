package mermaid

// These tests check the state-diagram parser; laying state diagrams out is
// tested in the diagram package. Some inputs follow demos/state.html of
// mermaid@11.16.0 (MIT license, (c) Knut Sveidqvist).

import (
	"slices"
	"strings"
	"testing"
)

func stateNode(a *StateAst, id string) *StateNode {
	if i := a.IndexOfState(id); i >= 0 {
		return &a.States[i]
	}
	return nil
}

func TestStateFamilyIsSupported(t *testing.T) {
	for _, header := range []string{"stateDiagram", "stateDiagram-v2"} {
		r := Parse(header + "\n  [*] --> Still\n  Still --> [*]")
		if r.Type != State || !r.Supported || r.HasErrors() {
			t.Errorf("%s: type %v supported %v error %q", header, r.Type, r.Supported, errorsOf(r))
		}
	}
}

func TestStateStartAndEndPseudoStates(t *testing.T) {
	r := Parse("stateDiagram-v2\n" +
		"  [*] --> Still\n" +
		"  Still --> Moving\n" +
		"  Moving --> [*]\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	starts, ends := 0, 0
	for _, s := range r.State.States {
		switch s.Kind {
		case StateStart:
			starts++
		case StateEnd:
			ends++
		}
	}
	if starts != 1 || ends != 1 {
		t.Errorf("starts %d ends %d, want 1 and 1", starts, ends)
	}
	if len(r.State.Transitions) != 3 {
		t.Errorf("transitions = %d, want 3", len(r.State.Transitions))
	}
}

func TestStateTransitionLabels(t *testing.T) {
	r := Parse("stateDiagram-v2\n  Still --> Moving : start moving")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if len(r.State.Transitions) != 1 {
		t.Fatalf("transitions = %d, want 1", len(r.State.Transitions))
	}
	if got := r.State.Transitions[0].Label; got != "start moving" {
		t.Errorf("label = %q", got)
	}
}

func TestStateLongDescriptionsAndColonText(t *testing.T) {
	r := Parse("stateDiagram-v2\n" +
		"  state \"This is a state description\" as s2\n" +
		"  s2 : more detail\n" +
		"  s2 : and another line\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	s := stateNode(&r.State, "s2")
	if s == nil || s.Label != "This is a state description" || len(s.Descriptions) != 2 {
		t.Errorf("s2 = %+v", s)
	}
}

func TestStateCompositeStatesScopeStartAndMembers(t *testing.T) {
	r := Parse("stateDiagram-v2\n" +
		"  [*] --> First\n" +
		"  state First {\n" +
		"    [*] --> second\n" +
		"    second --> [*]\n" +
		"  }\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	a := &r.State
	first := stateNode(a, "First")
	if first == nil || !first.Composite {
		t.Errorf("First = %+v", first)
	}
	second := stateNode(a, "second")
	if second == nil || second.ParentIndex != a.IndexOfState("First") {
		t.Errorf("second = %+v", second)
	}
	// The [*] inside the composite state is not the top level's.
	starts := 0
	for _, s := range a.States {
		if s.Kind == StateStart {
			starts++
		}
	}
	if starts != 2 {
		t.Errorf("starts = %d, want 2", starts)
	}
}

func TestStateForkJoinChoice(t *testing.T) {
	r := Parse("stateDiagram-v2\n" +
		"  state fork1 <<fork>>\n" +
		"  state join1 <<join>>\n" +
		"  state pick <<choice>>\n" +
		"  [*] --> fork1\n" +
		"  fork1 --> A\n" +
		"  fork1 --> B\n" +
		"  A --> join1\n" +
		"  B --> join1\n" +
		"  join1 --> pick\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	for id, want := range map[string]StateKind{"fork1": StateFork, "join1": StateJoin, "pick": StateChoice} {
		if got := stateNode(&r.State, id).Kind; got != want {
			t.Errorf("%s kind = %v, want %v", id, got, want)
		}
	}
}

func TestStateNotesSingleAndMultiline(t *testing.T) {
	r := Parse("stateDiagram-v2\n" +
		"  Active --> Idle\n" +
		"  note right of Active : quick note\n" +
		"  note left of Idle\n" +
		"    a longer note\n" +
		"    across lines\n" +
		"  end note\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	n := r.State.Notes
	if len(n) != 2 {
		t.Fatalf("notes = %d, want 2", len(n))
	}
	if n[0].StateID != "Active" || n[0].LeftOf {
		t.Errorf("note 0 = %+v", n[0])
	}
	if !n[1].LeftOf || !strings.Contains(n[1].Text, "across lines") {
		t.Errorf("note 1 = %+v", n[1])
	}
}

func TestStateDirectionAndStyling(t *testing.T) {
	r := Parse("stateDiagram-v2\n" +
		"  direction LR\n" +
		"  classDef badBadEvent fill:#f00\n" +
		"  A --> B:::badBadEvent\n" +
		"  class A badBadEvent\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if r.State.Direction != LR {
		t.Errorf("direction = %v", r.State.Direction)
	}
	if _, ok := r.State.ClassDefs["badBadEvent"]; !ok {
		t.Error("no classDef badBadEvent")
	}
	for _, id := range []string{"B", "A"} {
		if got := stateNode(&r.State, id).CSSClasses; !slices.Contains(got, "badBadEvent") {
			t.Errorf("%s classes = %q", id, got)
		}
	}
}

func TestStateRestrictedStatementsWarnNotFail(t *testing.T) {
	r := Parse("stateDiagram-v2\n" +
		"  A --> B\n" +
		"  scale 350 width\n" +
		"  click A href \"https://example.com\"\n" +
		"  state C {\n" +
		"    D --> E\n" +
		"    --\n" +
		"    F --> G\n" +
		"  }\n" +
		"  hide empty description\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	warnings := 0
	for _, d := range r.Diagnostics {
		if d.Severity == SeverityWarning {
			warnings++
		}
	}
	if warnings < 3 { // scale, click, the concurrency divider
		t.Errorf("warnings = %d, want 3 or more", warnings)
	}
}

func TestStateMissingBraceIsAnError(t *testing.T) {
	r := Parse("stateDiagram-v2\n  state A {\n  B --> C")
	if !r.HasErrors() || !strings.Contains(r.FirstError().Message, "}") {
		t.Errorf("first error = %q", errorsOf(r))
	}
}
