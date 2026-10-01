package editor

// Pipe-table parse/serialize/mutate. A table block's content is the raw
// pipe-table Markdown; this maps it to a cell grid and back and applies every
// mutation as a whole-Markdown rewrite, so each edit is one undo step.
// Escaped pipes (\|) survive in cells; per-column alignment comes from the
// delimiter row's colons. Serialization is canonical, so hand-authored
// ragged/padded tables normalize on save.

import (
	"regexp"
	"sort"
	"strconv"
	"strings"
)

// TableAlign is one column's alignment.
type TableAlign int

const (
	TableAlignNone TableAlign = iota
	TableAlignLeft
	TableAlignCenter
	TableAlignRight
)

// Table is a pipe table read into cells.
type PipeTable struct {
	Valid      bool
	Headers    []string
	Alignments []TableAlign
	Rows       [][]string
}

// ColumnCount is the header's width; RowCount counts data rows only.
func (t PipeTable) ColumnCount() int { return len(t.Headers) }
func (t PipeTable) RowCount() int    { return len(t.Rows) }

var (
	reBrDelim   = regexp.MustCompile(`(?i)[ \t]*<br\s*/?>`)
	reDelimCell = regexp.MustCompile(`^:?-+:?$`)
)

// breaksToNewlines carries a cell's line breaks as Markdown can: <br>.
// Whitespace immediately before the tag is padding around it and goes away
// with the break; whitespace after it is content, where an indented line
// inside the cell keeps its indentation.
func breaksToNewlines(cell string) string {
	return reBrDelim.ReplaceAllString(cell, "\n")
}

// splitTableCells splits a table row on unescaped '|', dropping the optional
// leading/trailing border pipes, unescaping \| to | and trimming each cell.
func splitTableCells(line string) []string {
	s := strings.TrimSpace(line)
	var cells []string
	var cur strings.Builder
	flush := func() {
		cells = append(cells, breaksToNewlines(strings.TrimSpace(cur.String())))
		cur.Reset()
	}
	for i := 0; i < len(s); i++ {
		if s[i] == '\\' && i+1 < len(s) && s[i+1] == '|' {
			cur.WriteByte('|')
			i++
			continue
		}
		if s[i] == '|' {
			flush()
			continue
		}
		cur.WriteByte(s[i])
	}
	flush()
	if len(cells) > 1 && cells[0] == "" {
		cells = cells[1:]
	}
	if len(cells) > 1 && cells[len(cells)-1] == "" {
		cells = cells[:len(cells)-1]
	}
	return cells
}

// escapeTableCell writes a cell value back: pipes escaped, newlines as <br>
// so the row stays one line of the file.
func escapeTableCell(cell string) string {
	e := strings.ReplaceAll(cell, "|", `\|`)
	return strings.ReplaceAll(e, "\n", "<br>")
}

func tableAlignmentMarker(a TableAlign) string {
	switch a {
	case TableAlignLeft:
		return ":---"
	case TableAlignCenter:
		return ":---:"
	case TableAlignRight:
		return "---:"
	}
	return "---"
}

func tableAlignmentOf(delimCell string) TableAlign {
	c := strings.TrimSpace(delimCell)
	left := strings.HasPrefix(c, ":")
	right := strings.HasSuffix(c, ":")
	switch {
	case left && right:
		return TableAlignCenter
	case left:
		return TableAlignLeft
	case right:
		return TableAlignRight
	}
	return TableAlignNone
}

// IsTableDelimiterRow reports whether a line is a table delimiter row.
func IsTableDelimiterRow(line string) bool {
	cells := splitTableCells(line)
	if len(cells) == 0 {
		return false
	}
	for _, c := range cells {
		if !reDelimCell.MatchString(strings.TrimSpace(c)) {
			return false
		}
	}
	return true
}

// LooksLikeTableStart reports whether the two-line pair begins a pipe table:
// the header holds a pipe (so a lone --- never masquerades as a one-column
// table) and the delimiter has the same column count.
func LooksLikeTableStart(headerLine, delimiterLine string) bool {
	if !strings.Contains(headerLine, "|") {
		return false
	}
	if !IsTableDelimiterRow(delimiterLine) {
		return false
	}
	return len(splitTableCells(headerLine)) == len(splitTableCells(delimiterLine))
}

