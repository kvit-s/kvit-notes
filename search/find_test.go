package search

import (
	"slices"
	"strings"
	"testing"
	"time"
	"unicode/utf8"
)

// The tests in this file are ported from Kvit's tests/test_documentsearch.cpp;
// each names the test it comes from. The tests that exercise the find
// bar's object (its revision counter, recomputing on model signals, undo,
// block ids surviving moves) have no counterpart, because these are pure
// functions and the caller owns that state.

// spansOf stands in for the editor's parser, enough for the tests'
// notes: **bold** and *italic*, not nested.
func spansOf(md string) []Span {
	rs := []rune(md)
	var out []Span
	for i := 0; i < len(rs); {
		if rs[i] != '*' {
			i++
			continue
		}
		m := 1
		if i+1 < len(rs) && rs[i+1] == '*' {
			m = 2
		}
		marker := strings.Repeat("*", m)
		end := -1
		for j := i + m + 1; j+m <= len(rs); j++ {
			if string(rs[j:j+m]) == marker {
				end = j
				break
			}
		}
		if end < 0 {
			i += m
			continue
		}
		out = append(out, Span{Start: i, ContentStart: i + m, ContentEnd: end, End: end + m})
		i = end + m
	}
	return out
}

func para(md string) Block { return Block{Markdown: md, Spans: spansOf(md)} }

func code(md string) Block { return Block{Markdown: md, Verbatim: true} }

// fixture is TestDocumentSearch::init's note: a formatted paragraph, a plain
// paragraph, a bullet, a divider, a two-line code block and a two-line
// quote.
func fixture() []Block {
	return []Block{
		para("This is **bold** text"),
		para("second Fox block fox FOX"),
		para("fox item"),
		{},
		code("let fox = **not markdown**\nfox()"),
		para("quote line one\nfox two"),
	}
}

func textsOf(blocks []Block) []string {
	out := make([]string, len(blocks))
	for i, b := range blocks {
		out[i] = b.Text()
	}
	return out
}

func find(t *testing.T, blocks []Block, query string, opts Options) []Match {
	t.Helper()
	ms, err := Find(textsOf(blocks), query, opts)
	if err != nil {
		t.Fatalf("%q: %v", query, err)
	}
	return ms
}

func inBlock(ms []Match, block int) []Match {
	var out []Match
	for _, m := range ms {
		if m.Block == block {
			out = append(out, m)
		}
	}
	return out
}

// applyEdits puts ReplaceAll's edits into the blocks, parsing each new
// Markdown as the editor would.
func applyEdits(blocks []Block, edits []Edit) {
	for _, e := range edits {
		b := &blocks[e.Block]
		b.Markdown = e.Markdown
		if !b.Verbatim {
			b.Spans = spansOf(e.Markdown)
		}
	}
}

// testPlainMatchAcrossBlocks: case is ignored by default; the divider has
// nothing to find, and the code block's text counts.
func TestPlainMatchAcrossBlocks(t *testing.T) {
	ms := find(t, fixture(), "fox", Options{})
	if len(ms) != 7 {
		t.Fatalf("%d matches, want 7", len(ms))
	}
	for block, want := range []int{0, 3, 1, 0, 2, 1} {
		if got := len(inBlock(ms, block)); got != want {
			t.Errorf("block %d: %d matches, want %d", block, got, want)
		}
	}
	if m := inBlock(ms, 1)[0]; m.Start != 7 || m.Length != 3 {
		t.Errorf("first match in block 1 at %d+%d, want 7+3", m.Start, m.Length)
	}
}

