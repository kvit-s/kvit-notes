package links

import (
	"slices"
	"testing"
)

// fixture is the vault of TestNoteCollection::makeFixture.
var fixture = []string{"Welcome.md", "Ideas/Reading.md", "Ideas/Plans.md", "Ideas/Projects/Kvit.md"}

func TestNormalizeTarget(t *testing.T) {
	cases := map[string]string{
		"Kvit":                   "kvit",
		" Kvit.MD ":              "kvit",
		"/Ideas/Kvit.md#Goals":   "ideas/kvit",
		"//Kvit # Goals":         "kvit",
		"Notes.md.md":            "notes.md",
		"#Goals":                 "",
		"\u0130stanbul":          "i\u0307stanbul",
		"Projects/Kvit|not here": "projects/kvit|not here",
	}
	for in, want := range cases {
		if got := NormalizeTarget(in); got != want {
			t.Errorf("NormalizeTarget(%q) = %q, want %q", in, got, want)
		}
	}
}

func TestPathMatchesTarget(t *testing.T) {
	cases := []struct {
		path, target string
		want         bool
	}{
		{"Ideas/Projects/Kvit.md", "kvit", true},
		{"Ideas/Projects/Kvit.md", "projects/kvit", true},
		{"Ideas/Projects/Kvit.md", "ideas/projects/kvit", true},
		{"Ideas/Projects/Kvit.md", "jects/kvit", false},
		{"Ideas/Projects/Kvit.md", "other/kvit", false},
		{"Kvit.MD", "kvit", true},
	}
	for _, c := range cases {
		if got := PathMatchesTarget(c.path, c.target); got != c.want {
			t.Errorf("PathMatchesTarget(%q, %q) = %v", c.path, c.target, got)
		}
	}
}

// From TestNoteCollection::testResolveWikiTarget.
func TestResolveAsTheQtAppDoes(t *testing.T) {
	ix := NewIndex(fixture)
	cases := map[string]string{
		// Bare basename, any case, ".md" implied.
		"Kvit":    "Ideas/Projects/Kvit.md",
		"kvit":    "Ideas/Projects/Kvit.md",
		"Kvit.md": "Ideas/Projects/Kvit.md",
		"Welcome": "Welcome.md",
		// More of the path, and a heading, which resolution ignores.
		"Projects/Kvit": "Ideas/Projects/Kvit.md",
		"Kvit#Heading":  "Ideas/Projects/Kvit.md",
		"/Welcome":      "Welcome.md",
		// No match.
		"Nope":       "",
		"Other/Kvit": "",
		"#Heading":   "",
	}
	for target, want := range cases {
		if got := ix.Resolve(target); got != want {
			t.Errorf("Resolve(%q) = %q, want %q", target, got, want)
		}
	}

	// Two notes of one name are ambiguous: neither is picked, and more of
	// the path chooses.
	ix.Add("Archive/Welcome.md")
	if got := ix.Resolve("Welcome"); got != "" {
		t.Errorf("an ambiguous target resolved to %q", got)
	}
	r := ix.Resolution("WELCOME.md", true)
	if r.Status != Ambiguous || !slices.Equal(r.Candidates, []string{"Archive/Welcome.md", "Welcome.md"}) {
		t.Errorf("ambiguous resolution: %+v", r)
	}
	if got := ix.Resolve("Archive/Welcome"); got != "Archive/Welcome.md" {
		t.Errorf("Archive/Welcome resolved to %q", got)
	}
}

// Candidates are listed without regard to case, as QStringList::sort with
// Qt::CaseInsensitive lists them.
func TestAmbiguousCandidatesAreSortedWithoutCase(t *testing.T) {
	ix := NewIndex([]string{"b/Welcome.md", "a/Welcome.md", "Welcome.md", "A/welcome.md"})
	r := ix.Resolution("welcome", true)
	want := []string{"A/welcome.md", "a/Welcome.md", "b/Welcome.md", "Welcome.md"}
	if r.Status != Ambiguous || !slices.Equal(r.Candidates, want) {
		t.Errorf("got %+v, want %q", r, want)
	}
}

