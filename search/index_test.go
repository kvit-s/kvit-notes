package search

import (
	"fmt"
	"math/rand/v2"
	"slices"
	"strings"
	"testing"
	"time"
	"unicode"
	"unicode/utf8"
)

// The tests in this file are ported from Kvit's tests/test_searchindexdb.cpp
// and tests/test_collectionsearch.cpp; each names the Qt test it comes from.
// The Qt tests of the SQLite database itself (its schema, integrity checks,
// connections, rebuilding, freshness of files on disk, cancellation and
// worker threads) have no counterpart: this index is in memory, is filled
// by the caller and answers in a few milliseconds.
//
// The Qt tests load notes from Markdown files. Here each note is given as
// the text of its blocks as the reader sees it, which is what the Qt app
// derives from the file (CollectionSearchIndex::parseNote): markers
// removed, a code block's source kept, front-matter tags passed as tags.

// note builds a note from its path and blocks, with the title and folder
// the Qt app would give it and the current time as its modification time.
func note(path string, blocks ...string) Note {
	title, folder := TitleFolder(path)
	return Note{Path: path, Title: title, Folder: folder, Modified: time.Now(), Blocks: blocks}
}

func tagged(n Note, tags ...string) Note { n.Tags = tags; return n }

func modified(n Note, t time.Time) Note { n.Modified = t; return n }

func index(notes ...Note) *Index {
	x := &Index{}
	for _, n := range notes {
		x.Add(n)
	}
	return x
}

func run(x *Index, text string) Results { return x.Query(Query{Text: text}) }

func paths(r Results) []string {
	var out []string
	for _, n := range r.Notes {
		out = append(out, n.Path)
	}
	return out
}

func result(r Results, path string) *Result {
	for i := range r.Notes {
		if r.Notes[i].Path == path {
			return &r.Notes[i]
		}
	}
	return nil
}

// testUnicodeScalarRouting: a character outside the Basic Multilingual
// Plane is one character, and so is a letter typed with a separate accent.
func TestUnicodeScalarRouting(t *testing.T) {
	cases := []struct {
		query string
		want  int
	}{
		{"AI", 2},
		{"row", 3},
		{"😀", 1},
		{"e\u0301", 1},
		{"\u1100\u1161\u11A8", 1}, // Hangul jamo that make one syllable
		{"\u1100\u1161\u1100\u1161", 2},
		{"a.", 2},
	}
	for _, c := range cases {
		if got := scalarCount(c.query); got != c.want {
			t.Errorf("%q: %d characters, want %d", c.query, got, c.want)
		}
	}
}

// testWholeWordBoundaries: one- and two-character queries match whole
// words only.
func TestWholeWordBoundaries(t *testing.T) {
	x := index(
		note("AI research.md", "AI research"),
		note("AI driven.md", "AI-driven work"),
		note("Chair.md", "a comfy chair"),
		note("Learn.md", "Learn Go today"),
		note("Going.md", "going home now"),
		note("Stats.md", "analysis in R"),
		note("R2.md", "uses R2 storage"),
		note("Para.md", "one paragraph here"),
	)
	ai := run(x, "AI")
	if ai.MatchCount < 2 || slices.Contains(paths(ai), "Chair.md") {
		t.Errorf("AI: %d matches in %v", ai.MatchCount, paths(ai))
	}
	gr := run(x, "Go")
	if r := result(gr, "Learn.md"); r == nil || r.MatchCount == 0 {
		t.Error("Go: not found in Learn Go today")
	}
	if r := result(gr, "Going.md"); r != nil && r.MatchCount > 0 {
		t.Error("Go: found in going")
	}
	rr := run(x, "R")
	if r := result(rr, "Stats.md"); r == nil || r.MatchCount == 0 {
		t.Error("R: not found in analysis in R")
	}
	if slices.Contains(paths(rr), "Para.md") || slices.Contains(paths(rr), "R2.md") {
		t.Errorf("R: found in %v", paths(rr))
	}
}

// testLongSubstring: three characters or more match anywhere.
func TestLongSubstring(t *testing.T) {
	x := index(note("Brown.md", "The brown fox jumps"), note("Cat.md", "concatenate values"))
	if r := result(run(x, "row"), "Brown.md"); r == nil || r.MatchCount == 0 {
		t.Error("row: not found in brown")
	}
	if r := result(run(x, "cat"), "Cat.md"); r == nil || r.MatchCount == 0 {
		t.Error("cat: not found in concatenate")
	}
	phrase := run(x, "brown fox")
	if len(phrase.Notes) != 1 || phrase.Notes[0].MatchCount != 1 {
		t.Errorf("brown fox: %+v", phrase)
	}
}

