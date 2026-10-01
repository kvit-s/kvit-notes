package textdiagram

// These tests check the Canvas: how it grows, lines and their end points,
// the junction drawn where arms meet (a table in TestJunctionTable, one
// subtest per row), boxes and double walls, lines that never draw over
// text, arrowheads, trimmed output, and the cell limits. A cell outside the
// grid reads as the rune 0.

import "testing"

func wantAt(t *testing.T, c *Canvas, row, col int, want rune) {
	t.Helper()
	if got := c.At(row, col); got != want {
		t.Errorf("At(%d, %d) = %q, want %q", row, col, got, want)
	}
}

func TestGrowthAndPut(t *testing.T) {
	var canvas Canvas
	if canvas.Rows() != 0 {
		t.Fatalf("rows = %d, want 0", canvas.Rows())
	}
	canvas.Put(2, 5, 'x')
	if canvas.Rows() != 3 {
		t.Errorf("rows = %d, want 3", canvas.Rows())
	}
	wantAt(t, &canvas, 2, 5, 'x')
	wantAt(t, &canvas, 2, 4, ' ')
	wantAt(t, &canvas, 9, 9, 0) // outside stays 0
}

func TestHLineAndVLineEndpoints(t *testing.T) {
	var canvas Canvas
	canvas.DrawHLine(0, 2, 6)
	for col := 2; col <= 6; col++ {
		wantAt(t, &canvas, 0, col, '─')
	}
	canvas.DrawVLine(0, 2, 5)
	for row := 2; row <= 5; row++ {
		wantAt(t, &canvas, row, 0, '│')
	}
	// Reversed coordinates draw the same line.
	var reversed Canvas
	reversed.DrawHLine(0, 6, 2)
	wantAt(t, &reversed, 0, 4, '─')
}

func TestJunctionTable(t *testing.T) {
	// Every meeting of a horizontal line on row 2 and a vertical line down
	// column 2, at cell (2, 2).
	rows := []struct {
		name         string
		hCol1, hCol2 int
		vRow1, vRow2 int
		expected     rune
	}{
		{"cross", 0, 4, 0, 4, '┼'},
		{"tee-down", 0, 4, 2, 4, '┬'},
		{"tee-up", 0, 4, 0, 2, '┴'},
		{"tee-right", 2, 4, 0, 4, '├'},
		{"tee-left", 0, 2, 0, 4, '┤'},
		{"corner-tl", 2, 4, 2, 4, '┌'},
		{"corner-tr", 0, 2, 2, 4, '┐'},
		{"corner-bl", 2, 4, 0, 2, '└'},
		{"corner-br", 0, 2, 0, 2, '┘'},
	}
	for _, r := range rows {
		t.Run(r.name, func(t *testing.T) {
			// Either drawing order makes the same junction.
			var hFirst Canvas
			hFirst.DrawHLine(2, r.hCol1, r.hCol2)
			hFirst.DrawVLine(2, r.vRow1, r.vRow2)
			wantAt(t, &hFirst, 2, 2, r.expected)

			var vFirst Canvas
			vFirst.DrawVLine(2, r.vRow1, r.vRow2)
			vFirst.DrawHLine(2, r.hCol1, r.hCol2)
			wantAt(t, &vFirst, 2, 2, r.expected)
		})
	}
}

func TestBoxCornersAndWalls(t *testing.T) {
	var canvas Canvas
	canvas.DrawBox(1, 2, 4, 8, false)
	wantAt(t, &canvas, 1, 2, '┌')
	wantAt(t, &canvas, 1, 8, '┐')
	wantAt(t, &canvas, 4, 2, '└')
	wantAt(t, &canvas, 4, 8, '┘')
	wantAt(t, &canvas, 1, 5, '─')
	wantAt(t, &canvas, 2, 2, '│')
	wantAt(t, &canvas, 3, 8, '│')
	wantAt(t, &canvas, 2, 5, ' ') // the inside is untouched
}

