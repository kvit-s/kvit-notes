package query

// A port of the suite tests/test_querydata.cpp, case for case: the spec
// grammar with its error cases, and evaluation (filters, typed sorting,
// grouping) against a small fixture collection. The suite writes the
// fixture notes to a temporary directory and indexes them with
// NoteCollection; here the fixture type below builds the same Note values
// from the same file texts, since this package does not read files.

import (
	"fmt"
	"path"
	"slices"
	"sort"
	"strings"
	"sync"
	"testing"
	"time"
)

// fixtureTime is the modification and creation time of every fixture note.
var fixtureTime = time.Date(2026, 9, 1, 12, 30, 0, 0, time.Local)

// noteFromFile builds the Note the collection would index for a file with
// this path and text (vaultscan.cpp entryFromText), for the simple front
// matter the fixtures use: "key: value" lines between "---" lines at the top.
func noteFromFile(relPath, content string) Note {
	note := Note{
		Path:     relPath,
		Title:    path.Base(relPath),
		Modified: fixtureTime,
		Created:  fixtureTime,
		Fields:   map[string]string{},
	}
	if strings.HasSuffix(strings.ToLower(note.Title), ".md") {
		note.Title = note.Title[:len(note.Title)-len(".md")]
	}
	if dir := path.Dir(relPath); dir != "." {
		note.Folder = dir
	}
	body := content
	if rest, ok := strings.CutPrefix(content, "---\n"); ok {
		if block, after, ok := strings.Cut(rest, "\n---\n"); ok {
			body = after
			for _, line := range strings.Split(block, "\n") {
				key, value, ok := strings.Cut(line, ":")
				if !ok {
					continue
				}
				value = strings.TrimSpace(value)
				note.Fields[key] = value
				if key == "tags" {
					if items, ok := parseInlineList(value); ok {
						note.Tags = items
					} else if value != "" {
						note.Tags = []string{stripMatchingQuotes(value)}
					}
				}
			}
		}
	}
	note.Words = len(strings.Fields(body))
	return note
}

// fixture stands in for the suite's temporary directory and the
// NoteCollection indexing it. writeNote writes a file; refresh indexes the
// files again and changes the revision, as NoteCollection::refresh and
// refreshPaths do; openRoot replaces the files and reports the new root to
// whoever set rootChanged, as NoteCollection::openRoot emits rootChanged.
type fixture struct {
	mu          sync.Mutex
	files       map[string]string
	notes       []Note
	revision    int
	rootChanged func()
}

func newFixture() *fixture {
	return &fixture{files: map[string]string{}}
}

func (f *fixture) writeNote(relPath, content string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.files[relPath] = content
}

func (f *fixture) refresh() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.notes = nil
	for relPath, content := range f.files {
		f.notes = append(f.notes, noteFromFile(relPath, content))
	}
	f.revision++
}

func (f *fixture) openRoot(files map[string]string) {
	f.mu.Lock()
	f.files = files
	f.mu.Unlock()
	f.refresh()
	if f.rootChanged != nil {
		f.rootChanged()
	}
}

func (f *fixture) Revision() int {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.revision
}

// Notes returns the notes in map order, not sorted, so that Tools' own
// sorting is exercised.
func (f *fixture) Notes() []Note {
	f.mu.Lock()
	defer f.mu.Unlock()
	return slices.Clone(f.notes)
}

// snapshot is QueryData::snapshotOf: the notes sorted by path.
func (f *fixture) snapshot() []Note {
	notes := f.Notes()
	SortNotes(notes)
	return notes
}

// makeFixture writes the suite's fixture: four project notes with mixed
// front matter plus one note without any.
func makeFixture(t *testing.T) *fixture {
	t.Helper()
	f := newFixture()
	f.writeNote("projects/Alpha.md",
		"---\nstatus: active\npriority: 2\ndue: 2026-08-01\n"+
			"tags: [work]\n---\nAlpha body\n")
	f.writeNote("projects/Beta.md",
		"---\nstatus: active\npriority: 10\ndue: 2026-07-15\n---\n"+
			"Beta body\n")
	f.writeNote("projects/Gamma.md",
		"---\nstatus: done\npriority: 2\n---\nGamma body\n")
	f.writeNote("notes/Delta.md",
		"---\nstatus: active\n---\nDelta body\n")
	f.writeNote("Plain.md", "No front-matter here\n")
	f.refresh()
	return f
}

func titlesOf(result Result) []string {
	titles := []string{}
	for _, row := range result.Rows {
		title := ""
		if len(row.Cells) > 0 {
			title = row.Cells[0]
		}
		titles = append(titles, title)
	}
	return titles
}

