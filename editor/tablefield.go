package editor

// The live cell's field: one text area over the grid cell, as the board's
// card editor opens one over a card. Enter moves down the column,
// Shift+Enter breaks the cell's line, Tab walks the grid, Ctrl+Enter leaves
// for a new block below, and Escape leaves the cell as it was. The field
// has the math typing aids a block has; every close that keeps text writes
// one undo step.

import (
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// tableFieldState is the open live-cell field, if any.
func (e *Editor) openTableField(i, row, col int, atStart bool) {
	w := e.ui.WindowOf(e)
	if w == nil || e.Doc.ReadOnly || i < 0 || i >= len(e.Doc.Blocks) {
		return
	}
	id := e.Doc.Blocks[i].ID
	g, ok := e.gridFor(i)
	if !ok || col < 0 || col >= len(g.cols) {
		return
	}
	if e.hideTableField != nil {
		hidePrev := e.hideTableField
		e.hideTableField = nil
		e.tableField = nil
		hidePrev()
	}
	text := TableCellValue(e.Doc.Blocks[i].Text, row, col)

	area := kvitui.NewTextArea(e.ui)
	field := &area.Field
	field.Label = "Table cell"
	field.SetText(text)
	edit := field.Edit()
	surface := fieldSurface{edit}
	track := &mathTrack{}
	sync := func() {
		e.syncMathOn(surface, track)
		e.MarkForRedraw()
	}

	e.tableField = field
	var hide func()
	done := func(keep bool, next func()) {
		if hide == nil {
			return
		}
		h := hide
		hide = nil
		e.hideTableField = nil
		e.tableField = nil
		h()
		if m := e.math.menu; m != nil && m.track == track {
			e.closeMathMenu()
		}
		if keep {
			e.writeTableCell(id, row, col, field.Text())
		}
		e.RequestFocus()
		e.changed()
		if next != nil {
			next()
		}
	}

	keys := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		ctrl, shift, alt := mods.OSMenuCommandDown(), mods.ShiftDown(), mods.OptionDown()
		if key == unison.KeyNumPadEnter {
			key = unison.KeyReturn
		}
		if m := e.math.menu; m != nil && m.track == track && !ctrl && !alt && e.mathMenuKey(key, shift) {
			sync()
			return true
		}
		if !alt && e.mathKeyOn(surface, track, key, ctrl, shift) {
			sync()
			return true
		}
		start, end := edit.Selection()
		full := field.Text()
		atEnd := func() bool { return start == end && end == len([]rune(full)) }
		atStartFn := func() bool { return start == end && start == 0 }
		firstLine, lastLine := caretFirstLastLine(full, start, end)
		switch {
		case key == unison.KeyEscape:
			done(false, nil)
			e.tableActive = nil
			e.changed()
			return true
		case key == unison.KeyReturn && ctrl:
			// Ctrl+Enter leaves for a new block below the table.
			p := e.tableActive
			done(true, nil)
			if p != nil {
				e.tableActive = nil
				e.leaveTableBelow(i)
			}
			return true
		case key == unison.KeyReturn && !shift:
			done(true, func() {
				e.tableActive = &tableCellPos{blockID: id, row: row, col: col}
				e.moveTableCellVertically(true)
			})
			return true
		case key == unison.KeyTab && shift:
			done(true, func() {
				e.tableActive = &tableCellPos{blockID: id, row: row, col: col}
				e.moveTableCell(false)
			})
			return true
		case key == unison.KeyTab:
			done(true, func() {
				e.tableActive = &tableCellPos{blockID: id, row: row, col: col}
				e.moveTableCell(true)
			})
			return true
		case key == unison.KeyUp && !ctrl && !alt && firstLine:
			done(true, func() {
				e.tableActive = &tableCellPos{blockID: id, row: row, col: col}
				e.moveTableCellVertically(false)
			})
			return true
		case key == unison.KeyDown && !ctrl && !alt && lastLine:
			done(true, func() {
				e.tableActive = &tableCellPos{blockID: id, row: row, col: col}
				e.moveTableCellVertically(true)
			})
			return true
		case key == unison.KeyLeft && !ctrl && !alt && atStartFn():
			done(true, func() {
				e.tableActive = &tableCellPos{blockID: id, row: row, col: col}
				e.moveTableCellHorizontally(false)
			})
			return true
		case key == unison.KeyRight && !ctrl && !alt && atEnd():
			done(true, func() {
				e.tableActive = &tableCellPos{blockID: id, row: row, col: col}
				e.moveTableCellHorizontally(true)
			})
			return true
		}
		used := keys != nil && keys(key, mods, repeat)
		sync()
		return used
	}

	runes := edit.RuneTypedCallback
	edit.RuneTypedCallback = func(ch rune) bool {
		if e.mathType(surface, track, string(ch)) {
			sync()
			return true
		}
		used := runes != nil && runes(ch)
		sync()
		return used
	}

	// A double press on a header cell sorts by its column. It has to be
	// caught here: the first press already opened this field over the cell,
	// so the second press lands in the field rather than reaching the grid.
	press := edit.MouseDownCallback
	edit.MouseDownCallback = func(where geom.Point, button, clicks int, mods mod.Modifiers) bool {
		if clicks == 2 && button == unison.ButtonLeft && row == -1 {
			done(false, nil)
			e.tableActive = nil
			e.sortTableBy(i, col)
			e.changed()
			return true
		}
		used := press != nil && press(where, button, clicks, mods)
		sync()
		return used
	}

	at := e.cellRect(i, g, row, col)
	o := e.gridOrigin(i)
	_ = o
	root := e.RectToRoot(at)
	hide = w.Show(&kvitui.Popup{Panel: field, Anchor: e,
		OnEscape: func() {
			done(false, nil)
			e.tableActive = nil
			e.changed()
		},
		OnPressOutside: func() {
			// The press still reaches the editor (Popup is not modal), which
			// handles the + Row / + Column buttons under a live cell. Keep
			// the live cell so that press sees it; any other outside press
			// clears it in mouseDown as stale.
			done(true, nil)
			e.changed()
		},
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			r := w.Content().RectFromRoot(root)
			// The field covers the cell it edits, never more: a taller
			// TextArea default would hang past the grid and cover the
			// hint under it. Longer text scrolls inside.
			return geom.NewRect(r.X, r.Y, max(r.Width, e.px(60)), r.Height)
		}})
	e.hideTableField = hide
	unison.InvokeTask(func() {
		field.Focus()
		if atStart {
			edit.SetSelection(0, 0)
		} else {
			n := len([]rune(field.Text()))
			edit.SetSelection(n, n)
		}
	})
}

