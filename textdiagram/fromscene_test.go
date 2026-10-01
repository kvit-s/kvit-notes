package textdiagram

// These tests check FromScene on flowcharts (back edges, fan-out, fan-in,
// self-loops, subgraphs) and on sequence, class, state and ER diagrams, and
// that it draws the same text twice and stays bounded. Each name starts
// with FromScene so it cannot meet the package's other tests. The sources
// are laid out through diagram.Render with a fixed advance of 8 pixels a
// character and a line of 17, close to sans-serif at 14 pixels. Every
// character counted here is one UTF-16 unit, so a rune count is the same
// number, and the size limit is checked in UTF-16 units with utf16Len.

import (
	"fmt"
	"strings"
	"testing"
	"time"
	"unicode"
	"unicode/utf8"

	"github.com/kvit-s/kvit-notes/diagram"
)

type fixedMeasurer struct{}

func (fixedMeasurer) Advance(s string) float64 { return 8 * float64(utf8.RuneCountInString(s)) }
func (fixedMeasurer) Height() float64          { return 17 }

// renderSource lays the source out and draws it as text, or "" when it
// does not render.
func renderSource(source string) string {
	r := diagram.Render(source, diagram.LayoutOptions{
		FontFamily: "sans-serif", FontSize: 14, Measure: fixedMeasurer{},
	})
	if !r.Valid {
		return ""
	}
	return FromScene(&r.Scene)
}

const (
	armU = 1
	armD = 2
	armL = 4
	armR = 8
)

// armsOfGlyph is the arms a box-drawing character reaches out with.
func armsOfGlyph(r rune) int {
	switch r {
	case '─':
		return armL | armR
	case '│', '║':
		return armU | armD
	case '┌':
		return armD | armR
	case '┐':
		return armD | armL
	case '└':
		return armU | armR
	case '┘':
		return armU | armL
	case '├':
		return armU | armD | armR
	case '┤':
		return armU | armD | armL
	case '┬':
		return armD | armL | armR
	case '┴':
		return armU | armL | armR
	case '┼':
		return armU | armD | armL | armR
	}
	return 0
}

// terminates reports a character that may end a line: an arrowhead, one of
// the simpler UML and ER markers, or a label's character, since a label may
// displace the line it is on.
func terminates(r rune) bool {
	return r != 0 && r != ' ' && (strings.ContainsRune("▲▼◄►△◇~", r) ||
		unicode.IsLetter(r) || unicode.IsNumber(r) || unicode.IsPunct(r) || unicode.IsSymbol(r))
}

// verifyClosure checks what every family's text must be: not empty, taken
// for a diagram by Classify, left as it is by Repair, and without a line
// that ends in nothing.
func verifyClosure(t *testing.T, out string) {
	t.Helper()
	if out == "" {
		t.Fatal("no text")
	}
	if !LooksLikeDiagram(out) {
		t.Errorf("the classifier rejects:\n%s", out)
	}
	if got := Repair(out); got != out {
		t.Errorf("Repair changes the text:\n%s\ninto\n%s", out, got)
	}
	verifyNoDanglingArms(t, out)
}

// verifyNoDanglingArms checks that every arm of every line character meets
// a neighbour that goes on with the line or ends it. A route that stops in
// the air, such as the stray dash a Z route with no length used to leave, a
// loop that never comes back, or a detour drawn across a box, reads as a
// diagram whose edges do not connect.
func verifyNoDanglingArms(t *testing.T, out string) {
	t.Helper()
	var lines [][]rune
	for _, l := range strings.Split(out, "\n") {
		lines = append(lines, []rune(l))
	}
	at := func(row, col int) rune {
		if row < 0 || row >= len(lines) || col < 0 || col >= len(lines[row]) {
			return 0
		}
		return lines[row][col]
	}
	steps := []struct{ arm, dRow, dCol, back int }{
		{armU, -1, 0, armD}, {armD, 1, 0, armU}, {armL, 0, -1, armR}, {armR, 0, 1, armL},
	}
	for row := range lines {
		for col := range lines[row] {
			arms := armsOfGlyph(at(row, col))
			for _, s := range steps {
				if arms&s.arm == 0 {
					continue
				}
				next := at(row+s.dRow, col+s.dCol)
				if armsOfGlyph(next)&s.back != 0 || terminates(next) {
					continue
				}
				// A label the line runs into keeps its padding: a
				// subgraph's title sits in its frame as `┌─ Name ─┐`, and an
				// edge label displaces its line with a space either side.
				if next == ' ' && terminates(at(row+2*s.dRow, col+2*s.dCol)) {
					continue
				}
				t.Fatalf("dangling arm at row %d col %d:\n%s", row, col, out)
			}
		}
	}
}

