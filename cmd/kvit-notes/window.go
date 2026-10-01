package main

// The note window: a strip of formatting buttons, the editor in a scrolling
// region, and a status line saying what the caret is in and whether the note
// is saved. It shows a note file opened on its own, and the scenarios run in
// it.

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/kvit-s/kvit-notes/app"
	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
)

// noteWindow is one window editing one note.
type noteWindow struct {
	ui      *kvitui.UI
	win     *kvitui.Window
	ed      *editor.Editor
	region  *kvitui.Region
	strip   *app.Toolbar
	status  *kvitui.StatusBar
	path    string
	message string // what the last save said
	// page is the file's front matter, kept as it was read and written back
	// on save; nil for a note that had none.
	page *vault.Page
}

// newNoteWindow opens a window editing doc, which is saved to path.
func newNoteWindow(ui *kvitui.UI, doc *editor.Doc, path string) (*noteWindow, error) {
	w, err := kvitui.NewWindow(ui, title(path))
	if err != nil {
		return nil, err
	}
	n := &noteWindow{ui: ui, win: w, path: path}
	n.ed = editor.New(ui, doc)
	n.ed.OnChange = n.update
	n.ed.SetMathCommands(app.MathCommands(ui))
	n.region = kvitui.NewRegion(ui, n.ed)
	n.region.Padding = kvitui.Px(0)
	n.status = kvitui.NewStatusBar(ui)

	body := unison.NewPanel()
	body.SetLayout(&unison.FlexLayout{Columns: 1, HAlign: align.Fill, VAlign: align.Fill})
	n.strip = app.NewToolbar(ui, n.ed, app.ToolbarHooks{File: n.fileMenu})
	strip := n.strip.Panel
	strip.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	n.region.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	body.AddChild(strip)
	body.AddChild(n.region)
	w.SetBody(body)
	w.SetStatusBar(n.status)
	w.SidebarVisible = false
	w.OnKeyDown = func(key unison.KeyCode, mods mod.Modifiers, _ bool) bool {
		if key == unison.KeyS && mods.OSMenuCommandDown() {
			n.save()
			return true
		}
		return false
	}
	app.ClosesToTray(ui, w, n.saveToFile)
	app.AcceptDroppedNotes(w, func(p string) { openFileWindow(ui, p) })
	n.update()
	return n, nil
}

func title(path string) string {
	if path == "" {
		return "Kvit Notes"
	}
	return filepath.Base(path) + " — Kvit Notes"
}

// update shows the caret's block and the note's counts.
func (n *noteWindow) update() {
	d := n.ed.Doc
	st := "Saved"
	if d.Dirty {
		st = "Unsaved"
	}
	if n.message != "" {
		st = n.message
	}
	n.status.Activity = st
	var facts []string
	if b := d.CaretBlock(); b != nil && d.Focused {
		i := d.Index(b.ID)
		ln, col := 1, 1
		r := []rune(b.Text)
		for _, c := range r[:min(d.Caret.Off, len(r))] {
			if c == '\n' {
				ln, col = ln+1, 1
			} else {
				col++
			}
		}
		facts = append(facts, fmt.Sprintf("Block %d · Ln %d, Col %d", i+1, ln, col))
	} else if ids := n.ed.SelectedBlocks(); len(ids) > 0 {
		facts = append(facts, fmt.Sprintf("%d blocks selected", len(ids)))
	}
	if n.path != "" {
		facts = append(facts, filepath.Base(n.path))
	} else {
		facts = append(facts, "Not saved to disk")
	}
	words, chars := d.Words()
	facts = append(facts, fmt.Sprintf("%d blocks", len(d.Blocks)), fmt.Sprintf("%d words", words), fmt.Sprintf("%d chars", chars))
	n.status.Facts = facts
	n.strip.Update()
	n.status.MarkForLayoutAndRedraw()
}

