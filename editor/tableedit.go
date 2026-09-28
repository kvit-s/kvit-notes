package editor

// Editing a table's cells in its grid (features.md 1.2.11, Kvit's
// TableBlock.qml): a press in a cell makes it live, opening one field over
// it; Tab walks the grid in reading order (adding a row past the last
// cell), Enter moves down the column, Shift+Enter breaks the cell's line
// (stored as <br> so the row stays one line of the file), Ctrl+Enter leaves
// for a new block below, and Escape leaves the cell as it was. A press on a
// header sorts by that column, again going the other way, with ▲ or ▼ in
// the header. Dragging from one cell to another sweeps a rectangle of
// cells between them: one cell is an ordinary press, more than one is a
// selection that owns the copy keys, as Kvit's sweep does. Ctrl+C copies
// the rectangle as a table of its own (the selected cells under the header
// cells of their columns, since Markdown has no notation for part of a
// table), Ctrl+X copies and empties it, Backspace or Delete empties it,
// and Escape or a press elsewhere drops it; the same copy and clear are on
// the right-click menu while cells are selected. A selection and a live
// cell are exclusive: one ends the other. The right-click menu inserts and
// deletes rows and columns, sorts, aligns and resets widths. Dragging a
// column's border resizes it, kept in the block's own cols attribute.
// Every change rewrites the whole Markdown (tabledata.go), one undo step
// each.

