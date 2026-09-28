package editor

import (
	"slices"
	"strings"
	"testing"
	"time"

	kvitui "github.com/kvit-s/kvit-ui"
)

func spansOf(s string) []span { return parseInline([]rune(s)) }

func render(s string, caret int, focused bool) string {
	src := []rune(s)
	var reveal func(span) bool
	if focused {
		reveal = func(sp span) bool { return revealed(sp, caret, 0, 0) }
	}
	return string(project(src, parseInline(src), reveal).Disp)
}

func TestInlineSpans(t *testing.T) {
	cases := []struct {
		src   string
		kinds []spanKind
	}{
		{"a **b** c", []spanKind{sBold}},
		{"*i*", []spanKind{sItalic}},
		{"***bi***", []spanKind{sBoldItalic}},
		{"**bold***italic*", []spanKind{sBold, sItalic}},
		{"*a **b** c*", []spanKind{sItalic, sBold}},
		{"`co*de*`", []spanKind{sCode}},
		{"[t **x**](http://u)", []spanKind{sLink, sBold}},
		{"[[Note]]", []spanKind{sWiki}},
		{"snake_case_name", nil},
		{"~~s~~ ==h== ++u++", []spanKind{sStrike, sHighlight, sUnderline}},
		{"****", []spanKind{sBold}},
		{"a ** b", nil},
		{"2 * 3 * 4", nil},
	}
	for _, c := range cases {
		got := spansOf(c.src)
		var kinds []spanKind
		for _, sp := range got {
			kinds = append(kinds, sp.Kind)
		}
		if len(kinds) != len(c.kinds) {
			t.Errorf("%q: kinds %v, want %v", c.src, kinds, c.kinds)
			continue
		}
		for i := range kinds {
			if kinds[i] != c.kinds[i] {
				t.Errorf("%q: kinds %v, want %v", c.src, kinds, c.kinds)
			}
		}
	}
}

// features.md 2.2.3 and 2.2.7
func TestRevealFollowsCaret(t *testing.T) {
	s := "This is **important** *information* here"
	if got := render(s, 0, false); got != "This is important information here" {
		t.Errorf("unfocused: %q", got)
	}
	if got := render(s, 0, true); got != "This is important information here" {
		t.Errorf("caret at start: %q", got)
	}
	if got := render(s, 12, true); got != "This is **important** information here" {
		t.Errorf("caret in bold: %q", got)
	}
	if got := render(s, 25, true); got != "This is important *information* here" {
		t.Errorf("caret in italic: %q", got)
	}
	// touching the edge reveals
	if got := render(s, 8, true); got != "This is **important** information here" {
		t.Errorf("caret at bold's opening edge: %q", got)
	}
	if got := render("***bi***", 4, true); got != "***bi***" {
		t.Errorf("bold italic: %q", got)
	}
	adj := "**bold***italic*"
	if got := render(adj, 3, true); got != "**bold**italic" {
		t.Errorf("adjacent, caret in bold: %q", got)
	}
	if got := render("see [the docs](http://x) now", 7, true); got != "see [the docs](http://x) now" {
		t.Errorf("link revealed: %q", got)
	}
	if got := render("see [the docs](http://x) now", 0, true); got != "see the docs now" {
		t.Errorf("link rendered: %q", got)
	}
}

func TestClickMapsInsideSpan(t *testing.T) {
	src := []rune("ab **cd** e")
	p := project(src, parseInline(src), nil) // "ab cd e"
	// between "ab " and "cd": inside the bold, after its hidden "**"
	if got := p.forClick(3); got != 5 {
		t.Errorf("click before c: %d, want 5", got)
	}
	// between "cd" and " e": inside the bold, before its hidden "**"
	if got := p.forClick(5); got != 7 {
		t.Errorf("click after d: %d, want 7", got)
	}
}

func TestMarkdownRoundTrip(t *testing.T) {
	src := strings.Join([]string{
		"# Title",
		"",
		"A paragraph with **bold**.",
		"Second line.",
		"",
		"- one",
		"  - nested",
		"- [ ] task",
		"- [x] done",
		"1. first",
		"2. second",
		"",
		"> quote",
		"> more",
		"",
		"```go",
		"func main() {",
		"}",
		"```",
		"",
		"---",
		"",
		"| a | b |",
		"| - | - |",
		"",
	}, "\n")
	blocks := ParseMarkdown(src)
	kinds := []Kind{Heading1, Paragraph, Bullet, Bullet, Todo, Todo, Numbered, Numbered, Quote, Code, Divider, Table}
	if len(blocks) != len(kinds) {
		t.Fatalf("got %d blocks: %+v", len(blocks), blocks)
	}
	for i, k := range kinds {
		if blocks[i].Kind != k {
			t.Errorf("block %d: %v, want %v", i, blocks[i].Kind, k)
		}
	}
	if blocks[3].Indent != 1 || !blocks[5].Checked || blocks[9].Lang != "go" {
		t.Errorf("attributes lost: %+v", blocks)
	}
	if got := Serialize(blocks); got != src {
		t.Errorf("round trip changed the note:\n%s\n---\n%s", got, src)
	}
}

