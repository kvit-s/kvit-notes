package search

// Searching every note of a vault (features.md 8.4). The rules are the Qt
// app's: src/search/searchindexdb.cpp (SearchIndexDb::query and the
// SearchMatching functions), src/search/collectionsearchindex.cpp
// (parseNote) and src/application/collectionsearch.cpp.
//
// The Qt app keeps its index in an SQLite database under the cache
// directory, with FTS5 word and trigram indexes that only propose candidate
// blocks, and checks every candidate against the note's text. This index is
// in memory and is rebuilt from the notes whenever the app opens a vault, so
// nothing on disk has to agree with the Qt app's database. What it keeps for
// each note, when the note is added:
//
//   - the text of every block with each character case-folded, joined into
//     one string with a NUL between blocks, so a query is one strings.Index
//     loop over the note;
//   - each word of at most two characters, with how often it occurs and
//     where its first ten occurrences are, so a one- or two-character
//     query, which must match whole words, is answered without reading the
//     note;
//   - a Bloom filter of the byte trigrams in that string, so a longer query
//     skips the notes that cannot hold it without reading them. The filter
//     can say a note may hold a query when it does not, never the reverse,
//     and every note it lets through is scanned, so results stay exact.
//
// A query goes through the notes in path order, on as many goroutines as
// there are processors when the vault is large, and builds each result the
// way the Qt app builds it after its database has proposed the note.

import (
	"runtime"
	"slices"
	"sort"
	"strings"
	"sync"
	"time"
	"unicode/utf8"
)

// Note is one note as the index takes it.
type Note struct {
	// Path is vault-relative with forward slashes: "Recipes/Bread.md".
	Path string
	// Title is what the results list shows for the note. The Qt app uses the
	// file name without ".md" (TitleFolder).
	Title string
	// Folder is the folder the note is in, "" at the top of the vault. The
	// Qt app uses the path up to the last slash (TitleFolder).
	Folder string
	// Tags are the note's front-matter tags.
	Tags []string
	// Modified is the file's modification time.
	Modified time.Time
	// Blocks are the text of each block as the reader sees it (Block.Text):
	// a code block's source, a divider's nothing. Hits name blocks by their
	// index here.
	Blocks []string
}

// TitleFolder is the title and folder the Qt app gives a note from its path
// (CollectionSearchIndex::parseNote): the file name without a ".md" in any
// case, and everything before the last slash.
func TitleFolder(path string) (title, folder string) {
	name := path
	if i := strings.LastIndexByte(path, '/'); i >= 0 {
		folder, name = path[:i], path[i+1:]
	}
	if n := len(name) - len(".md"); n >= 0 && strings.EqualFold(name[n:], ".md") {
		name = name[:n]
	}
	return name, folder
}

// Index is the search index of one vault. Its methods may be called from
// any goroutine; a query sees each note either before or after a change to
// it. The zero Index is empty and ready to use.
type Index struct {
	mu     sync.RWMutex
	byPath map[string]*entry
	list   []*entry // in path order, as the results are
	size   int      // bytes of text across every note
}

// entry is one note in the index.
type entry struct {
	note     Note
	modified int64            // the modification time in milliseconds, as the Qt app stores it
	title    string           // the title, folded
	body     string           // every block's text, folded, with a NUL between blocks
	starts   []int            // where each block starts in body
	same     bool             // body's byte offsets are the blocks' byte offsets
	ascii    []bool           // which blocks are all ASCII, where runes are bytes
	words    map[string]int32 // each word of at most two characters, as an index into shorts
	shorts   []shortWord      // where those words are
	grams    trigrams         // the byte trigrams of body
	revision int64
}

// Add adds a note, or replaces the note with the same path, and returns the
// note's revision: 1 when it is new, one more than before when it replaces
// one (SearchIndexDb::replaceNote). A result carries the revision it was
// found at, so a click can tell that the note has changed since.
func (x *Index) Add(n Note) int64 {
	e := newEntry(n)
	x.mu.Lock()
	defer x.mu.Unlock()
	if x.byPath == nil {
		x.byPath = map[string]*entry{}
	}
	i, found := x.position(n.Path)
	if found {
		old := x.list[i]
		e.revision = old.revision + 1
		x.size -= len(old.body)
		x.list[i] = e
	} else {
		e.revision = 1
		x.list = slices.Insert(x.list, i, e)
	}
	x.size += len(e.body)
	x.byPath[n.Path] = e
	return e.revision
}

