package mermaid

// These tests check the flowchart lexer's tokens and positions and the
// flowchart parser: its trees, diagnostics, limits, front matter and source
// spans. TestUnterminatedShapeRecovers only checks that parsing returns.

import (
	"fmt"
	"slices"
	"strings"
	"testing"
)

func flowNode(a *FlowchartAst, id string) *Node {
	if i := a.IndexOfNode(id); i >= 0 {
		return &a.Nodes[i]
	}
	return nil
}

// hasWarning reports whether r has a warning whose message contains needle.
func hasWarning(r ParseResult, needle string) bool {
	for _, d := range r.Diagnostics {
		if d.Severity == SeverityWarning && strings.Contains(d.Message, needle) {
			return true
		}
	}
	return false
}

// errorsOf is r's first error message, for failure output.
func errorsOf(r ParseResult) string {
	if r.HasErrors() {
		return r.FirstError().Message
	}
	return ""
}

// spanText is the text of span in src, counted in runes.
func spanText(src string, span Span) string {
	rs := []rune(src)
	return string(rs[span.Start:span.End()])
}

// runeIndex is the rune offset of the first sub in s, or -1.
func runeIndex(s, sub string) int {
	i := strings.Index(s, sub)
	if i < 0 {
		return -1
	}
	return runeLen(s[:i])
}

// ---- lexer ----

func TestLexerReportsOffsets(t *testing.T) {
	toks := Lex("flowchart LR\n  A --> B")
	// flowchart(1:1) LR(1:11) Sep A(2:3) Edge(2:5) B(2:9)
	if len(toks) < 6 {
		t.Fatalf("tokens = %d, want at least 6", len(toks))
	}
	if toks[0].Kind != TokenWord || toks[0].Text != "flowchart" || toks[0].Line != 1 || toks[0].Column != 1 {
		t.Errorf("token 0 = %+v", toks[0])
	}
	if toks[1].Text != "LR" || toks[1].Column != 11 {
		t.Errorf("token 1 = %+v", toks[1])
	}
	// The node on line 2 starts at column 3, after a two-space indent.
	var a *Token
	for i := range toks {
		if toks[i].Kind == TokenWord && toks[i].Text == "A" {
			a = &toks[i]
			break
		}
	}
	if a == nil {
		t.Fatal("no token A")
	}
	if a.Line != 2 || a.Column != 3 {
		t.Errorf("A at %d:%d, want 2:3", a.Line, a.Column)
	}
}

func TestLexerParsesEdgeVariants(t *testing.T) {
	edge := func(s string) Token {
		for _, tk := range Lex(s) {
			if tk.Kind == TokenEdge {
				return tk
			}
		}
		return newToken(TokenWord)
	}
	if edge("A --> B").Stroke != StrokeSolid {
		t.Error("--> is not solid")
	}
	if !edge("A --> B").ArrowEnd {
		t.Error("--> has no arrowhead")
	}
	if edge("A -.-> B").Stroke != StrokeDotted {
		t.Error("-.-> is not dotted")
	}
	if edge("A ==> B").Stroke != StrokeThick {
		t.Error("==> is not thick")
	}
	if edge("A --- B").ArrowEnd { // an open link
		t.Error("--- has an arrowhead")
	}
	if !edge("A <--> B").ArrowStart {
		t.Error("<--> has no arrowhead at the start")
	}
	if got := edge("A ---> B").MinLen; got != 2 { // an extra dash spans another rank
		t.Errorf("---> MinLen = %d, want 2", got)
	}
	if got := edge("A -- yes --> B").EdgeLabel; got != "yes" {
		t.Errorf("inline label = %q, want yes", got)
	}
	if got := edge("A == big text ==> B").EdgeLabel; got != "big text" {
		t.Errorf("thick inline label = %q, want big text", got)
	}
}

// ---- header and family ----

func TestDetectsFlowchart(t *testing.T) {
	for _, src := range []string{"flowchart TD\nA-->B", "graph LR\nA-->B"} {
		r := Parse(src)
		if r.Type != Flowchart || !r.Supported || r.HasErrors() {
			t.Errorf("%q: type %v supported %v error %q", src, r.Type, r.Supported, errorsOf(r))
		}
	}
}

