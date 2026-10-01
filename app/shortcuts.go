package app

// The keyboard shortcuts list: every shortcut by section, and the actions
// that have none, with where to find them instead.

import (
	"runtime"
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
)

// shortcut is one line of the list: an action and its keys, "" for an
// action reached from a menu.
type shortcut struct{ section, action, keys, instead string }

var shortcuts = []shortcut{
	{"Text Formatting", "Bold", "Ctrl+B", ""},
	{"Text Formatting", "Italic", "Ctrl+I", ""},
	{"Text Formatting", "Underline", "Ctrl+U", ""},
	{"Text Formatting", "Strikethrough", "Ctrl+Shift+S", ""},
	{"Text Formatting", "Inline code", "Ctrl+E", ""},
	{"Text Formatting", "Link", "Ctrl+K", ""},
	{"Text Formatting", "Superscript", "", "The toolbar's x² or the text menu"},
	{"Text Formatting", "Subscript", "", "The toolbar's x₂ or the text menu"},
	{"Block Operations", "Move block up", "Alt+Up", ""},
	{"Block Operations", "Move block down", "Alt+Down", ""},
	{"Block Operations", "Duplicate block", "Ctrl+D", ""},
	{"Block Operations", "Delete block", "Ctrl+Shift+D", ""},
	{"Block Operations", "Indent", "Tab", ""},
	{"Block Operations", "Outdent", "Shift+Tab", ""},
	{"Block Operations", "Line break in block", "Shift+Enter", ""},
	{"Block Operations", "Leave a code block or table", "Ctrl+Enter", ""},
	{"Block Operations", "Tick or untick a to-do", "Ctrl+Enter", ""},
	{"Block Operations", "Block menu", "Shift+F10", ""},
	{"Block Conversion", "Paragraph", "Ctrl+0", ""},
	{"Block Conversion", "Heading 1", "Ctrl+1", ""},
	{"Block Conversion", "Heading 2", "Ctrl+2", ""},
	{"Block Conversion", "Heading 3", "Ctrl+3", ""},
	{"Block Conversion", "Heading 4", "Ctrl+4", ""},
	{"Block Conversion", "To-do", "Ctrl+T", ""},
	{"Block Conversion", "Quote", "Ctrl+Shift+T", ""},
	{"General", "Save", "Ctrl+S", ""},
	{"General", "Save As", "", "File, Save As"},
	{"General", "Undo", "Ctrl+Z", ""},
	{"General", "Redo", "Ctrl+Y", ""},
	{"General", "Find", "Ctrl+F", ""},
	{"General", "Find and replace", "Ctrl+H", ""},
	{"General", "Select all", "Ctrl+A", ""},
	{"General", "New note", "Ctrl+N", ""},
	{"General", "Hide or show the sidebar and note list", "Ctrl+\\", ""},
	{"General", "Focus mode", "F11", ""},
	{"General", "Quick switcher", "Ctrl+P", ""},
	{"General", "Back", "Alt+Left", ""},
	{"General", "Forward", "Alt+Right", ""},
	{"General", "Backlinks", "Ctrl+Shift+B", ""},
}

// keysForPlatform writes a shortcut as this system names its keys: Cmd and
// Option on macOS.
func keysForPlatform(keys string) string {
	if runtime.GOOS != "darwin" {
		return keys
	}
	return strings.NewReplacer("Ctrl+", "Cmd+", "Alt+", "Option+").Replace(keys)
}

// openShortcuts shows the list.
func (w *Window) openShortcuts() { OpenShortcuts(w.ui, w.Win) }

// OpenShortcuts shows the keyboard shortcuts over a window.
func OpenShortcuts(ui *kvitui.UI, win *kvitui.Window) {
	list := kvitui.Column(ui, kvitui.SizeSpaceSnug)
	section := ""
	for _, s := range shortcuts {
		if s.section != section {
			section = s.section
			h := kvitui.NewLabel(ui, section)
			h.Role = kvitui.RoleStrong
			list.AddChild(h)
		}
		action := kvitui.NewLabel(ui, s.action)
		keys := keysForPlatform(s.keys)
		if keys == "" {
			keys = "— " + s.instead
		}
		k := kvitui.NewLabel(ui, keys)
		k.Ink = kvitui.InkTextSecondary
		action.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
		row := kvitui.Row(ui, kvitui.SizeSpace, action, k)
		row.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
		list.AddChild(row)
	}
	region := kvitui.NewRegion(ui, list)
	d := kvitui.NewDialog(ui, "Keyboard shortcuts", kvitui.Height(ui, kvitui.Px(420), region))
	d.ConfirmText, d.CancelText = "", "Close"
	d.Open(win)
}
