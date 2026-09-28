package mermaid

// These tests are the Qt app's tests/test_mermaidedits.cpp, one Go test per
// test function there and in the same order, with the same inputs and
// expected outputs. Eight of its functions lay a diagram out or render it and
// are ported in the diagram package instead: partialParseIsNotValid,
// cleanParseIsStillValid, hugeCoordinatesDoNotExplodeSceneBounds, the four
// arrangedMode… functions and pluginWrittenLinesParse. Where the Qt test
// passed QColor("#ff0000") or an invalid QColor, this one passes
// ParseColor("#ff0000") or the zero Color, and positions are NodePosition
// values rather than pairs of an id and a QPointF.

import (
	"fmt"
	"math"
	"regexp"
	"slices"
	"strings"
	"testing"
)

// differsOnlyInPosLine reports whether before and after are the same once
// their pos lines are taken out: the edit changed nothing else.
func differsOnlyInPosLine(before, after string) bool {
	strip := func(s string) []string {
		var kept []string
		for _, line := range strings.Split(s, "\n") {
			if !strings.HasPrefix(trimSpace(line), "%% mermaid-flow:pos") {
				kept = append(kept, line)
			}
		}
		return kept
	}
	return slices.Equal(strip(before), strip(after))
}

// An `A@{ shape: circle, label: "Old" }` node has its label in the data
// block. Writing `[New]` after the id would leave the block to win, so the
// edit has to rewrite the block or refuse, never report success while
// changing nothing.
func TestEditLabelEditOnExtendedNodeTakesEffect(t *testing.T) {
	src := "flowchart TD\n  A@{ shape: circle, label: \"Old\" }\n"
	r := SetNodeLabel(src, "A", "New")
	if !r.OK {
		t.Fatal(r.Error)
	}
	after := Parse(r.Source)
	i := after.Flowchart.IndexOfNode("A")
	if i < 0 {
		t.Fatal("no node A")
	}
	if got := after.Flowchart.Nodes[i].Label; got != "New" {
		t.Errorf("label = %q", got)
	}
	// The shape the block declares has to survive the label edit.
	// Appending `[New]` after the id would leave the block unread, turning
	// the circle into a rectangle while reporting success.
	if got := after.Flowchart.Nodes[i].Shape; got != ShapeCircle {
		t.Errorf("shape = %v", got)
	}
	if strings.Contains(r.Source, "[New]") {
		t.Errorf("brackets were appended: %s", r.Source)
	}
}

// A block with no `label:` entry has nowhere to put one without changing
// what mermaid.js would show, so the gesture refuses.
func TestEditLabelEditRefusesWhenTheBlockHasNoLabel(t *testing.T) {
	r := SetNodeLabel("flowchart TD\n  A@{ shape: circle }\n", "A", "New")
	if r.OK {
		t.Fatalf("expected a refusal, got: %s", r.Source)
	}
	if r.Error == "" {
		t.Error("the refusal gives no reason")
	}
}

func TestEditShapeEditRefusesOnExtendedNode(t *testing.T) {
	r := SetNodeShape("flowchart TD\n  A@{ shape: circle, label: \"Old\" }\n", "A", ShapeHexagon)
	if r.OK {
		t.Fatalf("expected a refusal, got: %s", r.Source)
	}
}

// A renamed node keeps its pinned position: the pos line is keyed by id.
func TestEditRenameCarriesThePinnedPosition(t *testing.T) {
	src := "flowchart TD\n  A[One]\n  B[Two]\n%% mermaid-flow:pos A=120,40 B=260,180\n"
	r := RenameNode(src, "A", "A2")
	if !r.OK {
		t.Fatal(r.Error)
	}
	found := false
	for _, pe := range Parse(r.Source).Flowchart.PosEntries {
		if pe.ID == "A2" {
			found = true
			if pe.X != 120 || pe.Y != 40 {
				t.Errorf("A2 at %v,%v", pe.X, pe.Y)
			}
		}
		if pe.ID == "A" {
			t.Error("the pos entry still has the old id")
		}
	}
	if !found {
		t.Errorf("no A2 entry in: %s", r.Source)
	}
}

