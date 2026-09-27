package app

// Keeping work safe (features.md 12.1, 12.4, and the external-change rule in
// usage.md): the recovery journal written while a note has unsaved changes
// and offered back when a vault opens after an interruption; noticing a note
// changed by another program; restoring an earlier version from the
// backups; the trash, with its notes shown read-only and put back or
// deleted; and a vault that cannot be written, opened for reading.

import (
	"fmt"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
)

// journalDelay is how long after a change the unsaved text is journalled.
const journalDelay = 500 * time.Millisecond

// journal writes the open note's unsaved text to the recovery journal.
func (w *Window) journal() {
	w.journalAt = time.Time{}
	if w.open == nil || !w.Editor.Doc.Dirty || w.Vault.ReadOnly {
		return
	}
	body := w.page.Body
	w.page.Body = editor.Serialize(w.Editor.Doc.Blocks)
	text := w.page.Text()
	w.page.Body = body
	_ = w.Vault.WriteJournal(w.open.Path, text)
}

// offerRecovery shows, above the note list, the unsaved changes an
// interrupted session left, each with Restore and Discard.
func (w *Window) offerRecovery() {
	w.banner.RemoveAllChildren()
	for _, r := range w.Vault.Journals() {
		e := w.Vault.Find(r.Path)
		if e == nil {
			w.Vault.ClearJournal(r.Path)
			continue
		}
		n := kvitui.NewNotice(w.ui, "Recovered unsaved changes: "+e.Title)
		n.Tone = kvitui.ToneWarning
		n.Detail = "An earlier session ended before they were saved."
		n.Action = "Restore"
		n.Dismissible = true
		r := r
		n.OnAction = func() {
			if p, err := w.Vault.Restore(e, r.Text); err != nil {
				w.fail("Could not restore the changes", err)
			} else if e == w.open {
				w.show(p)
			}
			w.Vault.ClearJournal(r.Path)
			w.offerRecovery()
			w.refreshList()
		}
		n.OnDismiss = func() {
			w.Vault.ClearJournal(r.Path)
			w.offerRecovery()
		}
		n.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
		w.banner.AddChild(n)
	}
	w.relayout()
}

// relayout lays the window out again after a part appeared or went.
func (w *Window) relayout() {
	w.Win.Content().MarkForLayoutRecursively()
	w.Win.MarkForRedraw()
}

// watch follows changes other programs make to the vault.
func (w *Window) watch() {
	stop, err := w.Vault.Watch(func(paths []string) {
		unison.InvokeTask(func() { w.external(paths) })
	})
	if err == nil {
		w.stopWatch = stop
	}
}

// external brings the window up to date after other programs changed the
// vault. The open note, changed elsewhere, is reloaded when it has no
// unsaved changes; with unsaved changes the reader chooses which to keep.
func (w *Window) external(paths []string) {
	if w.Win.Window == nil || !w.Win.IsValid() {
		return
	}
	changed := w.Vault.Refresh(paths)
	if w.open != nil {
		for _, rel := range changed {
			if rel != w.open.Path {
				continue
			}
			if w.Vault.Find(rel) == nil {
				w.open, w.page = nil, nil
				w.Editor.SetDoc(editor.NewDoc(nil))
				w.message = "The note was moved or deleted by another program"
				break
			}
			if !w.Editor.Doc.Dirty {
				w.reload()
				w.message = "Reloaded after a change in another program"
			} else {
				w.offerTheirs()
			}
		}
	}
	w.refreshScopes()
	w.refreshList()
	w.Editor.RefreshQueries()
	w.update()
}

// reload reads the open note again.
func (w *Window) reload() {
	p, err := w.Vault.Load(w.open.Path)
	if err != nil {
		w.fail("Could not read the note", err)
		return
	}
	w.show(p)
}

// show puts a page in the editor as the open note's.
func (w *Window) show(p *vault.Page) {
	w.page = p
	doc := editor.NewDoc(editor.ParseMarkdown(p.Body))
	doc.ReadOnly = w.Vault.ReadOnly
	if w.open != nil {
		w.Editor.LoadImage = w.imageLoader(w.open.Path)
	}
	w.Editor.SetDoc(doc)
	w.sessionWords = doc.Stats().Words
	w.refreshBacklinks()
	if w.finder != nil && w.finder.hide != nil {
		w.finder.recompute(true)
	}
	w.tags.SetTags(p.Tags())
	if w.outline.Window() != nil {
		w.outline.Refresh()
	}
}

// offerTheirs asks which version to keep when a note with unsaved changes
// was changed by another program.
func (w *Window) offerTheirs() {
	w.theirs.RemoveAllChildren()
	n := kvitui.NewNotice(w.ui, "This note was changed by another program.")
	n.Tone = kvitui.ToneWarning
	n.Detail = "You have unsaved changes too. Keep yours, which will be saved over the other version, or load the other version and lose yours."
	n.Action = "Load the other version"
	n.Dismissible = true
	n.OnAction = func() {
		w.theirs.RemoveAllChildren()
		w.reload()
		w.Vault.ClearJournal(w.open.Path)
		w.relayout()
		w.update()
	}
	n.OnDismiss = func() {
		w.theirs.RemoveAllChildren()
		w.relayout()
		w.saveNow()
	}
	n.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	w.theirs.AddChild(n)
	w.relayout()
}