// hideTableField hides the live-cell field without writing; retargeting
// hides it first and writes itself.
func (e *Editor) hideTableFieldFn() {
	if hide := e.hideTableField; hide != nil {
		e.hideTableField = nil
		e.tableField = nil
		hide()
	}
}

// leaveTableBelow ends the edit and inserts a paragraph below block i, the
// keyboard's way below a table.
func (e *Editor) leaveTableBelow(i int) {
	e.tableActive = nil
	e.changed()
	d := e.Doc
	nb := NewBlock(Paragraph, "")
	d.Edit("insert block", func() {
		d.Blocks = append(d.Blocks[:i+1:i+1], append([]Block{nb}, d.Blocks[i+1:]...)...)
		d.SetCaret(nb.ID, 0)
	})
	e.clearBlockSel()
	e.RequestFocus()
	e.touched()
	e.changed()
}

// caretFirstLastLine reports whether the selection's caret is on the first
// and last hard line of the field's text: Up leaves from the first,
// Down from the last, otherwise the field moves within.
func caretFirstLastLine(text string, start, end int) (first, last bool) {
	r := []rune(text)
	start = min(max(start, 0), len(r))
	end = min(max(end, 0), len(r))
	caret := end
	line, total := 0, 0
	for _, ch := range r {
		if ch == '\n' {
			total++
		}
	}
	for k := 0; k < caret; k++ {
		if r[k] == '\n' {
			line++
		}
	}
	_ = start
	return line == 0, line == total
}
