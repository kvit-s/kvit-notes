// Package vault is a Kvit Notes vault on disk: a folder of Markdown notes,
// with Kvit's own state in a .kvit directory beside them. It reads and writes
// the same files in the same formats as the Qt app, so the two can be used on
// one vault in turn, and takes the same lock, so they never have it open at
// once.
package vault

import (
	"io/fs"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/ignore"
)

// Note is one note of a vault, as the note list shows it.
type Note struct {
	// Path is the note's path in the vault, with forward slashes:
	// "Ideas/Reading list.md".
	Path string
	// Title is the file name without ".md".
	Title string
	// Folder is the path of the folder holding it, "" for the top.
	Folder   string
	Modified time.Time
	Size     int64
}

// Folder is one folder of a vault.
type Folder struct {
	// Path is the folder's path in the vault, with forward slashes.
	Path string
	// Name is its last element.
	Name string
}

// controlDirs are the vault's own directories, which are not note folders.
var controlDirs = map[string]bool{".kvit": true, "assets": true}

// scan walks the note tree: every .md file and every folder, not following
// symbolic links, and leaving out hidden entries, the vault's own
// directories, and what the ignore rules exclude (.git/info/exclude, each
// folder's .gitignore, and the patterns set for the vault), as the Qt app's
// scan does.
func scan(root string, rules ignore.Snapshot) (notes []Note, folders []Folder, err error) {
	var walk func(dir string, rules ignore.Snapshot) error
	walk = func(dir string, rules ignore.Snapshot) error {
		entries, err := os.ReadDir(filepath.Join(root, filepath.FromSlash(dir)))
		if err != nil {
			return err
		}
		for _, d := range entries {
			name := d.Name()
			rel := path.Join(dir, name)
			if strings.HasPrefix(name, ".") || hiddenOnDisk(d) || d.Type()&fs.ModeSymlink != 0 {
				continue
			}
			if d.IsDir() {
				if dir == "" && controlDirs[name] || rules.IsExcluded(rel, true) {
					continue
				}
				folders = append(folders, Folder{Path: rel, Name: name})
				_ = walk(rel, rules.WithDirectory(rel))
				continue
			}
			if !strings.EqualFold(path.Ext(name), ".md") || !d.Type().IsRegular() || rules.IsExcluded(rel, false) {
				continue
			}
			info, err := d.Info()
			if err != nil {
				continue
			}
			notes = append(notes, Note{Path: rel, Title: strings.TrimSuffix(name, path.Ext(name)), Folder: dir,
				Modified: info.ModTime(), Size: info.Size()})
		}
		return nil
	}
	err = walk("", rules)
	sort.Slice(notes, func(a, b int) bool { return notes[a].Path < notes[b].Path })
	sort.Slice(folders, func(a, b int) bool { return folders[a].Path < folders[b].Path })
	return notes, folders, err
}

// abs is a vault path as a path on this machine.
func (v *Vault) abs(rel string) string { return filepath.Join(v.Root, filepath.FromSlash(rel)) }

// Path is a note's file, from its path in the vault.
func (v *Vault) Path(rel string) string { return v.abs(rel) }

// exists reports whether a vault path exists.
func (v *Vault) exists(rel string) bool {
	_, err := os.Lstat(v.abs(rel))
	return err == nil
}
