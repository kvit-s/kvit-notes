package editor

// The app's tests/test_tabledata.cpp, ported: the pipe-table
// parse/serialize/mutate core behind every grid edit.

import (
	"slices"
	"testing"
)

func TestTableParseBasic(t *testing.T) {
	tb := ParseTable("| A | B | C |\n| :--- | :---: | ---: |\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |")
	if !tb.Valid || tb.ColumnCount() != 3 || tb.RowCount() != 2 {
		t.Fatalf("table: %+v", tb)
	}
	if !slices.Equal(tb.Headers, []string{"A", "B", "C"}) {
		t.Errorf("headers %q", tb.Headers)
	}
	if !slices.Equal(tb.Alignments, []TableAlign{TableAlignLeft, TableAlignCenter, TableAlignRight}) {
		t.Errorf("alignments %v", tb.Alignments)
	}
	if !slices.Equal(tb.Rows[0], []string{"1", "2", "3"}) || !slices.Equal(tb.Rows[1], []string{"4", "5", "6"}) {
		t.Errorf("rows %q", tb.Rows)
	}
}

func TestTableParseNoOuterPipesAndEscapedPipe(t *testing.T) {
	tb := ParseTable("a \\| b | c\n--- | ---\nx | y")
	if !tb.Valid || tb.ColumnCount() != 2 {
		t.Fatalf("table: %+v", tb)
	}
	if !slices.Equal(tb.Headers, []string{"a | b", "c"}) {
		t.Errorf("headers %q", tb.Headers)
	}
	if !slices.Equal(tb.Rows[0], []string{"x", "y"}) {
		t.Errorf("row %q", tb.Rows[0])
	}
}

func TestTableRaggedRowsSquareUp(t *testing.T) {
	tb := ParseTable("| A | B |\n| --- | --- |\n| 1 |\n| 2 | 3 | 4 |")
	if !tb.Valid {
		t.Fatalf("table: %+v", tb)
	}
	if !slices.Equal(tb.Rows[0], []string{"1", ""}) {
		t.Errorf("short row %q", tb.Rows[0])
	}
	if !slices.Equal(tb.Rows[1], []string{"2", "3"}) {
		t.Errorf("long row %q", tb.Rows[1])
	}
}

func TestTableInvalidTables(t *testing.T) {
	if ParseTable("just text").Valid {
		t.Error("plain text is not a table")
	}
	if ParseTable("| A | B |").Valid {
		t.Error("no delimiter row is not a table")
	}
	if ParseTable("| A |\n| not a delim |").Valid {
		t.Error("a non-delimiter second line is not a table")
	}
	if LooksLikeTableStart("Heading", "---") {
		t.Error("a lone divider is not a one-column table: the header needs a pipe")
	}
	// A delimiter with a different column count does not start a table.
	if ParseTable("| A | B |\n| --- |").Valid {
		t.Error("mismatched delimiter width is not a table")
	}
}

func TestTableSerializeIsCanonicalAndIdempotent(t *testing.T) {
	canonical := "| A | B |\n| :--- | ---: |\n| 1 | 2 |"
	if got := SerializeTable(ParseTable(canonical)); got != canonical {
		t.Errorf("canonical:\n%q\nwant\n%q", got, canonical)
	}
	padded := "|  A  |   B |\n|:----|----:|\n| 1   | 2   |"
	if got := SerializeTable(ParseTable(padded)); got != canonical {
		t.Errorf("padded normalizes:\n%q\nwant\n%q", got, canonical)
	}
}

func TestTableSetCellHeaderAndBody(t *testing.T) {
	md := "| A | B |\n| --- | --- |\n| 1 | 2 |"
	if got := SetTableCell(md, -1, 0, "Name"); got != "| Name | B |\n| --- | --- |\n| 1 | 2 |" {
		t.Errorf("header:\n%s", got)
	}
	if got := SetTableCell(md, 0, 1, "x"); got != "| A | B |\n| --- | --- |\n| 1 | x |" {
		t.Errorf("body:\n%s", got)
	}
	if got := SetTableCell(md, 0, 0, "a\nb"); got != "| A | B |\n| --- | --- |\n| a<br>b | 2 |" {
		t.Errorf("a newline rides as <br>:\n%s", got)
	}
	if got := SetTableCell(md, 0, 0, "a|b"); got != "| A | B |\n| --- | --- |\n| a\\|b | 2 |" {
		t.Errorf("a pipe is escaped:\n%s", got)
	}
	if got := SetTableCell(md, 5, 0, "x"); got != md {
		t.Error("a row outside the grid must leave the Markdown alone")
	}
}

func TestTableSetCellTrimsSurroundingWhitespace(t *testing.T) {
	md := "| A | B |\n| --- | --- |\n| 1 | 2 |"
	// Writing a cell is idempotent against the whitespace parse throws
	// away, so the live cell is not handed a rewrite for a space it has
	// not finished typing.
	if got := SetTableCell(md, 0, 0, "1 "); got != md {
		t.Errorf("trailing space:\n%s", got)
	}
	if got := SetTableCell(md, 0, 0, " 1"); got != md {
		t.Errorf("leading space:\n%s", got)
	}
	if got := SetTableCell(md, -1, 0, "A  "); got != md {
		t.Errorf("header trailing spaces:\n%s", got)
	}
	if got := SetTableCell(md, 0, 0, "  x  "); got != "| A | B |\n| --- | --- |\n| x | 2 |" {
		t.Errorf("trimmed:\n%s", got)
	}
	tb := ParseTable(md)
	tb.Rows[0][0] = " 1 "
	if got := SerializeTable(tb); got != md {
		t.Errorf("serialize is the exact inverse of parse:\n%s", got)
	}
}

