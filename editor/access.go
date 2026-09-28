package editor

// What a screen reader is told. Each block in view is an editable text, as
// in Kvit (accessibility.md): named by its kind ("Heading 2 block"), with a
// to-do's state on the block itself, and carrying its text as drawn, its
// lines, its styled runs, and the caret and selection when it has them. The
// keyboard focus the editor holds is reported on the block with the caret.
// A screen reader can move the caret, select and replace text, and tick a
// to-do; the gutter's controls of the block under the pointer are buttons.

import (
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/check"
	"github.com/richardwilkes/unison/enums/role"
)

// partKey names a control of one block, as a virtual child's key.
type partKey struct {
	block int64
	part  gutterPart
}

// ProvideAccessibility describes the blocks in view, and the block with the
// caret wherever it is.
func (e *Editor) ProvideAccessibility(b *unison.AccessibilityBuilder) {
	d := e.Doc
	node := b.Node()
	node.Role = role.Group
	if node.Name == "" {
		node.Name = e.Accessibility.Name
	}
	visible := b.VisibleRect()
	first, last := e.visibleRows(visible)
	caret := -1
	if d.Focused {
		caret = d.Index(d.Caret.Block)
	}
	var focusID accessibility.NodeID
	selected := e.SelectedBlocks()
	for i := range d.Blocks {
		if (i < first || i > last) && i != caret {
			continue
		}
		if i >= len(e.tops) {
			break
		}
		id := e.describeBlock(b, i)
		if i == caret || (caret < 0 && len(selected) > 0 && d.Blocks[i].ID == selected[0]) {
			focusID = id
		}
	}
	if focusID != 0 {
		b.FocusChild(focusID)
	}
}

// describeBlock adds block i as a virtual child and returns its node id.
func (e *Editor) describeBlock(b *unison.AccessibilityBuilder, i int) accessibility.NodeID {
	d := e.Doc
	blk := &d.Blocks[i]
	id := b.AddVirtualChild(blk.ID, func(n *accessibility.Node) {
		n.Bounds = e.bodyRect(i)
		n.Selected = e.blockSel[blk.ID]
		n.Actions = n.Actions.With(accessibility.Focus).With(accessibility.ScrollIntoView)
		if blk.Kind == Divider {
			n.Role = role.Separator
			n.Name = "Divider"
			return
		}
		if e.describeDiagram(i, n) {
			return
		}
		n.Role = role.TextArea
		n.Name = blk.Kind.String() + " block"
		n.ReadOnly = d.ReadOnly
		if blk.Kind == Paragraph && blk.Text == "" {
			n.Placeholder = e.Placeholder
		}
		if blk.Kind == Todo {
			n.Checked = check.Off
			if blk.Checked {
				n.Checked = check.On
			}
			n.Actions = n.Actions.With(accessibility.Toggle)
		}
		n.Actions = n.Actions.With(accessibility.SetTextSelection).With(accessibility.ReplaceText).
			With(accessibility.ScrollRangeIntoView)
		n.Text = e.textInfo(i)
		n.Value = n.Text.Text
	})
	if id == 0 {
		return 0
	}
	if e.hover == blk.ID && e.drag == nil && !d.ReadOnly {
		for _, g := range gutterControls {
			r := e.gutterCellRect(i, g.part)
			b.AddVirtualChildOf(id, partKey{blk.ID, g.part}, func(n *accessibility.Node) {
				n.Role = role.Button
				n.Name = g.name
				n.Bounds = r
				n.Actions = n.Actions.With(accessibility.Press)
			})
		}
	}
	e.describeDiagramParts(b, id, i)
	if blk.Kind == Code && !e.diagramReads(i) {
		r := e.copyButton(i)
		b.AddVirtualChildOf(id, partKey{blk.ID, partCopy}, func(n *accessibility.Node) {
			n.Role = role.Button
			n.Name = "Copy code"
			n.Bounds = r
			n.Actions = n.Actions.With(accessibility.Press)
		})
	}
	if blk.Kind == Table && !d.ReadOnly {
		if g, ok := e.gridFor(i); ok {
			o := e.gridOrigin(i)
			if rowR, colR, ok := e.tableAddRects(i, g, o); ok {
				b.AddVirtualChildOf(id, partKey{blk.ID, partTableAddRow}, func(n *accessibility.Node) {
					n.Role = role.Button
					n.Name = "Add row"
					n.Bounds = rowR
					n.Actions = n.Actions.With(accessibility.Press)
				})
				b.AddVirtualChildOf(id, partKey{blk.ID, partTableAddCol}, func(n *accessibility.Node) {
					n.Role = role.Button
					n.Name = "Add column"
					n.Bounds = colR
					n.Actions = n.Actions.With(accessibility.Press)
				})
			}
		}
	}
	return id
}