// Remove removes a note. It reports whether the note was in the index.
func (x *Index) Remove(path string) bool {
	x.mu.Lock()
	defer x.mu.Unlock()
	i, found := x.position(path)
	if !found {
		return false
	}
	x.size -= len(x.list[i].body)
	x.list = slices.Delete(x.list, i, i+1)
	delete(x.byPath, path)
	return true
}

// Revision is a note's revision, or 0 when the note is not in the index
// (SearchIndexDb::revisionOf).
func (x *Index) Revision(path string) int64 {
	x.mu.RLock()
	defer x.mu.RUnlock()
	if e := x.byPath[path]; e != nil {
		return e.revision
	}
	return 0
}

// Len is how many notes the index holds.
func (x *Index) Len() int {
	x.mu.RLock()
	defer x.mu.RUnlock()
	return len(x.list)
}

// position is where a path is in the list, or where it would go.
func (x *Index) position(path string) (int, bool) {
	i := sort.Search(len(x.list), func(i int) bool {
		return compareUTF16(x.list[i].note.Path, path) >= 0
	})
	return i, i < len(x.list) && x.list[i].note.Path == path
}

// compareUTF16 orders two strings as QString's operator< does, by their
// UTF-16 code units. That is the order of Go's byte comparison except that
// a character beyond the Basic Multilingual Plane (a surrogate pair to Qt)
// sorts before the characters from U+E000 to U+FFFF.
func compareUTF16(a, b string) int {
	i := 0
	for i < len(a) && i < len(b) && a[i] == b[i] {
		i++
	}
	switch {
	case i == len(a) && i == len(b):
		return 0
	case i == len(a):
		return -1
	case i == len(b):
		return 1
	}
	for i > 0 && !utf8.RuneStart(a[i]) {
		i--
	}
	ra, _ := utf8.DecodeRuneInString(a[i:])
	rb, _ := utf8.DecodeRuneInString(b[i:])
	return int(utf16Order(ra)) - int(utf16Order(rb))
}

func utf16Order(r rune) rune {
	if r >= 0xE000 && r <= 0xFFFF {
		return r + 0x110000
	}
	return r
}

// newEntry prepares a note for searching.
func newEntry(n Note) *entry {
	n.Tags = slices.Clone(n.Tags)
	n.Blocks = slices.Clone(n.Blocks)
	e := &entry{
		note:     n,
		modified: n.Modified.UnixMilli(),
		title:    foldString(n.Title),
		starts:   make([]int, len(n.Blocks)),
		same:     true,
		ascii:    make([]bool, len(n.Blocks)),
	}
	var b strings.Builder
	size := len(n.Blocks)
	for _, t := range n.Blocks {
		size += len(t)
	}
	b.Grow(size)
	for i, t := range n.Blocks {
		if i > 0 {
			b.WriteByte(0)
		}
		e.starts[i] = b.Len()
		f, same := fold(t)
		e.same = e.same && same
		e.ascii[i] = ascii(t)
		b.WriteString(f)
	}
	e.body = b.String()
	e.words, e.shorts = shortWords(e.body)
	e.grams = newTrigrams(e.body)
	return e
}

// trigrams is a Bloom filter of the three-byte sequences in a note's folded
// text, with one hash and about two bits per byte of text. On Kvit's own
// Markdown documentation cut into notes of 2 KB, 21% of the bits are set,
// and a random six-letter string that is not in a note gets past its
// filter once in about 350 notes.
type trigrams struct {
	bits  []uint64
	shift uint8 // a hash's top bits pick its bit
}

// gramHash is a trigram's hash before it is cut to a filter's size.
func gramHash(a, b, c byte) uint32 {
	return (uint32(a)<<16 | uint32(b)<<8 | uint32(c)) * 0x9E3779B1
}

func newTrigrams(body string) trigrams {
	n, log := 512, 9
	for n < 2*len(body) && n < 1<<20 {
		n, log = n<<1, log+1
	}
	t := trigrams{bits: make([]uint64, n/64), shift: uint8(32 - log)}
	for i := 0; i+2 < len(body); i++ {
		h := gramHash(body[i], body[i+1], body[i+2]) >> t.shift
		t.bits[h>>6] |= 1 << (h & 63)
	}
	return t
}