// testMatchSpansMarkerBoundary, testMarkersAreNotSearchable,
// testCodeBlockSearchesVerbatimContent, testCaseSensitiveOption.
func TestMatchesAreInTheTextTheReaderSees(t *testing.T) {
	cases := []struct {
		name    string
		query   string
		opts    Options
		want    int   // matches in the note
		block   int   // the block of the first match
		start   int   // and where it starts
		inBlock []int // matches per block, when checked
	}{
		// Block 0 reads "This is bold text": the query exists only there,
		// never in the Markdown.
		{name: "across a marker", query: "is bold", want: 1, block: 0, start: 5},
		// Block 0's asterisks are markers; the code block's are text.
		{name: "markers", query: "**", want: 2, block: 4, start: 10, inBlock: []int{0, 0, 0, 0, 2, 0}},
		{name: "code is verbatim", query: "not markdown", want: 1, block: 4, start: 12},
		// "Fox" and "FOX" in block 1 drop out.
		{name: "case sensitive", query: "fox", opts: Options{CaseSensitive: true}, want: 5, block: 1, start: 17,
			inBlock: []int{0, 1, 1, 0, 2, 1}},
	}
	for _, c := range cases {
		ms := find(t, fixture(), c.query, c.opts)
		if len(ms) != c.want {
			t.Errorf("%s: %d matches, want %d", c.name, len(ms), c.want)
			continue
		}
		if ms[0].Block != c.block || ms[0].Start != c.start {
			t.Errorf("%s: first match %d:%d, want %d:%d", c.name, ms[0].Block, ms[0].Start, c.block, c.start)
		}
		for block, want := range c.inBlock {
			if got := len(inBlock(ms, block)); got != want {
				t.Errorf("%s: block %d has %d matches, want %d", c.name, block, got, want)
			}
		}
	}
}

// testScanTextOptions.
func TestScanTextOptions(t *testing.T) {
	cases := []struct {
		name  string
		text  string
		query string
		opts  Options
		want  []int
	}{
		{"plain insensitive", "Fox fox FOX", "fox", Options{}, []int{0, 4, 8}},
		{"plain sensitive", "Fox fox FOX", "fox", Options{CaseSensitive: true}, []int{4}},
		{"whole word rejects substrings", "fox foxes 'fox' fox_y", "fox", Options{WholeWord: true}, []int{0, 11}},
		{"whole word with non-word query edges", "a+b c+d", "+", Options{WholeWord: true}, []int{1, 5}},
		{"non-overlapping scan", "aaaa", "aa", Options{}, []int{0, 2}},
		{"regex", "fax fix fox", "f.x", Options{Regex: true}, []int{0, 4, 8}},
		{"regex case sensitive", "Fox fox", "f.x", Options{CaseSensitive: true, Regex: true}, []int{4}},
		{"regex composed with whole word", "fix prefix fixes", "f.x", Options{WholeWord: true, Regex: true}, []int{0}},
		{"regex alternation whole word", "cat cats dog", "cat|dog", Options{WholeWord: true, Regex: true}, []int{0, 9}},
		{"dot does not cross lines", "a\nb axb", "a.b", Options{Regex: true}, []int{4}},
	}
	for _, c := range cases {
		p, err := Compile(c.query, c.opts)
		if err != nil {
			t.Errorf("%s: %v", c.name, err)
			continue
		}
		var got []int
		for _, m := range p.Scan(c.text) {
			got = append(got, m.Start)
		}
		if !slices.Equal(got, c.want) {
			t.Errorf("%s: starts %v, want %v", c.name, got, c.want)
		}
	}
}

// testInvalidRegexIsErrorState: a pattern that does not compile is an
// error with no matches, and the next one that compiles works.
func TestInvalidRegexIsAnError(t *testing.T) {
	texts := textsOf(fixture())
	ms, err := Find(texts, "(unclosed", Options{Regex: true})
	if err == nil || ms != nil {
		t.Fatalf("(unclosed: %d matches, error %v; want an error", len(ms), err)
	}
	ms, err = Find(texts, "(fox)", Options{Regex: true})
	if err != nil || len(ms) != 7 {
		t.Fatalf("(fox): %d matches, error %v; want 7", len(ms), err)
	}
	if ms[0].Captures[0] != "Fox" || ms[0].Captures[1] != "Fox" {
		t.Errorf("captures %q, want the match and its group", ms[0].Captures)
	}
}