// save writes the note to its file, asking for one when it has none.
func (n *noteWindow) save() {
	if n.path == "" {
		n.saveAs()
		return
	}
	d := n.ed.Doc
	text := editor.Serialize(d.Blocks)
	if n.page != nil {
		n.page.Body = text
		text = n.page.Text()
	}
	if err := os.WriteFile(n.path, []byte(text), 0o644); err != nil {
		n.message = "Save failed: " + err.Error()
	} else {
		d.Dirty = false
		n.message = ""
	}
	n.update()
}

// saveToFile saves a note that has a file and changes, without asking for
// a file: what closing the window into the tray does.
func (n *noteWindow) saveToFile() {
	if n.path != "" && n.ed.Doc.Dirty {
		n.save()
	}
}

// saveAs asks for a file and saves the note to it.
func (n *noteWindow) saveAs() {
	d := unison.NewSaveDialog()
	d.SetAllowedExtensions("md")
	name := "Untitled.md"
	if n.path != "" {
		name = filepath.Base(n.path)
	}
	d.SetInitialFileName(name)
	if !d.RunModal() || d.Path() == "" {
		return
	}
	n.path = d.Path()
	if filepath.Ext(n.path) == "" {
		n.path += ".md"
	}
	n.win.SetTitle(title(n.path))
	n.save()
}

// fileMenu is the File menu of a window editing one file.
func (n *noteWindow) fileMenu() []kvitui.MenuItem {
	return []kvitui.MenuItem{
		{Text: "Open File…", OnSelect: func() {
			d := unison.NewOpenDialog()
			d.SetAllowedExtensions("md", "markdown", "txt")
			if d.RunModal() && len(d.Paths()) > 0 {
				openFileWindow(n.ui, d.Paths()[0])
			}
		}},
		{Text: "Open Folder…", OnSelect: func() {
			d := unison.NewOpenDialog()
			d.SetCanChooseFiles(false)
			d.SetCanChooseDirectories(true)
			if !d.RunModal() || len(d.Paths()) == 0 {
				return
			}
			if w, err := app.OpenVault(n.ui, d.Paths()[0]); err != nil {
				n.message = "Could not open the folder: " + err.Error()
				n.update()
			} else {
				w.Win.ToFront()
			}
		}},
		{Separator: true},
		{Text: "Save", Key: unison.KeyBinding{KeyCode: unison.KeyS, Modifiers: mod.OSMenuCommand()}, OnSelect: n.save},
		{Text: "Save As…", OnSelect: n.saveAs},
		{Separator: true},
		{Text: "Settings…", OnSelect: func() { app.OpenSettings(n.ui, n.win, nil) }},
		{Text: "Keyboard shortcuts…", OnSelect: func() { app.OpenShortcuts(n.ui, n.win) }},
	}
}

// openFileWindow opens a Markdown file on its own in a new window.
func openFileWindow(ui *kvitui.UI, path string) {
	doc, page, err := loadFile(path)
	if err != nil {
		return
	}
	n, err := newNoteWindow(ui, doc, path)
	if err != nil {
		return
	}
	n.page = page
	n.win.ToFront()
	n.ed.FocusBlock(0, 0)
}

// loadFile reads a note file, its front matter apart from its body; a
// file that does not exist yet is an empty note.
func loadFile(path string) (*editor.Doc, *vault.Page, error) {
	data, err := os.ReadFile(path)
	if os.IsNotExist(err) {
		return editor.NewDoc(nil), nil, nil
	}
	if err != nil {
		return nil, nil, err
	}
	p := vault.ParseText(string(data))
	return editor.NewDoc(editor.ParseMarkdown(p.Body)), p, nil
}

// loadDoc reads a note, or returns the sample note for an empty path.
func loadDoc(path string) (*editor.Doc, error) {
	if path == "" {
		return editor.NewDoc(editor.ParseMarkdown(app.WelcomeNote)), nil
	}
	data, err := os.ReadFile(path)
	if os.IsNotExist(err) {
		return editor.NewDoc(nil), nil
	}
	if err != nil {
		return nil, err
	}
	return editor.NewDoc(editor.ParseMarkdown(string(data))), nil
}