// run parses body, which must parse, and evaluates it against the fixture.
func run(t *testing.T, f *fixture, body string) Result {
	t.Helper()
	spec, err := Parse(body)
	if err != nil {
		t.Fatalf("parse error: %v", err)
	}
	return Evaluate(spec, f.snapshot())
}

func TestParseFullSpec(t *testing.T) {
	spec, err := Parse(
		"from: projects/\n" +
			"where: status = active, priority > 1\n" +
			"where: due exists\n" +
			"view: board\n" +
			"columns: title, status, due\n" +
			"group-by: status\n" +
			"sort: due asc, priority desc\n" +
			"limit: 20\n" +
			"# a comment line is structure, not spec\n" +
			"\n")
	if err != nil {
		t.Fatal(err)
	}
	if spec.From != "projects" {
		t.Errorf("From = %q, want projects", spec.From)
	}
	if len(spec.Where) != 3 {
		t.Fatalf("len(Where) = %d, want 3", len(spec.Where))
	}
	if w := spec.Where[0]; w.Field != "status" || w.Op != OpEq || w.Value != "active" {
		t.Errorf("Where[0] = %+v", w)
	}
	if spec.Where[1].Op != OpGt {
		t.Errorf("Where[1].Op = %v, want OpGt", spec.Where[1].Op)
	}
	if spec.Where[2].Op != OpExists {
		t.Errorf("Where[2].Op = %v, want OpExists", spec.Where[2].Op)
	}
	if spec.View != ViewBoard {
		t.Errorf("View = %v, want board", spec.View)
	}
	if want := []string{"title", "status", "due"}; !slices.Equal(spec.Columns, want) {
		t.Errorf("Columns = %q, want %q", spec.Columns, want)
	}
	if spec.GroupBy != "status" {
		t.Errorf("GroupBy = %q, want status", spec.GroupBy)
	}
	if len(spec.Sort) != 2 {
		t.Fatalf("len(Sort) = %d, want 2", len(spec.Sort))
	}
	if !spec.Sort[0].Ascending {
		t.Error("Sort[0] is descending, want ascending")
	}
	if spec.Sort[1].Ascending {
		t.Error("Sort[1] is ascending, want descending")
	}
	if spec.Limit != 20 {
		t.Errorf("Limit = %d, want 20", spec.Limit)
	}
}

func TestParseDefaults(t *testing.T) {
	spec, err := Parse("")
	if err != nil {
		t.Fatal(err)
	}
	if spec.View != ViewTable {
		t.Errorf("View = %v, want table", spec.View)
	}
	if len(spec.Where) != 0 {
		t.Errorf("Where = %+v, want none", spec.Where)
	}
	if spec.Limit != 0 {
		t.Errorf("Limit = %d, want 0", spec.Limit)
	}
}

func TestParseErrors(t *testing.T) {
	cases := []struct{ name, body string }{
		{"unknown-key", "sortby: x"},
		{"not-a-mapping", "just words"},
		{"bad-view", "view: cards"},
		{"bad-condition", "where: status active"},
		{"missing-value", "where: status ="},
		{"empty-where", "where: ,"},
		{"bad-sort-dir", "sort: due sideways"},
		{"bad-limit", "limit: many"},
		{"zero-limit", "limit: 0"},
		{"board-without-group", "view: board"},
		{"group-on-table", "view: table\ngroup-by: status"},
		{"group-two-words", "group-by: two words"},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			_, err := Parse(c.body)
			if err == nil {
				t.Fatal("parsed without an error")
			}
			if err.Error() == "" {
				t.Fatal("error message is empty")
			}
		})
	}
}

func TestGroupByImpliesBoard(t *testing.T) {
	spec, err := Parse("group-by: status")
	if err != nil {
		t.Fatal(err)
	}
	if spec.View != ViewBoard {
		t.Errorf("View = %v, want board", spec.View)
	}
}

