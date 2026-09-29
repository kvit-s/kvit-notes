package editor

// What a program may draw inside the editor without changing the document
// (the Qt core's src/application/documentdecorations.h): panels of its own
// between blocks and in a column beside them, and washes and outlines over
// runs of characters. Kvit Works draws a conversation after the block it is
// about, a Run button beside a shell fence, its comments as washes over the
// words they are on, and the passage a conversation is anchored to as an
// outline.
//
// Nothing here reaches the document: a block's index, its Markdown and the
// undo history are the same with and without decorations. With none
// registered the editor lays out and draws exactly as it does without this
// file.

import (
	"math"
	"strconv"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// SpanStyle is how a marked run of characters is painted. The two compose: a
// wash is behind the characters and an outline around them, one box for each
// line the run crosses.
type SpanStyle int

// The span styles (Qt DocumentDecorations::SpanStyle).
const (
	SpanWash SpanStyle = 1 << iota
	SpanOutline
)

// DecorationPanel is what the panel of a container or a margin item may
// implement to be told which block it is drawn beside: the block a container
// follows, the block a margin item sits by (Qt's decorationBlock property).
type DecorationPanel interface {
	SetDecorationBlock(block int)
}

// DecorationEntry is one container or margin item, as ContainersAfter and
// MarginItemsForBlock list them.
type DecorationEntry struct {
	ID, Owner string
	// Block is the block a container follows, or the block a margin item
	// is beside.
	Block int
	// Line is a margin item's line within its block, counted from 0.
	Line  int
	Panel unison.Paneler
}

// SpanEntry is one marked run of characters, as SpansForBlock lists them.
// Start and Length are in the block's display text: its Markdown with every
// inline marker taken out (DisplayPosition).
type SpanEntry struct {
	ID, Owner            string
	Block, Start, Length int
	Style                SpanStyle
	Color                unison.Color
}

// Decorations is one editor's decorations. Every entry has an owner, the
// name of the program part that made it, so RemoveAll takes back one part's
// entries and leaves another's; and an id, which later moves and removals
// name and which it keeps across a move.
type Decorations struct {
	e          *Editor
	containers []*decoration
	margins    []*decoration
	spans      []*decoration
	next       int
	revision   int
	reserved   bool
	listeners  []func()
}

type decoration struct {
	id, owner string
	// block is the block a container follows, a margin item is beside or a
	// span marks.
	block               int
	line, start, length int
	panel               *unison.Panel
	given               unison.Paneler // the panel as the caller gave it
	style               SpanStyle
	color               unison.Color
	height              float32 // a container's height at the last measure
	lastBlock           int     // the block the panel was last told of
}

// marginColumnEms is the reserved column's width, in the reading font's
// size (Qt marginColumnEms).
const marginColumnEms = 1.5

// Decorations is the editor's decoration registry, made on first use.
func (e *Editor) Decorations() *Decorations {
	if e.seams == nil {
		e.seams = &seams{}
	}
	if e.seams.deco == nil {
		e.seams.deco = &Decorations{e: e}
	}
	return e.seams.deco
}

// Revision is bumped whenever any entry is added, moved, restyled or
// removed, or the margin column is reserved or given up.
func (d *Decorations) Revision() int { return d.revision }

// OnChange adds f to what runs after every change Revision counts.
func (d *Decorations) OnChange(f func()) { d.listeners = append(d.listeners, f) }

// Active reports whether anything is registered at all.
func (d *Decorations) Active() bool {
	return len(d.containers)+len(d.margins)+len(d.spans) > 0
}

// HasSpans reports whether any run of characters is marked.
func (d *Decorations) HasSpans() bool { return len(d.spans) > 0 }

// SetMarginColumnReserved reserves the column at the right of the rows that
// margin items are drawn in, or gives it up. A program reserves it once,
// when it starts, rather than with its first item, so the text never moves
// sideways under the reader when one appears.
func (d *Decorations) SetMarginColumnReserved(reserved bool) {
	if d.reserved == reserved {
		return
	}
	d.reserved = reserved
	d.e.laidWidth = 0
	d.bump(true)
}

// MarginColumnReserved reports whether the margin column is reserved.
func (d *Decorations) MarginColumnReserved() bool { return d.reserved }

// MarginColumnEms is the margin column's width in the reading font's size.
func (d *Decorations) MarginColumnEms() float32 { return marginColumnEms }

// MarginColumnWidth is the margin column's width in pixels, 0 while it is
// not reserved.
func (d *Decorations) MarginColumnWidth() float32 {
	if d == nil || !d.reserved {
		return 0
	}
	return float32(math.Round(float64(d.e.ui.Typography.BaseSize()) * marginColumnEms))
}

func (d *Decorations) newID(prefix string) string {
	d.next++
	return prefix + "-" + strconv.Itoa(d.next)
}

func (d *Decorations) bump(layout bool) {
	d.revision++
	if layout {
		d.e.relayout()
	} else {
		d.e.MarkForRedraw()
	}
	for _, f := range d.listeners {
		f()
	}
}

// AddContainer draws panel after block afterBlock, as wide as the rows and
// as tall as the panel's preferred height at that width, pushing the blocks
// below it down. Two containers after one block are stacked in the order
// they were added. A block the document does not have draws nothing until
// the container is moved to one it has. A nil panel registers nothing and
// returns "".
func (d *Decorations) AddContainer(owner string, afterBlock int, panel unison.Paneler) string {
	if panel == nil {
		return ""
	}
	p := panel.AsPanel()
	c := &decoration{id: d.newID("container"), owner: owner, block: afterBlock, panel: p, given: panel, lastBlock: -1}
	d.containers = append(d.containers, c)
	d.e.AddChild(p)
	d.bump(true)
	return c.id
}

// SetContainerBlock moves a container to after another block.
func (d *Decorations) SetContainerBlock(id string, afterBlock int) bool {
	c := find(d.containers, id)
	if c == nil {
		return false
	}
	if c.block != afterBlock {
		c.block = afterBlock
		d.bump(true)
	}
	return true
}

// RemoveContainer takes a container and its panel out of the editor.
func (d *Decorations) RemoveContainer(id string) bool {
	return d.remove(&d.containers, id, true)
}

// AddMarginItem draws panel in the margin column beside line line of block
// block, as wide as the column and as tall as the line. Lines are counted
// from 0 within the block's drawn text; a line past its last one is beside
// the last, and a block with no text of its own has one line, its whole
// row, and a line below 0 is line 0. The item shows only while the column
// is reserved.
func (d *Decorations) AddMarginItem(owner string, block, line int, panel unison.Paneler) string {
	if panel == nil {
		return ""
	}
	p := panel.AsPanel()
	m := &decoration{id: d.newID("margin"), owner: owner, block: block, line: max(0, line), panel: p, given: panel, lastBlock: -1}
	d.margins = append(d.margins, m)
	d.e.AddChild(p)
	d.bump(true)
	return m.id
}

// SetMarginItemPosition moves a margin item beside another block or line.
func (d *Decorations) SetMarginItemPosition(id string, block, line int) bool {
	m := find(d.margins, id)
	if m == nil {
		return false
	}
	if line = max(0, line); m.block != block || m.line != line {
		m.block, m.line = block, line
		d.bump(true)
	}
	return true
}

// RemoveMarginItem takes a margin item and its panel out of the editor.
func (d *Decorations) RemoveMarginItem(id string) bool {
	return d.remove(&d.margins, id, true)
}

// AddSpan marks length characters of block block's display text from start,
// painted in color through the channels style names. A run past the end of
// the text is cut at its end, and a block with no text of its own shows
// nothing. An empty style, a colour with no opacity or a length below 1
// registers nothing and returns "".
func (d *Decorations) AddSpan(owner string, block, start, length int, style SpanStyle, color unison.Color) string {
	if style&(SpanWash|SpanOutline) == 0 || color.Alpha() == 0 || length <= 0 {
		return ""
	}
	s := &decoration{id: d.newID("span"), owner: owner, block: block, start: start, length: length, style: style, color: color}
	d.spans = append(d.spans, s)
	d.bump(false)
	return s.id
}

// SetSpanRange moves a span. Its length may go to 0, which is what a mark
// whose words were just deleted is until it is placed again.
func (d *Decorations) SetSpanRange(id string, block, start, length int) bool {
	s := find(d.spans, id)
	if s == nil {
		return false
	}
	if s.block != block || s.start != start || s.length != max(0, length) {
		s.block, s.start, s.length = block, start, max(0, length)
		d.bump(false)
	}
	return true
}

// SetSpanStyle repaints a span.
func (d *Decorations) SetSpanStyle(id string, style SpanStyle, color unison.Color) bool {
	s := find(d.spans, id)
	if s == nil || style&(SpanWash|SpanOutline) == 0 || color.Alpha() == 0 {
		return false
	}
	if s.style != style || s.color != color {
		s.style, s.color = style, color
		d.bump(false)
	}
	return true
}

// RemoveSpan unmarks a span's characters.
func (d *Decorations) RemoveSpan(id string) bool {
	return d.remove(&d.spans, id, false)
}

// RemoveAll takes back every entry owner made.
func (d *Decorations) RemoveAll(owner string) {
	changed, layout := false, false
	for _, list := range []*[]*decoration{&d.containers, &d.margins, &d.spans} {
		kept := (*list)[:0]
		for _, x := range *list {
			if x.owner != owner {
				kept = append(kept, x)
				continue
			}
			changed = true
			if x.panel != nil {
				x.panel.RemoveFromParent()
				layout = true
			}
		}
		clear((*list)[len(kept):])
		*list = kept
	}
	if changed {
		d.bump(layout)
	}
}

// Clear takes back every entry.
func (d *Decorations) Clear() {
	if !d.Active() {
		return
	}
	for _, x := range append(append([]*decoration{}, d.containers...), d.margins...) {
		x.panel.RemoveFromParent()
	}
	d.containers, d.margins, d.spans = nil, nil, nil
	d.bump(true)
}

func (d *Decorations) remove(list *[]*decoration, id string, layout bool) bool {
	for k, x := range *list {
		if x.id != id {
			continue
		}
		if x.panel != nil {
			x.panel.RemoveFromParent()
		}
		*list = append((*list)[:k:k], (*list)[k+1:]...)
		d.bump(layout)
		return true
	}
	return false
}

func find(list []*decoration, id string) *decoration {
	for _, x := range list {
		if x.id == id {
			return x
		}
	}
	return nil
}

// ContainerCount, MarginItemCount and SpanCount count the entries.
func (d *Decorations) ContainerCount() int  { return len(d.containers) }
func (d *Decorations) MarginItemCount() int { return len(d.margins) }
func (d *Decorations) SpanCount() int       { return len(d.spans) }

// ContainersAfter lists the containers after block, in the order they were
// added, which is the order they are stacked in.
func (d *Decorations) ContainersAfter(block int) []DecorationEntry {
	var out []DecorationEntry
	for _, c := range d.containers {
		if c.block == block {
			out = append(out, DecorationEntry{ID: c.id, Owner: c.owner, Block: c.block, Panel: c.given})
		}
	}
	return out
}

// MarginItemsForBlock lists the margin items beside block, in the order they
// were added.
func (d *Decorations) MarginItemsForBlock(block int) []DecorationEntry {
	var out []DecorationEntry
	for _, m := range d.margins {
		if m.block == block {
			out = append(out, DecorationEntry{ID: m.id, Owner: m.owner, Block: m.block, Line: m.line, Panel: m.given})
		}
	}
	return out
}

// SpansForBlock lists the spans marking block, in the order they were
// added, which is the order they are painted in.
func (d *Decorations) SpansForBlock(block int) []SpanEntry {
	var out []SpanEntry
	for _, s := range d.spans {
		if s.block == block {
			out = append(out, SpanEntry{ID: s.id, Owner: s.owner, Block: s.block, Start: s.start, Length: s.length,
				Style: s.style, Color: s.color})
		}
	}
	return out
}

// Geometry. Every rectangle is in the editor's own coordinates, which are
// the coordinates of the content a scrolling region moves; an empty
// rectangle is the answer for an index or id that does not resolve.

// BlockGeometry is block's row, with the containers drawn after it.
func (d *Decorations) BlockGeometry(block int) geom.Rect {
	e := d.e
	if block < 0 || block >= len(e.tops) {
		return geom.Rect{}
	}
	r := e.rowRect(block)
	r.Height += e.decorationSpace(block)
	return r
}

// ContainerGeometry is where a container is drawn.
func (d *Decorations) ContainerGeometry(id string) geom.Rect {
	c := find(d.containers, id)
	if c == nil || c.panel.Hidden || c.panel.Parent() != d.e.AsPanel() {
		return geom.Rect{}
	}
	return c.panel.FrameRect()
}

// LineGeometry is visual line line of block's drawn text: the line's top
// and height, across the text's width. A line past the last is the last;
// a block with no text of its own answers its whole row.
func (d *Decorations) LineGeometry(block, line int) geom.Rect {
	e := d.e
	if block < 0 || block >= len(e.tops) {
		return geom.Rect{}
	}
	if !e.rowDrawsText(block) {
		return e.bodyRect(block)
	}
	l := e.layout(block)
	o := e.textOrigin(block)
	n := l.lines()
	if n == 0 {
		return e.bodyRect(block)
	}
	line = min(max(line, 0), n-1)
	_, _, top, h := l.text.LineBounds(line)
	return geom.NewRect(o.X, o.Y+top, l.width, h)
}

// SpanRects is where a span is painted: one rectangle for each line it
// crosses. It is empty for a span on no text.
func (d *Decorations) SpanRects(id string) []geom.Rect {
	s := find(d.spans, id)
	if s == nil {
		return nil
	}
	return d.e.displayRangeRects(s.block, s.start, s.length)
}

// Relayout measures the containers again. A container's height is read at
// every layout and when the editor draws, so this is only for a host that
// needs the new geometry before the next frame.
func (d *Decorations) Relayout() { d.e.relayout() }

// holds reports whether p is one of the decorations' panels or inside one.
func (d *Decorations) holds(p *unison.Panel) bool {
	for a := p; a != nil && a != d.e.AsPanel(); a = a.Parent() {
		for _, list := range [][]*decoration{d.containers, d.margins} {
			for _, x := range list {
				if x.panel == a {
					return true
				}
			}
		}
	}
	return false
}

// panelAt is the decoration panel under a point in the editor, or nil.
func (d *Decorations) panelAt(where geom.Point) *unison.Panel {
	for _, list := range [][]*decoration{d.containers, d.margins} {
		for _, x := range list {
			if !x.panel.Hidden && where.In(x.panel.FrameRect()) {
				return x.panel
			}
		}
	}
	return nil
}

// Laying the decorations out.

// containerWidth is how wide a container is: the rows' width, the gutter
// included.
func (e *Editor) containerWidth() float32 { return max(0, e.width()-2*e.side()) }

// measureDecorations measures the containers after block i at the rows'
// width, as the rows are measured, and answers the height they take.
func (e *Editor) measureDecorations(i int) float32 {
	if e.seams == nil || e.seams.deco == nil || len(e.seams.deco.containers) == 0 {
		return 0
	}
	var h float32
	w := e.containerWidth()
	for _, c := range e.seams.deco.containers {
		if c.block != i {
			continue
		}
		_, pref, _ := c.panel.Sizes(geom.NewSize(w, 0))
		c.height = pref.Height
		h += c.height
	}
	return h
}

// decorationSpace is the height the containers after block i took when the
// rows were last measured, which is where the rows below them are. It
// measures nothing: a container that has grown since keeps its old height
// here until the rows are measured again, so the question never hides the
// growth from the check that measures them again (drawOver).
func (e *Editor) decorationSpace(i int) float32 {
	if e.seams == nil || e.seams.deco == nil || len(e.seams.deco.containers) == 0 {
		return 0
	}
	var h float32
	for _, c := range e.seams.deco.containers {
		if c.block == i {
			h += c.height
		}
	}
	return h
}

// place puts each container under its block and each margin item beside its
// line, and hides those whose block the document does not have.
func (d *Decorations) place() {
	e := d.e
	n := len(e.tops)
	w := e.containerWidth()
	below := map[int]float32{}
	for _, c := range d.containers {
		if c.block < 0 || c.block >= n {
			c.panel.Hidden = true
			continue
		}
		c.panel.Hidden = false
		y := e.tops[c.block] + e.heights[c.block] + below[c.block]
		below[c.block] += c.height
		c.panel.SetFrameRect(geom.NewRect(e.side(), y, w, c.height))
		d.tell(c)
	}
	colW := d.MarginColumnWidth()
	for _, m := range d.margins {
		if !d.reserved || m.block < 0 || m.block >= n {
			m.panel.Hidden = true
			continue
		}
		m.panel.Hidden = false
		line := d.LineGeometry(m.block, m.line)
		m.panel.SetFrameRect(geom.NewRect(e.laidWidth, line.Y, colW, line.Height))
		d.tell(m)
	}
}

// tell gives a panel that asks for it the block it is drawn beside.
func (d *Decorations) tell(x *decoration) {
	if x.lastBlock == x.block {
		return
	}
	x.lastBlock = x.block
	if p, ok := x.panel.Self.(DecorationPanel); ok {
		p.SetDecorationBlock(x.block)
	}
}

// fullWidth is the width the editor was last measured at, the margin column
// included: what measureAt takes, where width is the rows' width without it.
func (e *Editor) fullWidth() float32 {
	if e.laidWidth > 0 {
		return e.laidWidth + e.marginColumn()
	}
	return e.width()
}

// marginColumn is the width taken from the rows for the margin column.
func (e *Editor) marginColumn() float32 {
	if e.seams == nil {
		return 0
	}
	return e.seams.deco.MarginColumnWidth()
}

// Drawing the marked runs.

// rowDrawsText reports whether block i's row draws its text, which is what
// a span can mark: a picture, a divider, a drawn table, board, diagram or
// equation and a closed callout draw none.
func (e *Editor) rowDrawsText(i int) bool {
	b := &e.Doc.Blocks[i]
	switch b.Kind {
	case Divider:
		return false
	case Code, Raw, Table:
		if _, ok := e.tableShowsGrid(i); ok {
			return false
		}
		if e.tocShows(i) || e.boardShows(i) || e.queryShows(i) || e.diagramReads(i) {
			return false
		}
	case Math:
		if e.mathReads(i) {
			return false
		}
	case Callout:
		if !e.calloutOpen(b) {
			return false
		}
	}
	if _, ok, shows := e.pictureBlock(i); ok && !shows {
		return false
	}
	return true
}

// displayRangeRects is where length characters of block i's display text
// from start are drawn: one rectangle for each line they cross.
func (e *Editor) displayRangeRects(i, start, length int) []geom.Rect {
	if i < 0 || i >= len(e.tops) || length <= 0 || !e.rowDrawsText(i) {
		return nil
	}
	b := &e.Doc.Blocks[i]
	from, to := displayToSource(b, start, start+length)
	l := e.layout(i)
	a, c := l.drawn(from), l.drawn(to)
	if a >= c {
		return nil
	}
	o := e.textOrigin(i)
	var out []geom.Rect
	for k := 0; k < l.lines(); k++ {
		ls, le, top, h := l.text.LineBounds(k)
		s, t := max(a, ls), min(c, le)
		if s >= t {
			continue
		}
		stops := l.text.LineStops(k)
		x0, x1 := stops[s-ls], stops[t-ls]
		if x1 < x0 {
			x0, x1 = x1, x0
		}
		out = append(out, geom.NewRect(o.X+x0, o.Y+top, x1-x0, h))
	}
	return out
}

// drawSpansBehind paints the washes marking block i's text, before the text
// is drawn over them.
func (e *Editor) drawSpansBehind(gc *unison.Canvas, i int) {
	if e.seams == nil || e.seams.deco == nil || len(e.seams.deco.spans) == 0 {
		return
	}
	for _, s := range e.seams.deco.spans {
		if s.block != i || s.style&SpanWash == 0 {
			continue
		}
		for _, r := range e.displayRangeRects(i, s.start, s.length) {
			gc.DrawRect(r, s.color.Paint(gc, r, paintstyle.Fill))
		}
	}
}

// drawOver draws what goes over the rows in view: the outlines of marked
// runs, the ring around a picture that holds the keyboard, and, when a
// container's height has changed since the rows were measured, asks for them
// to be measured again.
func (e *Editor) drawOver(gc *unison.Canvas, first, last int) {
	if e.seams == nil {
		return
	}
	if e.pictureHoldsKeyboard() && e.Focused() {
		if i := e.Doc.Index(e.Doc.Caret.Block); i >= 0 {
			e.stroke(gc, e.pictureRect(i).Inset(geom.NewUniformInsets(-e.px(2))), e.px(pictureRadius+2), e.px(2), e.tok().FocusRing)
		}
	}
	d := e.seams.deco
	if d == nil {
		return
	}
	for _, s := range d.spans {
		if s.block < first || s.block > last || s.style&SpanOutline == 0 {
			continue
		}
		for _, r := range e.displayRangeRects(s.block, s.start, s.length) {
			p := s.color.Paint(gc, r, paintstyle.Stroke)
			p.SetStrokeWidth(1)
			gc.DrawRoundedRect(r.Inset(geom.NewUniformInsets(0.5)), geom.NewSize(2, 2), p)
		}
	}
	w := e.containerWidth()
	for _, c := range d.containers {
		if c.panel.Hidden {
			continue
		}
		if _, pref, _ := c.panel.Sizes(geom.NewSize(w, 0)); pref.Height != c.height {
			e.relayoutSoon()
			break
		}
	}
}

// SpanColor is a theme colour at an opacity from 0 to 1, as a span is
// painted in: a wash is usually a faint one, an outline an opaque one.
func SpanColor(c palette.Color, opacity float32) unison.Color {
	return kvitui.Color(c).SetAlphaIntensity(opacity)
}
