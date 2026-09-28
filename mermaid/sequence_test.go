package mermaid

// These tests are the parser tests of the Qt app's
// tests/test_mermaidsequence.cpp, one Go test per test function there and in
// the same order, with the same inputs and expected outputs. The Qt file's
// layout tests (every function from layoutProducesLifelinesHeadersAndMessage
// on, and rendererRendersSequence) lay the diagram out into a scene and
// belong to the diagram package. Several inputs are from demos/sequence.html
// of mermaid@11.16.0 (MIT license, (c) Knut Sveidqvist).

import (
	"fmt"
	"strings"
	"testing"
)

func firstMessage(r *ParseResult) *SeqEvent {
	for i := range r.Sequence.Events {
		if r.Sequence.Events[i].Kind == EventMessage {
			return &r.Sequence.Events[i]
		}
	}
	return nil
}

func TestSequenceFamilyIsSupported(t *testing.T) {
	r := Parse("sequenceDiagram\n  Alice->>Bob: Hi")
	if r.Type != Sequence || !r.Supported || r.HasErrors() {
		t.Fatalf("type %v supported %v error %q", r.Type, r.Supported, errorsOf(r))
	}
	if len(r.Sequence.Participants) != 2 || r.Sequence.MessageCount() != 1 {
		t.Errorf("participants %d messages %d", len(r.Sequence.Participants), r.Sequence.MessageCount())
	}
}

func TestSequenceHeaderIsCaseInsensitive(t *testing.T) {
	r := Parse("sequencediagram\n  A->>B: x")
	if r.Type != Sequence || !r.Supported {
		t.Errorf("type %v supported %v", r.Type, r.Supported)
	}
}

func TestSequenceParticipantsAliasesAndActors(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  participant A as Alice the First\n" +
		"  actor B as Bob\n" +
		"  participant 3\n" +
		"  A->>B: hi\n" +
		"  3->>A: yo\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	p := r.Sequence.Participants
	if len(p) != 3 {
		t.Fatalf("participants = %d, want 3", len(p))
	}
	if p[0].ID != "A" || p[0].Label != "Alice the First" || p[0].ActorFigure {
		t.Errorf("participant 0 = %+v", p[0])
	}
	if !p[1].ActorFigure || p[1].Label != "Bob" {
		t.Errorf("participant 1 = %+v", p[1])
	}
	if p[2].ID != "3" {
		t.Errorf("participant 2 = %+v", p[2])
	}
}

func TestSequenceAutoDeclaredParticipantsKeepFirstUseOrder(t *testing.T) {
	r := Parse("sequenceDiagram\n  Zed->>Amy: one\n  Amy->>Bob: two")
	var ids []string
	for _, p := range r.Sequence.Participants {
		ids = append(ids, p.ID)
	}
	if strings.Join(ids, ",") != "Zed,Amy,Bob" {
		t.Errorf("participants = %q", ids)
	}
}

func TestSequenceArrowFormsMapToLineAndHead(t *testing.T) {
	cases := []struct {
		arrow string
		line  SeqLine
		head  SeqHead
		bidir bool
	}{
		{"->", SeqSolid, HeadOpen, false},
		{"-->", SeqDotted, HeadOpen, false},
		{"->>", SeqSolid, HeadFilled, false},
		{"-->>", SeqDotted, HeadFilled, false},
		{"<<->>", SeqSolid, HeadFilled, true},
		{"<<-->>", SeqDotted, HeadFilled, true},
		{"-x", SeqSolid, HeadCross, false},
		{"--x", SeqDotted, HeadCross, false},
		{"-)", SeqSolid, HeadPoint, false},
		{"--)", SeqDotted, HeadPoint, false},
	}
	for _, c := range cases {
		r := Parse(fmt.Sprintf("sequenceDiagram\n  A %s B: msg", c.arrow))
		if r.HasErrors() {
			t.Errorf("%s: error %q", c.arrow, errorsOf(r))
		}
		m := firstMessage(&r)
		if m == nil {
			t.Errorf("%s: no message", c.arrow)
			continue
		}
		if m.Line != c.line || m.Head != c.head || m.Bidirectional != c.bidir || m.Text != "msg" {
			t.Errorf("%s: message = %+v", c.arrow, *m)
		}
	}
}

