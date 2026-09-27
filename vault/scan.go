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
// symbolic links, and leaving out hidden entries and the vault's own
// directories.
func scan(root string) (notes []Note, folders []Folder, err error) {
	err = filepath.WalkDir(root, func(p string, d fs.DirEntry, walkErr error) error {
		if walkErr != nil {
			if p == root {
				return walkErr
			}
			return nil
		}
		if p == root {
			return nil
		}
		rel, _ := filepath.Rel(root, p)
		rel = filepath.ToSlash(rel)
		name := d.Name()
		if strings.HasPrefix(name, ".") {
			if d.IsDir() {
				return filepath.SkipDir
			}
			return nil
		}
		if d.Type()&fs.ModeSymlink != 0 {
			return nil
		}
		if d.IsDir() {
			if !strings.Contains(rel, "/") && controlDirs[name] {
				return filepath.SkipDir
			}
			folders = append(folders, Folder{Path: rel, Name: name})
			return nil
		}
		if !strings.EqualFold(path.Ext(name), ".md") || !d.Type().IsRegular() {
			return nil
		}
		info, err := d.Info()
		if err != nil {
			return nil
		}
		folder := path.Dir(rel)
		if folder == "." {
			folder = ""
		}
		notes = append(notes, Note{Path: rel, Title: strings.TrimSuffix(name, path.Ext(name)), Folder: folder,
			Modified: info.ModTime(), Size: info.Size()})
		return nil
	})
	sort.Slice(folders, func(a, b int) bool { return folders[a].Path < folders[b].Path })
	return notes, folders, err
}

// abs is a vault path as a path on this machine.
func (v *Vault) abs(rel string) string { return filepath.Join(v.Root, filepath.FromSlash(rel)) }

// exists reports whether a vault path exists.
func (v *Vault) exists(rel string) bool {
	_, err := os.Lstat(v.abs(rel))
	return err == nil
}
