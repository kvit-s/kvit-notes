package editor

// Editing a Mermaid diagram on its drawing. The tests drive each gesture
// through the pointer and the keys, and check the source it writes, that it
// is one undo step, and what the status line says. The edits themselves are
// tested on the source in package mermaid.

import (
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-notes/diagram"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// statusOf collects what the editor says in the status line.
func statusOf(s *uitest.Session, e *Editor) *[]string {
	var said []string
	s.Do(func() { e.OnStatus = func(msg string) { said = append(said, msg) } })
	return &said
}

// nodeOnScreen is where node id's centre is on the headless screen.
func nodeOnScreen(s *uitest.Session, e *Editor, i int, id string) geom.Point {
	var p diagram.Point
	s.Do(func() { p = centerOf(e, i, id) })
	return onScreen(s, e, i, p)
}

// sourceOf is block i's text.
func sourceOf(s *uitest.Session, e *Editor, i int) string {
	var src string
	s.Do(func() { src = e.Doc.Blocks[i].Text })
	return src
}

// rendered waits for block i's drawing to be the current source's.
func rendered(s *uitest.Session, e *Editor, i int) {
	s.WaitFor("the diagram to render its source", func() bool { return readCanvas(e, i).sceneCurrent() })
}

// Dragging a node snaps it to the grid and to the lines of the nodes it
// comes near, draws its ghost and the guides meanwhile, and on release
// writes every node's place into the pos line, as one undo step.
func TestDiagramDragNodeArranges(t *testing.T) {
	src := "flowchart TD\n  A[Top] --> B[Left]\n  A --> C[Right]"
	s, e := openEditor(t, mermaidNote(src))
	said := statusOf(s, e)
	i := diagramIndex(e)
	rendered(s, e, i)
	from := nodeOnScreen(s, e, i, "C")
	s.Screen.MouseDown(from, unison.ButtonLeft, mod.None)
	// A few pixels down: C stays on B's line, with a guide along it.
	s.Screen.MouseMove(from.Add(geom.NewPoint(0, 3)), mod.None)
	s.Screen.MouseMove(from.Add(geom.NewPoint(40, 5)), mod.None)
	s.Do(func() {
		pan := e.diagPan
		if pan == nil || !pan.dragging || pan.node != "C" {
			t.Errorf("no drag of C: %+v", pan)
			return
		}
		b := centerOf(e, i, "B")
		if pan.center.Y != b.Y || len(pan.guideYs) != 1 || pan.guideYs[0] != b.Y {
			t.Errorf("C at %v, guides %v; B's line is at %g", pan.center, pan.guideYs, b.Y)
		}
		if int(pan.center.X)%dragGrid != 0 {
			t.Errorf("C's x %g is not on the grid", pan.center.X)
		}
	})
	s.Screen.MouseUp(from.Add(geom.NewPoint(40, 5)), unison.ButtonLeft, mod.None)
	after := sourceOf(s, e, i)
	if !strings.HasPrefix(after, src+"\n%% mermaid-flow:pos A=") || !strings.Contains(after, " C=") {
		t.Fatalf("source after the drag:\n%s", after)
	}
	rendered(s, e, i)
	s.Do(func() {
		c := readCanvas(e, i)
		if !c.hasArrangement {
			t.Error("the diagram is not arranged")
		}
		if b, cc := centerOf(e, i, "B"), centerOf(e, i, "C"); cc.Y != b.Y || cc.X-b.X < 40 {
			t.Errorf("B at %v, C at %v", b, cc)
		}
		if len(*said) == 0 || (*said)[len(*said)-1] != "Arranged C" {
			t.Errorf("status %q", *said)
		}
	})
	// One undo step takes it all back.
	s.Screen.KeyPress(unison.KeyZ, mod.Control)
	if got := sourceOf(s, e, i); got != src {
		t.Errorf("after undo:\n%s", got)
	}
}

// Reset layout takes the pos line out and restores the source exactly.
func TestDiagramResetLayout(t *testing.T) {
	src := "flowchart TD\n  A --> B"
	s, e := openEditor(t, mermaidNote(src+"\n%% mermaid-flow:pos A=100,50 B=300,200"))
	i := diagramIndex(e)
	rendered(s, e, i)
	clickChip(t, s, e, i, partDiagramReset)
	if got := sourceOf(s, e, i); got != src {
		t.Errorf("after Reset layout:\n%q", got)
	}
}

// A double-click on a node edits its label in place; F2 does the same from
// the keyboard, and Escape closes the field unchanged.
func TestDiagramEditsALabel(t *testing.T) {
	src := "flowchart LR\n  A[Start] --> B[End]"
	s, e := openEditor(t, mermaidNote(src))
	said := statusOf(s, e)
	i := diagramIndex(e)
	rendered(s, e, i)
	s.Screen.DoubleClick(nodeOnScreen(s, e, i, "A"))
	s.Sync()
	s.Screen.Type("Begin")
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	if got := sourceOf(s, e, i); got != "flowchart LR\n  A[Begin] --> B[End]" {
		t.Errorf("after editing A's label:\n%s", got)
	}
	s.Do(func() {
		if len(*said) == 0 || (*said)[len(*said)-1] != "Label updated" {
			t.Errorf("status %q", *said)
		}
	})
	rendered(s, e, i)
	// F2 on the selected node, then Escape: nothing changes.
	s.Screen.Click(nodeOnScreen(s, e, i, "B"))
	s.Screen.KeyPress(unison.KeyF2, mod.None)
	s.Sync()
	s.Screen.Type("Other")
	s.Screen.KeyPress(unison.KeyEscape, mod.None)
	if got := sourceOf(s, e, i); got != "flowchart LR\n  A[Begin] --> B[End]" {
		t.Errorf("Escape changed the source:\n%s", got)
	}
}

// The context menu renames a node across its references, changes its
// shape, and adds a node linked from it; access keys choose the entries.
func TestDiagramContextMenu(t *testing.T) {
	src := "flowchart LR\n  A[Start] --> B\n  B --> A"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	rendered(s, e, i)
	a := nodeOnScreen(s, e, i, "A")

	// Shape, Decision.
	s.Screen.ClickWith(a, unison.ButtonRight, mod.None)
	s.Screen.Type("s")
	s.Screen.Type("d")
	if got := sourceOf(s, e, i); got != "flowchart LR\n  A{Start} --> B\n  B --> A" {
		t.Fatalf("after Shape, Decision:\n%s", got)
	}
	rendered(s, e, i)

	// Rename id, which renames every reference.
	s.Screen.ClickWith(nodeOnScreen(s, e, i, "A"), unison.ButtonRight, mod.None)
	s.Screen.Type("r")
	s.Sync()
	s.Screen.Type("First")
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	if got := sourceOf(s, e, i); got != "flowchart LR\n  First{Start} --> B\n  B --> First" {
		t.Fatalf("after Rename id:\n%s", got)
	}
	rendered(s, e, i)
	s.Do(func() {
		if got := readCanvas(e, i).selNode; got != "First" {
			t.Errorf("the selection did not follow the rename: %q", got)
		}
	})

	// Add connected node.
	s.Screen.ClickWith(nodeOnScreen(s, e, i, "B"), unison.ButtonRight, mod.None)
	s.Screen.Type("a")
	got := sourceOf(s, e, i)
	if !strings.HasPrefix(got, "flowchart LR\n  First{Start} --> B\n  B --> First\n") || strings.Count(got, "-->") != 3 {
		t.Errorf("after Add connected node:\n%s", got)
	}
}

// Delete removes a selected edge, and then a selected node with the edges
// that touch it; a node styled together with another is refused, and the
// status line says why.
func TestDiagramDeletes(t *testing.T) {
	src := "flowchart LR\n  A --> B\n  B --> C"
	s, e := openEditor(t, mermaidNote(src))
	said := statusOf(s, e)
	i := diagramIndex(e)
	rendered(s, e, i)
	var mid diagram.Point
	s.Do(func() {
		a, b := centerOf(e, i, "A"), centerOf(e, i, "B")
		mid = diagram.Point{X: (a.X + b.X) / 2, Y: (a.Y + b.Y) / 2}
	})
	s.Screen.Click(onScreen(s, e, i, mid))
	s.Do(func() {
		if got := readCanvas(e, i).selEdge; got != 0 {
			t.Errorf("the press between A and B selected edge %d", got)
		}
	})
	s.Screen.KeyPress(unison.KeyDelete, mod.None)
	if got := sourceOf(s, e, i); got != "flowchart LR\n  A\n  B --> C" && got != "flowchart LR\n  B --> C" {
		t.Errorf("after deleting the edge:\n%q", got)
	}
	rendered(s, e, i)
	s.Screen.Click(nodeOnScreen(s, e, i, "C"))
	s.Screen.KeyPress(unison.KeyDelete, mod.None)
	if got := sourceOf(s, e, i); strings.Contains(got, "C") {
		t.Errorf("after deleting C:\n%q", got)
	}

	styled := "flowchart LR\n  A --> B\n  class A,B hot\n  classDef hot fill:#f00"
	s.Do(func() {
		e.Doc.Blocks[i].Text = styled
		e.changed()
	})
	rendered(s, e, i)
	s.Screen.Click(nodeOnScreen(s, e, i, "A"))
	s.Screen.KeyPress(unison.KeyDelete, mod.None)
	if got := sourceOf(s, e, i); got != styled {
		t.Errorf("a refused delete changed the source:\n%s", got)
	}
	s.Do(func() {
		if len(*said) == 0 || !strings.Contains((*said)[len(*said)-1], "styled") {
			t.Errorf("status %q", *said)
		}
	})
}

// Dragging one of the selected node's anchors onto another node draws an
// edge to it; pressing an anchor without dragging adds a connected node.
func TestDiagramDrawsAnEdgeFromAnAnchor(t *testing.T) {
	src := "flowchart LR\n  A[One]\n  B[Two]"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	rendered(s, e, i)
	s.Screen.Click(nodeOnScreen(s, e, i, "A"))
	var right geom.Point
	s.Do(func() {
		anchors := e.diagramAnchors(i)
		if len(anchors) != 4 {
			t.Errorf("anchors = %d", len(anchors))
			return
		}
		right = anchors[1].Center()
	})
	from := s.Screen.PanelPoint(e, right)
	to := nodeOnScreen(s, e, i, "B")
	s.Screen.MouseDown(from, unison.ButtonLeft, mod.None)
	s.Screen.MouseMove(from.Add(geom.NewPoint(10, 0)), mod.None)
	s.Screen.MouseMove(to, mod.None)
	s.Do(func() {
		if pan := e.diagPan; pan == nil || !pan.linking || pan.target != "B" {
			t.Errorf("not drawing an edge to B: %+v", pan)
		}
	})
	s.Screen.MouseUp(to, unison.ButtonLeft, mod.None)
	after := sourceOf(s, e, i)
	if !strings.Contains(after, "A --> B") {
		t.Fatalf("no edge from A to B:\n%s", after)
	}
	rendered(s, e, i)
	s.Screen.Click(nodeOnScreen(s, e, i, "B"))
	s.Do(func() { right = e.diagramAnchors(i)[1].Center() })
	s.Screen.Click(s.Screen.PanelPoint(e, right))
	if got := sourceOf(s, e, i); strings.Count(got, "-->") != 2 || !strings.HasPrefix(got, after) {
		t.Errorf("pressing an anchor did not add a node:\n%s", got)
	}
}

// A sequence diagram's messages move with Ctrl+Up and Ctrl+Down and its
// participants with Ctrl+Left and Ctrl+Right, the selection going with
// them; dragging a participant sideways moves it one place.
func TestDiagramReordersASequence(t *testing.T) {
	src := "sequenceDiagram\n  participant A\n  participant B\n  A->>B: one\n  B->>A: two"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	rendered(s, e, i)
	var one geom.Point
	s.Do(func() {
		c := readCanvas(e, i)
		for _, p := range c.scene.Paths {
			if p.EdgeIndex >= 0 && p.EndMarker != diagram.MarkerNone {
				one = e.sceneToEditor(i, diagram.Point{X: (p.StartPoint.X + p.EndPoint.X) / 2, Y: p.StartPoint.Y})
				break
			}
		}
	})
	s.Screen.Click(s.Screen.PanelPoint(e, one))
	s.Screen.KeyPress(unison.KeyDown, mod.Control)
	want := "sequenceDiagram\n  participant A\n  participant B\n  B->>A: two\n  A->>B: one"
	if got := sourceOf(s, e, i); got != want {
		t.Fatalf("after Ctrl+Down:\n%s", got)
	}
	rendered(s, e, i)
	s.Screen.KeyPress(unison.KeyUp, mod.Control)
	if got := sourceOf(s, e, i); got != src {
		t.Fatalf("after Ctrl+Up, the moved message did not come back:\n%s", got)
	}
	rendered(s, e, i)

	// The top header of participant A, dragged right past B.
	var head geom.Point
	s.Do(func() {
		for _, sh := range readCanvas(e, i).scene.Shapes {
			if sh.NodeID == "A" {
				head = e.sceneToEditor(i, diagram.Point{X: sh.Rect.X + sh.Rect.W/2, Y: sh.Rect.Y + sh.Rect.H/2})
				break
			}
		}
	})
	start := s.Screen.PanelPoint(e, head)
	s.Screen.Drag(start, start.Add(geom.NewPoint(60, 0)), 4)
	want = "sequenceDiagram\n  participant B\n  participant A\n  A->>B: one\n  B->>A: two"
	if got := sourceOf(s, e, i); got != want {
		t.Fatalf("after dragging A right:\n%s", got)
	}
	rendered(s, e, i)
	s.Screen.KeyPress(unison.KeyLeft, mod.Control)
	if got := sourceOf(s, e, i); got != src {
		t.Errorf("after Ctrl+Left:\n%s", got)
	}
}

// A read-only note's diagram can be selected, and no gesture changes it.
func TestDiagramReadOnlyRefusesGestures(t *testing.T) {
	src := "flowchart LR\n  A --> B"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	rendered(s, e, i)
	s.Do(func() { e.Doc.ReadOnly = true })
	a := nodeOnScreen(s, e, i, "A")
	s.Screen.Drag(a, a.Add(geom.NewPoint(40, 40)), 4)
	s.Screen.Click(a)
	s.Screen.KeyPress(unison.KeyDelete, mod.None)
	s.Screen.KeyPress(unison.KeyF2, mod.None)
	s.Screen.Type("x")
	if got := sourceOf(s, e, i); got != src {
		t.Errorf("a read-only diagram changed:\n%s", got)
	}
}

// TestDiagramEditScreenshots writes, when KVIT_DIAGRAM_SHOTS names a
// folder, a node being dragged with its guide, and the arranged flowchart
// after it is let go.
func TestDiagramEditScreenshots(t *testing.T) {
	dir := os.Getenv("KVIT_DIAGRAM_SHOTS")
	if dir == "" {
		t.Skip("KVIT_DIAGRAM_SHOTS names no folder")
	}
	md := "# Release pipeline\n\n```mermaid\nflowchart TD\n    A[Commit] --> B[CI build]\n    B --> C[Unit suite]\n" +
		"    B --> D[Packaging]\n    C --> E{Gates green?}\n    D --> E\n    E -->|yes| F[Draft release]\n    E -->|no| G[Fix and retag]\n```\n"
	s, e := openEditor(t, md)
	i := diagramIndex(e)
	rendered(s, e, i)
	from := nodeOnScreen(s, e, i, "G")
	to := from.Add(geom.NewPoint(90, 3))
	s.Screen.MouseDown(from, unison.ButtonLeft, mod.None)
	s.Screen.MouseMove(from.Add(geom.NewPoint(20, 1)), mod.None)
	s.Screen.MouseMove(to, mod.None)
	save := func(name string) {
		f, err := os.Create(filepath.Join(dir, name))
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		if err := png.Encode(f, s.Capture()); err != nil {
			t.Fatal(err)
		}
	}
	save("mermaid-edit-drag.png")
	s.Screen.MouseUp(to, unison.ButtonLeft, mod.None)
	rendered(s, e, i)
	s.Screen.MouseMove(geom.NewPoint(2, 2), mod.None)
	s.Sync()
	save("mermaid-edit-arranged.png")
}
