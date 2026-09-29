package editor

// Mermaid diagram blocks. The first four tests are the functions of the
// app's tests/test_diagramlayout.cpp that exercise the DiagramCanvas item or
// the painter, which the diagram port left for the editor:
//   - canvasSelectionAndLinking, its half that does not edit the diagram:
// here through the editor's pointer and keys rather than the canvas's
// invokable methods, which the editor has no need to expose;
//   - resetSceneDropsLastGoodAcrossReuse: Kvit's list view reuses a delegate
// for another block, which the Go editor never does; the canvas's reset is
// tested as there, and so is the Go counterpart of the reported bug, a
// block turned into a diagram from another language;
//   - savePngWritesImage;
//   - painterTypesetsRatherThanDrawingTheSource, with a stand-in DiagramMath,
// so the test does not depend on the math library being built: it checks
// that the painter takes the typesetting branch, as the test does.
// The tests after them are not ports: switching between the drawing and the
// source, the preview that keeps the last valid diagram, zoom and panning,
// Copy and Copy as text, the PNG control, what a screen reader is told,
// printing, and the Tab and Enter keys of the source.

import (
	"bytes"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-notes/diagram"
	"github.com/kvit-s/kvit-notes/textdiagram"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/role"
)

// mermaidNote is a note holding the fence the test renders, and a
// paragraph after it.
func mermaidNote(src string) string {
	return "```mermaid\n" + src + "\n```\n\nAfter the diagram.\n"
}

// diagramIndex is the index of the note's first diagram block.
func diagramIndex(e *Editor) int {
	for i := range e.Doc.Blocks {
		if isMermaid(&e.Doc.Blocks[i]) {
			return i
		}
	}
	return -1
}

// readCanvas is block i's read canvas, made if need be.
func readCanvas(e *Editor, i int) *diagramCanvas { return e.diagramFor(&e.Doc.Blocks[i]).read }

// waitRendered waits until block i's read canvas has a result for its
// source.
func waitRendered(s *uitest.Session, e *Editor, i int) {
	s.WaitFor("the diagram to render", func() bool {
		c := readCanvas(e, i)
		return c.source == e.Doc.Blocks[i].Text && !c.rendering && (c.hasScene || c.hasError)
	})
}

// onScreen is where a point of block i's scene is on the headless screen.
func onScreen(s *uitest.Session, e *Editor, i int, p diagram.Point) geom.Point {
	var at geom.Point
	s.Do(func() {
		v := e.diagramFor(&e.Doc.Blocks[i])
		parts := e.diagramReadParts(i)
		at = geom.NewPoint(parts.window.X-v.pan.X+float32(p.X)*parts.scale, parts.window.Y-v.pan.Y+float32(p.Y)*parts.scale)
	})
	return s.Screen.PanelPoint(e, at)
}

func centerOf(e *Editor, i int, id string) diagram.Point {
	r, _ := readCanvas(e, i).scene.NodeRect(id)
	return diagram.Point{X: r.X + r.W/2, Y: r.Y + r.H/2}
}

func setText(s *uitest.Session, e *Editor, i int, src string) {
	s.Do(func() {
		e.Doc.Blocks[i].Text = src
		e.changed()
	})
}