func newTestDoc(md string) *Doc {
	d := NewDoc(ParseMarkdown(md))
	clock := time.Unix(0, 0)
	d.Now = func() time.Time { clock = clock.Add(time.Second); return clock }
	return d
}

func TestEnterAndBackspace(t *testing.T) {
	d := newTestDoc("hello world\n\n- item")
	d.SetCaret(d.Blocks[0].ID, 5)
	d.Enter()
	if got := texts(d); got != "Paragraph:hello | Paragraph: world | Bulleted list:item" {
		t.Fatalf("split: %s", got)
	}
	d.Backspace()
	if got := texts(d); got != "Paragraph:hello world | Bulleted list:item" {
		t.Fatalf("merge: %s", got)
	}
	if d.Caret.Off != 5 {
		t.Fatalf("caret after merge: %d", d.Caret.Off)
	}
	// Enter at the end of a list item continues the list
	d.SetCaret(d.Blocks[1].ID, 4)
	d.Enter()
	if d.Blocks[2].Kind != Bullet {
		t.Fatalf("list not continued: %s", texts(d))
	}
	// Enter on the empty item leaves the list
	d.Enter()
	if d.Blocks[2].Kind != Paragraph || len(d.Blocks) != 3 {
		t.Fatalf("empty item did not exit: %s", texts(d))
	}
	// Backspace at the start of a nested list item: outdent, then paragraph
	d2 := newTestDoc("- a\n  - b")
	d2.SetCaret(d2.Blocks[1].ID, 0)
	d2.Backspace()
	if d2.Blocks[1].Indent != 0 || d2.Blocks[1].Kind != Bullet {
		t.Fatalf("outdent: %+v", d2.Blocks[1])
	}
	d2.Backspace()
	if d2.Blocks[1].Kind != Paragraph {
		t.Fatalf("to paragraph: %+v", d2.Blocks[1])
	}
	d2.Backspace()
	if got := texts(d2); got != "Bulleted list:ab" {
		t.Fatalf("merge into list item: %s", got)
	}
}

func TestUndoMergesTyping(t *testing.T) {
	d := NewDoc(ParseMarkdown("x"))
	now := time.Unix(0, 0)
	d.Now = func() time.Time { return now }
	d.SetCaret(d.Blocks[0].ID, 1)
	for _, c := range "abc" {
		now = now.Add(100 * time.Millisecond)
		d.InsertText(string(c))
	}
	now = now.Add(2 * time.Second) // a pause starts a new step
	d.InsertText("d")
	if len(d.undo) != 2 {
		t.Fatalf("undo steps: %d", len(d.undo))
	}
	d.Undo()
	if d.Blocks[0].Text != "xabc" {
		t.Fatalf("after one undo: %q", d.Blocks[0].Text)
	}
	d.Undo()
	if d.Blocks[0].Text != "x" {
		t.Fatalf("after two undos: %q", d.Blocks[0].Text)
	}
	d.Redo()
	d.Redo()
	if d.Blocks[0].Text != "xabcd" {
		t.Fatalf("after redo: %q", d.Blocks[0].Text)
	}
}

func TestTypedConversionUndoesToLiteral(t *testing.T) {
	d := newTestDoc("")
	d.SetCaret(d.Blocks[0].ID, 0)
	d.InsertText("#")
	d.InsertText(" ")
	if !d.ApplyTypedConversion() || d.Blocks[0].Kind != Heading1 {
		t.Fatalf("no conversion: %s", texts(d))
	}
	d.Undo()
	if d.Blocks[0].Kind != Paragraph || d.Blocks[0].Text != "# " {
		t.Fatalf("undo did not restore the literal: %s", texts(d))
	}
}

func TestCrossBlockDeleteAndCopy(t *testing.T) {
	d := newTestDoc("first para\n\n- middle\n\nlast para")
	d.Anchor = Pos{d.Blocks[0].ID, 6}
	d.Caret = Pos{d.Blocks[2].ID, 4}
	d.Focused = true
	if got := d.SelectedMarkdown(); got != "para\n\n- middle\n\nlast" {
		t.Errorf("copy: %q", got)
	}
	d.DeleteSelection()
	if got := texts(d); got != "Paragraph:first  para" {
		t.Errorf("delete: %s", got)
	}
	d.Undo()
	if len(d.Blocks) != 3 {
		t.Errorf("undo: %s", texts(d))
	}
}

