package export

import (
	"os"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
)

// writeSource writes a source file under dir and returns its path.
func writeSource(t *testing.T, dir, rel, content string) string {
	t.Helper()
	abs := filepath.Join(dir, filepath.FromSlash(rel))
	if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(abs, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}
	return abs
}

func planned(plan ImportPlan) map[string]string {
	out := map[string]string{}
	for _, f := range plan.Files {
		out[f.RelPath] = string(f.Content)
	}
	return out
}

func TestIsImportable(t *testing.T) {
	for p, want := range map[string]bool{"a.md": true, "A.MD": true, "notes.txt": true, "x.markdown": true,
		"image.png": false, "data.json": false} {
		if IsImportable(p) != want {
			t.Errorf("IsImportable(%q) = %v", p, !want)
		}
	}
}

// Files are copied as they are, front matter and all, into the folder named,
// and a file that is not importable is passed over.
func TestImportFiles(t *testing.T) {
	src, vault := t.TempDir(), t.TempDir()
	obsidian := "---\ntags: [research]\naliases: [foo, bar]\ncssclass: wide\n---\n# Vault Note\n\nContent.\n"
	paths := []string{
		writeSource(t, src, "Note.md", "# Hi\n\nbody\n"),
		writeSource(t, src, "A.md", "alpha"),
		writeSource(t, src, "Vault Note.md", obsidian),
		writeSource(t, src, "image.png", "notmarkdown"),
		writeSource(t, src, "plain.txt", "text"),
	}
	plan, err := ImportFiles(vault, paths, "", ImportOptions{})
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]string{"Note.md": "# Hi\n\nbody\n", "A.md": "alpha", "Vault Note.md": obsidian, "plain.md": "text"}
	got := planned(plan)
	if len(got) != len(want) {
		t.Errorf("planned %v", got)
	}
	for rel, content := range want {
		if got[rel] != content {
			t.Errorf("%s: %q, want %q", rel, got[rel], content)
		}
	}
	plan, _ = ImportFiles(vault, paths[3:4], "", ImportOptions{})
	if len(plan.Files) != 0 {
		t.Error("a .png was imported")
	}
}

// A name already in the vault, or already given earlier in the same import,
// gets " 2", " 3".
func TestCollisionSuffixing(t *testing.T) {
	src, vault := t.TempDir(), t.TempDir()
	writeSource(t, vault, "Dup.md", "existing")
	a := writeSource(t, src, "Dup.md", "imported body")
	b := writeSource(t, src, "other/Dup.txt", "second")
	plan, err := ImportFiles(vault, []string{a, b}, "", ImportOptions{})
	if err != nil {
		t.Fatal(err)
	}
	got := planned(plan)
	if got["Dup 2.md"] != "imported body" || got["Dup 3.md"] != "second" {
		t.Errorf("planned %v", got)
	}
}

// The importer's name rules: the base name up to its last dot, trimmed,
// with / \ : * ? " < > | taken out, and "Imported" when nothing is left.
func TestImportNames(t *testing.T) {
	cases := map[string]string{
		"plain":           "plain",
		"  spaced  ":      "spaced",
		`a:b*c?d"e<f>g|h`: "abcdefgh",
		"a :":             "a ",
		`\/:`:             "Imported",
		"":                "Imported",
		"archive.tar":     "archive.tar",
	}
	for in, want := range cases {
		if got := sanitizeBase(in); got != want {
			t.Errorf("sanitizeBase(%q) = %q, want %q", in, got, want)
		}
	}
	for in, want := range map[string]string{"a.tar.gz": "a.tar", "note.md": "note", ".hidden.md": ".hidden", "noext": "noext"} {
		if got := completeBaseName(in); got != want {
			t.Errorf("completeBaseName(%q) = %q, want %q", in, got, want)
		}
	}
	src, vault := t.TempDir(), t.TempDir()
	p := writeSource(t, src, "what?.md", "q")
	plan, _ := ImportFiles(vault, []string{p}, "In", ImportOptions{})
	if len(plan.Files) != 1 || plan.Files[0].RelPath != "In/what.md" || strings.Join(plan.Folders, ",") != "In" {
		t.Errorf("plan %+v", plan)
	}
}