func TestDiagramCanvasSelectionAndLinking(t *testing.T) {
	src := "flowchart LR\n  A[Start] --> B"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	waitRendered(s, e, i)
	var a, b diagram.Point
	s.Do(func() {
		c := readCanvas(e, i)
		if !c.hasScene || !c.sceneCurrent() {
			t.Error("no current scene")
			return
		}
		a, b = centerOf(e, i, "A"), centerOf(e, i, "B")
		if c.scene.NodeAt(a) != "A" || c.scene.NodeAt(b) != "B" {
			t.Errorf("nodes at the centres = %q, %q", c.scene.NodeAt(a), c.scene.NodeAt(b))
		}
		// The middle between them is on edge 0; the corner is empty.
		mid := diagram.Point{X: (a.X + b.X) / 2, Y: (a.Y + b.Y) / 2}
		if got := c.scene.EdgeAt(mid); got != 0 {
			t.Errorf("edge at the middle = %d", got)
		}
		if got := c.scene.NodeAt(diagram.Point{X: 2, Y: 2}); got != "" {
			t.Errorf("node at (2, 2) = %q", got)
		}
	})

	// A press on A selects it.
	s.Screen.Click(onScreen(s, e, i, a))
	off := -1
	s.Do(func() {
		c := readCanvas(e, i)
		if !c.hasSelection() || c.selNode != "A" {
			t.Errorf("selection = %q %d", c.selNode, c.selEdge)
			return
		}
		if r, ok := c.scene.NodeRect(c.selNode); !ok || r.W <= 0 {
			t.Error("the selection has no rectangle")
		}
		off = c.sourceOffsetForSelection()
		if off < 0 || string([]rune(src)[off]) != "A" {
			t.Errorf("the selection's source offset = %d", off)
		}
		if line := sourceLineForOffset(src, off); line != 2 {
			t.Errorf("line = %d, want 2", line)
		}
		if e.Doc.Focused {
			t.Error("the caret is still in the text")
		}
	})

	// The keyboard moves the selection round the nodes.
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	s.Do(func() {
		if got := readCanvas(e, i).selNode; got != "B" {
			t.Errorf("after Tab, %q", got)
		}
	})
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	s.Do(func() {
		if got := readCanvas(e, i).selNode; got != "A" {
			t.Errorf("after Tab again, %q", got)
		}
	})
	s.Screen.KeyPress(unison.KeyEscape, mod.None)
	s.Do(func() {
		c := readCanvas(e, i)
		if c.hasSelection() {
			t.Error("Escape left the selection")
		}
		// A point to a source offset, and an offset to the element it is
		// in, which lights it up without selecting it.
		if got := c.scene.SourceOffsetAt(a); got != off {
			t.Errorf("offset at A = %d, want %d", got, off)
		}
		c.highlightSourceOffset(off)
		if c.hasSelection() || c.hlNode != "A" {
			t.Errorf("highlight = %q, selection %t", c.hlNode, c.hasSelection())
		}
	})

	// A new source: until it is rendered, the scene is not current, and it
	// cannot be copied as text.
	setText(s, e, i, "flowchart LR\n  A --> C")
	s.Do(func() {
		c := readCanvas(e, i)
		if c.sceneCurrent() && c.rendering {
			t.Error("the scene counts as current while the new source renders")
		}
	})
	waitRendered(s, e, i)
	s.Do(func() {
		c := readCanvas(e, i)
		if !c.sceneCurrent() || c.textDiagram() == "" {
			t.Error("the new scene is not current")
		}
	})
	// A source that does not render keeps the last scene, which is then
	// not current.
	setText(s, e, i, "gantt\n  oops")
	waitRendered(s, e, i)
	s.Do(func() {
		c := readCanvas(e, i)
		if !c.hasError || !c.hasScene || c.sceneCurrent() || c.textDiagram() != "" {
			t.Errorf("error %t, scene %t, current %t", c.hasError, c.hasScene, c.sceneCurrent())
		}
	})
}

func TestDiagramResetSceneDropsLastGood(t *testing.T) {
	good := "flowchart LR\nA[Start] --> B[End]"
	s, e := openEditor(t, mermaidNote(good))
	i := diagramIndex(e)
	waitRendered(s, e, i)

	// A source that is not Mermaid, and the canvas reset: the old scene is
	// not shown as its last valid one.
	setText(s, e, i, "┌──┐\n│ok│\n└──┘")
	s.Do(func() {
		c := readCanvas(e, i)
		c.resetScene()
		if c.hasScene || c.textDiagram() != "" {
			t.Error("the reset kept the scene")
		}
	})
	waitRendered(s, e, i)
	s.Do(func() {
		c := readCanvas(e, i)
		if !c.hasError || c.hasScene || c.sceneCurrent() {
			t.Errorf("error %t, scene %t, current %t", c.hasError, c.hasScene, c.sceneCurrent())
		}
	})

	// A reset with a valid source renders it again rather than leaving the
	// canvas empty.
	setText(s, e, i, good)
	waitRendered(s, e, i)
	s.Do(func() { readCanvas(e, i).resetScene() })
	s.Do(func() {
		if readCanvas(e, i).hasScene {
			t.Error("the reset kept the scene")
		}
	})
	waitRendered(s, e, i)
	s.Do(func() {
		if c := readCanvas(e, i); !c.hasScene || !c.sceneCurrent() {
			t.Error("the reset did not render again")
		}
	})

	// The Go counterpart of the bug: a block turned from a text diagram
	// into Mermaid must not show the drawing the block had before.
	s.Do(func() {
		e.Doc.SetCodeLanguage(e.Doc.Blocks[i].ID, "diagram")
		e.changed()
		e.Doc.Blocks[i].Text = "┌──┐\n│ok│\n└──┘"
		e.Doc.SetCodeLanguage(e.Doc.Blocks[i].ID, "mermaid")
		e.changed()
	})
	waitRendered(s, e, i)
	s.Do(func() {
		if c := readCanvas(e, i); c.hasScene {
			t.Error("the block shows the drawing it had before it was a text diagram")
		}
	})
}

