package vault

import (
	"errors"
	"slices"
	"strings"
	"testing"
	"time"
)

func TestTemplatesAreSeededOnceAndFilledIn(t *testing.T) {
	root := t.TempDir()
	write(t, root, "Note.md", "x\n")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	v.SeedTemplates()
	if got := v.TemplateNames(); !slices.Equal(got, []string{"Daily Journal", "Meeting Notes", "Project Plan"}) {
		t.Fatalf("seeded: %q", got)
	}
	if err := v.DeleteTemplate("Project Plan"); err != nil {
		t.Fatal(err)
	}
	v.SeedTemplates()
	if got := v.TemplateNames(); len(got) != 2 {
		t.Errorf("seeding must never add to templates the reader has: %q", got)
	}
	now := time.Date(2026, 9, 27, 14, 5, 0, 0, time.Local)
	inst, err := v.Instantiate("Meeting Notes", "Standup", now)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(inst.Body, "# Standup\n\n**Date:** 2026-09-27  \n**Time:** 14:05\n") {
		t.Errorf("meeting body: %q", inst.Body)
	}
	if !slices.Equal(inst.Tags, []string{"meeting"}) || inst.Favorite {
		t.Errorf("meeting tags %q favourite %v", inst.Tags, inst.Favorite)
	}
	inst, _ = v.Instantiate("Daily Journal", "x", now)
	if !strings.HasPrefix(inst.Body, "# Sunday, September 27, 2026\n") {
		t.Errorf("journal heading: %q", inst.Body)
	}
	for _, bad := range []string{"", " ", ".", "..", "a/b", `a\b`, "a:b", "a\x01b"} {
		if err := v.WriteTemplate(bad, "x"); !errors.Is(err, ErrName) {
			t.Errorf("%q should not be a template name: %v", bad, err)
		}
	}
	if err := v.WriteTemplate("  Mine  ", "---\nfavorite: true\n---\n{{title}} {{unknown}} {{ time:HH 'h' mm}} {{ Title }}"); err != nil {
		t.Fatal(err)
	}
	inst, _ = v.Instantiate("Mine", "T", now)
	if inst.Body != "T {{unknown}} 14 h 05 T" || !inst.Favorite {
		t.Errorf("own template: %q favourite %v", inst.Body, inst.Favorite)
	}
}

func TestQtDateFormats(t *testing.T) {
	at := time.Date(2026, 3, 7, 9, 4, 5, 0, time.UTC)
	for format, want := range map[string]string{
		"yyyy-MM-dd":         "2026-03-07",
		"d/M/yy":             "7/3/26",
		"ddd MMM d":          "Sat Mar 7",
		"dddd, MMMM d, yyyy": "Saturday, March 7, 2026",
		"h:mm AP":            "9:04 AM",
		"HH:mm:ss":           "09:04:05",
		"'week' d":           "week 7",
		"''d''":              "'7'",
	} {
		if got := QtTimeFormat(at, format); got != want {
			t.Errorf("%q: %q, want %q", format, got, want)
		}
	}
}

func TestCreateTitledTakesTheNextFreeName(t *testing.T) {
	root := t.TempDir()
	write(t, root, "Meeting Notes.md", "x\n")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	e, err := v.CreateTitled("", "Meeting Notes")
	if err != nil || e.Path != "Meeting Notes 2.md" {
		t.Fatalf("created %v: %v", e, err)
	}
}

func TestPictureFolders(t *testing.T) {
	root := t.TempDir()
	write(t, root, "hugo.toml", "")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	if v.SiteFolder() != "static" || v.ImageFolder() != "static/images" || v.Pictures.DetectedFrom != "hugo.toml" {
		t.Errorf("a Hugo site: site %q images %q from %q", v.SiteFolder(), v.ImageFolder(), v.Pictures.DetectedFrom)
	}
	site, image := "./public/", `media\pics`
	if err := v.SetPictureFolders(&site, &image); err != nil {
		t.Fatal(err)
	}
	if got := read(t, root, ".kvit/settings.json"); got != "{\n    \"imageFolder\": \"media/pics\",\n    \"siteFolder\": \"public\"\n}\n" {
		t.Errorf("settings file: %q", got)
	}
	v.Close()
	v, _ = Open(root)
	if v.SiteFolder() != "public" || v.ImageFolder() != "media/pics" {
		t.Errorf("read back: %q %q", v.SiteFolder(), v.ImageFolder())
	}
	for _, bad := range []string{"../out", "/abs", ".kvit/x", "a/../b", "C:/x"} {
		if _, ok := NormalizeFolder(bad); ok {
			t.Errorf("%q should not be a picture folder", bad)
		}
	}
	if err := v.SetPictureFolders(nil, nil); err != nil {
		t.Fatal(err)
	}
	if v.SiteFolder() != "static" || v.ImageFolder() != "static/images" {
		t.Errorf("reset: %q %q", v.SiteFolder(), v.ImageFolder())
	}
}

