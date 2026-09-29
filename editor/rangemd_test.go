package editor

import "testing"

// The rangeMarkdown cases of the Qt core's tests/test_documentselection.cpp.

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
	// next piece's reopened wherever the run of markers changes, as Qt's
	// markdownForRange rebuilds a partly covered span.
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
