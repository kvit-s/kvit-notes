package app

// The toolbar's File and View menus (Kvit's FileMenu.qml and ViewMenu.qml):
// opening vaults and files, saving, templates, import, export and settings;
// and which panes and modes are on, the theme, and reduced motion.

import (
	"os"
	"path/filepath"
	"strings"

	"github.com/kvit-s/kvit-notes/ignore"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// OpenFile opens a Markdown file on its own in a window of its own; the
// command sets it, since that window is the command's.
var OpenFile func(ui *kvitui.UI, path string)

// fileMenu is the File menu.
func (w *Window) fileMenu() []kvitui.MenuItem {
	if !w.Vault.ReadOnly {
		w.Vault.SeedTemplates()
	}
	var recent []kvitui.MenuItem
	for _, root := range w.prefs.strings("session.recentVaults") {
		recent = append(recent, kvitui.MenuItem{Text: kvitui.PlainMenuText(root), OnSelect: func() { w.switchVault(root, false) }})
	}
	var templates []kvitui.MenuItem
	for _, name := range w.Vault.TemplateNames() {
		templates = append(templates, kvitui.MenuItem{Text: kvitui.PlainMenuText(name), OnSelect: func() { w.newFromTemplate(name) }})
	}
	noVaultWrites := w.Vault.ReadOnly
	return []kvitui.MenuItem{
		{Text: "&Open File…", Disabled: OpenFile == nil, OnSelect: w.openFileDialog},
		{Text: "Open &Folder…", OnSelect: func() { w.openFolderDialog(false) }},
		{Text: "Open Folder in New &Window…", OnSelect: func() { w.openFolderDialog(true) }},
		{Separator: true},
		{Text: "&Save", Key: unison.KeyBinding{KeyCode: unison.KeyS, Modifiers: mod.OSMenuCommand()},
			Disabled: w.open == nil, OnSelect: w.saveNow},
		{Text: "Save &As…", Disabled: w.open == nil, OnSelect: w.saveAs},
		{Separator: true},
		{Text: "Open &Recent", Disabled: len(recent) == 0, Items: recent},
		{Separator: true},
		{Text: "&New from template", Disabled: noVaultWrites || len(templates) == 0, Items: templates},
		{Text: "&Manage templates…", Disabled: noVaultWrites, OnSelect: w.openTemplates},
		{Text: "&Quick capture note… (Ctrl+Alt+N)", Disabled: noVaultWrites, OnSelect: w.openCapture},
		{Separator: true},
		{Text: "&Import…", Disabled: noVaultWrites, OnSelect: w.openImport},
		{Text: "&Export…", OnSelect: w.openExport},
		{Separator: true},
		{Text: "Se&ttings…", OnSelect: w.openSettings},
		{Text: "&Keyboard shortcuts…", OnSelect: w.openShortcuts},
	}
}

// viewMenu is the View menu.
func (w *Window) viewMenu() []kvitui.MenuItem {
	var themes []kvitui.MenuItem
	for _, id := range tokens.AvailableThemes() {
		themes = append(themes, kvitui.MenuItem{Text: tokens.DisplayName(id), Checked: w.ui.Theme.ThemeID() == id,
			OnSelect: func() { w.ui.Theme.SetThemeID(id) }})
	}
	sides := !w.hidden && !w.focus
	return []kvitui.MenuItem{
		{Text: "&Sidebar", Checked: sides && w.paneOn("panels.sidebarCollapsed"),
			OnSelect: func() { w.showPane("panels.sidebarCollapsed", !(sides && w.paneOn("panels.sidebarCollapsed"))) }},
		{Text: "&Note list", Checked: sides && w.paneOn("panels.noteListCollapsed"),
			OnSelect: func() { w.showPane("panels.noteListCollapsed", !(sides && w.paneOn("panels.noteListCollapsed"))) }},
		{Text: "&Outline", Checked: w.paneOn("view.outline"),
			OnSelect: func() { w.showPane("view.outline", !w.paneOn("view.outline")) }},
		{Text: "&Backlinks", Key: unison.KeyBinding{KeyCode: unison.KeyB, Modifiers: mod.OSMenuCommand() | mod.Shift},
			Checked:  w.paneOn("view.backlinks"),
			OnSelect: func() { w.showPane("view.backlinks", !w.paneOn("view.backlinks")) }},
		{Separator: true},
		{Text: "Foc&us mode", Key: unison.KeyBinding{KeyCode: unison.KeyF11}, Checked: w.focus, OnSelect: w.toggleFocus},
		{Text: "&Typewriter mode", Checked: w.Editor.Typewriter != nil, OnSelect: w.toggleTypewriter},
		{Separator: true},
		{Text: "Status ba&r", Checked: w.prefs.bool("view.statusBar", true), OnSelect: func() {
			w.prefs.set("view.statusBar", !w.prefs.bool("view.statusBar", true))
			w.arrange()
		}},
		{Text: "&Code line numbers", Checked: w.Editor.LineNumbers, OnSelect: func() {
			w.Editor.LineNumbers = !w.Editor.LineNumbers
			w.prefs.set("view.codeLineNumbers", w.Editor.LineNumbers)
			w.Editor.Refresh()
		}},
		{Text: "&Equation numbers", Checked: w.Editor.EquationNumbers, OnSelect: func() {
			w.Editor.EquationNumbers = !w.Editor.EquationNumbers
			w.prefs.set("view.equationNumbers", w.Editor.EquationNumbers)
			w.Editor.Refresh()
		}},
		{Separator: true},
		{Text: "T&heme", Items: themes},
		{Text: "Reduced &motion", Checked: w.ui.Theme.MotionScale() == 0, OnSelect: func() {
			if w.ui.Theme.MotionScale() == 0 {
				w.ui.Theme.SetReducedMotionSetting("off")
			} else {
				w.ui.Theme.SetReducedMotionSetting("on")
			}
		}},
		{Separator: true},
		{Text: "Focus e&ditor", OnSelect: func() { w.Editor.RequestFocus() }},
	}
}

// toggleFocus turns focus mode on or off (F11): the editor alone, its text
// in a column in the middle, the window as large as it goes; Escape leaves.
func (w *Window) toggleFocus() {
	w.focus = !w.focus
	w.prefs.set("view.focusMode", w.focus)
	if uw := w.Win.Window; uw != nil && uw.IsValid() {
		switch {
		case w.focus && !uw.IsMaximized():
			uw.Maximize()
			w.maximized = true
		case !w.focus && w.maximized:
			uw.Maximize()
			w.maximized = false
		}
	}
	w.arrange()
	w.Editor.RequestFocus()
}

// toggleTypewriter turns typewriter mode on or off: the caret's line kept
// in the middle of the view, the other blocks faded.
func (w *Window) toggleTypewriter() {
	if w.Editor.Typewriter == nil {
		w.Editor.Typewriter = w.region
	} else {
		w.Editor.Typewriter = nil
	}
	w.prefs.set("view.typewriterMode", w.Editor.Typewriter != nil)
	w.Editor.Refresh()
	w.Editor.RequestFocus()
}

// FocusMode reports whether focus mode is on.
func (w *Window) FocusMode() bool { return w.focus }

// openFileDialog asks for a Markdown file and opens it on its own.
func (w *Window) openFileDialog() {
	d := unison.NewOpenDialog()
	d.SetAllowedExtensions("md", "markdown", "txt")
	if !d.RunModal() || len(d.Paths()) == 0 {
		return
	}
	OpenFile(w.ui, d.Paths()[0])
}

// openFolderDialog asks for a folder and opens it as a vault, in this
// window or a new one.
func (w *Window) openFolderDialog(newWindow bool) {
	d := unison.NewOpenDialog()
	d.SetCanChooseFiles(false)
	d.SetCanChooseDirectories(true)
	if !d.RunModal() || len(d.Paths()) == 0 {
		return
	}
	w.switchVault(d.Paths()[0], newWindow)
}

// switchVault opens a vault in a window: a new one, or this one's place,
// closing this. A vault already open in a window brings that window
// forward instead.
func (w *Window) switchVault(root string, newWindow bool) {
	if other := windowFor(root); other != nil {
		other.Win.ToFront()
		return
	}
	nw, err := OpenVault(w.ui, root)
	if err != nil {
		w.fail("Could not open "+root, err)
		return
	}
	if !newWindow {
		nw.Win.SetFrameRect(w.Win.FrameRect())
		w.Win.AttemptClose()
	}
	nw.Win.ToFront()
}

// saveAs writes the open note to a file chosen, and opens that file: in
// this vault when it is inside it, on its own otherwise.
func (w *Window) saveAs() {
	if w.open == nil {
		return
	}
	w.saveNow()
	d := unison.NewSaveDialog()
	d.SetAllowedExtensions("md")
	d.SetInitialFileName(w.open.Title + ".md")
	if !d.RunModal() || d.Path() == "" {
		return
	}
	target := d.Path()
	if filepath.Ext(target) == "" {
		target += ".md"
	}
	if err := os.WriteFile(target, []byte(w.page.Text()), 0o644); err != nil {
		w.fail("Could not save the copy", err)
		return
	}
	if rel, err := filepath.Rel(w.Vault.Root, target); err == nil && !strings.HasPrefix(rel, "..") {
		_ = w.Vault.Rescan()
		w.refreshScopes()
		w.refreshList()
		if e := w.Vault.Find(filepath.ToSlash(rel)); e != nil {
			w.openNote(e)
		}
		return
	}
	if OpenFile != nil {
		OpenFile(w.ui, target)
	}
}

// windows are the vault windows open, in the order they opened.
var windows []*Window

// windowFor is the window a vault is open in, or nil.
func windowFor(root string) *Window {
	abs, _ := filepath.Abs(root)
	for _, w := range windows {
		if other, _ := filepath.Abs(w.Vault.Root); other == abs {
			return w
		}
	}
	return nil
}

// OpenVault opens a vault in a new window. An empty vault gets a first note
// to read, as the Qt app's does.
func OpenVault(ui *kvitui.UI, root string) (*Window, error) {
	v, err := vault.Open(root)
	if err != nil {
		return nil, err
	}
	// The patterns set for this vault in the settings, beside .gitignore.
	if value, ok := newPrefs(ui).value(ignore.SettingsKey); ok {
		if patterns := ignore.LoadAdditionalPatterns(value, v.Root); len(patterns) > 0 {
			v.IgnorePatterns = patterns
			_ = v.Rescan()
		}
	}
	if len(v.Entries) == 0 && !v.ReadOnly {
		if err := os.WriteFile(filepath.Join(v.Root, "Welcome.md"), []byte(WelcomeNote), 0o644); err == nil {
			_ = v.Rescan()
		}
	}
	w, err := Open(ui, v)
	if err != nil {
		v.Close()
		return nil, err
	}
	return w, nil
}

// WelcomeNote is the note an empty vault starts with.
const WelcomeNote = `# Welcome to Kvit Notes

The quick **brown** fox has *seven* cubs. Markers such as the stars show only around the caret.

## Things to try

- Type ` + "`# `" + ` at the start of an empty paragraph to make a heading
- Type ` + "`/`" + ` in an empty block for the block menu
- [ ] Drag a block by the handle that appears on hover
- [x] Undo anything with Ctrl+Z

> A quotation keeps its lines
> together.

` + "```" + `
func main() {
    fmt.Println("code keeps its indentation")
}
` + "```" + `

---

Bold with Ctrl+B, italic with Ctrl+I, ==highlight==, ~~strike~~, ++underline++ and [links](https://kvit.app).
`

// OpenOrRaise brings forward the window a vault is open in, or opens it
// in a new window.
func OpenOrRaise(ui *kvitui.UI, root string) error {
	if w := windowFor(root); w != nil {
		w.Win.ToFront()
		return nil
	}
	w, err := OpenVault(ui, root)
	if err != nil {
		return err
	}
	w.Win.ToFront()
	return nil
}

// RaiseAny brings the first vault window forward, and reports whether
// there was one.
func RaiseAny() bool {
	if len(windows) == 0 {
		return false
	}
	windows[0].Win.ToFront()
	return true
}