// From TestNoteCollection::testWikiLinksReindexOnRefresh and
// testMoveKeepsBareLinksUntouched: a note appearing or moving changes what
// a target resolves to, and a bare name follows a moved note.
func TestIndexFollowsNotesAppearingAndMoving(t *testing.T) {
	ix := NewIndex(fixture)
	if got := ix.Resolve("Fresh"); got != "" {
		t.Fatalf("Fresh resolved to %q before it existed", got)
	}
	ix.Add("Fresh.md")
	if got := ix.Resolve("Fresh"); got != "Fresh.md" {
		t.Errorf("Fresh resolved to %q", got)
	}
	ix.Remove("Fresh.md")
	ix.Add("Sub/Fresh.md")
	for _, target := range []string{"fresh", "/Fresh"} {
		if got := ix.Resolve(target); got != "Sub/Fresh.md" {
			t.Errorf("after the move %q resolved to %q", target, got)
		}
	}
	if ix.NeedsRewrite("A bare [[fresh]] link") {
		t.Error("a moved note's bare links need no rewrite")
	}
}

// From TestReservedSubtrees::aBareNameNeverResolvesIntoTheRealm.
func TestABareNameNeverResolvesIntoARealm(t *testing.T) {
	ix := NewIndex([]string{"Ideas/Report.md", "Journal.md"})
	ix.AddRealm(".reports/monday/report.md")
	ix.AddRealm(".reports/tuesday/report.md")
	cases := map[string]string{
		"report":                 "Ideas/Report.md",
		".reports/monday/report": ".reports/monday/report.md",
		"tuesday/report":         ".reports/tuesday/report.md",
	}
	for target, want := range cases {
		if got := ix.Resolve(target); got != want {
			t.Errorf("Resolve(%q) = %q, want %q", target, got, want)
		}
	}
	ix.Remove("Ideas/Report.md")
	if got := ix.Resolve("report"); got != "" {
		t.Errorf("a bare name fell into the realm: %q", got)
	}
}

// What [[ completion inserts (qml/WikiLinkMenu.qml).
func TestCompletionTargetIsTheTitleWhenThatResolves(t *testing.T) {
	ix := NewIndex(append([]string{"Archive/Welcome.md"}, fixture...))
	cases := []struct{ path, title, want string }{
		{"Ideas/Projects/Kvit.md", "Kvit", "Kvit"},
		{"Archive/Welcome.md", "Welcome", "Archive/Welcome"},
		// A note at the top of the vault gets its path, which is its bare
		// name, even when that name is ambiguous.
		{"Welcome.md", "Welcome", "Welcome"},
	}
	for _, c := range cases {
		if got := ix.CompletionTarget(c.path, c.title); got != c.want {
			t.Errorf("CompletionTarget(%q) = %q, want %q", c.path, got, c.want)
		}
	}
}

// Following a link to no note creates it: from
// TestNoteSession::aDanglingWikiLinkIsCreatedAndReportedWithNoWindowToReportTo
// and tst_integration's test_wiki1_followOpensAndCreates and
// test_wiki1b_requestOpenLinkKeepsSpaces, with the path-qualified cases of
// createWikiTarget in qml/NoteSession.qml.
func TestNewNoteFor(t *testing.T) {
	cases := []struct {
		target, current string
		want            NewNote
		ok              bool
	}{
		{"A brand new note", "one.md", NewNote{"", "A brand new note", "A brand new note.md"}, true},
		{"Fresh idea", "Ideas/Projects/Kvit.md", NewNote{"Ideas/Projects", "Fresh idea", "Ideas/Projects/Fresh idea.md"}, true},
		{"Spaced idea", "Welcome.md", NewNote{"", "Spaced idea", "Spaced idea.md"}, true},
		{"Fresh", "", NewNote{"", "Fresh", "Fresh.md"}, true},
		// A path goes where it says, whatever the current note.
		{"New/Deeper/Idea", "Ideas/Plans.md", NewNote{"New/Deeper", "Idea", "New/Deeper/Idea.md"}, true},
		{"/Top", "Ideas/Plans.md", NewNote{"", "Top", "Top.md"}, true},
		{"x.MD", "Ideas/Plans.md", NewNote{"Ideas", "x", "Ideas/x.md"}, true},
		{".md", "", NewNote{}, false},
		// A trailing slash makes an untitled note in the folder.
		{"Folder/", "", NewNote{"Folder", "", ""}, true},
		// What the Qt app fails to create.
		{".hidden/x", "", NewNote{}, false},
		{"a//b", "", NewNote{}, false},
		{"a /b", "", NewNote{}, false},
		{`a\b`, "", NewNote{}, false},
		{".dot", "", NewNote{}, false},
	}
	for _, c := range cases {
		got, ok := NewNoteFor(c.target, c.current)
		if ok != c.ok || (ok && got != c.want) {
			t.Errorf("NewNoteFor(%q, %q) = %+v, %v; want %+v, %v", c.target, c.current, got, ok, c.want, c.ok)
		}
	}
}