// `A & B --> C` makes two edges that share one arrow in the source. Styling
// one must not restyle the other.
func TestEditStylingOneExpandedEdgeLeavesTheOtherAlone(t *testing.T) {
	src := "flowchart TD\n  A & B --> C\n"
	if n := len(Parse(src).Flowchart.Edges); n != 2 {
		t.Fatalf("edges = %d, want 2", n)
	}
	r := SetEdgeStroke(src, 0, StrokeThick)
	if !r.OK {
		return // refusing to split the group is an acceptable outcome
	}
	after := Parse(r.Source)
	if len(after.Flowchart.Edges) != 2 {
		t.Fatalf("edges after = %d, want 2", len(after.Flowchart.Edges))
	}
	if after.Flowchart.Edges[1].Stroke == StrokeThick {
		t.Errorf("both edges restyled: %s", r.Source)
	}
}

// Hyphens and dots inside an inline edge label are label text, not links.
func TestEditInlineEdgeLabelKeepsHyphensAndDots(t *testing.T) {
	r := Parse("flowchart TD\n  A -- well-known v1.2 --> B\n")
	if len(r.Flowchart.Edges) != 1 {
		t.Fatalf("edges = %d, want 1", len(r.Flowchart.Edges))
	}
	if got := r.Flowchart.Edges[0].Label; got != "well-known v1.2" {
		t.Errorf("label = %q", got)
	}
	if len(r.Flowchart.Nodes) != 2 {
		t.Errorf("nodes = %d, want 2", len(r.Flowchart.Nodes))
	}
}

// `mermaid-flow:pos` is an exact directive; `mermaid-flow:position` is an
// ordinary comment that an arrangement must not take over and overwrite.
func TestEditPosPrefixDoesNotSwallowSimilarComments(t *testing.T) {
	src := "flowchart TD\n  A[One]\n%% mermaid-flow:position is decided by hand\n"
	if Parse(src).Flowchart.HasPosLine {
		t.Error("an unrelated comment was taken for a pos line")
	}
	w := WriteArrangement(src, []NodePosition{{"A", 10, 20}})
	if !w.OK {
		t.Fatal(w.Error)
	}
	if !strings.Contains(w.Source, "mermaid-flow:position is decided by hand") {
		t.Errorf("comment destroyed: %s", w.Source)
	}
}

// ---- coordinate size ----

func TestEditNonFiniteCoordinatesRejected(t *testing.T) {
	r := Parse("flowchart TD\n  A[One]\n  B[Two]\n%% mermaid-flow:pos A=inf,0 B=nan,nan\n")
	for _, pe := range r.Flowchart.PosEntries {
		if math.IsInf(pe.X, 0) || math.IsNaN(pe.X) || math.IsInf(pe.Y, 0) || math.IsNaN(pe.Y) {
			t.Errorf("entry %s = %v,%v", pe.ID, pe.X, pe.Y)
		}
	}
}

// ---- reading the pos line ----

