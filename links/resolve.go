package links

import (
	"slices"
	"strings"
)

// Which note a link names, from the Qt app's
// src/repository/wikilinkindex.cpp, and where following a link that names
// no note creates one, from qml/NoteSession.qml.

// NormalizeTarget is the form of a link target that is compared with note
// paths: spaces around it removed, the "#heading" dropped, one ".md"
// dropped, leading slashes dropped, and lowercased. "/Ideas/Kvit.md#Goals"
// becomes "ideas/kvit". The redirect table is looked up with the same form.
func NormalizeTarget(target string) string {
	wanted := trim(target)
	if hash := strings.IndexByte(wanted, '#'); hash >= 0 {
		wanted = trim(wanted[:hash])
	}
	wanted = strings.TrimLeft(trimMD(wanted), "/")
	return lower(wanted)
}

// PathMatchesTarget reports whether a normalized target names a note path:
// the whole path without ".md", or its last segments. "kvit" and
// "projects/kvit" both name "Ideas/Projects/Kvit.md"; "jects/kvit" does not.
func PathMatchesTarget(path, normalized string) bool {
	p := lower(trimMD(path))
	return p == normalized || strings.HasSuffix(p, "/"+normalized)
}

// basenameKey is the key a note is filed under for resolution: its file
// name without ".md", lowercased.
func basenameKey(path string) string { return lower(trimMD(baseName(path))) }

// Status says how a target resolved.
type Status int

const (
	// Missing: no note has that name. Following the link creates one
	// (see NewNoteFor).
	Missing Status = iota
	// Unique: exactly one note has that name.
	Unique
	// Ambiguous: several notes have that name. Following the link opens
	// none of them and creates nothing; the Qt app says "Ambiguous link"
	// and lists the candidates.
	Ambiguous
)

// Resolution is what a target resolves to.
type Resolution struct {
	Status Status
	// Path is the note's path when Status is Unique, else "".
	Path string
	// Candidates are every note the target names, sorted without regard to
	// case.
	Candidates []string
	// Redirected is true when no note has the name and the redirect table
	// answered instead: the note was renamed and links to it have not all
	// been rewritten yet.
	Redirected bool
}

// Index answers which note a link names in one vault. It holds the vault's
// note paths, filed by file name, and optionally its redirect table.
type Index struct {
	// Redirects is the vault's table of renamed notes, consulted only for
	// a target no note answers. Nil means no table.
	Redirects *Redirects

	notes  map[string]bool // path -> is a realm file
	byBase map[string][]string
}

// NewIndex makes the index of a vault holding these notes.
func NewIndex(paths []string) *Index {
	ix := &Index{notes: map[string]bool{}, byBase: map[string][]string{}}
	for _, p := range paths {
		ix.add(p, false)
	}
	return ix
}

func (ix *Index) add(path string, realm bool) {
	if _, ok := ix.notes[path]; !ok {
		key := basenameKey(path)
		ix.byBase[key] = append(ix.byBase[key], path)
	}
	ix.notes[path] = realm
}

// Add records a note that has appeared at path, created there or renamed
// or moved there. A redirect from that path is dropped, because a redirect
// never hides a note that exists; Add reports whether one was, so the
// caller saves the table (NoteCollection::insertNoteEntry).
func (ix *Index) Add(path string) bool {
	ix.add(path, false)
	return ix.Redirects != nil && ix.Redirects.DropFrom(path)
}

// AddRealm records a file an application admitted from a subtree it
// manages, such as ".reports/monday/report.md" (src/domain/reservedsubtrees.h).
// Such a file answers only a target that names at least one of its folders,
// so a bare [[report]] still means the user's own note of that name. Kvit
// Notes itself registers no such subtree.
func (ix *Index) AddRealm(path string) { ix.add(path, true) }

// Remove forgets a note that has gone.
func (ix *Index) Remove(path string) {
	if _, ok := ix.notes[path]; !ok {
		return
	}
	delete(ix.notes, path)
	key := basenameKey(path)
	rest := slices.DeleteFunc(ix.byBase[key], func(p string) bool { return p == path })
	if len(rest) == 0 {
		delete(ix.byBase, key)
	} else {
		ix.byBase[key] = rest
	}
}

// Has reports whether a note is at path.
func (ix *Index) Has(path string) bool {
	_, ok := ix.notes[path]
	return ok
}

