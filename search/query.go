package search

// A query across notes and what it finds. The rules are
// SearchIndexDb::query, SearchMatching::scanOccurrences and buildMatch in
// src/search/searchindexdb.cpp.
//
// The Qt search box has no query syntax: what is typed is looked for
// literally, quotes and colons included. A query of one or two characters
// matches whole words only ("an" is not found in "and"), and a longer one
// matches anywhere ("row" is found in "brown"). Either way case is ignored
// and accents are not ("cafe" does not find "café"). The folder, the tag
// and the dates are separate filters the window sets from the sidebar and
// the results' date menu (qml/main.qml, qml/SearchResultsView.qml).

import (
	"slices"
	"strings"
	"time"
	"unicode/utf8"
)

// Query is one search across notes.
type Query struct {
	// Text is what was typed. White space around it is ignored.
	Text string
	// Folder keeps notes in this folder and in the folders inside it, the
	// name compared exactly; "" keeps every note.
	Folder string
	// Tag keeps notes with this tag, compared exactly; "" keeps every note.
	Tag string
	// Dates keeps notes by when they were last modified.
	Dates Dates
	// From and To are the first and last day of a CustomRange, each taken
	// in its own time zone; a zero time leaves that end open.
	From, To time.Time
	// Now is the time the other date filters count back from; zero is the
	// current time.
	Now time.Time
}

// Dates is the results' date menu (SearchResultsView.qml).
type Dates int

// The date filters. The last days are counted back from the current time in
// whole days of 24 hours, a year as 365 of them, as the Qt app does.
const (
	AnyTime     Dates = iota // every note
	Today                    // modified since midnight
	Last7Days                // modified in the last 7 days
	Last30Days               // modified in the last 30 days
	LastYear                 // modified in the last 365 days
	CustomRange              // modified from the start of From to the end of To
)

// Results is what a query found.
type Results struct {
	// Notes are the notes found, in path order, each once.
	Notes []Result
	// MatchCount is how many times the query was found in the notes'
	// text, across every note. Matches in titles are not counted.
	MatchCount int
}

// Result is one note that was found: in its title, in its text or both.
type Result struct {
	Path         string // the note's path, as it was added
	Title        string // the note's title, as it was added
	TitleMatched bool   // the title holds the query
	// MatchCount is every occurrence in the note's text, however many.
	MatchCount int
	// MoreMatches is how many occurrences are not in Hits; the results
	// list says so rather than leaving them out silently.
	MoreMatches int
	// Revision is the note's revision when it was found (Index.Add).
	Revision int64
	// Hits are the first ten occurrences in document order.
	Hits []Hit
}

// Hit is one occurrence of the query in a note's text.
type Hit struct {
	Block  int // the block's index in Note.Blocks
	Start  int // the match's first rune in the block's text
	Length int // the query's length in runes

	text  string // the block's text
	at    int    // the match's byte offset in it
	ascii bool   // the text is all ASCII
}

// Snippet is the context the results list shows for a hit: the line the
// match is on, from at most 32 characters before the match and 120
// characters in all, with "…" where the line was cut (buildMatch). start is
// the match's rune offset in the snippet; the match is Length runes long.
// It is worked out when asked for, since a list shows only some of its
// rows, from the note's text as it was when the query ran.
func (h Hit) Snippet() (snippet string, start int) {
	if h.ascii {
		return asciiSnippetAt(h.text, h.at)
	}
	return snippetAt(h.text, h.at)
}

// hitsPerNote is how many hits a result keeps (rowsPerNoteCap).
const hitsPerNote = 10

// plan is a query made ready to run.
type plan struct {
	needle string // the query without the white space around it, folded
	length int    // the query's length in runes
	whole  bool   // one or two characters: whole words only
	// word is the word a note must have for a whole-word query to be found
	// in it, and allWord says the query is that word and nothing else, in
	// which case the note's word counts are the answer.
	word    string
	allWord bool
	// nul says the query holds a NUL, which the folded text uses between
	// blocks; such a query is looked for block by block.
	nul bool
	// grams are the hashes of the query's trigrams, for skipping the notes
	// that cannot hold a query of three characters or more.
	grams []uint32

	folder, tag       string
	floor, ceil       int64
	hasFloor, hasCeil bool
}

