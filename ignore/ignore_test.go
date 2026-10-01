package ignore

import (
	"encoding/json"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"slices"
	"strings"
	"testing"
)

// The rules, checked on their own and through the walks that use them. This
// package cannot import the vault scan or the file watcher, so three tests
// run their files through scanVault and discoverDirectories below, which
// repeat those walks in the parts that use the rules.

func writeFile(t *testing.T, file, contents string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(file), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(file, []byte(contents), 0o644); err != nil {
		t.Fatal(err)
	}
}

func makeDir(t *testing.T, dir string) {
	t.Helper()
	if err := os.MkdirAll(dir, 0o755); err != nil {
		t.Fatal(err)
	}
}

// listing is what scanVault finds: the notes and the folders, as paths
// relative to the vault folder.
type listing struct {
	notes, folders []string
}

// scanVault walks the vault as VaultScan's scanDir does: entries in name
// order, symbolic links and names starting with "." skipped, each entry
// tested against the rules before it is used, and each folder entered
// with WithDirectory. Files ending in ".md" are notes.
func scanVault(t *testing.T, rules *Rules) listing {
	t.Helper()
	var result listing
	var scanDir func(relDir string, snapshot Snapshot)
	scanDir = func(relDir string, snapshot Snapshot) {
		entries, err := os.ReadDir(filepath.Join(rules.RootPath(), filepath.FromSlash(relDir)))
		if err != nil {
			t.Fatal(err)
		}
		for _, entry := range entries {
			name := entry.Name()
			relPath := path.Join(relDir, name)
			if strings.HasPrefix(name, ".") || entry.Type()&os.ModeSymlink != 0 {
				continue
			}
			if snapshot.IsExcluded(relPath, entry.IsDir()) {
				continue
			}
			if entry.IsDir() {
				result.folders = append(result.folders, relPath)
				scanDir(relPath, snapshot.WithDirectory(relPath))
				continue
			}
			if strings.HasSuffix(strings.ToLower(name), ".md") {
				result.notes = append(result.notes, relPath)
			}
		}
	}
	scanDir("", rules.Snapshot())
	return result
}

// resolveWikiTarget finds the note a [[target]] link without a folder
// names: the one whose file name without ".md" is target, ignoring case.
func (l listing) resolveWikiTarget(target string) string {
	for _, note := range l.notes {
		if strings.EqualFold(strings.TrimSuffix(path.Base(note), ".md"), target) {
			return note
		}
	}
	return ""
}

// discovery is what discoverDirectories finds: the folders a watcher would
// watch, the ignore files it would watch for changes, and every folder it
// listed.
type discovery struct {
	watchedDirectories []string
	watchedFiles       []string
	opened             []string
}

// discoverDirectories walks the vault's folders as the file watcher does: the
// vault folder and git's exclude file first, then breadth first, watching
// each folder's .gitignore when it exists and every child folder that is not
// a link, does not start with "." and is not excluded.
func discoverDirectories(t *testing.T, rules *Rules) discovery {
	t.Helper()
	type dir struct {
		absolutePath, relativePath string
		rules                      Snapshot
	}
	root := rules.RootPath()
	var result discovery
	exists := func(file string) bool {
		_, err := os.Stat(filepath.FromSlash(file))
		return err == nil
	}
	result.watchedDirectories = append(result.watchedDirectories, root)
	snapshot := rules.Snapshot()
	if infoExclude := snapshot.GitInfoExcludePath(); infoExclude != "" && exists(infoExclude) {
		result.watchedFiles = append(result.watchedFiles, infoExclude)
	}
	queue := []dir{{root, "", snapshot}}
	for len(queue) > 0 {
		current := queue[0]
		queue = queue[1:]
		if ignoreFile := current.rules.IgnoreFileForDirectory(current.relativePath); ignoreFile != "" && exists(ignoreFile) {
			result.watchedFiles = append(result.watchedFiles, ignoreFile)
		}
		result.opened = append(result.opened, current.relativePath)
		entries, err := os.ReadDir(filepath.FromSlash(current.absolutePath))
		if err != nil {
			t.Fatal(err)
		}
		for _, entry := range entries {
			name := entry.Name()
			if !entry.IsDir() || strings.HasPrefix(name, ".") {
				continue
			}
			relPath := path.Join(current.relativePath, name)
			if current.rules.IsExcluded(relPath, true) {
				continue
			}
			absPath := current.absolutePath + "/" + name
			result.watchedDirectories = append(result.watchedDirectories, absPath)
			queue = append(queue, dir{absPath, relPath, current.rules.WithDirectory(relPath)})
		}
	}
	return result
}

