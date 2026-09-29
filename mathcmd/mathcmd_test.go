package mathcmd

// These tests are the app's tests/test_mathcommandmodel.cpp, one Go test
// per test function there and in the same order, with the same inputs and
// expected outputs. The model reads the engine's commands from MicroTeX;
// here they are engineCommands, a fixed list holding the commands the tests
// rely on (\cfrac, \cdotB, \cdotBB, \vv) and some the hand-picked list also
// names, so the tests show those are not listed twice. The two  tests
// that render every entry through the engine, testCatalogPreviewsRender and
// testCatalogTemplatesRender, need the math engine and are skipped here; the
// entries' TeX has to be checked where mathtex is (see renderChecks). The
// test's recentChanged signal is OnRecentChanged.

import (
	"fmt"
	"slices"
	"strings"
	"testing"
	"unicode"
)

// engineCommands stands in for MicroTeX's command list.
var engineCommands = []string{
	"alpha", "beta", "Omega", "omega", "cdot", "cdots", "cdotB", "cdotBB", "cfrac",
	"frac", "sqrt", "in", "int", "iint", "sin", "vv", "mathbb", "left", "right",
}

func newModel() *Model { return New(func() []string { return engineCommands }) }

func names(rows []Entry) []string {
	var out []string
	for _, r := range rows {
		out = append(out, r.Name)
	}
	return out
}

// filled is a template with its empty slots filled so it parses: {} -> {x},
// [] -> [n].
func filled(insert string) string {
	return strings.ReplaceAll(strings.ReplaceAll(insert, "{}", "{x}"), "[]", "[n]")
}

func allCuratedEntries(m *Model) []Entry {
	var out []Entry
	for _, c := range m.Categories() {
		if c == RecentlyUsed {
			continue
		}
		out = append(out, m.ItemsForCategory(c)...)
	}
	return out
}

// renderChecks are the TeX strings the two skipped  tests render through
// the engine: every entry's preview, and every standalone entry's templates
// with their slots filled. A test beside the math engine can check them all.
func renderChecks(m *Model) []string {
	var out []string
	for _, e := range allCuratedEntries(m) {
		if e.Preview != "" {
			out = append(out, e.Preview)
		}
		if !e.Standalone {
			continue
		}
		for _, t := range []string{e.Insert, e.InsertDisplay} {
			if t != "" {
				out = append(out, filled(t))
			}
		}
	}
	return out
}

func TestCategoriesCanonical(t *testing.T) {
	m := newModel()
	cats := m.Categories()
	// No recency yet: the list as ordered, Greek first (LyX's toolbar
	// order), and no "Recently used".
	if slices.Contains(cats, RecentlyUsed) {
		t.Error(`"Recently used" before anything was used`)
	}
	if cats[0] != "Greek" {
		t.Errorf("first category %q", cats[0])
	}
	for _, c := range []string{"Arrows", "Big operators", "Fractions & roots", "Structure", "Spacing"} {
		if !slices.Contains(cats, c) {
			t.Errorf("no %q", c)
		}
	}
	// Every entry's category is a listed category.
	for _, e := range allCuratedEntries(m) {
		if !slices.Contains(cats, e.Category) {
			t.Errorf("%s: category %q", e.Name, e.Category)
		}
	}
}

func TestRecentlyUsedLeadsCategories(t *testing.T) {
	m := newModel()
	m.NoteUsed(`\frac`)
	if cats := m.Categories(); cats[0] != RecentlyUsed {
		t.Errorf("first category %q", cats[0])
	}
	recent := m.ItemsForCategory(RecentlyUsed)
	if len(recent) != 1 || recent[0].Name != `\frac` {
		t.Errorf("recent: %q", names(recent))
	}
}

func TestCategoryEntriesCarryTemplates(t *testing.T) {
	rows := newModel().ItemsForCategory("Fractions & roots")
	if len(rows) == 0 {
		t.Fatal("no entries")
	}
	var frac Entry
	for _, r := range rows {
		if r.Name == `\frac` {
			frac = r
		}
	}
	if frac.Insert != `\frac{}{}` {
		t.Errorf("insert %q", frac.Insert)
	}
	if frac.CursorOffset != 6 { // inside the first {}
		t.Errorf("cursor offset %d", frac.CursorOffset)
	}
	if frac.Preview != `\frac{a}{b}` {
		t.Errorf("preview %q", frac.Preview)
	}
	if !frac.Curated {
		t.Error("not curated")
	}
}

