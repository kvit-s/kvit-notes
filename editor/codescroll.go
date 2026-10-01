package editor

// Horizontal scrolling for code blocks: long lines do not wrap, the panel
// clips them, and the text scrolls under a fixed gutter and a scrollbar at
// the panel's bottom. The caret follows along a long line, and the wheel and
// the scrollbar move it.
//
// The text layer wraps at MaxWidth and never wraps at 0 (text.Options), so
// a code block is laid out with no width. Its viewport is textWidth, the
// width other blocks wrap at; its content is the layout's width; its scroll
// offset is per block id, clamped to the content past the viewport.

import (
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// The scrollbar in design pixels (8 px, across the panel's bottom past the
// gutter).
const (
	codeBarH      = 8
	codeBarMargin = 2
	codeBarRadius = 4
)

// codeDragState is a press on a code block's scrollbar, until let go.
type codeDragState struct {
	blockID int64
	startX  float32
	scroll  float32
	trackW  float32
	content float32
}

// codeNoWrap reports whether a block's text is laid out without wrapping:
// code and raw source in its panel. Printing wraps again, so a PDF holds
// every line.
func (e *Editor) codeNoWrap(b *Block) bool {
	if e.Printing {
		return false
	}
	return b.Kind == Code || b.Kind == Raw
}

// codeViewport is the visible width of block i's code text: what other
// blocks wrap at.
func (e *Editor) codeViewport(i int) float32 {
	return e.textWidth(&e.Doc.Blocks[i])
}

// codeContent is the width of block i's unwrapped text.
func (e *Editor) codeContent(i int) float32 {
	w, _ := e.layout(i).text.Size()
	return w
}

// codeMaxScroll is how far block i's code can scroll past its viewport.
func (e *Editor) codeMaxScroll(i int) float32 {
	if i < 0 || i >= len(e.Doc.Blocks) || !e.codeNoWrap(&e.Doc.Blocks[i]) {
		return 0
	}
	return max(0, e.codeContent(i)-e.codeViewport(i))
}

// codeScrollOf is block id's horizontal scroll offset, clamped.
func (e *Editor) codeScrollOf(id int64) float32 {
	s, ok := e.codeScroll[id]
	if !ok || s <= 0 {
		return 0
	}
	i := e.Doc.Index(id)
	if i < 0 {
		return 0
	}
	if max := e.codeMaxScroll(i); s > max {
		return max
	}
	return s
}

// setCodeScroll moves block id's code to scroll, kept within its ends.
func (e *Editor) setCodeScroll(id int64, scroll float32) {
	i := e.Doc.Index(id)
	if i < 0 {
		return
	}
	mx := e.codeMaxScroll(i)
	scroll = min(max(scroll, 0), mx)
	if scroll == e.codeScrollOf(id) && e.codeScroll[id] == scroll {
		return
	}
	if e.codeScroll == nil {
		e.codeScroll = map[int64]float32{}
	}
	if scroll == 0 {
		delete(e.codeScroll, id)
	} else {
		e.codeScroll[id] = scroll
	}
	e.MarkForRedraw()
}

// codeScrollDX is how far block i's text shifts left: its scroll offset.
func (e *Editor) codeScrollDX(i int) float32 {
	if i < 0 || i >= len(e.Doc.Blocks) || !e.codeNoWrap(&e.Doc.Blocks[i]) {
		return 0
	}
	return e.codeScrollOf(e.Doc.Blocks[i].ID)
}

// codeViewportRect is the window block i's code text is clipped to: from
// the text's unscrolled origin, viewport wide and the layout tall.
func (e *Editor) codeViewportRect(i int) geom.Rect {
	b := &e.Doc.Blocks[i]
	x := e.bodyLeft() + e.textLeft(b)
	y := e.tops[i] + e.textTop(b)
	return geom.NewRect(x, y, e.codeViewport(i), e.layout(i).height())
}

// ensureCodeCaretVisible scrolls block i's code so the caret shows: past
// the right edge scrolls it in with room, back past the left edge scrolls
// it home.
func (e *Editor) ensureCodeCaretVisible(i int) {
	d := e.Doc
	if i < 0 || i >= len(d.Blocks) || !e.codeNoWrap(&d.Blocks[i]) {
		return
	}
	b := &d.Blocks[i]
	if !d.Focused || d.Caret.Block != b.ID {
		return
	}
	l := e.layout(i)
	x, _ := l.caretAt(l.drawn(d.Caret.Off))
	view := e.codeViewport(i)
	mx := e.codeMaxScroll(i)
	if mx <= 0 {
		if e.codeScrollOf(b.ID) != 0 {
			e.setCodeScroll(b.ID, 0)
		}
		return
	}
	scroll := e.codeScrollOf(b.ID)
	margin := e.px(12)
	room := e.px(24)
	switch {
	case x-scroll > view-margin:
		scroll = min(mx, x-view+room)
	case x-scroll < 0:
		scroll = max(0, x-e.px(4))
	default:
		return
	}
	e.setCodeScroll(b.ID, scroll)
}

// ensureCodePointVisible scrolls block i's code so a pointer at editor x
// shows, for dragging a selection past the viewport's edge.
func (e *Editor) ensureCodePointVisible(i int, x float32) {
	if i < 0 || i >= len(e.Doc.Blocks) || !e.codeNoWrap(&e.Doc.Blocks[i]) {
		return
	}
	b := &e.Doc.Blocks[i]
	view := e.codeViewport(i)
	mx := e.codeMaxScroll(i)
	if mx <= 0 {
		return
	}
	originX := e.bodyLeft() + e.textLeft(b)
	rel := x - originX
	scroll := e.codeScrollOf(b.ID)
	margin := e.px(12)
	switch {
	case rel-scroll > view-margin:
		scroll = min(mx, rel-view+margin)
	case rel-scroll < 0:
		scroll = max(0, rel-margin)
	default:
		return
	}
	e.setCodeScroll(b.ID, scroll)
}

// codeBarRect is the scrollbar track under block i's code, or false when
// there is nothing to scroll.
func (e *Editor) codeBarRect(i int) (geom.Rect, bool) {
	if i < 0 || i >= len(e.Doc.Blocks) || !e.codeNoWrap(&e.Doc.Blocks[i]) || e.Printing {
		return geom.Rect{}, false
	}
	if e.codeMaxScroll(i) <= 0 {
		return geom.Rect{}, false
	}
	p := e.codePanel(i)
	x := e.bodyLeft() + e.textLeft(&e.Doc.Blocks[i])
	w := p.Right() - e.px(codePadSide) - x
	if w <= 0 {
		return geom.Rect{}, false
	}
	h := e.px(codeBarH)
	return geom.NewRect(x, p.Bottom()-h-e.px(codeBarMargin), w, h), true
}

// codeThumbRect is the scrollbar thumb in track: the viewport's share of
// the content, at the scroll's share.
func (e *Editor) codeThumbRect(i int, track geom.Rect) geom.Rect {
	content := e.codeContent(i)
	if content <= 0 {
		return geom.Rect{}
	}
	view := e.codeViewport(i)
	w := track.Width * view / content
	x := track.X + track.Width*e.codeScrollOf(e.Doc.Blocks[i].ID)/content
	return geom.NewRect(x, track.Y, max(w, e.px(20)), track.Height)
}

// drawCodeScroll draws the scrollbar thumb under block i's code, when its
// long line runs past the panel: in the border colour, strong under the
// pointer or while dragged, with no track of its own since the panel is
// already its ground.
func (e *Editor) drawCodeScroll(gc *unison.Canvas, i int) {
	track, ok := e.codeBarRect(i)
	if !ok {
		return
	}
	thumb := e.codeThumbRect(i, track)
	if thumb.Empty() {
		return
	}
	t := e.tok()
	id := e.Doc.Blocks[i].ID
	ink := t.Border
	if (e.hover == id && e.part == partCodeBar) || (e.codeDrag != nil && e.codeDrag.blockID == id) {
		ink = t.BorderStrong
	}
	e.fillRound(gc, thumb, e.px(codeBarRadius), ink)
}

// codeWheel scrolls block i's code from a wheel turn: a horizontal turn, or
// a vertical one with Shift held, as a horizontal one. It reports whether
// the turn moved the code, keeping it from the region the editor is in.
func (e *Editor) codeWheel(i int, delta geom.Point, mods mod.Modifiers) bool {
	if i < 0 || i >= len(e.Doc.Blocks) || !e.codeNoWrap(&e.Doc.Blocks[i]) {
		return false
	}
	if e.codeMaxScroll(i) <= 0 {
		return false
	}
	dx := delta.X
	if dx == 0 && mods.ShiftDown() {
		dx = delta.Y
	}
	if dx == 0 {
		return false
	}
	id := e.Doc.Blocks[i].ID
	before := e.codeScrollOf(id)
	e.setCodeScroll(id, before-dx*unison.MouseWheelMultiplier)
	return e.codeScrollOf(id) != before
}

// wheel scrolls what is under the pointer: a code block's long line
// across, a zoomed diagram in its window, and everything else in the
// region the editor is in.
func (e *Editor) wheel(where, delta geom.Point, mods mod.Modifiers) bool {
	if i := e.rowAt(where); i >= 0 {
		if e.codeWheel(i, delta, mods) {
			return true
		}
	}
	return e.diagramWheel(where, delta, mods)
}

// codeBarAt is block i when where is on its scrollbar.
func (e *Editor) codeBarAt(i int, where geom.Point) bool {
	track, ok := e.codeBarRect(i)
	return ok && where.In(track.Inset(geom.NewUniformInsets(-e.px(4))))
}

// scrollCodeTo maps a press at x in block i's scrollbar track to a scroll
// offset, centring the thumb on the press as a scrollbar does.
func (e *Editor) scrollCodeTo(i int, x float32) {
	track, ok := e.codeBarRect(i)
	if !ok {
		return
	}
	content := e.codeContent(i)
	if content <= 0 || track.Width <= 0 {
		return
	}
	thumbW := track.Width * e.codeViewport(i) / content
	at := (x - track.X - thumbW/2) / track.Width * content
	e.setCodeScroll(e.Doc.Blocks[i].ID, at)
}