func TestListNumbers(t *testing.T) {
	bs := ParseMarkdown("1. a\n2. b\n   - x\n3. c\n\npara\n\n1. again")
	var got []int
	for i, b := range bs {
		if b.Kind == Numbered {
			got = append(got, ListNumber(bs, i))
		}
	}
	want := []int{1, 2, 3, 1}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("numbers %v, want %v", got, want)
		}
	}
}

// texts describes the blocks for test messages.
func texts(d *Doc) string {
	var s []string
	for _, b := range d.Blocks {
		s = append(s, b.Kind.String()+":"+b.Text)
	}
	return strings.Join(s, " | ")
}

// Kvit keeps a block's presentation in a "<!--kvit ...-->" tag on its line
// (or a code fence's opening line); it must survive a load and a save, and
// not show as text.
func TestAttributeTagsRoundTrip(t *testing.T) {
	src := "Some text.  <!--kvit align=center-->\n\n" +
		"## A heading  <!--kvit align=right-->\n\n" +
		"---  <!--kvit style=dashed width=50%-->\n\n" +
		"- an item  <!--kvit align=center-->\n\n" +
		"```cpp  <!--kvit align=center-->\nint x = 1;\n```\n\n" +
		"> quoted\n> twice  <!--kvit align=center-->\n"
	blocks := ParseMarkdown(src)
	want := []struct {
		kind  Kind
		text  string
		attrs string
	}{
		{Paragraph, "Some text.", "align=center"},
		{Heading2, "A heading", "align=right"},
		{Divider, "", "style=dashed width=50%"},
		{Bullet, "an item", "align=center"},
		{Code, "int x = 1;", "align=center"},
		{Quote, "quoted\ntwice", "align=center"},
	}
	if len(blocks) != len(want) {
		t.Fatalf("blocks: %s", texts(NewDoc(blocks)))
	}
	for i, w := range want {
		b := blocks[i]
		if b.Kind != w.kind || b.Text != w.text || b.Attrs != w.attrs {
			t.Errorf("block %d: %v %q %q, want %v %q %q", i, b.Kind, b.Text, b.Attrs, w.kind, w.text, w.attrs)
		}
	}
	if got := Serialize(blocks); got != src {
		t.Errorf("round trip changed the note:\n%s\nwant\n%s", got, src)
	}
	if got := canonicalAttrs("width=50% style=dashed width=40%"); got != "style=dashed width=40%" {
		t.Errorf("canonical order: %q", got)
	}
}

// Kvit's callouts: a quote headed [!type], folded with "-", written back as
// Kvit writes it.
func TestCalloutsRoundTrip(t *testing.T) {
	src := "> [!tip] Files are the truth\n> No database, no accounts.\n>\n> A vault is a folder.\n\n> [!warning]- Folded\n> Hidden body\n\n> A plain quote\n>\n> with a gap\n"
	blocks := ParseMarkdown(src)
	if len(blocks) != 3 || blocks[0].Kind != Callout || blocks[0].Lang != "tip" || blocks[0].Title != "Files are the truth" ||
		blocks[0].Text != "No database, no accounts.\n\nA vault is a folder." || !blocks[1].Checked || blocks[2].Kind != Quote {
		t.Fatalf("blocks: %+v", blocks)
	}
	if got := Serialize(blocks); got != src {
		t.Errorf("round trip:\n%s\nwant\n%s", got, src)
	}
}

func TestTablesAreReadAsKvitReadsThem(t *testing.T) {
	src := "| A | B \\| c |  <!--kvit align=center-->\n| :-- | --: |\n| 1 | **two** |\n| 3 |\n"
	blocks := ParseMarkdown(src)
	if len(blocks) != 1 || blocks[0].Kind != Table || blocks[0].Attrs != "align=center" {
		t.Fatalf("blocks: %+v", blocks)
	}
	if got := Serialize(blocks); got != src {
		t.Errorf("a table must be saved as written:\n%q\nwant\n%q", got, src)
	}
	tb, ok := parseTable(blocks[0].Text)
	if !ok || len(tb.header) != 2 || tb.header[1] != "B | c" || tb.align[0] != alignLeft || tb.align[1] != alignRight ||
		len(tb.rows) != 2 || tb.rows[1][1] != "" {
		t.Errorf("table: %+v %v", tb, ok)
	}
	if _, ok := parseTable("| just | pipes |\n| no delimiter |"); ok {
		t.Errorf("a table needs its delimiter row")
	}
}

