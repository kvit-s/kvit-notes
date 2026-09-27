package vault

import (
	"errors"
	"fmt"
	"github.com/kvit-s/kvit-notes/ignore"
	"os"
	"path"
	"path/filepath"
	"regexp"
	"slices"
	"sort"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
)

// Vault is an open vault.
type Vault struct {
	// Root is the vault's folder, with every link on the way resolved.
	Root string
	// ReadOnly is true for a vault that cannot be written: nothing is
	// changed, and no lock is taken.
	ReadOnly bool
	// State is collection.json.
	State *Collection
	// Entries are the notes, in path order.
	Entries []*Entry
	// Folders are the note folders, in path order.
	Folders []Folder
	// Pictures are where pictures are read from and saved.
	Pictures PictureSettings
	// IgnorePatterns are the patterns set for the vault in the app's
	// settings, which the scan leaves out beside what .gitignore does.
	IgnorePatterns []string

	lock *os.File
	// baked holds the notes given a one-time .md.bak this session.
	baked map[string]bool
	// written is the text of each note as this app last read or wrote it,
	// so a change the watcher reports can be told from its own.
	written map[string]string
}

// Entry is a note with what the note list shows about it.
type Entry struct {
	Note
	Created  time.Time
	Tags     []string
	Pinned   bool
	Favorite bool
	Goal     int
	// Snippet is the start of the note's text as drawn, up to 120
	// characters, and Words its word count, as the Qt app counts them.
	Snippet string
	Words   int
	// Text is the note's text as drawn, which search looks through.
	Text string
}

// maxNote is the largest note whose text is read to list it, as in the Qt
// app; a larger note is listed by its name only.
const maxNote = 32 << 20

// Open opens the vault in root, creating the folder if it is missing. It
// takes the vault lock unless the vault cannot be written, and refuses a
// vault another program has open with ErrLocked.
func Open(root string) (*Vault, error) {
	if err := os.MkdirAll(root, 0o755); err != nil {
		return nil, err
	}
	canon, err := filepath.EvalSymlinks(root)
	if err != nil {
		return nil, err
	}
	canon, _ = filepath.Abs(canon)
	v := &Vault{Root: canon, baked: map[string]bool{}, written: map[string]string{}}
	for _, owned := range []string{".kvit", "assets"} {
		if info, err := os.Lstat(filepath.Join(canon, owned)); err == nil && info.Mode()&os.ModeSymlink != 0 {
			return nil, fmt.Errorf("vault: %s in %s is a link; Kvit refuses a vault whose own directories point elsewhere", owned, canon)
		}
	}
	v.ReadOnly = !writable(canon)
	if !v.ReadOnly {
		if err := os.MkdirAll(filepath.Join(canon, ".kvit"), 0o755); err != nil {
			return nil, err
		}
		if v.lock, err = takeLock(canon); err != nil {
			return nil, err
		}
	}
	v.State = loadCollection(canon)
	v.loadPictureSettings()
	if err := v.Rescan(); err != nil {
		v.Close()
		return nil, err
	}
	return v, nil
}

// Close lets the vault go, and the lock with it. The lock file stays.
func (v *Vault) Close() {
	if v.lock != nil {
		v.lock.Close()
		v.lock = nil
	}
}

// Rescan reads the note tree again.
func (v *Vault) Rescan() error {
	notes, folders, err := scan(v.Root, ignore.New(v.Root, v.IgnorePatterns).Snapshot())
	if err != nil {
		return err
	}
	v.Folders = folders
	// Entries that are still there keep their identity, so a caller holding
	// one, as the window holds the open note, still holds the same note.
	old := make(map[string]*Entry, len(v.Entries))
	for _, e := range v.Entries {
		old[e.Path] = e
	}
	v.Entries = v.Entries[:0]
	for _, n := range notes {
		fresh := v.entry(n)
		if e, ok := old[n.Path]; ok {
			*e = *fresh
			fresh = e
		}
		v.Entries = append(v.Entries, fresh)
	}
	sort.Slice(v.Entries, func(a, b int) bool { return v.Entries[a].Path < v.Entries[b].Path })
	return nil
}