// isIgnoredPath reports whether the file watcher drops a change the system
// reports at absolutePath before it reaches the refresh.
func isIgnoredPath(rules *Rules, absolutePath string, isDirectory bool) bool {
	relative, err := filepath.Rel(filepath.FromSlash(rules.RootPath()), absolutePath)
	if err != nil {
		return false
	}
	relative = filepath.ToSlash(relative)
	if relative == "." || relative == ".." || strings.HasPrefix(relative, "../") {
		return false
	}
	return rules.IsExcluded(relative, isDirectory)
}

func TestGitPatternsAndNestedNegation(t *testing.T) {
	root := t.TempDir()
	writeFile(t, filepath.Join(root, ".gitignore"),
		"build/\n"+
			"*.tmp\n"+
			"!important.tmp\n"+
			"/docs/generated/*.md\n"+
			"logs/**/debug.log\n")
	writeFile(t, filepath.Join(root, "src/.gitignore"), "*.md\n!keep.md\n")

	rules := New(root, nil)
	atRoot := rules.Snapshot()

	cases := []struct {
		snapshot    Snapshot
		path        string
		isDirectory bool
		want        bool
	}{
		{atRoot, "build", true, true},
		{atRoot, "build/generated/note.md", false, true},
		{atRoot, "scratch.tmp", false, true},
		{atRoot, "important.tmp", false, false},
		{atRoot, "docs/generated/api.md", false, true},
		{atRoot, "src/docs/generated/api.md", false, false},
		{atRoot, "logs/a/b/debug.log", false, true},
	}
	inSource := atRoot.WithDirectory("src")
	cases = append(cases, []struct {
		snapshot    Snapshot
		path        string
		isDirectory bool
		want        bool
	}{
		{inSource, "src/readme.md", false, true},
		{inSource, "src/keep.md", false, false},
		{inSource, "other/readme.md", false, false},
	}...)
	for _, c := range cases {
		if got := c.snapshot.IsExcluded(c.path, c.isDirectory); got != c.want {
			t.Errorf("IsExcluded(%q, %v) = %v, want %v", c.path, c.isDirectory, got, c.want)
		}
	}
}

// Git's exclude file and the settings list apply only to their own vault.
// The settings are a JSON object, written to settings.json in a folder of
// its own and read back.
func TestInfoExcludeAndSettingsAreRootSpecific(t *testing.T) {
	root := t.TempDir()
	other := t.TempDir()
	config := t.TempDir()
	writeFile(t, filepath.Join(root, ".git/info/exclude"), "vendor/\n")

	settingsFile := filepath.Join(config, "settings.json")
	saveSettings := func(settings map[string]any) {
		data, err := json.Marshal(settings)
		if err != nil {
			t.Fatal(err)
		}
		writeFile(t, settingsFile, string(data))
	}
	loadSettings := func() map[string]any {
		data, err := os.ReadFile(settingsFile)
		if err != nil {
			t.Fatal(err)
		}
		var settings map[string]any
		if err := json.Unmarshal(data, &settings); err != nil {
			t.Fatal(err)
		}
		return settings
	}
	saveSettings(map[string]any{})

	settings := loadSettings()
	rules := New(root, LoadAdditionalPatterns(settings[SettingsKey], root))
	if rules.SetAdditionalPatterns([]string{"node_modules/", "dist/**"}) {
		settings[SettingsKey] = StoreAdditionalPatterns(settings[SettingsKey], rules.RootPath(), rules.AdditionalPatterns())
		saveSettings(settings)
	}

	snapshot := rules.Snapshot()
	for _, p := range []string{"vendor/package/a.md", "node_modules/pkg/README.md", "dist/app/main.js"} {
		if !snapshot.IsExcluded(p, false) {
			t.Errorf("IsExcluded(%q) = false", p)
		}
	}

	settings = loadSettings()
	reloaded := New(root, LoadAdditionalPatterns(settings[SettingsKey], root))
	if got, want := reloaded.AdditionalPatterns(), rules.AdditionalPatterns(); !slices.Equal(got, want) {
		t.Errorf("reloaded patterns %q, want %q", got, want)
	}

	reloaded.SetRootPath(other, LoadAdditionalPatterns(settings[SettingsKey], other))
	if got := reloaded.AdditionalPatterns(); len(got) != 0 {
		t.Errorf("patterns for the other folder %q, want none", got)
	}
	if reloaded.Snapshot().IsExcluded("node_modules/pkg/README.md", false) {
		t.Error("the other folder excludes node_modules/pkg/README.md")
	}
}