func TestInlineSupSubMathColor(t *testing.T) {
	kinds := func(src string) []spanKind {
		var out []spanKind
		for _, sp := range parseInline([]rune(src)) {
			out = append(out, sp.Kind)
		}
		return out
	}
	cases := []struct {
		src  string
		want []spanKind
	}{
		{"x^2^ and H~2~O", []spanKind{sSup, sSub}},
		{"either ~5 or ~3", nil},
		{"a ~~gone~~ word", []spanKind{sStrike}},
		{"x^2 + y^2", nil},
		{"area $\\pi r^2$ here", []spanKind{sMath}},
		{"costs $5 and $6", nil},
		{`<span style="color:#e05c5c">red</span>`, []spanKind{sColor}},
		{`<span style='color: blue '>**b**</span>`, []spanKind{sColor, sBold}},
		{`<span style="color:#e05c5c; x">no</span>`, nil},
		{`<span style="color:red"></span>`, nil},
	}
	for _, c := range cases {
		if got := kinds(c.src); !slices.Equal(got, c.want) {
			t.Errorf("%q: spans %v, want %v", c.src, got, c.want)
		}
	}
	d := NewDoc([]Block{NewBlock(Paragraph, "make this red")})
	id := d.Blocks[0].ID
	d.Anchor, d.Caret = Pos{id, 10}, Pos{id, 13}
	d.SetColor("#e05c5c")
	if got := d.Blocks[0].Text; got != `make this <span style="color:#e05c5c">red</span>` {
		t.Fatalf("coloured: %q", got)
	}
	if d.CurrentColor() != "#e05c5c" {
		t.Fatalf("current colour %q", d.CurrentColor())
	}
	d.SetColor("#4a90d9")
	if got := d.Blocks[0].Text; got != `make this <span style="color:#4a90d9">red</span>` {
		t.Fatalf("recoloured: %q", got)
	}
	d.SetColor("")
	if got := d.Blocks[0].Text; got != "make this red" {
		t.Fatalf("colour removed: %q", got)
	}
	if a, c := d.Anchor.Off, d.Caret.Off; a != 10 || c != 13 {
		t.Fatalf("selection after removal %d..%d", a, c)
	}
}

func TestStatisticsCountWhatTheReaderSees(t *testing.T) {
	d := NewDoc(ParseMarkdown("# A **bold** title\n\n---\n\n```\nx := 1\n```\n\nTwo words\n"))
	s := d.Stats()
	if s.Words != 8 || s.Paragraphs != 3 || s.Blocks != 4 || s.ReadingMinutes != 1 {
		t.Errorf("stats: %+v", s)
	}
	if s.Chars != len("A bold title")+len("x := 1")+len("Two words") {
		t.Errorf("characters %d", s.Chars)
	}
	d.Anchor, d.Caret = Pos{d.Blocks[0].ID, 2}, Pos{d.Blocks[0].ID, 10}
	sel, ok := d.SelectionStats()
	if !ok || sel.Words != 1 || sel.Chars != 4 {
		t.Errorf("the selection \"**bold**\" is one word of four letters: %+v", sel)
	}
	if readingMinutes(299) != 1 || readingMinutes(301) != 2 || readingMinutes(0) != 0 {
		t.Errorf("reading minutes")
	}
}

// The tests from here to TestAsCodeOptsOut are the character-diagram tests
// of the Qt app's tests/test_documentserializer.cpp
// (testIngestTagsCharacterDiagram to testDiagramFenceRoundTrips), with the
// same inputs, then the Doc's side of tests/tst_integration.qml's
// test_69h4 and test_69h5.

// diagramBody is a compact two-box character diagram the classifier
// accepts: two framed regions joined by a connector.
const diagramBody = "┌─────────┐\n" +
	"│  START  │\n" +
	"└────┬────┘\n" +
	"     │\n" +
	"     ▼\n" +
	"┌─────────┐\n" +
	"│   END   │\n" +
	"└─────────┘"

// An untagged fence holding a character diagram is tagged `diagram` when it
// is read; its body is left byte for byte.
func TestIngestTagsCharacterDiagram(t *testing.T) {
	blocks := ParseMarkdown("```\n" + diagramBody + "\n```\n")
	if len(blocks) != 1 || blocks[0].Kind != Code {
		t.Fatalf("blocks: %+v", blocks)
	}
	if blocks[0].Lang != "diagram" || blocks[0].Text != diagramBody {
		t.Errorf("language %q, text changed %v", blocks[0].Lang, blocks[0].Text != diagramBody)
	}
	// `text`, `plaintext` and `ascii` wrappers are as eligible.
	for _, lang := range []string{"text", "plaintext", "ascii"} {
		b := ParseMarkdown("```" + lang + "\n" + diagramBody + "\n```\n")
		if len(b) != 1 || b[0].Lang != "diagram" {
			t.Errorf("%s: %+v", lang, b)
		}
	}
}

