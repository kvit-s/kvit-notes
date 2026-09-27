package editor

import (
	"slices"
	"strings"
	"testing"
	"time"
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