func TestOperators(t *testing.T) {
	cases := []struct {
		name, where string
		expected    []string
	}{
		{"eq", "where: status = active", []string{"Alpha", "Beta", "Delta"}},
		{"ne", "from: projects\nwhere: status != active", []string{"Gamma"}},
		// 10 > 2 numerically, although "10" < "2" as strings.
		{"gt-numeric", "where: priority > 2", []string{"Beta"}},
		{"ge", "where: priority >= 2", []string{"Alpha", "Beta", "Gamma"}},
		{"lt-date", "where: due < 2026-07-20", []string{"Beta"}},
		{"le-date", "where: due <= 2026-08-01", []string{"Alpha", "Beta"}},
		{"contains", "where: title contains lph", []string{"Alpha"}},
		{"has", "where: tags has work", []string{"Alpha"}},
		{"exists", "where: due exists", []string{"Alpha", "Beta"}},
		{"and", "where: status = active\nwhere: priority = 2", []string{"Alpha"}},
		{"case-insensitive-eq", "where: status = ACTIVE", []string{"Alpha", "Beta", "Delta"}},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			f := makeFixture(t)
			result := run(t, f, c.where+"\ncolumns: title\nsort: title asc")
			if got := titlesOf(result); !slices.Equal(got, c.expected) {
				t.Errorf("titles = %q, want %q", got, c.expected)
			}
		})
	}
}

func TestFolderScoping(t *testing.T) {
	f := makeFixture(t)
	result := run(t, f, "from: projects\ncolumns: title\nsort: title asc")
	if got, want := titlesOf(result), []string{"Alpha", "Beta", "Gamma"}; !slices.Equal(got, want) {
		t.Errorf("titles = %q, want %q", got, want)
	}

	// Nested folders are in scope; unrelated prefixes are not.
	f.writeNote("projects/sub/Nested.md", "---\nstatus: active\n---\nx\n")
	f.writeNote("projectsish/Other.md", "---\nstatus: active\n---\nx\n")
	f.refresh()
	result = run(t, f, "from: projects\ncolumns: title\nsort: title asc")
	if got, want := titlesOf(result), []string{"Alpha", "Beta", "Gamma", "Nested"}; !slices.Equal(got, want) {
		t.Errorf("titles = %q, want %q", got, want)
	}
}

func TestSortTypedAndStable(t *testing.T) {
	f := makeFixture(t)

	// Numeric: 2 < 10 (string order would be "10" < "2").
	result := run(t, f, "from: projects\ncolumns: title\n"+
		"sort: priority desc, title asc")
	if got, want := titlesOf(result), []string{"Beta", "Alpha", "Gamma"}; !slices.Equal(got, want) {
		t.Errorf("titles = %q, want %q", got, want)
	}

	// Dates sort as dates; notes without the field group deterministically.
	result = run(t, f, "from: projects\nwhere: due exists\ncolumns: title\n"+
		"sort: due asc")
	if got, want := titlesOf(result), []string{"Beta", "Alpha"}; !slices.Equal(got, want) {
		t.Errorf("titles = %q, want %q", got, want)
	}
}

func TestLimit(t *testing.T) {
	f := makeFixture(t)
	result := run(t, f, "columns: title\nsort: title asc\nlimit: 2")
	if got, want := titlesOf(result), []string{"Alpha", "Beta"}; !slices.Equal(got, want) {
		t.Errorf("titles = %q, want %q", got, want)
	}
}

func TestBoardGroups(t *testing.T) {
	f := makeFixture(t)
	result := run(t, f, "from: projects\ngroup-by: status\n"+
		"columns: title\nsort: title asc")
	if len(result.Groups) != 2 {
		t.Fatalf("len(Groups) = %d, want 2", len(result.Groups))
	}
	if g := result.Groups[0]; g.Name != "active" || len(g.Rows) != 2 {
		t.Errorf("Groups[0] = %q with %d rows, want active with 2", g.Name, len(g.Rows))
	}
	if g := result.Groups[1]; g.Name != "done" || len(g.Rows) != 1 {
		t.Errorf("Groups[1] = %q with %d rows, want done with 1", g.Name, len(g.Rows))
	}

	// Notes without the group key go in the trailing "(none)" group.
	all := run(t, f, "group-by: status\ncolumns: title\nsort: title asc")
	if len(all.Groups) == 0 {
		t.Fatal("no groups")
	}
	last := all.Groups[len(all.Groups)-1]
	if last.Name != "(none)" {
		t.Errorf("last group = %q, want (none)", last.Name)
	}
	if len(last.Rows) != 1 { // Plain.md
		t.Errorf("last group has %d rows, want 1", len(last.Rows))
	}
}

func TestPseudoFields(t *testing.T) {
	f := makeFixture(t)
	// The built-in properties resolve from the note itself.
	result := run(t, f, "where: path contains projects/A\n"+
		"columns: title, folder, words, tags, path")
	if len(result.Rows) != 1 {
		t.Fatalf("len(Rows) = %d, want 1", len(result.Rows))
	}
	want := []string{"Alpha", "projects", "2", "work", "projects/Alpha.md"} // "Alpha body" is 2 words
	if got := result.Rows[0].Cells; !slices.Equal(got, want) {
		t.Errorf("cells = %q, want %q", got, want)
	}

	// Defaulted columns.
	defaulted := run(t, f, "where: title = Alpha")
	if want := []string{"title", "modified"}; !slices.Equal(defaulted.Columns, want) {
		t.Errorf("Columns = %q, want %q", defaulted.Columns, want)
	}
}