// testPunctuationShortQueryInert.
func TestPunctuationShortQueryInert(t *testing.T) {
	x := index(note("Code.md", "x :: y"))
	if r := run(x, "::"); len(r.Notes) != 0 || r.MatchCount != 0 {
		t.Errorf(":: found %+v", r)
	}
}

// testCodeBlockVerbatimAndPunctuation: a code block's text is its source.
func TestCodeBlockVerbatimAndPunctuation(t *testing.T) {
	x := index(note("Ops.md", "Use the arrow", "SELECT a ->> b FROM t"))
	r := run(x, "->>")
	if len(r.Notes) != 1 || r.Notes[0].Hits[0].Block != 1 {
		t.Errorf("->>: %+v", r)
	}
	x.Add(note("Fmt.md", "bold text", "**bold** literal"))
	stars := run(x, "**bold**")
	if len(stars.Notes) != 1 || stars.Notes[0].Path != "Fmt.md" || stars.Notes[0].MatchCount != 1 ||
		stars.Notes[0].Hits[0].Block != 1 {
		t.Errorf("**bold**: %+v", stars)
	}
}

// testTitleMatchesDoNotCountBody, and testMatchesTitlesAndBodies's
// title-only note.
func TestTitleMatchesDoNotCountBody(t *testing.T) {
	x := index(note("Bread recipe.md", "Knead the dough well"))
	r := run(x, "bread")
	if len(r.Notes) != 1 || !r.Notes[0].TitleMatched || r.Notes[0].MatchCount != 0 ||
		len(r.Notes[0].Hits) != 0 || r.MatchCount != 0 {
		t.Errorf("%+v", r)
	}
}

// testIdenticalBlocksDistinctLocations.
func TestIdenticalBlocksDistinctLocations(t *testing.T) {
	x := index(note("Twins.md", "needle here", "filler", "needle here"))
	r := run(x, "needle")
	if len(r.Notes) != 1 || r.Notes[0].MatchCount != 2 ||
		r.Notes[0].Hits[0].Block != 0 || r.Notes[0].Hits[1].Block != 2 {
		t.Errorf("%+v", r)
	}
}

// testRowCapAndMoreMatches, testRowCapIsVisibleNeverSilent: ten hits are
// kept, and the rest are counted.
func TestRowCapAndMoreMatches(t *testing.T) {
	var blocks []string
	for i := range 15 {
		blocks = append(blocks, fmt.Sprintf("line %d has a needle in it", i))
	}
	r := run(index(note("Haystack.md", blocks...)), "needle")
	n := r.Notes[0]
	if r.MatchCount != 15 || n.MatchCount != 15 || len(n.Hits) != 10 || n.MoreMatches != 5 {
		t.Errorf("%d matches, %d hits, %d more", n.MatchCount, len(n.Hits), n.MoreMatches)
	}
}

// testFolderScopeEscapesWildcards, testFolderScopeIsRecursive: a folder
// keeps the notes inside it at any depth, and only those.
func TestFolderScope(t *testing.T) {
	x := index(
		note("a_b/note.md", "target text"),
		note("axb/note.md", "target text"),
		note("a_b/sub/deep.md", "target text"),
		note("a_bc/other.md", "target text"),
		note("root.md", "target text"),
	)
	cases := []struct {
		folder string
		want   []string
	}{
		{"", []string{"a_b/note.md", "a_b/sub/deep.md", "a_bc/other.md", "axb/note.md", "root.md"}},
		{"a_b", []string{"a_b/note.md", "a_b/sub/deep.md"}},
		{"a_b/sub", []string{"a_b/sub/deep.md"}},
		{"A_B", nil},
	}
	for _, c := range cases {
		if got := paths(x.Query(Query{Text: "target", Folder: c.folder})); !slices.Equal(got, c.want) {
			t.Errorf("folder %q: %v, want %v", c.folder, got, c.want)
		}
	}
}

// testTagFilterComposes.
func TestTagFilterComposes(t *testing.T) {
	x := index(
		tagged(note("Bread.md", "knead the o dough"), "cooking"),
		note("Notes.md", "other o content"),
	)
	if got := paths(x.Query(Query{Text: "o", Tag: "cooking"})); !slices.Equal(got, []string{"Bread.md"}) {
		t.Errorf("%v", got)
	}
}