// Reading a tagged fence again leaves it as it is.
func TestIngestTaggingIsIdempotent(t *testing.T) {
	once := ParseMarkdown("```\n" + diagramBody + "\n```\n")
	if once[0].Lang != "diagram" {
		t.Fatalf("language %q", once[0].Lang)
	}
	twice := ParseMarkdown("```diagram\n" + diagramBody + "\n```\n")
	if len(twice) != 1 || twice[0].Lang != "diagram" || twice[0].Text != diagramBody {
		t.Errorf("second read: %+v", twice)
	}
}

// A `plain` fence is the way to keep a body that looks like a diagram as
// code, and any other language is kept too.
func TestIngestLeavesExplicitLanguages(t *testing.T) {
	if b := ParseMarkdown("```plain\n" + diagramBody + "\n```\n"); b[0].Lang != "plain" {
		t.Errorf("plain became %q", b[0].Lang)
	}
	if b := ParseMarkdown("```python\n" + diagramBody + "\n```\n"); b[0].Lang != "python" {
		t.Errorf("python became %q", b[0].Lang)
	}
}

// An untagged fence shaped like a `tree` listing stays code: it has no
// framed regions.
func TestIngestLeavesOrdinaryCode(t *testing.T) {
	tree := ParseMarkdown("```\nproject/\n├── src/\n│   └── main.cpp\n└── README.md\n```\n")
	if len(tree) != 1 || tree[0].Lang != "" {
		t.Errorf("tree listing: %+v", tree)
	}
}

// A diagram fence with a ragged edge, the first box's top-right corner two
// columns short of its walls, is straightened when it is read, and a
// `plain` fence is not.
func TestIngestStraightensDiagramFences(t *testing.T) {
	flawed := "┌─────┐\n" +
		"│  A     │\n" +
		"└────┬───┘\n" +
		"     │\n" +
		"     ▼\n" +
		"┌────────┐\n" +
		"│  B     │\n" +
		"└────────┘"
	tagged := ParseMarkdown("```diagram\n" + flawed + "\n```\n")
	if len(tagged) != 1 || tagged[0].Text == flawed {
		t.Fatalf("not straightened: %+v", tagged)
	}
	first, _, _ := strings.Cut(tagged[0].Text, "\n")
	if col := slices.Index([]rune(first), '┐'); col != 9 {
		t.Errorf("the top-right corner is at column %d, want 9:\n%s", col, tagged[0].Text)
	}
	again := ParseMarkdown("```diagram\n" + tagged[0].Text + "\n```\n")
	if again[0].Text != tagged[0].Text {
		t.Errorf("a second read changed the diagram:\n%s", again[0].Text)
	}
	if plain := ParseMarkdown("```plain\n" + flawed + "\n```\n"); plain[0].Text != flawed {
		t.Errorf("a plain fence was straightened:\n%s", plain[0].Text)
	}
}

// The tagged fence is written back as a `diagram` fence, and reading that
// again writes the same note.
func TestDiagramFenceRoundTrips(t *testing.T) {
	out := Serialize(ParseMarkdown("```\n" + diagramBody + "\n```\n"))
	if !strings.Contains(out, "```diagram") {
		t.Fatalf("written as:\n%s", out)
	}
	if again := Serialize(ParseMarkdown(out)); again != out {
		t.Errorf("a second round trip changed the note:\n%s", again)
	}
}

// crookedDrawing and straightDrawing are test_69h4's diagram before and
// after straightening: a short top edge, a tee one column off its
// connector, and a ragged right wall.
const crookedDrawing = "┌──────────┐\n" +
	"│ Editor     │\n" +
	"│ (QML)      │\n" +
	"└────┬───────┘\n" +
	"      │\n" +
	"┌─────▼──────┐        ┌───────────┐\n" +
	"│ Serializer │ ─────► │ Markdown    │\n" +
	"│ blocks     │        │ file       │\n" +
	"└────────────┘        └───────────┘"

const straightDrawing = "┌────────────┐\n" +
	"│ Editor     │\n" +
	"│ (QML)      │\n" +
	"└─────┬──────┘\n" +
	"      │\n" +
	"┌─────▼──────┐        ┌───────────┐\n" +
	"│ Serializer │ ─────► │ Markdown  │\n" +
	"│ blocks     │        │ file      │\n" +
	"└────────────┘        └───────────┘"

