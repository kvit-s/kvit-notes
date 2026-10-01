package diagram

// These tests check what rendering and layout make of sources the
// on-diagram edits write: a parse that stops part way is not valid and a
// clean one is, huge pinned coordinates do not blow up the scene's bounds,
// an arranged diagram keeps its pinned centres, places unpinned nodes beyond
// them, routes its edges as curves (a CubicTo segment in the path) and bows
// parallel edges apart, and lines a plugin wrote parse. The edits
// themselves are tested in package mermaid.

import (
	"math"
	"testing"
)

// A source that failed to parse must not report a scene to draw because
// one node survived, or a typo in the middle of an edit replaces the working
// diagram.
func TestPartialParseIsNotValid(t *testing.T) {
	// A dangling arrow: the parser reports an error and still keeps one node.
	r := Render("flowchart TD\n  A[Good]\n  --> \n", testOpts())
	if !r.HasError {
		t.Error("expected this source to fail to parse")
	}
	if r.Valid {
		t.Error("a parse with errors must not replace the last good scene")
	}
}

func TestCleanParseIsStillValid(t *testing.T) {
	r := Render("flowchart TD\n  A[One] --> B[Two]\n", testOpts())
	if r.HasError {
		t.Errorf("error: %v", r.FirstError)
	}
	if !r.Valid {
		t.Error("a clean parse must still give a scene")
	}
}

func TestHugeCoordinatesDoNotExplodeSceneBounds(t *testing.T) {
	r := parse("flowchart TD\n  A[One]\n  B[Two]\n" +
		"%% mermaid-flow:pos A=0,0 B=1e12,1e12\n")
	s := LayoutFlowchart(&r.Flowchart, testOpts())
	t.Logf("scene bounds %g x %g", s.Bounds.W, s.Bounds.H)
	if s.Bounds.W >= 1e6 || s.Bounds.H >= 1e6 {
		t.Errorf("bounds %g x %g", s.Bounds.W, s.Bounds.H)
	}
}

// centers is the centre of each named node's shape.
func centers(s Scene, ids ...string) []Point {
	out := make([]Point, len(ids))
	for i, id := range ids {
		out[i] = shapeRect(s, id).Center()
	}
	return out
}

func TestArrangedModePinsCenters(t *testing.T) {
	r := parse("flowchart TD\n" +
		"  A --> B\n" +
		"%% mermaid-flow:pos A=100,50 B=300,200\n")
	c := centers(LayoutFlowchart(&r.Flowchart, testOpts()), "A", "B")
	// FinalizeSceneBounds moves everything by the same amount, so the
	// distances hold.
	if math.Round(c[1].X-c[0].X) != 200 || math.Round(c[1].Y-c[0].Y) != 150 {
		t.Errorf("B - A = %v, want (200, 150)", c[1].Sub(c[0]))
	}
}

func TestArrangedModePlacesUnpinnedBeyondContent(t *testing.T) {
	r := parse("flowchart TD\n" +
		"  A --> B\n" +
		"  B --> C\n" +
		"%% mermaid-flow:pos A=100,50 B=300,80\n")
	c := centers(LayoutFlowchart(&r.Flowchart, testOpts()), "A", "B", "C")
	// C, which has no entry, goes beyond the pinned content along the flow
	// without moving A or B.
	if c[2].Y <= max(c[0].Y, c[1].Y) {
		t.Errorf("C at %v is not below A %v and B %v", c[2], c[0], c[1])
	}
	if math.Round(c[1].X-c[0].X) != 200 {
		t.Errorf("B.x - A.x = %g, want 200", c[1].X-c[0].X)
	}
}

func TestArrangedModeRoutesBeziers(t *testing.T) {
	r := parse("flowchart TD\n" +
		"  A --> B\n" +
		"%% mermaid-flow:pos A=100,50 B=100,220\n")
	s := LayoutFlowchart(&r.Flowchart, testOpts())
	cubic := false
	for _, p := range s.Paths {
		if p.EdgeIndex < 0 {
			continue
		}
		for _, seg := range p.Outline.Segs {
			cubic = cubic || seg.Kind == CubicTo
		}
	}
	if !cubic {
		t.Error("no cubic curve")
	}
}

func TestArrangedModeBowsParallelEdges(t *testing.T) {
	r := parse("flowchart TD\n" +
		"  A --> B\n" +
		"  A --> B\n" +
		"%% mermaid-flow:pos A=100,50 B=100,220\n")
	s := LayoutFlowchart(&r.Flowchart, testOpts())
	var edges []Outline
	for _, p := range s.Paths {
		if p.EdgeIndex >= 0 {
			edges = append(edges, p.Outline)
		}
	}
	if len(edges) != 2 {
		t.Fatalf("edges = %d, want 2", len(edges))
	}
	// The two edges bow apart: their middles differ.
	if d := edges[0].PointAtPercent(0.5).Sub(edges[1].PointAtPercent(0.5)).Len(); d <= 8 {
		t.Errorf("the middles are %g apart", d)
	}
}

func TestPluginWrittenLinesParse(t *testing.T) {
	// A line as the obsidian-mermaid-flow plugin writes it.
	r := parse("flowchart LR\n" +
		"  Start --> Stop\n" +
		"%% mermaid-flow:pos Start=80,120 Stop=240,120,140,44\n")
	if !r.Flowchart.HasPosLine {
		t.Fatal("no pos line")
	}
	if len(r.Flowchart.PosEntries) != 2 {
		t.Fatalf("entries = %d, want 2", len(r.Flowchart.PosEntries))
	}
	c := centers(LayoutFlowchart(&r.Flowchart, testOpts()), "Start", "Stop")
	if math.Round(c[1].X-c[0].X) != 160 || math.Round(c[1].Y-c[0].Y) != 0 {
		t.Errorf("Stop - Start = %v, want (160, 0)", c[1].Sub(c[0]))
	}
}