func TestTagsAreRenamedMergedAndDeleted(t *testing.T) {
	root := t.TempDir()
	write(t, root, "A.md", "---\ntags: [books, draft]\ntitle: kept\n---\nA\n")
	write(t, root, "B.md", "---\ntags: [reading]\n---\nB\n")
	write(t, root, "C.md", "---\ntags: [books, reading]\n---\nC\n")
	write(t, root, ".kvit/collection.json", `{"tagColors":{"books":"#e05c5c"}}`)
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	if err := v.RenameTag("books", "reading"); err != nil {
		t.Fatal(err)
	}
	if got := read(t, root, "A.md"); got != "---\ntags: [reading, draft]\ntitle: kept\n---\nA\n" {
		t.Errorf("renamed: %q", got)
	}
	if got := read(t, root, "C.md"); got != "---\ntags: [reading]\n---\nC\n" {
		t.Errorf("merged: %q", got)
	}
	if v.State.TagColors["reading"] != "#e05c5c" || v.State.TagColors["books"] != "" {
		t.Errorf("the colour should follow the name: %v", v.State.TagColors)
	}
	if err := v.DeleteTag("reading"); err != nil {
		t.Fatal(err)
	}
	if got := read(t, root, "B.md"); got != "B\n" {
		t.Errorf("deleting the last tag should drop the front matter: %q", got)
	}
	if v.TagCount("reading") != 0 || v.TagCount("draft") != 1 {
		t.Errorf("counts after delete")
	}
}

func TestManualOrder(t *testing.T) {
	root := t.TempDir()
	write(t, root, "F/Apricot.md", "---\ncreated: 2026-01-02\n---\na\n")
	write(t, root, "F/Blueberry.md", "---\ncreated: 2026-01-01\n---\nb\n")
	write(t, root, "F/Citrus.md", "---\ncreated: 2026-01-03\n---\nc\n")
	write(t, root, ".kvit/collection.json", `{"manualOrder":{"F":["Citrus.md","Gone.md"]}}`)
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	titles := func() []string {
		var out []string
		for _, e := range v.ManualOrder("F") {
			out = append(out, e.Title)
		}
		return out
	}
	if got := titles(); !slices.Equal(got, []string{"Citrus", "Blueberry", "Apricot"}) {
		t.Errorf("listed first, then oldest first: %q", got)
	}
	if err := v.SetManualPosition(v.Find("F/Apricot.md"), 0); err != nil {
		t.Fatal(err)
	}
	if got := titles(); !slices.Equal(got, []string{"Apricot", "Citrus", "Blueberry"}) {
		t.Errorf("after moving Apricot first: %q", got)
	}
	if !strings.Contains(read(t, root, ".kvit/collection.json"), `"Apricot.md",`) {
		t.Errorf("the order should be kept: %s", read(t, root, ".kvit/collection.json"))
	}
}

func TestTheScanFollowsGitignore(t *testing.T) {
	root := t.TempDir()
	write(t, root, ".gitignore", "build/\n*.draft.md\n")
	write(t, root, "Note.md", "x\n")
	write(t, root, "Plan.draft.md", "x\n")
	write(t, root, "build/Out.md", "x\n")
	write(t, root, "docs/.gitignore", "private.md\n")
	write(t, root, "docs/private.md", "x\n")
	write(t, root, "docs/public.md", "x\n")
	v, err := Open(root)
	if err != nil {
		t.Fatal(err)
	}
	defer v.Close()
	var paths []string
	for _, e := range v.Entries {
		paths = append(paths, e.Path)
	}
	if !slices.Equal(paths, []string{"Note.md", "docs/public.md"}) {
		t.Errorf("scanned %q", paths)
	}
	for _, f := range v.Folders {
		if f.Path == "build" {
			t.Errorf("an ignored folder should not be listed")
		}
	}
}