// testDatePresetAndCustomRange, and TestCollectionSearch's testDatePreset.
func TestDatePresetAndCustomRange(t *testing.T) {
	now := time.Now()
	day := 24 * time.Hour
	old := now.Add(-90 * day)
	x := index(
		modified(note("Fresh.md", "interesting fresh"), now.Add(-2*day)),
		modified(note("Old.md", "interesting old"), old),
	)
	cases := []struct {
		name string
		q    Query
		want []string
	}{
		{"any", Query{Dates: AnyTime}, []string{"Fresh.md", "Old.md"}},
		{"today", Query{Dates: Today}, nil},
		{"week", Query{Dates: Last7Days}, []string{"Fresh.md"}},
		{"month", Query{Dates: Last30Days}, []string{"Fresh.md"}},
		{"year", Query{Dates: LastYear}, []string{"Fresh.md", "Old.md"}},
		{"custom around the old note", Query{Dates: CustomRange, From: old.AddDate(0, 0, -1), To: old.AddDate(0, 0, 1)},
			[]string{"Old.md"}},
		{"custom on the old note's day", Query{Dates: CustomRange, From: old, To: old}, []string{"Old.md"}},
		{"custom from the old note's next day", Query{Dates: CustomRange, From: old.AddDate(0, 0, 1)},
			[]string{"Fresh.md"}},
		{"custom up to the day before", Query{Dates: CustomRange, To: old.AddDate(0, 0, -1)}, nil},
		{"custom with no ends", Query{Dates: CustomRange}, []string{"Fresh.md", "Old.md"}},
	}
	for _, c := range cases {
		c.q.Text, c.q.Now = "interesting", now
		if got := paths(x.Query(c.q)); !slices.Equal(got, c.want) {
			t.Errorf("%s: %v, want %v", c.name, got, c.want)
		}
	}
	// A note modified this morning is today's.
	morning := startOfDay(now)
	x.Add(modified(note("Morning.md", "interesting morning"), morning))
	if got := paths(x.Query(Query{Text: "interesting", Dates: Today, Now: now})); !slices.Equal(got, []string{"Morning.md"}) {
		t.Errorf("today: %v", got)
	}
}

// testCaseAndDiacritics: case is ignored, accents are not.
func TestCaseAndDiacritics(t *testing.T) {
	x := index(note("Beverages.md", "Café society"))
	for query, want := range map[string]int{"CAFÉ": 1, "café": 1, "cafe": 0} {
		if got := len(run(x, query).Notes); got != want {
			t.Errorf("%s: %d notes, want %d", query, got, want)
		}
	}
}

// testReplaceAndRemoveKeepFtsConsistent, testLiveUpdateOnSave.
func TestReplaceAndRemove(t *testing.T) {
	x := index(note("Note.md", "alpha beta gamma"))
	if len(run(x, "beta").Notes) != 1 {
		t.Fatal("beta not found")
	}
	x.Add(note("Note.md", "delta epsilon"))
	if len(run(x, "beta").Notes) != 0 || len(run(x, "epsilon").Notes) != 1 || x.Len() != 1 {
		t.Error("the replaced text is still found, or the new text is not")
	}
	x.Add(note("Dough.md", "dough dough"))
	if r := run(x, "dough"); r.MatchCount != 2 {
		t.Errorf("a new note: %d matches, want 2", r.MatchCount)
	}
	if !x.Remove("Note.md") || x.Remove("Note.md") {
		t.Error("removing reports the wrong answer")
	}
	if len(run(x, "epsilon").Notes) != 0 || x.Len() != 1 {
		t.Error("a removed note is still found")
	}
}

// testIndexRevisionIncrements.
func TestIndexRevisionIncrements(t *testing.T) {
	x := &Index{}
	if rev := x.Add(note("Rev.md", "first")); rev != 1 {
		t.Errorf("first revision %d", rev)
	}
	if rev := x.Add(note("Rev.md", "second")); rev != 2 {
		t.Errorf("second revision %d", rev)
	}
	if x.Revision("Rev.md") != 2 || x.Revision("Missing.md") != 0 {
		t.Error("Revision")
	}
	if r := run(x, "second"); r.Notes[0].Revision != 2 {
		t.Errorf("result revision %d", r.Notes[0].Revision)
	}
}