func TestEditPosLineParses(t *testing.T) {
	r := Parse("flowchart TD\n  A --> B\n%% mermaid-flow:pos A=120,40 B=260,180\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	pe := r.Flowchart.PosEntries
	if !r.Flowchart.HasPosLine || len(pe) != 2 {
		t.Fatalf("pos line %v entries %d", r.Flowchart.HasPosLine, len(pe))
	}
	if pe[0].ID != "A" || pe[0].X != 120 || pe[1].Y != 180 {
		t.Errorf("entries = %+v", pe)
	}
}

func TestEditPosLineToleratesPluginDimensionsAndMalformedEntries(t *testing.T) {
	// The plugin's `,width,height` reads, and is ignored; malformed entries
	// are skipped one by one.
	r := Parse("flowchart TD\n  A --> B\n%% mermaid-flow:pos A=100,200,120,40 broken B=300,400 C=x,y\n")
	pe := r.Flowchart.PosEntries
	if !r.Flowchart.HasPosLine || len(pe) != 2 {
		t.Fatalf("pos line %v entries %d", r.Flowchart.HasPosLine, len(pe))
	}
	if pe[0].X != 100 || pe[1].ID != "B" {
		t.Errorf("entries = %+v", pe)
	}
}

func TestEditSecondPosLineIsIgnoredWithDiagnostic(t *testing.T) {
	r := Parse("flowchart TD\n  A --> B\n%% mermaid-flow:pos A=1,2\n%% mermaid-flow:pos A=9,9\n")
	pe := r.Flowchart.PosEntries
	if !r.Flowchart.HasPosLine || len(pe) != 1 || pe[0].X != 1 {
		t.Errorf("pos line %v entries %+v", r.Flowchart.HasPosLine, pe)
	}
	if !hasWarning(r, "mermaid-flow:pos") {
		t.Error("no warning about the second pos line")
	}
}

// ---- writing the arrangement ----

func TestEditWriteAppendsPosLineAsLastLine(t *testing.T) {
	src := "flowchart TD\n  %% a comment worth keeping\n  A --> B\n"
	r := WriteArrangement(src, []NodePosition{{"A", 100, 50}, {"B", 300, 200}})
	if !r.OK {
		t.Fatal(r.Error)
	}
	if !strings.HasSuffix(r.Source, "%% mermaid-flow:pos A=100,50 B=300,200\n") {
		t.Errorf("source = %q", r.Source)
	}
	if !differsOnlyInPosLine(src, r.Source) {
		t.Errorf("more than the pos line changed: %q", r.Source)
	}
	// The written source parses cleanly and pins the nodes.
	back := Parse(r.Source)
	if back.HasErrors() || !back.Flowchart.HasPosLine || len(back.Flowchart.PosEntries) != 2 {
		t.Errorf("reparsed: error %q pos line %v entries %d", errorsOf(back), back.Flowchart.HasPosLine, len(back.Flowchart.PosEntries))
	}
}

func TestEditWriteReplacesExistingLineInPlace(t *testing.T) {
	src := "flowchart TD\n" +
		"  A --> B\n" +
		"%% mermaid-flow:pos A=1,1 B=2,2 Ghost=9,9\n" +
		"  B --> C\n"
	r := WriteArrangement(src, []NodePosition{{"A", 10, 20}, {"B", 30, 40}, {"C", 50, 60}})
	if !r.OK {
		t.Fatal(r.Error)
	}
	if !differsOnlyInPosLine(src, r.Source) {
		t.Errorf("more than the pos line changed: %q", r.Source)
	}
	// Entries for nodes that are gone disappear: Ghost goes and C comes.
	if strings.Contains(r.Source, "Ghost") || !strings.Contains(r.Source, "C=50,60") {
		t.Errorf("source = %q", r.Source)
	}
	// The statements after the old line are unchanged.
	if !strings.Contains(r.Source, "  B --> C\n") {
		t.Errorf("source = %q", r.Source)
	}
}

func TestEditWriteIsDeterministicAndIdempotent(t *testing.T) {
	src := "flowchart TD\n  A --> B\n"
	pos := []NodePosition{{"A", 100, 50}, {"B", 300, 200}}
	first := WriteArrangement(src, pos)
	if !first.OK {
		t.Fatal(first.Error)
	}
	second := WriteArrangement(first.Source, pos)
	if !second.OK {
		t.Fatal(second.Error)
	}
	if second.Source != first.Source {
		t.Errorf("second write %q, first %q", second.Source, first.Source)
	}
}

func TestEditWriteDropsPluginDimensionSuffix(t *testing.T) {
	src := "flowchart TD\n  A --> B\n%% mermaid-flow:pos A=100,50,120,40 B=300,200,90,36\n"
	if !Parse(src).Flowchart.HasPosLine {
		t.Fatal("no pos line")
	}
	r := WriteArrangement(src, []NodePosition{{"A", 100, 50}, {"B", 300, 200}})
	if !r.OK {
		t.Fatal(r.Error)
	}
	if strings.Contains(r.Source, ",120,40") || !strings.Contains(r.Source, "A=100,50 B=300,200") {
		t.Errorf("source = %q", r.Source)
	}
}

func TestEditWriteRefusesNonFlowcharts(t *testing.T) {
	r := WriteArrangement("sequenceDiagram\n  A->>B: hi", []NodePosition{{"A", 1, 2}})
	if r.OK || r.Error == "" {
		t.Errorf("result = %+v", r)
	}
}

func TestEditResetRestoresExactPriorSource(t *testing.T) {
	original := "flowchart TD\n  %% keep me\n  A --> B\n"
	written := WriteArrangement(original, []NodePosition{{"A", 1, 2}, {"B", 3, 4}})
	if !written.OK {
		t.Fatal(written.Error)
	}
	reset := ResetArrangement(written.Source)
	if !reset.OK || reset.Source != original {
		t.Errorf("reset = %+v", reset)
	}
	// Resetting a source without a pos line changes nothing.
	again := ResetArrangement(original)
	if !again.OK || again.Source != original {
		t.Errorf("reset again = %+v", again)
	}
}

func TestEditResetHandlesLineWithoutTrailingNewline(t *testing.T) {
	r := ResetArrangement("flowchart TD\n  A --> B\n%% mermaid-flow:pos A=1,2")
	if !r.OK || r.Source != "flowchart TD\n  A --> B" {
		t.Errorf("reset = %+v", r)
	}
}

// ---- gestures that change only what they must ----

// gestureFixture has comments, uneven spacing, pipe labels and statements
// Kvit keeps without drawing, as real notes do.
func gestureFixture() string {
	return "flowchart TD\n" +
		"  %% keep this comment about Alpha\n" +
		"  Alpha[The Alpha node]   -->  Beta{Choice?}\n" +
		"  Beta -->|yes| Gamma\n" +
		"  Beta -->|no| Delta\n" +
		"  linkStyle 0 stroke:#f00\n" +
		"  classDef hot fill:#f96\n" +
		"  class Gamma hot\n"
}

func TestEditLabelEditReplacesOnlyTheLabelSpan(t *testing.T) {
	src := gestureFixture()
	r := SetNodeLabel(src, "Alpha", "Fresh text")
	if !r.OK {
		t.Fatal(r.Error)
	}
	if want := strings.ReplaceAll(src, "The Alpha node", "Fresh text"); r.Source != want {
		t.Errorf("source = %q, want %q", r.Source, want)
	}
}

func TestEditLabelEditQuotesOnlyWhenNeeded(t *testing.T) {
	src := "flowchart LR\n  A[old] --> B\n"
	if plain := SetNodeLabel(src, "A", "no quotes needed"); !plain.OK || !strings.Contains(plain.Source, "A[no quotes needed]") {
		t.Errorf("plain = %+v", plain)
	}
	if piped := SetNodeLabel(src, "A", "a|b"); !piped.OK || !strings.Contains(piped.Source, `A["a|b"]`) {
		t.Errorf("piped = %+v", piped)
	}
	if quoted := SetNodeLabel(src, "A", `he said "hi"`); quoted.OK {
		t.Errorf("quoted = %+v", quoted)
	}
}

func TestEditLabelEditAddsBracketsToBareNodes(t *testing.T) {
	r := SetNodeLabel("flowchart LR\n  A --> B\n", "B", "The end")
	if !r.OK || !strings.Contains(r.Source, "A --> B[The end]") {
		t.Errorf("result = %+v", r)
	}
}

func TestEditShapeChangeRewritesOnlyDelimiters(t *testing.T) {
	src := gestureFixture()
	r := SetNodeShape(src, "Alpha", ShapeHexagon)
	if !r.OK {
		t.Fatal(r.Error)
	}
	if want := strings.ReplaceAll(src, "Alpha[The Alpha node]", "Alpha{{The Alpha node}}"); r.Source != want {
		t.Errorf("source = %q, want %q", r.Source, want)
	}
	// A bare node gets brackets, with its id as the label.
	if bare := SetNodeShape(src, "Delta", ShapeStadium); !bare.OK || !strings.Contains(bare.Source, "Delta([Delta])") {
		t.Errorf("bare = %+v", bare)
	}
}

func TestEditRenameReplacesRefsButNeverCommentsOrLabels(t *testing.T) {
	src := gestureFixture()
	r := RenameNode(src, "Alpha", "Omega")
	if !r.OK {
		t.Fatal(r.Error)
	}
	// The comment and the label still say Alpha.
	if !strings.Contains(r.Source, "%% keep this comment about Alpha") ||
		!strings.Contains(r.Source, "Omega[The Alpha node]") || strings.Contains(r.Source, "Alpha[") {
		t.Errorf("source = %q", r.Source)
	}
	// Styling statements follow the rename too.
	if g := RenameNode(src, "Gamma", "G2"); !g.OK || !strings.Contains(g.Source, "class G2 hot") {
		t.Errorf("rename Gamma = %+v", g)
	}
}

func TestEditRenameValidatesTheNewId(t *testing.T) {
	src := gestureFixture()
	for _, c := range []struct{ old, new, why string }{
		{"Alpha", "Beta", "a collision"},
		{"Alpha", "end", "a reserved word"},
		{"Alpha", "has space", "a character not allowed"},
		{"Nope", "Fine", "an unknown node"},
	} {
		if r := RenameNode(src, c.old, c.new); r.OK {
			t.Errorf("renaming %s to %q, %s, was not refused", c.old, c.new, c.why)
		}
	}
}

func TestEditDeleteNodeRemovesWholeStatements(t *testing.T) {
	src := gestureFixture()
	r := DeleteNode(src, "Delta")
	if !r.OK {
		t.Fatal(r.Error)
	}
	if strings.Contains(r.Source, "Delta") {
		t.Errorf("source = %q", r.Source)
	}
	// Everything else is unchanged.
	if want := strings.ReplaceAll(src, "  Beta -->|no| Delta\n", ""); r.Source != want {
		t.Errorf("source = %q, want %q", r.Source, want)
	}
}

func TestEditDeleteNodeRefusedWhenStyledWithOthers(t *testing.T) {
	r := DeleteNode("flowchart LR\n  A --> B\n  class A,B hot\n  classDef hot fill:#f00\n", "A")
	if r.OK || !strings.Contains(r.Error, "styled") {
		t.Errorf("result = %+v", r)
	}
}

func TestEditDeleteEdgeRemovesItsStatement(t *testing.T) {
	src := gestureFixture()
	// Edge 1 is Beta -->|yes| Gamma.
	r := DeleteEdge(src, 1)
	if !r.OK {
		t.Fatal(r.Error)
	}
	if want := strings.ReplaceAll(src, "  Beta -->|yes| Gamma\n", ""); r.Source != want {
		t.Errorf("source = %q, want %q", r.Source, want)
	}
}

func TestEditDeleteEdgeSplitsChains(t *testing.T) {
	src := "flowchart LR\n  A[Start] --> B --> C\n  C --> D\n"
	r := DeleteEdge(src, 0) // A --> B
	if !r.OK {
		t.Fatal(r.Error)
	}
	if !strings.Contains(r.Source, "  B --> C\n") || strings.Contains(r.Source, "A[Start]") ||
		!strings.Contains(r.Source, "  C --> D\n") {
		t.Errorf("source = %q", r.Source)
	}
	// Pipe labels in a chain survive the split.
	rp := DeleteEdge("flowchart LR\n  A -->|x| B -->|y| C\n", 0)
	if !rp.OK {
		t.Fatal(rp.Error)
	}
	if !strings.Contains(rp.Source, "B -->|y| C") {
		t.Errorf("source = %q", rp.Source)
	}
}

func TestEditEdgeStyleRewritesTheArrowToken(t *testing.T) {
	src := gestureFixture()
	// Edge 0 is Alpha --> Beta; the uneven spacing around the arrow stays.
	dotted := SetEdgeStroke(src, 0, StrokeDotted)
	if !dotted.OK {
		t.Fatal(dotted.Error)
	}
	if !strings.Contains(dotted.Source, "Alpha[The Alpha node]   -.->  Beta") {
		t.Errorf("dotted = %q", dotted.Source)
	}
	if thick := SetEdgeStroke(src, 1, StrokeThick); !thick.OK || !strings.Contains(thick.Source, "Beta ==>|yes| Gamma") {
		t.Errorf("thick = %+v", thick)
	}
	// An inline label is kept in the rewritten arrow.
	r := SetEdgeStroke("flowchart LR\n  A -- ride --> B\n", 0, StrokeThick)
	if !r.OK {
		t.Fatal(r.Error)
	}
	if !strings.Contains(r.Source, "A == ride ==> B") {
		t.Errorf("source = %q", r.Source)
	}
}

func TestEditInsertEdgeLandsAfterLastMentionBeforePosLine(t *testing.T) {
	src := "flowchart TD\n" +
		"  A --> B\n" +
		"  B --> C\n" +
		"%% mermaid-flow:pos A=1,2 B=3,4 C=5,6\n"
	r := InsertEdge(src, "A", "C")
	if !r.OK {
		t.Fatal(r.Error)
	}
	inserted := strings.Index(r.Source, "A --> C")
	posLine := strings.Index(r.Source, "%% mermaid-flow:pos")
	if inserted <= 0 || inserted >= posLine {
		t.Errorf("source = %q", r.Source)
	}
	// Right after the last statement that mentions A.
	if inserted <= strings.Index(r.Source, "A --> B") || inserted >= strings.Index(r.Source, "B --> C") {
		t.Errorf("source = %q", r.Source)
	}
}

func TestEditQuickAddGeneratesCollisionFreeIds(t *testing.T) {
	r := QuickAddNode("flowchart LR\n  node1 --> B\n", "B")
	if !r.OK {
		t.Fatal(r.Error)
	}
	if r.NewID != "node2" || !strings.Contains(r.Source, "B --> node2[New node]") {
		t.Errorf("result = %+v", r)
	}
}

func TestEditStyleInsertsAndReusesKvitClassDefs(t *testing.T) {
	src := gestureFixture()
	first := SetNodeStyle(src, "Alpha", ParseColor("#ff0000"), Color{})
	if !first.OK {
		t.Fatal(first.Error)
	}
	if !strings.Contains(first.Source, "classDef kvit_style_1 fill:#ff0000") ||
		!strings.Contains(first.Source, "class Alpha kvit_style_1") {
		t.Errorf("first = %q", first.Source)
	}
	// The user's own classDef is never rewritten.
	if !strings.Contains(first.Source, "classDef hot fill:#f96") {
		t.Errorf("first = %q", first.Source)
	}
	// The same colour on another node uses the same classDef.
	second := SetNodeStyle(first.Source, "Beta", ParseColor("#ff0000"), Color{})
	if !second.OK {
		t.Fatal(second.Error)
	}
	if strings.Count(second.Source, "classDef kvit_style_1") != 1 || !strings.Contains(second.Source, "class Beta kvit_style_1") {
		t.Errorf("second = %q", second.Source)
	}
	// Restyling rewrites the node's class statement where it is.
	third := SetNodeStyle(second.Source, "Alpha", ParseColor("#00ff00"), Color{})
	if !third.OK {
		t.Fatal(third.Error)
	}
	if !strings.Contains(third.Source, "classDef kvit_style_2 fill:#00ff00") ||
		!strings.Contains(third.Source, "class Alpha kvit_style_2") ||
		strings.Contains(third.Source, "class Alpha kvit_style_1") {
		t.Errorf("third = %q", third.Source)
	}
}

func TestEditReparentMovesDeclarationsAcrossSubgraphs(t *testing.T) {
	src := "flowchart TD\n" +
		"  Solo[On its own]\n" +
		"  subgraph grp [Group]\n" +
		"    A --> B\n" +
		"  end\n"
	in := ReparentNode(src, "Solo", "grp")
	if !in.OK {
		t.Fatal(in.Error)
	}
	subgraphAt := strings.Index(in.Source, "subgraph grp")
	endAt := strings.Index(in.Source, "\n  end")
	soloAt := strings.Index(in.Source, "Solo[On its own]")
	if soloAt <= subgraphAt || soloAt >= endAt {
		t.Errorf("source = %q", in.Source)
	}
	// Membership through edges is refused.
	if out := ReparentNode(src, "A", ""); out.OK {
		t.Errorf("moving A out = %+v", out)
	}
}

func TestEditReorderSwapsStandaloneDeclarations(t *testing.T) {
	src := "flowchart LR\n  First\n  Second\n  First --> Second\n"
	r := ReorderNode(src, "First", 1)
	if !r.OK {
		t.Fatal(r.Error)
	}
	if strings.Index(r.Source, "Second") >= strings.Index(r.Source, "First") {
		t.Errorf("source = %q", r.Source)
	}
	// With the nodes pinned, reordering is refused (automatic layout only).
	if arranged := ReorderNode(src+"%% mermaid-flow:pos First=1,2\n", "First", 1); arranged.OK {
		t.Errorf("arranged = %+v", arranged)
	}
}

func TestEditGestureSequencesStayValid(t *testing.T) {
	// A fixed pseudo-random run of gestures; after each step the source
	// still parses without errors.
	src := gestureFixture()
	seed := uint32(0x5eed)
	next := func() uint32 {
		seed = seed*1664525 + 1013904223
		return seed >> 16
	}
	for step := 0; step < 60; step++ {
		pr := Parse(src)
		if pr.HasErrors() {
			t.Fatalf("step %d: %s", step, pr.FirstError().Message)
		}
		nodes := pr.Flowchart.Nodes
		if len(nodes) == 0 {
			break
		}
		id := nodes[next()%uint32(len(nodes))].ID
		var r EditResult
		switch next() % 6 {
		case 0:
			r = SetNodeLabel(src, id, fmt.Sprintf("L%d", step))
		case 1:
			shape := ShapeRoundRect
			if next()%2 != 0 {
				shape = ShapeHexagon
			}
			r = SetNodeShape(src, id, shape)
		case 2:
			r = RenameNode(src, id, fmt.Sprintf("n_%d", step))
		case 3:
			r = QuickAddNode(src, id)
		case 4:
			if len(pr.Flowchart.Edges) > 0 {
				r = DeleteEdge(src, int(next()%uint32(len(pr.Flowchart.Edges))))
			}
		case 5:
			r = SetNodeStyle(src, id, ParseColor("#abcdef"), Color{})
		}
		if r.OK {
			src = r.Source
		}
	}
	if pr := Parse(src); pr.HasErrors() {
		t.Errorf("error %q", errorsOf(pr))
	}
}

// ---- reordering a sequence diagram ----

func sequenceFixture() string {
	return "sequenceDiagram\n" +
		"  participant A as Alice\n" +
		"  participant B\n" +
		"  participant C\n" +
		"  %% a comment that must not move\n" +
		"  A->>B: first\n" +
		"  loop retries\n" +
		"    B->>C: second\n" +
		"  end\n" +
		"  C-->>A: third\n"
}

// nthMessageEvent is the event index of the n-th message, or -1.
func nthMessageEvent(pr *ParseResult, n int) int {
	seen := 0
	for i, e := range pr.Sequence.Events {
		if e.Kind == EventMessage {
			if seen == n {
				return i
			}
			seen++
		}
	}
	return -1
}

func TestEditMoveMessageSwapsLinesOneForOne(t *testing.T) {
	src := sequenceFixture()
	pr := Parse(src)
	// Move "first" down past "second": across the loop's start the two
	// message lines swap one for one, and the loop stays where it is.
	r := MoveSequenceMessage(src, nthMessageEvent(&pr, 0), 1)
	if !r.OK {
		t.Fatal(r.Error)
	}
	if strings.Index(r.Source, "B->>C: second") >= strings.Index(r.Source, "A->>B: first") {
		t.Errorf("source = %q", r.Source)
	}
	// Only the two message lines moved: swapping back gives the original.
	prAfter := Parse(r.Source)
	back := MoveSequenceMessage(r.Source, nthMessageEvent(&prAfter, 0), 1)
	if !back.OK || back.Source != src {
		t.Errorf("back = %+v", back)
	}
	// The comment did not move.
	if !strings.Contains(r.Source, "  %% a comment that must not move\n") {
		t.Errorf("source = %q", r.Source)
	}
}

func TestEditMoveMessageRefusesAtTheEdges(t *testing.T) {
	src := sequenceFixture()
	pr := Parse(src)
	if r := MoveSequenceMessage(src, nthMessageEvent(&pr, 0), -1); r.OK {
		t.Error("the first message moved up")
	}
	if r := MoveSequenceMessage(src, nthMessageEvent(&pr, 2), 1); r.OK {
		t.Error("the last message moved down")
	}
	// An index that is not a message is refused.
	if r := MoveSequenceMessage(src, -1, 1); r.OK {
		t.Error("event -1 moved")
	}
}

func TestEditMoveParticipantSwapsDeclarations(t *testing.T) {
	src := sequenceFixture()
	r := MoveSequenceParticipant(src, "A", 1)
	if !r.OK {
		t.Fatal(r.Error)
	}
	if strings.Index(r.Source, "participant B") >= strings.Index(r.Source, "participant A as Alice") {
		t.Errorf("source = %q", r.Source)
	}
	// The messages are unchanged.
	if !strings.Contains(r.Source, "  A->>B: first\n") {
		t.Errorf("source = %q", r.Source)
	}
	// Moving back gives the original.
	if back := MoveSequenceParticipant(r.Source, "A", -1); !back.OK || back.Source != src {
		t.Errorf("back = %+v", back)
	}
}

func TestEditMoveParticipantRefusesAutoDeclared(t *testing.T) {
	r := MoveSequenceParticipant("sequenceDiagram\n  participant A\n  A->>Zed: hi\n", "Zed", -1)
	if r.OK || !strings.Contains(r.Error, "declaration") {
		t.Errorf("result = %+v", r)
	}
}

func TestEditKvitWrittenLinesMatchPluginPattern(t *testing.T) {
	r := WriteArrangement("flowchart TD\n  A --> B\n", []NodePosition{{"A", -10, 50.6}, {"B", 300.2, 200}})
	if !r.OK {
		t.Fatal(r.Error)
	}
	line := ""
	for _, l := range strings.Split(r.Source, "\n") {
		if strings.HasPrefix(l, "%% mermaid-flow:pos") {
			line = l
		}
	}
	// The plugin's published entry form: id=int,int, separated by spaces.
	pattern := regexp.MustCompile(`^%% mermaid-flow:pos( \S+=-?\d+,-?\d+)+$`)
	if !pattern.MatchString(line) {
		t.Errorf("line = %q", line)
	}
	if !strings.Contains(line, "A=-10,51") {
		t.Errorf("line = %q", line)
	}
}