// testZeroLengthMatchesSkipped.
func TestZeroLengthMatchesSkipped(t *testing.T) {
	p, err := Compile("x*", Options{Regex: true})
	if err != nil {
		t.Fatal(err)
	}
	if ms := p.Scan("abc"); len(ms) != 0 {
		t.Errorf("x* in abc: %v, want nothing", ms)
	}
	p, _ = Compile("a*", Options{Regex: true})
	ms := p.Scan("aaab")
	if len(ms) != 1 || ms[0].Start != 0 || ms[0].Length != 3 {
		t.Errorf("a* in aaab: %v, want one match at 0 of 3", ms)
	}
}

// testEmojiContentIsSearchable: the rocket is one rune here where  counts
// two UTF-16 code units.
func TestEmojiContentIsSearchable(t *testing.T) {
	blocks := append(fixture(), para("launch 🚀 checklist"))
	ms := find(t, blocks, "🚀", Options{})
	if len(ms) != 1 || ms[0].Block != 6 || ms[0].Start != 7 || ms[0].Length != 1 {
		t.Errorf("rocket: %v, want block 6 at 7 of 1", ms)
	}
	if ms := find(t, blocks, "🚀 check", Options{}); len(ms) != 1 {
		t.Errorf("rocket and text: %d matches, want 1", len(ms))
	}
}

// testInactiveOrEmptyQueryYieldsNothing (the find bar being closed is the
// caller's state).
func TestEmptyQueryFindsNothing(t *testing.T) {
	for _, opts := range []Options{{}, {Regex: true}, {WholeWord: true}} {
		ms, err := Find(textsOf(fixture()), "", opts)
		if err != nil || len(ms) != 0 {
			t.Errorf("%+v: %d matches, error %v", opts, len(ms), err)
		}
	}
}

// testDocumentOrderAndCurrentNumber, testSeedToFirstMatchAfterCursor,
// testSeedWrapsToFirstWhenPastLastMatch, testMatchesForBlockMarksCurrent.
func TestNearestIsTheFirstMatchFromTheCaret(t *testing.T) {
	blocks := fixture()
	ms := find(t, blocks, "fox", Options{})
	cases := []struct {
		name       string
		block, pos int
		want       int
	}{
		{"no caret", -1, 0, 0},
		// Three matches in block 1 and one in block 2 come before it.
		{"caret at the start of the code block", 4, blocks[4].DisplayPos(0), 4},
		{"caret past the last match", 5, blocks[5].DisplayPos(20), 0},
		{"caret inside a match", 1, 8, 1},
	}
	for _, c := range cases {
		if got := Nearest(ms, c.block, c.pos); got != c.want {
			t.Errorf("%s: match %d, want %d", c.name, got, c.want)
		}
	}
	if m := ms[Nearest(ms, -1, 0)]; m.Block != 1 || m.Start != 7 {
		t.Errorf("first match %d:%d, want 1:7", m.Block, m.Start)
	}
	if got := Nearest(nil, 0, 0); got != -1 {
		t.Errorf("no matches: %d, want -1", got)
	}
}

// testNextPreviousWrap.
func TestNextPreviousWrap(t *testing.T) {
	cur := 0
	for range 6 {
		cur = Next(cur, 7)
	}
	if cur != 6 {
		t.Fatalf("after six steps: %d, want 6", cur)
	}
	if cur = Next(cur, 7); cur != 0 {
		t.Errorf("next from the last: %d, want the first", cur)
	}
	if cur = Previous(cur, 7); cur != 6 {
		t.Errorf("previous from the first: %d, want the last", cur)
	}
	if Next(-1, 7) != 0 || Previous(-1, 7) != 6 || Next(3, 0) != -1 {
		t.Error("stepping without a current match or without matches")
	}
}