func TestSequenceExoticPinnedArrowsParseWithoutError(t *testing.T) {
	// The half-head and reverse arrows of the grammar must not be refused as
	// unknown statements.
	for _, arrow := range []string{`-|\`, "-|/", `-\\`, "-//", `--|\`, "--|/", `--\\`, "--//"} {
		r := Parse(fmt.Sprintf("sequenceDiagram\n  A %s B: msg", arrow))
		if r.HasErrors() {
			t.Errorf("%s: error %q", arrow, errorsOf(r))
		}
		if firstMessage(&r) == nil {
			t.Errorf("%s: no message", arrow)
		}
	}
}

func TestSequenceActivationShorthandAndStatements(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  Alice->>+John: up\n" +
		"  John-->>-Alice: down\n" +
		"  activate Alice\n" +
		"  deactivate Alice\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	e := r.Sequence.Events
	if len(e) != 4 {
		t.Fatalf("events = %d, want 4", len(e))
	}
	if !e[0].ActivateTarget || !e[1].DeactivateSource || e[2].Kind != EventActivate || e[3].Kind != EventDeactivate {
		t.Errorf("events = %+v", e)
	}
}

func TestSequenceNotesAllPlacements(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  participant A\n" +
		"  participant B\n" +
		"  Note left of A: to the left\n" +
		"  note right of B: to the right\n" +
		"  NOTE over A: on top\n" +
		"  Note over A,B: spanning\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	notes := 0
	for _, e := range r.Sequence.Events {
		if e.Kind != EventNote {
			continue
		}
		notes++
		if e.Text == "spanning" && (e.Placement != PlaceOver || e.From != "A" || e.To != "B") {
			t.Errorf("spanning note = %+v", e)
		}
	}
	if notes != 4 {
		t.Errorf("notes = %d, want 4", notes)
	}
}

func TestSequenceBlocksAndDividers(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  loop every minute\n" +
		"    A->>B: tick\n" +
		"  end\n" +
		"  alt success\n" +
		"    A->>B: ok\n" +
		"  else failure\n" +
		"    A->>B: err\n" +
		"  end\n" +
		"  opt maybe\n" +
		"    A->>B: hm\n" +
		"  end\n" +
		"  par first\n" +
		"    A->>B: p1\n" +
		"  and second\n" +
		"    A->>B: p2\n" +
		"  end\n" +
		"  critical mount\n" +
		"    A->>B: c\n" +
		"  option timeout\n" +
		"    A->>B: t\n" +
		"  end\n" +
		"  break when boom\n" +
		"    A->>B: boom\n" +
		"  end\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	starts, dividers, ends := 0, 0, 0
	for _, e := range r.Sequence.Events {
		switch e.Kind {
		case EventBlockStart:
			starts++
		case EventBlockDivider:
			dividers++
		case EventBlockEnd:
			ends++
		}
	}
	if starts != 6 || dividers != 3 || ends != 6 {
		t.Errorf("starts %d dividers %d ends %d, want 6, 3, 6", starts, dividers, ends)
	}
}

func TestSequenceDividerOutsideItsBlockIsAnError(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  loop x\n" +
		"    else nope\n" +
		"  end\n")
	if !r.HasErrors() || !strings.Contains(r.FirstError().Message, "else") {
		t.Errorf("first error = %q", errorsOf(r))
	}
}

func TestSequenceMissingEndIsAnError(t *testing.T) {
	r := Parse("sequenceDiagram\n  loop forever\n  A->>B: x")
	if !r.HasErrors() || !strings.Contains(r.FirstError().Message, "end") {
		t.Errorf("first error = %q", errorsOf(r))
	}
}

func TestSequenceBoxesGroupParticipants(t *testing.T) {
	// From demos/sequence.html (mermaid, MIT).
	r := Parse("sequenceDiagram\n" +
		"  box lightgreen Alice & John\n" +
		"  participant A\n" +
		"  participant J\n" +
		"  end\n" +
		"  box Another Group\n" +
		"  participant B\n" +
		"  end\n" +
		"  A->>J: Hello John, how are you?\n" +
		"  J->>A: Great!\n" +
		"  A->>B: Hello Bob, how are you ?\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	a := r.Sequence
	if len(a.Boxes) != 2 {
		t.Fatalf("boxes = %d, want 2", len(a.Boxes))
	}
	if a.Boxes[0].Title != "Alice & John" || !a.Boxes[0].Color.Set {
		t.Errorf("box 0 = %+v", a.Boxes[0])
	}
	if a.Boxes[1].Color.Set {
		t.Errorf("box 1 = %+v", a.Boxes[1])
	}
	for i, want := range []int{0, 0, 1} {
		if got := a.Participants[i].BoxIndex; got != want {
			t.Errorf("participant %d box = %d, want %d", i, got, want)
		}
	}
}

func TestSequenceAutonumberVariants(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  autonumber\n" +
		"  A->>B: one\n" +
		"  autonumber 50 10\n" +
		"  A->>B: two\n" +
		"  autonumber off\n" +
		"  A->>B: three\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	var autos []SeqEvent
	for _, e := range r.Sequence.Events {
		if e.Kind == EventAutonumber {
			autos = append(autos, e)
		}
	}
	if len(autos) != 3 {
		t.Fatalf("autonumber events = %d, want 3", len(autos))
	}
	if !autos[0].AutonumberShown {
		t.Error("the first autonumber is off")
	}
	if autos[1].AutonumberStart != 50 || autos[1].AutonumberStep != 10 {
		t.Errorf("second autonumber = %+v", autos[1])
	}
	if autos[2].AutonumberShown {
		t.Error("autonumber off is shown")
	}
}

func TestSequenceRestrictedStatementsWarnNotFail(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  participant Alice\n" +
		"  link Alice: Dashboard @ https://dashboard.contoso.com/alice\n" +
		"  links Alice: {\"Repo\": \"https://x\"}\n" +
		"  create participant D\n" +
		"  destroy D\n" +
		"  participant C@{ \"type\": \"database\" }\n" +
		"  Alice ->> () C: central\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	for _, w := range []string{"link", "create", "destroy", "configuration", "Central connections"} {
		if !hasWarning(r, w) {
			t.Errorf("no warning about %s", w)
		}
	}
}

func TestSequenceEntityEscapesDecode(t *testing.T) {
	r := Parse("sequenceDiagram\n  A->>B: 1 #lt; 2 and #35; is a hash")
	m := firstMessage(&r)
	if m == nil {
		t.Fatal("no message")
	}
	if m.Text != "1 < 2 and # is a hash" {
		t.Errorf("text = %q", m.Text)
	}
}

func TestSequenceTitleAndAccessibility(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  title My interaction\n" +
		"  accTitle: acc title\n" +
		"  accDescr: acc description\n" +
		"  A->>B: x\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	a := r.Sequence
	if a.Title != "My interaction" || a.AccTitle != "acc title" || a.AccDescr != "acc description" {
		t.Errorf("title %q accTitle %q accDescr %q", a.Title, a.AccTitle, a.AccDescr)
	}
}

func TestSequenceCommentsIgnored(t *testing.T) {
	r := Parse("sequenceDiagram\n" +
		"  %% a comment line\n" +
		"  # a hash comment line\n" +
		"  A->>B: hello %% trailing comment\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if r.Sequence.MessageCount() != 1 {
		t.Fatalf("messages = %d, want 1", r.Sequence.MessageCount())
	}
	if got := firstMessage(&r).Text; got != "hello" {
		t.Errorf("text = %q", got)
	}
}

func TestSequenceMessageWithoutColonIsAnError(t *testing.T) {
	r := Parse("sequenceDiagram\n  A->>B no colon")
	if !r.HasErrors() || !strings.Contains(r.FirstError().Message, ":") {
		t.Errorf("first error = %q", errorsOf(r))
	}
}

func TestSequenceDemoCorpusParsesClean(t *testing.T) {
	// Shortened from demos/sequence.html of mermaid@11.16.0 (MIT).
	r := Parse("sequenceDiagram\n" +
		"  autonumber\n" +
		"  Alice->>John: Hello John,<br>how are you?\n" +
		"  autonumber 50 10\n" +
		"  Alice->>John: John,<br />can you hear me?\n" +
		"  John-->>Alice: Hi Alice,<br />I can hear you!\n" +
		"  autonumber off\n" +
		"  John-->>Alice: I feel great!\n" +
		"  Alice-)John: See you later!\n" +
		"  Alice<<->>John: We said that together!\n" +
		"  Alice-xJohn: Bye\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if got := r.Sequence.MessageCount(); got != 7 {
		t.Errorf("messages = %d, want 7", got)
	}
}
