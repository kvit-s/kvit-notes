package vault

import (
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-notes/editor"
)

func write(t *testing.T, root, rel, text string) {
	t.Helper()
	p := filepath.Join(root, filepath.FromSlash(rel))
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
}

func read(t *testing.T, root, rel string) string {
	t.Helper()
	data, err := os.ReadFile(filepath.Join(root, filepath.FromSlash(rel)))
	if err != nil {
		t.Fatal(err)
	}
	return string(data)
}

// Kvit takes a block as front matter only when every line is mapping-like
// and one is a key; a note that starts with a divider is all body.
func TestFrontMatterIsRecognisedAsKvitDoes(t *testing.T) {
	cases := []struct {
		text string
		fm   bool
	}{
		{"---\ntags: [a]\n---\nBody\n", true},
		{"---\r\ntitle: x\r\n---\r\nBody\r\n", true},
		{"---\n# comment\nlist:\n  - one\n- two\n\n---\n", true},
		{"---\nSome prose after a divider.\n---\n", false},
		{"---\n\n---\n", false},
		{"---\ntags: [a]\n", false},
		{"Body\n---\ntags: [a]\n---\n", false},
	}
	for _, c := range cases {
		fm, body := splitFrontMatter(c.text)
		if (fm != nil) != c.fm {
			t.Errorf("%q: front matter %v, want %v", c.text, fm != nil, c.fm)
		}
		if fm == nil && body != c.text {
			t.Errorf("%q: without front matter the body must be the whole text", c.text)
		}
	}
}

// A change to one key rewrites that key in the Qt app's form and leaves every
// other line as it was.
func TestFrontMatterEditsKeepForeignLines(t *testing.T) {
	src := "---\ntitle: \"Kept: exactly\"\naliases:\n  - one\n  - two\ntags:\n  - alpha\n  - \"b,c\"\ncustom: 1\n---\n# Body\n"
	p := parsePage(src)
	if got := p.Tags(); !slices.Equal(got, []string{"alpha", "b,c"}) {
		t.Fatalf("tags: %q", got)
	}
	p.SetTags([]string{"alpha", "b,c", "new tag"})
	p.SetPinned(true)
	p.SetFavorite(false)
	want := "---\ntitle: \"Kept: exactly\"\naliases:\n  - one\n  - two\ntags: [alpha, \"b,c\", new tag]\npinned: true\ncustom: 1\n---\n# Body\n"
	if got := p.Text(); got != want {
		t.Errorf("got\n%s\nwant\n%s", got, want)
	}
	p.SetPinned(false)
	p.SetTags(nil)
	if got := p.Text(); got != "---\ntitle: \"Kept: exactly\"\naliases:\n  - one\n  - two\ncustom: 1\n---\n# Body\n" {
		t.Errorf("removing keys: %q", got)
	}
	// A note with no front matter gets one only when a key is set, in the
	// Qt app's order.
	q := parsePage("Just text\n")
	q.SetFavorite(true)
	q.SetTags([]string{"x"})
	if got := q.Text(); got != "---\ntags: [x]\nfavorite: true\n---\nJust text\n" {
		t.Errorf("new front matter: %q", got)
	}
	if got := parsePage("---\ntags: [\"a\\\"b\", 'c''d', e]\n---\n").Tags(); !slices.Equal(got, []string{`a"b`, "c'd", "e"}) {
		t.Errorf("quoted tags: %q", got)
	}
}