// testCurrentMatchInfo (mdStart) and TestCollectionSearch's
// testMarkdownPosition: where the caret goes for a match.
func TestMarkdownPos(t *testing.T) {
	blocks := fixture()
	fox := para("The quick **brown fox** jumps")
	cases := []struct {
		name    string
		block   Block
		display int
		want    int
	}{
		{"plain", blocks[5], 0, 0},
		// "bold" is at 8 in the text and 10 in the Markdown.
		{"after a hidden marker", blocks[0], 8, 10},
		{"inside a span", fox, 16, 18},
		{"plain block", para("A second fox block"), 9, 9},
		{"code is verbatim", code("fox in a code block"), 4, 4},
		{"past the end is clamped", blocks[0], 100, len(blocks[0].Markdown)},
		{"before the start is clamped", blocks[0], -3, 0},
	}
	for _, c := range cases {
		if got := c.block.MarkdownPos(c.display); got != c.want {
			t.Errorf("%s: %d, want %d", c.name, got, c.want)
		}
	}
	// Offsets inside a hidden marker land on the nearest edge of the text.
	for md, want := range map[int]int{8: 8, 9: 8, 10: 8, 14: 12, 15: 12, 16: 12, 21: 17} {
		if got := blocks[0].DisplayPos(md); got != want {
			t.Errorf("DisplayPos(%d) = %d, want %d", md, got, want)
		}
	}
}

// testBlockDomainFilters.
func TestBlockDomainFilters(t *testing.T) {
	blocks := fixture()
	ms := find(t, blocks, "fox", Options{Within: InBlocks(1, 2)})
	if len(ms) != 4 || len(inBlock(ms, 4)) != 0 {
		t.Errorf("in blocks 1 and 2: %d matches, want 4", len(ms))
	}
	if InBlocks() != nil {
		t.Error("an empty block domain must be no domain")
	}
}

// testTextDomainEdgeFiltering: from block 1 at 10 (after "Fox") to block 4
// at 10 (after the code block's first "fox" but before its second).
func TestTextDomainEdgeFiltering(t *testing.T) {
	blocks := fixture()
	d := InText(1, blocks[1].DisplayPos(10), 4, blocks[4].DisplayPos(10))
	ms := find(t, blocks, "fox", Options{Within: d})
	if len(ms) != 4 {
		t.Errorf("%d matches, want 4", len(ms))
	}
	for block, want := range []int{0, 2, 1, 0, 1, 0} {
		if got := len(inBlock(ms, block)); got != want {
			t.Errorf("block %d: %d matches, want %d", block, got, want)
		}
	}
	// The ends may come in either order.
	back := find(t, blocks, "fox", Options{Within: InText(4, 10, 1, 10)})
	if !slices.EqualFunc(ms, back, func(a, b Match) bool { return a.Block == b.Block && a.Start == b.Start }) {
		t.Errorf("reversed ends: %v, want %v", back, ms)
	}
}

// testReplaceCurrentAdvancesAndIsOneUndoStep: the replacement goes in, and
// searching again from just after it makes the next remaining match
// current.
func TestReplaceOneMovesToTheNextMatch(t *testing.T) {
	blocks := fixture()
	ms := find(t, blocks, "fox", Options{})
	cur := Nearest(ms, -1, 0)
	md, after := ReplaceOne(blocks[ms[cur].Block], ms[cur], "cat", Options{})
	if md != "second cat block fox FOX" {
		t.Fatalf("block 1 is %q", md)
	}
	blocks[1] = para(md)
	ms = find(t, blocks, "fox", Options{})
	if len(ms) != 6 {
		t.Errorf("%d matches after the replacement, want 6", len(ms))
	}
	next := ms[Nearest(ms, 1, blocks[1].DisplayPos(after))]
	if next.Block != 1 || next.Start != 17 {
		t.Errorf("next match %d:%d, want 1:17", next.Block, next.Start)
	}
}

// testReplaceFullyCoveredSpanFollowsCutContract,
// testReplacePartialSpanKeepsMarkers, testReplaceAcrossMarkerBoundary,
// testReplaceInCodeBlockSplicesVerbatim, testReplacementTextIsMarkdown.
func TestReplaceFollowsTheCutRule(t *testing.T) {
	cases := []struct {
		name        string
		block       Block
		query, repl string
		want        string
	}{
		// The formatting belonged to the replaced text.
		{"whole span", para("x **bold** y"), "bold", "brave", "x brave y"},
		{"part of a span", para("x **bold** y"), "bo", "zz", "x **zzld** y"},
		// The plain part and the span's part go; the rest keeps its markers.
		{"across a marker", para("This is **bold** text"), "is bo", "X", "This X**ld** text"},
		{"code is spliced", code("let fox = **not markdown**\nfox()"), "not markdown", "still code",
			"let fox = **still code**\nfox()"},
		{"the replacement is Markdown", para("second Fox block fox FOX"), "second", "**loud**",
			"**loud** Fox block fox FOX"},
		{"plain and a whole span", para("x **bold** y"), "x bold", "X", "X y"},
	}
	for _, c := range cases {
		ms := find(t, []Block{c.block}, c.query, Options{})
		if len(ms) == 0 {
			t.Errorf("%s: %q not found", c.name, c.query)
			continue
		}
		if got, _ := ReplaceOne(c.block, ms[0], c.repl, Options{}); got != c.want {
			t.Errorf("%s: %q, want %q", c.name, got, c.want)
		}
	}
}

