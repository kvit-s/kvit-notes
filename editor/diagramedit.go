package editor

// Editing a Mermaid diagram on its drawing. Every gesture becomes one edit of
// the fence's source through package mermaid's edits, applied as one undo
// step, so everything outside the edited statement stays as written; a
// gesture whose result would not parse is refused, and the status line says
// why.
//
// A press on a shape or a line selects it. Then:
//   - on a flowchart: dragging a node moves it, on an 8-pixel grid with
//     guides to the nodes it lines up with, and writes every node's place
//     into a `%% mermaid-flow:pos` line, which switches the diagram to the
//     arranged layout (Reset layout takes the line out again); a
//     double-click, F2 or Enter edits a node's label; the context menu
//     renames a node, changes its shape or colour, restyles an edge, adds a
//     connected node, or deletes; Delete deletes; and the four dots on the
//     selected node's sides draw a new edge when dragged onto another node,
//     or add a connected node when pressed;
//   - on a sequence diagram: Ctrl+Up and Ctrl+Down move the selected message,
//     Ctrl+Left and Ctrl+Right the selected participant, and so does dragging
//     one a position's worth, or the context menu.
// Gestures act only while the drawing shown is the current source's.

import (
	"math"
	"slices"
	"strings"

	"github.com/kvit-s/kvit-notes/diagram"
	"github.com/kvit-s/kvit-notes/mermaid"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// supportsArrangement reports whether the canvas is a flowchart whose
// drawing is the current source's, which is what the flowchart gestures
// need.
func (c *diagramCanvas) supportsArrangement() bool {
	return c.family == mermaid.Flowchart && c.sceneCurrent()
}

// supportsSequenceReorder reports the same for a sequence diagram.
func (c *diagramCanvas) supportsSequenceReorder() bool {
	return c.family == mermaid.Sequence && c.sceneCurrent()
}

// applyGesture writes a gesture's edit into diagram block i as one undo
// step and says done, or says why the gesture was refused. It reports
// whether the source changed.
func (e *Editor) applyGesture(i int, r mermaid.EditResult, done string) bool {
	if e.Doc.ReadOnly {
		return false
	}
	b := &e.Doc.Blocks[i]
	if !r.OK {
		if r.Error != "" {
			e.status(r.Error)
		}
		return false
	}
	if r.Source == b.Text {
		return false
	}
	id := b.ID
	e.Doc.Edit("diagram", func() { e.Doc.Block(id).Text = r.Source })
	if done != "" {
		e.status(done)
	}
	e.changed()
	return true
}

// diagramPan is a press on a diagram's drawing, until it is let go. It may
// stay a click, which selects, or become a drag: of a node on a flowchart,
// of a new edge from an anchor of the selected node, of a participant or a
// message on a sequence diagram, or else of a zoomed drawing's scroll.
type diagramPan struct {
	block  int64
	start  geom.Point // where the press was, in the editor
	from   geom.Point // the drawing's scroll then
	node   string     // the node pressed, "" for none
	edge   int        // the edge pressed, -1 for none
	anchor bool       // pressed on an anchor of the selected node
	moved  bool       // the drawing scrolled

	// Dragging a node: where it was, where the press held it, where it
	// would go, and the guides it lines up with, in scene coordinates.
	dragging     bool
	grab, origin diagram.Point
	center       diagram.Point
	guideXs      []float64
	guideYs      []float64

	// Drawing an edge: the pointer, and the node it would connect to.
	linking bool
	pointer diagram.Point
	target  string
}

// Node dragging in scene pixels: the grid a dragged node snaps to, and how
// close it must come to another node's centre line to line up with it.
const (
	dragGrid  = 8
	dragGuide = 6
	// How far a participant or a message is dragged to move it one place,
	// in editor pixels.
	reorderAcross = 30
	reorderDown   = 24
	// The anchors on a selected node's sides, and the room around one a
	// press still hits.
	anchorSize   = 12
	anchorBorder = 2
	anchorReach  = 5
)

// diagramClick is a press on one of a diagram block's parts.
func (e *Editor) diagramClick(i int, part gutterPart, where geom.Point, clicks int) {
	switch part {
	case partDiagramCanvas:
		e.diagramPress(i, where, clicks)
	case partDiagramPreview:
		e.previewPress(i, where)
	default:
		e.diagramAct(i, part)
	}
}

// diagramPress starts a press on diagram block i's drawing. A double-click
// on a flowchart's node edits its label at once.
func (e *Editor) diagramPress(i int, where geom.Point, clicks int) {
	b := &e.Doc.Blocks[i]
	v := e.diagramFor(b)
	c := v.read
	pan := &diagramPan{block: b.ID, start: where, from: v.pan, edge: -1}
	if c.sceneCurrent() && where.In(e.diagramReadParts(i).window) {
		for _, a := range e.diagramAnchors(i) {
			if where.In(a.Inset(geom.NewUniformInsets(-e.px(anchorReach)))) {
				pan.anchor = true
			}
		}
		if !pan.anchor {
			pt := e.canvasPoint(i, where)
			pan.node = c.scene.NodeAt(pt)
			pan.edge = c.scene.EdgeAt(pt)
			if clicks == 2 && pan.node != "" && c.supportsArrangement() && !e.Doc.ReadOnly {
				e.selectDiagramElement(i, pan.node, -1)
				e.openLabelEditor(i, false)
				return
			}
		}
	}
	e.diagPan = pan
}

// diagramDrag follows the pointer while a diagram is pressed.
func (e *Editor) diagramDrag(where geom.Point) {
	pan := e.diagPan
	i := e.Doc.Index(pan.block)
	if i < 0 || !e.diagramReads(i) {
		return
	}
	v := e.diagramFor(&e.Doc.Blocks[i])
	c := v.read
	d := where.Sub(pan.start)
	limit := e.px(4)
	far := d.X*d.X+d.Y*d.Y > limit*limit
	switch {
	case pan.anchor:
		if !pan.linking && far && c.supportsArrangement() && c.selNode != "" && !e.Doc.ReadOnly {
			pan.linking = true
		}
		if pan.linking {
			pan.pointer = e.canvasPoint(i, where)
			pan.target = ""
			if t := c.scene.NodeAt(pan.pointer); t != c.selNode {
				pan.target = t
			}
			e.MarkForRedraw()
		}
	case pan.node != "":
		if !pan.dragging && far && c.supportsArrangement() && !e.Doc.ReadOnly {
			r, ok := c.scene.NodeRect(pan.node)
			if ok {
				pan.dragging = true
				pan.grab = e.canvasPoint(i, pan.start)
				pan.origin = diagram.Point{X: r.X + r.W/2, Y: r.Y + r.H/2}
				pan.center = pan.origin
			}
		}
		if pan.dragging {
			e.dragNodeTo(c, pan, e.canvasPoint(i, where))
			e.MarkForRedraw()
		}
	default:
		// Without a node under the press, a drag scrolls a zoomed drawing.
		limit := e.px(dragThreshold)
		if !pan.moved && d.X*d.X+d.Y*d.Y <= limit*limit {
			return
		}
		if !v.zoomed {
			// A fitted diagram does not scroll, so the press still acts.
			return
		}
		pan.moved = true
		v.pan = pan.from.Sub(d)
		e.clampPan(i)
		e.MarkForRedraw()
	}
}

// dragNodeTo moves a dragged node's place to follow the pointer: on the
// grid, and then onto the centre line of any node it comes near, noting
// that line as a guide.
func (e *Editor) dragNodeTo(c *diagramCanvas, pan *diagramPan, pointer diagram.Point) {
	x := pan.origin.X + pointer.X - pan.grab.X
	y := pan.origin.Y + pointer.Y - pan.grab.Y
	x = math.Round(x/dragGrid) * dragGrid
	y = math.Round(y/dragGrid) * dragGrid
	pan.guideXs, pan.guideYs = pan.guideXs[:0], pan.guideYs[:0]
	for _, sh := range c.scene.Shapes {
		if sh.NodeID == "" || sh.NodeID == pan.node {
			continue
		}
		other := sh.Rect.Center()
		if math.Abs(other.X-x) < dragGuide {
			x = other.X
			if !slices.Contains(pan.guideXs, other.X) {
				pan.guideXs = append(pan.guideXs, other.X)
			}
		}
		if math.Abs(other.Y-y) < dragGuide {
			y = other.Y
			if !slices.Contains(pan.guideYs, other.Y) {
				pan.guideYs = append(pan.guideYs, other.Y)
			}
		}
	}
	pan.center = diagram.Point{X: x, Y: y}
}

// diagramRelease ends a press on a diagram.
func (e *Editor) diagramRelease(where geom.Point) {
	pan := e.diagPan
	e.diagPan = nil
	i := e.Doc.Index(pan.block)
	if i < 0 || !e.diagramReads(i) {
		return
	}
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	switch {
	case pan.anchor:
		if !c.supportsArrangement() || c.selNode == "" {
			return
		}
		from := c.selNode
		if pan.linking {
			if pan.target != "" {
				e.applyGesture(i, mermaid.InsertEdge(b.Text, from, pan.target), "Connected")
			}
			return
		}
		e.applyGesture(i, mermaid.QuickAddNode(b.Text, from), "Added a connected node")
		return
	case pan.dragging:
		e.finishNodeDrag(i, pan)
		return
	case pan.moved:
		return
	}
	p := e.diagramReadParts(i)
	if !c.sceneCurrent() || !where.In(p.window) {
		c.clearSelection()
		e.editDiagramSource(i, -1)
		return
	}
	// On a sequence diagram, a participant dragged sideways or a message
	// dragged up or down moves one place.
	if c.supportsSequenceReorder() {
		d := where.Sub(pan.start)
		dx, dy := float64(d.X), float64(d.Y)
		if pan.node != "" && math.Abs(dx) > float64(e.px(reorderAcross)) && math.Abs(dx) > math.Abs(dy) {
			e.selectDiagramElement(i, pan.node, -1)
			e.moveParticipant(i, sign(dx), "Moved "+pan.node)
			return
		}
		if pan.edge >= 0 && math.Abs(dy) > float64(e.px(reorderDown)) && math.Abs(dy) > math.Abs(dx) {
			e.selectDiagramElement(i, "", pan.edge)
			e.moveMessage(i, sign(dy), "Moved message")
			return
		}
	}
	pt := e.canvasPoint(i, where)
	if id := c.scene.NodeAt(pt); id != "" {
		e.selectDiagramElement(i, id, -1)
		return
	}
	if edge := c.scene.EdgeAt(pt); edge >= 0 {
		e.selectDiagramElement(i, "", edge)
		return
	}
	c.clearSelection()
	e.editDiagramSource(i, -1)
}

func sign(v float64) int {
	if v < 0 {
		return -1
	}
	return 1
}

// finishNodeDrag writes where every node is, the dragged one where it was
// let go, into the pos line, in the order the nodes are drawn. A drag that
// ends where it began writes nothing.
func (e *Editor) finishNodeDrag(i int, pan *diagramPan) {
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	if !c.supportsArrangement() || pan.center == pan.origin {
		return
	}
	var positions []mermaid.NodePosition
	seen := map[string]bool{}
	for _, sh := range c.scene.Shapes {
		if sh.NodeID == "" || seen[sh.NodeID] {
			continue
		}
		seen[sh.NodeID] = true
		at := sh.Rect.Center()
		if sh.NodeID == pan.node {
			at = pan.center
		}
		positions = append(positions, mermaid.NodePosition{ID: sh.NodeID, X: at.X, Y: at.Y})
	}
	if e.applyGesture(i, mermaid.WriteArrangement(b.Text, positions), "Arranged "+pan.node) {
		unison.AnnounceForAccessibility("Moved " + pan.node)
	}
}

// ---- the selected node's anchors ----

// sceneToEditor is where a point of diagram block i's scene is in the
// editor.
func (e *Editor) sceneToEditor(i int, p diagram.Point) geom.Point {
	v := e.diagramFor(&e.Doc.Blocks[i])
	parts := e.diagramReadParts(i)
	return geom.NewPoint(parts.window.X-v.pan.X+float32(p.X)*parts.scale, parts.window.Y-v.pan.Y+float32(p.Y)*parts.scale)
}

// selectionRectOnScreen is the selected node's rectangle in the editor, and
// false without a selected node.
func (e *Editor) selectionRectOnScreen(i int) (geom.Rect, bool) {
	c := e.diagramFor(&e.Doc.Blocks[i]).read
	if c.selNode == "" {
		return geom.Rect{}, false
	}
	r, ok := c.scene.NodeRect(c.selNode)
	if !ok {
		return geom.Rect{}, false
	}
	tl := e.sceneToEditor(i, diagram.Point{X: r.X, Y: r.Y})
	br := e.sceneToEditor(i, diagram.Point{X: r.Right(), Y: r.Bottom()})
	return geom.NewRect(tl.X, tl.Y, br.X-tl.X, br.Y-tl.Y), true
}

// diagramAnchors are the four dots on the selected node's sides (top,
// right, bottom, left) of a flowchart whose drawing is current, while
// nothing is being dragged.
func (e *Editor) diagramAnchors(i int) []geom.Rect {
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	if !c.supportsArrangement() || e.Doc.ReadOnly || (e.diagPan != nil && e.diagPan.block == b.ID && e.diagPan.dragging) {
		return nil
	}
	r, ok := e.selectionRectOnScreen(i)
	if !ok {
		return nil
	}
	s := e.px(anchorSize)
	c0 := r.Center()
	var out []geom.Rect
	for _, p := range []geom.Point{{X: c0.X, Y: r.Y}, {X: r.Right(), Y: c0.Y}, {X: c0.X, Y: r.Bottom()}, {X: r.X, Y: c0.Y}} {
		out = append(out, geom.NewRect(p.X-s/2, p.Y-s/2, s, s))
	}
	return out
}

// ---- keys ----

// diagramKey is a key pressed while a diagram's element is selected: Tab
// and the arrows move the selection from node to node; with Ctrl, the arrows
// move a sequence diagram's selected message or participant; Escape lets
// the selection go; Enter or F2 edits a flowchart node's label, or opens the
// source at the element; Delete removes a flowchart's selected element; and
// the menu key opens the context menu.
func (e *Editor) diagramKey(key unison.KeyCode, ctrl, shift bool) bool {
	i := e.diagramSelection()
	if i < 0 {
		return false
	}
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	if ctrl {
		if !c.supportsSequenceReorder() {
			return false
		}
		switch key {
		case unison.KeyUp, unison.KeyDown:
			e.moveMessage(i, direction(key), "Moved")
		case unison.KeyLeft, unison.KeyRight:
			delta := 1
			if key == unison.KeyLeft {
				delta = -1
			}
			e.moveParticipant(i, delta, "Moved")
		default:
			return false
		}
		return true
	}
	switch key {
	case unison.KeyEscape:
		c.clearSelection()
		e.blockSel[b.ID] = true
		e.blockAnchor = b.ID
	case unison.KeyTab, unison.KeyRight, unison.KeyDown, unison.KeyLeft, unison.KeyUp:
		back := key == unison.KeyLeft || key == unison.KeyUp || (key == unison.KeyTab && shift)
		delta := 1
		if back {
			delta = -1
		}
		c.cycleNode(delta)
		e.announceDiagramSelection(i)
	case unison.KeyReturn, unison.KeyF2:
		if c.selNode != "" && c.supportsArrangement() && !e.Doc.ReadOnly {
			e.openLabelEditor(i, false)
			return true
		}
		off := c.sourceOffsetForSelection()
		c.clearSelection()
		e.editDiagramSource(i, off)
	case unison.KeyDelete, unison.KeyBackspace:
		e.deleteDiagramSelection(i)
	case unison.KeyMenu:
		e.openDiagramMenu(i, e.diagramMenuAnchor(i))
	case unison.KeyF10:
		if !shift {
			return false
		}
		e.openDiagramMenu(i, e.diagramMenuAnchor(i))
	default:
		return false
	}
	return true
}

// ---- gestures ----

// deleteDiagramSelection deletes the selected node, with every edge that
// touches it, or the selected edge, of a flowchart.
func (e *Editor) deleteDiagramSelection(i int) {
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	if !c.supportsArrangement() {
		return
	}
	var r mermaid.EditResult
	switch {
	case c.selNode != "":
		r = mermaid.DeleteNode(b.Text, c.selNode)
	case c.selEdge >= 0:
		r = mermaid.DeleteEdge(b.Text, c.selEdge)
	default:
		return
	}
	if e.applyGesture(i, r, "Deleted") {
		// The indexes of the edges after a deleted one move up, so an edge
		// selection would now name another edge.
		c.clearSelection()
	}
}

// moveMessage moves a sequence diagram's selected message one place up or
// down, and keeps it selected where it went.
func (e *Editor) moveMessage(i int, delta int, done string) {
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	if !c.supportsSequenceReorder() || c.selEdge < 0 {
		return
	}
	next := neighbourMessage(b.Text, c.selEdge, delta)
	if e.applyGesture(i, mermaid.MoveSequenceMessage(b.Text, c.selEdge, delta), done) && next >= 0 {
		c.setSelectedEdge(next)
	}
}

// neighbourMessage is the event index of the message delta places from the
// message at event index at, which is where a moved message ends up, or -1.
func neighbourMessage(src string, at, delta int) int {
	pr := mermaid.Parse(src)
	var messages []int
	for k, ev := range pr.Sequence.Events {
		if ev.Kind == mermaid.EventMessage {
			messages = append(messages, k)
		}
	}
	pos := slices.Index(messages, at)
	if pos < 0 || pos+delta < 0 || pos+delta >= len(messages) {
		return -1
	}
	return messages[pos+delta]
}

// moveParticipant moves a sequence diagram's selected participant one place
// left or right.
func (e *Editor) moveParticipant(i int, delta int, done string) {
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	if !c.supportsSequenceReorder() || c.selNode == "" {
		return
	}
	e.applyGesture(i, mermaid.MoveSequenceParticipant(b.Text, c.selNode, delta), done)
}

// resetArrangement takes the pos line out, so the diagram is laid out on its
// own again.
func (e *Editor) resetArrangement(i int) {
	b := &e.Doc.Blocks[i]
	r := mermaid.ResetArrangement(b.Text)
	if r.OK && r.Source != b.Text {
		e.applyGesture(i, r, "")
	}
}

// selectedNodeLabel is the selected node's label as its source gives it.
func (e *Editor) selectedNodeLabel(i int) string {
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	pr := mermaid.Parse(b.Text)
	if k := pr.Flowchart.IndexOfNode(c.selNode); k >= 0 {
		return pr.Flowchart.Nodes[k].Label
	}
	return ""
}

// ---- the label editor ----

// openLabelEditor edits the selected node's label, or with rename its id,
// in a field over the node. Enter applies it and closes the field, unless
// the edit is refused; Escape and a press elsewhere close it unchanged.
func (e *Editor) openLabelEditor(i int, rename bool) {
	w := e.ui.WindowOf(e)
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	if w == nil || e.Doc.ReadOnly || c.selNode == "" || !c.supportsArrangement() {
		return
	}
	id, node := b.ID, c.selNode
	field := kvitui.NewField(e.ui)
	field.Label = "Node label"
	field.Placeholder = "Label"
	field.SetText(e.selectedNodeLabel(i))
	if rename {
		field.Label, field.Placeholder = "Node id", "Id"
		field.SetText(node)
	}
	var hide func()
	closeField := func() {
		if hide != nil {
			hide()
			hide = nil
		}
		e.RequestFocus()
		e.changed()
	}
	apply := func() {
		k := e.Doc.Index(id)
		if k < 0 || !e.diagramReads(k) {
			closeField()
			return
		}
		src := e.Doc.Blocks[k].Text
		value := field.Text()
		var r mermaid.EditResult
		done := "Label updated"
		if rename {
			r, done = mermaid.RenameNode(src, node, strings.TrimSpace(value)), "Renamed"
		} else {
			r = mermaid.SetNodeLabel(src, node, value)
		}
		if r.OK && r.Source == src {
			closeField()
			return
		}
		if e.applyGesture(k, r, done) {
			if rename {
				// The selection follows the node to its new id.
				e.diagramFor(&e.Doc.Blocks[k]).read.setSelectedNode(strings.TrimSpace(value))
			}
			closeField()
		}
	}
	edit := field.Edit()
	keys := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		if key == unison.KeyReturn || key == unison.KeyNumPadEnter {
			apply()
			return true
		}
		return keys != nil && keys(key, mods, repeat)
	}
	r, _ := e.selectionRectOnScreen(i)
	at := e.RectToRoot(r)
	width := e.px(220)
	body := e.RectToRoot(e.bodyRect(i))
	hide = w.Show(&kvitui.Popup{Panel: field, Modal: false, Anchor: e,
		OnEscape:       closeField,
		OnPressOutside: closeField,
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			x := max(body.X, min(at.X, body.Right()-width))
			return geom.NewRect(x, at.Y+(at.Height-size.Height)/2, width, size.Height)
		}})
	unison.InvokeTask(func() { field.Focus(); field.Edit().SelectAll() })
}