func TestOpenScansNotesAndSkipsKvitsOwnFolders(t *testing.T) {
	root := t.TempDir()
	write(t, root, "Welcome.md", "---\ntags: [start]\npinned: true\n---\n# Kvit Notes\n\nWelcome to **Kvit**.\n")
	write(t, root, "Ideas/Projects/Plan.md", "Plan text\n")
	write(t, root, "Ideas/readme.txt", "not a note")
	write(t, root, ".hidden/Secret.md", "no")
	write(t, root, "assets/Picture.md", "no")
	write(t, root, ".kvit/trash/20260101-000000-Old.md", "no")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	var paths []string
	for _, e := range v.Entries {
		paths = append(paths, e.Path)
	}
	if !slices.Equal(paths, []string{"Ideas/Projects/Plan.md", "Welcome.md"}) {
		t.Errorf("notes: %q", paths)
	}
	var folders []string
	for _, f := range v.Folders {
		folders = append(folders, f.Path)
	}
	if !slices.Equal(folders, []string{"Ideas", "Ideas/Projects"}) {
		t.Errorf("folders: %q", folders)
	}
	w := v.Find("Welcome.md")
	if w.Snippet != "Kvit Notes Welcome to Kvit." || w.Words != 5 || !w.Pinned || !slices.Equal(w.Tags, []string{"start"}) {
		t.Errorf("welcome: %+v", w)
	}
	if !v.HasSubfolders("Ideas") || v.HasSubfolders("Ideas/Projects") || v.CountIn("Ideas/Projects") != 1 || v.TrashCount() != 1 {
		t.Errorf("counts are wrong")
	}
}

// The lock is flock on .kvit/vault.lock, as the Qt app takes it, so a second
// opener, in this process or another, is refused.
func TestTheVaultLockExcludesOtherOpeners(t *testing.T) {
	root := t.TempDir()
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := Open(root); !errors.Is(err, ErrLocked) {
		t.Fatalf("a second open should be refused: %v", err)
	}
	if !strings.Contains(read(t, root, ".kvit/vault.lock"), `"application":"Kvit Notes"`) {
		t.Errorf("the lock file should say who holds it")
	}
	if runtime.GOOS == "linux" {
		if _, err := exec.LookPath("flock"); err == nil {
			out, err := exec.Command("flock", "-n", filepath.Join(root, ".kvit", "vault.lock"), "true").CombinedOutput()
			if err == nil {
				t.Errorf("flock(1) got the lock while the vault was open: %s", out)
			}
		}
	}
	v.Close()
	v2, err := Open(root)
	if err != nil {
		t.Fatalf("after closing, the vault should open again: %v", err)
	}
	v2.Close()
}

func TestSaveBacksUpAndKeepsABakOnlyWhenTheEditorReshapes(t *testing.T) {
	root := t.TempDir()
	write(t, root, "Tidy.md", "# Tidy\n\nAlready in the editor's form.\n")
	write(t, root, "Loose.md", "# Loose\nText right under the heading.\n")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	for _, name := range []string{"Tidy.md", "Loose.md"} {
		e := v.Find(name)
		p, err := v.Load(name)
		if err != nil {
			t.Fatal(err)
		}
		p.Body += "\nMore.\n"
		if err := v.Save(e, p); err != nil {
			t.Fatal(err)
		}
		if !strings.HasSuffix(read(t, root, name), "More.\n") {
			t.Errorf("%s was not saved", name)
		}
		backups, _ := filepath.Glob(filepath.Join(root, ".kvit", "backups", name, "*.md"))
		if len(backups) != 1 {
			t.Errorf("%s: %d backups, want 1", name, len(backups))
		}
	}
	if _, err := os.Stat(filepath.Join(root, "Tidy.md.bak")); err == nil {
		t.Errorf("a note already in the editor's form needs no .md.bak")
	}
	if got := read(t, root, "Loose.md.bak"); got != "# Loose\nText right under the heading.\n" {
		t.Errorf("Loose.md.bak should hold the note as it was: %q", got)
	}
	leftovers, _ := filepath.Glob(filepath.Join(root, ".*.md.*"))
	if len(leftovers) != 0 {
		t.Errorf("temporary files left behind: %q", leftovers)
	}
}