func TestUnsupportedFamilyDiagnosesWithoutDiscarding(t *testing.T) {
	r := Parse("gantt\n  title A deferred family")
	if r.Type != Unsupported {
		t.Errorf("type = %v, want Unsupported", r.Type)
	}
	if r.Supported {
		t.Error("gantt is supported")
	}
	if !r.HasErrors() || !strings.Contains(r.FirstError().Message, "Unsupported") {
		t.Errorf("first error = %q", r.FirstError().Message)
	}
}

// ---- directions ----

func TestParsesDirections(t *testing.T) {
	for src, want := range map[string]Direction{
		"flowchart TB\nA-->B": TB,
		"graph TD\nA-->B":     TB,
		"flowchart LR\nA-->B": LR,
		"flowchart RL\nA-->B": RL,
		"flowchart BT\nA-->B": BT,
	} {
		if got := Parse(src).Flowchart.Direction; got != want {
			t.Errorf("%q: direction %v, want %v", src, got, want)
		}
	}
}

// ---- nodes, shapes, chains ----

func TestParsesNodesShapesAndChain(t *testing.T) {
	r := Parse("flowchart LR\n" +
		"  A[Start] --> B{Choice}\n" +
		"  B -->|yes| C([Done])\n" +
		"  B -->|no| D((Stop))\n")
	a := &r.Flowchart
	if len(a.Nodes) != 4 {
		t.Fatalf("nodes = %d, want 4", len(a.Nodes))
	}
	if flowNode(a, "A").Label != "Start" {
		t.Errorf("A label = %q", flowNode(a, "A").Label)
	}
	for id, want := range map[string]NodeShape{"A": ShapeRect, "B": ShapeRhombus, "C": ShapeStadium, "D": ShapeCircle} {
		if got := flowNode(a, id).Shape; got != want {
			t.Errorf("%s shape = %v, want %v", id, got, want)
		}
	}
	if len(a.Edges) != 3 {
		t.Fatalf("edges = %d, want 3", len(a.Edges))
	}
	// The pipe labels belong to their edges.
	sawYes, sawNo := false, false
	for _, e := range a.Edges {
		if e.From == "B" && e.To == "C" {
			if e.Label != "yes" {
				t.Errorf("B-C label = %q", e.Label)
			}
			sawYes = true
		}
		if e.From == "B" && e.To == "D" {
			if e.Label != "no" {
				t.Errorf("B-D label = %q", e.Label)
			}
			sawNo = true
		}
	}
	if !sawYes || !sawNo {
		t.Error("an edge from B is missing")
	}
}

func TestChainedEdges(t *testing.T) {
	r := Parse("flowchart LR\nA --> B --> C --> D")
	if len(r.Flowchart.Nodes) != 4 || len(r.Flowchart.Edges) != 3 {
		t.Errorf("nodes %d edges %d, want 4 and 3", len(r.Flowchart.Nodes), len(r.Flowchart.Edges))
	}
}

func TestAmpNodeLists(t *testing.T) {
	r := Parse("flowchart LR\nA & B --> C & D")
	// Every pair: A->C, A->D, B->C, B->D.
	if len(r.Flowchart.Edges) != 4 {
		t.Errorf("edges = %d, want 4", len(r.Flowchart.Edges))
	}
}

// ---- subgraphs ----

func TestSubgraphsGroupMembers(t *testing.T) {
	r := Parse("flowchart TB\n" +
		"  subgraph one [Group One]\n" +
		"    A --> B\n" +
		"  end\n" +
		"  B --> C\n")
	if len(r.Flowchart.Subgraphs) != 1 {
		t.Fatalf("subgraphs = %d, want 1", len(r.Flowchart.Subgraphs))
	}
	sg := r.Flowchart.Subgraphs[0]
	if sg.Title != "Group One" {
		t.Errorf("title = %q", sg.Title)
	}
	if !slices.Contains(sg.NodeIDs, "A") || !slices.Contains(sg.NodeIDs, "B") {
		t.Errorf("members = %q", sg.NodeIDs)
	}
	if slices.Contains(sg.NodeIDs, "C") { // declared after `end`
		t.Errorf("members = %q include C", sg.NodeIDs)
	}
}

