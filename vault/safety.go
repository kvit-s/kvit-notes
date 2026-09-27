package vault

// Keeping work safe, in the Qt app's formats: the crash-recovery journal in
// .kvit/recovery (src/repository/recoveryjournalstore.cpp), the backups in
// .kvit/backups (notebackupstore.cpp), and the trash in .kvit/trash
// (notetrashstore.cpp).

import (
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"
)

// The recovery journal holds the whole text of a note with unsaved changes,
// written a moment after each change and removed once the note is saved.
// A journal still there when a vault opens is what an interrupted session
// left: the reader is offered it back.

// journalName is a note path as the Qt app names its journal file: every
// byte percent-encoded except the unreserved characters, as
// QUrl::toPercentEncoding does, so the directory stays flat.
func journalName(rel string) string {
	var b strings.Builder
	for _, c := range []byte(rel) {
		switch {
		case c >= 'A' && c <= 'Z', c >= 'a' && c <= 'z', c >= '0' && c <= '9', c == '-', c == '.', c == '_', c == '~':
			b.WriteByte(c)
		default:
			fmt.Fprintf(&b, "%%%02X", c)
		}
	}
	return b.String()
}

// journalPath decodes a journal file's name back to a note path, or "" for
// a name the Qt app would not have written: it must encode back to itself
// and decode to a plain relative path ending in .md.
func journalPath(name string) string {
	var b []byte
	for i := 0; i < len(name); i++ {
		if name[i] == '%' && i+2 < len(name) {
			var c byte
			if _, err := fmt.Sscanf(name[i+1:i+3], "%02X", &c); err != nil {
				return ""
			}
			b = append(b, c)
			i += 2
			continue
		}
		b = append(b, name[i])
	}
	rel := string(b)
	if journalName(rel) != name || !plainRelative(rel) || !strings.HasSuffix(strings.ToLower(rel), ".md") {
		return ""
	}
	return rel
}

