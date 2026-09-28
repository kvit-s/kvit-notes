package editor

// The pointer: placing the caret and selecting text, the gutter's controls,
// a to-do's check box and a code block's Copy button, and dragging a block
// by its handle.

import (
	"slices"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// dragState is a press on a block's handle: a click selects the block, and
// moving the pointer further than dragThreshold drags it.
type dragState struct {
	id      int64
	start   geom.Point
	active  bool
	before  DocState
	origIdx int
}

// mouseSel is a text selection being made by dragging.
type mouseSel struct {
	anchor Pos
	words  bool // a double-click's drag extends by words
}

func (e *Editor) mouseDown(where geom.Point, button, clickCount int, mods mod.Modifiers) bool {
	if button == unison.ButtonRight {
		// A right-click in text opens the text menu there, keeping a
		// selection it lands in; elsewhere on a row, the block menu.
		i := e.rowAt(where)
		if i < 0 {
			return false
		}
		if !e.Focused() {
			e.RequestFocus()
		}
		if e.boardShows(i) && e.boardPress(i, where, true) {
			return true
		}
		if e.diagramRightClick(i, where) {
			return true
		}
		at := geom.NewRect(where.X, where.Y, 0, 0)
		if where.X >= e.bodyLeft() && e.Doc.Blocks[i].Kind.IsText() {
			if pos, ok := e.posAtPoint(where); ok && !e.inSelection(pos) {
				e.clearBlockSel()
				e.Doc.SetCaret(pos.Block, pos.Off)
				e.changed()
			}
			e.openTextMenu(at)
			return true
		}
		e.openBlockMenu(e.Doc.Blocks[i].ID, at)
		return true
	}
	if button != unison.ButtonLeft {
		return false
	}
	if !e.Focused() {
		e.RequestFocus()
	}
	e.closeMenu()
	defer e.changed()
	d := e.Doc
	if i, part := e.partAt(where); part >= partDiagramFit {
		e.diagramClick(i, part, where, clickCount)
		return true
	}
	if i, part := e.partAt(where); part != partNone && !(d.ReadOnly && part != partHandle && part != partMenu && part != partCopy && part != partTocEntry && part != partEmbedOpen && part != partQueryRow) {
		b := &d.Blocks[i]
		switch part {
		case partAdd:
			e.insertBelow(b.ID)
		case partDelete:
			d.DeleteBlocks([]int64{b.ID})
			e.clearBlockSel()
		case partMenu:
			e.openBlockMenu(b.ID, e.gutterCellRect(i, partMenu))
		case partHandle:
			e.drag = &dragState{id: b.ID, start: where}
		case partCheck:
			d.ToggleTodo(b.ID)
		case partCopy:
			unison.ClipboardSetText(b.Text)
		case partFold:
			d.Edit("fold", func() { b.Checked = !b.Checked })
		case partCalloutType:
			_, icon, _, _ := e.calloutParts(i)
			e.ui.ShowMenuAt(e, icon, "Callout type", e.calloutTypeItems(b.ID))
		case partCalloutColor:
			_, _, _, dot := e.calloutParts(i)
			e.ui.ShowMenuAt(e, dot, "Callout color", e.calloutColorItems(b.ID))
		case partCalloutTitle:
			e.editCalloutTitle(i)
		case partLanguage:
			e.ui.ShowMenuAt(e, e.languageButton(i), "Code language", e.languageItems(b.ID))
		case partEmbedLoad:
			if _, ref, ok := e.embedCard(i); ok && e.LoadPreview != nil {
				e.LoadPreview(ref.Path)
			}
		case partEmbedOpen:
			if _, ref, ok := e.embedCard(i); ok && e.OpenURL != nil {
				e.OpenURL(ref.Path)
			}
		case partQueryRow:
			if k := e.queryRowAt(i, where); k >= 0 && e.OpenNote != nil {
				e.OpenNote(e.queryResult(i).paths[k])
			}
		case partTocEntry:
			if k := e.tocEntryAt(i, where); k >= 0 {
				e.goToHeading(d.tocEntries()[k].block)
			}
		}
		return true
	}
	if i := e.rowAt(where); i >= 0 && where.X >= e.bodyLeft() && e.boardShows(i) {
		// A press on a board acts on it and never puts the caret in its
		// Markdown.
		if !d.ReadOnly {
			e.boardPress(i, where, false)
		}
		return true
	}
	if n := len(e.tops); n > 0 && where.Y > e.tops[n-1]+e.heights[n-1] && !d.ReadOnly {
		// A press below the last block puts the caret at its end, in a new
		// paragraph when the last block is not an empty one.
		last := d.Blocks[n-1]
		if last.Kind.IsText() && last.Text == "" {
			d.SetCaret(last.ID, 0)
		} else {
			d.Edit("insert block", func() {
				nb := NewBlock(Paragraph, "")
				d.Blocks = append(d.Blocks, nb)
				d.SetCaret(nb.ID, 0)
			})
		}
		e.clearBlockSel()
		e.touched()
		return true
	}
	pos, ok := e.posAtPoint(where)
	if !ok {
		return true
	}
	if clickCount == 1 && !mods.ShiftDown() && e.followAt(pos, mods.OSMenuCommandDown()) {
		return true
	}
	e.clearBlockSel()
	d.Seal()
	e.hasGoal = false
	if mods.ShiftDown() && d.Focused {
		d.Caret = pos
		e.msel = &mouseSel{anchor: d.Anchor}
		e.touched()
		return true
	}
	switch {
	case clickCount == 2:
		r := runes(d.Block(pos.Block).Text)
		w0, w1 := wordAt(r, pos.Off)
		d.Anchor = Pos{pos.Block, w0}
		d.Caret = Pos{pos.Block, w1}
		d.Focused = true
		e.msel = &mouseSel{anchor: d.Anchor, words: true}
	case clickCount >= 3:
		d.Anchor = Pos{pos.Block, 0}
		d.Caret = Pos{pos.Block, len(runes(d.Block(pos.Block).Text))}
		d.Focused = true
	default:
		d.SetCaret(pos.Block, pos.Off)
		e.msel = &mouseSel{anchor: pos}
	}
	e.touched()
	return true
}

func (e *Editor) mouseDrag(where geom.Point, _ int, _ mod.Modifiers) bool {
	d := e.Doc
	switch {
	case e.diagPan != nil:
		e.diagramDrag(where)
		return true
	case e.cardDrag != nil:
		e.dragCard(where)
		e.MarkForRedraw()
		return true
	case e.drag != nil:
		dv := where.Sub(e.drag.start)
		limit := e.px(dragThreshold)
		if !e.drag.active && dv.X*dv.X+dv.Y*dv.Y > limit*limit && !d.ReadOnly {
			e.drag.active = true
			e.drag.before = d.Begin()
			e.drag.origIdx = d.Index(e.drag.id)
			d.Focused = false
			e.clearBlockSel()
		}
		if e.drag.active {
			e.dragStep(where.Y)
		}
	case e.msel != nil:
		if pos, ok := e.posAtPoint(where); ok {
			if e.msel.words {
				pos = e.extendWord(pos)
			}
			d.Caret = pos
			d.Focused = true
			e.touched()
		}
	default:
		return false
	}
	e.changed()
	return true
}

func (e *Editor) mouseUp(where geom.Point, _ int, mods mod.Modifiers) bool {
	d := e.Doc
	switch {
	case e.diagPan != nil:
		e.diagramRelease(where)
	case e.cardDrag != nil:
		e.dropCard()
	case e.drag != nil:
		if e.drag.active {
			if d.Index(e.drag.id) != e.drag.origIdx {
				d.Commit("move", e.drag.before)
			}
		} else {
			e.clickHandle(e.drag.id, mods)
		}
		e.drag = nil
	case e.msel != nil:
		e.msel = nil
	default:
		return false
	}
	e.mouseMove(where, mods)
	e.changed()
	return true
}

func (e *Editor) mouseMove(where geom.Point, _ mod.Modifiers) bool {
	hover, part, entry := int64(0), partNone, -1
	if i := e.rowAt(where); i >= 0 {
		hover = e.Doc.Blocks[i].ID
		_, part = e.partAt(where)
		if part == partTocEntry {
			entry = e.tocEntryAt(i, where)
		}
		if part == partQueryRow {
			e.queryHover = e.queryRowAt(i, where)
		}
	}
	if hover != e.hover || part != e.part || entry != e.tocHover {
		e.hover, e.part, e.tocHover = hover, part, entry
		e.MarkForRedraw()
	}
	return true
}

func (e *Editor) mouseExit() bool {
	if e.hover != 0 || e.part != partNone {
		e.hover, e.part = 0, partNone
		e.MarkForRedraw()
	}
	return true
}

func (e *Editor) cursor(where geom.Point) *unison.Cursor {
	if _, part := e.partAt(where); part != partNone {
		return unison.ArrowCursor()
	}
	if where.X >= e.bodyLeft() && e.rowAt(where) >= 0 {
		return unison.TextCursor()
	}
	return unison.ArrowCursor()
}

// rowAt is the row under a point, gutter included, or -1.
func (e *Editor) rowAt(where geom.Point) int {
	for i := range e.tops {
		if where.In(e.rowRect(i)) {
			return i
		}
	}
	return -1
}

// partAt is the control of a row under a point: one of the hovered row's
// gutter controls, a to-do's check box or a code block's Copy button.
func (e *Editor) partAt(where geom.Point) (int, gutterPart) {
	if e.drag != nil && e.drag.active {
		return -1, partNone
	}
	if i := e.Doc.Index(e.hover); i >= 0 && i < len(e.tops) {
		for _, g := range gutterControls {
			if where.In(e.gutterCellRect(i, g.part)) {
				return i, g.part
			}
		}
	}
	i := e.rowAt(where)
	if i < 0 {
		return -1, partNone
	}
	b := &e.Doc.Blocks[i]
	if p := e.diagramPartAt(i, where); p != partNone {
		return i, p
	}
	switch {
	case b.Kind == Todo && where.In(e.checkBox(i)):
		return i, partCheck
	case b.Kind == Code && where.In(e.copyButton(i)):
		return i, partCopy
	case b.Kind == Code && !e.tocShows(i) && where.In(e.languageButton(i)):
		return i, partLanguage
	case e.tocShows(i) && e.tocEntryAt(i, where) >= 0:
		return i, partTocEntry
	case e.queryShows(i) && e.queryRowAt(i, where) >= 0:
		return i, partQueryRow
	case b.Kind == Image || b.Kind == Media:
		if card, ref, ok := e.embedCard(i); ok {
			title, load := e.embedParts(card)
			switch {
			case where.In(load) && e.previews[ref.Path] == nil:
				return i, partEmbedLoad
			case where.In(title):
				return i, partEmbedOpen
			}
		}
	case b.Kind == Callout:
		chevron, icon, title, dot := e.calloutParts(i)
		switch {
		case where.In(chevron):
			return i, partFold
		case where.In(icon):
			return i, partCalloutType
		case where.In(dot):
			return i, partCalloutColor
		case where.In(title):
			return i, partCalloutTitle
		}
	}
	return i, partNone
}

// posAtPoint is the text position under a point: in the text row whose
// vertical extent holds it (the nearest text row when it is between rows or
// past either end), at the drawn offset nearest the point.
func (e *Editor) posAtPoint(p geom.Point) (Pos, bool) {
	d := e.Doc
	best, bestD := -1, float32(1e9)
	for i := range e.tops {
		if !d.Blocks[i].Kind.IsText() {
			continue
		}
		top, bottom := e.tops[i], e.tops[i]+e.heights[i]
		var dist float32
		switch {
		case p.Y < top:
			dist = top - p.Y
		case p.Y > bottom:
			dist = p.Y - bottom
		}
		if dist < bestD {
			best, bestD = i, dist
		}
	}
	if best < 0 {
		return Pos{}, false
	}
	if e.mathReads(best) {
		// A press on a typeset equation opens its TeX with the caret at
		// the end (MathBlock.qml, focusAtEnd).
		b := &d.Blocks[best]
		return Pos{b.ID, len(runes(b.Text))}, true
	}
	l := e.layout(best)
	o := e.textOrigin(best)
	y := p.Y - o.Y
	switch {
	case p.Y < e.tops[best]:
		y = 0
	case p.Y > e.tops[best]+e.heights[best]:
		y = l.height() - 1
	}
	return Pos{d.Blocks[best].ID, l.proj.forClick(l.hitTest(p.X-o.X, y))}, true
}

func (e *Editor) extendWord(pos Pos) Pos {
	d := e.Doc
	r := runes(d.Block(pos.Block).Text)
	w0, w1 := wordAt(r, pos.Off)
	ia, ip := d.Index(e.msel.anchor.Block), d.Index(pos.Block)
	if ip > ia || (ip == ia && pos.Off >= e.msel.anchor.Off) {
		return Pos{pos.Block, w1}
	}
	return Pos{pos.Block, w0}
}

// clickHandle is a press on a block's handle that did not become a drag: it
// selects the block, Ctrl toggles it and Shift extends the selection to it.
func (e *Editor) clickHandle(id int64, mods mod.Modifiers) {
	d := e.Doc
	d.Focused = false
	switch {
	case mods.OSMenuCommandDown():
		if e.blockSel[id] {
			delete(e.blockSel, id)
		} else {
			e.blockSel[id] = true
		}
	case mods.ShiftDown() && e.blockAnchor != 0 && d.Index(e.blockAnchor) >= 0:
		e.selectRange(e.blockAnchor, id)
	default:
		e.clearBlockSel()
		e.blockSel[id] = true
		e.blockAnchor = id
	}
}

// dragStep moves the dragged block past a neighbour once the pointer
// crosses that neighbour's middle, so the rows make room as it moves.
func (e *Editor) dragStep(y float32) {
	d := e.Doc
	i := d.Index(e.drag.id)
	if i > 0 && y < e.tops[i-1]+e.heights[i-1]/2 {
		d.MoveTo(e.drag.id, i-1)
		e.measure()
		return
	}
	if i < len(d.Blocks)-1 && y > e.tops[i+1]+e.heights[i+1]/2 {
		d.MoveTo(e.drag.id, i+1)
		e.measure()
	}
}

// insertBelow adds an empty paragraph under a block and opens the / menu in
// it, as the gutter's + does.
func (e *Editor) insertBelow(id int64) {
	d := e.Doc
	i := d.Index(id)
	d.Edit("insert block", func() {
		nb := NewBlock(Paragraph, "")
		d.Blocks = slices.Insert(d.Blocks, i+1, nb)
		d.SetCaret(nb.ID, 0)
	})
	e.clearBlockSel()
	e.measure()
	e.openSlashMenu(d.Caret.Block, false)
	e.touched()
}