// openBackups shows the open note's backups, each drawn read-only, and
// restores the one chosen.
func (w *Window) openBackups() {
	if w.open == nil {
		return
	}
	e := w.open
	w.saveNow()
	backups := w.Vault.Backups(e.Path)
	if len(backups) == 0 {
		w.message = "No earlier versions of this note yet: one is kept before a save, at most every ten minutes"
		w.update()
		return
	}
	var options []kvitui.Option
	for i, b := range backups {
		options = append(options, kvitui.Option{Value: fmt.Sprint(i), Label: b.Time.Format("Jan 2, 2006 15:04:05")})
	}
	pick := kvitui.NewSelect(w.ui, "Version", options...)
	preview := editor.New(w.ui, editor.NewDoc(nil))
	preview.Placeholder = ""
	shown := 0
	showVersion := func(i int) {
		shown = i
		text, err := backups[i].Read()
		if err != nil {
			text = "This version could not be read: " + err.Error()
		}
		doc := editor.NewDoc(editor.ParseMarkdown(vault.ParseText(text).Body))
		doc.ReadOnly = true
		preview.SetDoc(doc)
	}
	showVersion(0)
	pick.OnChoose = func(v string) {
		var i int
		fmt.Sscan(v, &i)
		showVersion(i)
	}
	region := kvitui.NewRegion(w.ui, preview)
	region.Padding = kvitui.Px(0)
	body := kvitui.Column(w.ui, kvitui.SizeSpace, pick, kvitui.Height(w.ui, kvitui.Px(320), region))
	d := kvitui.NewDialog(w.ui, "Earlier versions of “"+e.Title+"”", body)
	d.Detail = "A version is kept before a save, at most every ten minutes, and the ten newest stay."
	d.ConfirmText = "Restore this version"
	d.OnAccept = func() {
		text, err := backups[shown].Read()
		if err != nil {
			w.fail("Could not read that version", err)
			return
		}
		p, err := w.Vault.Restore(e, text)
		if err != nil {
			w.fail("Could not restore that version", err)
			return
		}
		if e == w.open {
			w.show(p)
		}
		w.message = "Restored the version of " + backups[shown].Time.Format("Jan 2, 15:04")
		w.refreshList()
		w.update()
	}
	d.Open(w.Win)
}

// showTrashed shows a trashed note read-only in the editor.
func (w *Window) showTrashed(t vault.Trashed) {
	w.saveNow()
	w.open, w.page = nil, nil
	w.trashed = &t
	text, err := w.Vault.ReadTrashed(t)
	if err != nil {
		text = ""
	}
	doc := editor.NewDoc(editor.ParseMarkdown(vault.ParseText(text).Body))
	doc.ReadOnly = true
	w.Editor.SetDoc(doc)
	w.tags.SetTags(nil)
	w.update()
}

// trashMenu is a trashed item's menu.
func (w *Window) trashMenu(t vault.Trashed) []kvitui.MenuItem {
	return []kvitui.MenuItem{
		{Text: "Put back", OnSelect: func() { w.untrash(t) }},
		{Separator: true},
		{Text: "Delete for good", Danger: true, OnSelect: func() { w.confirmDeleteForever(t) }},
	}
}

func (w *Window) untrash(t vault.Trashed) {
	rel, err := w.Vault.Untrash(t)
	if err != nil {
		w.fail("Could not put it back", err)
		return
	}
	w.trashed = nil
	w.refreshScopes()
	w.refreshList()
	if e := w.Vault.Find(rel); e != nil {
		w.scope = Scope{Kind: ScopeAll}
		w.refreshScopes()
		w.refreshList()
		w.openNote(e)
	}
	w.message = "Put back as " + rel
	w.update()
}

func (w *Window) confirmDeleteForever(t vault.Trashed) {
	d := kvitui.NewDialog(w.ui, "Delete “"+t.Title+"” for good?")
	d.Detail = "It cannot be brought back."
	d.ConfirmText, d.Destructive = "Delete for good", true
	d.OnAccept = func() {
		if err := w.Vault.DeleteForever(t); err != nil {
			w.fail("Could not delete it", err)
			return
		}
		w.trashed = nil
		w.refreshScopes()
		w.refreshList()
	}
	d.Open(w.Win)
}

func (w *Window) confirmEmptyTrash() {
	n := w.Vault.TrashCount()
	if n == 0 {
		return
	}
	d := kvitui.NewDialog(w.ui, fmt.Sprintf("Empty the trash of %d items?", n))
	d.Detail = "Everything in it is deleted for good."
	d.ConfirmText, d.Destructive = "Empty trash", true
	d.OnAccept = func() {
		if err := w.Vault.EmptyTrash(); err != nil {
			w.fail("Could not empty the trash", err)
			return
		}
		w.trashed = nil
		w.Editor.SetDoc(editor.NewDoc(nil))
		w.refreshScopes()
		w.refreshList()
	}
	d.Open(w.Win)
}