// plainRelative reports whether a path is a plain relative path: not empty,
// not absolute, no backslashes, and no empty, "." or ".." element.
func plainRelative(rel string) bool {
	if rel == "" || strings.HasPrefix(rel, "/") || strings.Contains(rel, `\`) {
		return false
	}
	for _, e := range strings.Split(rel, "/") {
		if e == "" || e == "." || e == ".." {
			return false
		}
	}
	return true
}

func (v *Vault) journalDir() string { return filepath.Join(v.Root, ".kvit", "recovery") }

// WriteJournal keeps a note's unsaved text in the recovery journal.
func (v *Vault) WriteJournal(rel, text string) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	if err := os.MkdirAll(v.journalDir(), 0o755); err != nil {
		return err
	}
	return writeAtomic(filepath.Join(v.journalDir(), journalName(rel)), []byte(text))
}

// ClearJournal removes a note's journal once its changes are saved or
// thrown away.
func (v *Vault) ClearJournal(rel string) {
	_ = os.Remove(filepath.Join(v.journalDir(), journalName(rel)))
}

// Recovered is a note's unsaved text that an interrupted session left in
// the journal.
type Recovered struct {
	Path string
	Text string
}

// Journals are the unsaved changes an interrupted session left, for notes
// that still exist.
func (v *Vault) Journals() []Recovered {
	entries, err := os.ReadDir(v.journalDir())
	if err != nil {
		return nil
	}
	var out []Recovered
	for _, de := range entries {
		rel := journalPath(de.Name())
		if rel == "" || de.Type()&os.ModeSymlink != 0 || !de.Type().IsRegular() {
			continue
		}
		data, err := os.ReadFile(filepath.Join(v.journalDir(), de.Name()))
		if err != nil {
			continue
		}
		out = append(out, Recovered{Path: rel, Text: string(data)})
	}
	sort.Slice(out, func(a, b int) bool { return out[a].Path < out[b].Path })
	return out
}

// Restore replaces a note's text with a recovered or earlier version,
// through a save, so the version it replaces is backed up first.
func (v *Vault) Restore(e *Entry, text string) (*Page, error) {
	p := parsePage(text)
	current, err := v.Load(e.Path)
	if err == nil {
		p.reshapes, p.loaded = current.reshapes, current.loaded
	}
	if err := v.Save(e, p); err != nil {
		return nil, err
	}
	return p, nil
}

// Backup is one timed copy of a note kept before a save.
type Backup struct {
	Time time.Time
	path string
}

// Backups are a note's backups, newest first.
func (v *Vault) Backups(rel string) []Backup {
	names, _ := filepath.Glob(filepath.Join(v.Root, ".kvit", "backups", filepath.FromSlash(rel), "*.md"))
	var out []Backup
	for _, n := range names {
		t, err := time.ParseInLocation(stampLayout, strings.TrimSuffix(filepath.Base(n), ".md"), time.Local)
		if err == nil {
			out = append(out, Backup{Time: t, path: n})
		}
	}
	sort.Slice(out, func(a, b int) bool { return out[a].Time.After(out[b].Time) })
	return out
}

// Read is a backup's text.
func (b Backup) Read() (string, error) {
	data, err := os.ReadFile(b.path)
	return string(data), err
}

// Trashed is one note or folder in the trash.
type Trashed struct {
	// Name is its name in the trash: the time it went there, and its own
	// name.
	Name string
	// Title is the note's title or the folder's name.
	Title string
	Time  time.Time
	Dir   bool
}

var reTrashName = regexp.MustCompile(`^(\d{8}-\d{6})(?:-(\d+))?-(.+)$`)

func (v *Vault) trashDir() string { return filepath.Join(v.Root, ".kvit", "trash") }

// Trash lists what is in the trash, the most recent first.
func (v *Vault) TrashItems() []Trashed {
	entries, _ := os.ReadDir(v.trashDir())
	var out []Trashed
	for _, de := range entries {
		t := Trashed{Name: de.Name(), Title: de.Name(), Dir: de.IsDir()}
		if m := reTrashName.FindStringSubmatch(de.Name()); m != nil {
			t.Time, _ = time.ParseInLocation(stampLayout, m[1], time.Local)
			t.Title = m[3]
		}
		if !t.Dir {
			t.Title = strings.TrimSuffix(t.Title, path.Ext(t.Title))
		}
		out = append(out, t)
	}
	sort.Slice(out, func(a, b int) bool { return out[a].Name > out[b].Name })
	return out
}

// ReadTrashed is a trashed note's text.
func (v *Vault) ReadTrashed(t Trashed) (string, error) {
	if t.Dir {
		return "", errors.New("a folder has no text")
	}
	data, err := os.ReadFile(filepath.Join(v.trashDir(), t.Name))
	return string(data), err
}

// Untrash puts a trashed note or folder back at the top of the vault (the
// trash does not record where it was), under its own name, or its name with
// " 2", " 3" when that is taken. It returns its path.
func (v *Vault) Untrash(t Trashed) (string, error) {
	if v.ReadOnly {
		return "", ErrReadOnly
	}
	base := t.Title
	ext := ""
	if !t.Dir {
		ext = ".md"
	}
	rel := base + ext
	for n := 2; v.exists(rel); n++ {
		rel = fmt.Sprintf("%s %d%s", base, n, ext)
	}
	if err := os.Rename(filepath.Join(v.trashDir(), t.Name), v.abs(rel)); err != nil {
		return "", err
	}
	return rel, v.Rescan()
}

// DeleteForever removes one item from the trash.
func (v *Vault) DeleteForever(t Trashed) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	if strings.ContainsAny(t.Name, `/\`) || t.Name == "" || t.Name == "." || t.Name == ".." {
		return ErrName
	}
	return os.RemoveAll(filepath.Join(v.trashDir(), t.Name))
}

// EmptyTrash removes everything in the trash.
func (v *Vault) EmptyTrash() error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	return os.RemoveAll(v.trashDir())
}
