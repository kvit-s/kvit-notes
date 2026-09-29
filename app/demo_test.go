package app

import (
	"os"
	"path/filepath"
	"slices"
	"testing"
)

// TestTheQtDemoVault opens a copy of the repository's demo vault, when
// it is on this machine, and draws each note: a check that notes written by
// the app open and draw without failing.
func TestTheQtDemoVault(t *testing.T) {
	src := filepath.Join(os.Getenv("HOME"), "kvit-notes", "screenshots", "demo-vault")
	if _, err := os.Stat(src); err != nil {
		t.Skip("the reference demo vault is not on this machine")
	}
	n := notes{}
	_ = filepath.WalkDir(src, func(p string, d os.DirEntry, err error) error {
		if err != nil || d.IsDir() {
			return nil
		}
		rel, _ := filepath.Rel(src, p)
		data, _ := os.ReadFile(p)
		n[filepath.ToSlash(rel)] = string(data)
		return nil
	})
	s := openVault(t, n)
	for _, title := range s.listed() {
		s.clickRow(slices.Index(s.listed(), title))
		if s.openTitle() != title {
			t.Errorf("%s did not open", title)
		}
		s.shot("demo_" + title + ".png")
	}
	for rel, text := range n {
		if got := s.file(rel); got != text {
			t.Errorf("opening %s changed it", rel)
		}
	}
}