// A crooked drawing pasted into a code block is straightened and the block
// tagged `diagram`, as opening a note holding it would do, and one undo
// takes back the paste and the straightening together. Ordinary code is
// pasted as it is.
func TestPasteIntoCodeBlockStraightensDiagram(t *testing.T) {
	d := newTestDoc("```\n```")
	d.SetCaret(d.Blocks[0].ID, 0)
	d.Paste(crookedDrawing, false)
	if d.Blocks[0].Text != straightDrawing || d.Blocks[0].Lang != "diagram" {
		t.Fatalf("language %q, text:\n%s", d.Blocks[0].Lang, d.Blocks[0].Text)
	}
	if d.Caret.Off != len([]rune(straightDrawing)) {
		t.Errorf("caret at %d, want the end of the paste", d.Caret.Off)
	}
	d.Undo()
	if d.Blocks[0].Text != "" || d.Blocks[0].Lang != "" {
		t.Errorf("after one undo: language %q, text %q", d.Blocks[0].Lang, d.Blocks[0].Text)
	}

	program := "def f(x):\n    return x + 1\n\nprint(f(2))"
	code := newTestDoc("```\n```")
	code.SetCaret(code.Blocks[0].ID, 0)
	code.Paste(program, false)
	if code.Blocks[0].Text != program || code.Blocks[0].Lang != "" {
		t.Errorf("ordinary code: language %q, text %q", code.Blocks[0].Lang, code.Blocks[0].Text)
	}

	// Typing the same text is not a paste and changes nothing on its way in.
	typed := newTestDoc("```\n```")
	typed.SetCaret(typed.Blocks[0].ID, 0)
	typed.InsertText(crookedDrawing)
	if typed.Blocks[0].Text != crookedDrawing || typed.Blocks[0].Lang != "" {
		t.Errorf("typing: language %q, text:\n%s", typed.Blocks[0].Lang, typed.Blocks[0].Text)
	}
}

// Markdown pasted into a paragraph goes through the same step as a note
// being opened: an untagged diagram fence arrives tagged and straightened,
// as one undo step. Lines opening a fence become blocks even with no blank
// line among them (tst_integration.qml's test_zx0i).
func TestPastedFenceBecomesItsBlock(t *testing.T) {
	d := newTestDoc("")
	d.SetCaret(d.Blocks[0].ID, 0)
	d.Paste("```\n"+crookedDrawing+"\n```", false)
	if len(d.Blocks) != 1 || d.Blocks[0].Kind != Code || d.Blocks[0].Lang != "diagram" ||
		d.Blocks[0].Text != straightDrawing {
		t.Fatalf("pasted: %s\n%s", texts(d), d.Blocks[0].Text)
	}
	d.Undo()
	if len(d.Blocks) != 1 || d.Blocks[0].Kind != Paragraph || d.Blocks[0].Text != "" {
		t.Errorf("after one undo: %s", texts(d))
	}

	fence := "```mermaid\nflowchart LR\n" +
		"    A([Start]) --> B{Vault set?}\n" +
		"    B -- yes --> C[Open collection]\n```"
	m := newTestDoc("")
	m.SetCaret(m.Blocks[0].ID, 0)
	m.Paste(fence, false)
	if len(m.Blocks) != 1 || m.Blocks[0].Kind != Code || m.Blocks[0].Lang != "mermaid" ||
		!strings.HasPrefix(m.Blocks[0].Text, "flowchart LR") || strings.Contains(m.Blocks[0].Text, "```") {
		t.Errorf("the paste should land as the mermaid fence it was: %s", texts(m))
	}

	// Inside a code block the same paste is text, markers included.
	c := newTestDoc("```\n```")
	c.SetCaret(c.Blocks[0].ID, 0)
	c.Paste(fence, false)
	if len(c.Blocks) != 1 || !strings.Contains(c.Blocks[0].Text, "```mermaid") {
		t.Errorf("a fence pasted into a listing keeps its markers: %s", texts(c))
	}

	// Text after the caret follows a pasted fence as its own paragraph
	// rather than running on inside the code.
	tail := newTestDoc("before after")
	tail.SetCaret(tail.Blocks[0].ID, 7)
	tail.Paste("```\nx := 1\n```", false)
	if got := texts(tail); got != "Paragraph:before  | Code:x := 1 | Paragraph:after" {
		t.Errorf("paste in the middle of a paragraph: %s", got)
	}

	// A plain-text paste keeps its lines as text.
	p := newTestDoc("")
	p.SetCaret(p.Blocks[0].ID, 0)
	p.Paste("```\nx := 1\n```", true)
	if len(p.Blocks) != 1 || p.Blocks[0].Kind != Paragraph {
		t.Errorf("a plain paste became blocks: %s", texts(p))
	}
}

