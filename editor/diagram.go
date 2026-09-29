package editor

// Mermaid diagrams (features.md 1.2.17, Kvit's qml/DiagramBlock and
// src/content/diagrams/diagramcanvas.cpp): a code block whose language is
// `mermaid` keeps its source as its text, and is drawn as the diagram the
// source describes.
//
// With the caret elsewhere the block shows the diagram in a panel, fitted to
// the column's width and to 720 pixels of height, so a tall diagram is
// scaled down rather than cut off. Hovering shows the controls: Fit, 100%,
// zoom out and in (the diagram then pans when it is larger than the panel),
// Copy (the source), Copy as text (the diagram in box-drawing characters),
// PNG (a picture at twice the size), Edit and As code. The zoom level is in
// the bottom-right corner. A press on a shape or a line selects it, and a
// press on empty space puts the caret in the source.
//
// With the caret in the block it shows the source as a code block, and
// under it a preview that follows the source 250 ms after typing stops.
// While the source does not parse, the preview keeps the last diagram that
// did, and says so, with the error's line and column.
//
// Parsing and layout run on another goroutine (diagram.Render is safe to
// call from any), and the result comes back to the interface's thread
// through unison.InvokeTask, as a web page's preview does. A canvas renders
// at most one source at a time: a new source arriving meanwhile waits, and
// only the newest waiting one is rendered.