// oracle is the reference matcher of the Qt suite (TestSearchIndexDb::oracle):
// every block of every note scanned with the rules of section 4, written
// here rune by rune and independent of the index.
func oracle(notes []Note, query string) Results {
	var res Results
	q := []rune(trim(query))
	if len(q) == 0 {
		return res
	}
	whole := scalarCount(string(q)) <= 2
	if whole && !hasWordChar(string(q)) {
		return res
	}
	scan := func(text string) []int {
		t := []rune(text)
		var out []int
		for i := 0; i+len(q) <= len(t); {
			eq := true
			for k := range q {
				if foldRune(t[i+k]) != foldRune(q[k]) {
					eq = false
					break
				}
			}
			if !eq {
				i++
				continue
			}
			if !whole || ((i == 0 || !isWordScalar(t[i-1])) && (i+len(q) == len(t) || !isWordScalar(t[i+len(q)]))) {
				out = append(out, i)
			}
			i += len(q)
		}
		return out
	}
	sorted := slices.Clone(notes)
	slices.SortFunc(sorted, func(a, b Note) int { return compareUTF16(a.Path, b.Path) })
	for _, n := range sorted {
		r := Result{Path: n.Path, Title: n.Title, TitleMatched: len(scan(n.Title)) > 0}
		for b, text := range n.Blocks {
			for _, at := range scan(text) {
				r.MatchCount++
				if len(r.Hits) < 10 {
					r.Hits = append(r.Hits, Hit{Block: b, Start: at, Length: len(q)})
				}
			}
		}
		if r.TitleMatched || r.MatchCount > 0 {
			r.MoreMatches = r.MatchCount - len(r.Hits)
			res.MatchCount += r.MatchCount
			res.Notes = append(res.Notes, r)
		}
	}
	return res
}

// buildMatch is the reference snippet, Qt's buildMatch written over runes:
// the match's line, trimmed to 32 leading and 120 total characters with
// ellipses.
func buildMatch(text string, start int) (string, int) {
	t := []rune(text)
	lineStart := 0
	for i := max(0, start-1); i >= 0; i-- {
		if t[i] == '\n' {
			lineStart = i + 1
			break
		}
	}
	lineEnd := len(t)
	for i := start; i < len(t); i++ {
		if t[i] == '\n' {
			lineEnd = i
			break
		}
	}
	from, leadCut := lineStart, false
	if start-lineStart > 32 {
		from, leadCut = start-32, true
	}
	to := min(lineEnd, from+120)
	snippet, at := string(t[from:to]), start-from
	if leadCut {
		snippet, at = "…"+snippet, at+1
	}
	if to < lineEnd {
		snippet += "…"
	}
	return snippet, at
}

func blocksOf(notes []Note, path string) []string {
	for _, n := range notes {
		if n.Path == path {
			return n.Blocks
		}
	}
	return nil
}

// expectOracleAgreement: every query returns exactly what the reference
// matcher returns, note by note and hit by hit.
func expectOracleAgreement(t *testing.T, notes []Note, queries []string) {
	t.Helper()
	x := index(notes...)
	for _, q := range queries {
		got, want := run(x, q), oracle(notes, q)
		if got.MatchCount != want.MatchCount || len(got.Notes) != len(want.Notes) {
			t.Errorf("%q: %d matches in %d notes, want %d in %d",
				q, got.MatchCount, len(got.Notes), want.MatchCount, len(want.Notes))
			continue
		}
		for i, w := range want.Notes {
			g := got.Notes[i]
			if g.Path != w.Path || g.TitleMatched != w.TitleMatched || g.MatchCount != w.MatchCount ||
				g.MoreMatches != w.MoreMatches || len(g.Hits) != len(w.Hits) {
				t.Errorf("%q %s: %+v, want %+v", q, w.Path, g, w)
				continue
			}
			for j, h := range w.Hits {
				gh := g.Hits[j]
				if gh.Block != h.Block || gh.Start != h.Start || gh.Length != h.Length {
					t.Errorf("%q %s hit %d: %d:%d+%d, want %d:%d+%d",
						q, w.Path, j, gh.Block, gh.Start, gh.Length, h.Block, h.Start, h.Length)
					continue
				}
				snippet, start := gh.Snippet()
				wantSnippet, wantStart := buildMatch(blocksOf(notes, w.Path)[h.Block], h.Start)
				if snippet != wantSnippet || start != wantStart {
					t.Errorf("%q %s hit %d: snippet %q at %d, want %q at %d",
						q, w.Path, j, snippet, start, wantSnippet, wantStart)
				}
			}
		}
	}
}