import (
	"fmt"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// tableCellPos is the live cell: its block, its row (-1 the header, 0.. the
// data rows) and its column.
type tableCellPos struct {
	blockID int64
	row     int
	col     int
}

// tableSortState remembers a table's last sort for its header mark.
type tableSortState struct {
	col int
	asc bool
}

// tableResizeState is a column-border drag: the block, the column left of
// the border, and the width the pointer asks for.
type tableResizeState struct {
	blockID int64
	col     int
	startX  float32
	width   float32
}

// tableSweep is a swept rectangle of cells in one table: the anchor where
// the press began and the focus where the pointer is now. Row -1 is the
// header, 0.. the data rows. sweeping reports the button is still held.
// Anchor and focus in the same cell is an ordinary press, not a selection.
type tableSweep struct {
	blockID              int64
	anchorRow, anchorCol int
	focusRow, focusCol   int
	sweeping             bool
}

// TableCell reports the live cell: its block id, row (-1 header, 0.. data)
// and column, or false when no cell is live.
func (e *Editor) TableCell() (id int64, row, col int, ok bool) {
	if e.tableActive == nil {
		return 0, 0, 0, false
	}
	p := e.tableActive
	return p.blockID, p.row, p.col, true
}

// tableBlockIndex is the document index of the table holding the live cell.
func (e *Editor) tableBlockIndex() int {
	if e.tableActive == nil {
		return -1
	}
	return e.Doc.Index(e.tableActive.blockID)
}

// TableCellText reads one cell of block id's table, "" outside it.
func (e *Editor) TableCellText(id int64, row, col int) string {
	b := e.Doc.Block(id)
	if b == nil {
		return ""
	}
	return TableCellValue(b.Text, row, col)
}

// HasTableSelection reports whether a swept rectangle of more than one
// cell is selected.
func (e *Editor) HasTableSelection() bool {
	sw := e.tableSweep
	if sw == nil {
		return false
	}
	if e.Doc.Index(sw.blockID) < 0 {
		return false
	}
	b := e.Doc.Block(sw.blockID)
	if b == nil {
		return false
	}
	t := ParseTable(b.Text)
	if !t.Valid {
		return false
	}
	if !cellInTable(t, sw.anchorRow, sw.anchorCol) || !cellInTable(t, sw.focusRow, sw.focusCol) {
		return false
	}
	return sw.anchorRow != sw.focusRow || sw.anchorCol != sw.focusCol
}

// TableSelection is the swept rectangle normalized: top, bottom, left and
// right, rows -1 for the header, 0.. for data rows. ok is false when no
// rectangle is selected.
func (e *Editor) TableSelection() (id int64, top, bottom, left, right int, ok bool) {
	if !e.HasTableSelection() {
		return 0, 0, 0, 0, 0, false
	}
	sw := e.tableSweep
	return sw.blockID, min(sw.anchorRow, sw.focusRow), max(sw.anchorRow, sw.focusRow),
		min(sw.anchorCol, sw.focusCol), max(sw.anchorCol, sw.focusCol), true
}

// cellInTable reports whether row, col names a cell of t.
func cellInTable(t PipeTable, row, col int) bool {
	if col < 0 || col >= len(t.Headers) {
		return false
	}
	return row >= -1 && row < len(t.Rows)
}

// TableCellSelected reports whether cell row, col of block id is in the
// swept rectangle.
func (e *Editor) TableCellSelected(id int64, row, col int) bool {
	bid, top, bottom, left, right, ok := e.TableSelection()
	return ok && bid == id && row >= top && row <= bottom && col >= left && col <= right
}

// clearTableSweep drops the swept rectangle, if any.
func (e *Editor) clearTableSweep() {
	if e.tableSweep != nil {
		e.tableSweep = nil
		e.changed()
	}
}

// activateTableCell makes (row, col) of block i live, focusing the block
// for the shell and ending any cell rectangle. atStart puts the caret at
// the cell's start, else at its end.
func (e *Editor) activateTableCell(i, row, col int, atStart bool) {
	b := &e.Doc.Blocks[i]
	e.tableSweep = nil
	e.tableHold[b.ID] = true
	e.Doc.SetCaret(b.ID, 0)
	e.Doc.Focused = true
	e.RequestFocus()
	e.tableActive = &tableCellPos{blockID: b.ID, row: row, col: col}
	e.openTableField(i, row, col, atStart)
	e.changed()
	e.touched()
}

// closeTableField drops the live cell without writing; the overlay calls it
// after hiding itself.
func (e *Editor) closeTableField() {
	e.tableActive = nil
	e.changed()
}

// commitTableField writes the open field's text, if any, and hides it
// without opening another cell: what a sweep taking the keyboard does to
// the live cell it ends. It reports whether a field was open.
func (e *Editor) commitTableField() bool {
	f := e.tableField
	p := e.tableActive
	hide := e.hideTableField
	if f == nil || p == nil {
		return false
	}
	text := f.Text()
	if hide != nil {
		hide()
	}
	e.hideTableField = nil
	e.tableField = nil
	e.tableActive = nil
	e.writeTableCell(p.blockID, p.row, p.col, text)
	e.changed()
	return true
}

// writeTableCell writes a cell's text into its table, as one undo step.
// Unchanged text writes nothing, so typing a space the serializer trims
// does not fight the field.
func (e *Editor) writeTableCell(id int64, row, col int, text string) {
	blk := e.Doc.Block(id)
	if blk == nil || e.Doc.ReadOnly {
		return
	}
	if TableCellValue(blk.Text, row, col) == text {
		return
	}
	if content := SetTableCell(blk.Text, row, col, text); content != blk.Text {
		e.Doc.Edit("table cell", func() { blk.Text = content })
	}
}

// writeTable rewrites a table block's whole Markdown, as one undo step.
func (e *Editor) writeTable(id int64, content string) {
	blk := e.Doc.Block(id)
	if blk == nil || e.Doc.ReadOnly || content == blk.Text {
		return
	}
	e.Doc.Edit("table", func() { blk.Text = content })
}

// moveTableCell walks the grid in reading order, as Tab does: across the
// row, down to the next row's first cell, and past the last cell by adding
// a row and landing in it. Backwards mirrors it, staying at the header's
// first cell.
func (e *Editor) moveTableCell(forward bool) {
	p := e.tableActive
	if p == nil {
		return
	}
	i := e.Doc.Index(p.blockID)
	if i < 0 {
		e.tableActive = nil
		return
	}
	t := ParseTable(e.Doc.Blocks[i].Text)
	if !t.Valid {
		e.tableActive = nil
		return
	}
	cols, dataRows := len(t.Headers), len(t.Rows)
	r, c := p.row, p.col
	if forward {
		switch {
		case c+1 < cols:
			e.retargetTableCell(i, r, c+1, false)
		case r == -1:
			if dataRows > 0 {
				e.retargetTableCell(i, 0, 0, false)
			} else {
				e.writeTable(p.blockID, InsertTableRow(e.Doc.Blocks[i].Text, -1))
				e.retargetTableCell(i, 0, 0, false)
			}
		case r+1 < dataRows:
			e.retargetTableCell(i, r+1, 0, false)
		default:
			e.writeTable(p.blockID, InsertTableRow(e.Doc.Blocks[i].Text, dataRows-1))
			// The insert is the row after the last; its index is dataRows.
			e.retargetTableCell(i, dataRows, 0, false)
		}
		return
	}
	switch {
	case c-1 >= 0:
		e.retargetTableCell(i, r, c-1, false)
	case r == 0:
		e.retargetTableCell(i, -1, cols-1, false)
	case r > 0:
		e.retargetTableCell(i, r-1, cols-1, false)
	}
}

// moveTableCellVertically moves along the column, the header counting as
// the row above the first data row. Past either end the caret leaves the
// table for the neighbouring block.
func (e *Editor) moveTableCellVertically(down bool) {
	p := e.tableActive
	if p == nil {
		return
	}
	i := e.Doc.Index(p.blockID)
	if i < 0 {
		return
	}
	t := ParseTable(e.Doc.Blocks[i].Text)
	if !t.Valid {
		return
	}
	r, c := p.row, p.col
	if down {
		switch {
		case r == -1 && len(t.Rows) > 0:
			e.retargetTableCell(i, 0, c, false)
		case r >= 0 && r+1 < len(t.Rows):
			e.retargetTableCell(i, r+1, c, false)
		default:
			e.leaveTable(1)
		}
		return
	}
	switch {
	case r == 0:
		e.retargetTableCell(i, -1, c, false)
	case r > 0:
		e.retargetTableCell(i, r-1, c, false)
	default:
		e.leaveTable(-1)
	}
}

// moveTableCellHorizontally moves beside the cell, wrapping to the next or
// previous row, entering on the side it came from. Unlike Tab it never adds
// a row: past the last cell it leaves the table.
func (e *Editor) moveTableCellHorizontally(right bool) {
	p := e.tableActive
	if p == nil {
		return
	}
	i := e.Doc.Index(p.blockID)
	if i < 0 {
		return
	}
	t := ParseTable(e.Doc.Blocks[i].Text)
	if !t.Valid {
		return
	}
	cols, dataRows := len(t.Headers), len(t.Rows)
	r, c := p.row, p.col
	if right {
		switch {
		case c+1 < cols:
			e.retargetTableCell(i, r, c+1, true)
		case r == -1 && dataRows > 0:
			e.retargetTableCell(i, 0, 0, true)
		case r >= 0 && r+1 < dataRows:
			e.retargetTableCell(i, r+1, 0, true)
		default:
			e.leaveTable(1)
		}
		return
	}
	switch {
	case c-1 >= 0:
		e.retargetTableCell(i, r, c-1, false)
	case r == 0:
		e.retargetTableCell(i, -1, cols-1, false)
	case r > 0:
		e.retargetTableCell(i, r-1, cols-1, false)
	default:
		e.leaveTable(-1)
	}
}

// leaveTable ends the cell edit and puts the caret in the block above or
// below, the route Up and Down take when no cell is live.
func (e *Editor) leaveTable(direction int) {
	p := e.tableActive
	if p == nil {
		return
	}
	i := e.Doc.Index(p.blockID)
	e.tableActive = nil
	e.changed()
	if i < 0 {
		return
	}
	if j := i + direction; j >= 0 && j < len(e.Doc.Blocks) {
		nb := e.Doc.Blocks[j]
		if direction < 0 {
			e.Doc.SetCaret(nb.ID, len(runes(nb.Text)))
		} else {
			e.Doc.SetCaret(nb.ID, 0)
		}
		e.RequestFocus()
		e.touched()
		e.changed()
	}
}

// retargetTableCell commits the open field's text, then opens row, col of
// the same table: what Tab, Enter and the arrows do.
func (e *Editor) retargetTableCell(i, row, col int, atStart bool) {
	if f := e.tableField; f != nil {
		text := f.Text()
		p := e.tableActive
		hide := e.hideTableField
		if hide != nil {
			hide()
		}
		if p != nil {
			e.writeTableCell(p.blockID, p.row, p.col, text)
		}
	}
	e.tableActive = &tableCellPos{blockID: e.Doc.Blocks[i].ID, row: row, col: col}
	e.openTableField(i, row, col, atStart)
	e.changed()
}

// sortTableBy sorts block i's table by column col, toggling direction on a
// second press, as one undo step, and marks the header.
func (e *Editor) sortTableBy(i, col int) {
	e.commitTableField()
	e.clearTableSweep()
	b := &e.Doc.Blocks[i]
	t := ParseTable(b.Text)
	if !t.Valid || col < 0 || col >= len(t.Headers) {
		return
	}
	asc := true
	if e.tableSort != nil {
		if st, ok := e.tableSort[b.ID]; ok && st.col == col {
			asc = !st.asc
		}
	}
	if content := SortTableColumn(b.Text, col, asc); content != b.Text {
		e.Doc.Edit("sort table", func() { b.Text = content })
	}
	if e.tableSort == nil {
		e.tableSort = map[int64]tableSortState{}
	}
	e.tableSort[b.ID] = tableSortState{col: col, asc: asc}
	e.changed()
}

// tableSortMark is the header's ▲ or ▼ for column col, "" elsewhere.
func (e *Editor) tableSortMark(id int64, col int) string {
	if e.tableSort == nil {
		return ""
	}
	st, ok := e.tableSort[id]
	if !ok || st.col != col {
		return ""
	}
	if st.asc {
		return "▲"
	}
	return "▼"
}

// insertTableRowAfter adds an empty data row after afterRow of block i's
// table, as one undo step.
func (e *Editor) insertTableRowAfter(i, afterRow int) {
	e.commitTableField()
	e.clearTableSweep()
	b := &e.Doc.Blocks[i]
	e.writeTable(b.ID, InsertTableRow(b.Text, afterRow))
	e.changed()
}

// insertTableColumnAfter adds an empty column after afterCol, -1 at the
// left, as one undo step.
func (e *Editor) insertTableColumnAfter(i, afterCol int) {
	e.commitTableField()
	e.clearTableSweep()
	b := &e.Doc.Blocks[i]
	e.writeTable(b.ID, InsertTableColumn(b.Text, afterCol))
	e.changed()
}

// removeTableRow takes data row row away, as one undo step.
func (e *Editor) removeTableRow(i, row int) {
	e.commitTableField()
	e.clearTableSweep()
	b := &e.Doc.Blocks[i]
	e.writeTable(b.ID, RemoveTableRow(b.Text, row))
	if e.tableActive != nil && e.tableActive.blockID == b.ID {
		e.tableActive = nil
		if e.hideTableField != nil {
			hide := e.hideTableField
			e.hideTableField = nil
			e.tableField = nil
			hide()
		}
	}
	e.changed()
}

// removeTableColumn takes column col away, never the last one.
func (e *Editor) removeTableColumn(i, col int) {
	e.commitTableField()
	e.clearTableSweep()
	b := &e.Doc.Blocks[i]
	e.writeTable(b.ID, RemoveTableColumn(b.Text, col))
	if e.tableActive != nil && e.tableActive.blockID == b.ID {
		e.tableActive = nil
		if e.hideTableField != nil {
			hide := e.hideTableField
			e.hideTableField = nil
			e.tableField = nil
			hide()
		}
	}
	e.changed()
}

// setTableAlignment sets column col's alignment, as one undo step.
func (e *Editor) setTableAlignment(i, col int, align TableAlign) {
	e.commitTableField()
	e.clearTableSweep()
	b := &e.Doc.Blocks[i]
	e.writeTable(b.ID, SetTableAlignment(b.Text, col, align))
	e.changed()
}

// InsertTable adds an empty table of cols columns and rows data rows below
// the caret's block, and makes its first cell live.
func (e *Editor) InsertTable(cols, rows int) {
	if e.Doc.ReadOnly {
		return
	}
	cols, rows = clampTableSize(cols, rows)
	d := e.Doc
	i := len(d.Blocks) - 1
	if b := d.CaretBlock(); b != nil && d.Focused {
		i = d.Index(b.ID)
	}
	nb := NewBlock(Table, EmptyTable(cols, rows))
	d.Edit("insert table", func() {
		d.Blocks = append(d.Blocks[:i+1:i+1], append([]Block{nb}, d.Blocks[i+1:]...)...)
		d.SetCaret(nb.ID, 0)
	})
	e.clearBlockSel()
	j := d.Index(nb.ID)
	e.measure()
	e.activateTableCell(j, -1, 0, true)
}

// clampTableSize bounds the grid picker: 1..10 columns, 0..10 data rows.
func clampTableSize(cols, rows int) (int, int) {
	return min(max(cols, 1), 10), min(max(rows, 0), 10)
}

// tableCellMenu opens the right-click cell menu: rows, columns, sorting,
// alignment and widths, with the swept rectangle's copy and clear first
// while cells are selected.
func (e *Editor) tableCellMenu(i, row, col int, at geom.Rect) {
	e.ui.ShowMenuAt(e, at, "Table", e.tableCellMenuItems(i, row, col))
}

// tableCellMenuItems builds the right-click cell menu's lines.
func (e *Editor) tableCellMenuItems(i, row, col int) []kvitui.MenuItem {
	b := &e.Doc.Blocks[i]
	t := ParseTable(b.Text)
	if !t.Valid {
		return nil
	}
	cols, dataRows := len(t.Headers), len(t.Rows)
	cmd := func(label string, run func()) kvitui.MenuItem {
		return kvitui.MenuItem{Text: label, OnSelect: func() {
			run()
			e.touched()
			e.changed()
		}}
	}
	alignName := map[TableAlign]string{
		TableAlignNone: "None", TableAlignLeft: "Left",
		TableAlignCenter: "Center", TableAlignRight: "Right",
	}
	var aligns []kvitui.MenuItem
	for _, a := range []TableAlign{TableAlignNone, TableAlignLeft, TableAlignCenter, TableAlignRight} {
		a := a
		aligns = append(aligns, kvitui.MenuItem{
			Text: alignName[a], Checked: col < len(t.Alignments) && t.Alignments[col] == a,
			OnSelect: func() { e.setTableAlignment(i, col, a); e.touched(); e.changed() },
		})
	}
	items := []kvitui.MenuItem{
		cmd("Insert row above", func() {
			at := row - 1
			if row == -1 {
				at = -1
			}
			e.insertTableRowAfter(i, at)
		}),
		cmd("Insert row below", func() {
			at := row
			if row == -1 {
				at = len(t.Rows) - 1
				if len(t.Rows) == 0 {
					at = -1
				}
			}
			e.insertTableRowAfter(i, at)
		}),
		cmd("Delete row", func() {
			if row >= 0 {
				e.removeTableRow(i, row)
			}
		}),
		{Separator: true},
		cmd("Insert column left", func() { e.insertTableColumnAfter(i, col-1) }),
		cmd("Insert column right", func() { e.insertTableColumnAfter(i, col) }),
		cmd("Delete column", func() { e.removeTableColumn(i, col) }),
		{Separator: true},
		cmd("Sort ascending", func() { e.sortTableColumn(i, col, true) }),
		cmd("Sort descending", func() { e.sortTableColumn(i, col, false) }),
		{Separator: true},
		{Text: "Alignment", Items: aligns},
		cmd("Reset column widths", func() { e.clearTableWidths(i) }),
	}
	// A header cannot lose its row; a one-row table cannot lose it either.
	items[2].Disabled = row < 0
	_ = cols
	_ = dataRows
	if _, _, _, _, _, ok := e.TableSelection(); ok {
		sel := []kvitui.MenuItem{
			cmd("Copy selected cells", func() { e.copyTableSelection() }),
			cmd("Clear selected cells", func() { e.clearSelectedCells() }),
			{Separator: true},
		}
		items = append(sel, items...)
	}
	return items
}

// sortTableColumn sorts by col in the given direction, marking the header.
func (e *Editor) sortTableColumn(i, col int, asc bool) {
	e.commitTableField()
	e.clearTableSweep()
	b := &e.Doc.Blocks[i]
	t := ParseTable(b.Text)
	if !t.Valid || col < 0 || col >= len(t.Headers) {
		return
	}
	if content := SortTableColumn(b.Text, col, asc); content != b.Text {
		e.Doc.Edit("sort table", func() { b.Text = content })
	}
	if e.tableSort == nil {
		e.tableSort = map[int64]tableSortState{}
	}
	e.tableSort[b.ID] = tableSortState{col: col, asc: asc}
	e.changed()
}

// setTableColumnWidth pins one column's width, as one undo step. Only the
// dragged column is written; the rest keep measuring themselves.
func (e *Editor) setTableColumnWidth(i, col, width int) {
	b := &e.Doc.Blocks[i]
	t := ParseTable(b.Text)
	if !t.Valid || col < 0 || col >= len(t.Headers) {
		return
	}
	widths := storedTableWidths(b)
	for len(widths) < len(t.Headers) {
		widths = append(widths, 0)
	}
	widths[col] = max(minColumn, width)
	if raw, ok := formatTableWidths(widths); ok {
		e.Doc.SetAttr([]int64{b.ID}, "cols", raw)
	} else {
		e.Doc.SetAttr([]int64{b.ID}, "cols", "")
	}
	e.changed()
}

// clearTableWidths drops the cols key, so every column measures itself.
func (e *Editor) clearTableWidths(i int) {
	b := &e.Doc.Blocks[i]
	e.Doc.SetAttr([]int64{b.ID}, "cols", "")
	e.changed()
}

// tableHint is the hint under a grid with a live cell: the key that leaves
// the block, as the code panel names Ctrl+Enter while the caret is in it.
func (e *Editor) tableHint() string {
	if e.tableActive == nil {
		return ""
	}
	return "Ctrl+Enter: new block · Right-click: rows, columns, sorting"
}

// tableHeaderDoubleClick sorts by a header column on a double press. It
// runs before anything else on the press because the press usually closed
// the live cell's field first: the field's popup sees an outside press and
// clears the live cell, which hides the grid the normal dispatch looks for.
// Reading the grid straight from the Markdown keeps the sort working either
// way.
func (e *Editor) tableHeaderDoubleClick(where geom.Point, button, clicks int) bool {
	if button != unison.ButtonLeft || clicks != 2 || e.Doc.ReadOnly {
		return false
	}
	i := e.rowAt(where)
	if i < 0 || i >= len(e.Doc.Blocks) {
		return false
	}
	if e.Doc.Blocks[i].Kind != Table {
		return false
	}
	g, ok := e.gridFor(i)
	if !ok {
		return false
	}
	row, col, ok := e.tableCellAt(i, g, where)
	if !ok || row != -1 {
		return false
	}
	e.sortTableBy(i, col)
	return true
}

// beginTableSweep anchors a sweep where the press landed: not a selection
// yet, until the pointer reaches another cell this is an ordinary press.
// Any swept rectangle starts over, and the live cell's text is kept.
func (e *Editor) beginTableSweep(i, row, col int) {
	e.commitTableField()
	e.tableHold[e.Doc.Blocks[i].ID] = true
	e.tableSweep = &tableSweep{blockID: e.Doc.Blocks[i].ID,
		anchorRow: row, anchorCol: col, focusRow: row, focusCol: col, sweeping: true}
	b := &e.Doc.Blocks[i]
	e.Doc.SetCaret(b.ID, 0)
	e.Doc.Focused = true
	e.clearBlockSel()
	e.RequestFocus()
	e.changed()
}

// extendTableSweep moves the sweep's focus to the cell under the pointer.
// Past the first cell this is a selection, which ends the live cell: the
// two cannot both own the pointer or the copy key.
func (e *Editor) extendTableSweep(i, row, col int) {
	sw := e.tableSweep
	if sw == nil || !sw.sweeping {
		return
	}
	if e.Doc.Index(sw.blockID) != i {
		return
	}
	if sw.focusRow == row && sw.focusCol == col {
		return
	}
	sw.focusRow, sw.focusCol = row, col
	if e.HasTableSelection() {
		e.commitTableField()
		e.RequestFocus()
	}
	e.changed()
}

// finishTableSweep ends the gesture: a sweep that reached another cell
// keeps its rectangle; a press and release in one cell is a press into it.
func (e *Editor) finishTableSweep(i int) bool {
	sw := e.tableSweep
	if sw == nil || !sw.sweeping {
		return false
	}
	sw.sweeping = false
	if e.HasTableSelection() {
		e.commitTableField()
		e.RequestFocus()
		e.changed()
		return true
	}
	row, col := sw.anchorRow, sw.anchorCol
	e.tableSweep = nil
	e.activateTableCell(i, row, col, false)
	return true
}

// tableSelectionMarkdown is the swept rectangle as a table of its own.
// Markdown has no notation for part of a table, so what is copied is a
// whole small table: the selected cells under the header cells of the
// columns they came from, which is what makes the copy paste back as a
// table and say what its columns are. Selecting header cells alone yields
// a table with a header and no rows.
func (e *Editor) tableSelectionMarkdown() string {
	id, top, bottom, left, right, ok := e.TableSelection()
	if !ok {
		return ""
	}
	b := e.Doc.Block(id)
	if b == nil {
		return ""
	}
	cols := right - left + 1
	firstData := max(top, 0)
	rows := 0
	if bottom >= 0 {
		rows = bottom - firstData + 1
	}
	md := EmptyTable(cols, rows)
	for c := 0; c < cols; c++ {
		md = SetTableCell(md, -1, c, TableCellValue(b.Text, -1, left+c))
		for r := 0; r < rows; r++ {
			md = SetTableCell(md, r, c, TableCellValue(b.Text, firstData+r, left+c))
		}
	}
	return md
}

// copyTableSelection copies the swept rectangle as a table of its own.
func (e *Editor) copyTableSelection() {
	if md := e.tableSelectionMarkdown(); md != "" {
		e.copyMarkdown(md)
	}
}

// cutTableSelection copies the rectangle and empties it, dropping the
// selection, as one undo step for the emptying.
func (e *Editor) cutTableSelection() {
	if e.Doc.ReadOnly || !e.HasTableSelection() {
		return
	}
	e.copyTableSelection()
	e.clearSelectedCells()
	e.clearTableSweep()
}

// clearSelectedCells empties every selected cell in one model write, so the
// whole rectangle is one undo step rather than one per cell. The rectangle
// stays selected.
func (e *Editor) clearSelectedCells() {
	id, top, bottom, left, right, ok := e.TableSelection()
	if !ok || e.Doc.ReadOnly {
		return
	}
	b := e.Doc.Block(id)
	if b == nil {
		return
	}
	md := b.Text
	for r := top; r <= bottom; r++ {
		for c := left; c <= right; c++ {
			md = SetTableCell(md, r, c, "")
		}
	}
	e.writeTable(id, md)
	e.changed()
}

// caretInTableGrid reports whether the caret is in a table block whose
// grid is showing with no live cell: pointer territory, where typing into
// the Markdown source would corrupt what the grid shows.
func (e *Editor) caretInTableGrid() bool {
	if e.tableField != nil || !e.Doc.Focused {
		return false
	}
	b := e.Doc.CaretBlock()
	if b == nil || b.Kind != Table {
		return false
	}
	i := e.Doc.Index(b.ID)
	if i < 0 {
		return false
	}
	_, ok := e.tableShowsGrid(i)
	return ok
}

// tableGridKeys swallows the keys that would edit a table's Markdown while
// its grid is showing: the source is not on screen, so there is nowhere
// for the edit to be seen. Navigation, undo, copy and the find keys fall
// through; Escape does nothing.
func (e *Editor) tableGridKeys(key unison.KeyCode, ctrl, shift, alt bool) bool {
	if !e.caretInTableGrid() {
		return false
	}
	switch key {
	case unison.KeyEscape:
		return true
	case unison.KeyReturn, unison.KeyTab, unison.KeyBackspace, unison.KeyDelete:
		return true
	}
	if !ctrl && !alt {
		return false
	}
	switch key {
	case unison.KeyZ, unison.KeyY, unison.KeyC, unison.KeyA, unison.KeyF, unison.KeyH, unison.KeyP:
		return false
	}
	return true
}

// tableSweepKey owns the keys that act on a swept rectangle: copy it, cut
// it, empty it, or drop it. Anything else drops it first and then means
// what it always did. Modifier keys alone are ignored, so holding Ctrl to
// copy does not read as some other key dropping the rectangle before the C
// arrives. It reports whether the key was used.
func (e *Editor) tableSweepKey(key unison.KeyCode, ctrl, shift, alt bool) bool {
	if !e.HasTableSelection() {
		return false
	}
	switch key {
	case unison.KeyLShift, unison.KeyRShift, unison.KeyLControl, unison.KeyRControl,
		unison.KeyLOption, unison.KeyROption, unison.KeyLCommand, unison.KeyRCommand:
		return true
	case unison.KeyEscape:
		e.clearTableSweep()
		return true
	case unison.KeyC:
		if ctrl && !shift && !alt {
			e.copyTableSelection()
			return true
		}
	case unison.KeyX:
		if ctrl && !shift && !alt {
			if e.Doc.ReadOnly {
				e.copyTableSelection()
			} else {
				e.cutTableSelection()
			}
			return true
		}
	case unison.KeyBackspace, unison.KeyDelete:
		if !ctrl && !alt {
			if !e.Doc.ReadOnly {
				e.clearSelectedCells()
			}
			return true
		}
	}
	e.clearTableSweep()
	return false
}

// tableMouse handles a press in a table's grid: a column-border drag, a
// header double-press sorting, or making the cell live. It reports whether
// the press was in the grid.
func (e *Editor) tableMouse(i int, g *grid, where geom.Point, button, clicks int, mods mod.Modifiers) bool {
	if button == unison.ButtonRight {
		if row, col, ok := e.tableCellAt(i, g, where); ok {
			e.tableCellMenu(i, row, col, geom.NewRect(where.X, where.Y, 0, 0))
			return true
		}
		return false
	}
	if button != unison.ButtonLeft {
		return false
	}
	if col, ok := e.tableGripAt(i, g, where); ok {
		e.commitTableField()
		e.clearTableSweep()
		e.tableResize = &tableResizeState{blockID: e.Doc.Blocks[i].ID, col: col, startX: where.X, width: g.cols[col]}
		return true
	}
	row, col, ok := e.tableCellAt(i, g, where)
	if !ok {
		return false
	}
	if row == -1 && clicks == 2 {
		e.sortTableBy(i, col)
		return true
	}
	// A press anchors a sweep there; the release turns a press in one cell
	// into an edit, while a drag across cells keeps its rectangle.
	e.beginTableSweep(i, row, col)
	return true
}

// tableGripWidth is how far from a column's border a press starts a resize.
const tableGripWidth = 6

// tableGripAt is the column left of the border under where, for resizing.
func (e *Editor) tableGripAt(i int, g *grid, where geom.Point) (int, bool) {
	o := e.gridOrigin(i)
	rel := where.Sub(o)
	if rel.Y < 0 || rel.Y > g.height {
		return 0, false
	}
	x := float32(0)
	for c, w := range g.cols {
		x += w
		if c < len(g.cols)-1 && abs32(rel.X-x) <= e.px(tableGripWidth)/2+1 {
			return c, true
		}
	}
	return 0, false
}

func abs32(f float32) float32 {
	if f < 0 {
		return -f
	}
	return f
}

// tableDragResize follows a border drag: the grid previews the width.
func (e *Editor) tableDragResize(where geom.Point) {
	r := e.tableResize
	if r == nil {
		return
	}
	r.width = max(e.px(minColumn), r.width+where.X-r.startX)
	r.startX = where.X
	e.changed()
}

// tableEndResize writes the dragged width, as one undo step.
func (e *Editor) tableEndResize() {
	r := e.tableResize
	if r == nil {
		return
	}
	e.tableResize = nil
	i := e.Doc.Index(r.blockID)
	if i < 0 {
		return
	}
	// Stored widths are design pixels; the drag measured device pixels.
	px := e.ui.Interface.Px(100)
	design := int(float32(r.width) / float32(px) * 100)
	if design < minColumn {
		design = minColumn
	}
	e.setTableColumnWidth(i, r.col, design)
}

// drawTableDecor draws the live cell, the sort mark, the grips and the hint.
func (e *Editor) drawTableDecor(gc *unison.Canvas, i int, g *grid, o geom.Point, total float32) {
	t := e.tok()
	id := e.Doc.Blocks[i].ID
	if st, ok := e.tableSort[id]; ok && st.col >= 0 && st.col < len(g.cols) {
		mark := "▲"
		if !st.asc {
			mark = "▼"
		}
		l := e.label(mark, e.chrome(kvitui.RoleCaption, text.Regular, t.TextMuted))
		w, _ := l.Size()
		r := e.cellRect(i, g, -1, st.col)
		l.Draw(gc, r.Right()-w-e.px(4), r.Y+e.px(3))
	}
	if e.tableActive != nil && e.tableActive.blockID == id {
		p := e.tableActive
		r := e.cellRect(i, g, p.row, p.col)
		e.stroke(gc, r, e.px(2), e.px(2), t.Accent)
	}
	// Resize grips: a short handle centred on each inner border's top.
	if e.hover == id {
		x := o.X
		for c, w := range g.cols {
			x += w
			if c < len(g.cols)-1 {
				grip := geom.NewRect(x-e.px(4), o.Y, e.px(8), min(e.px(18), g.height))
				e.fill(gc, grip, t.Border)
			}
		}
	}
	if e.tableCellIn(id) {
		if hint := e.tableHint(); hint != "" {
			l := e.label(hint, e.chrome(kvitui.RoleSmall, text.Regular, t.TextFaint))
			l.Draw(gc, o.X, o.Y+g.height+e.px(4))
		}
	}
}

// tableStatus reports the live cell for the status line and screen readers.
func (e *Editor) tableStatus() string {
	p := e.tableActive
	if p == nil {
		return ""
	}
	if p.row == -1 {
		return fmt.Sprintf("Header row, column %d", p.col+1)
	}
	return fmt.Sprintf("Row %d, column %d", p.row+1, p.col+1)
}
