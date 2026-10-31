package editor

import "testing"

// A selection as Markdown that stands on its own (Doc.RangeMarkdown).

// selectDoc selects from Markdown offset from of block a to offset to of
// block b, the anchor first.
func selectDoc(d *Doc, a, from, b, to int) {
	d.Anchor = Pos{d.Blocks[a].ID, from}
	d.Caret = Pos{d.Blocks[b].ID, to}
	d.Focused = true
}

func selectionDoc() *Doc {
	return NewDoc([]Block{
		NewBlock(Paragraph, "alpha beta"),
		NewBlock(Paragraph, "second block"),
		NewBlock(Paragraph, "third"),
		NewBlock(Divider, ""),
		NewBlock(Paragraph, "fifth block here"),
		NewBlock(Paragraph, "last"),
	})
}

func TestRangeMarkdownFragmentsAndStructure(t *testing.T) {
	d := selectionDoc()
	want := "beta\n\nsecond block\n\nthird\n\n---\n\nfifth"
	selectDoc(d, 0, 6, 4, 5)
	if got := d.RangeMarkdown(); got != want {
		t.Errorf("forward %q", got)
	}
	selectDoc(d, 4, 5, 0, 6)
	if got := d.RangeMarkdown(); got != want {
		t.Errorf("backward %q", got)
	}
	if r := d.SelectionRange(); r != (TextRange{0, 6, 4, 5}) {
		t.Errorf("range %+v", r)
	}
	d.Anchor = d.Caret
	if got := d.RangeMarkdown(); got != "" {
		t.Errorf("no selection %q", got)
	}
	if r := d.SelectionRange(); r != NoRange {
		t.Errorf("no range %+v", r)
	}
}

func TestRangeMarkdownSpansStaySelfContained(t *testing.T) {
	d := selectionDoc()
	d.Blocks = append([]Block{NewBlock(Paragraph, "go **bold word** on")}, d.Blocks...)
	selectDoc(d, 0, 6, 1, 5)
	if got, want := d.RangeMarkdown(), "**old word** on\n\nalpha"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	// Inside one block, a sweep ending in the middle of a span closes it,
	// and one covering a whole span keeps it as it was written.
	selectDoc(d, 0, 0, 0, 8)
	if got, want := d.RangeMarkdown(), "go **bol**"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
	selectDoc(d, 0, 2, 0, 19)
	if got, want := d.RangeMarkdown(), " **bold word** on"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestRangeMarkdownNestedSpansReopenTheirWholeChain(t *testing.T) {
	d := NewDoc([]Block{NewBlock(Paragraph, "a **b *c d* e** f"), NewBlock(Paragraph, "next")})
	// From the "c" inside the italic run to the next block: each piece is
	// written inside every marker around it, all of them closed and the
	// next piece's reopened wherever the run of markers changes.
	selectDoc(d, 0, 7, 1, 4)
	if got, want := d.RangeMarkdown(), "***c d***** e** f\n\nnext"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestRangeMarkdownTightLists(t *testing.T) {
	d := NewDoc([]Block{
		NewBlock(Paragraph, "intro text"),
		NewBlock(Bullet, "one"),
		NewBlock(Numbered, "first"),
		NewBlock(Numbered, "second"),
		NewBlock(Paragraph, "outro"),
	})
	selectDoc(d, 0, 6, 4, 2)
	if got, want := d.RangeMarkdown(), "text\n\n- one\n1. first\n2. second\n\nou"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestPartialCodeBlockCopyIsVerbatim(t *testing.T) {
	code := "a*b_c`d`[e](f)$g$ *h*"
	d := NewDoc([]Block{{ID: NewBlock(Code, "").ID, Kind: Code, Text: code}})
	r := []rune(code)
	for from := 1; from < len(r); from++ {
		selectDoc(d, 0, from, 0, len(r))
		if got := d.RangeMarkdown(); got != string(r[from:]) {
			t.Errorf("from %d: %q", from, got)
		}
	}
	selectDoc(d, 0, 1, 0, 6)
	if got := d.RangeMarkdown(); got != string(r[1:6]) {
		t.Errorf("markers kept: %q", got)
	}
}

func TestPartialCodeBlockCopyAcrossBlocks(t *testing.T) {
	code := "a*b_c`d`[e](f)$g$ *h*"
	d := NewDoc([]Block{{ID: NewBlock(Code, "").ID, Kind: Code, Text: code}, NewBlock(Paragraph, "after **bold** end")})
	selectDoc(d, 0, 2, 1, 5)
	if got, want := d.RangeMarkdown(), code[2:]+"\n\nafter"; got != want {
		t.Errorf("got %q, want %q", got, want)
	}
}

func TestCopyInsideOneBlockIsTheCharactersDrawn(t *testing.T) {
	cmd := `D:\projects\kvit-works\report.cmd --from "%LOCALAPPDATA%\Kvit\Kvit Works\logs" kvit-works`
	d := NewDoc([]Block{
		{ID: NewBlock(Code, "").ID, Kind: Code, Lang: "bat", Text: cmd},
		NewBlock(Heading2, "A **bold** title"),
		NewBlock(Paragraph, "Run `report.cmd` from [the logs](logs/), with $x^2$ *set*."),
	})
	fenced := "```bat\n" + cmd + "\n```"
	copied := func(want string) {
		t.Helper()
		if got, markdown := d.CopiedText(); got != want || markdown {
			t.Errorf("copied %q (Markdown %v), want %q", got, markdown, want)
		}
	}
	// All of the code's text, as a sweep over the drawn block selects it:
	// the copy is the code, and a quote of it keeps the fence.
	selectDoc(d, 0, 0, 0, len([]rune(cmd)))
	copied(cmd)
	if got := d.RangeMarkdown(); got != fenced {
		t.Errorf("quoted %q", got)
	}
	selectDoc(d, 0, 3, 0, 23)
	copied(`projects\kvit-works\`)
	// A heading's whole text comes without its "#" and its markers.
	selectDoc(d, 1, 16, 1, 0)
	copied("A bold title")
	// Inline code, a link and emphasis as their words; a formula, drawn as
	// a picture, as its TeX.
	selectDoc(d, 2, 4, 2, 16)
	copied("report.cmd")
	selectDoc(d, 2, 0, 2, len([]rune(d.Blocks[2].Text)))
	copied("Run report.cmd from the logs, with $x^2$ set.")
	// Across blocks the copy is Markdown with each block's structure.
	selectDoc(d, 0, 0, 1, 16)
	if got, markdown := d.CopiedText(); got != fenced+"\n\n## A **bold** title" || !markdown {
		t.Errorf("across blocks copied %q (Markdown %v)", got, markdown)
	}
	d.Anchor = d.Caret
	copied("")
	// A block selected whole, by its handle, is copied with its fence.
	e := &Editor{Doc: d, blockSel: map[int64]bool{d.Blocks[0].ID: true}}
	if got, markdown := e.CopiedText(); got != fenced || !markdown {
		t.Errorf("selected block copied %q (Markdown %v)", got, markdown)
	}
}