// TableCellCount is how many cells a row splits into, by parse's own rule.
func TableCellCount(line string) int { return len(splitTableCells(line)) }

// ParseTable reads pipe-table Markdown into a grid; Valid is false when it
// is not a table.
func ParseTable(markdown string) PipeTable {
	var t PipeTable
	lines := strings.Split(markdown, "\n")
	if len(lines) < 2 || !LooksLikeTableStart(lines[0], lines[1]) {
		return t
	}
	t.Headers = splitTableCells(lines[0])
	cols := len(t.Headers)
	delim := splitTableCells(lines[1])
	for c := 0; c < cols; c++ {
		if c < len(delim) {
			t.Alignments = append(t.Alignments, tableAlignmentOf(delim[c]))
		} else {
			t.Alignments = append(t.Alignments, TableAlignNone)
		}
	}
	for _, l := range lines[2:] {
		if strings.TrimSpace(l) == "" {
			continue
		}
		row := splitTableCells(l)
		for len(row) < cols {
			row = append(row, "")
		}
		t.Rows = append(t.Rows, row[:cols])
	}
	t.Valid = true
	return t
}

// SerializeTable writes a grid in canonical form: unpadded, one space around
// each cell. Cells are trimmed on the way out because they are trimmed on
// the way in; writing an untrimmed value would parse back to something else.
func SerializeTable(t PipeTable) string {
	rowLine := func(cells []string) string {
		escaped := make([]string, len(cells))
		for i, c := range cells {
			escaped[i] = escapeTableCell(strings.TrimSpace(c))
		}
		return "| " + strings.Join(escaped, " | ") + " |"
	}
	out := []string{rowLine(t.Headers)}
	delim := make([]string, len(t.Alignments))
	for i, a := range t.Alignments {
		delim[i] = tableAlignmentMarker(a)
	}
	out = append(out, "| "+strings.Join(delim, " | ")+" |")
	for _, row := range t.Rows {
		out = append(out, rowLine(row))
	}
	return strings.Join(out, "\n")
}

// TableCellValue reads one cell; row -1 is the header row, "" when outside.
func TableCellValue(markdown string, row, col int) string {
	t := ParseTable(markdown)
	if !t.Valid || col < 0 || col >= t.ColumnCount() {
		return ""
	}
	if row == -1 {
		return t.Headers[col]
	}
	if row >= 0 && row < t.RowCount() {
		return t.Rows[row][col]
	}
	return ""
}

// SetTableCell returns the Markdown with one cell set; row -1 addresses the
// header. A newline in the value rides as <br>, a pipe escaped. Out of range
// or invalid Markdown comes back unchanged.
func SetTableCell(markdown string, row, col int, value string) string {
	t := ParseTable(markdown)
	if !t.Valid || col < 0 || col >= t.ColumnCount() {
		return markdown
	}
	switch {
	case row == -1:
		t.Headers[col] = value
	case row >= 0 && row < t.RowCount():
		t.Rows[row][col] = value
	default:
		return markdown
	}
	return SerializeTable(t)
}

// InsertTableRow returns the Markdown with an empty data row after afterRow:
// -1 puts it at the top, rowCount-1 or beyond appends.
func InsertTableRow(markdown string, afterRow int) string {
	t := ParseTable(markdown)
	if !t.Valid {
		return markdown
	}
	empty := make([]string, t.ColumnCount())
	at := min(max(afterRow+1, 0), t.RowCount())
	t.Rows = append(t.Rows[:at:at], append([][]string{empty}, t.Rows[at:]...)...)
	return SerializeTable(t)
}

// InsertTableColumn returns the Markdown with an empty column after
// afterCol: -1 puts it at the left.
func InsertTableColumn(markdown string, afterCol int) string {
	t := ParseTable(markdown)
	if !t.Valid {
		return markdown
	}
	at := min(max(afterCol+1, 0), t.ColumnCount())
	t.Headers = append(t.Headers[:at:at], append([]string{""}, t.Headers[at:]...)...)
	t.Alignments = append(t.Alignments[:at:at], append([]TableAlign{TableAlignNone}, t.Alignments[at:]...)...)
	for i, row := range t.Rows {
		at := min(at, len(row))
		t.Rows[i] = append(row[:at:at], append([]string{""}, row[at:]...)...)
	}
	return SerializeTable(t)
}