// The cut rule with nested spans: the outermost span whose whole text is
// replaced goes with its markers, and a span only partly replaced keeps
// them (InlineMarkdown::cutRangeResult).
func TestReplaceInNestedSpans(t *testing.T) {
	b := Block{Markdown: "**a *b* c**", Spans: []Span{
		{Start: 4, ContentStart: 5, ContentEnd: 6, End: 7},
		{Start: 0, ContentStart: 2, ContentEnd: 9, End: 11},
	}}
	if got := b.Text(); got != "a b c" {
		t.Fatalf("text %q", got)
	}
	cases := []struct {
		start, end int
		want       string
		after      int
	}{
		{2, 3, "**a X c**", 5},
		{0, 3, "**X c**", 3},
		{0, 5, "X", 1},
		{1, 4, "**aXc**", 4},
	}
	for _, c := range cases {
		got, after := b.Replace(c.start, c.end, "X")
		if got != c.want || after != c.after {
			t.Errorf("[%d,%d): %q after %d, want %q after %d", c.start, c.end, got, after, c.want, c.after)
		}
	}
}

// testReplaceAllRightToLeftWithinBlock: replacements that change the length
// do not move the matches still to be replaced.
func TestReplaceAllRightToLeftWithinBlock(t *testing.T) {
	blocks := fixture()
	blocks[1] = para("fox fox fox")
	ms := find(t, blocks, "fox", Options{})
	edits, n := ReplaceAll(blocks, ms, "foxy", Options{}, spansOf)
	applyEdits(blocks, edits)
	if blocks[1].Markdown != "foxy foxy foxy" || n != 7 {
		t.Errorf("block 1 %q after %d replacements", blocks[1].Markdown, n)
	}
	if ms := find(t, blocks, "fox", Options{}); len(ms) != 7 {
		t.Errorf("%d matches afterwards, want 7 (foxy holds fox)", len(ms))
	}
}

// testReplaceAllAcrossBlocksIsOneUndoStep (the contents; undo is the
// caller's).
func TestReplaceAllAcrossBlocks(t *testing.T) {
	blocks := fixture()
	opts := Options{CaseSensitive: true}
	ms := find(t, blocks, "fox", opts)
	edits, n := ReplaceAll(blocks, ms, "cat", opts, spansOf)
	if n != 5 || len(edits) != 4 {
		t.Fatalf("%d replacements in %d blocks, want 5 in 4", n, len(edits))
	}
	applyEdits(blocks, edits)
	for i, want := range map[int]string{
		1: "second Fox block cat FOX",
		2: "cat item",
		4: "let cat = **not markdown**\ncat()",
		5: "quote line one\ncat two",
	} {
		if blocks[i].Markdown != want {
			t.Errorf("block %d is %q, want %q", i, blocks[i].Markdown, want)
		}
	}
	if ms := find(t, blocks, "fox", opts); len(ms) != 0 {
		t.Errorf("%d matches left", len(ms))
	}
}

// testReplaceAllRespectsDomain.
func TestReplaceAllRespectsDomain(t *testing.T) {
	blocks := fixture()
	opts := Options{Within: InBlocks(2)}
	edits, n := ReplaceAll(blocks, find(t, blocks, "fox", opts), "cat", opts, spansOf)
	if n != 1 || len(edits) != 1 || edits[0] != (Edit{Block: 2, Markdown: "cat item"}) {
		t.Errorf("%d replacements, edits %v", n, edits)
	}
}