// ---- the context menu ----

// diagramMenuAnchor is where the context menu opens from the keyboard: at
// the selected node, or at the drawing's top left.
func (e *Editor) diagramMenuAnchor(i int) geom.Rect {
	if r, ok := e.selectionRectOnScreen(i); ok {
		return r
	}
	p := e.diagramReadParts(i).window
	return geom.NewRect(p.X, p.Y, 0, 0)
}

// The shapes the context menu offers, and the colours:
// each colour a light fill and a darker stroke, written into the source.
var (
	diagramShapes = []struct {
		label string
		shape mermaid.NodeShape
	}{
		{"&Rectangle", mermaid.ShapeRect}, {"Rou&nded", mermaid.ShapeRoundRect}, {"&Stadium", mermaid.ShapeStadium},
		{"Su&broutine", mermaid.ShapeSubroutine}, {"C&ylinder", mermaid.ShapeCylinder}, {"&Circle", mermaid.ShapeCircle},
		{"&Decision", mermaid.ShapeRhombus}, {"&Hexagon", mermaid.ShapeHexagon},
		{"&Parallelogram", mermaid.ShapeParallelogram}, {"&Trapezoid", mermaid.ShapeTrapezoid},
	}
	diagramColors = []struct{ label, fill, stroke string }{
		{"&Red", "#fecaca", "#dc2626"}, {"&Orange", "#fed7aa", "#ea580c"}, {"&Yellow", "#fef08a", "#ca8a04"},
		{"&Green", "#bbf7d0", "#16a34a"}, {"&Blue", "#bfdbfe", "#2563eb"}, {"&Purple", "#e9d5ff", "#9333ea"},
		{"Gr&ay", "#e5e7eb", "#4b5563"},
	}
	diagramStrokes = []struct {
		label  string
		stroke mermaid.EdgeStroke
	}{{"&Solid", mermaid.StrokeSolid}, {"&Dotted", mermaid.StrokeDotted}, {"&Thick", mermaid.StrokeThick}}
)

