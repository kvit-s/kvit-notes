package links

import (
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func TestIsPlainRelativePath(t *testing.T) {
	cases := map[string]bool{
		"Note.md":        true,
		"Ideas/Note.md":  true,
		".kvit/x.md":     true,
		"a b/c.md":       true,
		"":               false,
		"/Note.md":       false,
		"../Note.md":     false,
		"../../Away.md":  false,
		"a/../b.md":      false,
		"./a.md":         false,
		"a//b.md":        false,
		"a/":             false,
		`a\b.md`:         false,
		"C:/x.md":        false,
		"c:x.md":         false,
		":/resource.md":  false,
		"a/./b.md":       false,
		"Ideas/..":       false,
		"Ideas/...md":    true,
		"Ideas/C:/x.md":  true,
		"Ideas/a:b/c.md": true,
	}
	for p, want := range cases {
		if got := IsPlainRelativePath(p); got != want {
			t.Errorf("IsPlainRelativePath(%q) = %v", p, got)
		}
	}
}

func table(entries ...Redirect) *Redirects {
	r := &Redirects{entries: entries}
	r.reindex()
	return r
}

// The rules of LinkRedirects::record: no chains, nothing standing where a
// note arrived, one entry per old path, and nothing malformed.
func TestRecord(t *testing.T) {
	cases := []struct {
		name     string
		before   []Redirect
		from, to string
		changed  bool
		after    []Redirect
	}{
		{"first", nil, "A.md", "B.md", true, []Redirect{{"A.md", "B.md"}}},
		{"chain collapses", []Redirect{{"A.md", "B.md"}}, "B.md", "C.md", true,
			[]Redirect{{"A.md", "C.md"}, {"B.md", "C.md"}}},
		{"renamed back", []Redirect{{"A.md", "B.md"}}, "B.md", "A.md", true, []Redirect{{"B.md", "A.md"}}},
		{"a note arrives where one left", []Redirect{{"X.md", "Y.md"}}, "A.md", "X.md", true,
			[]Redirect{{"A.md", "X.md"}}},
		{"one entry per old path", []Redirect{{"A.md", "B.md"}}, "A.md", "C.md", true, []Redirect{{"A.md", "C.md"}}},
		{"same path", nil, "A.md", "A.md", false, nil},
		{"outside the vault", nil, "../A.md", "B.md", false, nil},
		{"absolute", nil, "A.md", "/B.md", false, nil},
	}
	for _, c := range cases {
		r := table(c.before...)
		if got := r.Record(c.from, c.to); got != c.changed {
			t.Errorf("%s: Record = %v", c.name, got)
		}
		if got := r.Entries(); !reflect.DeepEqual(got, c.after) && (len(got) != 0 || len(c.after) != 0) {
			t.Errorf("%s: table %+v, want %+v", c.name, got, c.after)
		}
	}
}

func TestRetargetDropFromRetainFrom(t *testing.T) {
	r := table(Redirect{"A.md", "B.md"}, Redirect{"X.md", "B.md"}, Redirect{"P.md", "Q.md"})
	if !r.Retarget("B.md", "C.md") {
		t.Error("Retarget changed nothing")
	}
	want := []Redirect{{"A.md", "C.md"}, {"X.md", "C.md"}, {"P.md", "Q.md"}}
	if got := r.Entries(); !reflect.DeepEqual(got, want) {
		t.Errorf("after Retarget: %+v", got)
	}
	// Moved back to where it started: that entry says nothing any more.
	if !r.Retarget("C.md", "A.md") {
		t.Error("Retarget back changed nothing")
	}
	want = []Redirect{{"X.md", "A.md"}, {"P.md", "Q.md"}}
	if got := r.Entries(); !reflect.DeepEqual(got, want) {
		t.Errorf("after Retarget back: %+v", got)
	}
	if r.Retarget("Nothing.md", "Else.md") || r.Retarget("Q.md", "../out.md") {
		t.Error("Retarget changed what it should not")
	}
	if !r.DropFrom("X.md") || r.DropFrom("X.md") {
		t.Error("DropFrom")
	}
	if !r.RetainFrom(map[string]bool{"A.md": true}) || r.Len() != 0 {
		t.Errorf("RetainFrom left %+v", r.Entries())
	}
}

// A lookup names an old path by the rules links name notes by, and two old
// paths that match with different destinations answer nothing.
func TestLookup(t *testing.T) {
	r := table(Redirect{"x/A.md", "P.md"}, Redirect{"y/A.md", "Q.md"}, Redirect{"z/B.md", "R.md"}, Redirect{"w/B.md", "R.md"})
	cases := map[string]string{
		"a":      "",
		"x/a":    "P.md",
		"y/a":    "Q.md",
		"b":      "R.md",
		"z/b":    "R.md",
		"other":  "",
		"":       "",
		"q/x/a":  "",
		"ax/a":   "",
		"/x/a":   "",
		"x/a.md": "",
	}
	for target, want := range cases {
		if got := r.TargetFor(target); got != want {
			t.Errorf("TargetFor(%q) = %q, want %q", target, got, want)
		}
	}
}

// The file is the app's compact JSON, and an empty table deletes it.
func TestSaveWritesTheQtFormat(t *testing.T) {
	root := t.TempDir()
	r := table(Redirect{"Target.md", "Renamed.md"}, Redirect{`Odd "name".md`, "Ideas/Café.md"})
	if err := r.Save(root); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(filepath.Join(root, ".kvit", "redirects.json"))
	if err != nil {
		t.Fatal(err)
	}
	want := `{"redirects":[{"from":"Target.md","to":"Renamed.md"},{"from":"Odd \"name\".md","to":"Ideas/Café.md"}],"version":1}`
	if string(data) != want {
		t.Errorf("file:\n%s\nwant\n%s", data, want)
	}
	if got := LoadRedirects(root).Entries(); !reflect.DeepEqual(got, r.Entries()) {
		t.Errorf("loaded %+v", got)
	}
	if err := (&Redirects{}).Save(root); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(root, ".kvit", "redirects.json")); !os.IsNotExist(err) {
		t.Error("an empty table left its file")
	}
	// Control characters are escaped as escapes them.
	if got := string(table(Redirect{"a\tb\x01.md", "c.md"}).encode()); got !=
		`{"redirects":[{"from":"a\tb\u0001.md","to":"c.md"}],"version":1}` {
		t.Errorf("escaped: %s", got)
	}
	// Deleting a file that is not there is not an error.
	if err := (&Redirects{}).Save(root); err != nil {
		t.Error(err)
	}
}