func TestQueryToolsCache(t *testing.T) {
	f := makeFixture(t)
	tools := &Tools{}
	tools.SetCollection(f)
	f.rootChanged = tools.RootChanged
	tools.ClearCache()

	const spec = "where: status = active\ncolumns: title\nsort: title asc"
	if !tools.Run(spec).OK || !tools.Run(spec).OK {
		t.Fatal("query failed")
	}
	if n := tools.EvaluationCount(); n != 1 { // identical blocks share the entry
		t.Errorf("EvaluationCount = %d, want 1", n)
	}

	if tools.Run("unknown: key").OK || tools.Run("unknown: key").OK {
		t.Fatal("unknown key parsed")
	}
	if n := tools.EvaluationCount(); n != 2 { // parse errors are cached too
		t.Errorf("EvaluationCount = %d, want 2", n)
	}

	f.writeNote("projects/New.md", "---\nstatus: active\n---\nnew\n")
	f.refresh()
	if n := len(tools.Run(spec).Rows); n != 4 {
		t.Errorf("rows = %d, want 4", n)
	}
	if n := tools.EvaluationCount(); n != 3 { // the revision is part of the key
		t.Errorf("EvaluationCount = %d, want 3", n)
	}

	f.openRoot(map[string]string{"Only.md": "---\nstatus: active\n---\nonly\n"})
	if n := tools.CacheSize(); n != 0 { // a new root starts a new generation
		t.Errorf("CacheSize = %d, want 0", n)
	}
	if n := len(tools.Run(spec).Rows); n != 1 {
		t.Errorf("rows = %d, want 1", n)
	}
	if n := tools.EvaluationCount(); n != 4 {
		t.Errorf("EvaluationCount = %d, want 4", n)
	}
}

// eventLoop stands in for the event loop the suite runs with
// QTRY_COMPARE and test wait: Tools.Post queues functions here, and they
// run only when the test processes them, on the test's goroutine.
type eventLoop struct {
	posted chan func()
}

func newEventLoop() *eventLoop {
	return &eventLoop{posted: make(chan func(), 64)}
}

func (l *eventLoop) post(fn func()) {
	l.posted <- fn
}

// processUntil runs posted functions until done reports true or timeout
// passes, and returns done's final answer (QTRY_COMPARE_WITH_TIMEOUT).
func (l *eventLoop) processUntil(timeout time.Duration, done func() bool) bool {
	deadline := time.After(timeout)
	for !done() {
		select {
		case fn := <-l.posted:
			fn()
		case <-deadline:
			return done()
		}
	}
	return true
}

// wait runs posted functions for d (test wait).
func (l *eventLoop) wait(d time.Duration) {
	deadline := time.After(d)
	for {
		select {
		case fn := <-l.posted:
			fn()
		case <-deadline:
			return
		}
	}
}

type delivery struct {
	token  string
	answer Answer
}