// Between two replacements in one block the spans are found again, as the
// app parses the block again for each one: after the second "fox" is
// deleted the span is shorter, and the first "fox" is cut from inside it.
// With the spans found before the first replacement, it would be cut from
// the wrong place.
func TestReplaceAllParsesBetweenReplacements(t *testing.T) {
	blocks := []Block{para("**fox fox**")}
	ms := find(t, blocks, "fox", Options{})
	calls := 0
	parse := func(md string) []Span {
		calls++
		return spansOf(md)
	}
	edits, n := ReplaceAll(blocks, ms, "", Options{}, parse)
	if n != 2 || len(edits) != 1 || edits[0].Markdown != "** **" {
		t.Errorf("%d replacements, edits %+v", n, edits)
	}
	if calls != 1 {
		t.Errorf("parsed %d times, want once, between the two replacements", calls)
	}
}

// testCaptureGroupSubstitution.
func TestCaptureGroupSubstitution(t *testing.T) {
	cases := []struct {
		name, replacement string
		captures          []string
		want              string
	}{
		{"group", "$1!", []string{"ab", "a"}, "a!"},
		{"two groups reordered", "$2$1", []string{"ab", "a", "b"}, "ba"},
		{"whole match", "[$&]", []string{"ab"}, "[ab]"},
		{"literal dollar", "$$5", []string{"ab"}, "$5"},
		{"absent group empty", "$3", []string{"ab", "a"}, ""},
		{"trailing dollar literal", "x$", []string{"ab"}, "x$"},
		{"dollar zero literal", "$0", []string{"ab"}, "$0"},
		{"digit in another script", "$٢", []string{"ab", "a", "b"}, "b"},
	}
	for _, c := range cases {
		if got := SubstituteCaptures(c.replacement, c.captures); got != c.want {
			t.Errorf("%s: %q, want %q", c.name, got, c.want)
		}
	}
}

// testRegexReplaceWithCaptures.
func TestRegexReplaceWithCaptures(t *testing.T) {
	blocks := fixture()
	blocks[1] = para("name: value")
	opts := Options{Regex: true}
	ms := find(t, blocks, `(\w+): (\w+)`, opts)
	if got, _ := ReplaceOne(blocks[1], ms[0], "$2 = $1", opts); got != "value = name" {
		t.Errorf("%q, want %q", got, "value = name")
	}
}

// testPreserveCase.
func TestPreserveCase(t *testing.T) {
	cases := []struct{ name, replacement, matched, want string }{
		{"upper", "cat", "FOX", "CAT"},
		{"lower", "Cat", "fox", "cat"},
		{"capitalized", "cAT", "Fox", "Cat"},
		{"mixed as typed", "cat", "fOx", "cat"},
		{"no letters as typed", "Cat", "123", "Cat"},
		{"single upper letter capitalizes", "cat", "F", "Cat"},
	}
	for _, c := range cases {
		if got := PreserveCase(c.replacement, c.matched); got != c.want {
			t.Errorf("%s: %q, want %q", c.name, got, c.want)
		}
	}
}

// testPreserveCaseEndToEnd.
func TestPreserveCaseEndToEnd(t *testing.T) {
	blocks := fixture()
	opts := Options{PreserveCase: true, Within: InBlocks(1)}
	edits, n := ReplaceAll(blocks, find(t, blocks, "fox", opts), "cat", opts, spansOf)
	if n != 3 || len(edits) != 1 || edits[0].Markdown != "second Cat block cat CAT" {
		t.Errorf("%d replacements, edits %v", n, edits)
	}
}

// testPreviewMatchesApply.
func TestPreviewMatchesApply(t *testing.T) {
	blocks := fixture()
	texts := textsOf(blocks)
	ms := find(t, blocks, "fox", Options{})
	rows := Preview(texts, ms, "cat", Options{})
	if _, n := ReplaceAll(blocks, ms, "cat", Options{}, spansOf); len(rows) != 7 || n != 7 {
		t.Fatalf("%d rows for %d replacements, want 7 and 7", len(rows), n)
	}
	want := PreviewRow{Block: 1, Prefix: "second ", Matched: "Fox", Replacement: "cat", Suffix: " block fox FOX"}
	if rows[0] != want {
		t.Errorf("first row %+v, want %+v", rows[0], want)
	}
}

