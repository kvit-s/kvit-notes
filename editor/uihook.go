package editor

import (
	"weak"

	kvitui "github.com/kvit-s/kvit-ui"
)

// changeWatcher is a UI that lets go of a hook once its owner is no longer
// in use (kvit-ui's UI.OnChangedWhile). The editor asks for it at run time,
// so it builds against whichever kvit-ui its embedder pins, one from before
// that method included.
type changeWatcher interface {
	OnChangedWhile(alive func() bool, fn func())
}

// followUIChanges has the editor lay its note out and draw it again after
// the theme, the interface size or the typography changed, for as long as
// the editor is in use. The UI lives as long as the program, while an
// embedder may make and throw away editors many times over (Kvit Works
// draws each turn of a conversation in one), so the hook holds the editor
// weakly: one holding it strongly would keep every editor ever made, with
// its note, its laid-out lines and its pictures.
func (e *Editor) followUIChanges(ui *kvitui.UI) {
	self := weak.Make(e)
	changed := func() {
		if e := self.Value(); e != nil {
			e.generation++
			e.changed()
		}
	}
	if w, ok := any(ui).(changeWatcher); ok {
		w.OnChangedWhile(func() bool { return self.Value() != nil }, changed)
		return
	}
	ui.OnChanged(changed)
}
