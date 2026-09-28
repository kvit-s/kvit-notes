package textdiagram

// These tests are the Qt app's tests/test_diagramrepair.cpp, one Go test per
// test function there and in the same order, with the same inputs and
// expected outputs. The fixture tests/fixtures/llm-diagram.md is copied into
// testdata. Columns are rune offsets where the Qt tests use QString indexes,
// which are the same numbers here because every character in these inputs is
// inside the Basic Multilingual Plane. TestCorpusDump, like the Qt test, is
// skipped unless KVIT_REPAIR_DUMP names a file to write the repaired fixture
// to. The last test, TestSlidePastEndOfEdgeLineRefused, is not from the Qt
// tests: it holds an input on which the Qt code reads past the end of a line.

import (
	"os"
	"strings"
	"testing"
)

// fixtureBody is the fixture without the fence lines around it, which is
// what opening the note hands Repair.
func fixtureBody(t *testing.T) string {
	t.Helper()
	data, err := os.ReadFile("testdata/llm-diagram.md")
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Split(string(data), "\n")
	for len(lines) > 0 && (strings.HasPrefix(lines[0], "```") || trimSpace(lines[0]) == "") {
		lines = lines[1:]
	}
	for len(lines) > 0 &&
		(strings.HasPrefix(lines[len(lines)-1], "```") || trimSpace(lines[len(lines)-1]) == "") {
		lines = lines[:len(lines)-1]
	}
	return strings.Join(lines, "\n")
}

// lineOf is line row of text, or "" past the last line.
func lineOf(text string, row int) string {
	lines := strings.Split(text, "\n")
	if row < 0 || row >= len(lines) {
		return ""
	}
	return lines[row]
}

// indexRune is the rune offset of the first r in line at or after rune
// offset from, or -1.
func indexRune(line string, r rune, from int) int {
	rs := []rune(line)
	for i := from; i < len(rs); i++ {
		if rs[i] == r {
			return i
		}
	}
	return -1
}

// lastIndexRune is the rune offset of the last r in line, or -1.
func lastIndexRune(line string, r rune) int {
	rs := []rune(line)
	for i := len(rs) - 1; i >= 0; i-- {
		if rs[i] == r {
			return i
		}
	}
	return -1
}

// indexString is the rune offset of the first sub in line, or -1.
func indexString(line, sub string) int {
	i := strings.Index(line, sub)
	if i < 0 {
		return -1
	}
	return len([]rune(line[:i]))
}

// colOf is the column of the first ch on line row of text.
func colOf(text string, row int, ch rune) int { return indexRune(lineOf(text, row), ch, 0) }

func wantInt(t *testing.T, what string, got, want int) {
	t.Helper()
	if got != want {
		t.Errorf("%s = %d, want %d", what, got, want)
	}
}

func TestShortTopEdgeExtendsToWallColumn(t *testing.T) {
	// The top-right corner is two columns short of the side bars: the
	// OPERATOR box's flaw from the corpus, made small.
	in := "┌─────┐\n" +
		"│ ab    │\n" +
		"│ cd    │\n" +
		"└───────┘"
	out := Repair(in)
	wantInt(t, "column of ┐", colOf(out, 0, '┐'), 8)
	// The text inside is untouched.
	if !strings.Contains(out, "│ ab    │") || !strings.Contains(out, "│ cd    │") {
		t.Errorf("the labels changed:\n%s", out)
	}
}

func TestRaggedWallBarsAlignWithoutShifting(t *testing.T) {
	// One side bar sticks out two columns past the frame; the text to the
	// right of it must keep its column when the bar moves.
	in := "┌─────┐\n" +
		"│ ab  │   note\n" +
		"│ cd    │ note\n" +
		"└─────┘"
	out := Repair(in)
	line := lineOf(out, 2)
	wantInt(t, "column of the bar", indexRune(line, '│', 1), 6) // the bar pulled in
	wantInt(t, "column of note", indexString(line, "note"), 10) // the text stays
}

func TestBottomEdgeTrimsToMatch(t *testing.T) {
	in := "┌─────┐\n" +
		"│ ab  │\n" +
		"└───────┘"
	out := Repair(in)
	wantInt(t, "column of ┘", colOf(out, 2, '┘'), 6)
}

func TestJoggedConnectorStraightens(t *testing.T) {
	// The tee is two columns left of the bar and the arrowhead below it:
	// the flaw of the connector from TRUSTED CORE to MEMORY in the corpus,
	// made small.
	in := "┌────────┐\n" +
		"│ top    │\n" +
		"└──┬─────┘\n" +
		"    │\n" +
		"┌───▼────┐\n" +
		"│ bottom │\n" +
		"└────────┘"
	out := Repair(in)
	wantInt(t, "column of ┬", colOf(out, 2, '┬'), 4)
	wantInt(t, "column of │", colOf(out, 3, '│'), 4)
	wantInt(t, "column of ▼", colOf(out, 4, '▼'), 4)
}