// entry reads a note's front matter and text for the list.
func (v *Vault) entry(n Note) *Entry {
	e := &Entry{Note: n}
	if n.Size > maxNote {
		return e
	}
	data, err := os.ReadFile(v.abs(n.Path))
	if err != nil {
		return e
	}
	v.written[n.Path] = string(data)
	v.fill(e, parsePage(string(data)))
	return e
}

// fill sets an entry's details from its page.
func (v *Vault) fill(e *Entry, p *Page) {
	e.Tags = p.Tags()
	e.Pinned, e.Favorite = p.Pinned(), p.Favorite()
	e.Goal = p.Goal()
	e.Created = p.Created()
	e.Snippet, e.Words, e.Text = editor.Summarize(p.Body)
}

// Find is the entry of a note path, or nil.
func (v *Vault) Find(path string) *Entry {
	for _, e := range v.Entries {
		if e.Path == path {
			return e
		}
	}
	return nil
}

// Load reads a note.
func (v *Vault) Load(path string) (*Page, error) {
	data, err := os.ReadFile(v.abs(path))
	if err != nil {
		return nil, err
	}
	v.written[path] = string(data)
	p := parsePage(string(data))
	// Whether writing the note back unedited would change its Markdown, as
	// it does for Markdown the editor writes in its own form.
	p.reshapes = editor.Serialize(editor.ParseMarkdown(p.Body)) != p.Body
	return p, nil
}

// ErrReadOnly is returned for a change to a vault that cannot be written.
var ErrReadOnly = errors.New("the vault cannot be written")

// Save writes a note. When the editor writes a note's Markdown in its own
// form, so that saving changes lines the reader did not edit, the note as it
// was is kept once beside it as "<note>.md.bak"; and a timed backup is taken
// before any save. Both are the Qt app's. The write is atomic: a temporary file renamed over the
// note, so an interrupted save never leaves a note cut short.
func (v *Vault) Save(e *Entry, p *Page) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	text := p.Text()
	target := v.abs(e.Path)
	current, err := os.ReadFile(target)
	if err == nil && string(current) == text {
		p.raw = text
		return nil
	}
	if err == nil {
		if p.reshapes && p.Body != p.loaded {
			v.keepBak(e.Path, current)
		}
		v.backup(e.Path, current)
	}
	v.written[e.Path] = text
	if err := writeAtomic(target, []byte(text)); err != nil {
		return err
	}
	p.raw = text
	if info, err := os.Stat(target); err == nil {
		e.Modified, e.Size = info.ModTime(), info.Size()
	}
	v.fill(e, p)
	return nil
}

// keepBak writes "<note>.md.bak" beside a note, once: it is never
// overwritten.
func (v *Vault) keepBak(rel string, current []byte) {
	if v.baked[rel] {
		return
	}
	v.baked[rel] = true
	f, err := os.OpenFile(v.abs(rel)+".bak", os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o644)
	if err != nil {
		return
	}
	_, _ = f.Write(current)
	_ = f.Close()
}

// backupEvery and backupsKept are the Qt app's backup rotation: at most one
// backup of a note every ten minutes, the ten newest kept.
const (
	backupEvery = 10 * time.Minute
	backupsKept = 10
	stampLayout = "20060102-150405"
)

// backup keeps a note's current bytes in .kvit/backups/<path>/<stamp>.md.
func (v *Vault) backup(rel string, current []byte) {
	dir := filepath.Join(v.Root, ".kvit", "backups", filepath.FromSlash(rel))
	names, _ := filepath.Glob(filepath.Join(dir, "*.md"))
	sort.Strings(names)
	now := time.Now()
	if n := len(names); n > 0 {
		last, err := time.ParseInLocation(stampLayout, strings.TrimSuffix(filepath.Base(names[n-1]), ".md"), time.Local)
		if err == nil && now.Sub(last) < backupEvery {
			return
		}
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return
	}
	if writeAtomic(filepath.Join(dir, now.Format(stampLayout)+".md"), current) != nil {
		return
	}
	names = append(names, "")
	for len(names) > backupsKept {
		_ = os.Remove(names[0])
		names = names[1:]
	}
}