// A note holding an untagged box diagram opens with the fence tagged
// `diagram` and the drawing straightened (textdiagram.Ingest, which the
// editor's parser runs). Saving it therefore changes the file, so the first
// save keeps the note as it was in "<note>.md.bak", and later saves leave
// that file alone, as the Qt app's one-time backup does.
func TestOpeningADiagramRetagsItAndTheFirstSaveKeepsABak(t *testing.T) {
	crooked := "Before the drawing.\n\n```\n" +
		"┌──────────┐\n" +
		"│ Editor     │\n" +
		"└────┬───────┘\n" +
		"      │\n" +
		"┌─────▼──────┐\n" +
		"│ Serializer │\n" +
		"└────────────┘\n" +
		"```\n"
	root := t.TempDir()
	write(t, root, "Drawing.md", crooked)
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	e := v.Find("Drawing.md")
	p, err := v.Load("Drawing.md")
	if err != nil {
		t.Fatal(err)
	}
	blocks := editor.ParseMarkdown(p.Body)
	if len(blocks) != 2 || blocks[1].Kind != editor.Code || blocks[1].Lang != "diagram" {
		t.Fatalf("the fence did not open as a diagram: %+v", blocks)
	}
	if !strings.HasPrefix(blocks[1].Text, "┌────────────┐\n│ Editor     │\n└─────┬──────┘") {
		t.Errorf("the drawing was not straightened:\n%s", blocks[1].Text)
	}

	// The window saves what the editor holds.
	p.Body = editor.Serialize(blocks)
	if err := v.Save(e, p); err != nil {
		t.Fatal(err)
	}
	if got := read(t, root, "Drawing.md"); !strings.Contains(got, "```diagram\n┌────────────┐") {
		t.Errorf("the note was saved as:\n%s", got)
	}
	if got := read(t, root, "Drawing.md.bak"); got != crooked {
		t.Errorf("Drawing.md.bak should hold the note as it was:\n%s", got)
	}

	blocks[0].Text = "Edited before the drawing."
	p.Body = editor.Serialize(blocks)
	if err := v.Save(e, p); err != nil {
		t.Fatal(err)
	}
	if got := read(t, root, "Drawing.md.bak"); got != crooked {
		t.Errorf("a second save changed Drawing.md.bak:\n%s", got)
	}

	// Opened again, the note is already in the editor's form.
	again, err := v.Load("Drawing.md")
	if err != nil {
		t.Fatal(err)
	}
	if again.reshapes {
		t.Errorf("the saved note still changes when it is opened")
	}
}

func TestCreateRenameMoveAndTrash(t *testing.T) {
	root := t.TempDir()
	write(t, root, "Untitled.md", "")
	write(t, root, "Journal/.keep", "")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	e, err := v.Create("")
	if err != nil || e.Path != "Untitled 2.md" || !e.Untitled() {
		t.Fatalf("create: %v %+v", err, e)
	}
	if err := v.Rename(e, " spaced"); !errors.Is(err, ErrName) {
		t.Errorf("a name with a leading space should be refused: %v", err)
	}
	if err := v.Rename(e, "Untitled"); !errors.Is(err, ErrExists) {
		t.Errorf("a taken name should be refused: %v", err)
	}
	if got := TitleFromText("  ../Reading: a list / of books\nsecond line"); got != "Reading: a list  of books" {
		t.Errorf("title from text: %q", got)
	}
	if err := v.Rename(e, "Reading list"); err != nil || e.Path != "Reading list.md" || e.Untitled() {
		t.Fatalf("rename: %v %+v", err, e)
	}
	v.State.LastOpenNote = e.Path
	if err := v.Move(e, "Journal"); err != nil || e.Path != "Journal/Reading list.md" || e.Folder != "Journal" {
		t.Fatalf("move: %v %+v", err, e)
	}
	if v.State.LastOpenNote != "Journal/Reading list.md" {
		t.Errorf("the last open note should follow the move: %q", v.State.LastOpenNote)
	}
	if err := v.Trash(e); err != nil {
		t.Fatal(err)
	}
	trashed, _ := filepath.Glob(filepath.Join(root, ".kvit", "trash", "*-Reading list.md"))
	if len(trashed) != 1 || v.Find("Journal/Reading list.md") != nil {
		t.Errorf("trash: %q", trashed)
	}
}