// newPlan prepares a query, or returns nil when it can find nothing: an
// empty query, or a short one without a word character, such as "::".
func newPlan(q Query) *plan {
	text := trim(q.Text)
	if text == "" {
		return nil
	}
	p := &plan{
		needle: foldString(text),
		length: utf8.RuneCountInString(text),
		whole:  scalarCount(text) <= 2,
		folder: q.Folder,
		tag:    q.Tag,
	}
	if p.whole && !hasWordChar(p.needle) {
		return nil
	}
	p.nul = strings.IndexByte(p.needle, 0) >= 0
	if p.whole {
		start, end := -1, len(p.needle)
		for i, r := range p.needle {
			if isWordScalar(r) {
				if start < 0 {
					start = i
				}
			} else if start >= 0 {
				end = i
				break
			}
		}
		p.word = p.needle[start:end]
		p.allWord = start == 0 && end == len(p.needle)
	} else if !p.nul {
		for i := 0; i+2 < len(p.needle); i++ {
			p.grams = append(p.grams, gramHash(p.needle[i], p.needle[i+1], p.needle[i+2]))
		}
	}

	now := q.Now
	if now.IsZero() {
		now = time.Now()
	}
	const day = 24 * time.Hour
	switch q.Dates {
	case Today:
		p.floor, p.hasFloor = startOfDay(now).UnixMilli(), true
	case Last7Days:
		p.floor, p.hasFloor = now.UnixMilli()-(7*day).Milliseconds(), true
	case Last30Days:
		p.floor, p.hasFloor = now.UnixMilli()-(30*day).Milliseconds(), true
	case LastYear:
		p.floor, p.hasFloor = now.UnixMilli()-(365*day).Milliseconds(), true
	case CustomRange:
		if !q.From.IsZero() {
			p.floor, p.hasFloor = startOfDay(q.From).UnixMilli(), true
		}
		if !q.To.IsZero() {
			// The last millisecond of the day, as QDate::endOfDay.
			p.ceil, p.hasCeil = startOfDay(q.To).AddDate(0, 0, 1).UnixMilli()-1, true
		}
	}
	return p
}

// startOfDay is midnight at the start of t's day in t's time zone.
func startOfDay(t time.Time) time.Time {
	y, m, d := t.Date()
	return time.Date(y, m, d, 0, 0, 0, 0, t.Location())
}

// keeps reports whether a note passes the filters.
func (p *plan) keeps(e *entry) bool {
	if p.folder != "" && e.note.Folder != p.folder &&
		!(strings.HasPrefix(e.note.Folder, p.folder) && strings.HasPrefix(e.note.Folder[len(p.folder):], "/")) {
		return false
	}
	if p.tag != "" && !slices.Contains(e.note.Tags, p.tag) {
		return false
	}
	if p.hasFloor && e.modified < p.floor {
		return false
	}
	if p.hasCeil && e.modified > p.ceil {
		return false
	}
	return true
}

// scan runs the query over some notes and appends what it finds to out.
func (p *plan) scan(list []*entry, out []Result) []Result {
	var at []int
	var c collector
	for _, e := range list {
		if !p.keeps(e) {
			continue
		}
		title, _ := occurrences(e.title, p.needle, p.whole, 0, false, nil)
		count := 0
		at = at[:0]
		switch {
		case p.nul:
			// The NUL between blocks would let it match across two.
			count, at = p.blockByBlock(e, at)
		case p.whole && p.allWord:
			// A short word: the note's list of short words is the answer.
			if k, ok := e.words[p.needle]; ok {
				w := &e.shorts[k]
				count = int(w.count)
				for _, a := range w.first[:w.n] {
					at = append(at, int(a))
				}
			}
		case p.whole:
			// A short word with punctuation, such as "a.": only a note with
			// the word can hold it.
			if _, ok := e.words[p.word]; ok {
				count, at = occurrences(e.body, p.needle, true, hitsPerNote, true, at)
			}
		case !e.grams.mayHold(p.grams):
			// The note lacks one of the query's trigrams.
		default:
			count, at = occurrences(e.body, p.needle, p.whole, hitsPerNote, true, at)
		}
		if title == 0 && count == 0 {
			continue
		}
		hits := c.hits(p, e, at)
		out = append(out, Result{
			Path:         e.note.Path,
			Title:        e.note.Title,
			TitleMatched: title > 0,
			MatchCount:   count,
			MoreMatches:  count - len(hits),
			Revision:     e.revision,
			Hits:         hits,
		})
	}
	return out
}

// occurrences finds needle in folded text as scanOccurrences does: case is
// already folded away, occurrences do not overlap, and the scan moves past
// an occurrence even when it is refused, which is what keeps it linear. A
// whole-word occurrence must have no word character (isWordScalar) either
// side of it; the NUL between blocks is not one. It keeps the byte offsets
// of the first `keep` occurrences in at, and counts them all when all is
// set, or stops at the last one kept (at the first when keep is 0).
func occurrences(text, needle string, whole bool, keep int, all bool, at []int) (int, []int) {
	count := 0
	for from := 0; from <= len(text)-len(needle); {
		i := strings.Index(text[from:], needle)
		if i < 0 {
			break
		}
		a := from + i
		end := a + len(needle)
		from = end
		if whole && (wordBefore(text, a) || wordAfter(text, end)) {
			continue
		}
		count++
		if len(at) < keep {
			at = append(at, a)
		}
		if !all && len(at) >= keep {
			break
		}
	}
	return count, at
}

func wordBefore(s string, i int) bool {
	if i == 0 {
		return false
	}
	if c := s[i-1]; c < utf8.RuneSelf {
		return wordScalars[c]
	}
	r, _ := utf8.DecodeLastRuneInString(s[:i])
	return isWordScalar(r)
}