// writeAtomic writes a file through a temporary file beside it and a
// rename, keeping the old file's permissions.
func writeAtomic(target string, data []byte) error {
	mode := os.FileMode(0o644)
	if info, err := os.Stat(target); err == nil {
		mode = info.Mode().Perm()
	}
	tmp, err := os.CreateTemp(filepath.Dir(target), "."+filepath.Base(target)+".*")
	if err != nil {
		return err
	}
	name := tmp.Name()
	_, err = tmp.Write(data)
	if err == nil {
		err = tmp.Sync()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Chmod(name, mode)
	}
	if err == nil {
		err = os.Rename(name, target)
	}
	if err != nil {
		_ = os.Remove(name)
	}
	return err
}

// validName reports whether a name the reader typed can be a note's or a
// folder's name, by the Qt app's rule: not empty, no space around it, no
// slash or backslash, and not starting with a dot.
func validName(name string) bool {
	return name != "" && strings.TrimSpace(name) == name && !strings.ContainsAny(name, `/\`) && !strings.HasPrefix(name, ".")
}

// ErrName is returned for a name that cannot be a note's or folder's name.
var ErrName = errors.New("not a name a note or folder can have")

// ErrExists is returned when a note or folder of that name is already there.
var ErrExists = errors.New("a note or folder of that name is already there")

var reUntitled = regexp.MustCompile(`^Untitled( [0-9]+)?$`)

// Untitled reports whether a note still has the name a new note is given,
// which the first block's text replaces.
func (e *Entry) Untitled() bool { return reUntitled.MatchString(e.Title) }

// Create makes an empty note named "Untitled", "Untitled 2" and so on in a
// folder, whichever is free first.
func (v *Vault) Create(folder string) (*Entry, error) { return v.CreateTitled(folder, "Untitled") }

// CreateTitled makes an empty note with a title in a folder, or when a
// note of that title is there, the title followed by 2, 3 and so on.
func (v *Vault) CreateTitled(folder, base string) (*Entry, error) {
	if v.ReadOnly {
		return nil, ErrReadOnly
	}
	if !validName(base) {
		return nil, ErrName
	}
	for n := 1; ; n++ {
		title := base
		if n > 1 {
			title = fmt.Sprintf("%s %d", base, n)
		}
		rel := path.Join(folder, title+".md")
		f, err := os.OpenFile(v.abs(rel), os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o644)
		if errors.Is(err, os.ErrExist) {
			continue
		}
		if err != nil {
			return nil, err
		}
		_ = f.Close()
		info, _ := os.Stat(v.abs(rel))
		e := &Entry{Note: Note{Path: rel, Title: title, Folder: folder, Modified: info.ModTime()}}
		v.Entries = append(v.Entries, e)
		v.sortEntries()
		return e, nil
	}
}

// Capture makes a note of captured text in the vault's top folder, named
// from its first line, or "Untitled" when that is taken or cannot be a
// name, and writes it in one write, since the text is usually its only copy
// (NoteCollection::captureNote).
func (v *Vault) Capture(text string) (*Entry, error) {
	if v.ReadOnly {
		return nil, ErrReadOnly
	}
	if title := TitleFromText(text); title != "" {
		if e, err := v.createWith("", title, text); err == nil {
			return e, nil
		}
	}
	for n := 1; ; n++ {
		title := "Untitled"
		if n > 1 {
			title = fmt.Sprintf("Untitled %d", n)
		}
		e, err := v.createWith("", title, text)
		if errors.Is(err, ErrExists) {
			continue
		}
		return e, err
	}
}

// createWith makes a note of a title holding text, failing with ErrExists
// when a file of that name is there.
func (v *Vault) createWith(folder, title, text string) (*Entry, error) {
	rel := path.Join(folder, title+".md")
	f, err := os.OpenFile(v.abs(rel), os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o644)
	if errors.Is(err, os.ErrExist) {
		return nil, ErrExists
	}
	if err != nil {
		return nil, err
	}
	_, werr := f.WriteString(text)
	if err := errors.Join(werr, f.Close()); err != nil {
		os.Remove(v.abs(rel))
		return nil, err
	}
	v.written[rel] = text
	info, _ := os.Stat(v.abs(rel))
	e := &Entry{Note: Note{Path: rel, Title: title, Folder: folder, Modified: info.ModTime()}}
	v.fill(e, parsePage(text))
	v.Entries = append(v.Entries, e)
	v.sortEntries()
	return e, nil
}

func (v *Vault) sortEntries() {
	sort.Slice(v.Entries, func(a, b int) bool { return v.Entries[a].Path < v.Entries[b].Path })
}

// TitleFromText is the name a note takes from the text of its first block,
// by the Qt app's rule: its first line, without slashes or leading dots, at
// most 60 characters, or "" when that cannot be a name.
func TitleFromText(text string) string {
	for _, line := range strings.Split(text, "\n") {
		if t := strings.TrimSpace(line); t != "" {
			t = strings.NewReplacer("/", "", `\`, "").Replace(t)
			t = strings.TrimSpace(strings.TrimLeft(t, "."))
			if r := []rune(t); len(r) > 60 {
				t = strings.TrimSpace(string(r[:60]))
			}
			if validName(t) {
				return t
			}
			return ""
		}
	}
	return ""
}

// Rename gives a note a new title in its folder.
func (v *Vault) Rename(e *Entry, title string) error {
	if !validName(title) {
		return ErrName
	}
	return v.move(e, path.Join(e.Folder, title+".md"))
}

// Move puts a note in another folder under its own title.
func (v *Vault) Move(e *Entry, folder string) error {
	return v.move(e, path.Join(folder, e.Title+".md"))
}

func (v *Vault) move(e *Entry, rel string) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	if rel == e.Path {
		return nil
	}
	if v.exists(rel) && !strings.EqualFold(rel, e.Path) {
		return ErrExists
	}
	if err := os.Rename(v.abs(e.Path), v.abs(rel)); err != nil {
		return err
	}
	old := e.Path
	v.written[rel] = v.written[old]
	e.Path = rel
	e.Title = strings.TrimSuffix(path.Base(rel), ".md")
	e.Folder = Parent(rel)
	if v.State.LastOpenNote == old {
		v.State.LastOpenNote = rel
	}
	v.sortEntries()
	return v.SaveState()
}

// Trash moves a note to .kvit/trash, named with the time it went there.
func (v *Vault) Trash(e *Entry) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	if err := v.toTrash(e.Path); err != nil {
		return err
	}
	v.Entries = slices.DeleteFunc(v.Entries, func(x *Entry) bool { return x == e })
	if v.State.LastOpenNote == e.Path {
		v.State.LastOpenNote = ""
	}
	return v.SaveState()
}

// toTrash moves a note or folder into the trash as
// "<yyyyMMdd-HHmmss>-<name>", with "-2", "-3" after the stamp when that is
// taken.
func (v *Vault) toTrash(rel string) error {
	dir := filepath.Join(v.Root, ".kvit", "trash")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	stamp, base := time.Now().Format(stampLayout), path.Base(rel)
	target := filepath.Join(dir, stamp+"-"+base)
	for n := 2; ; n++ {
		if _, err := os.Lstat(target); errors.Is(err, os.ErrNotExist) {
			break
		}
		target = filepath.Join(dir, fmt.Sprintf("%s-%d-%s", stamp, n, base))
	}
	return os.Rename(v.abs(rel), target)
}

// TrashCount is how many notes and folders are in the trash.
func (v *Vault) TrashCount() int {
	entries, _ := os.ReadDir(filepath.Join(v.Root, ".kvit", "trash"))
	return len(entries)
}

// RenameFolder gives a folder a new name, carrying its notes and folders
// with it.
func (v *Vault) RenameFolder(rel, name string) (string, error) {
	if v.ReadOnly {
		return "", ErrReadOnly
	}
	if !validName(name) {
		return "", ErrName
	}
	to := path.Join(Parent(rel), name)
	if to == rel {
		return rel, nil
	}
	if v.exists(to) && !strings.EqualFold(to, rel) {
		return "", ErrExists
	}
	if err := os.Rename(v.abs(rel), v.abs(to)); err != nil {
		return "", err
	}
	moved := func(p string) (string, bool) {
		if p == rel {
			return to, true
		}
		if rest, ok := strings.CutPrefix(p, rel+"/"); ok {
			return to + "/" + rest, true
		}
		return p, false
	}
	for _, e := range v.Entries {
		if p, ok := moved(e.Path); ok {
			e.Path, e.Folder = p, Parent(p)
		}
	}
	for i := range v.Folders {
		if p, ok := moved(v.Folders[i].Path); ok {
			v.Folders[i].Path, v.Folders[i].Name = p, path.Base(p)
		}
	}
	for f, st := range v.State.Folders {
		if p, ok := moved(f); ok {
			delete(v.State.Folders, f)
			v.State.Folders[p] = st
		}
	}
	if p, ok := moved(v.State.LastOpenNote); ok {
		v.State.LastOpenNote = p
	}
	v.sortEntries()
	sort.Slice(v.Folders, func(a, b int) bool { return v.Folders[a].Path < v.Folders[b].Path })
	return to, v.SaveState()
}

// TrashFolder moves a folder and everything in it to the trash.
func (v *Vault) TrashFolder(rel string) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	if err := v.toTrash(rel); err != nil {
		return err
	}
	inside := func(p string) bool { return p == rel || strings.HasPrefix(p, rel+"/") }
	v.Entries = slices.DeleteFunc(v.Entries, func(e *Entry) bool { return inside(e.Path) })
	v.Folders = slices.DeleteFunc(v.Folders, func(f Folder) bool { return inside(f.Path) })
	if inside(v.State.LastOpenNote) {
		v.State.LastOpenNote = ""
	}
	return v.SaveState()
}

// CreateFolder makes a folder inside another ("" for the top).
func (v *Vault) CreateFolder(parent, name string) (string, error) {
	if v.ReadOnly {
		return "", ErrReadOnly
	}
	if !validName(name) {
		return "", ErrName
	}
	rel := path.Join(parent, name)
	if err := os.Mkdir(v.abs(rel), 0o755); err != nil {
		if errors.Is(err, os.ErrExist) {
			return "", ErrExists
		}
		return "", err
	}
	v.Folders = append(v.Folders, Folder{Path: rel, Name: name})
	sort.Slice(v.Folders, func(a, b int) bool { return v.Folders[a].Path < v.Folders[b].Path })
	return rel, nil
}

// SaveState writes collection.json, keeping only folders that exist.
func (v *Vault) SaveState() error {
	if v.ReadOnly {
		return nil
	}
	for f := range v.State.Folders {
		if !slices.ContainsFunc(v.Folders, func(x Folder) bool { return x.Path == f }) {
			delete(v.State.Folders, f)
		}
	}
	data, err := v.State.encode()
	if err != nil {
		return err
	}
	return writeAtomic(filepath.Join(v.Root, ".kvit", "collection.json"), data)
}

// CountIn is how many notes are directly in a folder.
func (v *Vault) CountIn(folder string) int {
	n := 0
	for _, e := range v.Entries {
		if e.Folder == folder {
			n++
		}
	}
	return n
}

// CountFavorites is how many notes are favourites.
func (v *Vault) CountFavorites() int {
	n := 0
	for _, e := range v.Entries {
		if e.Favorite {
			n++
		}
	}
	return n
}

// HasSubfolders reports whether a folder holds folders.
func (v *Vault) HasSubfolders(folder string) bool {
	for _, f := range v.Folders {
		if Parent(f.Path) == folder && f.Path != folder {
			return true
		}
	}
	return false
}

// Parent is the folder holding a vault path, "" for the top.
func Parent(rel string) string {
	if d := path.Dir(rel); d != "." {
		return d
	}
	return ""
}

// TagCount is a tag and how many notes carry it.
type TagCount struct {
	Name  string
	Count int
}

// Tags are every tag in the vault with its count, in name order.
func (v *Vault) Tags() []TagCount {
	counts := map[string]int{}
	for _, e := range v.Entries {
		for _, t := range e.Tags {
			counts[t]++
		}
	}
	out := make([]TagCount, 0, len(counts))
	for t, n := range counts {
		out = append(out, TagCount{t, n})
	}
	sort.Slice(out, func(a, b int) bool { return strings.ToLower(out[a].Name) < strings.ToLower(out[b].Name) })
	return out
}