// diagramMenuItems are the context menu of diagram block i for its
// selection.
func (e *Editor) diagramMenuItems(i int) []kvitui.MenuItem {
	b := &e.Doc.Blocks[i]
	id := b.ID
	c := e.diagramFor(b).read
	flow, seq := c.supportsArrangement(), c.supportsSequenceReorder()
	node, edge := c.selNode != "", c.selEdge >= 0
	// Every entry looks its block up again when chosen, since the menu stays
	// open while other things can change the note.
	act := func(f func(k int)) func() {
		return func() {
			if k := e.Doc.Index(id); k >= 0 && e.diagramReads(k) {
				f(k)
				e.changed()
			}
		}
	}
	gesture := func(edit func(src string, c *diagramCanvas) mermaid.EditResult, done string) func() {
		return act(func(k int) {
			blk := &e.Doc.Blocks[k]
			e.applyGesture(k, edit(blk.Text, e.diagramFor(blk).read), done)
		})
	}
	var items []kvitui.MenuItem
	if flow {
		items = append(items,
			kvitui.MenuItem{Text: "&Rename id…", Disabled: !node, OnSelect: act(func(k int) { e.openLabelEditor(k, true) })},
			kvitui.MenuItem{Text: "Edit &label…", Disabled: !node, OnSelect: act(func(k int) { e.openLabelEditor(k, false) })})
	}
	if seq {
		items = append(items,
			kvitui.MenuItem{Text: "Move message &up", Disabled: !edge, OnSelect: act(func(k int) { e.moveMessage(k, -1, "Moved up") })},
			kvitui.MenuItem{Text: "Move message dow&n", Disabled: !edge, OnSelect: act(func(k int) { e.moveMessage(k, 1, "Moved down") })},
			kvitui.MenuItem{Text: "Move participant le&ft", Disabled: !node, OnSelect: act(func(k int) { e.moveParticipant(k, -1, "Moved left") })},
			kvitui.MenuItem{Text: "Move participant ri&ght", Disabled: !node, OnSelect: act(func(k int) { e.moveParticipant(k, 1, "Moved right") })})
	}
	if flow {
		var shapes, strokes, colors []kvitui.MenuItem
		for _, s := range diagramShapes {
			shapes = append(shapes, kvitui.MenuItem{Text: s.label, OnSelect: gesture(func(src string, c *diagramCanvas) mermaid.EditResult {
				return mermaid.SetNodeShape(src, c.selNode, s.shape)
			}, "Shape changed")})
		}
		for _, s := range diagramStrokes {
			strokes = append(strokes, kvitui.MenuItem{Text: s.label, OnSelect: gesture(func(src string, c *diagramCanvas) mermaid.EditResult {
				return mermaid.SetEdgeStroke(src, c.selEdge, s.stroke)
			}, "Edge restyled")})
		}
		for _, col := range diagramColors {
			colors = append(colors, kvitui.MenuItem{Text: col.label, OnSelect: gesture(func(src string, c *diagramCanvas) mermaid.EditResult {
				return mermaid.SetNodeStyle(src, c.selNode, mermaid.ParseColor(col.fill), mermaid.ParseColor(col.stroke))
			}, "Color applied")})
		}
		items = append(items,
			kvitui.MenuItem{Text: "&Shape", Disabled: !node, Items: shapes},
			kvitui.MenuItem{Text: "Edge st&yle", Disabled: !edge, Items: strokes},
			kvitui.MenuItem{Text: "&Color", Disabled: !node, Items: colors},
			kvitui.MenuItem{Text: "&Add connected node", Disabled: !node, OnSelect: gesture(func(src string, c *diagramCanvas) mermaid.EditResult {
				return mermaid.QuickAddNode(src, c.selNode)
			}, "Added a connected node")},
			kvitui.MenuItem{Text: "&Delete", Disabled: !node && !edge, OnSelect: act(e.deleteDiagramSelection)},
			kvitui.MenuItem{Separator: true},
			kvitui.MenuItem{Text: "Reset layou&t", Disabled: !c.hasArrangement, OnSelect: act(e.resetArrangement)})
	}
	items = append(items, kvitui.MenuItem{Text: "&Edit source", OnSelect: act(func(k int) {
		cc := e.diagramFor(&e.Doc.Blocks[k]).read
		off := cc.sourceOffsetForSelection()
		cc.clearSelection()
		e.editDiagramSource(k, off)
	})})
	return items
}

