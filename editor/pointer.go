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
// moving the pointer further than dragThreshold drags it. A press on the
// handle of a selected block drags the whole selection, showing a gap
// indicator instead of live-moving one row (BlockDragController.qml).
type dragState struct {
	id      int64
	start   geom.Point
	active  bool
	before  DocState
	origIdx int
	multi   bool
	ids     []int64
	indexes []int
	gap     int
}

// mouseSel is a text selection being made by dragging.
type mouseSel struct {
	anchor Pos
	words  bool // a double-click's drag extends by words
}

func (e *Editor) mouseDown(where geom.Point, button, clickCount int, mods mod.Modifiers) bool {
	if e.yieldsPress(where) {
		return false
	}
	if e.light != nil {
		// A press anywhere closes the full-size picture.
		e.CloseLightbox()
		return true
	}
	if e.tableActive != nil && e.tableField == nil {
		// The live-cell field just closed on an outside press (its popup kept
		// the live cell so a + Row / + Column press still sees it). Keep it
		// for a press on the table's grid or its add buttons; any other press
		// leaves the table, so drop it as stale.
		keep := false
		if idx := e.Doc.Index(e.tableActive.blockID); idx >= 0 {
			if g, ok := e.tableShowsGrid(idx); ok {
				if e.tableAddAt(idx, g, where) != partNone {
					keep = true
				} else if _, _, ok := e.tableCellAt(idx, g, where); ok {
					keep = true
				} else if _, ok := e.tableGripAt(idx, g, where); ok {
					keep = true
				}
			}
		}
		if !keep {
			e.tableActive = nil
		}
	}
	if e.tableHeaderDoubleClick(where, button, clickCount) {
		return true
	}
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
		if g, ok := e.tableShowsGrid(i); ok {
			if row, col, ok := e.tableCellAt(i, g, where); ok {
				e.tableCellMenu(i, row, col, geom.NewRect(where.X, where.Y, 0, 0))
				return true
			}
		}
		at := geom.NewRect(where.X, where.Y, 0, 0)
		if where.X >= e.bodyLeft() && e.Doc.Blocks[i].Kind.IsText() {
			if pos, ok := e.posAtPoint(where); ok && !e.inSelection(pos) {
				e.clearBlockSel()
				e.Doc.SetCaret(pos.Block, pos.Off)
				e.changed()
			}
			// A press on a link opens the link menu, as in Kvit.
			if pos, ok := e.posAtPoint(where); ok {
				if b := e.Doc.Block(pos.Block); b != nil {
					if _, _, found := linkAt(b.Text, pos.Off); found {
						e.openLinkMenu(at, pos)
						return true
					}
				}
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
	if g := e.gapAt(where); g >= 0 && button == unison.ButtonLeft && !d.ReadOnly {
		e.placeGap(g)
		return true
	}
	if e.gapArmed >= 0 {
		// A press elsewhere ends the seam caret; normal handling places the
		// caret where pressed.
		e.dismissGap()
	}
	if e.tableSweep != nil {
		// A press outside the swept grid drops the rectangle; a press in
		// it re-anchors below.
		if i, part := e.partAt(where); part != partTableCell && part != partTableGrip {
			e.clearTableSweep()
		} else if i < 0 || e.Doc.Blocks[i].ID != e.tableSweep.blockID {
			e.clearTableSweep()
		}
	}
	if i, part := e.partAt(where); part >= partDiagramFit {
		e.diagramClick(i, part, where, clickCount)
		return true
	}
	if i, part := e.partAt(where); part != partNone && !(d.ReadOnly && part != partHandle && part != partMenu && part != partCopy && part != partTocEntry && part != partEmbedOpen && part != partQueryRow && part != partCodeBar) {
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
			if len(e.blockSel) > 0 && e.blockSel[b.ID] {
				ids := e.SelectedBlocks()
				idx := make([]int, 0, len(ids))
				for _, id := range ids {
					idx = append(idx, d.Index(id))
				}
				e.drag = &dragState{id: b.ID, start: where, multi: true, ids: ids, indexes: idx, gap: -1}
			} else {
				// Clearing waits for the drag to activate (a single drag)
				// or for the click to toggle or reselect, so Ctrl+click
				// keeps adding to the selection.
				e.drag = &dragState{id: b.ID, start: where}
			}
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
			// A press on an embed's title opens the page on release (a
			// single click); a drag, double- or triple-click selects the
			// card's own text instead (features.md 2.5).
			if e.drawnPress(i, where, clickCount) {
			}
		case partPictureLoad:
			if ref, ok, _ := e.pictureBlock(i); ok && e.RemotePolicy != nil {
				e.RemotePolicy.Allow(ref.Path)
				e.ForgetPicture(ref.Path)
			}
		case partTableCell:
			if g, ok := e.tableShowsGrid(i); ok {
				if row, col, ok := e.tableCellAt(i, g, where); ok {
					if row == -1 && clickCount == 2 {
						e.sortTableBy(i, col)
					} else if !d.ReadOnly {
						// Anchors a sweep; the release turns one cell
						// into an edit, a drag keeps its rectangle.
						e.beginTableSweep(i, row, col)
					}
				}
			}
		case partTableGrip:
			if g, ok := e.tableShowsGrid(i); ok {
				if col, ok := e.tableGripAt(i, g, where); ok {
					e.tableResize = &tableResizeState{blockID: b.ID, col: col, startX: where.X, width: g.cols[col]}
				}
			}
		case partTableAddRow:
			if !d.ReadOnly {
				if t := ParseTable(b.Text); t.Valid {
					p := e.tableActive
					e.insertTableRowAfter(i, len(t.Rows)-1)
					if p != nil && p.blockID == b.ID {
						e.activateTableCell(i, p.row, p.col, false)
					}
				}
			}
		case partTableAddCol:
			if !d.ReadOnly {
				if t := ParseTable(b.Text); t.Valid {
					p := e.tableActive
					e.insertTableColumnAfter(i, len(t.Headers)-1)
					if p != nil && p.blockID == b.ID {
						e.activateTableCell(i, p.row, p.col, false)
					}
				}
			}
		case partCodeBar:
			if track, ok := e.codeBarRect(i); ok && track.Width > 0 {
				e.scrollCodeTo(i, where.X)
				e.codeDrag = &codeDragState{blockID: b.ID, startX: where.X,
					scroll: e.codeScrollOf(b.ID), trackW: track.Width, content: e.codeContent(i)}
			}
		case partQueryRow:
			// A press on a query's row opens its note on release; a drag,
			// double- or triple-click selects the results' own text.
			if e.drawnPress(i, where, clickCount) {
			}
		case partTocEntry:
			// A press on a table of contents entry goes to its heading on
			// release; a drag, double- or triple-click selects the card's
			// own text.
			if e.drawnPress(i, where, clickCount) {
			}
		}
		return true
	}
	if i := e.rowAt(where); i >= 0 && where.X >= e.bodyLeft() {
		if g, ok := e.tableShowsGrid(i); ok {
			// A press in a grid acts on it and never puts the caret in
			// its Markdown.
			if e.tableMouse(i, g, where, button, clickCount, mods) {
				return true
			}
		}
	}
	if i := e.rowAt(where); i >= 0 && where.X >= e.bodyLeft() && e.boardShows(i) {
		// A press on a board acts on it and never puts the caret in its
		// Markdown.
		if !d.ReadOnly {
			e.boardPress(i, where, false)
		}
		return true
	}
	if i := e.rowAt(where); i >= 0 {
		// A press on a resolved picture opens it full-size; on a sound
		// or video it opens externally, as playing inline has no Go
		// toolkit behind it (features.md 1.2.8).
		if ref, ok, shows := e.pictureBlock(i); ok && !shows {
			if _, _, ok := e.embedCard(i); !ok && where.In(e.pictureRect(i)) {
				if ref.Media {
					if e.OpenURL != nil {
						e.OpenURL(ref.Path)
					}
					return true
				}
				if p := e.pictureFor(ref); p.img != nil {
					e.OpenLightbox(ref.Path, ref.Alt)
					return true
				}
			}
		}
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
	case e.drawnDrag != nil:
		i := e.rowAt(where)
		if i < 0 {
			i = e.Doc.Index(e.drawnDrag.block)
		}
		if i >= 0 {
			e.drawnDragStep(i, where)
		}
		return true
	case e.tableResize != nil:
		e.tableDragResize(where)
		return true
	case e.codeDrag != nil:
		if e.codeDrag.trackW > 0 && e.codeDrag.content > 0 {
			at := e.codeDrag.scroll + (where.X-e.codeDrag.startX)/e.codeDrag.trackW*e.codeDrag.content
			e.setCodeScroll(e.codeDrag.blockID, at)
		}
		return true
	case e.tableSweep != nil && e.tableSweep.sweeping:
		if i := e.Doc.Index(e.tableSweep.blockID); i >= 0 {
			if g, ok := e.gridFor(i); ok {
				if row, col, ok := e.tableCellAt(i, g, where); ok {
					e.extendTableSweep(i, row, col)
				}
			}
		}
		return true
	case e.diagPan != nil:
		e.diagramDrag(where)
		return true
	case e.cardDrag != nil:
		e.dragCard(where)
		e.MarkForRedraw()
		return true
	case e.colDrag != nil:
		e.dragColumn(where)
		e.MarkForRedraw()
		return true
	case e.drag != nil:
		dv := where.Sub(e.drag.start)
		limit := e.px(dragThreshold)
		if !e.drag.active && dv.X*dv.X+dv.Y*dv.Y > limit*limit && !d.ReadOnly {
			e.dismissGap()
			e.gapHover = -1
			e.drag.active = true
			e.drag.before = d.Begin()
			e.drag.origIdx = d.Index(e.drag.id)
			d.Focused = false
			if !e.drag.multi {
				e.clearBlockSel()
			}
		}
		if e.drag.active {
			if e.drag.multi {
				e.dragGapStep(where)
			} else {
				e.dragStep(where.Y)
			}
		}
	case e.msel != nil:
		// Dragging a selection past a code block's viewport scrolls it,
		// so a long line can be swept into view.
		if i := e.rowAt(where); i >= 0 && i < len(e.Doc.Blocks) && e.codeNoWrap(&e.Doc.Blocks[i]) {
			e.ensureCodePointVisible(i, where.X)
		}
		if pos, ok := e.posAtPoint(where); ok {
			if e.msel.words {
				pos = e.extendWord(pos)
			}
			d.Caret = pos
			d.Focused = true
			e.touched()
		}
	case e.tableResize == nil && e.drag == nil && e.diagPan == nil && e.cardDrag == nil:
		// A drag whose press never reached the grid (a live field's popup
		// took it closing the field): anchor at the cell under the pointer
		// so the drag still sweeps.
		if i := e.rowAt(where); i >= 0 && i < len(e.Doc.Blocks) && e.Doc.Blocks[i].Kind == Table {
			if g, ok := e.gridFor(i); ok {
				if row, col, ok := e.tableCellAt(i, g, where); ok && !d.ReadOnly {
					e.beginTableSweep(i, row, col)
					e.extendTableSweep(i, row, col)
				}
			}
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
	case e.drawnDrag != nil:
		i := e.rowAt(where)
		if i < 0 {
			i = e.Doc.Index(e.drawnDrag.block)
		}
		if i < 0 {
			e.drawnDrag = nil
			return true
		}
		e.drawnRelease(i, where)
	case e.tableResize != nil:
		e.tableEndResize()
	case e.codeDrag != nil:
		e.codeDrag = nil
	case e.tableSweep != nil && e.tableSweep.sweeping:
		i := e.Doc.Index(e.tableSweep.blockID)
		if i >= 0 {
			e.finishTableSweep(i)
		} else {
			e.tableSweep = nil
		}
	case e.diagPan != nil:
		e.diagramRelease(where)
	case e.cardDrag != nil:
		e.dropCard()
	case e.colDrag != nil:
		e.dropColumn()
	case e.drag != nil:
		if e.drag.active {
			if e.drag.multi {
				if e.drag.gap >= 0 {
					d.MoveBlocksTo(e.drag.indexes, e.drag.gap)
					// The selection follows the moved blocks by id.
				}
			} else if d.Index(e.drag.id) != e.drag.origIdx {
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
	g := e.gapAt(where)
	if g != e.gapHover {
		e.gapHover = g
		e.MarkForRedraw()
	}
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
	e.updateBoardHover(where)
	return true
}

func (e *Editor) mouseExit() bool {
	if e.hover != 0 || e.part != partNone {
		e.hover, e.part = 0, partNone
		e.MarkForRedraw()
	}
	if e.boardHoverID != 0 {
		e.boardHoverID, e.boardHoverCol, e.boardHoverIdx = 0, -1, -1
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
	if i := e.Doc.Index(e.hover); i >= 0 && i < len(e.tops) && !e.seams.noGutter() {
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
	case (b.Kind == Code || b.Kind == Raw) && e.codeBarAt(i, where):
		return i, partCodeBar
	case e.tocShows(i) && e.tocEntryAt(i, where) >= 0:
		return i, partTocEntry
	case e.queryShows(i) && e.queryRowAt(i, where) >= 0:
		return i, partQueryRow
	case b.Kind == Table:
		if g, ok := e.tableShowsGrid(i); ok {
			if p := e.tableAddAt(i, g, where); p != partNone {
				return i, p
			}
			if _, ok := e.tableGripAt(i, g, where); ok {
				return i, partTableGrip
			}
			if _, _, ok := e.tableCellAt(i, g, where); ok {
				return i, partTableCell
			}
		}
	case b.Kind == Image || b.Kind == Media:
		if card, ref, ok := e.embedCard(i); ok {
			title, load := e.embedParts(card)
			switch {
			case where.In(load) && e.previews[ref.Path] == nil:
				return i, partEmbedLoad
			case where.In(title):
				return i, partEmbedOpen
			}
		} else if ref, ok, _ := e.pictureBlock(i); ok {
			if _, needs := e.pictureNeedsApproval(ref); needs {
				if where.In(e.pictureRect(i)) {
					return i, partPictureLoad
				}
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