// Choosing "Text diagram" for a code block that holds a crooked drawing
// straightens it; choosing a programming language leaves the text alone
// (test_69h5).
func TestDeclaringATextDiagramStraightensIt(t *testing.T) {
	crooked := "┌──────────┐\n│ ab    │\n│ cd    │\n└───────┘"
	straight := "┌───────┐\n│ ab    │\n│ cd    │\n└───────┘"
	d := newTestDoc("```python\n" + crooked + "\n```")
	id := d.Blocks[0].ID
	d.SetCodeLanguage(id, "diagram")
	if d.Blocks[0].Lang != "diagram" || d.Blocks[0].Text != straight {
		t.Fatalf("language %q, text:\n%s", d.Blocks[0].Lang, d.Blocks[0].Text)
	}
	d.Undo()
	if d.Blocks[0].Lang != "python" || d.Blocks[0].Text != crooked {
		t.Errorf("one undo should bring back the language and the text: %q\n%s", d.Blocks[0].Lang, d.Blocks[0].Text)
	}

	d.Blocks[0].Lang = ""
	d.SetCodeLanguage(id, "python")
	if d.Blocks[0].Lang != "python" || d.Blocks[0].Text != crooked {
		t.Errorf("choosing a code language rewrote the body: %q\n%s", d.Blocks[0].Lang, d.Blocks[0].Text)
	}
}

// "Plain code" and a rendered diagram's "As code" both tag a block `plain`,
// and a `plain` block is never tagged as a diagram again: not when the note
// is opened, and not when text is pasted into it.
func TestAsCodeOptsOut(t *testing.T) {
	d := newTestDoc("```mermaid\nflowchart LR\n  A --> B\n```")
	id := d.Blocks[0].ID
	d.AsCode(id)
	if d.Blocks[0].Lang != "plain" || d.Blocks[0].Kind != Code {
		t.Fatalf("As code: %+v", d.Blocks[0])
	}
	d.Undo()
	if d.Blocks[0].Lang != "mermaid" {
		t.Errorf("one undo should bring back the diagram: %q", d.Blocks[0].Lang)
	}

	p := newTestDoc("```\n" + diagramBody + "\n```")
	if p.Blocks[0].Lang != "diagram" {
		t.Fatalf("not tagged on open: %q", p.Blocks[0].Lang)
	}
	p.SetCodeLanguage(p.Blocks[0].ID, "plain")
	if p.Blocks[0].Lang != "plain" {
		t.Fatalf("Plain code: %q", p.Blocks[0].Lang)
	}
	// Choosing plain text instead tags it again at once, as in the Qt app.
	p.SetCodeLanguage(p.Blocks[0].ID, "")
	if p.Blocks[0].Lang != "diagram" {
		t.Errorf("plain text on a diagram: %q", p.Blocks[0].Lang)
	}
	p.SetCodeLanguage(p.Blocks[0].ID, "plain")

	reopened := ParseMarkdown(Serialize(p.Blocks))
	if reopened[0].Lang != "plain" {
		t.Errorf("reopened as %q", reopened[0].Lang)
	}
	p.SetCaret(p.Blocks[0].ID, 0)
	p.Paste(crookedDrawing+"\n", false)
	if p.Blocks[0].Lang != "plain" || !strings.HasPrefix(p.Blocks[0].Text, crookedDrawing) {
		t.Errorf("a paste into a plain block retagged or straightened it: %q\n%s", p.Blocks[0].Lang, p.Blocks[0].Text)
	}
}

// An aliased wiki link shows only its alias away from the caret, the whole
// inside with the caret in it (Qt's BlockEditorEngine over
// WikiLinkScanner::matchAt); following still resolves the target.
func TestWikiAliasShowsAlias(t *testing.T) {
	const src = "See [[Plan|the plan]] now"
	sps := spansOf(src)
	if len(sps) != 1 || sps[0].Kind != sWiki {
		t.Fatalf("spans: %+v", sps)
	}
	if got := string([]rune(src)[sps[0].CStart:sps[0].CEnd]); got != "the plan" {
		t.Errorf("content %q, want %q", got, "the plan")
	}
	if got := PlainText(src); got != "See the plan now" {
		t.Errorf("drawn %q", got)
	}
	if got := render(src, 0, false); got != "See the plan now" {
		t.Errorf("unfocused %q", got)
	}
	if got := render(src, 7, true); got != src {
		t.Errorf("caret in the link should reveal it: %q", got)
	}
	for _, bad := range []string{"[[a|b|c]]", "[[|alias]]", "[[  ]]", "[[]]", "[[a|]]", "[[a#]]", "[[a#b#c]]"} {
		if len(spansOf(bad)) != 0 {
			t.Errorf("%q should be no link: %+v", bad, spansOf(bad))
		}
		if got := PlainText(bad); got != bad {
			t.Errorf("%q draws as %q", bad, got)
		}
	}
	ref, _, ok := linkAt("Go to [[Plan#Goals|goals]] now", 9)
	if !ok || !ref.Wiki || ref.Target != "Plan#Goals" {
		t.Errorf("follow resolves %+v %v", ref, ok)
	}
	ref, _, ok = linkAt("Go to [[Plan#Goals|goals]] now", 21)
	if !ok || !ref.Wiki || ref.Target != "Plan#Goals" {
		t.Errorf("follow from the alias resolves %+v %v", ref, ok)
	}
}

