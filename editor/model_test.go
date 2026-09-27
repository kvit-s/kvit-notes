package editor

import (
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
	kinds := []Kind{Heading1, Paragraph, Bullet, Bullet, Todo, Todo, Numbered, Numbered, Quote, Code, Divider, Raw}
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