func TestDiagramSavePNGWritesImage(t *testing.T) {
	s, e := openEditor(t, mermaidNote("flowchart LR\nA[Start] --> B[End]"))
	i := diagramIndex(e)
	waitRendered(s, e, i)
	path := filepath.Join(t.TempDir(), "diagram.png")
	s.Do(func() {
		id := e.Doc.Blocks[i].ID
		if err := e.saveDiagramPNG(id, path); err != nil {
			t.Error(err)
		}
		// An empty path, or a block without a drawing, refuses.
		if e.saveDiagramPNG(id, "") == nil {
			t.Error("an empty path was accepted")
		}
		if e.saveDiagramPNG(e.Doc.Blocks[i+1].ID, path+"2") == nil {
			t.Error("a block without a diagram was saved")
		}
	})
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	img, err := png.Decode(bytes.NewReader(data))
	if err != nil {
		t.Fatal(err)
	}
	if img.Bounds().Dx() <= 100 {
		t.Errorf("the picture is %d wide", img.Bounds().Dx())
	}
}

// fakeMath typesets every formula as a solid block, which is plainly not
// the formula's source drawn as text.
type fakeMath struct{}

func (fakeMath) Size(tex string, textSize float32) (diagram.Size, bool) {
	return diagram.Size{W: float64(textSize) * 4, H: float64(textSize) * 2.5}, !strings.Contains(tex, "}}")
}

func (fakeMath) Draw(gc *unison.Canvas, tex string, textSize float32, origin geom.Point, c unison.Color) bool {
	r := geom.NewRect(origin.X, origin.Y, textSize*4, textSize*2.5)
	gc.DrawRect(r, brush(c))
	return true
}

// inkOf counts the pixels of a PNG that differ from its corner.
func inkOf(t *testing.T, data []byte) (image.Image, int) {
	t.Helper()
	img, err := png.Decode(bytes.NewReader(data))
	if err != nil {
		t.Error(err)
		return image.NewRGBA(image.Rect(0, 0, 1, 1)), 0
	}
	bg := img.At(0, 0)
	n := 0
	for y := img.Bounds().Min.Y; y < img.Bounds().Max.Y; y++ {
		for x := img.Bounds().Min.X; x < img.Bounds().Max.X; x++ {
			if img.At(x, y) != bg {
				n++
			}
		}
	}
	return img, n
}

func TestDiagramPainterTypesetsRatherThanDrawingTheSource(t *testing.T) {
	s, e := openEditor(t, "Just a paragraph.\n")
	s.Do(func() {
		e.DiagramMath = fakeMath{}
		r := diagram.Render("flowchart LR\nA[\"$$\\frac{a}{b}$$\"]", e.diagramOptions())
		scene := r.Scene
		if len(scene.Texts) == 0 || scene.Texts[0].Tex == "" {
			t.Error("the label was not taken for a formula")
			return
		}
		typeset, ok := e.scenePNG(&scene, 1)
		if !ok {
			t.Error("no picture")
			return
		}
		imgA, inkA := inkOf(t, typeset)
		if inkA <= 50 {
			t.Errorf("the formula was not drawn: %d pixels", inkA)
		}
		// The same scene without the formula draws the label's source.
		asText := scene.Clone()
		for k := range asText.Texts {
			asText.Texts[k].Tex = ""
		}
		drawn, _ := e.scenePNG(&asText, 1)
		imgB, inkB := inkOf(t, drawn)
		if inkB <= 50 {
			t.Errorf("the source was not drawn: %d pixels", inkB)
		}
		if bytes.Equal(typeset, drawn) || sameImage(imgA, imgB) {
			t.Error("typesetting drew the same pixels as drawing $$...$$")
		}
	})
}