// ---- classes and styles ----

func TestClassDefAndClassApply(t *testing.T) {
	r := Parse("flowchart LR\n" +
		"  classDef hot fill:#f96,stroke:#333,stroke-width:2px\n" +
		"  A --> B\n" +
		"  class A,B hot\n")
	d, ok := r.Flowchart.ClassDefs["hot"]
	if !ok {
		t.Fatal("no classDef hot")
	}
	if !d.HasFill || d.Fill != ParseColor("#f96") {
		t.Errorf("fill = %+v", d.Fill)
	}
	if !d.HasStroke {
		t.Error("no stroke")
	}
	if d.StrokeWidth != 2.0 {
		t.Errorf("stroke width = %v", d.StrokeWidth)
	}
	for _, id := range []string{"A", "B"} {
		if !slices.Contains(flowNode(&r.Flowchart, id).Classes, "hot") {
			t.Errorf("%s classes = %q", id, flowNode(&r.Flowchart, id).Classes)
		}
	}
}

func TestTripleColonClass(t *testing.T) {
	r := Parse("flowchart LR\nA:::hot --> B")
	if !slices.Contains(flowNode(&r.Flowchart, "A").Classes, "hot") {
		t.Errorf("A classes = %q", flowNode(&r.Flowchart, "A").Classes)
	}
}

// ---- comments, accessibility, restricted statements ----

func TestCommentsIgnored(t *testing.T) {
	r := Parse("flowchart LR\n" +
		"%% this is a comment\n" +
		"A --> B %% trailing\n")
	if len(r.Flowchart.Nodes) != 2 || len(r.Flowchart.Edges) != 1 {
		t.Errorf("nodes %d edges %d, want 2 and 1", len(r.Flowchart.Nodes), len(r.Flowchart.Edges))
	}
}

func TestAccessibilityDirectives(t *testing.T) {
	r := Parse("flowchart LR\n" +
		"  accTitle: My flow\n" +
		"  accDescr: Two steps\n" +
		"  A --> B\n")
	if r.Flowchart.AccTitle != "My flow" || r.Flowchart.AccDescr != "Two steps" {
		t.Errorf("accTitle %q accDescr %q", r.Flowchart.AccTitle, r.Flowchart.AccDescr)
	}
}

func TestClickIsWarnedAndIgnored(t *testing.T) {
	r := Parse("flowchart LR\nA --> B\nclick A \"https://x\"\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if !hasWarning(r, "click") {
		t.Error("no warning about click")
	}
}

func TestFrontmatterStripped(t *testing.T) {
	r := Parse("---\ntitle: My Chart\n---\nflowchart LR\nA --> B\n")
	if r.Type != Flowchart {
		t.Errorf("type = %v", r.Type)
	}
	if r.Flowchart.AccTitle != "My Chart" {
		t.Errorf("accTitle = %q", r.Flowchart.AccTitle)
	}
	if len(r.Flowchart.Nodes) != 2 {
		t.Errorf("nodes = %d, want 2", len(r.Flowchart.Nodes))
	}
}

// The size limit covers the whole source, front matter included. Checking
// only what is left once the front matter is split off would let a front
// matter of megabytes before a two-line diagram through: it would be copied
// and scanned in full, then left out of the only size measured.
func TestFrontmatterCountsTowardTheSourceBudget(t *testing.T) {
	padding := strings.Repeat("x", MaxSourceChars+1024)
	r := Parse("---\ncomment: " + padding + "\n---\nflowchart LR\nA --> B\n")
	if !r.HasErrors() {
		t.Fatal("no error")
	}
	rejectedForSize := false
	for _, d := range r.Diagnostics {
		if strings.Contains(d.Message, "exceeds") {
			rejectedForSize = true
		}
	}
	if !rejectedForSize {
		t.Error("an oversized source must be refused by size")
	}
	if len(r.Flowchart.Nodes) != 0 {
		t.Errorf("nodes = %d, want 0", len(r.Flowchart.Nodes))
	}
}