// mayHold reports whether text with these trigram hashes may be in the
// note: false when one of them is certainly not.
func (t trigrams) mayHold(hashes []uint32) bool {
	for _, h := range hashes {
		h >>= t.shift
		if t.bits[h>>6]&(1<<(h&63)) == 0 {
			return false
		}
	}
	return true
}

// shortWord is where a word of at most two characters occurs in a note's
// folded text.
type shortWord struct {
	count int32              // how often
	n     int32              // how many of first are set
	first [hitsPerNote]int32 // the byte offsets of the first occurrences
}

// shortWords finds the words of at most two characters in folded text. A
// word is a run of word characters (isWordScalar), the unit a whole-word
// match is bounded by.
//
// For a query made only of word characters, SearchMatching::scanOccurrences
// finds exactly the words equal to it: an occurrence bounded by non-word
// characters is a whole word, and the scan's step past an occurrence it
// refused cannot skip one, since the character before a word is not a word
// character while every character of the refused occurrence is. So these
// counts and offsets are the scan's answer.
func shortWords(body string) (map[string]int32, []shortWord) {
	words := map[string]int32{}
	var list []shortWord
	for i := 0; i < len(body); {
		r, n := decode(body, i)
		if !isWordScalar(r) {
			i += n
			continue
		}
		j := i + n
		for j < len(body) {
			r, n := decode(body, j)
			if !isWordScalar(r) {
				break
			}
			j += n
		}
		if w := body[i:j]; short(w) {
			k, ok := words[w]
			if !ok {
				k = int32(len(list))
				words[w] = k
				list = append(list, shortWord{})
			}
			sw := &list[k]
			sw.count++
			if sw.n < hitsPerNote {
				sw.first[sw.n] = int32(i)
				sw.n++
			}
		}
		i = j
	}
	return words, list
}

// short reports whether a word counts as at most two characters
// (scalarCount), without counting a long word to its end.
func short(w string) bool {
	if len(w) <= 2 {
		return true
	}
	for i := 0; i < len(w) && w[i] < utf8.RuneSelf; i++ {
		if i >= 2 {
			return false
		}
	}
	return scalarCount(w) <= 2
}

// decode is utf8.DecodeRuneInString at s[i:], quicker for ASCII.
func decode(s string, i int) (rune, int) {
	if c := s[i]; c < utf8.RuneSelf {
		return rune(c), 1
	}
	return utf8.DecodeRuneInString(s[i:])
}

// Query finds a query in the notes (SearchIndexDb::query). The results are
// in path order, as the Qt app lists them; it does not rank notes.
func (x *Index) Query(q Query) Results {
	p := newPlan(q)
	if p == nil {
		return Results{}
	}
	x.mu.RLock()
	defer x.mu.RUnlock()

	var notes []Result
	workers := runtime.GOMAXPROCS(0)
	if workers == 1 || x.size < parallelBytes {
		notes = p.scan(x.list, nil)
	} else {
		chunks := chunk(x.list, x.size, 2*workers)
		parts := make([][]Result, len(chunks))
		var wg sync.WaitGroup
		for c, list := range chunks {
			wg.Go(func() { parts[c] = p.scan(list, nil) })
		}
		wg.Wait()
		notes = slices.Concat(parts...)
	}
	r := Results{Notes: notes}
	for _, n := range notes {
		r.MatchCount += n.MatchCount
	}
	return r
}

// parallelBytes is how much text a vault holds before a query is shared
// between goroutines: below it, starting them costs more than they save.
const parallelBytes = 1 << 20

// chunk cuts the list into about n runs of notes holding similar amounts of
// text.
func chunk(list []*entry, size, n int) [][]*entry {
	var out [][]*entry
	per := size/n + 1
	start, bytes := 0, 0
	for i, e := range list {
		bytes += len(e.body)
		if bytes >= per {
			out = append(out, list[start:i+1])
			start, bytes = i+1, 0
		}
	}
	if start < len(list) {
		out = append(out, list[start:])
	}
	return out
}
