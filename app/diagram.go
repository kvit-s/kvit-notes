package app

// What the editor's Mermaid diagrams ask of the window: where to save a
// diagram's picture, and the line the status bar shows after a gesture or a
// save (Kvit's DiagramBlock.qml asks the same of AppActions).

import (
	"os"
	"path/filepath"

	"github.com/richardwilkes/unison"
)

// wireDiagrams connects the editor's diagram blocks to the window.
func (w *Window) wireDiagrams() {
	w.Editor.SaveDiagramPNG = w.saveDiagramPNG
	w.Editor.OnStatus = func(message string) {
		w.message = message
		w.update()
	}
}

// saveDiagramPNG asks where to save a diagram's picture and writes it
// there.
func (w *Window) saveDiagramPNG(data []byte) {
	dlg := unison.NewSaveDialog()
	dlg.SetAllowedExtensions("png")
	name := "diagram.png"
	if w.open != nil {
		name = w.open.Title + " diagram.png"
	}
	dlg.SetInitialFileName(name)
	if !dlg.RunModal() || dlg.Path() == "" {
		return
	}
	target := dlg.Path()
	if filepath.Ext(target) == "" {
		target += ".png"
	}
	if err := os.WriteFile(target, data, 0o644); err != nil {
		w.fail("Could not save the diagram", err)
		return
	}
	w.message = "Diagram saved to " + target
	w.update()
}
