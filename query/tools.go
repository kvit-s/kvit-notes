package query

import (
	"slices"
	"sync"
)

// StarterSpec is the body of a query block inserted from the "/" menu. The
// two filters are commented out, so a new block lists the whole collection
// instead of showing an error, and the reader edits lines that are already
// there rather than having to know the keys.
const StarterSpec = "# from: projects/\n" +
	"# where: status = active\n" +
	"view: table\n" +
	"columns: title, tags, modified\n" +
	"sort: modified desc"

// The "/" menu entry that inserts a query block.
const (
	FenceLanguage   = "query" // the fence's language word
	MenuName        = "Collection Query"
	MenuDescription = "Live table or board over your notes' front-matter"
	MenuGroup       = "Advanced"
	MenuIcon        = "⌕"
)

// MenuAliases are the other words that find the entry when typed after "/".
var MenuAliases = []string{"query", "database", "dataview", "filter", "frontmatter"}

// NoCollectionError is the message a query block shows when no collection
// is open.
const NoCollectionError = "no collection is open"

// Answer is what a query block shows for one body: either the reason it
// cannot show results or the evaluated result. Answers are shared between
// the cache and every block that asked, so they must be treated as
// read-only.
type Answer struct {
	// OK is false when the body does not parse or no collection is open.
	OK bool
	// Error is the message the block shows in place of results when OK is
	// false.
	Error string
	// View is the spec's view; ViewTable when OK is false.
	View View
	Result
}

// Source is the open collection as Tools reads it.
type Source interface {
	// Revision is a number that changes whenever a note is added, removed or
	// changed. Tools keeps results per revision.
	Revision() int
	// Notes returns every note in the collection, in any order. Tools calls
	// it once per revision and copies the slice, but it shares the Tags
	// slices and Fields maps inside the notes with a background goroutine,
	// so the collection must replace those rather than change them in place.
	Notes() []Note
}

// Limits on what the cache keeps. Rows are what a cached result costs, so the
// row budget stops one unlimited query over a large collection from filling
// memory 64 times over.
const (
	maxCacheEntries = 64
	maxCacheRows    = 20000
)

// Tools evaluates query blocks for the editor.
//
// A query reads every note, sorts the matches and builds the rows, which is
// too much work for the goroutine that draws frames when several blocks
// refresh after one change to the collection. RequestRun therefore evaluates
// on a background goroutine against a copy of the notes, and three things
// keep the work from multiplying:
//
//   - one copy of the notes per collection revision, shared by every query
//     at that revision;
//   - requests for the same body at the same revision share one evaluation,
//     so several blocks showing one query cost one evaluation;
//   - a result from a revision that has since changed is dropped rather
//     than delivered, because the blocks have already asked again.
//
// Answers are cached by body and revision. The methods may be called from
// any goroutine. Tools calls Source methods while holding its own lock, so a
// Source must not call back into Tools from them.
type Tools struct {
	// OnResult receives each answer RequestRun produces, with the token the
	// request was made with. Set it before the first call and do not change
	// it afterwards.
	OnResult func(token string, answer Answer)

	// Post, when set, is how a background evaluation hands its answer back.
	// Tools calls it with a function that stores the answer and calls
	// OnResult; Post should run that function on the goroutine that drives
	// the editor. When Post is nil the function runs on the background
	// goroutine. Set it before the first call.
	Post func(func())

	mu          sync.Mutex
	collection  Source
	generation  uint64 // changes with the collection or its root folder
	cache       []cacheEntry
	evaluations int
	pending     []*pendingRun

	// The notes every query at snapshotRevision evaluates against.
	snapshot           []Note
	snapshotRevision   int
	snapshotGeneration uint64
	snapshotValid      bool
}

type cacheEntry struct {
	generation uint64
	revision   int
	body       string
	answer     Answer
	rows       int // what this entry costs, for the row budget
}

// pendingRun is one evaluation running in the background and the tokens of
// every request waiting for it.
type pendingRun struct {
	generation uint64
	revision   int
	body       string
	tokens     []string
}