func TestTouchingBoxesShareJunctions(t *testing.T) {
	// Two boxes sharing a column make ┬ and ┴ junctions there rather than
	// overwriting each other's corners.
	var canvas Canvas
	canvas.DrawBox(0, 0, 2, 4, false)
	canvas.DrawBox(0, 4, 2, 8, false)
	wantAt(t, &canvas, 0, 4, '┬')
	wantAt(t, &canvas, 2, 4, '┴')

	// A line ending on a box's wall becomes a junction into it.
	var withLine Canvas
	withLine.DrawBox(0, 4, 2, 8, false)
	withLine.DrawHLine(1, 0, 4)
	wantAt(t, &withLine, 1, 4, '┤')
}

func TestDoubleWalls(t *testing.T) {
	var canvas Canvas
	canvas.DrawBox(0, 0, 3, 6, true)
	wantAt(t, &canvas, 1, 0, '║')
	wantAt(t, &canvas, 2, 6, '║')
	wantAt(t, &canvas, 0, 0, '┌') // the corners stay light
}

func TestLinesNeverEatText(t *testing.T) {
	var canvas Canvas
	canvas.DrawText(1, 3, "hi")
	canvas.DrawHLine(1, 0, 8)
	wantAt(t, &canvas, 1, 3, 'h')
	wantAt(t, &canvas, 1, 4, 'i')
	wantAt(t, &canvas, 1, 2, '─')
	wantAt(t, &canvas, 1, 5, '─')

	// Text drawn later does replace line cells, as labels sit on edges.
	canvas.DrawText(1, 6, "yo")
	wantAt(t, &canvas, 1, 6, 'y')
}

func TestArrowheads(t *testing.T) {
	var canvas Canvas
	canvas.DrawArrowhead(0, 0, Up)
	canvas.DrawArrowhead(0, 1, Down)
	canvas.DrawArrowhead(0, 2, Left)
	canvas.DrawArrowhead(0, 3, Right)
	wantAt(t, &canvas, 0, 0, '▲')
	wantAt(t, &canvas, 0, 1, '▼')
	wantAt(t, &canvas, 0, 2, '◄')
	wantAt(t, &canvas, 0, 3, '►')
}

func TestToStringTrims(t *testing.T) {
	var canvas Canvas
	canvas.Put(0, 0, 'a')
	canvas.Put(0, 4, 'b')
	canvas.Put(1, 0, 'c')
	canvas.Put(3, 8, ' ') // grows the rows and columns with only spaces
	if got := canvas.String(); got != "a   b\nc" {
		t.Errorf("String() = %q, want %q", got, "a   b\nc")
	}
}

// The row and column limits bound each dimension on its own, and their
// product is 4 × 10^8 cells. A box spanning both reaches it, since every row
// it touches grows out to the far column for the right-hand wall. The cell
// total is therefore the limit that has to bind, and drawing past it is cut
// off.
func TestCellBudgetClipsWideAndTallDrawing(t *testing.T) {
	var canvas Canvas
	canvas.DrawBox(0, 0, MaxCanvasRows-1, MaxCanvasCols-1, false)
	if canvas.Cells() > MaxCanvasCells {
		t.Errorf("materialized %d cells", canvas.Cells())
	}

	// Cut off, not corrupted: what fitted is still the drawing asked for.
	wantAt(t, &canvas, 0, 0, '┌')
	wantAt(t, &canvas, 0, 1, '─')

	// The limit holds however the grid is filled, including one row at a
	// time from an empty canvas.
	var rows Canvas
	for row := 0; row < MaxCanvasRows; row++ {
		rows.DrawHLine(row, 0, MaxCanvasCols-1)
	}
	if rows.Cells() > MaxCanvasCells {
		t.Errorf("materialized %d cells", rows.Cells())
	}
}

func TestCellBudgetLeavesOrdinaryDrawingsIntact(t *testing.T) {
	// A diagram far larger than any real one still draws in full: the limit
	// is there for hostile input.
	var canvas Canvas
	canvas.DrawBox(0, 0, 400, 400, false)
	wantAt(t, &canvas, 400, 400, '┘')
	if canvas.Rows() != 401 {
		t.Errorf("rows = %d, want 401", canvas.Rows())
	}
	if canvas.Cols() != 401 {
		t.Errorf("cols = %d, want 401", canvas.Cols())
	}
}