// textInfo is block i's text for a screen reader, with its lines, styled
// runs, caret and selection, in the editor's coordinates.
//
// A typeset inline formula is drawn as one placeholder character, U+FFFC,
// which is what a reader would otherwise hear. Instead the text here holds
// the span's source, "$…$", so the formula is heard as its TeX. The lines,
// runs, caret and selection are mapped onto that text: a placeholder's width
// is shared across its source, and an action's offsets map back through
// accessDrawn before reaching the projection.
func (e *Editor) textInfo(i int) *accessibility.TextInfo {
	d := e.Doc
	blk := &d.Blocks[i]
	l := e.layout(i)
	o := e.textOrigin(i)
	acc, d2a, _ := e.accessExpansion(l)
	info := &accessibility.TextInfo{Text: string(acc), Multiline: l.lines() > 1}
	for li := 0; li < l.lines(); li++ {
		start, end, top, h := l.text.LineBounds(li)
		stops := l.text.LineStops(li)
		for k := range stops {
			stops[k] -= stops[0]
		}
		x, _, _ := l.text.CaretAt(start)
		astart, aend := d2a[start], d2a[end]
		adv := make([]float32, 0, aend-astart+1)
		adv = append(adv, 0)
		for dr := start; dr < end; dr++ {
			x0 := stops[dr-start]
			x1 := stops[dr+1-start]
			if bx := l.proj.boxes[dr]; bx != nil {
				n := len(l.proj.Src[bx.span.Start:bx.span.End])
				if n <= 0 {
					continue
				}
				for k := 1; k <= n; k++ {
					adv = append(adv, x0+(x1-x0)*float32(k)/float32(n))
				}
				continue
			}
			adv = append(adv, x1)
		}
		info.Lines = append(info.Lines, accessibility.Line{Advances: adv, Start: astart, End: aend,
			Bounds: geom.NewRect(o.X+x, o.Y+top, stops[len(stops)-1], h)})
	}
	// The lines partition the text: a line break belongs to the line it ends.
	for li := 0; li+1 < len(info.Lines); li++ {
		if next := info.Lines[li+1].Start; info.Lines[li].End < next {
			ln := &info.Lines[li]
			for ln.End < next {
				ln.Advances = append(ln.Advances, ln.Advances[len(ln.Advances)-1])
				ln.End++
			}
		}
	}

	info.Runs = e.accessRuns(blk, l, d2a)
	if d.Focused && d.Caret.Block == blk.ID {
		info.Caret = d2a[l.drawn(d.Caret.Off)]
		info.SelStart, info.SelEnd = info.Caret, info.Caret
	}
	if from, to, ok := d.SelectionIn(i); ok {
		info.SelStart, info.SelEnd = d2a[l.drawn(from)], d2a[l.drawn(to)]
		if d.Caret.Block == blk.ID {
			info.Caret = d2a[l.drawn(d.Caret.Off)]
		} else {
			info.Caret = info.SelEnd
		}
	}
	return info
}

// accessExpansion is block layout l's text for a screen reader: every
// typeset $…$ placeholder expanded to its source, "$…$", with the maps
// between drawn and accessible offsets. d2a has one entry per drawn offset,
// a2d one per accessible offset; inside an expansion every accessible offset
// maps to the placeholder's drawn offset, its end to the one after it.
// Without typeset math the accessible text is the drawn text and the maps
// are the identity.
func (e *Editor) accessExpansion(l *blockLayout) ([]rune, []int, []int) {
	disp := l.proj.Disp
	if len(l.proj.boxes) == 0 {
		d2a := make([]int, len(disp)+1)
		a2d := make([]int, len(disp)+1)
		for k := range d2a {
			d2a[k], a2d[k] = k, k
		}
		return disp, d2a, a2d
	}
	d2a := make([]int, len(disp)+1)
	a2d := []int{0}
	var acc []rune
	for dr := 0; dr < len(disp); dr++ {
		d2a[dr] = len(acc)
		if bx := l.proj.boxes[dr]; bx != nil {
			src := l.proj.Src[bx.span.Start:bx.span.End]
			for k, r := range src {
				acc = append(acc, r)
				if k+1 < len(src) {
					a2d = append(a2d, dr)
				} else {
					a2d = append(a2d, dr+1)
				}
			}
			continue
		}
		acc = append(acc, disp[dr])
		a2d = append(a2d, dr+1)
	}
	d2a[len(disp)] = len(acc)
	return acc, d2a, a2d
}

// accessDrawn maps an accessible offset back to a drawn one, clamping a
// stale offset to the text.
func accessDrawn(a2d []int, a int) int {
	if a < 0 {
		return 0
	}
	if a >= len(a2d) {
		return a2d[len(a2d)-1]
	}
	return a2d[a]
}