func TestCatalogPreviewsRender(t *testing.T) {
	t.Skipf("needs the math engine to render the %d TeX strings of renderChecks", len(renderChecks(newModel())))
}

func TestCatalogTemplatesRender(t *testing.T) {
	t.Skip("needs the math engine; its strings are in renderChecks with the previews")
}

func TestCursorOffsets(t *testing.T) {
	for _, e := range allCuratedEntries(newModel()) {
		insert := []rune(e.Insert)
		off := e.CursorOffset
		if off < -1 || off > len(insert) {
			t.Errorf("%s: offset %d", e.Name, off)
			continue
		}
		// A template with an empty slot puts the caret inside one.
		if strings.Contains(e.Insert, "{}") || strings.Contains(e.Insert, "[]") {
			if off <= 0 {
				t.Errorf("%s: offset %d", e.Insert, off)
				continue
			}
			before, after := insert[off-1], insert[off]
			if !(before == '{' && after == '}') && !(before == '[' && after == ']') {
				t.Errorf("%s: the caret is between %q and %q", e.Insert, before, after)
			}
		}
	}
}

func TestPrefixBeatsSubstringBeatsSubsequence(t *testing.T) {
	// "in": \in (a prefix) comes before \sin (inside the word, hand-picked)
	// and before any match of scattered letters such as \iint.
	got := names(newModel().ItemsFor("in"))
	if !slices.Contains(got, `\in`) || !slices.Contains(got, `\sin`) {
		t.Fatalf("results: %q", got)
	}
	if slices.Index(got, `\in`) > slices.Index(got, `\sin`) {
		t.Errorf(`\in after \sin: %q`, got)
	}
	// \int is a prefix match too and comes before the others as well.
	if slices.Index(got, `\int`) > slices.Index(got, `\sin`) {
		t.Errorf(`\int after \sin: %q`, got)
	}
}

func TestCaseExactBeatsCaseInsensitive(t *testing.T) {
	// TeX tells cases apart: \omega and \Omega both come for either query,
	// the one in the query's case first.
	m := newModel()
	lower := names(m.ItemsFor("ome"))
	if !slices.Contains(lower, `\omega`) || !slices.Contains(lower, `\Omega`) ||
		slices.Index(lower, `\omega`) > slices.Index(lower, `\Omega`) {
		t.Errorf("ome: %q", lower)
	}
	upper := names(m.ItemsFor("Ome"))
	if !slices.Contains(upper, `\omega`) || !slices.Contains(upper, `\Omega`) ||
		slices.Index(upper, `\Omega`) > slices.Index(upper, `\omega`) {
		t.Errorf("Ome: %q", upper)
	}
}

func TestCuratedRanksAboveEnumerated(t *testing.T) {
	// "frac" matches the hand-picked \frac and engine commands such as
	// \cfrac; the hand-picked entry leads.
	rows := newModel().ItemsFor("frac")
	if len(rows) == 0 || rows[0].Name != `\frac` || !rows[0].Curated {
		t.Errorf("rows: %q", names(rows))
	}
	if !slices.Contains(names(rows), `\cfrac`) {
		t.Errorf(`the engine's \cfrac is missing: %q`, names(rows))
	}
}

func TestCdotCompletesExactly(t *testing.T) {
	// \cdot has longer engine commands beside it, \cdotB and \cdotBB. The
	// ordinary binary operator still leads.
	rows := newModel().ItemsFor("cdot")
	if len(rows) == 0 || rows[0].Name != `\cdot` || rows[0].Insert != `\cdot` {
		t.Errorf("rows: %q", names(rows))
	}
}

