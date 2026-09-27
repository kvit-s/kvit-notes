package app

// Renaming and moving notes and folders without breaking the links to
// them (Kvit's NoteRenameWorkflow.qml over src/repository/notecollection.cpp).
// When wiki links name what is being renamed, the reader is asked first:
// Update links rewrites them to the new name, Rename only leaves them, and
// Cancel does nothing. Updating goes through the vault's table of renamed
// notes, .kvit/redirects.json, as the Qt app's does: the rename is recorded,
// every note linking through it is rewritten, and the entry is dropped once
// no link needs it, so an interrupted rewrite is finished the next time.

import (
	"fmt"
	"strings"

	"github.com/kvit-s/kvit-notes/links"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
)

// relocate renames or moves a note with do, asking about the links to it
// first when there are any, and runs after once it is done.
func (w *Window) relocate(e *vault.Entry, do func() error, after func()) {
	w.saveNow()
	old := e.Path
	refs := w.linkIndex().Referrers(old, w.bodies())
	count, notes := 0, 0
	for _, r := range refs {
		count += r.Count
		notes++
	}
	apply := func(update bool) {
		if err := do(); err != nil {
			w.fail("Could not rename the note", err)
			return
		}
		w.rewriteLinks(update, func(r *links.Redirects) {
			r.Retarget(old, e.Path)
			if update {
				r.Record(old, e.Path)
			}
		})
		after()
	}
	if count == 0 {
		apply(false)
		return
	}
	w.askUpdateLinks(count, notes, apply)
}

// relocateFolder renames a folder with do, which returns its new path,
// asking about the links whose paths go through it first.
func (w *Window) relocateFolder(folder string, do func() (string, error), after func(to string)) {
	w.saveNow()
	refs := links.FolderReferrers(folder, w.bodies())
	count := 0
	for _, r := range refs {
		count += r.Count
	}
	apply := func(update bool) {
		var inside []string
		for _, e := range w.Vault.Entries {
			if strings.HasPrefix(e.Path, folder+"/") {
				inside = append(inside, e.Path)
			}
		}
		to, err := do()
		if err != nil {
			w.fail("Could not rename the folder", err)
			return
		}
		w.rewriteLinks(update, func(r *links.Redirects) {
			if update {
				r.RecordFolder(folder, to, inside)
			}
		})
		after(to)
	}
	if count == 0 {
		apply(false)
		return
	}
	w.askUpdateLinks(count, len(refs), apply)
}

// askUpdateLinks asks whether to update the links a rename would break.
func (w *Window) askUpdateLinks(count, notes int, apply func(update bool)) {
	var d *kvitui.Dialog
	only := kvitui.NewButton(w.ui, "Rename only")
	only.Explanation = "Rename without changing the links, which then name nothing"
	only.OnClick = func() {
		d.OnAccept, d.OnReject = nil, nil
		d.Accept()
		apply(false)
	}
	only.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Start})
	d = kvitui.NewDialog(w.ui, "Update wiki-links?", only)
	d.Detail = fmt.Sprintf("Update %d links in %d notes?", count, notes)
	if count == 1 && notes == 1 {
		d.Detail = "Update 1 link in 1 note?"
	}
	d.ConfirmText = "Update links"
	d.OnAccept = func() { apply(true) }
	d.Open(w.Win)
}

// rewriteLinks brings the redirect table and the notes up to a rename that
// has happened: change edits the table, and with update every note linking
// through the table is rewritten to name the note directly, the open one
// reloaded, and entries no link needs any more are dropped.
func (w *Window) rewriteLinks(update bool, change func(*links.Redirects)) {
	ix := w.linkIndex()
	change(ix.Redirects)
	if update {
		failed := 0
		for p, body := range w.bodies() {
			if !ix.NeedsRewrite(body) {
				continue
			}
			rewritten, n := ix.RewriteRedirected(body)
			if n == 0 {
				continue
			}
			e := w.Vault.Find(p)
			page, err := w.Vault.Load(p)
			if e == nil || err != nil {
				failed++
				continue
			}
			page.Body = rewritten
			if err := w.Vault.Save(e, page); err != nil {
				failed++
				continue
			}
			if e == w.open {
				w.show(page)
			}
		}
		if failed > 0 {
			w.say(fmt.Sprintf("Some links are not rewritten yet: %d notes could not be read or written", failed))
		}
	}
	ix.PruneRedirects(w.bodies())
	if !w.Vault.ReadOnly {
		_ = ix.Redirects.Save(w.Vault.Root)
	}
}