// accessRuns are the styled runs of a block's accessible text: the drawn
// runs mapped through d2a, so a placeholder's run covers its source.
func (e *Editor) accessRuns(blk *Block, l *blockLayout, d2a []int) []accessibility.TextRun {
	var runs []accessibility.TextRun
	fl := l.proj.flags
	for s := 0; s < len(fl); {
		t := s
		for t < len(fl) && fl[t] == fl[s] {
			t++
		}
		st := e.styleFor(fl[s], l.style)
		if blk.Kind == Todo && blk.Checked {
			st.Strike = true
		}
		runs = append(runs, accessibility.TextRun{
			Family:        e.ui.Fonts.ResolveFamily(st.Family),
			Start:         d2a[s],
			End:           d2a[t],
			Weight:        max(st.Weight, text.Regular),
			Size:          st.Size,
			Italic:        st.Italic,
			Underline:     st.Underline,
			Strikethrough: st.Strike,
			Monospace:     st.Family == e.monoFamily(),
		})
		s = t
	}
	return runs
}

// textRuns are the styled runs of a block's drawn text.
func (e *Editor) textRuns(blk *Block, l *blockLayout) []accessibility.TextRun {
	var runs []accessibility.TextRun
	fl := l.proj.flags
	for s := 0; s < len(fl); {
		t := s
		for t < len(fl) && fl[t] == fl[s] {
			t++
		}
		st := e.styleFor(fl[s], l.style)
		if blk.Kind == Todo && blk.Checked {
			st.Strike = true
		}
		runs = append(runs, accessibility.TextRun{
			Family:        e.ui.Fonts.ResolveFamily(st.Family),
			Start:         s,
			End:           t,
			Weight:        max(st.Weight, text.Regular),
			Size:          st.Size,
			Italic:        st.Italic,
			Underline:     st.Underline,
			Strikethrough: st.Strike,
			Monospace:     st.Family == e.monoFamily(),
		})
		s = t
	}
	return runs
}

// PerformAccessibilityAction carries out a screen reader's request on a
// block or on one of a block's controls.
func (e *Editor) PerformAccessibilityAction(req accessibility.ActionRequest) bool {
	d := e.Doc
	if pk, ok := req.Key.(partKey); ok {
		i := d.Index(pk.block)
		if i < 0 || req.Action != accessibility.Press {
			return false
		}
		if pk.part >= partDiagramFit {
			e.diagramAct(i, pk.part)
			e.changed()
			return true
		}
		switch pk.part {
		case partAdd:
			e.insertBelow(pk.block)
		case partDelete:
			d.DeleteBlocks([]int64{pk.block})
		case partMenu:
			e.openBlockMenu(pk.block, e.gutterCellRect(i, partMenu))
		case partHandle:
			e.clickHandle(pk.block, 0)
		case partCopy:
			unison.ClipboardSetText(d.Blocks[i].Text)
		case partTableAddRow:
			if !d.ReadOnly {
				if t := ParseTable(d.Blocks[i].Text); t.Valid {
					p := e.tableActive
					e.insertTableRowAfter(i, len(t.Rows)-1)
					if p != nil && p.blockID == d.Blocks[i].ID {
						e.activateTableCell(i, p.row, p.col, false)
					}
				}
			}
		case partTableAddCol:
			if !d.ReadOnly {
				if t := ParseTable(d.Blocks[i].Text); t.Valid {
					p := e.tableActive
					e.insertTableColumnAfter(i, len(t.Headers)-1)
					if p != nil && p.blockID == d.Blocks[i].ID {
						e.activateTableCell(i, p.row, p.col, false)
					}
				}
			}
		}
		e.changed()
		return true
	}
	id, ok := req.Key.(int64)
	if !ok {
		return false
	}
	i := d.Index(id)
	if i < 0 {
		return false
	}
	blk := &d.Blocks[i]
	switch req.Action {
	case accessibility.Focus:
		if !blk.Kind.IsText() {
			return false
		}
		e.FocusBlock(i, len(runes(blk.Text)))
		return true
	case accessibility.Toggle:
		d.ToggleTodo(id)
	case accessibility.SetTextSelection:
		l := e.layout(i)
		_, _, a2d := e.accessExpansion(l)
		e.clearBlockSel()
		d.Anchor = Pos{id, l.proj.forClick(min(accessDrawn(a2d, req.Start), len(l.proj.D2S)))}
		d.Caret = Pos{id, l.proj.forClick(min(accessDrawn(a2d, req.End), len(l.proj.D2S)))}
		d.Focused = true
		e.RequestFocus()
		e.touched()
	case accessibility.ReplaceText:
		l := e.layout(i)
		_, _, a2d := e.accessExpansion(l)
		from := l.proj.afterPrev(min(accessDrawn(a2d, req.Start), len(l.proj.D2S)))
		to := l.proj.beforeNext(min(accessDrawn(a2d, req.End), len(l.proj.D2S)))
		if to < from {
			to = from
		}
		d.Edit("replace", func() {
			r := runes(blk.Text)
			blk.Text = string(r[:from]) + req.Value + string(r[to:])
			d.SetCaret(id, from+len(runes(req.Value)))
		})
		e.touched()
	case accessibility.ScrollIntoView, accessibility.ScrollRangeIntoView:
		e.ScrollRectIntoView(e.rowRect(i))
	default:
		return false
	}
	e.changed()
	return true
}
