package textdiagram

// These tests are the app's tests/test_diagramclassifier.cpp, one Go test
// per test function there and in the same order, with the same inputs and
// expected outputs. The fixture tests/fixtures/llm-diagram.md is copied into
// testdata. The two timing tests keep the tests' limit of 250 ms and log
// the time as the tests print it.

import (
	"os"
	"strings"
	"testing"
	"time"
)

// fenceBody is the body of the first fenced block in a Markdown file, which
// is what opening a note hands the classifier.
func fenceBody(t *testing.T, path string) string {
	t.Helper()
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	lines := strings.Split(strings.ReplaceAll(string(data), "\r\n", "\n"), "\n")
	open, close := -1, -1
	for i, l := range lines {
		if strings.HasPrefix(trimSpace(l), "```") {
			if open < 0 {
				open = i
			} else {
				close = i
				break
			}
		}
	}
	if open < 0 || close < 0 {
		return ""
	}
	return strings.Join(lines[open+1:close], "\n")
}

func wantDiagram(t *testing.T, body string, want bool) {
	t.Helper()
	r := Classify(body)
	if r.IsDiagram != want {
		t.Errorf("IsDiagram = %v, want %v: %s", r.IsDiagram, want, strings.Join(r.Reasons, "; "))
	}
}

// The checked-in fixture, read from disk so the test shows that the file
// itself, with its misalignments, classifies as a diagram.
func TestClassifiesCanonicalFixture(t *testing.T) {
	body := fenceBody(t, "testdata/llm-diagram.md")
	if body == "" {
		t.Fatal("could not read the fence body of llm-diagram.md")
	}
	wantDiagram(t, body, true)
}

func TestClassifiesMisalignedFixture(t *testing.T) {
	// Heavier column shifts, ASCII '+' corners mixed with Unicode strokes,
	// ragged widths; still two framed regions joined by an arrow.
	body := "   +-----------------+\n" +
		"   |   INGEST  node  |\n" +
		"   |  parse · repair |\n" +
		"   +--------+--------+\n" +
		"            |  hands off\n" +
		"            v\n" +
		"      ┌────────────────────┐\n" +
		"      │   RENDER  (stage)    │\n" +
		"      │ layout → paint       │\n" +
		"      └──────────────────────┘\n"
	wantDiagram(t, body, true)
}

func TestRejectsTreeListing(t *testing.T) {
	body := "project/\n" +
		"├── src/\n" +
		"│   ├── main.cpp\n" +
		"│   └── util.cpp\n" +
		"├── tests/\n" +
		"│   └── test_main.cpp\n" +
		"└── README.md\n"
	wantDiagram(t, body, false)
}

func TestRejectsPsqlConsoleTable(t *testing.T) {
	body := "+----+-------+---------+\n" +
		"| id | name  | balance |\n" +
		"+----+-------+---------+\n" +
		"|  1 | Alice |   10.00 |\n" +
		"|  2 | Bob   |    5.50 |\n" +
		"|  3 | Carol |    0.00 |\n" +
		"+----+-------+---------+\n"
	wantDiagram(t, body, false)
}

func TestRejectsMarkdownTable(t *testing.T) {
	body := "| Name  | Role      | Notes |\n" +
		"|-------|-----------|-------|\n" +
		"| Alice | Lead      | on    |\n" +
		"| Bob   | Reviewer  | off   |\n"
	wantDiagram(t, body, false)
}

func TestRejectsSourceCode(t *testing.T) {
	body := "int main(int argc, char **argv) {\n" +
		"    auto x = compute(argc);\n" +
		"    for (int i = 0; i < x; ++i) {\n" +
		"        process(i);\n" +
		"    }\n" +
		"    return 0;\n" +
		"}\n"
	wantDiagram(t, body, false)
}

func TestRejectsShellTranscript(t *testing.T) {
	body := "$ git status\n" +
		"On branch main\n" +
		"nothing to commit, working tree clean\n" +
		"$ ls -la\n" +
		"total 24\n" +
		"drwxr-xr-x  3 sk sk 4096 build\n"
	wantDiagram(t, body, false)
}

func TestRejectsStackTrace(t *testing.T) {
	body := "Traceback (most recent call last):\n" +
		"  File \"app.py\", line 42, in <module>\n" +
		"    main()\n" +
		"  File \"app.py\", line 30, in main\n" +
		"    raise ValueError(\"boom\")\n" +
		"ValueError: boom\n"
	wantDiagram(t, body, false)
}

func TestRejectsProse(t *testing.T) {
	body := "The quick brown fox jumps over the lazy dog.\n" +
		"It was the best of times, it was the worst of times.\n" +
		"All that glitters is not gold.\n"
	wantDiagram(t, body, false)
}

func TestRejectsSingleBoxWithArrow(t *testing.T) {
	// One box and a lone arrow are not enough.
	body := "┌─────────────┐\n" +
		"│   only box  │\n" +
		"└──────┬──────┘\n" +
		"       ▼\n" +
		"   dangling label\n"
	wantDiagram(t, body, false)
}

func TestRejectsDecorativeRule(t *testing.T) {
	body := "Section A\n" +
		"──────────────────────────\n" +
		"Some body text goes here.\n" +
		"──────────────────────────\n" +
		"Section B\n"
	wantDiagram(t, body, false)
}

func TestOversizedFenceNeverTagged(t *testing.T) {
	const piece = "┌──┐\n│ x│\n└──┘\n"
	var big strings.Builder
	for size := 0; size <= InspectionCapChars; size += utf16Len(piece) {
		big.WriteString(piece)
	}
	if Classify(big.String()).IsDiagram {
		t.Error("an oversized fence was tagged")
	}
}

// Two dense rows of vertical strokes whose columns never come within two of
// each other, the worst case for the count of recurring columns: nothing
// stops it early, and comparing every column with every column would read
// them all. Both rows fit inside the inspection cap, so a paste or a note
// can hold them.
func TestAdversarialVerticalColumnsStayCheap(t *testing.T) {
	// Box-drawing strokes, so each row is also a line with diagram evidence
	// and the count is reached.
	const n = 50000
	content := strings.Repeat("│", n) + "\n" +
		strings.Repeat(" ", n+3) + strings.Repeat("│", n) +
		"\n┌──┐\n"
	if utf16Len(content) > InspectionCapChars {
		t.Fatalf("the input is %d units, over the cap", utf16Len(content))
	}

	start := time.Now()
	Classify(content)
	ms := time.Since(start).Milliseconds()
	t.Logf("adversarial classify took %d ms", ms)
	if ms >= 250 {
		t.Errorf("classify took %d ms", ms)
	}
}

// Dense box-drawing up to the cap, to catch any other part of Classify that
// is worse than linear in the length it accepts.
func TestCapSizedInputStaysCheap(t *testing.T) {
	const row = "┌─┬─┐ ├─┼─┤ └─┴─┘ │ │ │ ──> A1\n"
	rowLen := utf16Len(row)
	var content strings.Builder
	for size := 0; size+rowLen <= InspectionCapChars; size += rowLen {
		content.WriteString(row)
	}

	start := time.Now()
	Classify(content.String())
	ms := time.Since(start).Milliseconds()
	t.Logf("cap-sized classify took %d ms", ms)
	if ms >= 250 {
		t.Errorf("classify took %d ms", ms)
	}
}

func TestEmptyContentRejected(t *testing.T) {
	if LooksLikeDiagram("") {
		t.Error("empty content is a diagram")
	}
	if LooksLikeDiagram("\n\n") {
		t.Error("two empty lines are a diagram")
	}
}