// testPreviewLineContext: the context stops at the match's line and at 30
// characters, with an ellipsis where it was cut.
func TestPreviewLineContext(t *testing.T) {
	blocks := fixture()
	blocks[5] = para(strings.Repeat("a", 40) + " fox " + strings.Repeat("b", 40) + "\nsecond line")
	texts := textsOf(blocks)
	rows := Preview(texts, find(t, blocks, "fox", Options{}), "cat", Options{})
	row := rows[len(rows)-1]
	if row.Block != 5 {
		t.Fatalf("last row in block %d", row.Block)
	}
	if !strings.HasPrefix(row.Prefix, "…") || !strings.HasSuffix(row.Suffix, "…") {
		t.Errorf("prefix %q and suffix %q need ellipses", row.Prefix, row.Suffix)
	}
	if strings.Contains(row.Prefix, "\n") || strings.Contains(row.Suffix, "\n") {
		t.Error("the context crosses a line")
	}
	if utf8.RuneCountInString(row.Prefix) != 31 || utf8.RuneCountInString(row.Suffix) != 31 {
		t.Errorf("prefix %q and suffix %q should be 30 characters and an ellipsis", row.Prefix, row.Suffix)
	}
}

// testCodeToParagraphRecomputesMatches,
// testParagraphToCodeReplacesTheRightSpan: the same Markdown is different
// text as code and as a paragraph, and a replacement uses the positions of
// the text it was found in.
func TestCodeAndParagraphTextDiffer(t *testing.T) {
	src := "a*b*c"
	if ms := find(t, []Block{code(src)}, "b", Options{}); len(ms) != 1 || ms[0].Start != 2 {
		t.Errorf("as code: %v, want b at 2", ms)
	}
	if ms := find(t, []Block{para(src)}, "b", Options{}); len(ms) != 1 || ms[0].Start != 1 {
		t.Errorf("as a paragraph: %v, want b at 1", ms)
	}
	b := code(src)
	ms := find(t, []Block{b}, "b", Options{})
	if got, _ := ReplaceOne(b, ms[0], "Z", Options{}); got != "a*Z*c" {
		t.Errorf("as code: %q, want a*Z*c", got)
	}
}

// testDenseInSelectionFindStaysLinear: a text domain over 2,000 blocks with
// 40 matches each.
func TestDenseInSelectionFindStaysLinear(t *testing.T) {
	const blocks = 2000
	texts := make([]string, blocks)
	for i := range texts {
		texts[i] = para(strings.Repeat("*x* ", 40)).Text()
	}
	last := utf8.RuneCountInString(texts[blocks-1])
	start := time.Now()
	ms, err := Find(texts, "x", Options{Within: InText(0, 0, blocks-1, last)})
	elapsed := time.Since(start)
	if err != nil || len(ms) != blocks*40 {
		t.Fatalf("%d matches, error %v; want %d", len(ms), err, blocks*40)
	}
	if elapsed > 3*time.Second {
		t.Errorf("%d in-selection matches over %d blocks took %v", len(ms), blocks, elapsed)
	}
}

// A character that changes length when folded (the Kelvin sign folds to K)
// does not move the matches after it.
func TestFoldingKeepsOffsets(t *testing.T) {
	ms := find(t, []Block{para("\u212A fox and ſ fox")}, "fox", Options{})
	if len(ms) != 2 || ms[0].Start != 2 || ms[1].Start != 12 {
		t.Errorf("%v, want fox at 2 and 12", ms)
	}
	if ms := find(t, []Block{para("\u212Aelvin")}, "kelvin", Options{}); len(ms) != 1 {
		t.Errorf("the Kelvin sign is a K when case is ignored: %v", ms)
	}
	if ms := find(t, []Block{para("İstanbul")}, "istanbul", Options{}); len(ms) != 0 {
		t.Errorf("İ has no simple case folding to i: %v", ms)
	}
}
