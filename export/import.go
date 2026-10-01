package export

// Importing Markdown and text files into a vault: one file, several, or a
// whole folder tree, into a folder of the vault. Each file is copied as it is,
// byte for byte, front matter and all, so a note from another tool (an
// Obsidian vault, say) keeps every key it had; a .markdown or .txt file lands
// as a .md note. A name already taken gets " 2", " 3" and so on, and a folder
// import recreates the folder's subfolders under the target folder.
//
// This package plans the import: the plan says which folders to create and
// which files to write with what bytes, and the caller writes them. The plan
// reads the source files and asks whether names are taken in the vault; it
// writes nothing.

import (
	"errors"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"runtime"
	"slices"
	"strconv"
	"strings"
)

// ImportOptions tune an import.
type ImportOptions struct {
	// MaxFileBytes is the largest file imported; a larger one is skipped. 0
	// means 64 MiB, and a negative value means no cap.
	MaxFileBytes int64
}

// ImportFile is one note an import writes.
type ImportFile struct {
	// Source is the file read.
	Source string
	// RelPath is where the note goes in the vault, with "/" between
	// folders: "Imported/sub/Note 2.md".
	RelPath string
	// Content is the source's bytes, unchanged.
	Content []byte
}

// ImportPlan is what an import would do.
type ImportPlan struct {
	// Folders are the vault folders to create before writing, parents first.
	Folders []string
	// Files are the notes to write, in order.
	Files []ImportFile
	// Skipped are the importable sources that were left out because they
	// are over the size cap or could not be read.
	Skipped []string
}

// DryRun is the summary the import dialog shows before importing.
type DryRun struct {
	// Files is how many files would be imported.
	Files int
	// Collisions is how many of them already have a note of their name in
	// the folder they go to, and will be given a number.
	Collisions int
	// Folders is how many subfolders a folder import recreates.
	Folders int
}

// IsImportable reports whether a file is one the importer takes: .md,
// .markdown or .txt, in any case.
func IsImportable(p string) bool {
	lower := strings.ToLower(p)
	return strings.HasSuffix(lower, ".md") || strings.HasSuffix(lower, ".markdown") ||
		strings.HasSuffix(lower, ".txt")
}

// completeBaseName is the file name up to its last dot.
func completeBaseName(p string) string {
	name := filepath.Base(p)
	if dot := strings.LastIndexByte(name, '.'); dot >= 0 {
		return name[:dot]
	}
	return name
}

// sanitizeBase is the importer's name rule: the base name trimmed, with
// / \ : * ? " < > | removed, and "Imported" when nothing is left.
func sanitizeBase(name string) string {
	out := strings.Map(func(r rune) rune {
		if strings.ContainsRune(`/\:*?"<>|`, r) {
			return -1
		}
		return r
	}, trimSpace(name))
	if out == "" {
		return "Imported"
	}
	return out
}

// importer plans one run: it remembers the names it has already given, so
// that a later file of the same name in the run gets the next number.
type importer struct {
	root    string
	opt     ImportOptions
	claimed map[string]bool
	folders map[string]bool
	plan    ImportPlan
}

func newImporter(root string, opt ImportOptions) *importer {
	return &importer{root: root, opt: opt, claimed: map[string]bool{}, folders: map[string]bool{}}
}

// nameKey is how a name is compared with the names already given: without
// regard to case where the file system usually ignores it.
func nameKey(rel string) string {
	if runtime.GOOS == "windows" || runtime.GOOS == "darwin" {
		return strings.ToLower(rel)
	}
	return rel
}

func (im *importer) exists(rel string) bool {
	if im.claimed[nameKey(rel)] {
		return true
	}
	_, err := os.Stat(filepath.Join(im.root, filepath.FromSlash(rel)))
	return err == nil
}

// uniqueRelPath is base.md in the folder, or "base 2.md", "base 3.md" and so
// on, the first that is free.
func (im *importer) uniqueRelPath(folder, baseName string) string {
	base := sanitizeBase(baseName)
	prefix := ""
	if folder != "" {
		prefix = folder + "/"
	}
	rel := prefix + base + ".md"
	for n := 2; im.exists(rel); n++ {
		rel = prefix + base + " " + strconv.Itoa(n) + ".md"
	}
	return rel
}

// read is the source's bytes, or false when it is over the cap or cannot be
// read whole.
func (im *importer) read(source string) ([]byte, bool) {
	limit := im.opt.MaxFileBytes
	if limit == 0 {
		limit = 64 << 20
	}
	f, err := os.Open(source)
	if err != nil {
		return nil, false
	}
	defer f.Close()
	info, err := f.Stat()
	if err != nil || !info.Mode().IsRegular() {
		return nil, false
	}
	if limit > 0 && info.Size() > limit {
		return nil, false
	}
	var r io.Reader = f
	if limit > 0 {
		r = io.LimitReader(f, limit+1)
	}
	data, err := io.ReadAll(r)
	if err != nil || (limit > 0 && int64(len(data)) > limit) {
		return nil, false
	}
	if info.Size() > 0 && int64(len(data)) != info.Size() {
		return nil, false
	}
	return data, true
}

// addFolder records a folder to create, with each parent before it.
func (im *importer) addFolder(folder string) {
	if folder == "" {
		return
	}
	parts := strings.Split(folder, "/")
	for i := range parts {
		f := strings.Join(parts[:i+1], "/")
		if !im.folders[f] {
			im.folders[f] = true
			im.plan.Folders = append(im.plan.Folders, f)
		}
	}
}