// testDifferentialOracle: ASCII, composed and decomposed Unicode, other
// scripts, emoji, punctuation, code and formatted text.
func TestDifferentialOracle(t *testing.T) {
	notes := []Note{
		note("ascii.md", "The quick brown fox jumps over the lazy dog", "AI and Go and R and R2 in one line"),
		note("folder/nested.md", "A bold brown and italic fox span", "concatenate cats and category"),
		note("unicode.md", "Café near the naïve résumé", "Straße und Grüße"),
		note("cyrillic.md", "Привет мир и Москва"),
		note("emoji.md", "smile 😀 and grin 😀 twice"),
		note("code.md", "prose about arrows", "map ->> reduce ->> collect\n::marker::"),
		note("punct.md", "dot.dot and (a.) plus a_b_c tokens"),
		note("dup.md", "repeat word", "other", "repeat word"),
		note("kelvin.md", "\u212A is a kelvin, ſ is a long s: Kelvin sums"),
	}
	expectOracleAgreement(t, notes, []string{
		"AI", "Go", "R", "R2", "fox", "row", "cat", "brown fox", "bold brown", "café",
		"CAFÉ", "cafe", "straße", "grüße", "Привет", "мир", "Москва", "😀", "->>",
		"::marker::", "a.", "a_b_c", "dot.dot", "repeat word", "the", "category",
		"concatenate", "zzz", "k", "kelvin", "s", "is", "(a", "a)", ".a", "  fox  ", "",
	})
}

// testAstralAndPrivateUseWordBoundaries: a letter beyond the Basic
// Multilingual Plane is a letter, so it can be searched for as a word, and
// "x" is not a word inside "𝐀x".
func TestAstralAndPrivateUseWordBoundaries(t *testing.T) {
	astral := "\U0001D400"
	if !hasWordChar(foldString(astral)) {
		t.Error("a supplementary-plane letter is not a word character")
	}
	x := index(note("Astral.md", astral+"x glued", astral+" alone"))
	hit := run(x, astral)
	if len(hit.Notes) != 1 || hit.MatchCount != 1 || hit.Notes[0].Hits[0].Block != 1 {
		t.Errorf("%s: %+v", astral, hit)
	}
	if r := run(x, "x"); r.MatchCount != 0 {
		t.Errorf("x: %d matches inside the glued word", r.MatchCount)
	}
}

// testTokenizationDifferentialOracle: private-use characters and combining
// marks continue a word.
func TestTokenizationDifferentialOracle(t *testing.T) {
	astral := "\U0001D400"
	private := "\uE000"
	combining := "nai\u0308ve"
	decomposedE := "e\u0301"
	notes := []Note{
		note("astral.md", astral+"x glued and "+astral+" alone"),
		note("private.md", private+"y glued and y alone"),
		note("combining.md", combining+" and "+decomposedE+" alone and caf"+decomposedE),
		note("composed.md", "naïve and é alone and café"),
	}
	expectOracleAgreement(t, notes, []string{
		astral, astral + "x", "x", private, "y", decomposedE, "é", "ve", combining,
		"naïve", "caf" + decomposedE, "café", "alone", "^", "a^",
	})
	// Spot checks against the Qt suite's expectations.
	x := index(notes...)
	for query, want := range map[string]int{"y": 1, decomposedE: 1, "é": 1, "ve": 0} {
		if got := run(x, query).MatchCount; got != want {
			t.Errorf("%q: %d matches, want %d", query, got, want)
		}
	}
}

// The differential oracle over a vault large enough for a query to be
// shared between goroutines.
func TestDifferentialOracleLargeVault(t *testing.T) {
	notes := generate(1200, 1)
	x := index(notes...)
	if x.size < parallelBytes {
		t.Fatalf("%d bytes is too small to take the parallel path", x.size)
	}
	expectOracleAgreement(t, notes, []string{"needle", "the", "to", "a", "ing", "Qu", "zqxj", "of the", "a,",
		"café", "CAFÉ", "naïve", "мир", "straße", "é"})
}

// testHugeBlockKeepsBoundedMatches: counting is exact, and only ten hits
// are kept.
func TestHugeBlockKeepsBoundedMatches(t *testing.T) {
	block := strings.Repeat("needle ", 20000)
	r := run(index(note("Huge.md", block)), "needle")
	n := r.Notes[0]
	if n.MatchCount != 20000 || len(n.Hits) != 10 || n.MoreMatches != 19990 ||
		n.Hits[0].Start != 0 || n.Hits[1].Start != 7 {
		t.Errorf("%d matches, %d hits, %d more, starting %d and %d",
			n.MatchCount, len(n.Hits), n.MoreMatches, n.Hits[0].Start, n.Hits[1].Start)
	}
}

// vault is TestCollectionSearch::init's vault.
func vault() *Index {
	return index(
		note("Fox notes.md", "The quick brown fox jumps", "A second fox block"),
		tagged(note("Recipes/Bread.md", "Knead the dough well", "Loaf recipe here"), "cooking"),
		note("Recipes/Soup/Stock.md", "Simmer bones for stock", "Stock recipe notes", "fox in a code block"),
		note("Plain.md", "Nothing interesting here"),
	)
}