func wordAfter(s string, i int) bool {
	if i >= len(s) {
		return false
	}
	r, _ := decode(s, i)
	return isWordScalar(r)
}

// blockByBlock is the scan for a query holding a NUL: each block is folded
// and scanned on its own, and the offsets returned are in the note's body.
func (p *plan) blockByBlock(e *entry, at []int) (int, []int) {
	count := 0
	for b, text := range e.note.Blocks {
		n, local := occurrences(foldString(text), p.needle, p.whole, hitsPerNote-len(at), true, nil)
		count += n
		for _, a := range local {
			// The block's folded text is body's slice from its start.
			at = append(at, e.starts[b]+a)
		}
	}
	return count, at
}

// collector builds hits for one scan in arrays of a few hundred rather
// than one array per note. A full array is left to the results that use it
// and a new one started, so nothing is copied.
type collector struct {
	slab []Hit
}

const slabHits = 256

// hits builds the hits at byte offsets at of a note's body. The offsets are
// in order, so the block and the rune count move forward.
func (c *collector) hits(p *plan, e *entry, at []int) []Hit {
	if len(at) == 0 {
		return nil
	}
	if cap(c.slab)-len(c.slab) < len(at) {
		c.slab = make([]Hit, 0, max(slabHits, len(at)))
	}
	first := len(c.slab)
	b, from, runes := 0, 0, 0
	for _, a := range at {
		for b+1 < len(e.starts) && e.starts[b+1] <= a {
			b++
		}
		if from < e.starts[b] {
			from, runes = e.starts[b], 0
		}
		text := e.note.Blocks[b]
		local := a - e.starts[b]
		if e.ascii[b] {
			runes = local
		} else {
			runes += utf8.RuneCountInString(e.body[from:a])
			if !e.same {
				local = byteOffset(text, runes)
			}
		}
		from = a
		c.slab = append(c.slab, Hit{Block: b, Start: runes, Length: p.length,
			text: text, at: local, ascii: e.ascii[b]})
	}
	return c.slab[first:len(c.slab):len(c.slab)]
}

// byteOffset is the byte offset of rune n in s.
func byteOffset(s string, n int) int {
	i := 0
	for ; n > 0 && i < len(s); n-- {
		_, size := decode(s, i)
		i += size
	}
	return i
}

// snippetAt is the snippet of a match at byte offset a of a block's text
// (buildMatch), and the rune offset of the match in it.
func snippetAt(text string, a int) (string, int) {
	const lead, total = 32, 120
	lineStart := strings.LastIndexByte(text[:a], '\n') + 1
	lineEnd := len(text)
	if i := strings.IndexByte(text[a:], '\n'); i >= 0 {
		lineEnd = a + i
	}

	// A line has at least as many bytes as characters, so a short stretch
	// in bytes needs no counting, and a stretch of ASCII needs no decoding.
	from, before := lineStart, 0
	switch {
	case a-lineStart <= lead:
		before = utf8.RuneCountInString(text[lineStart:a])
	case ascii(text[a-lead-1 : a]):
		from, before = a-lead-1, lead+1
	default:
		for from = a; from > lineStart && before <= lead; before++ {
			_, n := utf8.DecodeLastRuneInString(text[lineStart:from])
			from -= n
		}
	}
	leadCut := before > lead
	if leadCut {
		_, n := decode(text, from)
		from += n
		before--
	}

	to := lineEnd
	switch {
	case lineEnd-from <= total:
	case ascii(text[from : from+total]):
		to = from + total
	default:
		to = from
		for n := 0; n < total && to < lineEnd; n++ {
			_, size := decode(text, to)
			to += size
		}
	}
	return cutSnippet(text, from, to, before, leadCut, to < lineEnd)
}

// asciiSnippetAt is snippetAt for text that is all ASCII, where a character
// is a byte and the window is arithmetic. Only the lead has to be looked at
// for the start of the line.
func asciiSnippetAt(text string, a int) (string, int) {
	const lead, total = 32, 120
	lo := max(0, a-lead-1)
	from := lo
	if i := strings.LastIndexByte(text[lo:a], '\n'); i >= 0 {
		from = lo + i + 1
	}
	leadCut := a-from > lead
	if leadCut {
		from = a - lead
	}
	lineEnd := len(text)
	if i := strings.IndexByte(text[a:], '\n'); i >= 0 {
		lineEnd = a + i
	}
	to := min(lineEnd, from+total)
	return cutSnippet(text, from, to, a-from, leadCut, to < lineEnd)
}

// cutSnippet is the snippet text[from:to] with the match `before` runes
// into it, and an ellipsis at each end that was cut.
func cutSnippet(text string, from, to, before int, leadCut, tailCut bool) (string, int) {
	snippet := text[from:to]
	if leadCut {
		snippet = "…" + snippet
		before++
	}
	if tailCut {
		snippet += "…"
	}
	return snippet, before
}

// ascii reports whether s is all ASCII.
func ascii(s string) bool {
	for i := 0; i < len(s); i++ {
		if s[i] >= utf8.RuneSelf {
			return false
		}
	}
	return true
}
