package app

// The strip across the top of a window (Kvit's Toolbar.qml): the File and
// View menus, Back and Forward, the kind of block the caret is in, the
// inline formats and text colour, alignment, and Insert.

import (
	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// ToolbarHeight is the strip's height in design pixels, as Kvit's toolbar.
const ToolbarHeight = 36

// ToolbarHooks are what the strip's window-level buttons do; a nil hook
// leaves its button out, as a window over a single file has no history.
type ToolbarHooks struct {
	// File and View build the menus of those names when they open.
	File, View func() []kvitui.MenuItem
	// Back and Forward move through the notes opened.
	Back, Forward func()
	// Link opens the link dialog, as Ctrl+K does.
	Link func()
}

// Toolbar is the strip for an editor.
type Toolbar struct {
	*unison.Panel
	ed    *editor.Editor
	kind  *kvitui.Select
	align []*kvitui.IconButton
	// menus are the buttons Alt and a letter open: File, View and Insert.
	menus map[unison.KeyCode]*kvitui.Button
}

// NewToolbar returns the strip for an editor.
func NewToolbar(ui *kvitui.UI, ed *editor.Editor, hooks ToolbarHooks) *Toolbar {
	t := &Toolbar{ed: ed, menus: map[unison.KeyCode]*kvitui.Button{}}
	var parts []unison.Paneler
	menuButton := func(label string, key unison.KeyCode, items func() []kvitui.MenuItem) {
		b := kvitui.NewButton(ui, label)
		b.Form = kvitui.ButtonFlat
		b.Explanation = "Alt+" + string(rune(key))
		b.OnClick = func() { ui.ShowMenu(b, label, items()) }
		t.menus[key] = b
		parts = append(parts, b)
	}
	if hooks.File != nil {
		menuButton("File", unison.KeyF, hooks.File)
	}
	if hooks.View != nil {
		menuButton("View", unison.KeyV, hooks.View)
	}
	if hooks.Back != nil {
		back := kvitui.NewIconButton(ui, "arrow-left", "Back (Alt+Left)")
		back.OnClick = hooks.Back
		forward := kvitui.NewIconButton(ui, "arrow-right", "Forward (Alt+Right)")
		forward.OnClick = hooks.Forward
		parts = append(parts, back, forward)
	}

	var options []kvitui.Option
	for _, c := range editor.ToolbarKinds {
		options = append(options, kvitui.Option{Value: c.Name, Label: c.Name})
	}
	t.kind = kvitui.NewSelect(ui, "Block type", options...)
	t.kind.Placeholder = "Block type"
	t.kind.Current = ""
	t.kind.OnChoose = func(name string) {
		for _, c := range editor.ToolbarKinds {
			if c.Name == name {
				ed.TurnInto(c.Kind)
			}
		}
	}
	parts = append(parts, kvitui.Width(ui, kvitui.Px(130), t.kind), separator(ui))

	for _, f := range []struct{ label, name, marker string }{
		{"B", "Bold (Ctrl+B)", "**"}, {"I", "Italic (Ctrl+I)", "*"}, {"U", "Underline (Ctrl+U)", "++"},
		{"S", "Strikethrough (Ctrl+Shift+S)", "~~"}, {"<>", "Inline code (Ctrl+E)", "`"}, {"H", "Highlight", "=="},
		{"x²", "Superscript", "^"}, {"x₂", "Subscript", "~"},
	} {
		b := kvitui.NewButton(ui, f.label)
		b.Form = kvitui.ButtonFlat
		b.Explanation = f.name
		b.OnClick = func() { ed.Format(f.marker) }
		parts = append(parts, b)
	}
	if hooks.Link != nil {
		link := kvitui.NewButton(ui, "Link")
		link.Form = kvitui.ButtonFlat
		link.Explanation = "Link (Ctrl+K)"
		link.OnClick = hooks.Link
		parts = append(parts, link)
	}
	colour := kvitui.NewButton(ui, "A")
	colour.Form = kvitui.ButtonFlat
	colour.Explanation = "Text color"
	colour.OnClick = func() { ui.ShowMenu(colour, "Text color", ed.ColorItems()) }
	parts = append(parts, colour, separator(ui))

	for _, a := range []struct{ value, symbol, name string }{
		{"left", "text-align-left", "Align left"}, {"center", "text-align-center", "Align center"},
		{"right", "text-align-right", "Align right"},
	} {
		b := kvitui.NewIconButton(ui, a.symbol, a.name)
		b.OnClick = func() { ed.Align(a.value) }
		t.align = append(t.align, b)
		parts = append(parts, b)
	}
	parts = append(parts, separator(ui))
	insert := kvitui.NewButton(ui, "+ Insert")
	insert.Form = kvitui.ButtonFlat
	insert.Explanation = "Insert a block below the caret"
	insert.OnClick = func() { ui.ShowMenu(insert, "Insert block", ed.InsertItems()) }
	t.menus[unison.KeyI] = insert
	parts = append(parts, insert)

	row := kvitui.Row(ui, kvitui.SizeSpaceSnug, parts...)
	for _, p := range parts {
		p.AsPanel().SetLayoutData(&unison.FlexLayoutData{VAlign: align.Middle})
	}
	row.SetBorder(kvitui.Insets(ui, kvitui.Px(4), kvitui.Px(10), kvitui.Px(4), kvitui.Px(10)))
	row.DrawCallback = func(gc *unison.Canvas, _ geom.Rect) {
		tk := ui.Theme.Tokens()
		r := row.ContentRect(true)
		gc.DrawRect(r, kvitui.Color(tk.FooterBackground).Paint(gc, r, paintstyle.Fill))
		line := geom.NewRect(r.X, r.Bottom()-float32(ui.Interface.Hairline()), r.Width, float32(ui.Interface.Hairline()))
		gc.DrawRect(line, kvitui.Color(tk.Border).Paint(gc, line, paintstyle.Fill))
	}
	t.Panel = kvitui.Height(ui, kvitui.Px(ToolbarHeight), row)
	return t
}

// OpenMenu opens the menu Alt and a key names, File, View or Insert, and
// reports whether there is one.
func (t *Toolbar) OpenMenu(key unison.KeyCode) bool {
	b, ok := t.menus[key]
	if ok && b.Window() != nil {
		b.OnClick()
	}
	return ok
}

// separator is the rule between the strip's groups.
func separator(ui *kvitui.UI) unison.Paneler {
	d := kvitui.NewDivider(ui)
	d.Vertical = true
	return kvitui.Height(ui, kvitui.Px(24), d)
}

// Update shows the kind of block the caret is in.
func (t *Toolbar) Update() {
	d := t.ed.Doc
	kind := ""
	if b := d.CaretBlock(); b != nil && d.Focused {
		for _, c := range editor.ToolbarKinds {
			if c.Kind == b.Kind {
				kind = c.Name
			}
		}
	}
	if kind != t.kind.Current {
		t.kind.Current = kind
		t.kind.MarkForRedraw()
	}
}

// BlockKind is what the block type list shows.
func (t *Toolbar) BlockKind() string {
	if t.kind.Current == "" {
		return t.kind.Placeholder
	}
	return t.kind.Current
}
