package editor

// Files, images and text dropped from other applications (features.md 5.4,
// Kvit's drag handling in BlockEditorSurface and EditableBlock.qml): a file
// manager's image lands as an image block where it is dropped, copied into
// the vault's picture folder first; dropped text lands as blocks there, the
// way pasted text does; a web address lands as an embed or an image. Note
// files (.md/.txt) are not taken here: the window opens those, and the
// editor declines them so the drop bubbles to it.

import (
	"path/filepath"
	"strings"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison/drag"
	"github.com/richardwilkes/unison/enums/mod"
)

// dropImageExts are the picture extensions a dropped file may name.
var dropImageExts = map[string]bool{
	"png": true, "jpg": true, "jpeg": true, "gif": true,
	"webp": true, "svg": true, "bmp": true,
}

// noteExts are the note extensions the window opens: the editor declines
// drops holding only these so they bubble to it.
func dropNoteExt(p string) bool {
	switch strings.ToLower(filepath.Ext(strings.TrimSpace(p))) {
	case ".md", ".markdown", ".txt":
		return true
	}
	return false
}

// dropFiles splits a drag's file paths into note files (for the window),
// image files (for an image block) and the rest.
func dropFiles(paths []string) (notes, images, other []string) {
	for _, p := range paths {
		p = strings.TrimSpace(p)
		if p == "" {
			continue
		}
		if dropNoteExt(p) {
			notes = append(notes, p)
			continue
		}
		ext := strings.ToLower(strings.TrimPrefix(filepath.Ext(p), "."))
		if dropImageExts[ext] {
			images = append(images, p)
			continue
		}
		other = append(other, p)
	}
	return
}

// webURLs are a drag's web addresses: http and https URLs only, not the
// file:// URLs a file-manager drag also carries.
func webURLs(di drag.Info) []string {
	if !di.HasURLs() {
		return nil
	}
	var out []string
	for _, u := range di.URLs() {
		s := strings.TrimSpace(u.String())
		if isRemoteURL(s) {
			out = append(out, s)
		}
	}
	return out
}

// canAcceptExternal reports whether a drag from another application has
// anything the editor inserts: image files, a web address, or text. Drops
// holding only note files are declined so they bubble to the window, which
// opens them.
func (e *Editor) canAcceptExternal(di drag.Info) bool {
	if di.HasFilePaths() {
		_, images, other := dropFiles(di.FilePaths())
		if len(images) > 0 || len(other) > 0 {
			return true
		}
	}
	if len(webURLs(di)) > 0 {
		return true
	}
	if di.HasString() {
		return strings.TrimSpace(di.Text()) != ""
	}
	return false
}

// dropIndexAt is the block index a drop at a point inserts before: the row
// under the point, or past the last row at its end.
func (e *Editor) dropIndexAt(where geom.Point) int {
	if len(e.tops) == 0 {
		return 0
	}
	if i := e.rowAt(where); i >= 0 {
		return i
	}
	// Above the first row inserts at its start; anywhere else past the
	// last row appends.
	if where.Y < e.tops[0] {
		return 0
	}
	return len(e.Doc.Blocks)
}

// enableExternalDrops makes the editor take files, images and text dropped
// from other applications, with a drop indicator at the insertion row.
func (e *Editor) enableExternalDrops() {
	e.CanAcceptDropCallback = func(di drag.Info) bool { return e.canAcceptExternal(di) }
	e.DragEnteredCallback = func(di drag.Info, where geom.Point, _ mod.Modifiers) drag.Op {
		e.dropIndex = e.dropIndexAt(e.toLocal(where))
		e.MarkForRedraw()
		return drag.Copy
	}
	e.DragUpdatedCallback = func(di drag.Info, where geom.Point, _ mod.Modifiers) drag.Op {
		if idx := e.dropIndexAt(e.toLocal(where)); idx != e.dropIndex {
			e.dropIndex = idx
			e.MarkForRedraw()
		}
		return drag.Copy
	}
	e.DragExitedCallback = func() {
		if e.dropIndex >= 0 {
			e.dropIndex = -1
			e.MarkForRedraw()
		}
	}
	e.DropCallback = func(di drag.Info, where geom.Point, _ mod.Modifiers) bool {
		idx := e.dropIndexAt(e.toLocal(where))
		e.dropIndex = -1
		return e.dropExternal(di, idx)
	}
}

// toLocal converts a drop point in the editor's parent coordinates to the
// editor's own. Drops arrive in the panel's coordinates already; the helper
// keeps the call sites honest if that ever changes.
func (e *Editor) toLocal(where geom.Point) geom.Point { return where }

// dropExternal inserts a drag from another application at index, as one undo
// step per kind: image files as image blocks (copied into the vault first
// when SaveDroppedImage is set), web addresses as embeds or remote images,
// and text as blocks the way pasted text goes.
func (e *Editor) dropExternal(di drag.Info, index int) bool {
	if e.Doc.ReadOnly {
		return false
	}
	done := false
	if di.HasFilePaths() {
		_, images, other := dropFiles(di.FilePaths())
		for _, p := range images {
			stored := p
			if e.SaveDroppedImage != nil {
				if s, ok := e.SaveDroppedImage(p); ok {
					stored = s
				}
			}
			base := filepath.Base(stored)
			e.Doc.InsertMarkdownAt(index, "![]("+stored+")")
			index++
			_ = base
			done = true
		}
		for _, p := range other {
			e.Doc.InsertMarkdownAt(index, "![]("+p+")")
			index++
			done = true
		}
	}
	for _, s := range webURLs(di) {
		e.Doc.InsertMarkdownAt(index, "![]("+s+")")
		index++
		done = true
	}
	if di.HasString() {
		if text := di.Text(); strings.TrimSpace(text) != "" {
			if IsLoneURL(text) {
				e.Doc.InsertMarkdownAt(index, "![]("+strings.TrimSpace(text)+")")
			} else if strings.Contains(text, "\n\n") || PasteOpensAFence(text) {
				e.Doc.InsertMarkdownAt(index, text)
			} else if strings.Contains(text, "\n") {
				e.Doc.InsertPlainTextAt(index, text)
			} else {
				e.Doc.InsertPlainTextAt(index, text)
			}
			done = true
		}
	}
	if done {
		e.touched()
		e.changed()
	}
	return done
}
