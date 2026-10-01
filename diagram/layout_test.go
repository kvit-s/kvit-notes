package diagram

// These tests check the flowchart layout and the renderer, measured as
// helpers_test.go describes: shapes and paths are produced, the same source
// gives the same scene, nodes do not overlap, edges end on node borders,
// subgraphs become groups, cycles and a 100-node flowchart lay out in time,
// the renderer flags a family it does not draw and reuses its cache, edges
// and their labels keep clear of nodes, and labels that are one $$…$$
// expression are typeset. Drawing a scene is tested in the editor.

import (
	"fmt"
	"math"
	"reflect"
	"slices"
	"strings"
	"testing"
	"time"
)

func TestProducesShapesAndPaths(t *testing.T) {
	s := LayoutFlowchart(parseFlow("flowchart TD\nA[Start]-->B{Choice}\nB-->C([End])\nB-->D((Stop))"), testOpts())
	if len(s.Shapes) != 4 {
		t.Fatalf("shapes = %d, want 4", len(s.Shapes))
	}
	if len(s.Paths) != 3 {
		t.Errorf("paths = %d, want 3", len(s.Paths))
	}
	if len(s.Texts) < 4 { // one label per node, and any edge labels
		t.Errorf("texts = %d, want at least 4", len(s.Texts))
	}
	if s.Bounds.W <= 0 || s.Bounds.H <= 0 {
		t.Errorf("bounds = %v", s.Bounds)
	}
	want := map[string]ShapeKind{"B": KindRhombus, "C": KindStadium, "D": KindCircle}
	for _, sh := range s.Shapes {
		if k, ok := want[sh.NodeID]; ok && sh.Kind != k {
			t.Errorf("node %s kind = %d, want %d", sh.NodeID, sh.Kind, k)
		}
	}
}

func TestDeterministicScene(t *testing.T) {
	src := "flowchart LR\nA-->B-->C\nA-->C\nC-->D\nB-->D\nZ-->A"
	s1 := LayoutFlowchart(parseFlow(src), testOpts())
	s2 := LayoutFlowchart(parseFlow(src), testOpts())
	if !reflect.DeepEqual(s1, s2) {
		t.Error("two layouts of the same source differ")
	}
	if len(s1.Shapes) != 5 || len(s1.Paths) != 6 {
		t.Errorf("shapes, paths = %d, %d; want 5, 6", len(s1.Shapes), len(s1.Paths))
	}
}

func TestNodesDoNotOverlap(t *testing.T) {
	s := LayoutFlowchart(parseFlow("flowchart TB\nA-->B\nA-->C\nB-->D\nC-->D"), testOpts())
	for i := range s.Shapes {
		for j := i + 1; j < len(s.Shapes); j++ {
			// Shrunk a little so touching borders do not count.
			a := s.Shapes[i].Rect.Adjusted(1, 1, -1, -1)
			b := s.Shapes[j].Rect.Adjusted(1, 1, -1, -1)
			if a.Intersects(b) {
				t.Errorf("overlap %s/%s", s.Shapes[i].NodeID, s.Shapes[j].NodeID)
			}
		}
	}
}

func TestEdgeEndpointsOnBorders(t *testing.T) {
	s := LayoutFlowchart(parseFlow("flowchart TB\nA-->B"), testOpts())
	if len(s.Shapes) != 2 || len(s.Paths) != 1 {
		t.Fatalf("shapes, paths = %d, %d; want 2, 1", len(s.Shapes), len(s.Paths))
	}
	ra, rb := s.Shapes[0].Rect, s.Shapes[1].Rect
	p := s.Paths[0]
	onBorder := func(r Rect, pt Point) bool {
		const eps = 1.5
		inX := pt.X >= r.Left()-eps && pt.X <= r.Right()+eps
		inY := pt.Y >= r.Top()-eps && pt.Y <= r.Bottom()+eps
		nearEdge := math.Abs(pt.X-r.Left()) < eps || math.Abs(pt.X-r.Right()) < eps ||
			math.Abs(pt.Y-r.Top()) < eps || math.Abs(pt.Y-r.Bottom()) < eps
		return inX && inY && nearEdge
	}
	if !onBorder(rb, p.EndPoint) {
		t.Errorf("end %v is not on B's border %v", p.EndPoint, rb)
	}
	if !onBorder(ra, p.StartPoint) {
		t.Errorf("start %v is not on A's border %v", p.StartPoint, ra)
	}
	if p.EndMarker != MarkerArrow {
		t.Errorf("end marker = %d, want an arrow", p.EndMarker)
	}
}