// SetCollection sets the collection queries read, or nil for none. Changing
// it drops every cached answer and every running evaluation's answer. The
// Source is compared with ==, so pass a pointer.
func (t *Tools) SetCollection(c Source) {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.collection == c {
		return
	}
	t.resetLocked()
	t.collection = c
}

// RootChanged tells Tools that the collection now reads a different folder.
// It drops every cached answer and every running evaluation's answer; the
// owner of the collection calls it.
func (t *Tools) RootChanged() {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.resetLocked()
}

func (t *Tools) resetLocked() {
	// The goroutines already running finish on their own; with their
	// entries gone from pending, finishPending drops what they produce.
	t.pending = nil
	t.generation++
	t.cache = nil
	t.snapshotValid = false
	t.snapshot = nil
}

// ClearCache empties the cache and resets EvaluationCount. Tests use it.
func (t *Tools) ClearCache() {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.cache = nil
	t.evaluations = 0
	t.snapshotValid = false
	t.snapshot = nil
}

// EvaluationCount is how many answers Tools has worked out rather than
// taken from the cache, parse errors included.
func (t *Tools) EvaluationCount() int {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.evaluations
}

// CacheSize is the number of cached answers.
func (t *Tools) CacheSize() int {
	t.mu.Lock()
	defer t.mu.Unlock()
	return len(t.cache)
}

// Run parses and evaluates body on the calling goroutine, using and filling
// the cache. The query block itself uses RequestRun; Run is for callers
// with nowhere to wait.
func (t *Tools) Run(body string) Answer {
	t.mu.Lock()
	defer t.mu.Unlock()
	revision := t.revisionLocked()
	if i := t.findLocked(body, revision); i >= 0 {
		answer := t.cache[i].answer
		t.moveToFrontLocked(i)
		return answer
	}
	answer := t.evaluateNowLocked(body, revision)
	t.cacheResultLocked(body, revision, answer)
	return answer
}

// CachedResult is the cached answer for body at the collection's current
// revision, if there is one, so a block can show a result that has already
// been worked out without waiting for OnResult.
func (t *Tools) CachedResult(body string) (Answer, bool) {
	t.mu.Lock()
	defer t.mu.Unlock()
	if i := t.findLocked(body, t.revisionLocked()); i >= 0 {
		return t.cache[i].answer, true
	}
	return Answer{}, false
}

// RequestRun answers body through OnResult(token, answer). The token is the
// caller's own name for the request, normally the block's id, and comes
// back unchanged so one Tools can serve every query block.
//
// A cached answer, a parse error and the no-collection error are delivered
// before RequestRun returns, on the calling goroutine. Anything else is
// evaluated on a background goroutine and delivered through Post; a request
// for a body already being evaluated at the same revision waits for that
// evaluation instead of starting another.
func (t *Tools) RequestRun(token, body string) {
	t.mu.Lock()
	revision := t.revisionLocked()

	if i := t.findLocked(body, revision); i >= 0 {
		answer := t.cache[i].answer
		t.moveToFrontLocked(i)
		t.mu.Unlock()
		t.deliver(token, answer)
		return
	}

	// A parse error needs no evaluation and no goroutine; it is also the
	// answer to most bodies, which are being typed.
	spec, err := Parse(body)
	if err != nil || t.collection == nil {
		t.evaluations++
		answer := Answer{Error: NoCollectionError}
		if err != nil {
			answer.Error = err.Error()
		}
		t.cacheResultLocked(body, revision, answer)
		t.mu.Unlock()
		t.deliver(token, answer)
		return
	}

	for _, p := range t.pending {
		if p.generation == t.generation && p.revision == revision && p.body == body {
			if !slices.Contains(p.tokens, token) {
				p.tokens = append(p.tokens, token)
			}
			t.mu.Unlock()
			return
		}
	}

	t.evaluations++
	p := &pendingRun{
		generation: t.generation,
		revision:   revision,
		body:       body,
		tokens:     []string{token},
	}
	t.pending = append(t.pending, p)
	snapshot := t.snapshotForLocked(revision)
	post := t.Post
	t.mu.Unlock()

	go func() {
		answer := evaluateSpec(spec, snapshot)
		finish := func() { t.finishPending(p, answer) }
		if post != nil {
			post(finish)
		} else {
			finish()
		}
	}()
}