// RemoveLinkAt takes a Markdown link's formatting away keeping its text, as
// the link menu's Remove link does; bare addresses and wiki links stay.
func TestRemoveLinkAt(t *testing.T) {
	s, e := openEditor(t, "see [the docs](https://x/a) now and [[Plan|alias]] end")
	text := func() string {
		var out string
		s.Do(func() { out = e.Doc.Blocks[0].Text })
		return out
	}
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	s.Do(func() {
		if !e.RemoveLinkAt(Pos{id, 8}) {
			t.Fatal("should remove the Markdown link")
		}
	})
	if got := text(); got != "see the docs now and [[Plan|alias]] end" {
		t.Errorf("removed: %q", got)
	}
	s.Do(func() {
		d := e.Doc
		if d.Caret.Off != 12 {
			t.Errorf("caret after the kept text: %d", d.Caret.Off)
		}
		d.Undo()
		if d.Blocks[0].Text != "see [the docs](https://x/a) now and [[Plan|alias]] end" {
			t.Errorf("one undo restores: %q", d.Blocks[0].Text)
		}
		// On the wiki link there is nothing to remove.
		if e.RemoveLinkAt(Pos{id, 30}) {
			t.Error("a wiki link should not lose its brackets")
		}
	})
}

// CaretLineColumn is the 1-based line and column in the block's display
// text, for the status line.
func TestCaretLineColumn(t *testing.T) {
	s, e := openEditor(t, "first **bold** line\nsecond line")
	s.Do(func() {
		// "first bold line\nsecond line": offset of the 'y' in "bold".
		e.FocusBlock(0, 9)
		if ln, col := e.CaretLineColumn(); ln != 1 || col != 10 {
			t.Errorf("line %d col %d, want 1/10", ln, col)
		}
		e.FocusBlock(0, 21)
		if ln, col := e.CaretLineColumn(); ln != 2 || col != 2 {
			t.Errorf("line %d col %d, want 2/2", ln, col)
		}
	})
}

// The block menu carries Export, which hands the blocks to the app.
func TestBlockMenuHasExport(t *testing.T) {
	lines, _ := BlockMenuCommands()
	if !slices.Contains(lines, "Export…") {
		t.Fatalf("block menu lines: %q", lines)
	}
	s, e := openEditor(t, "one\n\ntwo\n")
	var got []int64
	s.Do(func() {
		e.OnExport = func(ids []int64) { got = append([]int64(nil), ids...) }
		items := e.menuItems(blockCommands, []int64{e.Doc.Blocks[1].ID})
		for _, it := range items {
			name, _, _ := kvitui.AccessText(it.Text)
			if name == "Export…" {
				it.OnSelect()
			}
		}
	})
	if len(got) != 1 {
		t.Fatalf("export got %v", got)
	}
	s.Do(func() {
		if got[0] != e.Doc.Blocks[1].ID {
			t.Errorf("export got %v", got)
		}
	})
}

// The link menu opens a link, edits a Markdown one, and removes its
// formatting; a wiki link only opens, and plain text has no link menu.
func TestLinkMenuItems(t *testing.T) {
	s, e := openEditor(t, "see [the docs](https://x/a) and [[Plan|alias]] end")
	names := func(items []kvitui.MenuItem) []string {
		var out []string
		for _, it := range items {
			out = append(out, it.Text)
		}
		return out
	}
	var id int64
	s.Do(func() {
		id = e.Doc.Blocks[0].ID
		e.FollowLink = func(LinkRef) {}
		e.OnLink = func() {}
		if got := names(e.linkMenuItems(Pos{id, 8})); !slices.Equal(got, []string{"Open link", "Edit link…", "Remove link"}) {
			t.Errorf("markdown link: %q", got)
		}
		if got := names(e.linkMenuItems(Pos{id, 32})); !slices.Equal(got, []string{"Open link"}) {
			t.Errorf("wiki link: %q", got)
		}
		if e.linkMenuItems(Pos{id, 0}) != nil {
			t.Error("plain text should fall back to the text menu")
		}
		e.FollowLink, e.OnLink = nil, nil
		if got := names(e.linkMenuItems(Pos{id, 8})); !slices.Equal(got, []string{"Remove link"}) {
			t.Errorf("remove works without hooks: %q", got)
		}
	})
}