// verifyEdgeLeavesRightWall checks that an edge leaves the box labelled
// label through its right wall: the cell beyond the wall has an arm
// reaching back to it. Which character that is depends on the font the
// scene was measured in, so only the connection is checked.
func verifyEdgeLeavesRightWall(t *testing.T, out, label string) {
	t.Helper()
	wall := fmt.Sprintf("│ %s │", label)
	at := strings.Index(out, wall)
	if at < 0 {
		t.Fatalf("no %s box:\n%s", label, out)
	}
	rest := out[at+len(wall):]
	if rest == "" {
		t.Fatalf("%s's wall ends the drawing:\n%s", label, out)
	}
	next, _ := utf8.DecodeRuneInString(rest)
	if armsOfGlyph(next)&armL == 0 {
		t.Errorf("nothing leaves %s's right wall (found %q):\n%s", label, next, out)
	}
}

func wantContains(t *testing.T, out string, parts ...string) {
	t.Helper()
	for _, p := range parts {
		if !strings.Contains(out, p) {
			t.Errorf("no %q in:\n%s", p, out)
		}
	}
}

func wantCount(t *testing.T, out, part string, want int) {
	t.Helper()
	if got := strings.Count(out, part); got != want {
		t.Errorf("%q appears %d times, want %d:\n%s", part, got, want, out)
	}
}

func TestFromSceneEmptyScene(t *testing.T) {
	if got := FromScene(&diagram.Scene{}); got != "" {
		t.Errorf("an empty scene gives %q", got)
	}
}

func TestFromSceneFlowchartFixture(t *testing.T) {
	out := renderSource("flowchart TD\n" +
		"  A[Start] --> B{Decision}\n" +
		"  B -->|yes| C[Done]\n" +
		"  B -->|no| D[Retry]\n")
	verifyClosure(t, out)
	// Every label is inside a box; the decision shows as < … >.
	wantContains(t, out, "│ Start │", "< Decision >", "│ Done │", "│ Retry │")
	// The edges point down, and their labels go with them.
	if strings.Count(out, "▼") < 3 {
		t.Errorf("fewer than three ▼:\n%s", out)
	}
	wantContains(t, out, "yes", "no")
	// Light corners only.
	wantContains(t, out, "┌", "└")
}

func TestFromSceneFlowchartBackEdgeAvoidsBoxes(t *testing.T) {
	out := renderSource("flowchart TD\n" +
		"  A[Start] --> B{Decision}\n" +
		"  B -->|yes| C[Done]\n" +
		"  B -->|no| D[Retry]\n" +
		"  D --> A\n")
	verifyClosure(t, out)
	// The back edge goes round the side and enters a side wall.
	wantContains(t, out, "◄")
	// No line cuts through a label: each still sits whole between its
	// walls.
	wantContains(t, out, "│ Start │", "< Decision >", "│ Retry │")
}

// One node fanning out to three. A box three rows high has one usable cell
// on each wall, so all three edges leave through the same one; when the
// first edge's stub counted as taken, the other two gave up on a direct
// route and went round below the drawing, arriving at the wrong boxes from
// underneath. The branches share the stub and split at one spine instead.
func TestFromSceneFlowchartFanOutReachesEveryTarget(t *testing.T) {
	out := renderSource("flowchart LR\n" +
		"  A[Write markdown] --> B{Rendered live}\n" +
		"  B --> C[Math]\n" +
		"  B --> D[Diagrams]\n" +
		"  B --> E[Tables]\n")
	verifyClosure(t, out)
	// Every edge arrives at its own target's left wall.
	wantContains(t, out, "►│ < Rendered live > │", "►│ Math │", "►│ Diagrams │", "►│ Tables │")
	// Four edges and four arrowheads, all pointing right: none went round
	// below, which would arrive pointing up.
	wantCount(t, out, "►", 4)
	wantCount(t, out, "▲", 0)
	wantCount(t, out, "▼", 0)
}

// The mirror of the fan-out: three edges arriving at one wall cell.
func TestFromSceneFlowchartFanInReachesTheTarget(t *testing.T) {
	out := renderSource("flowchart LR\n" +
		"  A[One] --> D[Sink]\n" +
		"  B[Two] --> D\n" +
		"  C[Three] --> D\n")
	verifyClosure(t, out)
	wantContains(t, out, "►│ Sink │")
	// A line leaves each source's right wall.
	verifyEdgeLeavesRightWall(t, out, "One")
	verifyEdgeLeavesRightWall(t, out, "Two")
	verifyEdgeLeavesRightWall(t, out, "Three")
	wantCount(t, out, "▲", 0)
}

// A box three rows high has one row inside, so a loop cannot leave and
// return through the same wall. It leaves the side, drops past the box and
// comes back up into the floor; returning along the row it left on drew a
// stub that went nowhere.
func TestFromSceneFlowchartSelfLoopReturns(t *testing.T) {
	out := renderSource("flowchart TD\n" +
		"  A[Loop] --> A\n" +
		"  A --> B[Next]\n")
	verifyClosure(t, out)
	wantContains(t, out, "│ Loop │─")
	// The loop comes back into the floor, and the other edge still drops
	// into Next.
	wantContains(t, out, "▲", "│ Next │")
	wantCount(t, out, "▼", 1)
}