// A folder import recreates the folder's tree under the target, .txt files
// written as .md notes.
func TestImportFolderPreservesTree(t *testing.T) {
	src, vault := t.TempDir(), t.TempDir()
	writeSource(t, src, "top.md", "# top\n")
	writeSource(t, src, "sub/one.md", "# one\n")
	writeSource(t, src, "sub/deeper/two.md", "# two\n")
	writeSource(t, src, "sub/notes.txt", "plain text\n")
	writeSource(t, src, "ignored.png", "not importable\n")
	writeSource(t, src, ".hidden/secret.md", "hidden\n")
	writeSource(t, src, ".dotfile.md", "hidden\n")
	if files, err := ImportableFiles(src); err != nil || len(files) != 4 {
		t.Errorf("importable files: %v %v", files, err)
	}
	plan, err := ImportFolder(vault, src, "In", ImportOptions{})
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]string{"In/top.md": "# top\n", "In/sub/one.md": "# one\n", "In/sub/deeper/two.md": "# two\n",
		"In/sub/notes.md": "plain text\n"}
	got := planned(plan)
	if len(got) != len(want) {
		t.Errorf("planned %v", got)
	}
	for rel, content := range want {
		if got[rel] != content {
			t.Errorf("%s: %q, want %q", rel, got[rel], content)
		}
	}
	if folders := strings.Join(plan.Folders, ","); folders != "In,In/sub,In/sub/deeper" {
		t.Errorf("folders: %s", folders)
	}
}

// The dry runs count what the dialog's summary shows.
func TestDryRuns(t *testing.T) {
	src, vault := t.TempDir(), t.TempDir()
	writeSource(t, vault, "Exists.md", "")
	a := writeSource(t, src, "Exists.md", "x")
	b := writeSource(t, src, "New.md", "y")
	c := writeSource(t, src, "pic.png", "z")
	if d := DryRunFiles(vault, []string{a, b, c}, ""); d.Files != 2 || d.Collisions != 1 {
		t.Errorf("files: %+v", d)
	}
	src2 := t.TempDir()
	writeSource(t, src2, "one.md", "1")
	writeSource(t, src2, "sub/two.md", "2")
	writeSource(t, src2, "sub/three.txt", "3")
	if d, err := DryRunFolder(vault, src2, "Into"); err != nil || d.Files != 3 || d.Folders != 1 || d.Collisions != 0 {
		t.Errorf("folder: %+v %v", d, err)
	}
}

// A target outside the vault is refused.
func TestTraversalAndAbsoluteTargetsAreRejected(t *testing.T) {
	src, vault := t.TempDir(), t.TempDir()
	p := writeSource(t, src, "Escape.md", "must stay in the vault")
	if _, err := ImportFiles(vault, []string{p}, "../outside", ImportOptions{}); err == nil {
		t.Error("a target walking out of the vault was accepted")
	}
	if _, err := ImportFolder(vault, src, filepath.Join(vault+"-outside"), ImportOptions{}); err == nil {
		t.Error("an absolute target was accepted")
	}
	link := filepath.Join(vault, "link")
	if err := os.Symlink(t.TempDir(), link); err == nil {
		if _, err := ImportFiles(vault, []string{p}, "link", ImportOptions{}); err == nil {
			t.Error("a folder linked out of the vault was accepted")
		}
	}
}

// A file over the cap is skipped and counted.
func TestOversizedSourceIsSkipped(t *testing.T) {
	src, vault := t.TempDir(), t.TempDir()
	small := writeSource(t, src, "Small.md", "# small\n")
	large := writeSource(t, src, "Large.md", strings.Repeat("x", 64*1024))
	plan, err := ImportFiles(vault, []string{small, large}, "", ImportOptions{MaxFileBytes: 1024})
	if err != nil {
		t.Fatal(err)
	}
	if len(plan.Files) != 1 || plan.Files[0].RelPath != "Small.md" || len(plan.Skipped) != 1 || plan.Skipped[0] != large {
		t.Errorf("plan: %+v", plan)
	}
	plan, _ = ImportFolder(vault, src, "", ImportOptions{MaxFileBytes: 1024})
	if len(plan.Files) != 1 || len(plan.Skipped) != 1 {
		t.Errorf("folder plan: %d files, %d skipped", len(plan.Files), len(plan.Skipped))
	}
}

// A source that opens but fails to read is skipped, so no note is written
// from part of it.
func TestUnreadableSourceIsSkipped(t *testing.T) {
	if runtime.GOOS != "linux" {
		t.Skip("no file on this platform that opens and then fails to read")
	}
	src, vault := t.TempDir(), t.TempDir()
	broken := filepath.Join(src, "Broken.md")
	if err := os.Symlink("/proc/self/mem", broken); err != nil {
		t.Skip(err)
	}
	plan, err := ImportFiles(vault, []string{broken}, "", ImportOptions{})
	if err != nil {
		t.Fatal(err)
	}
	if len(plan.Files) != 0 || len(plan.Skipped) != 1 {
		t.Errorf("plan: %+v", plan)
	}
}