// testEmptyQueryIsInert.
func TestEmptyQueryIsInert(t *testing.T) {
	for _, q := range []string{"", "   ", "\t\n"} {
		if r := run(vault(), q); len(r.Notes) != 0 || r.MatchCount != 0 {
			t.Errorf("%q: %+v", q, r)
		}
	}
}

// testMatchesTitlesAndBodies, testResultShapeAndOrder: notes come in path
// order, hits in document order with display offsets.
func TestMatchesTitlesAndBodies(t *testing.T) {
	r := run(vault(), "fox")
	if got := paths(r); !slices.Equal(got, []string{"Fox notes.md", "Recipes/Soup/Stock.md"}) {
		t.Fatalf("%v", got)
	}
	first := r.Notes[0]
	if !first.TitleMatched || len(first.Hits) != 2 {
		t.Errorf("%+v", first)
	}
	h0, h1 := first.Hits[0], first.Hits[1]
	if h0.Block != 0 || h0.Start != 16 || h0.Length != 3 || h1.Block != 1 || h1.Start != 9 {
		t.Errorf("hits %+v %+v", h0, h1)
	}
}

// testWholeWordShortQuery.
func TestWholeWordShortQuery(t *testing.T) {
	x := vault()
	x.Add(note("Words.md", "an ant and analysis"))
	if r := result(run(x, "an"), "Words.md"); r == nil || r.MatchCount != 1 {
		t.Errorf("an: %+v", r)
	}
}

// testSnippetWindows: the line, with the match's place in it; a long line
// is cut before the match with an ellipsis.
func TestSnippetWindows(t *testing.T) {
	h := run(vault(), "fox").Notes[0].Hits[0]
	if s, start := h.Snippet(); s != "The quick brown fox jumps" || start != 16 || h.Length != 3 {
		t.Errorf("%q at %d, %+v", s, start, h)
	}

	var long strings.Builder
	for i := range 30 {
		fmt.Fprintf(&long, "word%d ", i)
	}
	long.WriteString("needle end of the line goes on and on")
	x := vault()
	x.Add(note("Long.md", long.String()))
	h = run(x, "needle").Notes[0].Hits[0]
	s, start := h.Snippet()
	if rs := []rune(s); !strings.HasPrefix(s, "…") || string(rs[start:start+h.Length]) != "needle" {
		t.Errorf("%q at %d", s, start)
	}
}

// The snippet rules of buildMatch in detail: 32 characters before the
// match, 120 in all, the match's line only, counted in characters.
func TestSnippetRules(t *testing.T) {
	cases := []struct {
		name, text string
		at         int // rune offset of the match
		snippet    string
		start      int
	}{
		{"short line", "one fox two", 4, "one fox two", 4},
		{"other lines are left out", "first\nsecond fox\nthird", 13, "second fox", 7},
		{"at the start of a line", "first\nfox", 6, "fox", 0},
		{"32 characters before is not cut", strings.Repeat("é", 32) + "fox", 32, strings.Repeat("é", 32) + "fox", 32},
		{"33 characters before is", strings.Repeat("é", 33) + "fox", 33, "…" + strings.Repeat("é", 32) + "fox", 33},
		{"the end is cut at 120", "fox" + strings.Repeat("ü", 200), 0, "fox" + strings.Repeat("ü", 117) + "…", 0},
		{"exactly 120 is not cut", "fox" + strings.Repeat("ü", 117), 0, "fox" + strings.Repeat("ü", 117), 0},
		{"32 ASCII characters before", strings.Repeat("a", 32) + "fox", 32, strings.Repeat("a", 32) + "fox", 32},
		{"33 ASCII characters before", strings.Repeat("a", 33) + "fox", 33, "…" + strings.Repeat("a", 32) + "fox", 33},
		{"ASCII end cut at 120", "fox" + strings.Repeat("b", 200), 0, "fox" + strings.Repeat("b", 117) + "…", 0},
		{"both ends cut, on a later line", "x\n" + strings.Repeat("a", 40) + "fox" + strings.Repeat("c", 200) + "\nnext", 42,
			"…" + strings.Repeat("a", 32) + "fox" + strings.Repeat("c", 85) + "…", 33},
		{"ASCII before, accents after", strings.Repeat("a", 40) + "fox" + strings.Repeat("ü", 200), 40,
			"…" + strings.Repeat("a", 32) + "fox" + strings.Repeat("ü", 85) + "…", 33},
		{"accents before, ASCII after", strings.Repeat("é", 40) + "fox" + strings.Repeat("c", 200), 40,
			"…" + strings.Repeat("é", 32) + "fox" + strings.Repeat("c", 85) + "…", 33},
	}
	for _, c := range cases {
		a := byteOffset(c.text, c.at)
		s, start := snippetAt(c.text, a)
		if s != c.snippet || start != c.start {
			t.Errorf("%s: %q at %d, want %q at %d", c.name, s, start, c.snippet, c.start)
		}
		if !ascii(c.text) {
			continue
		}
		if s, start := asciiSnippetAt(c.text, a); s != c.snippet || start != c.start {
			t.Errorf("%s, ASCII: %q at %d, want %q at %d", c.name, s, start, c.snippet, c.start)
		}
	}
}