// From TestNoteCollection::testRedirectFileIsOptionalAndStaysInsideTheVault:
// no file is an empty table, and an entry naming a path outside the vault,
// either end, is left out. So is anything that is not the table.
func TestLoadRefusesWhatIsNotAPlainTable(t *testing.T) {
	root := t.TempDir()
	if r := LoadRedirects(root); r.Len() != 0 {
		t.Errorf("no file loaded %+v", r.Entries())
	}
	cases := map[string][]Redirect{
		`{"version":1,"redirects":[{"from":"../../Away.md","to":"Target.md"}]}`:    nil,
		`{"version":1,"redirects":[{"from":"Away.md","to":"../../elsewhere.md"}]}`: nil,
		`{"version":1,"redirects":[{"from":"Away.md","to":"Target.md"},{"from":"Same.md","to":"Same.md"},` +
			`{"from":1,"to":"x.md"},"text",{"to":"y.md"}]}`: {{"Away.md", "Target.md"}},
		`{"version":2,"redirects":[{"from":"a.md","to":"b.md"}]}`: {{"a.md", "b.md"}},
		`{"Redirects":[{"from":"a.md","to":"b.md"}]}`:             nil,
		`{"redirects":{"from":"a.md","to":"b.md"}}`:               nil,
		`[{"from":"a.md","to":"b.md"}]`:                           nil,
		`{"redirects":[{"from":"a.md","to":"b.md"}]`:              nil,
		``: nil,
	}
	for text, want := range cases {
		write(t, root, ".kvit/redirects.json", text)
		got := LoadRedirects(root).Entries()
		if !reflect.DeepEqual(got, want) && (len(got) != 0 || len(want) != 0) {
			t.Errorf("%s: loaded %+v, want %+v", text, got, want)
		}
	}
}

// A file over 16 MiB was not written by Kvit and is read as nothing.
func TestLoadRefusesAnOversizedFile(t *testing.T) {
	root := t.TempDir()
	entry := `{"from":"a.md","to":"b.md"}`
	body := `{"redirects":[` + entry + `],"version":1,"pad":"`
	text := body + strings.Repeat("x", maxRedirectBytes-len(body)-2) + `"}`
	write(t, root, ".kvit/redirects.json", text)
	if r := LoadRedirects(root); r.Len() != 1 {
		t.Fatalf("a file of exactly 16 MiB loaded %d entries", r.Len())
	}
	write(t, root, ".kvit/redirects.json", body+strings.Repeat("x", maxRedirectBytes-len(body)-1)+`"}`)
	if r := LoadRedirects(root); r.Len() != 0 {
		t.Errorf("a file over 16 MiB loaded %+v", r.Entries())
	}
}

// .kvit standing as a link is refused for reading and writing, as the
// app refuses to write through it.
func TestALinkedKvitDirectoryIsRefused(t *testing.T) {
	root, elsewhere := t.TempDir(), t.TempDir()
	write(t, elsewhere, "redirects.json", `{"redirects":[{"from":"a.md","to":"b.md"}],"version":1}`)
	if err := os.Symlink(elsewhere, filepath.Join(root, ".kvit")); err != nil {
		t.Skip("cannot make a symbolic link here:", err)
	}
	if r := LoadRedirects(root); r.Len() != 0 {
		t.Errorf("read through the link: %+v", r.Entries())
	}
	if err := table(Redirect{"c.md", "d.md"}).Save(root); err == nil {
		t.Error("wrote through the link")
	}
	if err := (&Redirects{}).Save(root); err == nil {
		t.Error("deleted through the link")
	}
	if _, err := os.Stat(filepath.Join(elsewhere, "redirects.json")); err != nil {
		t.Error("the file behind the link is gone")
	}
}