// A title inside the limit is still capped: it becomes the diagram's
// accessible title, which is a label, not a document.
func TestFrontmatterTitleIsCapped(t *testing.T) {
	longTitle := strings.Repeat("t", MaxFrontMatterTitleChars*2)
	r := Parse("---\ntitle: " + longTitle + "\n---\nflowchart LR\nA --> B\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if got := runeLen(r.Flowchart.AccTitle); got != MaxFrontMatterTitleChars {
		t.Errorf("accTitle length = %d, want %d", got, MaxFrontMatterTitleChars)
	}
}

// Past the front-matter limit the closing fence is not looked for, so the
// block counts as not closed and goes to the lexer whole, rather than being
// split off and scanned as not part of the body.
func TestOversizedFrontmatterIsNotStripped(t *testing.T) {
	padding := strings.Repeat("y", MaxFrontMatterChars+16)
	src := "---\ncomment: " + padding + "\n---\nflowchart LR\nA --> B\n"
	if runeLen(src) > MaxSourceChars {
		t.Fatal("the source is over the source limit")
	}
	r := Parse(src)
	// Whatever the lexer makes of it, the header is no longer a flowchart
	// header, so no diagram is made and nothing was left out of the limit.
	if len(r.Flowchart.Nodes) != 0 {
		t.Errorf("nodes = %d, want 0", len(r.Flowchart.Nodes))
	}
}

// ---- limits and robustness ----

func TestDeterministicNodeOrder(t *testing.T) {
	r1 := Parse("flowchart LR\nA-->B-->C\nZ-->A")
	r2 := Parse("flowchart LR\nA-->B-->C\nZ-->A")
	if len(r1.Flowchart.Nodes) != len(r2.Flowchart.Nodes) {
		t.Fatalf("node counts %d and %d", len(r1.Flowchart.Nodes), len(r2.Flowchart.Nodes))
	}
	for i := range r1.Flowchart.Nodes {
		if r1.Flowchart.Nodes[i].ID != r2.Flowchart.Nodes[i].ID {
			t.Errorf("node %d: %q and %q", i, r1.Flowchart.Nodes[i].ID, r2.Flowchart.Nodes[i].ID)
		}
	}
	// The order first met: A, B, C, Z.
	if r1.Flowchart.Nodes[0].ID != "A" || r1.Flowchart.Nodes[3].ID != "Z" {
		t.Errorf("first %q last %q", r1.Flowchart.Nodes[0].ID, r1.Flowchart.Nodes[3].ID)
	}
}

func TestNodeLimitEnforced(t *testing.T) {
	var b strings.Builder
	b.WriteString("flowchart LR\n")
	for i := 0; i < MaxNodes+50; i++ {
		fmt.Fprintf(&b, "n%d --> n%d\n", i, i+1)
	}
	r := Parse(b.String())
	if len(r.Flowchart.Nodes) > MaxNodes {
		t.Errorf("nodes = %d, over %d", len(r.Flowchart.Nodes), MaxNodes)
	}
	capped := false
	for _, d := range r.Diagnostics {
		if strings.Contains(d.Message, "Too many nodes") {
			capped = true
		}
	}
	if !capped {
		t.Error("no diagnostic about too many nodes")
	}
}

func TestUnterminatedShapeRecovers(t *testing.T) {
	// A missing closing bracket must not hang or crash; the source is kept.
	_ = Parse("flowchart LR\nA[unterminated --> B")
}

// ---- the audit against flow.jison of mermaid@11.16.0 ----

