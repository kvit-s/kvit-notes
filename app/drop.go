package app

// Opening a note file dropped on a window: a Markdown or text file dragged
// from the file manager opens as the File menu's Open File… would open it, in
// the vault's window when it is one of the vault's notes, and on its own
// otherwise.

import (
	"path/filepath"
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/toolbox/v2/uti"
	"github.com/richardwilkes/unison/drag"
	"github.com/richardwilkes/unison/enums/mod"
)

// AcceptDroppedNotes makes a window take note files dropped anywhere on it,
// giving each to open. A part of the window that takes drops of its own
// comes first, as unison asks the panel under the pointer before its
// parents, and the window's content is the last of those. The window also
// registers the text and address types so the editor can take drops from
// other applications; the content itself only takes note
// files.
func AcceptDroppedNotes(win *kvitui.Window, open func(path string)) {
	win.RegisterForDragTypes(uti.FileURL, uti.URL, uti.UTF8PlainText)
	content := win.Content()
	content.CanAcceptDropCallback = func(di drag.Info) bool { return len(droppedNotes(di)) > 0 }
	content.DragEnteredCallback = func(di drag.Info, _ geom.Point, _ mod.Modifiers) drag.Op {
		if di.SourceDragOpMask()&drag.Copy != 0 {
			return drag.Copy
		}
		return di.SourceDragOpMask() & drag.Move
	}
	content.DropCallback = func(di drag.Info, _ geom.Point, _ mod.Modifiers) bool {
		paths := droppedNotes(di)
		for _, p := range paths {
			open(p)
		}
		return len(paths) > 0
	}
}

// droppedNotes are the files a drag holds that open as notes: the kinds the
// Open File… dialog lists.
func droppedNotes(di drag.Info) []string {
	if !di.HasFilePaths() {
		return nil
	}
	var notes []string
	for _, p := range di.FilePaths() {
		switch strings.ToLower(filepath.Ext(p)) {
		case ".md", ".markdown", ".txt":
			notes = append(notes, p)
		}
	}
	return notes
}

// openDropped opens a dropped file: one of this vault's notes in this
// window, anything else on its own.
func (w *Window) openDropped(path string) {
	abs, err := filepath.Abs(path)
	root, rerr := filepath.Abs(w.Vault.Root)
	if err == nil && rerr == nil {
		if rel, err := filepath.Rel(root, abs); err == nil && rel != ".." && !strings.HasPrefix(rel, ".."+string(filepath.Separator)) {
			if e := w.Vault.Find(filepath.ToSlash(rel)); e != nil {
				w.openNote(e)
				w.Win.ToFront()
				return
			}
		}
	}
	if OpenFile != nil {
		OpenFile(w.ui, path)
	}
}