// A note with a character that changes length when folded keeps its hits'
// offsets and snippets in its own text.
func TestFoldedLengthChangeKeepsHits(t *testing.T) {
	x := index(note("K.md", "\u212A and ſ, then the fox"))
	h := run(x, "fox").Notes[0].Hits[0]
	if s, start := h.Snippet(); h.Start != 18 || start != 18 || s != "\u212A and ſ, then the fox" {
		t.Errorf("%q at %d, %+v", s, start, h)
	}
	if r := run(x, "k"); r.MatchCount != 1 {
		t.Errorf("k: %d matches, want the Kelvin sign", r.MatchCount)
	}
}

// testCodeBlockContentMatches.
func TestCodeBlockContentMatches(t *testing.T) {
	r := run(vault(), "fox in a code")
	if len(r.Notes) != 1 || r.Notes[0].Path != "Recipes/Soup/Stock.md" || r.Notes[0].Hits[0].Block != 2 {
		t.Errorf("%+v", r)
	}
}

// testFolderScopeIsRecursive, testTagFilter, testFiltersCompose.
func TestFiltersCompose(t *testing.T) {
	cases := []struct {
		folder, tag string
		want        []string
	}{
		{"", "", []string{"Recipes/Bread.md", "Recipes/Soup/Stock.md"}},
		{"Recipes", "", []string{"Recipes/Bread.md", "Recipes/Soup/Stock.md"}},
		{"Recipes/Soup", "", []string{"Recipes/Soup/Stock.md"}},
		{"", "cooking", []string{"Recipes/Bread.md"}},
		{"Recipes", "cooking", []string{"Recipes/Bread.md"}},
		{"Recipes/Soup", "cooking", nil},
	}
	for _, c := range cases {
		got := paths(vault().Query(Query{Text: "recipe", Folder: c.folder, Tag: c.tag}))
		if !slices.Equal(got, c.want) {
			t.Errorf("folder %q tag %q: %v, want %v", c.folder, c.tag, got, c.want)
		}
	}
}

// Results come in the order of QString's comparison, by UTF-16 code units,
// where a character beyond the Basic Multilingual Plane sorts before one
// from U+E000 up.
func TestResultsAreInQtPathOrder(t *testing.T) {
	want := []string{"A.md", "B.md", "a.md", "é.md", "😀.md", "Ａ.md"}
	shuffled := slices.Clone(want)
	rand.New(rand.NewPCG(1, 2)).Shuffle(len(shuffled), func(i, j int) {
		shuffled[i], shuffled[j] = shuffled[j], shuffled[i]
	})
	x := &Index{}
	for _, p := range shuffled {
		x.Add(note(p, "common text"))
	}
	if got := paths(run(x, "common")); !slices.Equal(got, want) {
		t.Errorf("%q, want %q", got, want)
	}
}

// TitleFolder follows CollectionSearchIndex::parseNote.
func TestTitleFolder(t *testing.T) {
	cases := []struct{ path, title, folder string }{
		{"Plain.md", "Plain", ""},
		{"Recipes/Soup/Stock.md", "Stock", "Recipes/Soup"},
		{"Notes/UPPER.MD", "UPPER", "Notes"},
		{"Notes/no suffix", "no suffix", "Notes"},
		{"a.md.md", "a.md", ""},
	}
	for _, c := range cases {
		if title, folder := TitleFolder(c.path); title != c.title || folder != c.folder {
			t.Errorf("%s: %q in %q, want %q in %q", c.path, title, folder, c.title, c.folder)
		}
	}
}

// A query holding a NUL, which the index uses between blocks, is looked for
// block by block and never across two blocks.
func TestQueryWithNul(t *testing.T) {
	x := index(note("Nul.md", "one\x00two", "one", "two"))
	r := run(x, "one\x00two")
	if r.MatchCount != 1 || r.Notes[0].Hits[0].Block != 0 {
		t.Errorf("%+v", r)
	}
}