func sameImage(a, b image.Image) bool {
	if a.Bounds() != b.Bounds() {
		return false
	}
	for y := a.Bounds().Min.Y; y < a.Bounds().Max.Y; y++ {
		for x := a.Bounds().Min.X; x < a.Bounds().Max.X; x++ {
			if a.At(x, y) != b.At(x, y) {
				return false
			}
		}
	}
	return true
}

// ---- not ports ----

// With the caret elsewhere the block is its drawing; a press on empty
// space opens the source with the preview under it, and Ctrl+Enter leaves
// for a new paragraph, where the drawing comes back.
func TestDiagramSwitchesBetweenDrawingAndSource(t *testing.T) {
	src := "flowchart TD\n  A[Start] --> B[End]"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	waitRendered(s, e, i)
	var readH float32
	var empty diagram.Point
	s.Do(func() {
		if !e.diagramReads(i) {
			t.Error("the diagram is not drawn")
			return
		}
		readH = e.heights[i]
		// Right of the drawing, inside the panel.
		c := readCanvas(e, i)
		empty = diagram.Point{X: c.scene.Bounds.W - 2, Y: 4}
	})
	s.Screen.Click(onScreen(s, e, i, empty))
	s.Do(func() {
		if !e.diagramEditing(i) {
			t.Error("a press on empty space did not open the source")
			return
		}
		if off := e.Doc.Caret.Off; off != len([]rune(src)) {
			t.Errorf("the caret is at %d, want the end", off)
		}
	})
	s.WaitFor("the preview", func() bool { return e.diagramFor(&e.Doc.Blocks[i]).preview.hasScene })
	s.Do(func() {
		if e.heights[i] <= readH {
			t.Errorf("the source and preview (%g) are not taller than the drawing (%g)", e.heights[i], readH)
		}
		p := e.diagramPreviewParts(i)
		if p.panel.Y <= e.codePanel(i).Bottom() {
			t.Error("the preview is not under the source")
		}
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		if e.diagramEditing(i) || !e.diagramReads(i) {
			t.Error("Ctrl+Enter did not leave the block")
		}
		if b := e.Doc.CaretBlock(); b == nil || b.Kind != Paragraph || b.Text != "" || e.Doc.Index(b.ID) != i+1 {
			t.Error("Ctrl+Enter did not start a paragraph below")
		}
	})
}

// The preview follows the source a moment after typing stops, and keeps
// the last valid drawing while the source does not parse, saying so.
func TestDiagramPreviewKeepsTheLastValidDrawing(t *testing.T) {
	src := "flowchart LR\n  A --> B"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	s.Do(func() { e.FocusBlock(i, len([]rune(src))) })
	s.WaitFor("the preview", func() bool { return e.diagramFor(&e.Doc.Blocks[i]).preview.hasScene })
	s.Screen.Type("\n  -->")
	s.Do(func() {
		v := e.diagramFor(&e.Doc.Blocks[i])
		if v.previewSource == e.Doc.Blocks[i].Text {
			t.Error("the preview followed the typing at once")
		}
	})
	s.WaitFor("the preview to catch up", func() bool {
		v := e.diagramFor(&e.Doc.Blocks[i])
		return v.previewSource == e.Doc.Blocks[i].Text && !v.preview.rendering && v.preview.hasError
	})
	s.Do(func() {
		c := e.diagramFor(&e.Doc.Blocks[i]).preview
		if !c.hasScene {
			t.Error("the last valid drawing was dropped")
		}
		p := e.diagramPreviewParts(i)
		if len(p.notes) != 2 {
			t.Errorf("notes = %d, want the error and the last-valid note", len(p.notes))
			return
		}
		if msg := p.notes[0].Text(); !strings.HasPrefix(msg, fmt.Sprintf("line %d:", c.errorLine)) || c.errorLine <= 0 {
			t.Errorf("error note = %q", msg)
		}
		if msg := p.notes[1].Text(); msg != "Preview is from the last valid source" {
			t.Errorf("second note = %q", msg)
		}
	})
}