// Resolve is the note a target names, or "" when none does or several do.
// The target may carry a "#heading", which is ignored.
func (ix *Index) Resolve(target string) string {
	return ix.Resolution(target, true).Path
}

// Resolution resolves a target as the Qt app does. A target matches a note
// when it is the note's path or the last segments of it, without regard to
// case and with ".md" implied: [[kvit]], [[Kvit.md]] and [[Projects/Kvit]]
// all name "Ideas/Projects/Kvit.md". More than one match is Ambiguous and
// never picks one; writing more of the path chooses. Only when no note
// matches is the redirect table consulted, and followRedirects false leaves
// it out, to ask what the files alone say.
func (ix *Index) Resolution(target string, followRedirects bool) Resolution {
	lowered := NormalizeTarget(target)
	if lowered == "" {
		return Resolution{Status: Missing}
	}
	qualified := strings.Contains(lowered, "/")
	var matches []string
	for _, p := range ix.byBase[baseName(lowered)] {
		if !PathMatchesTarget(p, lowered) {
			continue
		}
		if !qualified && ix.notes[p] {
			continue
		}
		matches = append(matches, p)
	}
	slices.SortFunc(matches, compareFold)

	if len(matches) == 0 && followRedirects && ix.Redirects != nil {
		if to := ix.Redirects.TargetFor(lowered); to != "" && ix.Has(to) {
			return Resolution{Status: Unique, Path: to, Candidates: []string{to}, Redirected: true}
		}
	}
	switch len(matches) {
	case 0:
		return Resolution{Status: Missing}
	case 1:
		return Resolution{Status: Unique, Path: matches[0], Candidates: matches}
	}
	return Resolution{Status: Ambiguous, Candidates: matches}
}

// CompletionTarget is what [[ completion inserts for a note: its title when
// that alone resolves to the note, else its path without ".md"
// (qml/WikiLinkMenu.qml).
func (ix *Index) CompletionTarget(path, title string) string {
	if ix.Resolve(title) == path {
		return title
	}
	return trimMD(path)
}

// NewNote is where following a link to a note that does not exist creates
// it.
type NewNote struct {
	// Folder is the folder the note goes in, "" for the top of the vault.
	// Any folder on the way to it that is missing is created first.
	Folder string
	// Title is the note's name. "" makes an untitled note ("Untitled",
	// "Untitled 2", ...), which is what [[Folder/]] does in the Qt app.
	Title string
	// Path is Folder/Title.md, or "" for an untitled note.
	Path string
}

// NewNoteFor says where following target creates its note, from
// createWikiTarget in qml/NoteSession.qml. target is the link's note part
// and current the path of the note holding the link. A bare name goes in
// the current note's folder; a name with a path goes where the path says,
// its folders created as needed, and a leading "/" means the top of the
// vault. A trailing ".md" is dropped. It reports false when the Qt app would
// fail: a folder or note name that starts with a dot, holds a backslash, has
// spaces around it, or is empty between two slashes.
func NewNoteFor(target, current string) (NewNote, bool) {
	var n NewNote
	if slash := strings.LastIndexByte(target, '/'); slash >= 0 {
		for _, part := range strings.Split(target[:slash], "/") {
			if part == "" && n.Folder == "" {
				continue
			}
			if !validName(part) {
				return NewNote{}, false
			}
			n.Folder = join(n.Folder, part)
		}
		n.Title = target[slash+1:]
	} else {
		if slash := strings.LastIndexByte(current, '/'); slash >= 0 {
			n.Folder = current[:slash]
		}
		n.Title = target
	}
	if len(n.Title) > 3 && strings.EqualFold(n.Title[len(n.Title)-3:], ".md") {
		n.Title = n.Title[:len(n.Title)-3]
	}
	n.Title = trim(n.Title)
	if n.Title == "" {
		return n, true
	}
	if !validName(n.Title) {
		return NewNote{}, false
	}
	n.Path = join(n.Folder, n.Title+".md")
	return n, true
}

// validName is the Qt app's rule for a note's or folder's name
// (NoteCollection::validName): not blank, no spaces around it, no slash or
// backslash, and not starting with a dot.
func validName(name string) bool {
	return trim(name) != "" && trim(name) == name && !strings.ContainsAny(name, `/\`) && !strings.HasPrefix(name, ".")
}

// join puts a name under a folder, "" being the top of the vault.
func join(folder, name string) string {
	if folder == "" {
		return name
	}
	return folder + "/" + name
}