// NoteCollection's note list, note count, wiki-link resolution, note
// lookup and folder list are all built from the scan, so here they are
// checked on scanVault's listing.
func TestCollectionExcludesRulesFromEveryDerivedIndex(t *testing.T) {
	root := t.TempDir()
	writeFile(t, filepath.Join(root, ".gitignore"), "node_modules/\nbuild/\n")
	writeFile(t, filepath.Join(root, "Keep.md"), "# Keep\n\n[[README]]\n")
	writeFile(t, filepath.Join(root, "node_modules/pkg/README.md"), "# Dependency documentation\nneedle from a package\n")
	writeFile(t, filepath.Join(root, "build/Generated.md"), "# Generated\n")
	writeFile(t, filepath.Join(root, "private/Secret.md"), "# Secret\n")

	rules := New(root, nil)
	rules.SetAdditionalPatterns([]string{"private/"})
	collection := scanVault(t, rules)

	if want := []string{"Keep.md"}; !slices.Equal(collection.notes, want) {
		t.Errorf("notes %q, want %q", collection.notes, want)
	}
	if len(collection.notes) != 1 {
		t.Errorf("note count %d, want 1", len(collection.notes))
	}
	if got := collection.resolveWikiTarget("README"); got != "" {
		t.Errorf("[[README]] resolves to %q", got)
	}
	if slices.Contains(collection.notes, "build/Generated.md") {
		t.Error("build/Generated.md is a note")
	}
	if slices.Contains(collection.folders, "node_modules") {
		t.Error("node_modules is a folder")
	}
}

// NoteCollection rescans when IgnoreRules emits rulesChanged. Here that is
// SetAdditionalPatterns reporting a change and Revision going up, and the
// rescan is a second scanVault.
func TestChangingSettingsRescansAnOpenCollection(t *testing.T) {
	root := t.TempDir()
	writeFile(t, filepath.Join(root, "Keep.md"), "# Keep\n")
	writeFile(t, filepath.Join(root, "drafts/Draft.md"), "# Draft\n")

	rules := New(root, nil)
	if got := len(scanVault(t, rules).notes); got != 2 {
		t.Fatalf("note count %d, want 2", got)
	}

	revision := rules.Revision()
	if !rules.SetAdditionalPatterns([]string{"drafts/"}) || rules.Revision() == revision {
		t.Fatal("changing the patterns did not report a change")
	}
	if got, want := scanVault(t, rules).notes, []string{"Keep.md"}; !slices.Equal(got, want) {
		t.Errorf("notes %q, want %q", got, want)
	}
}

// The watcher never lists an excluded folder, watches the .gitignore, and
// drops a change inside an excluded folder (isIgnoredPath).
// discoverDirectories registers no watches, so a watch the system refuses
// is not checked here.
func TestWatcherNeverEntersIgnoredDirectories(t *testing.T) {
	root := t.TempDir()
	writeFile(t, filepath.Join(root, ".gitignore"), "build/\n")
	makeDir(t, filepath.Join(root, "visible"))
	for i := range 600 {
		makeDir(t, filepath.Join(root, fmt.Sprintf("build/generated-%d/nested", i)))
	}

	rules := New(root, nil)
	watcher := discoverDirectories(t, rules)

	if got := len(watcher.watchedDirectories); got != 2 { // root + visible
		t.Errorf("watched folders %q, want the root and visible", watcher.watchedDirectories)
	}
	if want := []string{"", "visible"}; !slices.Equal(watcher.opened, want) {
		t.Errorf("folders listed %q, want %q", watcher.opened, want)
	}
	gitignore := rules.RootPath() + "/.gitignore"
	if !slices.Contains(watcher.watchedFiles, gitignore) {
		t.Errorf("watched files %q do not include %q", watcher.watchedFiles, gitignore)
	}

	build := filepath.Join(root, "build")
	if rules.IsRulesFile(build) {
		t.Errorf("IsRulesFile(%q) = true", build)
	}
	if !isIgnoredPath(rules, build, true) {
		t.Errorf("a change at %q reaches the refresh", build)
	}

	revision := rules.Revision()
	rules.SetAdditionalPatterns([]string{"visible/"})
	if rules.Revision() == revision {
		t.Fatal("changing the patterns did not report a change")
	}
	if got := len(discoverDirectories(t, rules).watchedDirectories); got != 1 {
		t.Errorf("watched folder count %d, want 1", got)
	}
}

func TestEmptyPolicyPreservesTheExistingWalk(t *testing.T) {
	root := t.TempDir()
	writeFile(t, filepath.Join(root, "One.md"), "# One\n")
	writeFile(t, filepath.Join(root, "folder/Two.md"), "# Two\n")

	rules := New(root, nil)
	collection := scanVault(t, rules)
	if want := []string{"One.md", "folder/Two.md"}; !slices.Equal(collection.notes, want) {
		t.Errorf("notes %q, want %q", collection.notes, want)
	}
	if want := []string{"folder"}; !slices.Equal(collection.folders, want) {
		t.Errorf("folders %q, want %q", collection.folders, want)
	}

	// .kvit is the app's own folder and is skipped; everything else is the
	// same vault folder and folder watch as a walk without any rules. The
	// .kvit folder is made here so the walk has one to skip.
	makeDir(t, filepath.Join(root, ".kvit"))
	if got := len(discoverDirectories(t, rules).watchedDirectories); got != 2 {
		t.Errorf("watched folder count %d, want 2", got)
	}
}
