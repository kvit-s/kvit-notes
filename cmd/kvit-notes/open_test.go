package main

import (
	"os"
	"path/filepath"
	"slices"
	"testing"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
)

// features.md 15.3: what the running app is asked to open, by a copy
// started later or by macOS's Finder and Dock, goes where a path given on
// the command line goes: a folder opens as a vault, once; a Markdown file
// opens on its own; a path that is not there opens nothing.
func TestPathsOpenAsOnTheCommandLine(t *testing.T) {
	root := t.TempDir()
	if err := os.WriteFile(filepath.Join(root, "Alpha.md"), []byte("Alpha\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	loose := filepath.Join(t.TempDir(), "Loose.md")
	if err := os.WriteFile(loose, []byte("Loose note\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	ui, err := kvitui.New(kvitui.Options{IgnoreDesktop: true})
	if err != nil {
		t.Fatal(err)
	}
	screen, err := unison.StartHeadless(unison.HeadlessConfig{Width: 1100, Height: 720})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(screen.Stop)
	titles := func() []string {
		var out []string
		screen.Do(func() {
			for _, w := range unison.Windows() {
				out = append(out, w.Title())
			}
		})
		slices.Sort(out)
		return out
	}
	open := func(paths ...string) {
		screen.Do(func() { openPaths(ui, paths) })
		screen.Sync()
	}

	vaultTitle := filepath.Base(root) + " — Kvit Notes"
	open(root, loose)
	want := []string{"Loose.md — Kvit Notes", vaultTitle}
	slices.Sort(want)
	if got := titles(); !slices.Equal(got, want) {
		t.Fatalf("after opening the folder and the file: windows %q, want %q", got, want)
	}
	// The vault again, and a path that is not there: no new window, and
	// the vault's window comes forward.
	open(root, filepath.Join(root, "Missing.md"))
	if got := titles(); !slices.Equal(got, want) {
		t.Errorf("opening the vault again made a window: %q", got)
	}
	var front string
	screen.Do(func() {
		if w := screen.FocusedWindow(); w != nil {
			front = w.Title()
		}
	})
	if front != vaultTitle {
		t.Errorf("the vault's window should be brought forward, %q is in front", front)
	}
}