func TestSubgraphProducesGroup(t *testing.T) {
	s := LayoutFlowchart(parseFlow("flowchart TB\nsubgraph g [Box]\nA-->B\nend\nB-->C"), testOpts())
	if len(s.Groups) != 1 {
		t.Fatalf("groups = %d, want 1", len(s.Groups))
	}
	g := s.Groups[0]
	if g.Title != "Box" {
		t.Errorf("title = %q, want Box", g.Title)
	}
	// The group holds its members A and B.
	for _, id := range []string{"A", "B"} {
		if !g.Rect.Contains(shapeRect(s, id).Center()) {
			t.Errorf("the group does not hold %s", id)
		}
	}
}

func TestCyclesDoNotHang(t *testing.T) {
	// A cycle of three must rank and lay out without hanging.
	s := LayoutFlowchart(parseFlow("flowchart LR\nA-->B\nB-->C\nC-->A"), testOpts())
	if len(s.Shapes) != 3 || len(s.Paths) != 3 {
		t.Errorf("shapes, paths = %d, %d; want 3, 3", len(s.Shapes), len(s.Paths))
	}
}

func TestRendererFlagsUnsupportedFamily(t *testing.T) {
	ClearCache()
	r := Render("gantt\n  title Deferred family", testOpts())
	if r.Valid {
		t.Error("a gantt chart is valid")
	}
	if !r.UnsupportedFamily {
		t.Error("a gantt chart is not flagged as unsupported")
	}
	if !r.HasError {
		t.Error("a gantt chart has no error")
	}
}

func TestRendererCacheHit(t *testing.T) {
	ClearCache()
	src := "flowchart LR\nA-->B-->C"
	r1 := Render(src, testOpts())
	afterFirst := CacheCount()
	r2 := Render(src, testOpts())
	if !r1.Valid || !r2.Valid {
		t.Fatal("not valid")
	}
	if afterFirst < 1 {
		t.Errorf("cache count = %d after the first render", afterFirst)
	}
	if len(r1.Scene.Shapes) != len(r2.Scene.Shapes) {
		t.Errorf("shapes = %d then %d", len(r1.Scene.Shapes), len(r2.Scene.Shapes))
	}
}

func TestLargeFlowchartWithinBudget(t *testing.T) {
	// A flowchart of 100 nodes and about 150 edges lays out well within
	// budget.
	var b strings.Builder
	b.WriteString("flowchart TB\n")
	for i := range 100 {
		fmt.Fprintf(&b, "n%d[Node %d]\n", i, i)
	}
	for i := range 99 {
		fmt.Fprintf(&b, "n%d --> n%d\n", i, i+1)
	}
	for i := range 50 {
		fmt.Fprintf(&b, "n%d --> n%d\n", i, (i+7)%100)
	}
	a := parseFlow(b.String())
	if len(a.Nodes) != 100 {
		t.Fatalf("nodes = %d, want 100", len(a.Nodes))
	}
	start := time.Now()
	s := LayoutFlowchart(a, testOpts())
	elapsed := time.Since(start)
	if len(s.Shapes) != 100 {
		t.Errorf("shapes = %d, want 100", len(s.Shapes))
	}
	t.Logf("100-node layout: %v", elapsed)
	if elapsed > 1500*time.Millisecond {
		t.Errorf("layout took %v", elapsed)
	}
}

// ---- edge labels and the nodes they must keep clear of ----

// An edge whose ends are more than one rank apart passes over the ranks
// between them. Its route and its label both have to keep out of whatever
// stands in those ranks, or the label is printed over a box and the line
// runs through it.
func TestRankSkippingEdgeClearsTheNodeBetween(t *testing.T) {
	// B->C spans two ranks (B, then D, then C), so it passes over D.
	s := layoutAny("flowchart LR\n" +
		"A([Start]) --> B{Vault set?}\n" +
		"B -- yes --> C[Open collection]\n" +
		"B -- no --> D[(Seed Welcome)]\n" +
		"D --> C\n" +
		"C --> E[/Render note/]")
	d := shapeRect(s, "D")
	if d.IsNull() {
		t.Fatal("no node D")
	}
	for _, tx := range s.Texts {
		if tx.Role == RoleEdgeLabel && tx.Rect.Intersects(d) {
			t.Errorf("edge label %q printed over node D", tx.Text)
		}
	}
	// The route must not cross D either: the same defect seen as a line
	// through a box rather than text over one.
	for _, p := range s.Paths {
		if p.Outline.Intersects(d.Adjusted(2, 2, -2, -2)) {
			t.Errorf("edge %d is routed through node D", p.EdgeIndex)
		}
	}
}