func TestAuditVertexShapes(t *testing.T) {
	r := Parse("flowchart LR\n" +
		"  A(((double)))\n" +
		"  B(-ellipse-)\n" +
		"  C>odd flag]\n" +
		"  D[/trap\\]\n" +
		"  E[\\invtrap/]\n" +
		"  F[\\leanleft\\]\n" +
		"  G[/leanright/]\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	a := &r.Flowchart
	for id, want := range map[string]NodeShape{
		"A": ShapeDoubleCircle, "B": ShapeEllipse, "C": ShapeOdd, "D": ShapeTrapezoid,
		"E": ShapeTrapezoidAlt, "F": ShapeParallelogramAlt, "G": ShapeParallelogram,
	} {
		if n := flowNode(a, id); n == nil || n.Shape != want {
			t.Errorf("%s = %+v, want shape %v", id, n, want)
		}
	}
	if got := flowNode(a, "A").Label; got != "double" {
		t.Errorf("A label = %q", got)
	}
	if got := flowNode(a, "C").Label; got != "odd flag" {
		t.Errorf("C label = %q", got)
	}
}

func TestAuditShapeDataBlocks(t *testing.T) {
	r := Parse("flowchart TD\n" +
		"  A@{ shape: hexagon, label: \"Prep step\" }\n" +
		"  B@{ shape: cyl }\n" +
		"  A --> B\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	a := &r.Flowchart
	if n := flowNode(a, "A"); n.Shape != ShapeHexagon || n.Label != "Prep step" {
		t.Errorf("A = %+v", n)
	}
	if n := flowNode(a, "B"); n.Shape != ShapeCylinder {
		t.Errorf("B = %+v", n)
	}
	if len(a.Edges) != 1 {
		t.Errorf("edges = %d, want 1", len(a.Edges))
	}
}

func TestAuditShapeDataMultilineAndUnknown(t *testing.T) {
	r := Parse("flowchart TD\n" +
		"  A@{\n" +
		"    shape: cloud\n" +
		"    icon: \"fa:fa-user\"\n" +
		"  }\n" +
		"  A --> B\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	// An unknown shape is drawn as a rectangle with a warning; unknown keys
	// warn.
	if got := flowNode(&r.Flowchart, "A").Shape; got != ShapeRect {
		t.Errorf("A shape = %v", got)
	}
	if !hasWarning(r, "Unknown shape") || !hasWarning(r, "icon") {
		t.Errorf("diagnostics = %+v", r.Diagnostics)
	}
	if len(r.Flowchart.Edges) != 1 {
		t.Errorf("edges = %d, want 1", len(r.Flowchart.Edges))
	}
}

func TestAuditEdgeIdsAndInvisibleLinks(t *testing.T) {
	r := Parse("flowchart LR\n" +
		"  A e1@--> B\n" +
		"  B ~~~ C\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	e := r.Flowchart.Edges
	if len(e) != 2 {
		t.Fatalf("edges = %d, want 2", len(e))
	}
	if e[0].ID != "e1" || e[0].From != "A" || e[0].To != "B" {
		t.Errorf("edge 0 = %+v", e[0])
	}
	if !e[1].Invisible || e[1].ArrowEnd {
		t.Errorf("edge 1 = %+v", e[1])
	}
}

func TestAuditHeaderVariants(t *testing.T) {
	elk := Parse("flowchart-elk TD\nA-->B")
	if elk.Type != Flowchart || !elk.Supported {
		t.Errorf("flowchart-elk: type %v supported %v", elk.Type, elk.Supported)
	}
	if !hasWarning(elk, "layout engine") {
		t.Error("no warning about the layout engine")
	}
	for src, want := range map[string]Direction{
		"graph >\nA-->B": LR,
		"graph <\nA-->B": RL,
		"graph ^\nA-->B": BT,
		"graph v\nA-->B": TB,
	} {
		if got := Parse(src).Flowchart.Direction; got != want {
			t.Errorf("%q: direction %v, want %v", src, got, want)
		}
	}
}

func TestAuditClassDefCommaNames(t *testing.T) {
	r := Parse("flowchart LR\n" +
		"  classDef hot,cold fill:#f96\n" +
		"  A:::hot --> B:::cold\n")
	if _, ok := r.Flowchart.ClassDefs["hot"]; !ok {
		t.Error("no classDef hot")
	}
	if cold, ok := r.Flowchart.ClassDefs["cold"]; !ok || !cold.HasFill {
		t.Errorf("classDef cold = %+v", cold)
	}
}

func TestAuditSubgraphMultiWordTitle(t *testing.T) {
	r := Parse("flowchart TB\n" +
		"  subgraph Main Processing Stage\n" +
		"    A --> B\n" +
		"  end\n")
	if len(r.Flowchart.Subgraphs) != 1 {
		t.Fatalf("subgraphs = %d, want 1", len(r.Flowchart.Subgraphs))
	}
	if got := r.Flowchart.Subgraphs[0].Title; got != "Main Processing Stage" {
		t.Errorf("title = %q", got)
	}
}

func TestAuditMarkdownStringLabel(t *testing.T) {
	r := Parse("flowchart LR\n  A[\"`emphasised label`\"] --> B")
	if got := flowNode(&r.Flowchart, "A").Label; got != "emphasised label" {
		t.Errorf("label = %q", got)
	}
}

func TestAuditAccDescrMultiline(t *testing.T) {
	r := Parse("flowchart LR\n" +
		"  accDescr {\n" +
		"    A description across\n" +
		"    two lines\n" +
		"  }\n" +
		"  A --> B\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	d := r.Flowchart.AccDescr
	if !strings.Contains(d, "A description across") || !strings.Contains(d, "two lines") {
		t.Errorf("accDescr = %q", d)
	}
	if len(r.Flowchart.Nodes) != 2 {
		t.Errorf("nodes = %d, want 2", len(r.Flowchart.Nodes))
	}
}

func TestAuditVertexWithProps(t *testing.T) {
	r := Parse("flowchart LR\n  A[|borders:none|the text] --> B")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if got := flowNode(&r.Flowchart, "A").Label; got != "the text" {
		t.Errorf("label = %q", got)
	}
	if !hasWarning(r, "properties") {
		t.Error("no warning about properties")
	}
}

func TestAuditLinkStyleWarns(t *testing.T) {
	r := Parse("flowchart LR\nA --> B\nlinkStyle 0 stroke:#f00\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if !hasWarning(r, "linkStyle") {
		t.Error("no warning about linkStyle")
	}
}

// ---- source spans ----

func TestSourceSpansMapOntoSource(t *testing.T) {
	src := "flowchart LR\n  Alpha[Start here] --> Beta\n  Alpha --> Gamma\n"
	r := Parse(src)
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	a := flowNode(&r.Flowchart, "Alpha")
	if a == nil {
		t.Fatal("no node Alpha")
	}
	// The declaration's span is the first `Alpha`.
	if !a.IDSpan.Valid() || spanText(src, a.IDSpan) != "Alpha" || a.IDSpan.Start != runeIndex(src, "Alpha") {
		t.Errorf("id span = %+v", a.IDSpan)
	}
	// The label's span is between the brackets.
	if !a.LabelSpan.Valid() || spanText(src, a.LabelSpan) != "Start here" {
		t.Errorf("label span = %+v", a.LabelSpan)
	}
	// The shape's span is `[Start here]`.
	if got := spanText(src, a.ShapeSpan); got != "[Start here]" {
		t.Errorf("shape span text = %q", got)
	}
	// Every mention is recorded: the declaration and the second statement.
	if len(a.RefSpans) != 2 {
		t.Fatalf("ref spans = %d, want 2", len(a.RefSpans))
	}
	if got := spanText(src, a.RefSpans[1]); got != "Alpha" {
		t.Errorf("second ref = %q", got)
	}
	// The edge's spans: the arrow and the statement.
	e := r.Flowchart.Edges[0]
	if got := spanText(src, e.OpSpan); got != "-->" {
		t.Errorf("op span text = %q", got)
	}
	if !e.StmtSpan.Valid() || spanText(src, e.StmtSpan) != "Alpha[Start here] --> Beta" {
		t.Errorf("statement span = %+v", e.StmtSpan)
	}
}

func TestSourceSpansShiftPastFrontmatter(t *testing.T) {
	src := "---\ntitle: T\n---\nflowchart LR\n  A --> B\n"
	r := Parse(src)
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	a := flowNode(&r.Flowchart, "A")
	if a == nil || !a.IDSpan.Valid() {
		t.Fatalf("A = %+v", a)
	}
	if spanText(src, a.IDSpan) != "A" || a.IDSpan.Start != runeIndex(src, "A --> B") {
		t.Errorf("id span = %+v", a.IDSpan)
	}
}