// RemoveTableRow returns the Markdown without data row row.
func RemoveTableRow(markdown string, row int) string {
	t := ParseTable(markdown)
	if !t.Valid || row < 0 || row >= t.RowCount() {
		return markdown
	}
	t.Rows = append(t.Rows[:row], t.Rows[row+1:]...)
	return SerializeTable(t)
}

// RemoveTableColumn returns the Markdown without column col, never removing
// the last column.
func RemoveTableColumn(markdown string, col int) string {
	t := ParseTable(markdown)
	if !t.Valid || col < 0 || col >= t.ColumnCount() || t.ColumnCount() <= 1 {
		return markdown
	}
	t.Headers = append(t.Headers[:col], t.Headers[col+1:]...)
	t.Alignments = append(t.Alignments[:col], t.Alignments[col+1:]...)
	for i, row := range t.Rows {
		if col < len(row) {
			t.Rows[i] = append(row[:col], row[col+1:]...)
		}
	}
	return SerializeTable(t)
}

// SortTableColumn returns the Markdown with data rows sorted by a column's
// text, ascending or not, as one step. A column that parses as numbers on
// both sides sorts numerically (10 after 3); otherwise case-insensitively.
// The sort is stable.
func SortTableColumn(markdown string, col int, ascending bool) string {
	t := ParseTable(markdown)
	if !t.Valid || col < 0 || col >= t.ColumnCount() {
		return markdown
	}
	cell := func(row []string) string {
		if col < len(row) {
			return row[col]
		}
		return ""
	}
	sort.SliceStable(t.Rows, func(i, j int) bool {
		sa, sb := cell(t.Rows[i]), cell(t.Rows[j])
		da, erra := strconv.ParseFloat(strings.TrimSpace(sa), 64)
		db, errb := strconv.ParseFloat(strings.TrimSpace(sb), 64)
		var cmp int
		switch {
		case erra == nil && errb == nil:
			switch {
			case da < db:
				cmp = -1
			case da > db:
				cmp = 1
			}
		default:
			cmp = strings.Compare(strings.ToLower(sa), strings.ToLower(sb))
		}
		if ascending {
			return cmp < 0
		}
		return cmp > 0
	})
	return SerializeTable(t)
}

// SetTableAlignment returns the Markdown with a column's alignment set.
func SetTableAlignment(markdown string, col int, align TableAlign) string {
	t := ParseTable(markdown)
	if !t.Valid || col < 0 || col >= t.ColumnCount() {
		return markdown
	}
	t.Alignments[col] = align
	return SerializeTable(t)
}

// TableAlignmentName is the alignment's name in menus and tests.
func TableAlignmentName(a TableAlign) string {
	switch a {
	case TableAlignLeft:
		return "left"
	case TableAlignCenter:
		return "center"
	case TableAlignRight:
		return "right"
	}
	return "none"
}

// TableAlignmentFromName parses a menu name back; unknown names mean none.
func TableAlignmentFromName(name string) TableAlign {
	switch strings.ToLower(strings.TrimSpace(name)) {
	case "left":
		return TableAlignLeft
	case "center":
		return TableAlignCenter
	case "right":
		return TableAlignRight
	}
	return TableAlignNone
}

// EmptyTable builds an empty table of the given size, for the grid-picker
// insertion: at least one column, rows may be zero data rows.
func EmptyTable(columns, rows int) string {
	columns = max(columns, 1)
	rows = max(rows, 0)
	t := PipeTable{Valid: true}
	for c := 0; c < columns; c++ {
		t.Headers = append(t.Headers, "")
		t.Alignments = append(t.Alignments, TableAlignNone)
	}
	for r := 0; r < rows; r++ {
		t.Rows = append(t.Rows, make([]string, columns))
	}
	return SerializeTable(t)
}