func TestEveryCuratedEntryCompletes(t *testing.T) {
	m := newModel()
	fragmentQueries := map[string]string{"^{}": "sup", "_{}": "sub", "_{}^{}": "subsup", "&": "cell"}
	var failures []string
	for _, e := range allCuratedEntries(m) {
		name := []rune(e.Name)
		var query string
		switch {
		case name[0] != '\\':
			query = fragmentQueries[e.Name]
			if query == "" {
				failures = append(failures, e.Name+" has no query to try")
				continue
			}
		case len(name) >= 2 && unicode.IsLetter(name[1]):
			end := 2
			for end < len(name) && unicode.IsLetter(name[end]) {
				end++
			}
			query = string(name[1:end])
		case len(name) >= 2:
			// A TeX control symbol is one character; \\ is the menu's
			// query of one backslash.
			query = string(name[1])
		default:
			failures = append(failures, e.Name+" has no query to try")
			continue
		}
		if !slices.Contains(names(m.ItemsFor(query)), e.Name) {
			failures = append(failures, fmt.Sprintf("%s via %q", e.Name, query))
		}
	}
	if len(failures) > 0 {
		t.Error(strings.Join(failures, ", "))
	}
}

func TestEnumeratedCommandsComplete(t *testing.T) {
	// \vv, from the NewTX additions, is not in the hand-picked list; the
	// engine's list brings it.
	if got := names(newModel().ItemsFor("vv")); !slices.Contains(got, `\vv`) {
		t.Errorf("vv: %q", got)
	}
}

func TestAliasesMatch(t *testing.T) {
	m := newModel()
	for q, want := range map[string]string{
		"choose": `\binom`, "root": `\sqrt`, "iff": `\Leftrightarrow`, "infinity": `\infty`,
	} {
		if got := names(m.ItemsFor(q)); !slices.Contains(got, want) {
			t.Errorf("%q: %q has no %s", q, got, want)
		}
	}
}

func TestDoubleBackslashMatches(t *testing.T) {
	// A second backslash straight after the one that opened the menu
	// queries `\`: the row break leads, so \\ goes in through the menu
	// inside a matrix.
	got := names(newModel().ItemsFor(`\`))
	if len(got) == 0 || got[0] != `\\` {
		t.Errorf("results: %q", got)
	}
}

func TestNoMatchYieldsEmpty(t *testing.T) {
	if got := newModel().ItemsFor("zzqqxy"); len(got) != 0 {
		t.Errorf("results: %q", names(got))
	}
}

func TestRecencyReordersAndCaps(t *testing.T) {
	m := newModel()
	for i := 0; i < MaxRecent+3; i++ {
		m.NoteUsed(fmt.Sprintf(`\cmd%d`, i))
	}
	if n := len(m.RecentCommands()); n != MaxRecent {
		t.Errorf("%d recent", n)
	}
	// Using one again moves it to the front without repeating it.
	m.NoteUsed(`\cmd5`)
	recent := m.RecentCommands()
	if len(recent) != MaxRecent || recent[0] != `\cmd5` {
		t.Errorf("recent: %q", recent)
	}
}

func TestRecentRoundTrip(t *testing.T) {
	m := newModel()
	m.NoteUsed(`\alpha`)
	m.NoteUsed(`\frac`)
	saved := m.RecentCommands()

	restored := newModel()
	restored.SetRecentCommands(saved)
	if got := restored.RecentCommands(); !slices.Equal(got, saved) {
		t.Errorf("restored %q, saved %q", got, saved)
	}
	// Restored, they list as entries with their templates.
	rows := restored.ItemsForCategory(RecentlyUsed)
	if len(rows) != 2 || rows[0].Name != `\frac` || rows[0].Insert != `\frac{}{}` {
		t.Errorf("rows: %+v", rows)
	}
}

func TestSetRecentDoesNotSignal(t *testing.T) {
	m := newModel()
	calls := 0
	m.OnRecentChanged = func() { calls++ }
	m.SetRecentCommands([]string{`\frac`})
	if calls != 0 { // loading saved state must not save it again
		t.Errorf("%d calls after loading", calls)
	}
	m.NoteUsed(`\frac`)
	if calls != 1 {
		t.Errorf("%d calls after one use", calls)
	}
}
