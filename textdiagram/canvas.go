package textdiagram

import "strings"

// Direction is a way a line or an arrowhead points on a Canvas.
type Direction int

const (
	Up Direction = iota
	Down
	Left
	Right
)

// The arms of a line cell: which of its four sides a line leaves through.
// A cell's character follows from its set of arms, so a line drawn across
// another becomes ┼, and one ending on a box's top edge becomes ┴, whichever
// is drawn first.
const (
	armUp    = 1
	armDown  = 2
	armLeft  = 4
	armRight = 8
)

// Canvas is a grid of characters that diagrams are drawn onto as text,
// ported from the Qt app's TextCanvas. It grows as cells are drawn, up to
// MaxCanvasRows, MaxCanvasCols and MaxCanvasCells; a cell beyond them is not
// drawn. Where lines meet, the cell shows the junction their arms make. It
// draws only the light box-drawing characters Repair recognizes, so Repair
// leaves its text unchanged.
//
// The zero Canvas is empty and ready to draw on.
type Canvas struct {
	lines   [][]rune
	arms    [][]uint8 // each cell's arms, beside lines; 0 when not a line cell
	doubles [][]bool  // each cell drawn with a double vertical, beside lines
	cells   int64     // the total of the rows' lengths
}

// Rows is the number of rows the grid has grown to.
func (c *Canvas) Rows() int { return len(c.lines) }

// Cols is the length of the longest row.
func (c *Canvas) Cols() int {
	width := 0
	for _, line := range c.lines {
		width = max(width, len(line))
	}
	return width
}

// Cells is the number of cells the grid holds across all its rows. It never
// passes MaxCanvasCells, which tests use to check that a drawing stayed
// inside the limit.
func (c *Canvas) Cells() int64 { return c.cells }

// At is the character at a cell, a space for a cell inside the grid that
// nothing was drawn on, and 0 outside the grid.
func (c *Canvas) At(row, col int) rune {
	if row < 0 || row >= len(c.lines) {
		return 0
	}
	line := c.lines[row]
	if col < 0 || col >= len(line) {
		return 0
	}
	return line[col]
}

// ensure grows the grid to hold the cell at row, col. It returns false when
// the cell is outside the limits, and the caller then draws nothing there.
func (c *Canvas) ensure(row, col int) bool {
	if row < 0 || col < 0 {
		return false
	}
	// A diagram's coordinates decide the rows and columns, so a note decides
	// how much this grid would hold without the limits.
	if row >= MaxCanvasRows || col >= MaxCanvasCols {
		return false
	}
	// The row and column limits do not bound their product: a box spanning
	// both grows every row it touches out to the far column. The total of
	// the rows' lengths is what the three per-row slices cost, so it is the
	// limit that has to hold.
	have := 0
	if row < len(c.lines) {
		have = len(c.lines[row])
	}
	wanted := int64(col) + 1 - int64(have)
	if wanted > 0 && c.cells+wanted > MaxCanvasCells {
		return false
	}
	for len(c.lines) <= row {
		c.lines = append(c.lines, nil)
		c.arms = append(c.arms, nil)
		c.doubles = append(c.doubles, nil)
	}
	for len(c.lines[row]) <= col {
		c.lines[row] = append(c.lines[row], ' ')
		c.arms[row] = append(c.arms[row], 0)
		c.doubles[row] = append(c.doubles[row], false)
	}
	if wanted > 0 {
		c.cells += wanted
	}
	return true
}

// Put writes a character into a cell, as text or an arrowhead is drawn: it
// replaces whatever was there, and the cell stops being part of a line.
func (c *Canvas) Put(row, col int, r rune) {
	if !c.ensure(row, col) {
		return
	}
	c.lines[row][col] = r
	c.arms[row][col] = 0
	c.doubles[row][col] = false
}

// DrawText writes text into a row starting at col, one character per cell.
func (c *Canvas) DrawText(row, col int, text string) {
	i := 0
	for _, r := range text {
		c.Put(row, col+i, r)
		i++
	}
}

// armsOf is the arms of a character already in a cell, so that a line drawn
// over one Put there earlier joins it. Only the characters this canvas draws
// need to be known.
func armsOf(r rune) int {
	switch r {
	case '─':
		return armLeft | armRight
	case '│', '║':
		return armUp | armDown
	case '┌':
		return armDown | armRight
	case '┐':
		return armDown | armLeft
	case '└':
		return armUp | armRight
	case '┘':
		return armUp | armLeft
	case '├':
		return armUp | armDown | armRight
	case '┤':
		return armUp | armDown | armLeft
	case '┬':
		return armDown | armLeft | armRight
	case '┴':
		return armUp | armLeft | armRight
	case '┼':
		return armUp | armDown | armLeft | armRight
	}
	return 0
}