// hoverDiagram puts the pointer over block i's panel, which shows its
// controls, and returns the one with a part.
func hoverDiagram(s *uitest.Session, e *Editor, i int) {
	var at geom.Point
	s.Do(func() {
		p := e.diagramReadParts(i)
		at = geom.NewPoint(p.panel.X+4, p.panel.Bottom()-4)
	})
	s.Screen.MouseMove(s.Screen.PanelPoint(e, at), mod.None)
}

func clickChip(t *testing.T, s *uitest.Session, e *Editor, i int, part gutterPart) {
	t.Helper()
	hoverDiagram(s, e, i)
	var at geom.Point
	found := false
	s.Do(func() {
		chips, _ := e.diagramChips(i)
		for _, c := range chips {
			if c.part == part {
				at, found = c.rect.Center(), true
			}
		}
	})
	if !found {
		t.Fatalf("no control %d", part)
	}
	pt := s.Screen.PanelPoint(e, at)
	s.Screen.MouseMove(pt, mod.None)
	s.Screen.Click(pt)
}

// Zoom steps go on from the scale on screen, 100% is the diagram's own
// size, and a zoomed diagram larger than its panel scrolls.
func TestDiagramZoomAndPan(t *testing.T) {
	var b strings.Builder
	b.WriteString("flowchart LR\n")
	for k := range 12 {
		fmt.Fprintf(&b, "  N%d[A node with a long label %d] --> N%d[A node with a long label %d]\n", k, k, k+1, k+1)
	}
	s, e := openEditor(t, mermaidNote(strings.TrimSpace(b.String())))
	i := diagramIndex(e)
	waitRendered(s, e, i)
	var fit float32
	s.Do(func() {
		v := e.diagramFor(&e.Doc.Blocks[i])
		fit = e.readScale(v, e.diagramInnerWidth(i))
		if v.zoomed || fit >= 1 {
			t.Errorf("a wide diagram is not fitted: %g", fit)
		}
	})
	clickChip(t, s, e, i, partDiagramIn)
	s.Do(func() {
		v := e.diagramFor(&e.Doc.Blocks[i])
		if !v.zoomed || v.zoom != fit*diagramZoomStep {
			t.Errorf("zoom in: %t %g, want %g", v.zoomed, v.zoom, fit*diagramZoomStep)
		}
	})
	clickChip(t, s, e, i, partDiagram100)
	var window geom.Rect
	s.Do(func() {
		v := e.diagramFor(&e.Doc.Blocks[i])
		if !v.zoomed || v.zoom != 1 {
			t.Errorf("100%%: %t %g", v.zoomed, v.zoom)
		}
		window = e.diagramReadParts(i).window
	})
	// A drag across empty space scrolls the drawing (one on a node would
	// move the node), and the wheel scrolls it too.
	from := s.Screen.PanelPoint(e, geom.NewPoint(window.Center().X, window.Bottom()-4))
	s.Screen.Drag(from, from.Add(geom.NewPoint(-200, 0)), 4)
	s.Do(func() {
		v := e.diagramFor(&e.Doc.Blocks[i])
		if v.pan.X <= 0 {
			t.Errorf("the drag did not scroll: %v", v.pan)
		}
		if e.diagramEditing(i) {
			t.Error("a drag opened the source")
		}
	})
	var before geom.Point
	s.Do(func() { before = e.diagramFor(&e.Doc.Blocks[i]).pan })
	s.Screen.Wheel(from, geom.NewPoint(-1, 0), mod.None)
	s.Do(func() {
		if after := e.diagramFor(&e.Doc.Blocks[i]).pan; after.X <= before.X {
			t.Errorf("the wheel did not scroll: %v then %v", before, after)
		}
	})
	clickChip(t, s, e, i, partDiagramOut)
	s.Do(func() {
		if v := e.diagramFor(&e.Doc.Blocks[i]); v.zoom != 1/diagramZoomStep {
			t.Errorf("zoom out: %g", v.zoom)
		}
	})
	clickChip(t, s, e, i, partDiagramFit)
	s.Do(func() {
		v := e.diagramFor(&e.Doc.Blocks[i])
		if v.zoomed || v.pan != (geom.Point{}) {
			t.Errorf("fit: %t %v", v.zoomed, v.pan)
		}
	})
}