// Every edge label sits in open space, in one diagram of each family.
func TestChecklistDiagramsKeepLabelsOffBoxes(t *testing.T) {
	cases := []struct{ name, src string }{
		{"flowchart", "flowchart LR\n" +
			"    A([Start]) --> B{Vault set?}\n" +
			"    B -- yes --> C[Open collection]\n" +
			"    B -- no --> D[(Seed Welcome)]\n" +
			"    D --> C\n" +
			"    C --> E[/Render note/]\n"},
		{"sequence", "sequenceDiagram\n" +
			"    autonumber\n" +
			"    participant U as User\n" +
			"    participant E as Editor\n" +
			"    participant S as Serializer\n" +
			"    U->>E: type a heading\n" +
			"    activate E\n" +
			"    E->>S: block changed\n" +
			"    S-->>E: markdown\n" +
			"    deactivate E\n" +
			"    Note over S: debounced save\n"},
		{"class", "classDiagram\n" +
			"    class Block {\n" +
			"        +BlockType type\n" +
			"        +QString content\n" +
			"        +render() void\n" +
			"    }\n" +
			"    class CodeBlock {\n" +
			"        +QString language\n" +
			"    }\n" +
			"    Block <|-- CodeBlock\n" +
			"    Block \"1\" o-- \"0..*\" Attribute\n"},
		{"state", "stateDiagram-v2\n" +
			"    [*] --> Idle\n" +
			"    Idle --> Editing: keypress\n" +
			"    Editing --> Saving: debounce\n" +
			"    Saving --> Idle: written\n" +
			"    Saving --> Conflict: file changed\n" +
			"    Conflict --> Idle: resolved\n" +
			"    Conflict --> [*]: discarded\n"},
		{"er", "erDiagram\n" +
			"    COLLECTION ||--o{ NOTE : contains\n" +
			"    NOTE ||--o{ BLOCK : \"is made of\"\n" +
			"    NOTE }o--o{ NOTE : links-to\n" +
			"    NOTE {\n" +
			"        string title PK\n" +
			"        date created\n" +
			"        string tags \"comma separated\"\n" +
			"    }\n"},
	}
	for _, c := range cases {
		s := layoutAny(c.src)
		if len(s.Shapes) == 0 {
			t.Errorf("%s: no shapes", c.name)
			continue
		}

		// The ground an arrowhead, diamond or crow's foot covers: from the
		// end it sits on, back along the line it came in on, widened a
		// little across the line, since every marker has some width.
		var markers []Rect
		addMarker := func(kind Marker, tip, outward Point) {
			reach := MarkerLength(kind)
			l := outward.Len()
			if reach <= 0 || l < 0.001 {
				return
			}
			end := tip.Add(outward.Neg().Div(l).Mul(reach))
			r := Rect{tip.X, tip.Y, end.X - tip.X, end.Y - tip.Y}.Normalized()
			markers = append(markers, r.Adjusted(-2, -2, 2, 2))
		}
		for _, p := range s.Paths {
			addMarker(p.StartMarker, p.StartPoint, p.StartDir)
			addMarker(p.EndMarker, p.EndPoint, p.EndDir)
		}

		var seen []Rect
		for _, tx := range s.Texts {
			if tx.Role != RoleEdgeLabel || tx.Text == "" {
				continue
			}
			for _, sh := range s.Shapes {
				// Two pixels of contact with a border is contact rather
				// than an overlap.
				if tx.Rect.Intersects(sh.Rect.Adjusted(2, 2, -2, -2)) {
					t.Errorf("%s: label %q over node %q", c.name, tx.Text, sh.NodeID)
				}
			}
			// A text rectangle has the font's leading above and below the
			// glyphs, so a label set just clear of a line brushes the
			// marker's box without a reader seeing them touch. The ink is
			// what is judged.
			ink := tx.Rect.Adjusted(2, 3, -2, -3)
			for _, m := range markers {
				if ink.Intersects(m) {
					t.Errorf("%s: label %q over a line end", c.name, tx.Text)
				}
			}
			for _, other := range seen {
				if tx.Rect.Intersects(other) {
					t.Errorf("%s: label %q over another label", c.name, tx.Text)
				}
			}
			seen = append(seen, tx.Rect)
		}
	}
}

// ---- mathematics in labels ----

// Only a whole `$$…$$` label is an expression. Everything else stays text,
// which keeps every diagram written before math labels drawn as it was.
func TestMathLabelRecognizesWholeLabelsOnly(t *testing.T) {
	for in, want := range map[string]string{
		"$$x^2$$":             "x^2",
		"$$x^2$$<br>":         "x^2", // line-break markup is normalized first
		`  $$\frac{a}{b}$$  `: `\frac{a}{b}`,
	} {
		if got := MathLabel(in); got != want {
			t.Errorf("MathLabel(%q) = %q, want %q", in, got, want)
		}
	}
	for _, in := range []string{
		// A single dollar is text: currency and shell variables in existing
		// diagrams must not start typesetting.
		"costs $5 and $6",
		"$PATH",
		// Mixed labels stay text.
		"Step $$x^2$$ done",
		// Two expressions are not one label's worth of mathematics.
		"$$a$$ $$b$$",
		"$$$$",
		"plain",
	} {
		if got := MathLabel(in); got != "" {
			t.Errorf("MathLabel(%q) = %q, want none", in, got)
		}
	}
}