// A query reads every note, sorts the matches and builds the rows, so it
// runs on a background goroutine and answers through OnResult. Three
// properties matter beyond the right rows coming back: the call returns
// before the work is done, several blocks asking the same question at the
// same revision cost one evaluation rather than one each, and a result whose
// revision has been superseded is dropped instead of being shown.
func TestQueryToolsRunsOffTheCallingThread(t *testing.T) {
	f := makeFixture(t)
	loop := newEventLoop()
	var delivered []delivery
	tools := &Tools{
		Post: loop.post,
		OnResult: func(token string, answer Answer) {
			delivered = append(delivered, delivery{token, answer})
		},
	}
	tools.SetCollection(f)
	tools.ClearCache()

	const spec = "where: status = active\ncolumns: title\nsort: title asc"

	// Three blocks, one question, one revision. The answer arrives for all
	// three and the notes are read once.
	tools.RequestRun("a", spec)
	tools.RequestRun("b", spec)
	tools.RequestRun("c", spec)
	if len(delivered) != 0 {
		t.Fatal("RequestRun answered before returning, so it evaluated on the calling goroutine")
	}

	if !loop.processUntil(5*time.Second, func() bool { return len(delivered) == 3 }) {
		t.Fatalf("%d answers delivered, want 3", len(delivered))
	}
	if n := tools.EvaluationCount(); n != 1 {
		t.Errorf("EvaluationCount = %d, want 1", n)
	}
	var tokens []string
	for _, d := range delivered {
		tokens = append(tokens, d.token)
		if !d.answer.OK {
			t.Errorf("answer for %s is not OK: %s", d.token, d.answer.Error)
		}
		if n := len(d.answer.Rows); n != 3 {
			t.Errorf("answer for %s has %d rows, want 3", d.token, n)
		}
	}
	sort.Strings(tokens)
	if want := []string{"a", "b", "c"}; !slices.Equal(tokens, want) {
		t.Errorf("tokens = %q, want %q", tokens, want)
	}

	// Cached for that revision, so a later request is answered at once.
	delivered = nil
	tools.RequestRun("d", spec)
	if len(delivered) != 1 {
		t.Errorf("%d answers delivered at once, want 1", len(delivered))
	}
	if n := tools.EvaluationCount(); n != 1 {
		t.Errorf("EvaluationCount = %d, want 1", n)
	}

	// A parse error needs no evaluation and no goroutine.
	delivered = nil
	tools.RequestRun("e", "unknown: key")
	if len(delivered) != 1 {
		t.Fatalf("%d answers delivered at once, want 1", len(delivered))
	}
	if delivered[0].answer.OK {
		t.Error("unknown key parsed")
	}

	// A result whose revision has been superseded is not delivered. The
	// request below is made, then the collection changes underneath it;
	// whatever the worker produced describes a state nothing is showing.
	delivered = nil
	f.writeNote("projects/New.md", "---\nstatus: active\n---\nnew\n")
	f.refresh()
	tools.RequestRun("f", spec)
	f.writeNote("projects/Newer.md", "---\nstatus: active\n---\nnewer\n")
	f.refresh()
	loop.wait(200 * time.Millisecond)
	for _, d := range delivered {
		if n := len(d.answer.Rows); n != 5 {
			t.Errorf("answer for %s has %d rows, want 5", d.token, n)
		}
	}

	// Asking again against the current revision does answer, with the row
	// the superseded run would have missed.
	delivered = nil
	tools.RequestRun("g", spec)
	if !loop.processUntil(5*time.Second, func() bool { return len(delivered) > 0 }) {
		t.Fatal("no answer delivered")
	}
	if n := len(delivered[0].answer.Rows); n != 5 {
		t.Errorf("rows = %d, want 5", n)
	}
}

// The suite budgets this evaluation in process CPU time: a median under
// 20 ms, and a ceiling of 55 ms that no sample may reach, enforced in release
// builds. This port times each sample on the wall clock with the same two
// limits and skips in -short mode, where the suite skips in debug builds.
func TestEvaluate1000NoteBudget(t *testing.T) {
	if testing.Short() {
		t.Skip("the 1000-note timing budget is not checked in -short mode")
	}
	f := newFixture()
	for i := 0; i < 1000; i++ {
		status := "done"
		if i%3 == 0 {
			status = "active"
		}
		f.writeNote(fmt.Sprintf("notes/%04d.md", i),
			fmt.Sprintf("---\nstatus: %s\npriority: %d\ndue: 2026-%02d-%02d\n---\nbody words here\n",
				status, i%17, i%12+1, i%28+1))
	}
	f.refresh() // indexing is not timed
	notes := f.snapshot()
	spec, err := Parse(
		"from: notes\nwhere: status = active\nwhere: priority >= 3\n" +
			"columns: title, status, priority, due, folder\n" +
			"sort: due asc, priority desc, title asc\nlimit: 250")
	if err != nil {
		t.Fatal(err)
	}

	Evaluate(spec, notes) // warm caches and the allocator
	samples := make([]float64, 0, 9)
	for i := 0; i < 9; i++ {
		start := time.Now()
		result := Evaluate(spec, notes)
		samples = append(samples, float64(time.Since(start))/float64(time.Millisecond))
		if len(result.Rows) == 0 {
			t.Fatal("no rows")
		}
	}
	slices.Sort(samples)
	median, maximum := samples[len(samples)/2], samples[len(samples)-1]
	t.Logf("QUERY 1000: median %.3f ms, max %.3f ms", median, maximum)

	const medianBudgetMs, ceilingMs = 20.0, 55.0
	if maximum >= ceilingMs {
		t.Errorf("query 1000-note evaluate exceeded its ceiling: %.1f ms against %.1f ms", maximum, ceilingMs)
	}
	if median >= medianBudgetMs {
		t.Errorf("query 1000-note evaluate exceeded its budget: median %.1f ms against %.1f ms", median, medianBudgetMs)
	}
}