func TestFromSceneFlowchartSubgraph(t *testing.T) {
	out := renderSource("flowchart TD\n" +
		"  subgraph Backend\n" +
		"    S[Server] --> Q[Queue]\n" +
		"  end\n" +
		"  U[User] --> S\n")
	verifyClosure(t, out)
	wantContains(t, out, "Backend", "│ Server │", "│ Queue │")
}

func TestFromSceneSequenceFixture(t *testing.T) {
	out := renderSource("sequenceDiagram\n" +
		"  participant A as Alice\n" +
		"  participant B as Bob\n" +
		"  A->>B: Hello\n" +
		"  B-->>A: Hi back\n")
	verifyClosure(t, out)
	// A box at the top and the bottom of each lifeline, so each name is
	// there twice.
	wantCount(t, out, "│ Alice │", 2)
	wantCount(t, out, "│ Bob │", 2)
	// One arrow each way, with the labels over the lines.
	wantContains(t, out, "►", "◄", "Hello", "Hi back")
	// Lifelines run down between the boxes.
	wantContains(t, out, "│")
}

func TestFromSceneClassFixture(t *testing.T) {
	out := renderSource("classDiagram\n" +
		"  class Animal {\n" +
		"    +name: string\n" +
		"    +speak()\n" +
		"  }\n" +
		"  Animal <|-- Dog\n")
	verifyClosure(t, out)
	wantContains(t, out, "Animal", "+name: string", "+speak()", "Dog")
	// The UML extension head is drawn as the open triangle, on purpose.
	wantContains(t, out, "△")
	// A compartment line crosses the box and has no ends of its own.
	// Routed as an edge, it hung a loop off the box's side; the box's rows
	// now end at its right wall.
	for _, line := range strings.Split(out, "\n") {
		if (strings.Contains(line, "Animal") || strings.Contains(line, "+name")) && !strings.HasSuffix(line, "│") {
			t.Errorf("line %q does not end at the wall:\n%s", line, out)
		}
	}
}

func TestFromSceneStateFixture(t *testing.T) {
	out := renderSource("stateDiagram-v2\n" +
		"  [*] --> Idle\n" +
		"  Idle --> Busy: start\n" +
		"  Busy --> [*]\n")
	verifyClosure(t, out)
	wantContains(t, out, "│ Idle │", "│ Busy │", "start")
	// The start and end circles are (*) boxes, two of them: the end
	// state's inner disc joins its outer circle.
	wantCount(t, out, "(*)", 2)
	if strings.Count(out, "▼") < 3 {
		t.Errorf("fewer than three ▼:\n%s", out)
	}
}

func TestFromSceneErFixture(t *testing.T) {
	out := renderSource("erDiagram\n" +
		"  CUSTOMER ||--o{ ORDER : places\n")
	verifyClosure(t, out)
	wantContains(t, out, "CUSTOMER", "ORDER", "places")
	// A crow's foot is drawn as one of < > ^ v.
	if !strings.ContainsAny(out, "v^<>") {
		t.Errorf("no crow's foot:\n%s", out)
	}
}

func TestFromSceneDeterminism(t *testing.T) {
	source := "flowchart LR\n  A[Input] --> B[Process]\n  B --> C[Output]\n" +
		"  B --> D[Log]\n  D --> A\n"
	diagram.ClearCache() // the second drawing starts from a new layout too
	once := renderSource(source)
	diagram.ClearCache()
	twice := renderSource(source)
	if once == "" {
		t.Fatal("no text")
	}
	if once != twice {
		t.Errorf("two drawings differ:\n%s\n\n%s", once, twice)
	}
}

// A note's arrangement comment decides where nodes sit, and so how large a
// grid "Copy as text" builds. Two nodes pinned far apart on both axes would
// make a grid of about 2 × 10^8 cells, about 500 MiB, without a limit. The
// canvas stops at a cell limit, so the work and the text are bounded by the
// limit rather than by the coordinates.
func TestFromSceneExtremePinnedCoordinatesStayBounded(t *testing.T) {
	start := time.Now()
	out := renderSource("flowchart TD\n" +
		"  A[Start] --> B[End]\n" +
		"%% mermaid-flow:pos A=0,0 B=170000,200000\n")
	elapsed := time.Since(start)
	// The time allowed is far above what the cut-off drawing costs and far
	// below what the unbounded one did, so it does not depend on the
	// machine's speed.
	if elapsed > 5*time.Second {
		t.Errorf("drawing took %v", elapsed)
	}
	// At most one character for each cell allowed, and the line breaks.
	if n := utf16Len(out); n > 2*MaxCanvasCells {
		t.Errorf("the text is %d characters", n)
	}
	// Still a drawing: the first node came out.
	wantContains(t, out, "┌")
}
