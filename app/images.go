package app

// Loading the pictures a note's image blocks name, by Kvit's rule
// (src/content/imageassets.cpp, resolveSource): beside the note first, then
// from the top of the vault, then as written, and a path starting with "/"
// from the vault's site folder, the first that is a file.

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"strings"
	"time"
	"unicode"

	"github.com/kvit-s/kvit-notes/vault"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// maxPicture is the largest picture file read.
const maxPicture = 64 << 20

// imageLoader loads pictures for the note at a vault path.
func (w *Window) imageLoader(note string) func(string) (unison.Drawable, error) {
	root := w.Vault.Root
	noteDir := filepath.Join(root, filepath.FromSlash(vault.Parent(note)))
	return func(stored string) (unison.Drawable, error) {
		var candidates []string
		if !filepath.IsAbs(stored) {
			candidates = append(candidates, filepath.Join(noteDir, filepath.FromSlash(stored)),
				filepath.Join(root, filepath.FromSlash(stored)))
		} else {
			candidates = append(candidates, stored)
		}
		if rel, ok := strings.CutPrefix(stored, "/"); ok && rel != "" && !strings.HasPrefix(rel, "/") {
			candidates = append(candidates, filepath.Join(w.Vault.SiteRoot(), filepath.FromSlash(rel)))
		}
		for _, c := range candidates {
			info, err := os.Stat(c)
			if err != nil || !info.Mode().IsRegular() || info.Size() > maxPicture {
				continue
			}
			data, err := os.ReadFile(c)
			if err != nil {
				continue
			}
			if strings.EqualFold(filepath.Ext(c), ".svg") {
				svg, err := unison.NewSVGFromReader(bytes.NewReader(data))
				if err != nil {
					return nil, err
				}
				return svgDrawable{svg}, nil
			}
			return unison.NewImageFromBytes(data, geom.NewPoint(1, 1))
		}
		return nil, errors.New("no such picture")
	}
}

// svgDrawable draws an SVG as a picture of its own size.
type svgDrawable struct{ *unison.SVG }

func (s svgDrawable) LogicalSize() geom.Size { return s.Size() }

// pickImage asks for a picture file for a new image block, adds it to the
// vault by Kvit's asset rule, and writes the block's line.
func (w *Window) pickImage(block int64) {
	d := unison.NewOpenDialog()
	d.SetAllowedExtensions("png", "jpg", "jpeg", "gif", "webp", "svg", "bmp")
	if !d.RunModal() || len(d.Paths()) == 0 {
		return
	}
	note := ""
	if w.open != nil {
		note = w.open.Path
	}
	stored, err := w.ingestFile(d.Paths()[0], note)
	if err != nil {
		w.fail("Could not add the picture", err)
		return
	}
	w.setImageLine(block, stored)
}

// setImageLine writes an image block's line for a stored path.
func (w *Window) setImageLine(block int64, stored string) {
	doc := w.Editor.Doc
	b := doc.Block(block)
	if b == nil {
		return
	}
	doc.Edit("image", func() {
		b.Text = "![](" + stored + ")"
		doc.SetCaret(block, len([]rune(b.Text)))
	})
	w.Editor.Refresh()
}

// ingestFile adds a picture to the vault as Kvit's AssetStore does
// (src/repository/assetstore.cpp): a file already inside the vault is
// linked where it is; any other is copied into the vault's picture folder
// (assets/ unless set) as "<note>-<yyyyMMdd-HHmmss>.<ext>", named after the
// note it is for, with "-N" added when that is taken. The path returned is
// from the top of the vault, or from "/" for a file in the site folder.
func (w *Window) ingestFile(src, note string) (string, error) {
	abs, err := filepath.Abs(src)
	if err != nil {
		return "", err
	}
	if rel, err := filepath.Rel(w.Vault.Root, abs); err == nil && !strings.HasPrefix(rel, "..") {
		return w.storedPath(abs), nil
	}
	data, err := os.ReadFile(abs)
	if err != nil {
		return "", err
	}
	dir := filepath.Join(w.Vault.Root, filepath.FromSlash(w.Vault.ImageFolder()))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	ext := safeSegment(strings.TrimPrefix(filepath.Ext(abs), "."), "bin")
	base := noteSlug(note) + "-" + time.Now().Format("20060102-150405")
	name := base + "." + ext
	for n := 1; ; n++ {
		if _, err := os.Stat(filepath.Join(dir, name)); errors.Is(err, os.ErrNotExist) {
			break
		}
		name = fmt.Sprintf("%s-%d.%s", base, n, ext)
	}
	target := filepath.Join(dir, name)
	if err := os.WriteFile(target, data, 0o644); err != nil {
		return "", err
	}
	return w.storedPath(target), nil
}

// storedPath is how a note names a picture file in the vault: from "/"
// inside a site folder that is not the vault's top, otherwise from the top
// of the vault (assetstore.cpp, storedPathFor).
func (w *Window) storedPath(abs string) string {
	root := filepath.Clean(w.Vault.Root)
	site := filepath.Clean(w.Vault.SiteRoot())
	if site != root {
		if rel, err := filepath.Rel(site, abs); err == nil && !strings.HasPrefix(rel, "..") {
			return "/" + filepath.ToSlash(rel)
		}
	}
	rel, err := filepath.Rel(root, abs)
	if err != nil || strings.HasPrefix(rel, "..") {
		return abs
	}
	return filepath.ToSlash(rel)
}

// noteSlug is a note's name as new picture names start with: its file
// name lowercased, every run of other characters than a to z and 0 to 9 a
// dash, or "image" (BlockEditorSurface.qml, documentSlug).
func noteSlug(note string) string {
	name := strings.TrimSuffix(path.Base(note), path.Ext(note))
	var b strings.Builder
	dash := false
	for _, r := range strings.ToLower(name) {
		if r >= 'a' && r <= 'z' || r >= '0' && r <= '9' {
			b.WriteRune(r)
			dash = false
		} else if !dash {
			b.WriteByte('-')
			dash = true
		}
	}
	slug := strings.Trim(b.String(), "-")
	if slug == "" {
		return "image"
	}
	return slug
}

// safeSegment keeps letters, digits, "-", "_" and spaces, turning any run
// of other characters into one "-", as Kvit's asset names do.
func safeSegment(value, fallback string) string {
	var b strings.Builder
	for _, r := range value {
		switch {
		case unicode.IsLetter(r) || unicode.IsDigit(r) || r == '-' || r == '_' || r == ' ':
			b.WriteRune(r)
		case b.Len() > 0 && !strings.HasSuffix(b.String(), "-"):
			b.WriteByte('-')
		}
	}
	out := strings.TrimRight(strings.TrimSpace(b.String()), "-")
	if out == "" {
		return fallback
	}
	return out
}