// Copy puts the source on the clipboard, and Copy as text the drawing in
// box-drawing characters; the PNG control hands over a picture twice the
// drawing's size.
func TestDiagramCopyCopyAsTextAndPNG(t *testing.T) {
	src := "flowchart LR\n  A[Start] --> B[End]"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	waitRendered(s, e, i)
	clickChip(t, s, e, i, partDiagramCopy)
	s.Do(func() {
		if got := unison.ClipboardGetText(); got != src {
			t.Errorf("Copy put %q on the clipboard", got)
		}
	})
	clickChip(t, s, e, i, partDiagramText)
	s.Do(func() {
		c := readCanvas(e, i)
		want := textdiagram.FromScene(&c.scene)
		if got := unison.ClipboardGetText(); got != want || !strings.Contains(got, "│ Start │") {
			t.Errorf("Copy as text put:\n%s", got)
		}
	})
	var saved []byte
	s.Do(func() { e.SaveDiagramPNG = func(p []byte) { saved = p } })
	clickChip(t, s, e, i, partDiagramPNG)
	img, err := png.Decode(bytes.NewReader(saved))
	if err != nil {
		t.Fatalf("PNG: %v", err)
	}
	s.Do(func() {
		want := int(readCanvas(e, i).scene.Bounds.W * 2)
		if w := img.Bounds().Dx(); w < want-1 || w > want+1 {
			t.Errorf("the picture is %d wide, want %d", w, want)
		}
	})
}

// A family Kvit does not draw, or a source with an error, shows why, and a
// reader is told the same; a drawn diagram is a picture named by its
// summary, with its title and description.
func TestDiagramTellsWhatItShows(t *testing.T) {
	src := "flowchart LR\n  accTitle: Release\n  accDescr: How a build ships\n  A --> B"
	s, e := openEditor(t, mermaidNote(src)+"\n```mermaid\ngantt\n  title Later\n```\n")
	i := diagramIndex(e)
	waitRendered(s, e, i)
	var gantt int
	s.Do(func() {
		for k := i + 1; k < len(e.Doc.Blocks); k++ {
			if isMermaid(&e.Doc.Blocks[k]) {
				gantt = k
			}
		}
	})
	waitRendered(s, e, gantt)
	s.Sync()
	pictures := s.Nodes(role.Image)
	var names []string
	for _, n := range pictures {
		names = append(names, n.Name+" / "+n.Description)
	}
	want := "Mermaid flowchart with 2 nodes and 1 connection / Release. How a build ships"
	if !containsString(names, want) {
		t.Errorf("no picture %q among %q", want, names)
	}
	found := false
	for _, n := range names {
		if strings.HasPrefix(n, "Unsupported Mermaid diagram type") {
			found = true
		}
	}
	if !found {
		t.Errorf("the gantt chart does not say it is unsupported: %q", names)
	}
	s.Do(func() {
		p := e.diagramReadParts(gantt)
		if p.note == nil || !strings.HasPrefix(p.note.Text(), "Unsupported") || p.panel.Height < e.px(diagramMinRead) {
			t.Error("the unsupported diagram's panel is blank")
		}
	})
}

func containsString(list []string, s string) bool {
	for _, x := range list {
		if x == s {
			return true
		}
	}
	return false
}

// A printed note draws its diagrams, which are laid out there and then
// rather than waited for.
func TestDiagramPrints(t *testing.T) {
	s, e := openEditor(t, mermaidNote("flowchart TD\n  A[Start] --> B[End]"))
	s.Do(func() {
		e.SetDoc(e.Doc)
		pages := e.Paginate(600, 800)
		i := diagramIndex(e)
		if c := readCanvas(e, i); !c.hasScene {
			t.Error("the diagram was not laid out for printing")
			return
		}
		img, err := unison.NewImageFromDrawing(600, int(pages[0].To-pages[0].From)+1, 72, func(gc *unison.Canvas) {
			e.fill(gc, geom.NewRect(0, 0, 600, 2000), e.tok().WindowBackground)
			e.DrawPage(gc, pages[0])
		})
		if err != nil {
			t.Error(err)
			return
		}
		data, err := img.ToPNG(6)
		if err != nil {
			t.Error(err)
			return
		}
		_, ink := inkOf(t, data)
		if ink < 500 {
			t.Errorf("the page has %d pixels of ink", ink)
		}
		e.Printing = false
	})
}