import (
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/diagram"
	"github.com/kvit-s/kvit-notes/mermaid"
	"github.com/kvit-s/kvit-notes/textdiagram"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// isMermaid reports whether a block is a Mermaid diagram.
func isMermaid(b *Block) bool { return b.Kind == Code && b.Lang == "mermaid" }

// DiagramMath typesets the $$…$$ labels of Mermaid diagrams. The editor
// works without one, and then draws such a label as its source.
type DiagramMath interface {
	// Size is tex typeset beside text of textSize pixels, and false when it
	// does not typeset.
	Size(tex string, textSize float32) (diagram.Size, bool)
	// Draw typesets tex beside text of textSize pixels with its top left at
	// origin, in colour c, and reports false when it does not typeset.
	Draw(gc *unison.Canvas, tex string, textSize float32, origin geom.Point, c unison.Color) bool
}

// mathAt is a DiagramMath at one text size, as layout measures formulas.
type mathAt struct {
	m    DiagramMath
	size float32
}

func (m mathAt) Size(tex string) (diagram.Size, bool) { return m.m.Size(tex, m.size) }

// fontMeasurer measures a diagram's text through the editor's fonts, which
// are safe to use from the goroutine a diagram is laid out on.
type fontMeasurer struct {
	fonts *text.Fonts
	style text.Style
}

func (m fontMeasurer) Advance(s string) float64 {
	w, _ := m.fonts.Layout([]text.Span{{Text: s, Style: m.style}}, text.Options{}).Size()
	return float64(w)
}

func (m fontMeasurer) Height() float64 {
	_, h := m.fonts.Layout([]text.Span{{Text: "Mg", Style: m.style}}, text.Options{}).Size()
	return float64(h)
}

// diagramOptions are what the editor lays diagrams out with: the note's
// text face at its body size, as Kvit's diagram block does.
func (e *Editor) diagramOptions() diagram.LayoutOptions {
	ty := e.ui.Typography
	size := float32(ty.BodySize())
	o := diagram.LayoutOptions{
		FontFamily: ty.FontFamily(),
		FontSize:   float64(size),
		Measure:    fontMeasurer{e.ui.Fonts, text.Style{Family: ty.FontFamily(), Size: size, Weight: text.Regular}},
	}
	// This is where the math library is plugged in: with DiagramMath set,
	// a whole $$…$$ label is typeset rather than shown as its source.
	if e.DiagramMath != nil {
		o.Math = mathAt{e.DiagramMath, size}
	}
	return o
}

// diagramCanvas is one rendered diagram: the source asked for, the last
// valid scene, and what went wrong with the newest source. It is the
// app's DiagramCanvas apart from the painting, which diagramdraw.go does.
type diagramCanvas struct {
	e       *Editor
	source  string
	opts    diagram.LayoutOptions
	optsKey string // the options' font and maths, to tell a new font apart

	rendering bool // a render is running
	queued    bool // a newer source waits for it
	revision  int  // bumped per render, so only the newest result is used

	// The last valid scene, and the source it came from.
	scene       diagram.Scene
	hasScene    bool
	sceneSource string

	// What the newest result said.
	hasError       bool
	unsupported    bool
	errorText      string
	errorLine      int
	errorColumn    int
	family         mermaid.DiagramType
	hasArrangement bool

	// The selected node or edge, and the one the source's caret is in.
	selNode string
	selEdge int
	hlNode  string
	hlEdge  int
}

func newCanvas(e *Editor) *diagramCanvas {
	return &diagramCanvas{e: e, selEdge: -1, hlEdge: -1}
}

// sceneCurrent reports whether the scene shown is the current source's.
func (c *diagramCanvas) sceneCurrent() bool {
	return c.hasScene && !c.hasError && c.sceneSource == c.source
}

// setSource asks for src to be rendered with opts.
func (c *diagramCanvas) setSource(src string, opts diagram.LayoutOptions) {
	key := fmt.Sprintf("%s|%g|%t", opts.FontFamily, opts.FontSize, opts.Math != nil)
	if src == c.source && key == c.optsKey {
		switch {
		case !c.e.Printing && (c.hasScene || c.hasError || c.rendering):
			return
		case c.e.Printing && !c.rendering && (c.sceneCurrent() || c.hasError):
			// A page needs the drawing now, and it is there.
			return
		}
	}
	c.source, c.opts, c.optsKey = src, opts, key
	c.schedule()
}

// schedule renders the current source, or remembers to once the render
// running now is done: only the newest source is worth rendering.
func (c *diagramCanvas) schedule() {
	if c.e.Printing {
		// A page is drawn at once, so it cannot wait for another goroutine.
		c.revision++
		c.apply(diagram.Render(c.source, c.opts), c.source)
		return
	}
	if c.rendering {
		c.queued = true
		return
	}
	c.rendering = true
	c.start()
}

func (c *diagramCanvas) start() {
	c.revision++
	rev, src, opts := c.revision, c.source, c.opts
	go func() {
		r := diagram.Render(src, opts)
		unison.InvokeTask(func() { c.finished(rev, src, r) })
	}()
}

func (c *diagramCanvas) finished(rev int, src string, r diagram.RenderResult) {
	if c.queued {
		// The source moved on while this ran; the next render is what will
		// be shown.
		c.queued = false
		c.start()
		return
	}
	c.rendering = false
	if rev == c.revision {
		c.apply(r, src)
	}
	c.e.diagramRendered()
}

// apply takes a result in, keeping the last valid scene when the new
// source does not parse.
func (c *diagramCanvas) apply(r diagram.RenderResult, src string) {
	c.unsupported = r.UnsupportedFamily
	c.family = r.Family
	c.hasArrangement = r.HasArrangement
	c.hasError = r.HasError || r.UnsupportedFamily
	c.errorText, c.errorLine, c.errorColumn = "", 0, 0
	if c.hasError {
		c.errorText, c.errorLine, c.errorColumn = r.FirstError.Message, r.FirstError.Line, r.FirstError.Column
		if c.errorText == "" && r.UnsupportedFamily {
			c.errorText = "Unsupported Mermaid diagram type"
		}
	}
	if !r.Valid {
		return
	}
	c.scene, c.hasScene, c.sceneSource = r.Scene, true, src
	// A selection whose element is gone is dropped.
	if c.selNode != "" {
		if _, ok := c.scene.NodeRect(c.selNode); !ok {
			c.clearSelection()
		}
	}
	if c.selEdge >= 0 && !c.scene.HasEdge(c.selEdge) {
		c.clearSelection()
	}
}

// resetScene forgets the last valid scene and every choice made on it, and
// renders the current source again (a cache hit when it is unchanged), so
// a canvas given to another block never shows the old block's diagram.
func (c *diagramCanvas) resetScene() {
	*c = diagramCanvas{e: c.e, source: c.source, opts: c.opts, optsKey: c.optsKey,
		rendering: c.rendering, queued: c.queued, revision: c.revision, selEdge: -1, hlEdge: -1}
	// A render running now belongs to before the reset.
	c.revision++
	if c.source != "" {
		c.schedule()
	}
}

// textDiagram is the scene as box-drawing text (Copy as text), and "" when
// the scene shown is not the current source's.
func (c *diagramCanvas) textDiagram() string {
	if !c.sceneCurrent() || c.scene.Empty() {
		return ""
	}
	return textdiagram.FromScene(&c.scene)
}

// Selection, in scene coordinates.

func (c *diagramCanvas) setSelectedNode(id string) { c.selNode, c.selEdge = id, -1 }
func (c *diagramCanvas) setSelectedEdge(i int)     { c.selNode, c.selEdge = "", i }
func (c *diagramCanvas) clearSelection()           { c.selNode, c.selEdge = "", -1 }
func (c *diagramCanvas) hasSelection() bool        { return c.selNode != "" || c.selEdge >= 0 }

// cycleNode selects the next node in drawing order, or the previous for a
// negative delta, going round at the ends.
func (c *diagramCanvas) cycleNode(delta int) string {
	ids := c.scene.NodeIDs()
	if len(ids) == 0 {
		return ""
	}
	at := -1
	for k, id := range ids {
		if id == c.selNode {
			at = k
		}
	}
	if at < 0 {
		at = -1
		if delta < 0 {
			at = 0
		}
	}
	at = ((at+delta)%len(ids) + len(ids)) % len(ids)
	c.setSelectedNode(ids[at])
	return ids[at]
}

// selectionLabel is what a screen reader is told of the selection.
func (c *diagramCanvas) selectionLabel() string {
	switch {
	case c.selNode != "":
		return "Node " + c.selNode
	case c.selEdge >= 0:
		return fmt.Sprintf("Connection %d", c.selEdge+1)
	}
	return ""
}

// sourceOffsetForSelection is where the selected element starts in the
// source, or -1.
func (c *diagramCanvas) sourceOffsetForSelection() int {
	if c.selNode != "" {
		return c.scene.NodeSource(c.selNode)
	}
	if c.selEdge >= 0 {
		return c.scene.EdgeSource(c.selEdge)
	}
	return -1
}

// highlightSourceOffset lights up the element the source offset is in.
func (c *diagramCanvas) highlightSourceOffset(off int) {
	c.hlNode, c.hlEdge = c.scene.ElementAtOffset(off)
}

// sourceLineForOffset is the one-based line of the source an offset is on,
// or -1.
func sourceLineForOffset(src string, off int) int {
	if off < 0 {
		return -1
	}
	r := []rune(src)
	return strings.Count(string(r[:min(off, len(r))]), "\n") + 1
}

// diagramView is one diagram block's state: the canvas the diagram is read
// in, the preview's while its source is edited, and the zoom.
type diagramView struct {
	read    *diagramCanvas
	preview *diagramCanvas

	// Fit is the default; zoomed holds a zoom level chosen with 100%, − or
	// +, and pan how far a zoomed diagram is scrolled in its panel.
	zoomed bool
	zoom   float32
	pan    geom.Point

	editing       bool   // the caret was in the block when last measured
	previewSource string // the source the preview shows, 250 ms behind
	pending       string // the source a debounce is waiting to show
	debounce      int    // bumped per edit, so only the newest waits
}

// diagramDebounce is how long the preview waits after typing stops.
const diagramDebounce = 250 * time.Millisecond

// diagramFor is block b's diagram state, made on first use.
func (e *Editor) diagramFor(b *Block) *diagramView {
	if e.diagrams == nil {
		e.diagrams = map[int64]*diagramView{}
	}
	v := e.diagrams[b.ID]
	if v == nil {
		v = &diagramView{read: newCanvas(e), preview: newCanvas(e), previewSource: b.Text}
		e.diagrams[b.ID] = v
	}
	return v
}

// pruneDiagrams drops the state of diagram blocks that are gone or are no
// longer diagrams, so a block turned back into one starts afresh rather than
// showing what it drew before.
func (e *Editor) pruneDiagrams() {
	if len(e.diagrams) == 0 {
		return
	}
	keep := map[int64]bool{}
	for i := range e.Doc.Blocks {
		if isMermaid(&e.Doc.Blocks[i]) {
			keep[e.Doc.Blocks[i].ID] = true
		}
	}
	for id := range e.diagrams {
		if !keep[id] {
			delete(e.diagrams, id)
		}
	}
}

// diagramRendered follows a render finishing: the row may have changed
// height.
func (e *Editor) diagramRendered() {
	if e.Window() == nil && !e.Printing {
		return
	}
	e.changed()
}

// diagramEditing reports whether block i is a diagram with the caret in it.
func (e *Editor) diagramEditing(i int) bool {
	b := &e.Doc.Blocks[i]
	return isMermaid(b) && e.Doc.Focused && e.Doc.Caret.Block == b.ID
}

// diagramReads reports whether block i is a diagram shown as its drawing.
func (e *Editor) diagramReads(i int) bool {
	return isMermaid(&e.Doc.Blocks[i]) && !e.diagramEditing(i)
}

// syncDiagram brings block i's canvases up to its source: the read canvas
// follows the text, and the preview follows it 250 ms behind while the
// caret is in the block.
func (e *Editor) syncDiagram(i int) *diagramView {
	b := &e.Doc.Blocks[i]
	v := e.diagramFor(b)
	if e.Doc.Focused || len(e.blockSel) > 0 {
		// The caret or a block selection is elsewhere, and the keys with
		// it, so a selected element of the drawing is let go.
		v.read.clearSelection()
	}
	opts := e.diagramOptions()
	editing := e.diagramEditing(i)
	if !editing {
		v.editing = false
		v.read.setSource(b.Text, opts)
		return v
	}
	if !v.editing {
		// The caret has just come in: the preview starts from the source
		// as it is.
		v.editing = true
		v.previewSource, v.pending = b.Text, b.Text
		v.debounce++
	}
	if b.Text != v.previewSource && b.Text != v.pending {
		v.pending = b.Text
		v.debounce++
		gen := v.debounce
		unison.InvokeTaskAfter(func() {
			if v.debounce != gen || e.diagrams[b.ID] != v {
				return
			}
			v.previewSource = v.pending
			e.changed()
		}, diagramDebounce)
	}
	v.preview.setSource(v.previewSource, opts)
	if v.preview.sceneCurrent() && v.previewSource == b.Text && e.Doc.Caret.Block == b.ID {
		// The caret's statement lights up in the preview.
		v.preview.highlightSourceOffset(e.Doc.Caret.Off)
	} else {
		v.preview.hlNode, v.preview.hlEdge = "", -1
	}
	return v
}

// The diagram block in design pixels (qml/DiagramBlock).
const (
	diagramRowTop      = 8   // above the panel
	diagramRowBottom   = 8   // below the last part
	diagramSpacing     = 6   // between the source, the preview and the hint
	diagramRadius      = 6   // the read panel's corners
	diagramPadX        = 8   // the panel's content from its sides
	diagramPadY        = 5   // and from its top and bottom
	diagramBodyGap     = 4   // between the drawing and the note under it
	diagramMaxRead     = 720 // the tallest a drawing is shown before it is scaled down
	diagramMinRead     = 24
	diagramChipH       = 18
	diagramChipPad     = 6 // either side of a control's word
	diagramChipGap     = 4
	diagramChipTop     = 4
	diagramChipRight   = 6
	diagramChipRadius  = 4
	diagramZoomH       = 16
	diagramZoomPad     = 5
	diagramZoomMargin  = 6
	diagramPreviewMax  = 320 // the tallest the preview is
	diagramPreviewMin  = 32
	diagramPreviewPad  = 6
	diagramPreviewRad  = 4
	diagramPreviewTail = 14 // the preview's padding and the gap above its notes
	diagramZoomMin     = 0.05
	diagramZoomMax     = 3.0
	diagramZoomStep    = 1.25
)

// rowTop is where row i starts, or 0 while the rows are being measured, when
// the parts of a row are worked out from its own top.
func (e *Editor) rowTop(i int) float32 {
	if i < len(e.tops) {
		return e.tops[i]
	}
	return 0
}

// diagramPanel is the read panel of a diagram block whose row starts at
// top: where a code block's panel is.
func (e *Editor) diagramPanel(top, height float32) geom.Rect {
	return geom.NewRect(e.bodyLeft()+e.px(codeInset), top+e.px(diagramRowTop),
		e.bodyRight()-e.bodyLeft()-e.px(codeInset+contentRight), height)
}

// diagramInnerWidth is the width inside a diagram block's panel.
func (e *Editor) diagramInnerWidth(int) float32 {
	return e.bodyRight() - e.bodyLeft() - e.px(codeInset+contentRight) - 2*e.px(diagramPadX)
}

// sourcePanel is the code panel of diagram block i, whose row starts at top,
// as codePanel places it.
func (e *Editor) sourcePanel(i int, top float32) geom.Rect {
	return geom.NewRect(e.bodyLeft()+e.px(codeInset), top+e.px(codeRowTop), e.bodyRight()-e.bodyLeft()-e.px(codeInset+contentRight),
		e.px(codeHeader+2*codeTextPad+codeFooter)+e.layout(i).height())
}

// readScale is the scale the read canvas draws at: in fit mode as large as
// fits the panel's width and 720 pixels, never larger than the diagram's
// own size, and otherwise the zoom chosen.
func (e *Editor) readScale(v *diagramView, width float32) float32 {
	if v.zoomed {
		return v.zoom
	}
	return fitScale(v.read.scene.Bounds, width, e.px(diagramMaxRead))
}

// fitScale is the largest scale up to 1 at which a scene fits a width and
// a height.
func fitScale(bounds diagram.Rect, width, height float32) float32 {
	s := float32(1)
	if bounds.W > 0 {
		s = min(s, width/float32(bounds.W))
	}
	if bounds.H > 0 {
		s = min(s, height/float32(bounds.H))
	}
	return max(s, 0.02)
}

// diagramNotice is the text a read panel without a drawing shows, and its
// style: why there is no diagram.
func (e *Editor) diagramNotice(i int, v *diagramView) (string, text.Style) {
	b := &e.Doc.Blocks[i]
	t := e.tok()
	c := v.read
	switch {
	case strings.TrimSpace(b.Text) == "":
		st := e.chrome(roleStrong, text.Regular, t.TextFaint)
		st.Italic = true
		return "Empty Mermaid diagram — click to edit", st
	case c.unsupported:
		return "Unsupported Mermaid diagram type in this Kvit version. The source is preserved — click to edit, or treat it as code.",
			e.chrome(roleBody, text.Regular, t.TextMuted)
	case c.hasError:
		msg := "⚠ " + c.errorText
		if c.errorLine > 0 {
			msg += fmt.Sprintf(" (line %d)", c.errorLine)
		}
		return msg, e.chrome(roleBody, text.Regular, t.Danger)
	}
	return "Rendering…", e.chrome(roleBody, text.Regular, t.TextFaint)
}

// readParts are where the parts of diagram block i's read panel are: the
// panel, the window the drawing shows in, the note under it (the reason
// there is no drawing, or that it is from the last valid source), and the
// canvas's scale.
type readParts struct {
	panel, window geom.Rect
	note          *text.Layout
	noteAt        geom.Point
	scale         float32
}

func (e *Editor) diagramReadParts(i int) readParts { return e.diagramReadPartsAt(i, e.rowTop(i)) }

func (e *Editor) diagramReadPartsAt(i int, top float32) readParts {
	v := e.diagramFor(&e.Doc.Blocks[i])
	c := v.read
	width := e.diagramInnerWidth(i)
	var p readParts
	y := float32(0)
	if c.hasScene {
		p.scale = e.readScale(v, width)
		h := float32(c.scene.Bounds.H) * p.scale
		p.window = geom.NewRect(0, 0, width, max(e.px(diagramMinRead), min(h, e.px(diagramMaxRead))))
		y = p.window.Height
		if c.hasError {
			p.note = e.label("⚠ Preview is from the last valid source", e.chrome(roleSmall, text.Regular, e.tok().Warning))
		}
	} else {
		msg, st := e.diagramNotice(i, v)
		p.note = e.ui.Fonts.Layout([]text.Span{{Text: msg, Style: st}}, text.Options{MaxWidth: max(1, width)})
	}
	if p.note != nil {
		if c.hasScene {
			y += e.px(diagramBodyGap)
		}
		p.noteAt = geom.NewPoint(0, y)
		_, nh := p.note.Size()
		y += nh
	}
	p.panel = e.diagramPanel(top, y+2*e.px(diagramPadY))
	inner := geom.NewPoint(p.panel.X+e.px(diagramPadX), p.panel.Y+e.px(diagramPadY))
	p.window.X, p.window.Y = p.window.X+inner.X, p.window.Y+inner.Y
	p.noteAt = p.noteAt.Add(inner)
	return p
}

// previewParts are where the parts of the preview under diagram block i's
// source are: the panel, the window the drawing shows in, the notes under
// it, the key hint, and the canvas's scale.
type previewParts struct {
	panel, window geom.Rect
	notes         []*text.Layout
	notesAt       geom.Point
	hint          *text.Layout
	hintAt        geom.Point
	scale         float32
}

func (e *Editor) diagramPreviewParts(i int) previewParts {
	return e.diagramPreviewPartsAt(i, e.rowTop(i))
}

func (e *Editor) diagramPreviewPartsAt(i int, top float32) previewParts {
	v := e.diagramFor(&e.Doc.Blocks[i])
	c := v.preview
	t := e.tok()
	code := e.sourcePanel(i, top)
	var p previewParts
	width := code.Width - 2*e.px(diagramPreviewPad)
	canvasH := float32(0)
	if c.hasScene {
		p.scale = fitScale(c.scene.Bounds, width, e.px(diagramPreviewMax))
		canvasH = float32(c.scene.Bounds.H) * p.scale
	}
	viewH := max(min(canvasH, e.px(diagramPreviewMax)), e.px(diagramPreviewMin))
	if c.hasError {
		msg := "⚠ " + c.errorText
		if c.errorLine > 0 {
			msg = fmt.Sprintf("line %d:%d  ", c.errorLine, c.errorColumn) + msg
		}
		p.notes = append(p.notes, e.ui.Fonts.Layout([]text.Span{{Text: msg, Style: e.chrome(roleSmall, text.Regular, t.Danger)}},
			text.Options{MaxWidth: max(1, width)}))
		if c.hasScene {
			p.notes = append(p.notes, e.label("Preview is from the last valid source", e.chrome(roleCaption, text.Regular, t.Warning)))
		}
	}
	var notesH float32
	for k, n := range p.notes {
		_, h := n.Size()
		notesH += h
		if k > 0 {
			notesH += e.px(1)
		}
	}
	p.panel = geom.NewRect(code.X, code.Bottom()+e.px(diagramSpacing), code.Width, viewH+notesH+e.px(diagramPreviewTail))
	p.window = geom.NewRect(p.panel.X+e.px(diagramPreviewPad), p.panel.Y+e.px(diagramPreviewPad), width, min(canvasH, e.px(diagramPreviewMax)))
	p.notesAt = geom.NewPoint(p.window.X, p.panel.Bottom()-e.px(diagramPreviewPad)-notesH)
	// The key that leaves the block, in the corner under the preview.
	hintSize := max(9, e.ui.Typography.MonoSize()-4)
	p.hint = e.ui.Fonts.Layout([]text.Span{{Text: "Ctrl+Enter: new block",
		Style: text.Style{Family: e.ui.Typography.FontFamily(), Size: float32(hintSize), Color: colour(t.TextFaint)}}}, text.Options{})
	hw, _ := p.hint.Size()
	p.hintAt = geom.NewPoint(code.Right()-hw, p.panel.Bottom()+e.px(diagramSpacing))
	return p
}

// diagramHeight is diagram block i's row height, and false for a block
// that is not a diagram.
func (e *Editor) diagramHeight(i int) (float32, bool) {
	if !isMermaid(&e.Doc.Blocks[i]) {
		return 0, false
	}
	e.syncDiagram(i)
	if e.diagramReads(i) {
		p := e.diagramReadPartsAt(i, 0)
		return e.px(diagramRowTop+diagramRowBottom) + p.panel.Height, true
	}
	p := e.diagramPreviewPartsAt(i, 0)
	_, hh := p.hint.Size()
	return p.hintAt.Y + hh + e.px(diagramRowBottom), true
}

// ---- controls ----

// Controls of a diagram block, as parts of a row. They follow gutterPart's
// own values, well clear of them.
const (
	partDiagramFit gutterPart = iota + 1000
	partDiagram100
	partDiagramOut
	partDiagramIn
	partDiagramCopy
	partDiagramText
	partDiagramPNG
	partDiagramEdit
	partDiagramAsCode
	partDiagramReset
	partDiagramCanvas // the drawing itself
	partDiagramPreview
)

// diagramChip is one of a read panel's hover controls.
type diagramChip struct {
	part   gutterPart
	label  string
	name   string // what a screen reader calls it
	active bool
	rect   geom.Rect
}

// diagramChips are the hover controls of diagram block i, laid out from the
// panel's top-right corner, with where the word "Mermaid" goes before them.
func (e *Editor) diagramChips(i int) ([]diagramChip, geom.Point) {
	b := &e.Doc.Blocks[i]
	v := e.diagramFor(b)
	c := v.read
	chips := []diagramChip{
		{part: partDiagramFit, label: "Fit", name: "Fit the diagram", active: !v.zoomed},
		{part: partDiagram100, label: "100%", name: "Actual size", active: v.zoomed && v.zoom == 1},
		{part: partDiagramOut, label: "−", name: "Zoom out"},
		{part: partDiagramIn, label: "+", name: "Zoom in"},
		{part: partDiagramCopy, label: "Copy", name: "Copy the source"},
	}
	if c.sceneCurrent() {
		chips = append(chips, diagramChip{part: partDiagramText, label: "Copy as text", name: "Copy as text"})
	}
	if c.hasArrangement && !e.Doc.ReadOnly {
		chips = append(chips, diagramChip{part: partDiagramReset, label: "Reset layout", name: "Reset layout"})
	}
	if c.hasScene {
		chips = append(chips, diagramChip{part: partDiagramPNG, label: "PNG", name: "Save as PNG"})
	}
	if !e.Doc.ReadOnly {
		chips = append(chips, diagramChip{part: partDiagramEdit, label: "Edit", name: "Edit the source"},
			diagramChip{part: partDiagramAsCode, label: "As code", name: "Show as code"})
	}
	panel := e.diagramReadParts(i).panel
	st := e.chrome(roleCaption, text.Regular, e.tok().TextSecondary)
	x := panel.Right() - e.px(diagramChipRight)
	y := panel.Y + e.px(diagramChipTop)
	for k := len(chips) - 1; k >= 0; k-- {
		w, _ := e.label(chips[k].label, st).Size()
		w += 2 * e.px(diagramChipPad)
		x -= w
		chips[k].rect = geom.NewRect(x, y, w, e.px(diagramChipH))
		x -= e.px(diagramChipGap)
	}
	lw, _ := e.label("Mermaid", st).Size()
	return chips, geom.NewPoint(x-lw, y)
}

// diagramPartAt is the control of diagram block i under a point: one of the
// hover controls, the drawing, or the preview.
func (e *Editor) diagramPartAt(i int, where geom.Point) gutterPart {
	if !isMermaid(&e.Doc.Blocks[i]) {
		return partNone
	}
	if e.diagramReads(i) {
		if e.hover == e.Doc.Blocks[i].ID {
			chips, _ := e.diagramChips(i)
			for _, c := range chips {
				if where.In(c.rect) {
					return c.part
				}
			}
		}
		if where.In(e.diagramReadParts(i).panel) {
			return partDiagramCanvas
		}
		return partNone
	}
	if p := e.diagramPreviewParts(i); where.In(p.panel) {
		return partDiagramPreview
	}
	return partNone
}

// diagramAct carries out one of diagram block i's controls.
func (e *Editor) diagramAct(i int, part gutterPart) bool {
	b := &e.Doc.Blocks[i]
	v := e.diagramFor(b)
	c := v.read
	scale := e.readScale(v, e.diagramInnerWidth(i))
	switch part {
	case partDiagramFit:
		v.zoomed, v.pan = false, geom.Point{}
	case partDiagram100:
		v.zoomed, v.zoom = true, 1
	case partDiagramOut:
		// A zoom step goes on from the scale on screen, which in fit mode
		// is the scale Fit chose.
		v.zoomed, v.zoom = true, max(diagramZoomMin, scale/diagramZoomStep)
	case partDiagramIn:
		v.zoomed, v.zoom = true, min(diagramZoomMax, scale*diagramZoomStep)
	case partDiagramCopy:
		unison.ClipboardSetText(b.Text)
	case partDiagramText:
		if s := c.textDiagram(); s != "" {
			unison.ClipboardSetText(s)
			e.status("Copied the diagram as text")
		}
	case partDiagramPNG:
		e.savePNG(b.ID)
	case partDiagramReset:
		e.resetArrangement(i)
	case partDiagramEdit:
		if !e.Doc.ReadOnly {
			e.FocusBlock(i, len(runes(b.Text)))
		}
	case partDiagramAsCode:
		if !e.Doc.ReadOnly {
			e.Doc.AsCode(b.ID)
		}
	default:
		return false
	}
	e.clampPan(i)
	return true
}

// clampPan keeps a zoomed diagram's scroll inside the drawing.
func (e *Editor) clampPan(i int) {
	v := e.diagramFor(&e.Doc.Blocks[i])
	p := e.diagramReadParts(i)
	if !v.zoomed || !v.read.hasScene {
		v.pan = geom.Point{}
		return
	}
	w := float32(v.read.scene.Bounds.W) * p.scale
	h := float32(v.read.scene.Bounds.H) * p.scale
	v.pan.X = max(0, min(v.pan.X, w-p.window.Width))
	v.pan.Y = max(0, min(v.pan.Y, h-p.window.Height))
}

// canvasPoint is a point of the editor in a read canvas's scene
// coordinates.
func (e *Editor) canvasPoint(i int, where geom.Point) diagram.Point {
	v := e.diagramFor(&e.Doc.Blocks[i])
	p := e.diagramReadParts(i)
	x := (where.X - p.window.X + v.pan.X) / p.scale
	y := (where.Y - p.window.Y + v.pan.Y) / p.scale
	return diagram.Point{X: float64(x), Y: float64(y)}
}

// selectDiagramElement selects a node or an edge of diagram block i, takes
// the caret out of the text so the keys go to the selection, and says what
// was selected and on which line of the source it is.
func (e *Editor) selectDiagramElement(i int, node string, edge int) {
	b := &e.Doc.Blocks[i]
	for id, v := range e.diagrams {
		if id != b.ID {
			v.read.clearSelection()
		}
	}
	c := e.diagramFor(b).read
	if node != "" {
		c.setSelectedNode(node)
	} else {
		c.setSelectedEdge(edge)
	}
	e.clearBlockSel()
	e.Doc.Focused = false
	e.announceDiagramSelection(i)
}

func (e *Editor) announceDiagramSelection(i int) {
	b := &e.Doc.Blocks[i]
	c := e.diagramFor(b).read
	if !c.hasSelection() {
		return
	}
	msg := c.selectionLabel()
	if line := sourceLineForOffset(b.Text, c.sourceOffsetForSelection()); line > 0 {
		msg += fmt.Sprintf(" — line %d", line)
	}
	e.status(msg)
}

// editDiagramSource puts the caret in diagram block i's source at a source
// offset, or at its end for -1.
func (e *Editor) editDiagramSource(i int, off int) {
	if e.Doc.ReadOnly {
		return
	}
	b := &e.Doc.Blocks[i]
	n := len(runes(b.Text))
	if off < 0 || off > n {
		off = n
	}
	e.FocusBlock(i, off)
}

// diagramSelection is the diagram block holding a selected element, or -1.
func (e *Editor) diagramSelection() int {
	for id, v := range e.diagrams {
		if v.read.hasSelection() {
			if i := e.Doc.Index(id); i >= 0 && e.diagramReads(i) {
				return i
			}
			v.read.clearSelection()
		}
	}
	return -1
}

// diagramWheel scrolls a zoomed diagram larger than its panel, and leaves
// every other wheel turn to the region the editor is in.
func (e *Editor) diagramWheel(where, delta geom.Point, _ mod.Modifiers) bool {
	i := e.rowAt(where)
	if i < 0 || !e.diagramReads(i) {
		return false
	}
	v := e.diagramFor(&e.Doc.Blocks[i])
	p := e.diagramReadParts(i)
	if !v.zoomed || !where.In(p.window) {
		return false
	}
	before := v.pan
	step := e.px(40)
	v.pan = v.pan.Sub(geom.NewPoint(delta.X*step, delta.Y*step))
	e.clampPan(i)
	if v.pan == before {
		return false
	}
	e.MarkForRedraw()
	return true
}

// status shows a short message where the application shows one, and tells
// a screen reader.
func (e *Editor) status(msg string) {
	if e.OnStatus != nil {
		e.OnStatus(msg)
	}
	unison.AnnounceForAccessibility(msg)
}

// ---- PNG ----

// diagramPNG is block id's diagram as a PNG at scale times its size, on the
// page's background, or false when there is no diagram to save.
func (e *Editor) diagramPNG(id int64, scale float32) ([]byte, bool) {
	v := e.diagrams[id]
	if v == nil || !v.read.hasScene || v.read.scene.Empty() {
		return nil, false
	}
	return e.scenePNG(&v.read.scene, scale)
}

func (e *Editor) scenePNG(s *diagram.Scene, scale float32) ([]byte, bool) {
	scale = max(0.5, min(scale, 8))
	// The picture is bounded twice, each side and the whole area; a scene
	// too large for either is drawn smaller rather than asking for memory
	// the process cannot have.
	w := float64(s.Bounds.W) * float64(scale)
	h := float64(s.Bounds.H) * float64(scale)
	edgeFit := min(1, diagram.MaxRasterEdge/max(w, 1), diagram.MaxRasterEdge/max(h, 1))
	w, h = w*edgeFit, h*edgeFit
	areaFit := min(1, math.Sqrt(diagram.MaxRasterPixels/max(w*h, 1)))
	w, h = w*areaFit, h*areaFit
	final := scale * float32(edgeFit*areaFit)
	img, err := unison.NewImageFromDrawing(max(1, int(w)), max(1, int(h)), 72, func(gc *unison.Canvas) {
		r := geom.NewRect(0, 0, float32(w), float32(h))
		e.fill(gc, r, e.tok().WindowBackground)
		gc.Scale(geom.NewPoint(final, final))
		e.paintScene(gc, s, nil)
	})
	if err != nil {
		return nil, false
	}
	data, err := img.ToPNG(6)
	return data, err == nil
}

// saveDiagramPNG writes block id's diagram as a PNG at twice its size to a
// path.
func (e *Editor) saveDiagramPNG(id int64, path string) error {
	if path == "" {
		return fmt.Errorf("no file named")
	}
	data, ok := e.diagramPNG(id, 2)
	if !ok {
		return fmt.Errorf("no diagram to save")
	}
	return os.WriteFile(path, data, 0o644)
}

// savePNG is the PNG control: SaveDiagramPNG is given the picture when set,
// and otherwise a file dialog asks where to write it.
func (e *Editor) savePNG(id int64) {
	if e.SaveDiagramPNG != nil {
		if data, ok := e.diagramPNG(id, 2); ok {
			// After the press, since the application may show a dialog.
			save := e.SaveDiagramPNG
			unison.InvokeTask(func() { save(data) })
		}
		return
	}
	unison.InvokeTask(func() {
		dlg := unison.NewSaveDialog()
		dlg.SetAllowedExtensions("png")
		dlg.SetInitialFileName("diagram.png")
		if !dlg.RunModal() || dlg.Path() == "" {
			return
		}
		path := dlg.Path()
		if filepath.Ext(path) == "" {
			path += ".png"
		}
		if err := e.saveDiagramPNG(id, path); err != nil {
			e.status("Could not save the diagram")
			return
		}
		e.status("Diagram saved to " + path)
	})
}

// previewPress is a press on the preview under the source: on a shape or a
// line it puts the caret on the statement that draws it, while the preview
// shows the source as it is now.
func (e *Editor) previewPress(i int, where geom.Point) {
	b := &e.Doc.Blocks[i]
	v := e.diagramFor(b)
	c := v.preview
	p := e.diagramPreviewParts(i)
	if !c.sceneCurrent() || v.previewSource != b.Text || !where.In(p.window) || p.scale <= 0 {
		return
	}
	at := diagram.Point{X: float64((where.X - p.window.X) / p.scale), Y: float64((where.Y - p.window.Y) / p.scale)}
	if off := c.scene.SourceOffsetAt(at); off >= 0 {
		e.Doc.SetCaret(b.ID, min(off, len(runes(b.Text))))
		e.touched()
	}
}
