package export

// Exporting several notes of a vault: the Qt export dialog's "Selected notes"
// and "Whole collection" scopes (DocumentExporter::exportNotes and
// exportCollection in src/application/documentexporter.cpp). Each note
// becomes one file under the destination, at its path in the vault with the
// format's extension, or all of them become one file named collection.<ext>.
//
// Before anything is made, the whole plan is checked, and a plan that would
// write over a note is refused as a whole. Deciding one note at a time would
// be too late: by the time a collision is noticed, the notes before it would
// already be written.

import (
	"io/fs"
	"path"
	"path/filepath"
	"strings"
)

// VaultNote is one note of a vault export.
type VaultNote struct {
	// RelPath is the note's path in the vault, with "/" between folders:
	// "Sub/Beta.md".
	RelPath string
	// Text is the note's file as saved, front matter included. For the note
	// open in the editor, pass its unsaved state, as the Qt app does
	// (DocumentExporter::setLiveNote): exporting reads what the reader sees
	// and does not save it.
	Text string
	// Title is the note's title, used for an HTML page's title and a
	// combined Markdown file's headings; "" means the file name without
	// ".md", which is the Qt app's title.
	Title string
}

// VaultExport is an export of several notes.
type VaultExport struct {
	// Root is the vault's folder.
	Root string
	// Notes are the notes to export, in order.
	Notes []VaultNote
	// AllNotes are the paths of every note in the vault. An output that
	// would land on one of them refuses the export. When nil, the Markdown
	// files under the destination are found by reading the folder.
	AllNotes []string
	// Dest is the folder the files go in.
	Dest string
	// Format is the format to write; PDF returns ErrPDF.
	Format Format
	// SingleFile combines every note into Dest/collection.<ext>.
	SingleFile bool
	// Options are the colours, budgets and preview cache for HTML. NoteDir
	// and VaultRoot are set for each note and need not be given.
	Options Options
}

// File is one file an export would write.
type File struct {
	// Path is where the file goes, under the destination.
	Path string
	// RelPath is the note the file was made from, or "" for a combined file.
	RelPath string
	// Content is the file's bytes.
	Content []byte
}

// Refusal is an export the Qt app would refuse before writing anything, with
// the Qt app's message saying why.
type Refusal struct {
	Reason string
}

func (r *Refusal) Error() string { return r.Reason }

// Vault plans a vault export: the files to write, in order, or a *Refusal.
// Nothing is written; the caller creates the folders each Path needs and
// writes the files.
func Vault(e VaultExport) ([]File, error) {
	if e.Format == FormatPDF {
		return nil, ErrPDF
	}
	switch e.Format {
	case FormatMarkdown, FormatHTML, FormatText:
	default:
		return nil, ErrFormat
	}
	outputs, err := e.plan()
	if err != nil {
		return nil, err
	}
	if e.SingleFile {
		return e.combined(outputs[0])
	}
	files := make([]File, len(e.Notes))
	for i, n := range e.Notes {
		files[i] = File{Path: outputs[i], RelPath: n.RelPath, Content: []byte(e.one(n))}
	}
	return files, nil
}

// noteTitle is the Qt app's title for a note: its file name without ".md".
func noteTitle(n VaultNote) string {
	if n.Title != "" {
		return n.Title
	}
	name := path.Base(n.RelPath)
	if strings.HasSuffix(strings.ToLower(name), ".md") {
		return name[:len(name)-3]
	}
	return name
}

// noteOptions are the options for one note: relative images are looked for
// in the note's own folder, then in the vault.
func (e VaultExport) noteOptions(n VaultNote) Options {
	opt := e.Options
	opt.NoteDir = filepath.Dir(filepath.Join(e.Root, filepath.FromSlash(n.RelPath)))
	opt.VaultRoot = e.Root
	return opt
}

// one is a note's own file (DocumentExporter::exportOneNote). A Markdown
// export is the note itself, so it includes the note's front matter in the
// form the Qt app writes it; the other formats leave the front matter out.
func (e VaultExport) one(n VaultNote) string {
	s := splitFrontMatter(n.Text)
	opt := e.noteOptions(n)
	switch e.Format {
	case FormatMarkdown:
		fm := ""
		if s.present {
			fm = CanonicalFrontMatter(s.block)
		}
		return fm + s.body
	case FormatHTML:
		return HTMLFromMarkdown(s.body, noteTitle(n), opt)
	}
	return TextFromMarkdown(s.body, opt)
}

// combined is every note in one file (DocumentExporter::appendCombinedNote
// and writeCombined). Markdown puts each note under a "# Title" heading; text
// runs the notes on; HTML makes one page with one section per note, each
// after the first starting a new printed page.
func (e VaultExport) combined(out string) ([]File, error) {
	limit := e.Options.MaxCombinedChars
	if limit == 0 {
		limit = 128 << 20
	}
	var sb strings.Builder
	size := 0
	sawMath, sawMermaid := false, false
	for i, n := range e.Notes {
		body := splitFrontMatter(n.Text).body
		opt := e.noteOptions(n)
		var piece string
		switch e.Format {
		case FormatMarkdown:
			piece = "# " + noteTitle(n) + "\n\n" + body + "\n\n"
		case FormatText:
			piece = TextFromMarkdown(body, opt) + "\n\n"
		default:
			doc := fromEditor(parseBody(body), false)
			r := newRenderer(doc, opt)
			html := r.body(doc, r.docSlugs)
			sawMath = sawMath || r.sawMath
			sawMermaid = sawMermaid || r.sawMermaid
			if i == 0 {
				piece = "<section>\n"
			} else {
				piece = "<hr>\n<section style=\"page-break-before:always\">\n"
			}
			piece += html + "\n</section>\n"
		}
		sb.WriteString(piece)
		size += utf16Len(piece)
		if limit > 0 && int64(size) > limit {
			return nil, &Refusal{"This selection is too large to combine into a single file. " +
				"Export it as one file per note instead."}
		}
	}
	content := sb.String()
	if e.Format == FormatHTML {
		content = wrapPage(content, "", e.Options.Colors, sawMath, sawMermaid)
	}
	return []File{{Path: out, Content: []byte(content)}}, nil
}