// Tab indents the source by two spaces, and Enter keeps the line's
// indentation up to the caret.
func TestDiagramSourceKeys(t *testing.T) {
	src := "flowchart LR\n  A --> B"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	s.Do(func() { e.FocusBlock(i, len([]rune(src))) })
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	s.Screen.Type("C")
	s.Do(func() {
		if got := e.Doc.Blocks[i].Text; got != src+"\n    C" {
			t.Errorf("text = %q", got)
		}
	})
}

// TestDiagramScreenshots writes, when KVIT_DIAGRAM_SHOTS names a folder,
// a flowchart and a sequence diagram as the editor draws them, the
// flowchart beside the app's picture of the same note.
func TestDiagramScreenshots(t *testing.T) {
	dir := os.Getenv("KVIT_DIAGRAM_SHOTS")
	if dir == "" {
		t.Skip("KVIT_DIAGRAM_SHOTS names no folder")
	}
	flow := "# Release pipeline\n\n```mermaid\nflowchart TD\n    A[Commit] --> B[CI build]\n    B --> C[Unit suite]\n" +
		"    B --> D[Packaging]\n    C --> E{Gates green?}\n    D --> E\n    E -->|yes| F[Draft release]\n    E -->|no| G[Fix and retag]\n```\n"
	seq := "# Saving a note\n\n```mermaid\nsequenceDiagram\n    autonumber\n    participant U as User\n    participant E as Editor\n" +
		"    participant S as Serializer\n    U->>E: type a heading\n    activate E\n    E->>S: block changed\n    S-->>E: markdown\n" +
		"    deactivate E\n    Note over S: debounced save\n```\n"
	for _, shot := range []struct{ name, md, qt string }{
		{"mermaid-flowchart", flow, filepath.Join(os.Getenv("HOME"), "kvit-notes/screenshots/press/mermaid-flowchart.png")},
		{"mermaid-sequence", seq, ""},
	} {
		t.Run(shot.name, func(t *testing.T) {
			var e *Editor
			s := uitest.Open(t, uitest.Options{Width: 780, Height: 760}, func(ui *kvitui.UI) unison.Paneler {
				e = New(ui, NewDoc(ParseMarkdown(shot.md)))
				return e
			})
			i := diagramIndex(e)
			waitRendered(s, e, i)
			s.Sync()
			img := s.Capture()
			out := img
			if shot.qt != "" {
				if qt, err := readImage(shot.qt); err == nil {
					out = beside(qt, img)
				}
			}
			f, err := os.Create(filepath.Join(dir, shot.name+".png"))
			if err != nil {
				t.Fatal(err)
			}
			defer f.Close()
			if err := png.Encode(f, out); err != nil {
				t.Fatal(err)
			}
		})
	}
}

func readImage(path string) (image.Image, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return png.Decode(f)
}

// beside is two pictures side by side on white, the first on the left.
func beside(a, b image.Image) image.Image {
	w := a.Bounds().Dx() + b.Bounds().Dx() + 16
	h := max(a.Bounds().Dy(), b.Bounds().Dy())
	out := image.NewRGBA(image.Rect(0, 0, w, h))
	draw.Draw(out, out.Bounds(), &image.Uniform{C: color.White}, image.Point{}, draw.Src)
	draw.Draw(out, a.Bounds().Sub(a.Bounds().Min), a, a.Bounds().Min, draw.Src)
	off := image.Pt(a.Bounds().Dx()+16, 0)
	draw.Draw(out, b.Bounds().Sub(b.Bounds().Min).Add(off), b, b.Bounds().Min, draw.Src)
	return out
}

// As code turns the diagram into a code block tagged plain, in one step
// that Ctrl+Z takes back.
func TestDiagramAsCode(t *testing.T) {
	src := "flowchart LR\n  A --> B"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	waitRendered(s, e, i)
	clickChip(t, s, e, i, partDiagramAsCode)
	s.Do(func() {
		b := &e.Doc.Blocks[i]
		if b.Kind != Code || b.Lang != "plain" || b.Text != src {
			t.Errorf("block = %v %q %q", b.Kind, b.Lang, b.Text)
		}
		if e.diagramReads(i) {
			t.Error("the block is still drawn as a diagram")
		}
	})
	s.Screen.KeyPress(unison.KeyZ, mod.Control)
	s.Do(func() {
		if b := &e.Doc.Blocks[i]; b.Lang != "mermaid" {
			t.Errorf("after undo the language is %q", b.Lang)
		}
	})
}