// openDiagramMenu opens diagram block i's context menu at a place.
func (e *Editor) openDiagramMenu(i int, at geom.Rect) {
	if e.Doc.ReadOnly {
		return
	}
	e.ui.ShowMenuAt(e, at, "Diagram", e.diagramMenuItems(i))
}

// diagramRightClick is a right-click on diagram block i: on a drawing that
// is the current source's it selects the element under the pointer and
// opens the context menu, and reports false otherwise, for the block menu.
func (e *Editor) diagramRightClick(i int, where geom.Point) bool {
	if !e.diagramReads(i) || e.Doc.ReadOnly {
		return false
	}
	c := e.diagramFor(&e.Doc.Blocks[i]).read
	if !c.sceneCurrent() || !where.In(e.diagramReadParts(i).window) {
		return false
	}
	pt := e.canvasPoint(i, where)
	if id := c.scene.NodeAt(pt); id != "" {
		e.selectDiagramElement(i, id, -1)
	} else if edge := c.scene.EdgeAt(pt); edge >= 0 {
		e.selectDiagramElement(i, "", edge)
	}
	e.openDiagramMenu(i, geom.NewRect(where.X, where.Y, 0, 0))
	return true
}

// ---- drawing what a gesture shows ----

// drawDiagramGestures draws, in diagram block i's scene coordinates, the
// dragged node's ghost and guides, or the edge being drawn and the node it
// would connect to.
func (e *Editor) drawDiagramGestures(gc *unison.Canvas, i int) {
	pan := e.diagPan
	if pan == nil || pan.block != e.Doc.Blocks[i].ID {
		return
	}
	c := e.diagramFor(&e.Doc.Blocks[i]).read
	ring := kvitui.Color(e.tok().FocusRing)
	switch {
	case pan.dragging:
		b := c.scene.Bounds
		guide := pen(ring.SetAlphaIntensity(110.0/255), 1, diagram.LineDashed, false)
		for _, x := range pan.guideXs {
			gc.DrawLine(geom.NewPoint(float32(x), 0), geom.NewPoint(float32(x), float32(b.H)), guide)
		}
		for _, y := range pan.guideYs {
			gc.DrawLine(geom.NewPoint(0, float32(y)), geom.NewPoint(float32(b.W), float32(y)), guide)
		}
		if r, ok := c.scene.NodeRect(pan.node); ok {
			ghost := geom.NewRect(float32(pan.center.X-r.W/2), float32(pan.center.Y-r.H/2), float32(r.W), float32(r.H))
			gc.DrawRoundedRect(ghost, geom.NewSize(6, 6), brush(ring.SetAlphaIntensity(30.0/255)))
			gc.DrawRoundedRect(ghost, geom.NewSize(6, 6), pen(ring.SetAlphaIntensity(200.0/255), 2, diagram.LineDashed, false))
		}
	case pan.linking:
		if r, ok := c.scene.NodeRect(c.selNode); ok {
			from := geom.NewPoint(float32(r.X+r.W/2), float32(r.Y+r.H/2))
			gc.DrawLine(from, pt(pan.pointer), pen(ring.SetAlphaIntensity(190.0/255), 2, diagram.LineDashed, true))
		}
		if r, ok := c.scene.NodeRect(pan.target); ok && pan.target != "" {
			box := rect(r).Inset(geom.NewUniformInsets(-3))
			gc.DrawRoundedRect(box, geom.NewSize(5, 5), pen(ring.SetAlphaIntensity(220.0/255), 2.2, diagram.LineSolid, false))
		}
	}
}

// drawDiagramAnchors draws the four dots on the selected node's sides.
func (e *Editor) drawDiagramAnchors(gc *unison.Canvas, i int) {
	t := e.tok()
	for _, a := range e.diagramAnchors(i) {
		gc.DrawOval(a, brush(kvitui.Color(t.Accent)))
		border := e.px(anchorBorder)
		gc.DrawOval(a.Inset(geom.NewUniformInsets(border/2)), pen(kvitui.Color(t.PanelBackground), border, diagram.LineSolid, false))
	}
}

// diagramDragging reports whether a node of block id is being dragged or an
// edge drawn from it, for tests and the pointer's shape.
func (e *Editor) diagramDragging(id int64) bool {
	return e.diagPan != nil && e.diagPan.block == id && (e.diagPan.dragging || e.diagPan.linking)
}
