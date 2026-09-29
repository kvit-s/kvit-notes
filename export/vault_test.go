package export

import (
	"encoding/base64"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

// vaultOf writes notes into a new vault folder and returns the folder and
// the notes as a vault export takes them.
func vaultOf(t *testing.T, files map[string]string) (string, []VaultNote) {
	t.Helper()
	root := t.TempDir()
	var notes []VaultNote
	for _, rel := range sortedKeys(files) {
		abs := filepath.Join(root, filepath.FromSlash(rel))
		if err := os.MkdirAll(filepath.Dir(abs), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(abs, []byte(files[rel]), 0o644); err != nil {
			t.Fatal(err)
		}
		if strings.HasSuffix(rel, ".md") {
			notes = append(notes, VaultNote{RelPath: rel, Text: files[rel]})
		}
	}
	return root, notes
}

func paths(files []File, base string) []string {
	var out []string
	for _, f := range files {
		rel, _ := filepath.Rel(base, f.Path)
		out = append(out, filepath.ToSlash(rel))
	}
	return out
}

// Each note becomes one file, mirroring the vault's folders, or all become
// collection.<ext> (testExportCollectionPerNote, testExportCollectionSingleFile).
func TestVaultLayout(t *testing.T) {
	root, notes := vaultOf(t, map[string]string{"Alpha.md": "a\n", "Sub/Beta.md": "b\n"})
	dest := t.TempDir()
	files, err := Vault(VaultExport{Root: root, Notes: notes, Dest: dest, Format: FormatHTML})
	if err != nil {
		t.Fatal(err)
	}
	if got := strings.Join(paths(files, dest), ","); got != "Alpha.html,Sub/Beta.html" {
		t.Errorf("per note: %s", got)
	}
	for _, format := range []Format{FormatHTML, FormatText, FormatMarkdown} {
		files, err = Vault(VaultExport{Root: root, Notes: notes, Dest: dest, Format: format, SingleFile: true})
		if err != nil {
			t.Fatal(err)
		}
		if got := strings.Join(paths(files, dest), ","); got != "collection."+format.Extension() {
			t.Errorf("single %s: %s", format, got)
		}
	}
	if _, err := Vault(VaultExport{Root: root, Notes: notes, Dest: dest, Format: FormatPDF}); err != ErrPDF {
		t.Errorf("PDF: %v", err)
	}
}

// A combined HTML export is one page, with the stylesheet and each script
// tag once, and a page break before each note after the first
// (testSingleFileHtmlIsOneDocument, testSingleFileHtmlInjectsSharedAssetsOnce,
// testSingleFileHtmlSeparatesNotesWithPageBreaks).
func TestCombinedHTMLIsOnePage(t *testing.T) {
	root, notes := vaultOf(t, map[string]string{
		"One.md":   "# One\n\nAlpha body.\n\n$$\nE = mc^2\n$$\n",
		"Three.md": "# Three\n\nGamma.\n",
		"Two.md":   "# Two\n\nBeta body.\n\n$$\na^2 + b^2\n$$\n",
	})
	files, err := Vault(VaultExport{Root: root, Notes: notes, Dest: t.TempDir(), Format: FormatHTML, SingleFile: true})
	if err != nil {
		t.Fatal(err)
	}
	html := string(files[0].Content)
	for needle, n := range map[string]int{"<!DOCTYPE html>": 1, "<html": 1, "</html>": 1, "<head>": 1, "<body>": 1,
		"</body>": 1, "<style>": 1, "MathJax": 1, "page-break-before": 2} {
		if got := strings.Count(html, needle); got != n {
			t.Errorf("%q appears %d times, want %d", needle, got, n)
		}
	}
	if !strings.HasSuffix(strings.TrimSpace(html), "</html>") || !strings.Contains(html, "Alpha body.") ||
		!strings.Contains(html, "Beta body.") || strings.Contains(html, "</html>\n<hr>") {
		t.Errorf("page:\n%s", bodyOf(html))
	}
}

// Combined Markdown puts each note under its title; combined text runs them
// on. Neither includes front matter.
func TestCombinedMarkdownAndText(t *testing.T) {
	root, notes := vaultOf(t, map[string]string{"A.md": "---\ntags: [x]\n---\nAlpha\n", "B.md": "- b\n"})
	files, err := Vault(VaultExport{Root: root, Notes: notes, Dest: t.TempDir(), Format: FormatMarkdown, SingleFile: true})
	if err != nil {
		t.Fatal(err)
	}
	if got, want := string(files[0].Content), "# A\n\nAlpha\n\n\n# B\n\n- b\n\n\n"; got != want {
		t.Errorf("markdown: %q, want %q", got, want)
	}
	files, err = Vault(VaultExport{Root: root, Notes: notes, Dest: t.TempDir(), Format: FormatText, SingleFile: true})
	if err != nil {
		t.Fatal(err)
	}
	if got, want := string(files[0].Content), "Alpha\n\n\n- b\n\n\n"; got != want {
		t.Errorf("text: %q, want %q", got, want)
	}
}

// Each note's relative images are found in that note's own folder
// (testPerNoteImageBaseInCollectionExport).
func TestPerNoteImageBase(t *testing.T) {
	root, notes := vaultOf(t, map[string]string{
		"A/one.md":  "# One\n\n![](pic.png)\n",
		"A/pic.png": "AAAAAAAAAAAAAAAA",
		"B/pic.png": "BBBBBBBBBBBBBBBB",
		"B/two.md":  "# Two\n\n![](pic.png)\n",
	})
	files, err := Vault(VaultExport{Root: root, Notes: notes, Dest: t.TempDir(), Format: FormatHTML,
		Options: Options{NoteDir: filepath.Join(root, "A")}})
	if err != nil {
		t.Fatal(err)
	}
	a := base64.StdEncoding.EncodeToString([]byte("AAAAAAAAAAAAAAAA"))
	b := base64.StdEncoding.EncodeToString([]byte("BBBBBBBBBBBBBBBB"))
	if !strings.Contains(string(files[0].Content), a) || !strings.Contains(string(files[1].Content), b) {
		t.Error("an image was resolved against the wrong note's folder")
	}
}

// The note open in the editor exports with its unsaved text, which the
// caller passes in its place; nothing is written to the vault
// (testLiveNoteSnapshotOverridesSavedBody, testLiveNoteSnapshotIsIgnoredForOtherNotes).
func TestLiveNoteText(t *testing.T) {
	root, notes := vaultOf(t, map[string]string{"Open.md": "# Open\n\nSaved open.\n", "Other.md": "# Other\n\nSaved other.\n"})
	notes[0].Text = "# Open\n\nUnsaved open."
	files, err := Vault(VaultExport{Root: root, Notes: notes, Dest: t.TempDir(), Format: FormatHTML})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(files[0].Content), "Unsaved open.") || strings.Contains(string(files[0].Content), "Saved open.") ||
		!strings.Contains(string(files[1].Content), "Saved other.") {
		t.Error("the open note's unsaved text was not what was exported")
	}
	if data, _ := os.ReadFile(filepath.Join(root, "Open.md")); string(data) != "# Open\n\nSaved open.\n" {
		t.Error("the note on disk changed")
	}
}

// A standalone Markdown export is the note, front matter included, in the
// form the app writes it (testExportOutsideTheVaultStillWorks).
func TestMarkdownExportIncludesFrontMatter(t *testing.T) {
	root, notes := vaultOf(t, map[string]string{
		"One.md":        "---\ntags: [a]\n---\nBody one.\n",
		"Folder/Two.md": "Body two.\n",
		"Three.md":      "---\nreviewer: ada\npinned: true\ntags:\n  - b\n  - \"c,d\"\ncreated: 2026-07-06\n---\nThree.\n",
	})
	dest := t.TempDir()
	files, err := Vault(VaultExport{Root: root, Notes: notes, Dest: dest, Format: FormatMarkdown})
	if err != nil {
		t.Fatal(err)
	}
	want := map[string]string{
		"One.md":        "---\ntags: [a]\n---\nBody one.\n",
		"Folder/Two.md": "Body two.\n",
		"Three.md":      "---\ntags: [b, \"c,d\"]\ncreated: 2026-07-06T00:00:00\npinned: true\nreviewer: ada\n---\nThree.\n",
	}
	for i, f := range files {
		rel := paths(files, dest)[i]
		if string(f.Content) != want[rel] {
			t.Errorf("%s: %q, want %q", rel, f.Content, want[rel])
		}
	}
}

// Plans that would write over a note are refused whole, with the app's
// messages (testMarkdownExportIntoTheVaultLeavesSourcesByteIdentical,
// testMarkdownExportIntoASubfolderOfTheVaultIsRefused,
// testCombinedExportOntoASourceIsRefused, testCollidingOutputsAreRefused).
func TestUnsafePlansAreRefused(t *testing.T) {
	root, notes := vaultOf(t, map[string]string{
		"Kept.md":          "---\ntags: [work, urgent]\nfavorite: true\nreviewer: ada\n---\n# Kept\n\nThe body.\n",
		"Folder/Nested.md": "---\ntags: [nested]\n---\nNested body.\n",
		"collection.md":    "---\ntags: [meta]\n---\nIndex of notes.\n",
	})
	inside := filepath.Join(root, "Exports")
	if err := os.MkdirAll(inside, 0o755); err != nil {
		t.Fatal(err)
	}
	cases := []struct {
		name   string
		e      VaultExport
		reason string
	}{
		{"testMarkdownExportIntoTheVaultLeavesSourcesByteIdentical",
			VaultExport{Root: root, Notes: notes, Dest: root, Format: FormatMarkdown},
			"Markdown export writes note bodies without their metadata"},
		{"testMarkdownExportIntoASubfolderOfTheVaultIsRefused",
			VaultExport{Root: root, Notes: notes, Dest: inside, Format: FormatMarkdown},
			"cannot write inside the collection itself"},
		{"testCombinedExportOntoASourceIsRefused",
			VaultExport{Root: root, Notes: notes, Dest: root, Format: FormatMarkdown, SingleFile: true},
			"Exporting there would overwrite one of your notes."},
		{"testCollidingOutputsAreRefused",
			VaultExport{Root: root, Notes: []VaultNote{notes[1], notes[1]}, Dest: t.TempDir(), Format: FormatHTML},
			"Two notes in this export would be written to the same file"},
		{"nothing to export", VaultExport{Root: root, Dest: t.TempDir(), Format: FormatHTML}, "There is nothing to export."},
		{"no destination", VaultExport{Root: root, Notes: notes, Format: FormatHTML}, "No destination was chosen."},
		{"not a note", VaultExport{Root: root, Notes: []VaultNote{{RelPath: "../x.md"}}, Dest: t.TempDir(), Format: FormatHTML},
			`"../x.md" is not a note in this collection.`},
	}
	var refusal *Refusal
	for _, c := range cases {
		_, err := Vault(c.e)
		if !errors.As(err, &refusal) || !strings.Contains(refusal.Reason, c.reason) {
			t.Errorf("%s: %v, want a refusal saying %q", c.name, err, c.reason)
		}
	}
	// HTML into a folder inside the vault cannot land on a .md note, so it
	// is allowed.
	files, err := Vault(VaultExport{Root: root, Notes: notes[:1], Dest: inside, Format: FormatHTML})
	if err != nil || len(files) != 1 {
		t.Errorf("HTML inside the vault: %v", err)
	}
	// With no list of the vault's notes, the notes under the destination are
	// found on disk: a combined Markdown file that would land on one that is
	// not being exported is refused.
	if err := os.WriteFile(filepath.Join(inside, "collection.md"), []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	_, err = Vault(VaultExport{Root: root, Notes: notes[:1], Dest: inside, Format: FormatMarkdown, SingleFile: true})
	if !errors.As(err, &refusal) {
		t.Errorf("a combined file onto a note found on disk: %v", err)
	}
	// Given the list, a note not on it is not protected, as in the app.
	_, err = Vault(VaultExport{Root: root, Notes: notes[:1], AllNotes: []string{"Kept.md"}, Dest: inside,
		Format: FormatMarkdown, SingleFile: true})
	if err != nil {
		t.Errorf("with the list of notes: %v", err)
	}
}

// A combined file over the budget is refused rather than built
// (testCombinedExportOverTheDocumentBudgetIsRefused).
func TestCombinedBudget(t *testing.T) {
	files := map[string]string{}
	for _, n := range []string{"Big0.md", "Big1.md", "Big2.md", "Big3.md", "Big4.md", "Big5.md"} {
		files[n] = strings.Repeat("w", 4000) + "\n"
	}
	root, notes := vaultOf(t, files)
	e := VaultExport{Root: root, Notes: notes, Dest: t.TempDir(), Format: FormatHTML, SingleFile: true,
		Options: Options{MaxCombinedChars: 5000}}
	var refusal *Refusal
	if _, err := Vault(e); !errors.As(err, &refusal) {
		t.Errorf("over the budget: %v", err)
	}
	e.Options.MaxCombinedChars = 0
	if out, err := Vault(e); err != nil || len(out) != 1 {
		t.Errorf("within the budget: %v", err)
	}
}

// An image over the attachment budget is left out of a vault export
// (testOversizedAttachmentIsSkippedNotInlined).
func TestVaultAttachmentBudget(t *testing.T) {
	big := strings.Repeat("Z", 200*1024)
	root, notes := vaultOf(t, map[string]string{"WithImage.md": "# With image\n\n![](big.png)\n", "big.png": big})
	enc := base64.StdEncoding.EncodeToString([]byte(big))
	files, _ := Vault(VaultExport{Root: root, Notes: notes, Dest: t.TempDir(), Format: FormatHTML})
	if !strings.Contains(string(files[0].Content), enc) {
		t.Error("an image under the budget was not embedded")
	}
	files, _ = Vault(VaultExport{Root: root, Notes: notes, Dest: t.TempDir(), Format: FormatHTML,
		Options: Options{MaxAttachmentBytes: 1024}})
	if strings.Contains(string(files[0].Content), enc) || !strings.Contains(string(files[0].Content), "With image") {
		t.Error("an image over the budget was embedded")
	}
}