func TestLabelBlocksBarMoveBoxStaysUntouched(t *testing.T) {
	// The stray bar cannot move left through the label, so the whole side is
	// left alone rather than repaired in part.
	in := "┌─────┐\n" +
		"│ abcdef│\n" +
		"│ cd  │\n" +
		"└─────┘"
	if out := Repair(in); out != in {
		t.Errorf("Repair changed the box:\n%s", out)
	}
}

func TestAsciiBoxStraightens(t *testing.T) {
	in := "+-----+\n" +
		"| ab    |\n" +
		"| cd    |\n" +
		"+-------+"
	out := Repair(in)
	wantInt(t, "column of the last +", lastIndexRune(lineOf(out, 0), '+'), 8)
}

func TestStraightDiagramUntouched(t *testing.T) {
	in := "┌─────┐   ┌─────┐\n" +
		"│ ab  │ ─►│ cd  │\n" +
		"└──┬──┘   └─────┘\n" +
		"   │\n" +
		"   ▼"
	if out := Repair(in); out != in {
		t.Errorf("Repair changed a straight diagram:\n%s", out)
	}
}

func TestTabsDisableRepair(t *testing.T) {
	in := "┌─────┐\n" +
		"│\tab    │\n" +
		"└───────┘"
	if out := Repair(in); out != in {
		t.Errorf("Repair changed a body with a tab:\n%s", out)
	}
}

func TestIdempotentOnCorpus(t *testing.T) {
	body := fixtureBody(t)
	if body == "" {
		t.Fatal("the fixture is empty")
	}
	once := Repair(body)
	if once == body {
		t.Error("Repair left the corpus unchanged, but it is known to have flaws")
	}
	if twice := Repair(once); twice != once {
		t.Errorf("repairing again changed the text:\n%s", twice)
	}
}

func TestCorpusEdgesAlignAfterRepair(t *testing.T) {
	out := Repair(fixtureBody(t))
	lines := strings.Split(out, "\n")
	at := func(row int) string {
		if row < len(lines) {
			return lines[row]
		}
		return ""
	}
	// The OPERATOR box (rows 0 to 4): the top-right corner reaches the wall
	// column, 67.
	wantInt(t, "row 0 ┐", indexRune(at(0), '┐', 0), 67)
	wantInt(t, "row 4 ┘", indexRune(at(4), '┘', 0), 67)
	// The TRUSTED CORE box (rows 6 to 10): the right edge is at 72.
	wantInt(t, "row 6 ┐", indexRune(at(6), '┐', 0), 72)
	wantInt(t, "row 10 ┘", indexRune(at(10), '┘', 0), 72)
	// The jogged connector into MEMORY: tee, bar and arrowhead in one
	// column.
	tee := lastIndexRune(at(10), '┬')
	wantInt(t, "row 11 │", lastIndexRune(at(11), '│'), tee)
	wantInt(t, "row 12 ▼", lastIndexRune(at(12), '▼'), tee)
}

func TestRepairedCorpusStillClassifiesAsDiagram(t *testing.T) {
	if !LooksLikeDiagram(Repair(fixtureBody(t))) {
		t.Error("the repaired corpus is not a diagram")
	}
}

func TestOversizedBodyUntouched(t *testing.T) {
	big := "┌─────┐\n│ ab    │\n└───────┘\n" + strings.Repeat("x", RepairCapChars)
	if out := Repair(big); out != big {
		t.Error("Repair changed a body over the cap")
	}
}

// Not a check: writes the repaired corpus for reading by eye when
// KVIT_REPAIR_DUMP names a file.
func TestCorpusDump(t *testing.T) {
	path := os.Getenv("KVIT_REPAIR_DUMP")
	if path == "" {
		t.Skip("set KVIT_REPAIR_DUMP=<path.txt> to write the repaired corpus")
	}
	if err := os.WriteFile(path, []byte(Repair(fixtureBody(t))), 0o644); err != nil {
		t.Fatal(err)
	}
}

// Not from the Qt tests. A connector whose column is past the end of the
// edge line it starts on: sliding the '+' there reads one cell past the end
// of that line. The Qt code reads the string's terminating 0 there in a
// release build (a debug build stops on an assertion) and refuses the slide,
// so the diagram is left as it is.
func TestSlidePastEndOfEdgeLineRefused(t *testing.T) {
	in := "┌──+\n" +
		"│  |\n" +
		"└──+\n" +
		"    |\n" +
		"    |"
	if out := Repair(in); out != in {
		t.Errorf("Repair changed the diagram:\n%s", out)
	}
}
