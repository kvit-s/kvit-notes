package app

// Export and import (features.md 12.5–12.6, Kvit's ExportDialog.qml and
// ImportDialog.qml over the export package's port of DocumentExporter and
// DocumentImporter). Export writes the open note, or every note of the
// vault, as Markdown, HTML or plain text, to a file or a folder chosen;
// the open note is exported as it is in the editor, saved or not. Import
// copies Markdown and text files, or a folder of them, into the folder
// shown, after a summary of what it will do.

import (
	"errors"
	"fmt"
	"os"
	"path"
	"path/filepath"
	"strings"

	"github.com/kvit-s/kvit-notes/export"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/richardwilkes/unison"
)

// exportFormats are the export dialog's formats, as it names them.
var exportFormats = []struct {
	format export.Format
	label  string
}{
	{export.FormatMarkdown, "Markdown (.md)"}, {export.FormatHTML, "HTML (.html)"}, {export.FormatPDF, "PDF (.pdf)"},
	{export.FormatText, "Plain text (.txt)"},
}

// exportColors are the theme's colours for an HTML export.
func exportColors(t tokens.Tokens) export.Colors {
	return export.Colors{
		Text: t.TextPrimary.Hex(), Muted: t.TextSecondary.Hex(), Background: t.WindowBackground.Hex(),
		Accent: t.Link.Hex(), Border: t.Border.Hex(), CodeBackground: t.CodePanelBackground.Hex(),
		Danger: t.Danger.Hex(), Highlight: t.HighlightBackground.Hex(), CodeKeyword: t.CodeKeyword.Hex(),
		CodeType: t.CodeType.Hex(), CodeString: t.CodeString.Hex(), CodeComment: t.CodeComment.Hex(),
		CodeNumber: t.CodeNumber.Hex(),
	}
}

// exportOptions are the options for exporting the open note.
func (w *Window) exportOptions() export.Options {
	opt := export.Options{Colors: exportColors(w.ui.Theme.Tokens()), VaultRoot: w.Vault.Root, SiteRoot: w.Vault.SiteRoot()}
	if w.open != nil {
		opt.NoteDir = filepath.Dir(w.Vault.Path(w.open.Path))
	}
	return opt
}

// openExport shows the export dialog.
func (w *Window) openExport() {
	ui := w.ui
	var options []kvitui.Option
	for _, f := range exportFormats {
		options = append(options, kvitui.Option{Value: string(f.format), Label: f.label})
	}
	format := kvitui.NewSelect(ui, "Format", options...)
	format.Current = w.prefs.string("export.format", string(export.FormatHTML))
	scope := kvitui.NewSegmented(ui, "Scope",
		kvitui.Option{Value: "note", Label: "This note"}, kvitui.Option{Value: "vault", Label: "Whole collection"})
	scope.Current = "note"
	if w.open == nil {
		scope.Current = "vault"
	}
	single := kvitui.NewCheck(ui, "One combined file")
	d := kvitui.NewDialog(ui, "Export", settingRow(ui, "Format", format), settingRow(ui, "Scope", scope, single))
	d.Detail = "PDF is made for this note only."
	d.ConfirmText = "Choose destination…"
	d.OnAccept = func() {
		f := export.Format(format.Current)
		w.prefs.set("export.format", string(f))
		if scope.Current == "vault" && f != export.FormatPDF {
			w.exportVault(f, single.Checked)
		} else {
			w.exportNote(f)
		}
	}
	d.Open(w.Win)
}

// exportNote writes the open note, as the editor holds it, to a file.
func (w *Window) exportNote(f export.Format) {
	if w.open == nil {
		return
	}
	var data []byte
	var err error
	if f != export.FormatPDF {
		if data, err = export.Note(w.Editor.Doc.Blocks, w.open.Title, f, w.exportOptions()); err != nil {
			w.fail("Could not export the note", err)
			return
		}
	}
	dlg := unison.NewSaveDialog()
	dlg.SetAllowedExtensions(f.Extension())
	dlg.SetInitialFileName(w.open.Title + "." + f.Extension())
	if !dlg.RunModal() || dlg.Path() == "" {
		return
	}
	target := dlg.Path()
	if filepath.Ext(target) == "" {
		target += "." + f.Extension()
	}
	if err := w.refuseVaultNote(target); err != nil {
		w.fail("Could not export the note", err)
		return
	}
	if f == export.FormatPDF {
		err = w.writePDF(w.Editor.Doc.Blocks, w.open.Title, target)
	} else {
		err = os.WriteFile(target, data, 0o644)
	}
	if err != nil {
		w.fail("Could not export the note", err)
		return
	}
	w.say("Exported to " + target)
}

// refuseVaultNote refuses to write an export over one of the vault's notes.
func (w *Window) refuseVaultNote(target string) error {
	if rel, err := filepath.Rel(w.Vault.Root, target); err == nil && !strings.HasPrefix(rel, "..") {
		if w.Vault.Find(filepath.ToSlash(rel)) != nil {
			return errors.New("that file is one of the vault's notes")
		}
	}
	return nil
}

