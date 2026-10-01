package export

import (
	"strings"
	"testing"

	"github.com/kvit-s/kvit-notes/editor"
)

// textCases render one Markdown body as plain text, one row per input, each
// named after what it checks.
var textCases = []struct {
	name string
	md   string
	want []string
	not  []string
}{
	{"CharacterDiagramExports", "```diagram\n┌────┐\n│ <a>│\n└────┘\n```", []string{"┌────┐", "│ <a>│"}, nil},
	{"MermaidScriptOnlyWithMermaid", "```mermaid\nflowchart LR\nA-->B\n```", []string{"flowchart LR", "A-->B"}, nil},
	{"PlainTextStructuralPrefixes/prefixes", "# Title\n\n- item\n\n> quote", []string{"# Title", "- item", "> quote"}, nil},
	{"PlainTextStructuralPrefixes/markers", "**bold** word", []string{"bold word"}, []string{"**"}},
	{"KanbanFenceExportsAsABoardNotItsMarkdown",
		"```kanban\n## To do\n- [ ] Ship it #release 📅 2026-07-15\n  Needs a changelog\n- [x] Draft the notes\n## Done\n```",
		[]string{"To do (2)", "Done (0)", "[ ] Ship it", "[x] Draft the notes", "#release", "(due 2026-07-15)", "Needs a changelog"},
		[]string{"## To do", "- [ ] Ship it"}},
	{"QueryFenceExportsItsAnswerAsText/no vault", "```query\nfrom: Projects/\n```", []string{"from: Projects/"}, nil},
	{"TocFenceExportsTheDocumentsHeadings", "# One\n\n```toc\nstale\n```\n\n## Under one\n\n# Two",
		[]string{"One\n  Under one\nTwo"}, []string{"stale"}},
	{"TableExportsAsAnAlignedTextTable", "| Name | Owner |\n| --- | --- |\n| Alpha | Dana |\n| B | R |",
		[]string{"Name  | Owner", "------+------", "Alpha | Dana", "B     | R"}, []string{"| --- |"}},
	{"CalloutAndMediaCarryWhatTheyAreInText/callout", "> [!warning] Careful\n> Mind the gap",
		[]string{"[WARNING] Careful", "  Mind the gap"}, nil},
	{"CalloutAndMediaCarryWhatTheyAreInText/image", "![A diagram](pic.png \"the caption\")",
		[]string{"[image: A diagram] pic.png", "the caption"}, nil},
	{"CalloutAndMediaCarryWhatTheyAreInText/embed", "![](https://example.com/page)",
		[]string{"[embed] https://example.com/page"}, nil},
	{"MermaidTextIsLabelledSource", "```mermaid\nflowchart LR\nA-->B\n```", []string{"[mermaid diagram]", "flowchart LR"}, nil},
	{"DisplayMathKeepsItsTeXInText", "$$\na_1 * b^2\n$$", []string{"a_1 * b^2"}, nil},
	{"TodoMetadataSurvivesTheText", "- [ ] Ship it 📅 2026-07-15 ⏫", []string{"[ ] Ship it", "2026-07-15", "⏫"}, nil},
	{"NestedNumberedListsRestartTheirNumbering", "1. one\n   1. sub one\n   2. sub two\n2. two",
		[]string{"1. one", "  1. sub one", "  2. sub two", "2. two"}, nil},
}

func TestTextExport(t *testing.T) {
	for _, c := range textCases {
		t.Run(c.name, func(t *testing.T) {
			text := TextFromMarkdown(c.md, Options{})
			for _, w := range c.want {
				if !strings.Contains(text, w) {
					t.Errorf("missing %q in\n%s", w, text)
				}
			}
			for _, n := range c.not {
				if strings.Contains(text, n) {
					t.Errorf("unexpected %q in\n%s", n, text)
				}
			}
		})
	}
}

// The whole text of a small note, blank line after every block, as the
// exporter writes it.
func TestTextShape(t *testing.T) {
	got := TextFromMarkdown("# Title\n\nSome *text*.\n\n1. a\n2. b\n  - c\n3. d\n\n> q\n>\n> r\n\n---", Options{})
	want := "# Title\n\nSome text.\n\n1. a\n\n2. b\n\n  - c\n\n3. d\n\n> q\n> \n> r\n\n---\n"
	if got != want {
		t.Errorf("text:\n%q\nwant\n%q", got, want)
	}
}

// A note's Markdown export is the note as the editor saves it, and
// exporting leaves the editor's blocks as they were.
func TestNoteMarkdownIsTheSavedNote(t *testing.T) {
	blocks := []editor.Block{editor.NewBlock(editor.Heading1, "Report"), editor.NewBlock(editor.Paragraph, "The note's own text.")}
	before := append([]editor.Block(nil), blocks...)
	md, err := Note(blocks, "Report", FormatMarkdown, Options{})
	if err != nil || string(md) != editor.Serialize(blocks) || !strings.HasPrefix(string(md), "# Report") {
		t.Errorf("markdown: %v %q", err, md)
	}
	if _, err := Note(blocks, "Report", FormatHTML, Options{}); err != nil {
		t.Error(err)
	}
	if _, err := Note(blocks, "Report", FormatPDF, Options{}); err != ErrPDF {
		t.Errorf("PDF: %v", err)
	}
	for i := range blocks {
		if blocks[i] != before[i] {
			t.Errorf("block %d changed", i)
		}
	}
}

// A selection's Markdown is in note order, list items kept tight, and a
// numbered item keeps its number in the note.
func TestSelectionMarkdown(t *testing.T) {
	blocks := editor.ParseMarkdown("Intro\n\n1. one\n2. two\n3. three\n\nOutro")
	got := MarkdownFromSelection(blocks, []int{4, 2, 3, 2, 99})
	if want := "2. two\n3. three\n\nOutro"; got != want {
		t.Errorf("selection: %q, want %q", got, want)
	}
}