// charForArms is the character a line cell with these arms shows. A cell
// with only vertical arms shows ║ when it is part of a double wall.
func charForArms(arms int, doubleVertical bool) rune {
	switch arms {
	case armLeft, armRight, armLeft | armRight:
		return '─'
	case armUp, armDown, armUp | armDown:
		if doubleVertical {
			return '║'
		}
		return '│'
	case armDown | armRight:
		return '┌'
	case armDown | armLeft:
		return '┐'
	case armUp | armRight:
		return '└'
	case armUp | armLeft:
		return '┘'
	case armUp | armDown | armRight:
		return '├'
	case armUp | armDown | armLeft:
		return '┤'
	case armDown | armLeft | armRight:
		return '┬'
	case armUp | armLeft | armRight:
		return '┴'
	case armUp | armDown | armLeft | armRight:
		return '┼'
	}
	return ' '
}

// mergeArms adds arms to a cell and redraws it as the junction they make.
// It returns false when the cell is outside the limits and nothing was
// drawn. Along one row the answer only goes from true to false as the
// column grows, since a further column costs more cells, so a horizontal
// line stops at the first false.
func (c *Canvas) mergeArms(row, col, arms int, doubleVertical bool) bool {
	if !c.ensure(row, col) {
		return false
	}
	existing := c.lines[row][col]
	current := int(c.arms[row][col])
	if current == 0 {
		current = armsOf(existing)
	}
	// A line never overwrites text: a cell holding anything other than a
	// line or a space keeps it.
	if current == 0 && existing != ' ' {
		return true
	}
	merged := current | arms
	dbl := c.doubles[row][col] || doubleVertical
	c.lines[row][col] = charForArms(merged, dbl)
	c.arms[row][col] = uint8(merged)
	c.doubles[row][col] = dbl
	return true
}

// DrawHLine draws a horizontal line along row from col1 to col2, both
// included, in either order.
func (c *Canvas) DrawHLine(row, col1, col2 int) {
	from, to := min(col1, col2), max(col1, col2)
	if from == to {
		c.mergeArms(row, from, armLeft|armRight, false)
		return
	}
	if !c.mergeArms(row, from, armRight, false) {
		return
	}
	for col := from + 1; col < to; col++ {
		if !c.mergeArms(row, col, armLeft|armRight, false) {
			return // the rest of the row is outside the limits too
		}
	}
	c.mergeArms(row, to, armLeft, false)
}

// DrawVLine draws a vertical line down col from row1 to row2, both included,
// in either order.
func (c *Canvas) DrawVLine(col, row1, row2 int) {
	from, to := min(row1, row2), max(row1, row2)
	if from == to {
		c.mergeArms(from, col, armUp|armDown, false)
		return
	}
	c.mergeArms(from, col, armDown, false)
	for row := from + 1; row < to; row++ {
		c.mergeArms(row, col, armUp|armDown, false)
	}
	c.mergeArms(to, col, armUp, false)
}

// DrawBox draws the outline of a box whose corners are the given rows and
// columns, in either order. The corners come out as ┌┐└┘, or as junctions
// where the box meets other lines. doubleWalls draws the side walls as ║,
// which is how the subroutine shape of a flowchart is drawn.
func (c *Canvas) DrawBox(top, left, bottom, right int, doubleWalls bool) {
	if bottom < top {
		top, bottom = bottom, top
	}
	if right < left {
		left, right = right, left
	}
	c.DrawHLine(top, left, right)
	if bottom > top {
		c.DrawHLine(bottom, left, right)
		c.mergeArms(top, left, armDown, false)
		c.mergeArms(top, right, armDown, false)
		for row := top + 1; row < bottom; row++ {
			c.mergeArms(row, left, armUp|armDown, doubleWalls)
			c.mergeArms(row, right, armUp|armDown, doubleWalls)
		}
		c.mergeArms(bottom, left, armUp, false)
		c.mergeArms(bottom, right, armUp, false)
	}
}

// DrawStub adds a single arm to one cell: the half cell that joins the end
// of a connector to the wall it leaves from. A one-cell line would add both
// arms of its axis; this adds only the one, so the cell becomes the corner
// the connector turns through rather than a dash pointing at nothing.
func (c *Canvas) DrawStub(row, col int, dir Direction) {
	switch dir {
	case Up:
		c.mergeArms(row, col, armUp, false)
	case Down:
		c.mergeArms(row, col, armDown, false)
	case Left:
		c.mergeArms(row, col, armLeft, false)
	case Right:
		c.mergeArms(row, col, armRight, false)
	}
}

// DrawArrowhead puts an arrowhead pointing in dir into a cell, replacing
// what was there: Down is ▼.
func (c *Canvas) DrawArrowhead(row, col int, dir Direction) {
	switch dir {
	case Up:
		c.Put(row, col, '▲')
	case Down:
		c.Put(row, col, '▼')
	case Left:
		c.Put(row, col, '◄')
	case Right:
		c.Put(row, col, '►')
	}
}

// String is the grid as text: its rows joined by "\n", each without its
// trailing spaces, and without blank rows at the end.
func (c *Canvas) String() string {
	out := make([]string, 0, len(c.lines))
	for _, line := range c.lines {
		out = append(out, strings.TrimRight(string(line), " "))
	}
	for len(out) > 0 && out[len(out)-1] == "" {
		out = out[:len(out)-1]
	}
	return strings.Join(out, "\n")
}
