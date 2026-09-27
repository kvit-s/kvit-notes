// Package editor is Kvit's block editor as a unison widget: a Markdown note
// shown as a column of blocks (paragraphs, headings, list items, to-dos,
// quotes, code, dividers), edited in place with the inline formatting drawn
// and its Markdown markers shown only around the caret.
//
// The note is a flat list of blocks (Doc), each holding its Markdown source.
// The caret and the selection are positions in that source. The Editor panel
// lays every block out with kvit-ui's text package, draws the rows in view,
// turns keys and pointer input into operations on the Doc, and describes
// each block to screen readers as an editable text.
package editor

import (
	"time"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// Editor is the block editor. Put it in a kvitui.Region to scroll it; it
// takes the width it is given and is as tall as its note.
type Editor struct {
	unison.Panel
	ui *kvitui.UI
	// Doc is the note being edited.
	Doc *Doc
	// OnChange runs after anything a status line would show has changed:
	// the note, the caret, the selection or the block selection.
	OnChange func()
	// Placeholder is shown in an empty paragraph.
	Placeholder string

	layouts    map[int64]cachedLayout
	tops       []float32 // the top of each block's row, in the editor's coordinates
	heights    []float32 // the height of each block's row
	laidWidth  float32   // the width the rows were measured at
	generation int       // bumped when the theme or typography changes

	goalX   float32 // the x Up and Down keep to
	hasGoal bool

	blockSel    map[int64]bool // the blocks selected as whole blocks
	blockAnchor int64          // where Shift extends a block selection from
	selectAllN  int            // Ctrl+A presses in a row

	menu   *slashMenu
	drag   *dragState
	msel   *mouseSel
	hover  int64      // the block under the pointer, 0 for none
	part   gutterPart // the gutter control under the pointer
	recent []Kind     // the / menu's recently used kinds, newest first

	blinkOff  bool      // the caret is in the off half of its blink
	blinkFrom time.Time // when the caret last moved, which restarts the blink
	blinking  bool
}

type cachedLayout struct {
	key    layoutKey
	layout *blockLayout
}

// New returns an editor showing doc.
func New(ui *kvitui.UI, doc *Doc) *Editor {
	e := &Editor{ui: ui, Doc: doc, Placeholder: "Type something...", layouts: map[int64]cachedLayout{},
		blockSel: map[int64]bool{}}
	e.Self = e
	e.SetFocusable(true)
	e.SetSizer(e.sizes)
	e.DrawCallback = e.draw
	e.KeyDownCallback = e.keyDown
	e.RuneTypedCallback = e.runeTyped
	e.MouseDownCallback = e.mouseDown
	e.MouseDragCallback = e.mouseDrag
	e.MouseUpCallback = e.mouseUp
	e.MouseEnterCallback = e.mouseMove
	e.MouseMoveCallback = e.mouseMove
	e.MouseExitCallback = e.mouseExit
	e.UpdateCursorCallback = e.cursor
	e.GainedFocusCallback = func() { e.touched(); e.MarkForRedraw() }
	e.LostFocusCallback = func() { e.MarkForRedraw() }
	e.FrameChangeCallback = func() {
		if w := e.ContentRect(false).Width; w > 0 {
			e.measureAt(w)
		}
	}
	e.Accessibility.Name = "Note"
	ui.OnChanged(func() {
		e.generation++
		e.changed()
	})
	return e
}

// SetDoc replaces the note being edited.
func (e *Editor) SetDoc(doc *Doc) {
	e.Doc = doc
	clear(e.layouts)
	clear(e.blockSel)
	e.closeMenu()
	e.drag, e.msel = nil, nil
	e.changed()
}

// UI is the interface the editor draws with.
func (e *Editor) UI() *kvitui.UI { return e.ui }

// FocusBlock puts the caret in block i at a source offset and gives the
// editor the keyboard focus.
func (e *Editor) FocusBlock(i, off int) {
	e.clearBlockSel()
	e.Doc.SetCaret(e.Doc.Blocks[i].ID, off)
	e.RequestFocus()
	e.touched()
	e.changed()
}

// Refresh redraws the editor after its Doc was changed from outside, and
// brings the caret into view.
func (e *Editor) Refresh() {
	e.touched()
	e.changed()
}

// ClearFocus takes the caret out of every block, as a press outside the
// note does.
func (e *Editor) ClearFocus() {
	e.Doc.Focused = false
	e.Doc.Anchor = e.Doc.Caret
	e.clearBlockSel()
	e.changed()
}

// SelectedBlocks are the ids of the blocks selected as whole blocks, in
// document order.
func (e *Editor) SelectedBlocks() []int64 {
	var ids []int64
	for _, b := range e.Doc.Blocks {
		if e.blockSel[b.ID] {
			ids = append(ids, b.ID)
		}
	}
	return ids
}

func (e *Editor) clearBlockSel() {
	clear(e.blockSel)
	e.selectAllN = 0
}

// changed re-measures the rows after the note, the caret or the selection
// changed, asks for a new layout when the note's height changed, and
// redraws.
func (e *Editor) changed() {
	before := e.total()
	e.measure()
	if e.total() != before {
		for p := e.AsPanel(); p != nil; p = p.Parent() {
			p.NeedsLayout = true
		}
	}
	e.MarkForRedraw()
	if e.menu != nil {
		e.menu.relayout()
	}
	if e.OnChange != nil {
		e.OnChange()
	}
}

// touched restarts the caret's blink and brings the caret into view.
func (e *Editor) touched() {
	e.blinkFrom = time.Now()
	e.blinkOff = false
	e.startBlink()
	e.revealCaret()
}

// Geometry. Everything is in the editor's own coordinates.

// width is the width the rows were measured at: the editor's own width
// once it has one.
func (e *Editor) width() float32 {
	if e.laidWidth > 0 {
		return e.laidWidth
	}
	if w := e.ContentRect(false).Width; w > 0 {
		return w
	}
	return e.px(defaultWidth)
}

// defaultWidth is the width an editor is measured at before it is given one.
const defaultWidth = 800

// bodyLeft and bodyRight are the left and right edges of a row's body: the
// part after the gutter and the focus bar, which is tinted on hover.
func (e *Editor) bodyLeft() float32 { return e.px(pageMargin + gutterWidth + focusBar) }

func (e *Editor) bodyRight() float32 { return e.width() - e.px(pageMargin) }

// textLeft is how far a block's text starts from the body's left edge.
func (e *Editor) textLeft(b *Block) float32 {
	switch {
	case b.Kind.IsList():
		return e.markerLeft(b) + e.px(markerWidth(b.Kind))
	case b.Kind == Quote:
		return e.markerLeft(b) + e.px(quoteMarker)
	case b.Kind == Code || b.Kind == Raw:
		return e.px(codeInset + codePadSide + codeTextInset)
	}
	return e.px(contentLeft)
}

// markerLeft is where a list item's marker, or a quote's bar, starts from
// the body's left edge: a little left of a paragraph's text, and one step
// further in for each level of a list.
func (e *Editor) markerLeft(b *Block) float32 {
	switch {
	case b.Kind.IsList():
		return e.px(contentLeft-listShift) + float32(b.Indent)*e.px(indentStep)
	case b.Kind == Quote:
		return e.px(contentLeft - quoteShift)
	}
	return 0
}

// How far a list's markers and a quote's bar sit left of a paragraph's text.
const (
	listShift  = 8
	quoteShift = 10
)

func markerWidth(k Kind) float32 {
	switch k {
	case Bullet:
		return bulletMarker
	case Numbered:
		return numberMarker
	case Todo:
		return todoMarker
	case Quote:
		return quoteMarker
	}
	return 0
}

// textWidth is the width a block's text wraps at.
func (e *Editor) textWidth(b *Block) float32 {
	right := e.px(contentRight)
	if b.Kind == Code || b.Kind == Raw {
		right += e.px(codePadSide)
	}
	return max(e.bodyRight()-e.bodyLeft()-e.textLeft(b)-right, e.px(40))
}

// textTop is how far a block's text starts below its row's top.
func (e *Editor) textTop(b *Block) float32 {
	if b.Kind == Code || b.Kind == Raw {
		return e.px(codeRowTop + codeHeader + codeTextPad)
	}
	return e.px(rowPadTop)
}

// textOrigin is the top-left corner of block i's text.
func (e *Editor) textOrigin(i int) geom.Point {
	b := &e.Doc.Blocks[i]
	return geom.NewPoint(e.bodyLeft()+e.textLeft(b), e.tops[i]+e.textTop(b))
}

// rowRect is block i's whole row, the gutter included.
func (e *Editor) rowRect(i int) geom.Rect {
	return geom.NewRect(e.px(pageMargin), e.tops[i], e.width()-2*e.px(pageMargin), e.heights[i])
}

// bodyRect is block i's row after the gutter and the focus bar.
func (e *Editor) bodyRect(i int) geom.Rect {
	return geom.NewRect(e.bodyLeft(), e.tops[i], e.bodyRight()-e.bodyLeft(), e.heights[i])
}

// layout is block i's text laid out for the current caret and selection,
// from the cache when nothing it depends on has changed.
func (e *Editor) layout(i int) *blockLayout {
	d := e.Doc
	b := &d.Blocks[i]
	key := layoutKey{text: b.Text, kind: b.Kind, checked: b.Checked, width: e.textWidth(b), caret: -1,
		generation: e.generation}
	if d.Focused && d.Caret.Block == b.ID {
		key.caret = min(d.Caret.Off, len([]rune(b.Text)))
	}
	if from, to, ok := d.SelectionIn(i); ok {
		key.selA, key.selB = from, to
		if d.CrossBlock() {
			// Across blocks the caret's block reveals nothing for the
			// selection, only for the caret.
			key.caret = -1
		}
	}
	if c, ok := e.layouts[b.ID]; ok && c.key == key {
		return c.layout
	}
	caret := key.caret
	l := e.layOut(b, key.width, caret, key.selA, key.selB)
	e.layouts[b.ID] = cachedLayout{key, l}
	return l
}

// rowHeight is the height of block i's row.
func (e *Editor) rowHeight(i int) float32 {
	b := &e.Doc.Blocks[i]
	if b.Kind == Divider {
		return e.px(dividerRow)
	}
	if b.Kind == Code || b.Kind == Raw {
		return e.px(codeRowTop+codeHeader+2*codeTextPad+codeFooter+codeRowBottom) + e.layout(i).height()
	}
	return e.px(rowPadTop+rowPadBottom) + e.layout(i).height()
}

// measure works out the top and height of every row at the current width.
func (e *Editor) measure() {
	d := e.Doc
	n := len(d.Blocks)
	e.tops = e.tops[:0]
	e.heights = e.heights[:0]
	y := e.px(pageMargin)
	for i := 0; i < n; i++ {
		h := e.rowHeight(i)
		e.tops = append(e.tops, y)
		e.heights = append(e.heights, h)
		y += h + e.px(blockGap)
	}
	// Forget the layouts of blocks that are gone.
	if len(e.layouts) > 2*n+16 {
		live := make(map[int64]bool, n)
		for _, b := range d.Blocks {
			live[b.ID] = true
		}
		for id := range e.layouts {
			if !live[id] {
				delete(e.layouts, id)
			}
		}
	}
}

// total is the height of every row, with the space above the first.
func (e *Editor) total() float32 {
	if len(e.tops) == 0 {
		return 0
	}
	n := len(e.tops) - 1
	return e.tops[n] + e.heights[n]
}

// tail is the space below the last block, a third of the window, so the
// last lines of a note can be read in the middle of the screen, and a press
// there puts the caret at the end.
func (e *Editor) tail() float32 {
	h := float32(600)
	if w := e.Window(); w != nil {
		h = w.ContentRect().Height
	}
	return max(h/3, e.px(40))
}

func (e *Editor) sizes(hint geom.Size) (minSize, prefSize, maxSize geom.Size) {
	w := hint.Width
	if w <= 0 {
		w = e.width()
	}
	e.measureAt(w)
	h := e.total() + e.tail()
	return geom.NewSize(e.px(minWidth), h), geom.NewSize(w, h), geom.NewSize(unison.DefaultMaxSize, h)
}

// minWidth is the narrowest the editor lays out at.
const minWidth = 200

// measureAt measures the rows at a width, unless they already were.
func (e *Editor) measureAt(w float32) {
	if w != e.laidWidth || len(e.tops) != len(e.Doc.Blocks) {
		e.laidWidth = w
		e.measure()
	}
}

// blockAt is the index of the row whose vertical extent holds y, the
// nearest row when y is between rows or past either end, or -1 for an
// empty note.
func (e *Editor) blockAt(y float32) int {
	best, bestD := -1, float32(1e9)
	for i := range e.tops {
		top, bottom := e.tops[i], e.tops[i]+e.heights[i]
		var dist float32
		switch {
		case y < top:
			dist = top - y
		case y > bottom:
			dist = y - bottom
		}
		if dist < bestD {
			best, bestD = i, dist
		}
		if top > y {
			break
		}
	}
	return best
}

// visibleRows is the range of rows that intersect r.
func (e *Editor) visibleRows(r geom.Rect) (first, last int) {
	first, last = len(e.tops), -1
	for i := range e.tops {
		if e.tops[i]+e.heights[i] < r.Y {
			continue
		}
		if e.tops[i] > r.Bottom() {
			break
		}
		first = min(first, i)
		last = i
	}
	return first, last
}

// caretRect is the caret's rectangle, when a block holds it.
func (e *Editor) caretRect() (geom.Rect, bool) {
	d := e.Doc
	i := d.Index(d.Caret.Block)
	if !d.Focused || i < 0 || i >= len(e.tops) {
		return geom.Rect{}, false
	}
	l := e.layout(i)
	x, top := l.caretAt(l.drawn(d.Caret.Off))
	o := e.textOrigin(i)
	return geom.NewRect(o.X+x, o.Y+top, e.px(caretWidth), l.pitch), true
}

// revealCaret scrolls the caret into view, with a little room around it.
func (e *Editor) revealCaret() {
	r, ok := e.caretRect()
	if !ok {
		return
	}
	room := e.px(8)
	e.ScrollRectIntoView(geom.NewRect(r.X, r.Y-room, r.Width, r.Height+2*room))
}

// blinkPeriod is how long the caret stays on, and then off.
const blinkPeriod = 530 * time.Millisecond

// startBlink makes the caret blink while the editor has the keyboard. It
// stays on with motion reduced, which is also how tests and screenshots see
// it.
func (e *Editor) startBlink() {
	if e.blinking || e.ui.Theme.MotionScale() == 0 {
		return
	}
	e.blinking = true
	var tick func()
	tick = func() {
		if !e.Focused() || !e.Doc.Focused || e.Window() == nil || e.ui.Theme.MotionScale() == 0 {
			e.blinking, e.blinkOff = false, false
			e.MarkForRedraw()
			return
		}
		off := int(time.Since(e.blinkFrom)/blinkPeriod)%2 == 1
		if off != e.blinkOff {
			e.blinkOff = off
			e.MarkForRedraw()
		}
		unison.InvokeTaskAfter(tick, blinkPeriod/4)
	}
	unison.InvokeTaskAfter(tick, blinkPeriod/4)
}