// collection.json is written as the Qt app writes it, keeping the fields
// this app does not use yet.
func TestCollectionStateRoundTrips(t *testing.T) {
	root := t.TempDir()
	write(t, root, "Ideas/Note.md", "x")
	write(t, root, ".kvit/collection.json", `{"folders":{"Ideas":{"color":"#e05c5c","expanded":true},"Gone":{"expanded":false}},"manualOrder":{"":["Note.md"]},"tagColors":{"work":"#4a90d9"}}`)
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	if !v.State.FolderExpanded("Ideas") || !v.State.FolderExpanded("Elsewhere") {
		t.Errorf("folders are open unless marked closed")
	}
	v.State.SetFolderExpanded("Ideas", false)
	if err := v.SaveState(); err != nil {
		t.Fatal(err)
	}
	want := `{
    "folders": {
        "Ideas": {
            "color": "#e05c5c",
            "expanded": false
        }
    },
    "manualOrder": {
        "": [
            "Note.md"
        ]
    },
    "tagColors": {
        "work": "#4a90d9"
    }
}
`
	if got := read(t, root, ".kvit/collection.json"); got != want {
		t.Errorf("collection.json:\n%s\nwant\n%s", got, want)
	}
}

// The recovery journal is named as the Qt app names it: the note path
// percent-encoded into one flat file name.
func TestTheRecoveryJournal(t *testing.T) {
	if got := journalName("Ideas/Reading list é.md"); got != "Ideas%2FReading%20list%20%C3%A9.md" {
		t.Errorf("journal name: %q", got)
	}
	for _, bad := range []string{"..%2Fx.md", "a%2F%2Fb.md", "x.txt", "a%2fb.md"} {
		if journalPath(bad) != "" {
			t.Errorf("%q should not be accepted as a journal", bad)
		}
	}
	root := t.TempDir()
	write(t, root, "Ideas/Note.md", "saved\n")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	if err := v.WriteJournal("Ideas/Note.md", "unsaved\n"); err != nil {
		t.Fatal(err)
	}
	got := v.Journals()
	if len(got) != 1 || got[0].Path != "Ideas/Note.md" || got[0].Text != "unsaved\n" {
		t.Fatalf("journals: %+v", got)
	}
	p, err := v.Restore(v.Find("Ideas/Note.md"), got[0].Text)
	if err != nil || p.Body != "unsaved\n" || read(t, root, "Ideas/Note.md") != "unsaved\n" {
		t.Errorf("restore: %v %q", err, read(t, root, "Ideas/Note.md"))
	}
	if len(v.Backups("Ideas/Note.md")) != 1 {
		t.Errorf("restoring should back up the version it replaces")
	}
	v.ClearJournal("Ideas/Note.md")
	if len(v.Journals()) != 0 {
		t.Errorf("the journal should be gone")
	}
}

func TestTheTrashCanBeListedRestoredAndEmptied(t *testing.T) {
	root := t.TempDir()
	write(t, root, "Old.md", "old\n")
	write(t, root, "Keep/Inside.md", "in\n")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	if err := v.Trash(v.Find("Old.md")); err != nil {
		t.Fatal(err)
	}
	if err := v.TrashFolder("Keep"); err != nil {
		t.Fatal(err)
	}
	items := v.TrashItems()
	if len(items) != 2 {
		t.Fatalf("trash: %+v", items)
	}
	var note, folder Trashed
	for _, it := range items {
		if it.Dir {
			folder = it
		} else {
			note = it
		}
	}
	if note.Title != "Old" || folder.Title != "Keep" || note.Time.IsZero() {
		t.Errorf("items: %+v %+v", note, folder)
	}
	if text, _ := v.ReadTrashed(note); text != "old\n" {
		t.Errorf("trashed text: %q", text)
	}
	write(t, root, "Old.md", "a new note of the same name\n")
	rel, err := v.Untrash(note)
	if err != nil || rel != "Old 2.md" || v.Find("Old 2.md") == nil {
		t.Errorf("untrash: %v %q", err, rel)
	}
	if err := v.DeleteForever(folder); err != nil || v.TrashCount() != 0 {
		t.Errorf("delete forever: %v, %d left", err, v.TrashCount())
	}
	if err := v.Trash(v.Find("Old 2.md")); err != nil {
		t.Fatal(err)
	}
	if err := v.EmptyTrash(); err != nil || v.TrashCount() != 0 {
		t.Errorf("empty trash: %v", err)
	}
}