// exportVault writes every note of the vault into a folder chosen, the
// open note as the editor holds it.
func (w *Window) exportVault(f export.Format, single bool) {
	dlg := unison.NewOpenDialog()
	dlg.SetCanChooseFiles(false)
	dlg.SetCanChooseDirectories(true)
	if !dlg.RunModal() || len(dlg.Paths()) == 0 {
		return
	}
	dest := dlg.Paths()[0]
	w.saveNow()
	var notes []export.VaultNote
	var all []string
	for _, e := range w.Vault.Entries {
		all = append(all, e.Path)
		data, err := os.ReadFile(w.Vault.Path(e.Path))
		if err != nil {
			continue
		}
		notes = append(notes, export.VaultNote{RelPath: e.Path, Text: string(data), Title: e.Title})
	}
	files, err := export.Vault(export.VaultExport{Root: w.Vault.Root, Notes: notes, AllNotes: all, Dest: dest,
		Format: f, SingleFile: single, Options: export.Options{Colors: exportColors(w.ui.Theme.Tokens()), SiteRoot: w.Vault.SiteRoot()}})
	if err != nil {
		w.fail("Could not export the notes", err)
		return
	}
	for _, file := range files {
		if err := os.MkdirAll(filepath.Dir(file.Path), 0o755); err == nil {
			err = os.WriteFile(file.Path, file.Content, 0o644)
		}
		if err != nil {
			w.fail("Could not export the notes", err)
			return
		}
	}
	w.say(fmt.Sprintf("Exported %d files to %s", len(files), dest))
}

// importFolder is the vault folder an import goes into: the one shown.
func (w *Window) importFolder() (string, string) {
	if w.scope.Kind == ScopeFolder {
		return w.scope.Path, "“" + w.scope.Path + "”"
	}
	return "", "the top level"
}

// openImport shows the import dialog.
func (w *Window) openImport() {
	ui := w.ui
	folder, where := w.importFolder()
	var d *kvitui.Dialog
	files := kvitui.NewButton(ui, "Choose files…")
	files.OnClick = func() {
		dlg := unison.NewOpenDialog()
		dlg.SetAllowedExtensions("md", "markdown", "txt")
		dlg.SetAllowsMultipleSelection(true)
		if !dlg.RunModal() || len(dlg.Paths()) == 0 {
			return
		}
		paths := dlg.Paths()
		sum := export.DryRunFiles(w.Vault.Root, paths, folder)
		d.Accept()
		w.confirmImport(sum, where, func() (export.ImportPlan, error) {
			return export.ImportFiles(w.Vault.Root, paths, folder, export.ImportOptions{})
		})
	}
	dir := kvitui.NewButton(ui, "Choose folder…")
	dir.OnClick = func() {
		dlg := unison.NewOpenDialog()
		dlg.SetCanChooseFiles(false)
		dlg.SetCanChooseDirectories(true)
		if !dlg.RunModal() || len(dlg.Paths()) == 0 {
			return
		}
		src := dlg.Paths()[0]
		sum, err := export.DryRunFolder(w.Vault.Root, src, folder)
		if err != nil {
			w.fail("Could not read the folder", err)
			return
		}
		d.Accept()
		w.confirmImport(sum, where, func() (export.ImportPlan, error) {
			return export.ImportFolder(w.Vault.Root, src, folder, export.ImportOptions{})
		})
	}
	dest := kvitui.NewLabel(ui, "Destination: "+where)
	d = kvitui.NewDialog(ui, "Import into collection", dest, files, dir)
	d.ConfirmText, d.CancelText = "", "Close"
	d.Open(w.Win)
}

// confirmImport says what an import will do and does it when agreed.
func (w *Window) confirmImport(sum export.DryRun, where string, plan func() (export.ImportPlan, error)) {
	if sum.Files == 0 {
		w.say("No Markdown or text files to import")
		return
	}
	detail := fmt.Sprintf("Import %d file(s) into %s.", sum.Files, where)
	if sum.Folders > 0 {
		detail += fmt.Sprintf("\n%d folder(s) will be recreated.", sum.Folders)
	}
	if sum.Collisions > 0 {
		detail += fmt.Sprintf("\n%d name collision(s) will be suffixed.", sum.Collisions)
	}
	d := kvitui.NewDialog(w.ui, "Confirm import")
	d.Detail = detail
	d.ConfirmText = "OK"
	d.OnAccept = func() { w.runImport(plan) }
	d.Open(w.Win)
}

// runImport writes the notes an import plans, and opens the first.
func (w *Window) runImport(plan func() (export.ImportPlan, error)) {
	p, err := plan()
	if err != nil {
		w.fail("Could not import", err)
		return
	}
	for _, f := range p.Folders {
		if err := os.MkdirAll(w.Vault.Path(f), 0o755); err != nil {
			w.fail("Could not import", err)
			return
		}
	}
	written := 0
	for _, f := range p.Files {
		target := w.Vault.Path(f.RelPath)
		if _, err := os.Stat(target); err == nil {
			continue // never over a note that is there
		}
		if err := os.WriteFile(target, f.Content, 0o644); err != nil {
			w.fail("Could not import "+path.Base(f.RelPath), err)
			continue
		}
		written++
	}
	_ = w.Vault.Rescan()
	w.refreshScopes()
	w.refreshList()
	msg := fmt.Sprintf("Imported %d note(s)", written)
	if len(p.Skipped) > 0 {
		msg += fmt.Sprintf(", %d skipped as too large or unreadable", len(p.Skipped))
	}
	w.say(msg)
	if len(p.Files) > 0 {
		if e := w.Vault.Find(p.Files[0].RelPath); e != nil {
			w.openNote(e)
		}
	}
}