// finishPending delivers a background evaluation's answer to everyone
// waiting for it, unless the request was cancelled or the collection has
// changed since it started. In that case the answer describes notes nothing
// is showing any more, and every waiting block has already asked again, so
// it is neither delivered nor cached.
func (t *Tools) finishPending(p *pendingRun, answer Answer) {
	t.mu.Lock()
	i := slices.Index(t.pending, p)
	if i < 0 {
		t.mu.Unlock()
		return
	}
	t.pending = slices.Delete(t.pending, i, i+1)
	current := p.generation == t.generation &&
		t.collection != nil &&
		p.revision == t.collection.Revision()
	var tokens []string
	if current {
		t.cacheResultLocked(p.body, p.revision, answer)
		tokens = p.tokens
	}
	t.mu.Unlock()
	for _, token := range tokens {
		t.deliver(token, answer)
	}
}

func (t *Tools) deliver(token string, answer Answer) {
	if t.OnResult != nil {
		t.OnResult(token, answer)
	}
}

func (t *Tools) revisionLocked() int {
	if t.collection == nil {
		return -1
	}
	return t.collection.Revision()
}

func (t *Tools) findLocked(body string, revision int) int {
	for i, entry := range t.cache {
		if entry.generation == t.generation && entry.revision == revision && entry.body == body {
			return i
		}
	}
	return -1
}

func (t *Tools) moveToFrontLocked(i int) {
	if i == 0 {
		return
	}
	entry := t.cache[i]
	copy(t.cache[1:i+1], t.cache[:i])
	t.cache[0] = entry
}

func (t *Tools) evaluateNowLocked(body string, revision int) Answer {
	t.evaluations++
	spec, err := Parse(body)
	if err != nil {
		return Answer{Error: err.Error()}
	}
	if t.collection == nil {
		return Answer{Error: NoCollectionError}
	}
	return evaluateSpec(spec, t.snapshotForLocked(revision))
}

// evaluateSpec is the whole evaluation, touching nothing in Tools, so it
// can run on a background goroutine.
func evaluateSpec(spec Spec, notes []Note) Answer {
	return Answer{OK: true, View: spec.View, Result: Evaluate(spec, notes)}
}

func (t *Tools) snapshotForLocked(revision int) []Note {
	if t.snapshotValid && t.snapshotRevision == revision && t.snapshotGeneration == t.generation {
		return t.snapshot
	}
	var notes []Note
	if t.collection != nil {
		notes = slices.Clone(t.collection.Notes())
		SortNotes(notes)
	}
	t.snapshot = notes
	t.snapshotRevision = revision
	t.snapshotGeneration = t.generation
	t.snapshotValid = true
	return notes
}

func (t *Tools) cacheResultLocked(body string, revision int, answer Answer) {
	entry := cacheEntry{
		generation: t.generation,
		revision:   revision,
		body:       body,
		answer:     answer,
		rows:       len(answer.Rows),
	}
	t.cache = slices.Insert(t.cache, 0, entry)
	t.pruneCacheLocked(revision)
}

// pruneCacheLocked drops every entry from another revision or collection,
// since those can never be served again, then trims the rest to the entry
// and row limits. The newest entry is kept whatever it costs: evicting the
// answer just asked for would mean never serving it.
func (t *Tools) pruneCacheLocked(revision int) {
	t.cache = slices.DeleteFunc(t.cache, func(e cacheEntry) bool {
		return e.generation != t.generation || e.revision != revision
	})
	rows := 0
	for i, entry := range t.cache {
		rows += entry.rows
		if i > 0 && (i >= maxCacheEntries || rows > maxCacheRows) {
			clear(t.cache[i:])
			t.cache = t.cache[:i]
			break
		}
	}
}