// isPlainRelativePath is VaultPaths::isPlainRelativePath: relative, with "/"
// between folders, already clean, and with no "." or ".." in it.
func isPlainRelativePath(rel string) bool {
	if rel == "" || path.IsAbs(rel) || filepath.IsAbs(rel) || filepath.VolumeName(rel) != "" ||
		strings.Contains(rel, `\`) || path.Clean(rel) != rel {
		return false
	}
	for _, seg := range strings.Split(rel, "/") {
		if seg == "" || seg == "." || seg == ".." {
			return false
		}
	}
	return true
}

// canonicalTarget is a path made absolute with its symbolic links resolved,
// for a path that need not exist yet: the deepest ancestor that exists is
// resolved and the rest appended (DocumentExporter::canonicalTarget).
func canonicalTarget(p string) string {
	if p == "" {
		return ""
	}
	abs, err := filepath.Abs(p)
	if err != nil {
		abs = filepath.Clean(p)
	}
	if real, err := filepath.EvalSymlinks(abs); err == nil {
		return real
	}
	tail := filepath.Base(abs)
	dir := filepath.Dir(abs)
	for {
		if real, err := filepath.EvalSymlinks(dir); err == nil {
			return filepath.Join(real, tail)
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			return abs
		}
		tail = filepath.Join(filepath.Base(dir), tail)
		dir = parent
	}
}

func isInsideDirectory(p, dir string) bool {
	if p == "" || dir == "" {
		return false
	}
	if p == dir {
		return true
	}
	prefix := dir
	if !strings.HasSuffix(prefix, string(filepath.Separator)) {
		prefix += string(filepath.Separator)
	}
	return strings.HasPrefix(p, prefix)
}

// plan is DocumentExporter::buildOutputPlan: where each note goes, or the
// reason the export is refused.
func (e VaultExport) plan() ([]string, error) {
	if len(e.Notes) == 0 || e.Root == "" {
		return nil, &Refusal{"There is nothing to export."}
	}
	if e.Dest == "" {
		return nil, &Refusal{"No destination was chosen."}
	}
	for _, n := range e.Notes {
		if !isPlainRelativePath(n.RelPath) {
			return nil, &Refusal{`"` + n.RelPath + `" is not a note in this collection.`}
		}
	}
	ext := e.Format.Extension()
	dest := canonicalTarget(e.Dest)
	root := canonicalTarget(e.Root)

	// The notes an output could land on: the ones being read, and, when the
	// destination is inside the vault, every note under it.
	candidates := make([]string, 0, len(e.Notes))
	for _, n := range e.Notes {
		candidates = append(candidates, n.RelPath)
	}
	if isInsideDirectory(dest, root) {
		prefix := strings.TrimLeft(filepath.ToSlash(strings.TrimPrefix(dest, root)), "/")
		for _, rel := range e.vaultNotesUnder(dest, root) {
			if prefix == "" || strings.HasPrefix(rel, prefix+"/") {
				candidates = append(candidates, rel)
			}
		}
	}
	sources := map[string]bool{}
	for _, rel := range candidates {
		sources[canonicalTarget(filepath.Join(e.Root, filepath.FromSlash(rel)))] = true
	}

	if e.SingleFile {
		out := filepath.Join(e.Dest, "collection."+ext)
		if sources[canonicalTarget(out)] {
			return nil, &Refusal{"Exporting there would overwrite one of your notes. " +
				"Choose a destination outside the collection."}
		}
		return []string{out}, nil
	}
	if e.Format == FormatMarkdown && root != "" && isInsideDirectory(dest, root) {
		return nil, &Refusal{"Markdown export writes note bodies without their metadata, " +
			"so it cannot write inside the collection itself. Choose a destination outside it."}
	}
	claimed := map[string]bool{}
	var outputs []string
	for _, n := range e.Notes {
		outRel := n.RelPath
		if strings.HasSuffix(strings.ToLower(outRel), ".md") {
			outRel = outRel[:len(outRel)-3]
		}
		out := filepath.Join(e.Dest, filepath.FromSlash(outRel+"."+ext))
		canonical := canonicalTarget(out)
		if sources[canonical] {
			return nil, &Refusal{"Exporting there would overwrite " + n.RelPath +
				". Choose a destination outside the collection."}
		}
		if claimed[canonical] {
			return nil, &Refusal{"Two notes in this export would be written to the same file (" + out + ")."}
		}
		claimed[canonical] = true
		outputs = append(outputs, out)
	}
	return outputs, nil
}

// vaultNotesUnder is the vault's notes, as paths in the vault: AllNotes when
// given, else the Markdown files found under the destination.
func (e VaultExport) vaultNotesUnder(dest, root string) []string {
	if e.AllNotes != nil {
		return e.AllNotes
	}
	var found []string
	_ = filepath.WalkDir(dest, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			return nil
		}
		if !d.IsDir() && strings.HasSuffix(strings.ToLower(d.Name()), ".md") {
			if rel, err := filepath.Rel(root, canonicalTarget(p)); err == nil {
				found = append(found, filepath.ToSlash(rel))
			}
		}
		return nil
	})
	return found
}
