package app

// Note templates: File, New from template makes a note named after the
// template, with the template's text filled in and its tags and favourite
// mark; File, Manage templates edits, adds and deletes the templates in
// .kvit/templates, and saves the open note as one.

import (
	"time"

	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
)

// newFromTemplate makes a note from a template in the folder shown, and
// opens it.
func (w *Window) newFromTemplate(name string) {
	folder := ""
	if w.scope.Kind == ScopeFolder {
		folder = w.scope.Path
	}
	e, err := w.Vault.CreateTitled(folder, name)
	if err != nil {
		w.fail("Could not make the note", err)
		return
	}
	inst, err := w.Vault.Instantiate(name, e.Title, time.Now())
	if err != nil {
		w.fail("Could not read the template", err)
		return
	}
	p := vault.ParseText("")
	p.Body = inst.Body
	if len(inst.Tags) > 0 {
		p.SetTags(inst.Tags)
	}
	if inst.Favorite {
		p.SetFavorite(true)
	}
	if err := w.Vault.Save(e, p); err != nil {
		w.fail("The note was made but its template text could not be saved", err)
	}
	w.refreshScopes()
	w.refreshList()
	w.openNote(e)
	w.Editor.FocusBlock(0, 0)
}

// openTemplates shows the templates dialog on the first template.
func (w *Window) openTemplates() { w.openTemplatesAt("") }

// openTemplatesAt shows the templates dialog with a template chosen.
func (w *Window) openTemplatesAt(chosen string) {
	w.Vault.SeedTemplates()
	names := w.Vault.TemplateNames()
	if chosen == "" && len(names) > 0 {
		chosen = names[0]
	}
	var options []kvitui.Option
	for _, n := range names {
		options = append(options, kvitui.Option{Value: n, Label: n})
	}
	pick := kvitui.NewSelect(w.ui, "Template", options...)
	pick.Current = chosen
	pick.Placeholder = "No templates"
	area := kvitui.NewTextArea(w.ui)
	area.Mono = true
	area.Label = "Template text"
	area.Placeholder = "Template Markdown: use {{date}}, {{time}}, {{title}}, {{date:FORMAT}}"
	load := func(name string) {
		text, _ := w.Vault.ReadTemplate(name)
		area.SetText(text)
		area.SetEnabled(name != "")
	}
	load(chosen)
	var d *kvitui.Dialog
	reopen := func(name string) {
		unison.InvokeTask(func() { w.openTemplatesAt(name) })
	}
	pick.OnChoose = func(name string) {
		chosen = name
		load(name)
	}
	save := kvitui.NewButton(w.ui, "Save template")
	save.Form = kvitui.ButtonPrimary
	save.OnClick = func() {
		if chosen == "" {
			return
		}
		if err := w.Vault.WriteTemplate(chosen, area.Text()); err != nil {
			w.fail("Could not save the template", err)
			return
		}
		w.message = "Saved the template " + chosen
		w.update()
	}
	add := kvitui.NewButton(w.ui, "New…")
	add.OnClick = func() {
		d.Accept()
		w.askNameWith("New template", "", "Add", func(name string) {
			if err := w.Vault.WriteTemplate(name, "# {{title}}\n\n"); err != nil {
				w.fail("Could not add the template", err)
				return
			}
			reopen(name)
		})
	}
	del := kvitui.NewButton(w.ui, "Delete")
	del.Danger = true
	del.OnClick = func() {
		if chosen == "" {
			return
		}
		if err := w.Vault.DeleteTemplate(chosen); err != nil {
			w.fail("Could not delete the template", err)
			return
		}
		d.Accept()
		reopen("")
	}
	fromNote := kvitui.NewButton(w.ui, "Save current note…")
	fromNote.SetEnabled(w.open != nil)
	fromNote.OnClick = func() {
		if w.open == nil {
			return
		}
		d.Accept()
		w.saveNow()
		w.askNameWith("Save note as template", w.open.Title, "Save", func(name string) {
			if err := w.Vault.WriteTemplate(name, w.page.Text()); err != nil {
				w.fail("Could not save the template", err)
				return
			}
			reopen(name)
		})
	}
	buttons := kvitui.Row(w.ui, kvitui.SizeSpaceSnug, add, del, fromNote)
	body := kvitui.Column(w.ui, kvitui.SizeSpace, pick, buttons, kvitui.Height(w.ui, kvitui.Px(240), area), save)
	d = kvitui.NewDialog(w.ui, "Manage templates", body)
	d.Detail = "A new note made from a template takes its text, with {{title}}, {{date}} and {{time}} filled in, and its tags."
	d.ConfirmText, d.CancelText = "", "Close"
	d.Open(w.Win)
}
