package app

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// waitFor runs the window until cond holds, for up to three seconds.
func (s *session) waitFor(what string, cond func() bool) {
	s.t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		ok := false
		s.do(func() { ok = cond() })
		if ok {
			return
		}
		time.Sleep(50 * time.Millisecond)
	}
	s.t.Fatalf("waited three seconds for %s", what)
}

func (s *session) editorText() string {
	var text string
	s.do(func() {
		var parts []string
		for _, b := range s.w.Editor.Doc.Blocks {
			parts = append(parts, b.Text)
		}
		text = strings.Join(parts, "\n")
	})
	return text
}

func (s *session) writeFile(rel, text string) {
	s.t.Helper()
	if err := os.WriteFile(filepath.Join(s.root, filepath.FromSlash(rel)), []byte(text), 0o644); err != nil {
		s.t.Fatal(err)
	}
}

func TestUnsavedChangesAreOfferedBackAfterAnInterruption(t *testing.T) {
	n := notes{}
	for k, v := range demo {
		n[k] = v
	}
	n[".kvit/recovery/Reading%20list.md"] = "Books I meant to keep\n"
	s := openVault(t, n)
	var offers int
	s.do(func() { offers = len(s.w.banner.Children()) })
	if offers != 1 {
		t.Fatalf("one recovered note should be offered, %d are", offers)
	}
	s.shot("vault_05_recovered.png")
	s.press("Restore")
	if got := s.file("Reading list.md"); got != "Books I meant to keep\n" {
		t.Errorf("restoring: %q", got)
	}
	if s.exists(".kvit/recovery/Reading%20list.md") {
		t.Errorf("the journal should be cleared")
	}
}

func TestTheJournalFollowsUnsavedChanges(t *testing.T) {
	s := openVault(t, demo)
	s.clickRow(slices.Index(s.listed(), "Reading list"))
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.screen.Type("Soon ")
	s.waitFor("the journal", func() bool { return s.exists(".kvit/recovery/Reading%20list.md") })
	if got := s.file(".kvit/recovery/Reading%20list.md"); !strings.HasPrefix(got, "Soon Books") {
		t.Errorf("journal: %q", got)
	}
	s.screen.KeyPress(unison.KeyS, mod.Control)
	if s.exists(".kvit/recovery/Reading%20list.md") {
		t.Errorf("saving should clear the journal")
	}
}

func TestAChangeByAnotherProgramReloadsTheNote(t *testing.T) {
	s := openVault(t, demo)
	s.clickRow(slices.Index(s.listed(), "Reading list"))
	s.writeFile("Reading list.md", "Rewritten elsewhere\n")
	s.waitFor("the reload", func() bool { return s.w.Editor.Doc.Blocks[0].Text == "Rewritten elsewhere" })
	// With unsaved changes, the reader chooses.
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.screen.Type("Mine: ")
	s.writeFile("Reading list.md", "Theirs\n")
	s.waitFor("the question", func() bool { return len(s.w.theirs.Children()) == 1 })
	if !strings.HasPrefix(s.editorText(), "Mine: ") {
		t.Errorf("unsaved changes must not be replaced before the reader chooses: %q", s.editorText())
	}
	s.shot("vault_06_changed_elsewhere.png")
	s.press("Load the other version")
	if s.editorText() != "Theirs" {
		t.Errorf("loading the other version: %q", s.editorText())
	}
}

func TestRestoringAnEarlierVersion(t *testing.T) {
	n := notes{}
	for k, v := range demo {
		n[k] = v
	}
	n[".kvit/backups/Reading list.md/20260101-090000.md"] = "The January list\n"
	n[".kvit/backups/Reading list.md/20260201-090000.md"] = "The February list\n"
	s := openVault(t, n)
	s.clickRow(slices.Index(s.listed(), "Reading list"))
	s.press("Earlier versions of this note")
	s.shot("vault_07_versions.png")
	s.press("Restore this version")
	if got := s.file("Reading list.md"); got != "The February list\n" {
		t.Errorf("the newest version should be restored: %q", got)
	}
	if s.editorText() != "The February list" {
		t.Errorf("the editor should show it: %q", s.editorText())
	}
}

func TestTheTrashShowsNotesReadOnlyAndPutsThemBack(t *testing.T) {
	s := openVault(t, demo)
	s.do(func() { s.w.trash(s.w.Vault.Find("Reading list.md")) })
	s.clickScope("Trash")
	if got := s.listed(); !slices.Equal(got, []string{"Reading list"}) {
		t.Fatalf("the trash shows %q", got)
	}
	s.clickRow(0)
	var readOnly bool
	s.do(func() { readOnly = s.w.Editor.Doc.ReadOnly })
	if !readOnly || !strings.Contains(s.editorText(), "summer") {
		t.Errorf("a trashed note shows read-only: %v %q", readOnly, s.editorText())
	}
	var p geom.Point
	s.do(func() {
		h := s.w.list.rowHeight()
		p = s.screen.PanelPoint(s.w.list, geom.NewPoint(40, h/2))
	})
	s.screen.ClickWith(p, unison.ButtonRight, mod.None)
	s.press("Put back")
	if !s.exists("Reading list.md") || s.openTitle() != "Reading list" {
		t.Errorf("putting back should restore and open the note")
	}
}

func TestAVaultThatCannotBeWrittenOpensReadOnly(t *testing.T) {
	if os.Geteuid() == 0 {
		t.Skip("root can write anywhere")
	}
	n := notes{"Note.md": "Text\n"}
	root := t.TempDir()
	for rel, text := range n {
		if err := os.WriteFile(filepath.Join(root, rel), []byte(text), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	if err := os.Chmod(root, 0o555); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = os.Chmod(root, 0o755) })
	s := openVaultAt(t, root)
	var readOnly bool
	var status string
	s.do(func() { readOnly, status = s.w.Editor.Doc.ReadOnly, s.w.status.Activity })
	if !readOnly || !strings.Contains(status, "reading only") {
		t.Errorf("read only %v, status %q", readOnly, status)
	}
	s.screen.Type("x")
	if got := s.file("Note.md"); got != "Text\n" {
		t.Errorf("nothing may change: %q", got)
	}
}