func (im *importer) importOne(source, folder string) {
	data, ok := im.read(source)
	if !ok {
		im.plan.Skipped = append(im.plan.Skipped, source)
		return
	}
	im.addFolder(folder)
	rel := im.uniqueRelPath(folder, completeBaseName(source))
	im.claimed[nameKey(rel)] = true
	im.plan.Files = append(im.plan.Files, ImportFile{Source: source, RelPath: rel, Content: data})
}

// checkTarget refuses a target folder that is not a plain path inside the
// vault.
func checkTarget(root, folder string) error {
	if root == "" {
		return errors.New("export: no vault to import into")
	}
	if folder == "" {
		return nil
	}
	if !isPlainRelativePath(folder) {
		return errors.New(`"` + folder + `" is not a safe relative path`)
	}
	if !isInsideDirectory(canonicalTarget(filepath.Join(root, filepath.FromSlash(folder))), canonicalTarget(root)) {
		return errors.New(`"` + folder + `" is outside the notes folder`)
	}
	return nil
}

// ImportFiles plans importing files into a folder of the vault ("" is the
// vault's top level). Files that are not importable are passed over; the
// rest are imported in the order given.
func ImportFiles(root string, paths []string, folder string, opt ImportOptions) (ImportPlan, error) {
	if err := checkTarget(root, folder); err != nil {
		return ImportPlan{}, err
	}
	im := newImporter(root, opt)
	im.addFolder(folder)
	for _, p := range paths {
		if IsImportable(p) {
			im.importOne(p, folder)
		}
	}
	return im.plan, nil
}

// ImportFolder plans importing every importable file under dir into a folder
// of the vault, each subfolder of dir recreated under it.
func ImportFolder(root, dir, folder string, opt ImportOptions) (ImportPlan, error) {
	if err := checkTarget(root, folder); err != nil {
		return ImportPlan{}, err
	}
	entries, err := importableUnder(dir)
	if err != nil {
		return ImportPlan{}, err
	}
	im := newImporter(root, opt)
	for _, e := range entries {
		target := joinFolder(folder, e.sub)
		if err := checkTarget(root, target); err != nil {
			im.plan.Skipped = append(im.plan.Skipped, e.path)
			continue
		}
		im.importOne(e.path, target)
	}
	return im.plan, nil
}

func joinFolder(a, b string) string {
	switch {
	case a == "":
		return b
	case b == "":
		return a
	}
	return a + "/" + b
}

type importEntry struct {
	path string // the file
	sub  string // its folder under the imported folder, "/"-separated
}

// isHidden reports whether a file counts as hidden: its name starts with a
// dot.
func isHidden(name string) bool { return strings.HasPrefix(name, ".") }

// importableUnder lists the importable files under dir in the order the
// file system lists each folder, going into a subfolder where it is listed,
// files only, hidden files and folders passed over, links to folders not
// followed. The order decides which of two files with one name in a folder
// gets the " 2".
func importableUnder(dir string) ([]importEntry, error) {
	var out []importEntry
	var walk func(abs, sub string) error
	walk = func(abs, sub string) error {
		f, err := os.Open(abs)
		if err != nil {
			return err
		}
		names, err := f.Readdirnames(-1)
		f.Close()
		if err != nil {
			return err
		}
		for _, name := range names {
			if isHidden(name) {
				continue
			}
			p := filepath.Join(abs, name)
			info, err := os.Lstat(p)
			if err != nil {
				continue
			}
			switch {
			case info.IsDir():
				// A folder that cannot be read is passed over.
				_ = walk(p, joinFolder(sub, name))
				continue
			case info.Mode()&fs.ModeSymlink != 0:
				if target, err := os.Stat(p); err != nil || !target.Mode().IsRegular() {
					continue
				}
			case !info.Mode().IsRegular():
				continue
			}
			if IsImportable(name) {
				out = append(out, importEntry{p, sub})
			}
		}
		return nil
	}
	if err := walk(dir, ""); err != nil {
		return nil, err
	}
	return out, nil
}

// ImportableFiles are the files a folder import of dir would take, in the
// order it would take them.
func ImportableFiles(dir string) ([]string, error) {
	entries, err := importableUnder(dir)
	if err != nil {
		return nil, err
	}
	out := make([]string, len(entries))
	for i, e := range entries {
		out[i] = e.path
	}
	return out, nil
}

// DryRunFiles is the summary for importing files into a folder: how many are
// importable, and how many of those already have a note of their name
// there. A collision is counted against the notes already in the vault, by
// the file's own name.
func DryRunFiles(root string, paths []string, folder string) DryRun {
	var d DryRun
	prefix := ""
	if folder != "" {
		prefix = folder + "/"
	}
	for _, p := range paths {
		if !IsImportable(p) {
			continue
		}
		d.Files++
		if fileExists(root, prefix+completeBaseName(p)+".md") {
			d.Collisions++
		}
	}
	return d
}

// DryRunFolder is the summary for importing a folder: its importable files,
// how many collide with notes already there, and how many subfolders it
// recreates.
func DryRunFolder(root, dir, folder string) (DryRun, error) {
	entries, err := importableUnder(dir)
	if err != nil {
		return DryRun{}, err
	}
	d := DryRun{Files: len(entries)}
	var subs []string
	for _, e := range entries {
		if e.sub != "" && !slices.Contains(subs, e.sub) {
			subs = append(subs, e.sub)
		}
		target := joinFolder(folder, e.sub)
		prefix := ""
		if target != "" {
			prefix = target + "/"
		}
		if fileExists(root, prefix+completeBaseName(e.path)+".md") {
			d.Collisions++
		}
	}
	d.Folders = len(subs)
	return d, nil
}

func fileExists(root, rel string) bool {
	if root == "" {
		return false
	}
	_, err := os.Stat(filepath.Join(root, filepath.FromSlash(rel)))
	return err == nil
}