func TestTableInsertAndRemoveRowsColumns(t *testing.T) {
	md := "| A | B |\n| --- | --- |\n| 1 | 2 |"
	if got := InsertTableRow(md, 0); got != "| A | B |\n| --- | --- |\n| 1 | 2 |\n|  |  |" {
		t.Errorf("insert row:\n%s", got)
	}
	if got := InsertTableRow(md, -1); got != "| A | B |\n| --- | --- |\n|  |  |\n| 1 | 2 |" {
		t.Errorf("insert row at top:\n%s", got)
	}
	if got := InsertTableColumn(md, 1); got != "| A | B |  |\n| --- | --- | --- |\n| 1 | 2 |  |" {
		t.Errorf("insert column:\n%s", got)
	}
	if got := RemoveTableRow(md, 0); got != "| A | B |\n| --- | --- |" {
		t.Errorf("remove row:\n%s", got)
	}
	if got := RemoveTableColumn(md, 0); got != "| B |\n| --- |\n| 2 |" {
		t.Errorf("remove column:\n%s", got)
	}
	if got := RemoveTableColumn("| A |\n| --- |\n| 1 |", 0); got != "| A |\n| --- |\n| 1 |" {
		t.Errorf("the last column cannot be removed:\n%s", got)
	}
}

func TestTableSortByColumn(t *testing.T) {
	md := "| N | V |\n| --- | --- |\n| b | 3 |\n| a | 10 |\n| c | 2 |"
	if got := SortTableColumn(md, 0, true); got != "| N | V |\n| --- | --- |\n| a | 10 |\n| b | 3 |\n| c | 2 |" {
		t.Errorf("text sort:\n%s", got)
	}
	if got := SortTableColumn(md, 1, true); got != "| N | V |\n| --- | --- |\n| c | 2 |\n| b | 3 |\n| a | 10 |" {
		t.Errorf("a numeric column sorts numerically:\n%s", got)
	}
	if got := SortTableColumn(md, 1, false); got != "| N | V |\n| --- | --- |\n| a | 10 |\n| b | 3 |\n| c | 2 |" {
		t.Errorf("descending:\n%s", got)
	}
}

func TestTableSetAlignmentAndEmptyTable(t *testing.T) {
	md := "| A |\n| --- |\n| 1 |"
	if got := SetTableAlignment(md, 0, TableAlignCenter); got != "| A |\n| :---: |\n| 1 |" {
		t.Errorf("align:\n%s", got)
	}
	if got := EmptyTable(2, 1); got != "|  |  |\n| --- | --- |\n|  |  |" {
		t.Errorf("empty:\n%s", got)
	}
	if got := TableCellValue(md, -1, 0); got != "A" {
		t.Errorf("header value %q", got)
	}
	if got := TableCellValue(md, 0, 0); got != "1" {
		t.Errorf("body value %q", got)
	}
}

func TestTableBrTagsBecomeLineBreaks(t *testing.T) {
	tb := ParseTable("| a<br>b | c |\n| --- | --- |\n| d<br/>e | f <BR /> g |")
	if !tb.Valid {
		t.Fatalf("table: %+v", tb)
	}
	if !slices.Equal(tb.Headers, []string{"a\nb", "c"}) {
		t.Errorf("headers %q", tb.Headers)
	}
	if !slices.Equal(tb.Rows[0], []string{"d\ne", "f\n g"}) {
		t.Errorf("row %q", tb.Rows[0])
	}
	canonical := SerializeTable(tb)
	if SerializeTable(ParseTable(canonical)) != canonical {
		t.Errorf("not byte-stable:\n%s", canonical)
	}
	if count := countLines(canonical); count != 2 {
		t.Errorf("three rows hold two breaks: %q", canonical)
	}
	if !contains(canonical, "a<br>b") {
		t.Errorf("breaks write back as tags:\n%s", canonical)
	}
}

func TestTableMultiLineCellKeepsIndentation(t *testing.T) {
	md := SetTableCell("| Step | Code |\n| --- | --- |\n|  |  |", 0, 1, "for i in rows\n    for j in cols\n        sum += a * b")
	want := "| Step | Code |\n| --- | --- |\n|  | for i in rows<br>    for j in cols<br>        sum += a * b |"
	if md != want {
		t.Errorf("multiline:\n%s\nwant\n%s", md, want)
	}
	tb := ParseTable(md)
	if tb.Rows[0][1] != "for i in rows\n    for j in cols\n        sum += a * b" {
		t.Errorf("indentation %q", tb.Rows[0][1])
	}
	piped := SetTableCell(md, 0, 1, "a | b\nc")
	if ParseTable(piped).Rows[0][1] != "a | b\nc" {
		t.Errorf("a pipe in a multiline cell must not split the row:\n%s", piped)
	}
}

func countLines(s string) int {
	n := 0
	for _, r := range s {
		if r == '\n' {
			n++
		}
	}
	return n
}

func contains(s, sub string) bool {
	return len(s) >= len(sub) && (func() bool {
		for i := 0; i+len(sub) <= len(s); i++ {
			if s[i:i+len(sub)] == sub {
				return true
			}
		}
		return false
	})()
}
