package editor

// Tables in the grid (features.md 1.2.11, Kvit's TableBlock.qml): a press in
// a cell makes it live for editing in place, Tab walks the grid, Enter moves
// down, Shift+Enter breaks the line (<br>), Ctrl+Enter leaves, Escape
// cancels, headers sort, the menu restructures, widths persist, and the grid
// picker inserts.

import (
	"strings"
	"testing"

	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

const tableNote = "| N | V |\n| --- | --- |\n| b | 3 |\n| a | 10 |\n| c | 2 |"

const twoCellNote = "| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |"

func TestTablePressMakesCellLive(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Sync()
	var p geom.Point
	s.Do(func() {
		r := e.PartRect(0, "cell")
		if r.Width <= 0 || r.Height <= 0 {
			t.Fatalf("no cell rect: %+v", r)
		}
		p = s.Screen.PanelPoint(e, r.Center())
	})
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Sync()
	s.Do(func() {
		_, row, col, ok := e.TableCell()
		if !ok || row != 0 || col != 0 {
			t.Fatalf("press should make (0,0) live, got row %d col %d ok %v", row, col, ok)
		}
	})
	if name, text := focusedField(s); name != "Table cell" || text != "1" {
		t.Fatalf("the field: %q holding %q, want %q holding %q", name, text, "Table cell", "1")
	}
	s.CheckNamed()
}

func TestTableCellCommitAndUndo(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Do(func() { e.activateTableCell(0, 0, 0, false) })
	s.Sync()
	if name, text := focusedField(s); name != "Table cell" || text != "1" {
		t.Fatalf("the field: %q holding %q", name, text)
	}
	key(s, unison.KeyEnd, mod.None)
	typed(s, "x")
	if _, text := focusedField(s); text != "1x" {
		t.Fatalf("typed: %q", text)
	}
	steps := 0
	s.Do(func() { steps = e.Doc.UndoSteps() })
	key(s, unison.KeyReturn, mod.None) // Enter commits and moves down
	s.Sync()
	text, _ := blockText(s, e, 0)
	if !strings.Contains(text, "| 1x | 2 |") {
		t.Fatalf("committed: %q", text)
	}
	s.Do(func() {
		if _, row, col, ok := e.TableCell(); !ok || row != 1 || col != 0 {
			t.Errorf("Enter should move down to (1,0), got (%d,%d) ok %v", row, col, ok)
		}
	})
	// One undo takes the cell back.
	s.Do(func() { e.Doc.Undo() })
	if text, _ := blockText(s, e, 0); strings.Contains(text, "1x") {
		t.Errorf("one undo should take the cell back: %q", text)
	}
	_ = steps
}

func TestTableEscapeLeavesCell(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Do(func() { e.activateTableCell(0, 0, 0, false) })
	s.Sync()
	typed(s, "zzz")
	key(s, unison.KeyEscape, mod.None)
	s.Sync()
	if text, _ := blockText(s, e, 0); strings.Contains(text, "zzz") {
		t.Fatalf("Escape should leave the cell as it was: %q", text)
	}
	s.Do(func() {
		if _, _, _, ok := e.TableCell(); ok {
			t.Error("Escape should leave no live cell")
		}
	})
}

func TestTableTabWalksAndAddsRow(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Do(func() { e.activateTableCell(0, -1, 0, true) })
	s.Sync()
	// Header (A) -> header B -> (0,0) -> (0,1) -> (1,0) -> (1,1) -> new (2,0).
	want := [][2]int{{-1, 1}, {0, 0}, {0, 1}, {1, 0}, {1, 1}, {2, 0}}
	for k, w := range want {
		key(s, unison.KeyTab, mod.None)
		s.Sync()
		var row, col int
		var ok bool
		s.Do(func() { _, row, col, ok = e.TableCell() })
		if !ok || row != w[0] || col != w[1] {
			t.Fatalf("tab %d: live (%d,%d) ok %v, want (%d,%d)", k, row, col, ok, w[0], w[1])
		}
	}
	text, _ := blockText(s, e, 0)
	if !strings.Contains(text, "|  |  |") {
		t.Fatalf("Tab past the last cell should add a row: %q", text)
	}
	// Backwards from the new row lands on the last old cell.
	key(s, unison.KeyTab, mod.Shift)
	s.Sync()
	s.Do(func() {
		_, row, col, _ := e.TableCell()
		if row != 1 || col != 1 {
			t.Errorf("Shift+Tab should step back to (1,1), got (%d,%d)", row, col)
		}
	})
}

func TestTableEnterMovesDownAndCtrlEnterLeaves(t *testing.T) {
	s, e := openEditor(t, twoCellNote+"\n\nAfter\n")
	s.Do(func() { e.activateTableCell(0, 0, 0, false) })
	s.Sync()
	key(s, unison.KeyReturn, mod.None)
	s.Sync()
	s.Do(func() {
		_, row, col, ok := e.TableCell()
		if !ok || row != 1 || col != 0 {
			t.Fatalf("Enter should move down to (1,0), got (%d,%d) ok %v", row, col, ok)
		}
	})
	// Ctrl+Enter leaves for a new paragraph below the table.
	key(s, unison.KeyReturn, mod.Control)
	s.Sync()
	var n int
	var kind Kind
	var caret int64
	s.Do(func() {
		n = len(e.Doc.Blocks)
		kind = e.Doc.Blocks[1].Kind
		caret = e.Doc.Caret.Block
		if _, _, _, ok := e.TableCell(); ok {
			t.Error("Ctrl+Enter should leave no live cell")
		}
	})
	if n != 3 || kind != Paragraph {
		t.Fatalf("Ctrl+Enter should insert a paragraph below: %d blocks, kind %v", n, kind)
	}
	s.Do(func() {
		if caret != e.Doc.Blocks[1].ID {
			t.Errorf("the caret should be in the new block")
		}
	})
}

func TestTableShiftEnterBreaksLine(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Do(func() { e.activateTableCell(0, 0, 0, false) })
	s.Sync()
	key(s, unison.KeyEnd, mod.None)
	key(s, unison.KeyReturn, mod.Shift)
	s.Sync()
	if _, text := focusedField(s); !strings.Contains(text, "1\n") && text != "1\n" {
		t.Fatalf("Shift+Enter should break the field's line: %q", text)
	}
	typed(s, "more")
	key(s, unison.KeyTab, mod.None) // commit and move
	s.Sync()
	text, _ := blockText(s, e, 0)
	if !strings.Contains(text, "1<br>more") {
		t.Fatalf("the break is stored as <br>, one line of the file: %q", text)
	}
	if got := len(strings.Split(text, "\n")); got != 4 {
		t.Fatalf("the row must stay one line of the file (%d lines): %q", got, text)
	}
	// The cell reads back with its line break.
	tb := ParseTable(text)
	if tb.Rows[0][0] != "1\nmore" {
		t.Errorf("the cell reads back with its break: %q", tb.Rows[0][0])
	}
}

func TestTableArrowsMoveBetweenCells(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Do(func() { e.activateTableCell(0, 0, 1, false) }) // "2"
	s.Sync()
	// Left at the start goes to (0,0).
	s.Do(func() { e.tableField.Edit().SetSelection(0, 0) })
	key(s, unison.KeyLeft, mod.None)
	s.Sync()
	s.Do(func() {
		_, row, col, ok := e.TableCell()
		if !ok || row != 0 || col != 0 {
			t.Fatalf("Left at the start should reach (0,0), got (%d,%d) %v", row, col, ok)
		}
	})
	// Right at the end goes to the next cell in reading order.
	s.Do(func() {
		e.tableField.Edit().SetSelection(len([]rune(e.tableField.Text())), len([]rune(e.tableField.Text())))
	})
	key(s, unison.KeyRight, mod.None)
	s.Sync()
	s.Do(func() {
		_, row, col, ok := e.TableCell()
		if !ok || row != 0 || col != 1 {
			t.Fatalf("Right at the end of (0,0) should reach (0,1), got (%d,%d) %v", row, col, ok)
		}
	})
	// Right at the end of the row wraps to the next row.
	s.Do(func() {
		e.tableField.Edit().SetSelection(len([]rune(e.tableField.Text())), len([]rune(e.tableField.Text())))
	})
	key(s, unison.KeyRight, mod.None)
	s.Sync()
	s.Do(func() {
		_, row, col, ok := e.TableCell()
		if !ok || row != 1 || col != 0 {
			t.Fatalf("Right at the end should wrap to (1,0), got (%d,%d) %v", row, col, ok)
		}
	})
	// Up climbs the column; Down comes back.
	key(s, unison.KeyUp, mod.None)
	s.Sync()
	s.Do(func() {
		_, row, col, ok := e.TableCell()
		if !ok || row != 0 || col != 0 {
			t.Fatalf("Up should reach (0,0), got (%d,%d) %v", row, col, ok)
		}
	})
	key(s, unison.KeyUp, mod.None)
	s.Sync()
	s.Do(func() {
		_, row, col, ok := e.TableCell()
		if !ok || row != -1 || col != 0 {
			t.Fatalf("Up again should reach the header, got (%d,%d) %v", row, col, ok)
		}
	})
	key(s, unison.KeyDown, mod.None)
	s.Sync()
	s.Do(func() {
		_, row, col, ok := e.TableCell()
		if !ok || row != 0 || col != 0 {
			t.Fatalf("Down should return to (0,0), got (%d,%d) %v", row, col, ok)
		}
	})
}

func TestTableSortTogglesAndMarks(t *testing.T) {
	s, e := openEditor(t, tableNote)
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	s.Do(func() { e.sortTableBy(0, 1) })
	s.Do(func() {
		got := e.Doc.Blocks[0].Text
		want := "| N | V |\n| --- | --- |\n| c | 2 |\n| b | 3 |\n| a | 10 |"
		if got != want {
			t.Fatalf("numeric ascending:\n%s\nwant\n%s", got, want)
		}
		if m := e.tableSortMark(id, 1); m != "▲" {
			t.Fatalf("mark after ascending: %q", m)
		}
	})
	steps := 0
	s.Do(func() { steps = e.Doc.UndoSteps() })
	// A second press on the same header goes the other way.
	s.Do(func() { e.sortTableBy(0, 1) })
	s.Do(func() {
		if !strings.Contains(e.Doc.Blocks[0].Text, "| a | 10 |") || !strings.HasPrefix(e.Doc.Blocks[0].Text, "| N | V |") {
			t.Fatalf("descending:\n%s", e.Doc.Blocks[0].Text)
		}
		if m := e.tableSortMark(id, 1); m != "▼" {
			t.Fatalf("mark after descending: %q", m)
		}
		if e.Doc.UndoSteps() != steps+1 {
			t.Fatalf("sort should be one undo step, %d -> %d", steps, e.Doc.UndoSteps())
		}
	})
	// Sorting another column restarts ascending.
	s.Do(func() { e.sortTableBy(0, 0) })
	s.Do(func() {
		if m := e.tableSortMark(id, 0); m != "▲" {
			t.Fatalf("new column ascends: %q", m)
		}
		if m := e.tableSortMark(id, 1); m != "" {
			t.Fatalf("only the sorted column is marked: %q", m)
		}
	})
	// Double press on a header sorts too.
	s.Do(func() {
		g, ok := e.gridFor(0)
		if !ok {
			t.Fatal("no grid")
		}
		o := e.gridOrigin(0)
		_ = o
		where := e.cellRect(0, g, -1, 0).Center()
		if !e.tableMouse(0, g, where, unison.ButtonLeft, 2, mod.None) {
			t.Fatal("header double press should sort")
		}
	})
	s.Do(func() {
		if m := e.tableSortMark(id, 0); m != "▼" {
			t.Fatalf("double press toggles: %q", m)
		}
	})
}

func TestTableHeaderDoubleClickSorts(t *testing.T) {
	s, e := openEditor(t, tableNote)
	s.Sync()
	var p geom.Point
	s.Do(func() {
		r := e.PartRect(0, "header:1")
		if r.Width <= 0 || r.Height <= 0 {
			t.Fatalf("no header rect: %+v", r)
		}
		p = s.Screen.PanelPoint(e, r.Center())
	})
	steps := 0
	s.Do(func() { steps = e.Doc.UndoSteps() })
	// As a person would: the first press opens the header cell, the second
	// press of the double sorts by it.
	s.Screen.DoubleClick(p)
	s.Sync()
	s.Do(func() {
		got := e.Doc.Blocks[0].Text
		want := "| N | V |\n| --- | --- |\n| c | 2 |\n| b | 3 |\n| a | 10 |"
		if got != want {
			t.Fatalf("double-click should sort ascending:\n%s\nwant\n%s", got, want)
		}
		if e.Doc.UndoSteps() != steps+1 {
			t.Fatalf("sort should be one undo step, %d -> %d", steps, e.Doc.UndoSteps())
		}
		var id int64
		id = e.Doc.Blocks[0].ID
		if m := e.tableSortMark(id, 1); m != "▲" {
			t.Fatalf("mark after ascending: %q", m)
		}
	})
}

func TestTableStructureOps(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	steps := 0
	s.Do(func() { steps = e.Doc.UndoSteps() })
	s.Do(func() { e.insertTableRowAfter(0, 0) })
	s.Do(func() {
		tb := ParseTable(e.Doc.Blocks[0].Text)
		if tb.RowCount() != 3 || tb.Rows[1][0] != "" {
			t.Fatalf("row after (0,0): %+v", tb.Rows)
		}
		if e.Doc.UndoSteps() != steps+1 {
			t.Fatalf("insert row should be one undo step")
		}
		_ = id
	})
	s.Do(func() { e.Doc.Undo() })
	s.Do(func() {
		if ParseTable(e.Doc.Blocks[0].Text).RowCount() != 2 {
			t.Fatalf("undo should take the row back")
		}
	})
	s.Do(func() { e.insertTableColumnAfter(0, 0) })
	s.Do(func() {
		tb := ParseTable(e.Doc.Blocks[0].Text)
		if tb.ColumnCount() != 3 {
			t.Fatalf("columns: %+v", tb)
		}
	})
	s.Do(func() { e.removeTableColumn(0, 1) })
	s.Do(func() {
		if ParseTable(e.Doc.Blocks[0].Text).ColumnCount() != 2 {
			t.Fatalf("remove column")
		}
	})
	// Never the last column.
	s.Do(func() {
		e.removeTableColumn(0, 0)
		e.removeTableColumn(0, 0)
	})
	s.Do(func() {
		if ParseTable(e.Doc.Blocks[0].Text).ColumnCount() != 1 {
			t.Fatalf("one column must remain: %q", e.Doc.Blocks[0].Text)
		}
	})
	s.Do(func() { e.Doc.Undo() })
	// Alignment writes the delimiter, one step.
	s.Do(func() { e.setTableAlignment(0, 0, TableAlignCenter) })
	s.Do(func() {
		tb := ParseTable(e.Doc.Blocks[0].Text)
		if tb.Alignments[0] != TableAlignCenter {
			t.Fatalf("alignment: %+v", tb.Alignments)
		}
		if !strings.Contains(e.Doc.Blocks[0].Text, ":---:") {
			t.Fatalf("delimiter: %q", e.Doc.Blocks[0].Text)
		}
	})
}

func TestTableColumnWidths(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Do(func() { e.setTableColumnWidth(0, 0, 140) })
	s.Do(func() {
		b := &e.Doc.Blocks[0]
		if v, ok := b.Attr("cols"); !ok || v != "140," {
			t.Fatalf("cols=%q ok=%v attrs=%q", v, ok, b.Attrs)
		}
		if got := storedTableWidths(b); len(got) != 2 || got[0] != 140 || got[1] != 0 {
			t.Fatalf("stored: %v", got)
		}
		g, ok := e.gridFor(0)
		if !ok {
			t.Fatal("no grid")
		}
		// The dragged column keeps its width (in device pixels); the other
		// measures itself.
		if g.cols[0] < g.cols[1] {
			t.Fatalf("the sized column should be wider: %v", g.cols)
		}
	})
	// Only the dragged column is pinned.
	s.Do(func() { e.setTableColumnWidth(0, 1, 200) })
	s.Do(func() {
		if v, _ := e.Doc.Blocks[0].Attr("cols"); v != "140,200" {
			t.Fatalf("both widths: %q", v)
		}
	})
	// A drag previews before anything is written.
	s.Do(func() {
		g, _ := e.gridFor(0)
		before := append([]float32(nil), g.cols...)
		e.tableResize = &tableResizeState{blockID: e.Doc.Blocks[0].ID, col: 0, startX: 0, width: g.cols[0]}
		e.tableDragResize(geom.NewPoint(30, 0))
		g2, _ := e.gridFor(0)
		if g2.cols[0] <= before[0] {
			t.Fatalf("the grid should follow the drag: %v -> %v", before, g2.cols)
		}
		if v, _ := e.Doc.Blocks[0].Attr("cols"); v != "140,200" {
			t.Fatalf("the drag must not write until it ends: %q", v)
		}
		steps := e.Doc.UndoSteps()
		e.tableEndResize()
		if v, _ := e.Doc.Blocks[0].Attr("cols"); v == "140,200" {
			t.Fatalf("ending the drag should write: %q", v)
		}
		if e.Doc.UndoSteps() != steps+1 {
			t.Fatalf("the drag should be one undo step")
		}
		if e.tableResize != nil {
			t.Fatal("the drag should be over")
		}
	})
	// Resetting drops the key: plain Markdown again.
	s.Do(func() { e.clearTableWidths(0) })
	s.Do(func() {
		if _, ok := e.Doc.Blocks[0].Attr("cols"); ok {
			t.Fatalf("reset should drop cols: %q", e.Doc.Blocks[0].Attrs)
		}
	})
}

func TestTablePickerInserts(t *testing.T) {
	s, e := openEditor(t, "Intro\n")
	s.Do(func() { e.FocusBlock(0, len([]rune("Intro"))) })
	var id int64
	s.Do(func() {
		id = e.Doc.Blocks[0].ID
		e.openTablePicker(id, func(cols, rows int) { e.convertToTable(id, cols, rows) })
	})
	s.Sync()
	s.Do(func() {
		if !e.TablePickerOpen() {
			t.Fatal("the picker should be open")
		}
		if e.picker.hoverCols != 3 || e.picker.hoverRows != 3 {
			t.Fatalf("each opening starts at 3x3: %dx%d", e.picker.hoverCols, e.picker.hoverRows)
		}
	})
	// Arrows move the hover; Enter takes it.
	key(s, unison.KeyRight, mod.None)
	key(s, unison.KeyDown, mod.None)
	s.Do(func() {
		if e.picker.hoverCols != 4 || e.picker.hoverRows != 4 {
			t.Fatalf("arrows: %dx%d", e.picker.hoverCols, e.picker.hoverRows)
		}
	})
	key(s, unison.KeyReturn, mod.None)
	s.Sync()
	s.Do(func() {
		if e.TablePickerOpen() {
			t.Fatal("Enter should take the size")
		}
		b := e.Doc.Block(id)
		tb := ParseTable(b.Text)
		if !tb.Valid || tb.ColumnCount() != 4 || tb.RowCount() != 4 {
			t.Fatalf("4x4 grid: %+v %q", tb, b.Text)
		}
		if _, row, col, ok := e.TableCell(); !ok || row != -1 || col != 0 {
			t.Fatalf("the first cell should be live: (%d,%d) %v", row, col, ok)
		}
	})
	// Escape shuts the picker without touching the block.
	var id2 int64
	s.Do(func() {
		id2 = e.Doc.Blocks[0].ID
		e.openTablePicker(id2, func(cols, rows int) { e.convertToTable(id2, cols, rows) })
	})
	s.Sync()
	before := ""
	s.Do(func() { before = e.Doc.Blocks[0].Text })
	key(s, unison.KeyEscape, mod.None)
	s.Sync()
	s.Do(func() {
		if e.TablePickerOpen() {
			t.Fatal("Escape should shut the picker")
		}
		if e.Doc.Blocks[0].Text != before {
			t.Fatalf("Escape must not touch the block: %q", e.Doc.Blocks[0].Text)
		}
	})
	// Sizes clamp to the 8x8 grid.
	if c, r := pickerSize(0, 99); c != 1 || r != 8 {
		t.Fatalf("clamp: %dx%d", c, r)
	}
	if got := pickerSizeText(2, 5); got != "2 × 5" {
		t.Fatalf("size text: %q", got)
	}
}

// dragCell sweeps the pointer from one cell of block i to another, as a
// person dragging across the grid would.
func dragCell(t *testing.T, s *uitest.Session, e *Editor, i int, from, to string) {
	t.Helper()
	var a, b geom.Point
	s.Do(func() {
		ra := e.PartRect(i, from)
		rb := e.PartRect(i, to)
		if ra.Width <= 0 || ra.Height <= 0 || rb.Width <= 0 || rb.Height <= 0 {
			t.Fatalf("no cell rects: %+v %+v", ra, rb)
		}
		a = s.Screen.PanelPoint(e, ra.Center())
		b = s.Screen.PanelPoint(e, rb.Center())
	})
	s.Screen.Drag(a, b, 4)
	s.Sync()
}

// pressCell presses and releases on one cell of block i.
func pressCell(t *testing.T, s *uitest.Session, e *Editor, i int, part string) {
	t.Helper()
	var p geom.Point
	s.Do(func() {
		r := e.PartRect(i, part)
		if r.Width <= 0 || r.Height <= 0 {
			t.Fatalf("no cell rect: %+v", r)
		}
		p = s.Screen.PanelPoint(e, r.Center())
	})
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Sync()
}

func TestTableSweepSelectsRectangle(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Sync()
	dragCell(t, s, e, 0, "cell", "cell:1:1")
	s.Do(func() {
		if !e.HasTableSelection() {
			t.Fatal("a drag across cells should select a rectangle")
		}
		id, top, bottom, left, right, ok := e.TableSelection()
		if !ok || id != e.Doc.Blocks[0].ID || top != 0 || bottom != 1 || left != 0 || right != 1 {
			t.Fatalf("bounds: id=%d (%d..%d)x(%d..%d) ok=%v", id, top, bottom, left, right, ok)
		}
		if _, _, _, ok := e.TableCell(); ok {
			t.Fatal("a selection and a live cell are exclusive")
		}
		if e.tableField != nil {
			t.Fatal("the live field should be shut")
		}
		if !e.TableCellSelected(e.Doc.Blocks[0].ID, 1, 1) || e.TableCellSelected(e.Doc.Blocks[0].ID, -1, 0) {
			t.Fatal("only the swept cells are selected")
		}
	})
	// A press in one cell drops the rectangle and edits it instead.
	pressCell(t, s, e, 0, "cell:1:1")
	s.Do(func() {
		if e.HasTableSelection() {
			t.Fatal("a press into a cell should drop the rectangle")
		}
		if _, row, col, ok := e.TableCell(); !ok || row != 1 || col != 1 {
			t.Fatalf("the press should make (1,1) live, got (%d,%d) %v", row, col, ok)
		}
	})
}

func TestTableSweepCopy(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Sync()
	dragCell(t, s, e, 0, "cell", "cell:1:1")
	steps := 0
	s.Do(func() { steps = e.Doc.UndoSteps() })
	key(s, unison.KeyC, mod.Control)
	s.Sync()
	if got := unison.ClipboardGetText(); got != "| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |" {
		t.Fatalf("copied as a table of its own: %q", got)
	}
	// Copying keeps the rectangle and writes nothing.
	s.Do(func() {
		if !e.HasTableSelection() {
			t.Fatal("copy should keep the rectangle")
		}
		if e.Doc.UndoSteps() != steps {
			t.Fatal("copy should write nothing")
		}
	})
	// Header cells alone copy a header with no rows.
	s.Do(func() { e.clearTableSweep() })
	dragCell(t, s, e, 0, "header", "header:1")
	key(s, unison.KeyC, mod.Control)
	s.Sync()
	if got := unison.ClipboardGetText(); got != "| A | B |\n| --- | --- |" {
		t.Fatalf("header-only copy: %q", got)
	}
}

func TestTableSweepCut(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Sync()
	dragCell(t, s, e, 0, "cell", "cell:1:1")
	steps := 0
	s.Do(func() { steps = e.Doc.UndoSteps() })
	key(s, unison.KeyX, mod.Control)
	s.Sync()
	if got := unison.ClipboardGetText(); got != "| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |" {
		t.Fatalf("cut copies first: %q", got)
	}
	text, _ := blockText(s, e, 0)
	if text != "| A | B |\n| --- | --- |\n|  |  |\n|  |  |" {
		t.Fatalf("cut empties the rectangle: %q", text)
	}
	s.Do(func() {
		if e.HasTableSelection() {
			t.Fatal("cut should drop the rectangle")
		}
		if e.Doc.UndoSteps() != steps+1 {
			t.Fatal("the emptying should be one undo step")
		}
		e.Doc.Undo()
	})
	if text, _ := blockText(s, e, 0); text != twoCellNote {
		t.Fatalf("one undo should bring the cells back: %q", text)
	}
}

func TestTableSweepDeleteAndEscape(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Sync()
	dragCell(t, s, e, 0, "cell", "cell:0:1")
	key(s, unison.KeyBackspace, mod.None)
	s.Sync()
	text, _ := blockText(s, e, 0)
	if text != "| A | B |\n| --- | --- |\n|  |  |\n| 3 | 4 |" {
		t.Fatalf("Backspace should empty the rectangle: %q", text)
	}
	// Emptying keeps the rectangle for another press.
	s.Do(func() {
		if !e.HasTableSelection() {
			t.Fatal("Delete should keep the rectangle selected")
		}
	})
	key(s, unison.KeyEscape, mod.None)
	s.Sync()
	s.Do(func() {
		if e.HasTableSelection() {
			t.Fatal("Escape should drop the rectangle")
		}
	})
}

func TestTableSweepMenu(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Sync()
	var plain []string
	s.Do(func() {
		for _, it := range e.tableCellMenuItems(0, 0, 0) {
			plain = append(plain, it.Text)
		}
	})
	for _, want := range []string{"Copy selected cells", "Clear selected cells"} {
		for _, got := range plain {
			if got == want {
				t.Fatalf("no selection yet, but the menu offers %q", want)
			}
		}
	}
	dragCell(t, s, e, 0, "cell", "cell:1:1")
	s.Do(func() {
		items := e.tableCellMenuItems(0, 1, 1)
		if len(items) < 3 || items[0].Text != "Copy selected cells" || items[1].Text != "Clear selected cells" {
			names := make([]string, 0, len(items))
			for _, it := range items {
				names = append(names, it.Text)
			}
			t.Fatalf("the selection's commands should lead: %q", names)
		}
		before := e.Doc.Blocks[0].Text
		items[0].OnSelect()
		if e.Doc.Blocks[0].Text != before {
			t.Fatal("copy should write nothing")
		}
		if got := unison.ClipboardGetText(); got != "| A | B |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |" {
			t.Fatalf("menu copy: %q", got)
		}
		items[1].OnSelect()
		if text := e.Doc.Blocks[0].Text; text != "| A | B |\n| --- | --- |\n|  |  |\n|  |  |" {
			t.Fatalf("menu clear: %q", text)
		}
	})
}

func TestTableSweepEndsLiveCell(t *testing.T) {
	s, e := openEditor(t, twoCellNote)
	s.Do(func() { e.activateTableCell(0, 0, 0, false) })
	s.Sync()
	if name, text := focusedField(s); name != "Table cell" || text != "1" {
		t.Fatalf("the field: %q holding %q", name, text)
	}
	// A sweep starting in another cell ends the live one, keeping its text.
	s.Do(func() {
		if e.tableField == nil {
			t.Fatal("no live field")
		}
		e.tableField.SetText("kept")
	})
	dragCell(t, s, e, 0, "cell:0:1", "cell:1:1")
	s.Do(func() {
		if !e.HasTableSelection() {
			t.Fatal("the drag should select")
		}
		if _, _, _, ok := e.TableCell(); ok {
			t.Fatal("the selection should end the live cell")
		}
	})
	if text, _ := blockText(s, e, 0); !strings.Contains(text, "| kept | 2 |") {
		t.Fatalf("the ended edit keeps its text: %q", text)
	}
}