// An expression that does not typeset falls back to its source: no size, so
// layout measures the label's text and the editor draws it. Here fakeMath
// rejects the expression as MicroTeX would.
func TestUnparseableMathFallsBackToItsSource(t *testing.T) {
	o := testOpts()
	if _, ok := o.mathSize(MathLabel("$$x^2$$")); !ok {
		t.Error("x^2 does not typeset")
	}
	// An unmatched brace and a bare alignment ampersand are the two shapes
	// MicroTeX rejects outright.
	for _, tex := range []string{"}", "a & b", ""} {
		if _, ok := o.mathSize(tex); ok {
			t.Errorf("%q typesets", tex)
		}
	}

	// A node whose expression does not typeset is laid out and drawn from
	// its source, so it has no TeX in the scene.
	s := LayoutFlowchart(parseFlow("flowchart LR\nA[\"$$}$$\"]"), testOpts())
	if len(s.Texts) == 0 {
		t.Fatal("no texts")
	}
	if s.Texts[0].Tex != "" {
		t.Errorf("tex = %q, want none", s.Texts[0].Tex)
	}
	if s.Texts[0].Text != "$$}$$" {
		t.Errorf("text = %q", s.Texts[0].Text)
	}
}

func TestFlowchartTypesetsMathNodeAndEdgeLabels(t *testing.T) {
	s := LayoutFlowchart(parseFlow("flowchart LR\n"+
		"A[\"$$\\frac{a}{b}$$\"] -->|\"$$x^2$$\"| B[plain]\n"), testOpts())
	var node, edge, plain *Text
	for i := range s.Texts {
		tx := &s.Texts[i]
		switch {
		case tx.Role == RoleEdgeLabel:
			edge = tx
		case tx.Text == "plain":
			plain = tx
		case strings.Contains(tx.Text, "frac"):
			node = tx
		}
	}
	if node == nil || edge == nil || plain == nil {
		t.Fatalf("node, edge, plain = %v, %v, %v", node, edge, plain)
	}
	// The expression goes to the editor, and the source stays beside it for
	// the fallback and for screen readers.
	if node.Tex != `\frac{a}{b}` {
		t.Errorf("node tex = %q", node.Tex)
	}
	if node.Text != `$$\frac{a}{b}$$` {
		t.Errorf("node text = %q", node.Text)
	}
	if edge.Tex != "x^2" {
		t.Errorf("edge tex = %q", edge.Tex)
	}
	if plain.Tex != "" {
		t.Errorf("plain tex = %q", plain.Tex)
	}
	// The node box was sized from the formula: a fraction is taller than a
	// line of text.
	if node.Rect.H <= plain.Rect.H {
		t.Errorf("a fraction's node (%g high) is not taller than a word's (%g)", node.Rect.H, plain.Rect.H)
	}
}

func TestSequenceTypesetsMessageParticipantAndNote(t *testing.T) {
	r := parse("sequenceDiagram\n" +
		"participant A as \"$$\\alpha$$\"\n" +
		"participant B\n" +
		"A->>B: $$\\int_0^1 f$$\n" +
		"Note right of B: $$e^{i\\pi}+1=0$$\n")
	s := LayoutSequence(&r.Sequence, testOpts())
	var found []string
	for _, tx := range s.Texts {
		if tx.Tex != "" {
			found = append(found, tx.Tex)
		}
	}
	// The participant's label is at the top and the bottom of its lifeline,
	// so its expression is there twice.
	if !slices.Contains(found, `\alpha`) {
		t.Errorf("a participant name is not typeset: %q", found)
	}
	if !slices.Contains(found, `\int_0^1 f`) {
		t.Errorf("a message label is not typeset: %q", found)
	}
	if !slices.Contains(found, `e^{i\pi}+1=0`) {
		t.Errorf("a note is not typeset: %q", found)
	}

	// The diagram's title is not one of the kinds Mermaid typesets, so it
	// stays text even when it looks like an expression.
	titled := parse("sequenceDiagram\ntitle $$x^2$$\nA->>B: hi\n")
	for _, tx := range LayoutSequence(&titled.Sequence, testOpts()).Texts {
		if tx.Text == "$$x^2$$" && tx.Tex != "" {
			t.Error("a title is typeset")
		}
	}
}