// testQueryPerformanceGate: a 500-note vault answers well inside the Qt
// suite's 45 ms budget. The budget is loose on purpose; the benchmark
// below gives the numbers.
func TestQueryPerformanceGate(t *testing.T) {
	x := index(generate(500, 3)...)
	for _, q := range []string{"needle", "paragraph", "to"} {
		start := time.Now()
		r := run(x, q)
		if elapsed := time.Since(start); elapsed > 45*time.Millisecond {
			t.Errorf("%q took %v (%d matches in %d notes)", q, elapsed, r.MatchCount, len(r.Notes))
		}
	}
}

// words is the vocabulary of generated notes: common English words first,
// so a Zipf distribution over it makes text with the frequencies of prose.
var words = strings.Fields(`the of and to a in is it that for you was with on as have but be they
at one this from or had by word not what all were we when your can said there use an each which she do
how their if will up other about out many then them these so some her would make like him into time
has look two more write go see number no way could people my than first water been call who oil its now
find long down day did get come made may part paragraph note text block search index query result
kvit markdown editor folder tag recipe bread dough flour water salt oven stock simmer bones notes
running thinking writing reading building testing indexing matching finding keeping making taking
quick brown fox jumps over lazy dog Quantum quality question quiet quote
café naïve Grüße Straße résumé Привет мир`)

// generate makes n notes of about 2 KB each: ten blocks of prose from the
// vocabulary, in a few folders, some tagged, with "needle" in every fiftieth
// note. The same seed makes the same notes.
func generate(n int, seed uint64) []Note {
	rng := rand.New(rand.NewPCG(seed, 7))
	zipf := rand.NewZipf(rng, 1.1, 2, uint64(len(words)-1))
	now := time.Now()
	notes := make([]Note, n)
	for i := range notes {
		blocks := make([]string, 10)
		for b := range blocks {
			var s strings.Builder
			for s.Len() < 195 {
				if s.Len() > 0 {
					s.WriteByte(' ')
				}
				w := words[zipf.Uint64()]
				if s.Len() == 0 {
					r, n := utf8.DecodeRuneInString(w)
					w = string(unicode.ToUpper(r)) + w[n:]
				}
				s.WriteString(w)
				if rng.IntN(12) == 0 {
					s.WriteByte(',')
				}
			}
			s.WriteByte('.')
			blocks[b] = s.String()
		}
		if i%50 == 0 {
			blocks[rng.IntN(10)] += " A needle."
		}
		path := fmt.Sprintf("Folder %d/Note %d.md", i%20, i)
		nt := modified(note(path, blocks...), now.Add(-time.Duration(i)*time.Hour))
		if i%7 == 0 {
			nt.Tags = []string{"seven"}
		}
		notes[i] = nt
	}
	return notes
}

// BenchmarkQuery times queries over 5,000 generated notes of about 2 KB
// (10 MB of text): a rare word, common words, a short whole word, a single
// letter, a common fragment, a phrase, a word that is not there, and a
// common word inside one folder.
func BenchmarkQuery(b *testing.B) {
	notes := generate(5000, 42)
	x := index(notes...)
	b.Logf("%d notes, %.1f MB of text", x.Len(), float64(x.size)/1e6)
	cases := []struct {
		name string
		q    Query
	}{
		{"rare", Query{Text: "needle"}},
		{"common", Query{Text: "the"}},
		{"paragraph", Query{Text: "paragraph"}},
		{"short-word", Query{Text: "to"}},
		{"letter", Query{Text: "a"}},
		{"fragment", Query{Text: "ing"}},
		{"phrase", Query{Text: "quick brown fox"}},
		{"absent", Query{Text: "zqxj"}},
		{"common-in-folder", Query{Text: "the", Folder: "Folder 3"}},
	}
	for _, c := range cases {
		b.Run(c.name, func(b *testing.B) {
			var r Results
			for b.Loop() {
				r = x.Query(c.q)
			}
			b.ReportMetric(float64(r.MatchCount), "matches")
			b.ReportMetric(float64(len(r.Notes)), "notes")
		})
	}
}

// BenchmarkBuild times building the index of 5,000 generated notes.
func BenchmarkBuild(b *testing.B) {
	notes := generate(5000, 42)
	for b.Loop() {
		index(notes...)
	}
}

// The benchmark's notes are what they claim to be.
func TestGeneratedNotes(t *testing.T) {
	notes := generate(500, 42)
	size := 0
	for _, n := range notes {
		for _, b := range n.Blocks {
			size += len(b)
			if !utf8.ValidString(b) {
				t.Fatal("invalid text")
			}
		}
	}
	if avg := size / len(notes); avg < 1900 || avg > 2300 {
		t.Errorf("notes average %d bytes, want about 2 KB", avg)
	}
}
