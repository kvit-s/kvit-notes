package app

// Moving between notes: back and forward through the notes opened, from the
// toolbar's arrows and Alt+Left and Alt+Right, and the quick switcher,
// Ctrl+P, which finds a note by its title or path and makes one from the
// words typed when none matches.

import (
	"path"

	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// maxHistory is how many notes back the history reaches.
const maxHistory = 100

// remember records that a note was left for another, so Back returns to
// it; going somewhere new drops what Forward would have returned to.
func (w *Window) remember(from *vault.Entry) {
	if from == nil || w.travelling {
		return
	}
	w.back = append(w.back, from.Path)
	if len(w.back) > maxHistory {
		w.back = w.back[1:]
	}
	w.forward = nil
}

// goBack opens the note before this one in the history.
func (w *Window) goBack() { w.travel(&w.back, &w.forward) }

// goForward opens the note Back left.
func (w *Window) goForward() { w.travel(&w.forward, &w.back) }

func (w *Window) travel(from, to *[]string) {
	for len(*from) > 0 {
		p := (*from)[len(*from)-1]
		*from = (*from)[:len(*from)-1]
		e := w.Vault.Find(p)
		if e == nil {
			continue
		}
		if w.open != nil {
			*to = append(*to, w.open.Path)
		}
		w.travelling = true
		w.openNote(e)
		w.travelling = false
		return
	}
}

// openSwitcher shows the quick switcher over the window.
func (w *Window) openSwitcher() {
	if w.switcher != nil {
		return
	}
	var source []kvitui.Suggestion
	for _, e := range w.Vault.Entries {
		label := e.Title
		if e.Folder != "" {
			label += ", in " + e.Folder
		}
		source = append(source, kvitui.Suggestion{Value: e.Path, Label: label})
	}
	field := kvitui.NewTypeAhead(w.ui, "Find or create a note", true, source...)
	field.Placeholder = "Find or create a note…"
	field.MaximumSuggestions = 12
	field.Rank = func(typed string, _ []kvitui.Suggestion) []kvitui.Suggestion {
		var out []kvitui.Suggestion
		for _, e := range rankNotes(w.Vault.Entries, typed, 0) {
			label := e.Title
			if e.Folder != "" {
				label += ", in " + e.Folder
			}
			out = append(out, kvitui.Suggestion{Value: e.Path, Label: label})
		}
		return out
	}
	card := kvitui.NewCard(w.ui, kvitui.Width(w.ui, kvitui.Px(460), field))
	var hide func()
	closeIt := func() {
		if hide != nil {
			hide()
			hide = nil
		}
		w.switcher = nil
	}
	field.OnChoose = func(value string) {
		closeIt()
		if e := w.Vault.Find(value); e != nil {
			w.openNote(e)
			w.Editor.FocusBlock(0, 0)
			return
		}
		w.createTitled(value)
	}
	popup := &kvitui.Popup{Panel: card, Modal: true, OnEscape: closeIt, OnPressOutside: closeIt,
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			return geom.NewRect(bounds.X+(bounds.Width-size.Width)/2, bounds.Y+bounds.Height/6, size.Width, size.Height)
		}}
	hide = w.Win.Show(popup)
	w.switcher = popup
	unison.InvokeTask(field.Focus)
}

// createTitled makes a note with a title in the folder shown, as the quick
// switcher does for words no note matches.
func (w *Window) createTitled(title string) {
	folder := ""
	if w.scope.Kind == ScopeFolder {
		folder = w.scope.Path
	}
	e, err := w.Vault.Create(folder)
	if err == nil {
		err = w.Vault.Rename(e, path.Base(title))
	}
	if err != nil {
		w.fail("Could not make the note", err)
		return
	}
	w.refreshScopes()
	w.refreshList()
	w.openNote(e)
	w.Editor.FocusBlock(0, 0)
}