// Enter inside a line's indentation copies only the indentation before
// the caret, as Kvit's source editors do.
func TestDiagramEnterInsideIndentation(t *testing.T) {
	src := "flowchart LR\n    A --> B"
	s, e := openEditor(t, mermaidNote(src))
	i := diagramIndex(e)
	s.Do(func() { e.FocusBlock(i, len([]rune("flowchart LR\n  "))) })
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if got := e.Doc.Blocks[i].Text; got != "flowchart LR\n  \n    A --> B" {
			t.Errorf("text = %q", got)
		}
	})
}

// A selected element of a drawing is let go when the caret goes elsewhere,
// so the keys typed there are not taken for moving the selection.
func TestDiagramSelectionEndsWhenTheCaretMoves(t *testing.T) {
	s, e := openEditor(t, mermaidNote("flowchart LR\n  A[Start] --> B"))
	i := diagramIndex(e)
	waitRendered(s, e, i)
	var a diagram.Point
	s.Do(func() { a = centerOf(e, i, "A") })
	s.Screen.Click(onScreen(s, e, i, a))
	s.Do(func() {
		if readCanvas(e, i).selNode != "A" {
			t.Error("A was not selected")
		}
		e.FocusBlock(i+1, 0)
	})
	s.Screen.Type("x")
	s.Do(func() {
		if readCanvas(e, i).hasSelection() {
			t.Error("the selection outlived the caret moving away")
		}
		if got := e.Doc.Blocks[i+1].Text; got != "xAfter the diagram." {
			t.Errorf("typed into the paragraph: %q", got)
		}
	})
}

// Found on Windows in the dark theme: a subgraph's title, a label on a
// light fill the diagram's own style gives, and the lines dividing a class
// box must all show. Kvit's painter draws the title dark on dark, the label
// light on light, and the dividers under the box's fill.
func TestDiagramReadsInTheDarkTheme(t *testing.T) {
	var e *Editor
	s := uitest.Open(t, uitest.Options{Width: 700, Height: 700, Theme: tokens.Dark}, func(ui *kvitui.UI) unison.Paneler {
		e = New(ui, NewDoc(ParseMarkdown("Just a paragraph.\n")))
		return e
	})
	s.Do(func() {
		tk := e.tok()
		if r := palette.ContrastRatio(e.groupTitleColor(), tk.BlockHoverTint); r < 3 {
			t.Errorf("a group title has a contrast of %.2f on its group", r)
		}
		flow := diagram.Render("flowchart LR\n  A[Plain] --> B[Bright]\n  style B fill:#fde68a", e.diagramOptions()).Scene
		for _, tx := range flow.Texts {
			col, _ := e.roleColor(tx.Role)
			got := labelOnFill(&flow, tx, col)
			switch tx.Text {
			case "Plain":
				if got != tk.TextPrimary {
					t.Errorf("a label on the theme's fill changed colour")
				}
			case "Bright":
				if r := palette.ContrastRatio(got, palette.RGB8(0xfd, 0xe6, 0x8a)); r < 4.5 {
					t.Errorf("the label on a light fill has a contrast of %.2f", r)
				}
			}
		}
		class := diagram.Render("classDiagram\n  class Block {\n    +int size\n    +draw()\n  }", e.diagramOptions()).Scene
		data, ok := e.scenePNG(&class, 1)
		if !ok {
			t.Error("no picture")
			return
		}
		img, err := png.Decode(bytes.NewReader(data))
		if err != nil {
			t.Error(err)
			return
		}
		box := class.Shapes[0].Rect
		fill := img.At(int(box.X+4), int(box.Y+4))
		seen := 0
		for _, p := range class.Paths {
			if !isDivider(p) {
				continue
			}
			y := int(p.Outline.Bounds().Y)
			if img.At(int(box.X+box.W/2), y) != fill {
				seen++
			}
		}
		if seen != 2 {
			t.Errorf("%d of the class box's two dividers show over its fill", seen)
		}
	})
}
