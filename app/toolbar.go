package app

// The strip across the top of a window (Kvit's Toolbar): the File and
// View menus, Back and Forward, the kind of block the caret is in, the
// inline formats and text colour, alignment, and Insert.

import (
	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
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
	ui    *kvitui.UI
	row   *unison.Panel
	prefs *prefs
	// groups are the strip's parts by customization group, in order.
	groups []toolbarGroup
}

// toolbarGroup is one show/hide group of the strip (Kvit's Toolbar
// showBlockGroup and friends, persisted per group).
type toolbarGroup struct {
	key   string // settings key
	label string // menu line
	parts []unison.Paneler
	on    bool
}

// toolbarGroupKeys are the groups in strip order, with their settings keys.
var toolbarGroupKeys = []struct{ key, label string }{
	{"toolbar.showView", "Menus and history"},
	{"toolbar.showBlockType", "Block type"},
	{"toolbar.showFormatting", "Formatting"},
	{"toolbar.showInsert", "Insert"},
}

// NewToolbar returns the strip for an editor.
func NewToolbar(ui *kvitui.UI, ed *editor.Editor, hooks ToolbarHooks) *Toolbar {
	t := &Toolbar{ed: ed, ui: ui, menus: map[unison.KeyCode]*kvitui.Button{}}
	var view, block, format, insert []unison.Paneler
	menuButton := func(label string, key unison.KeyCode, items func() []kvitui.MenuItem) {
		b := kvitui.NewButton(ui, label)
		b.Form = kvitui.ButtonFlat
		b.Explanation = "Alt+" + string(rune(key))
		b.OnClick = func() { ui.ShowMenu(b, label, items()) }
		t.menus[key] = b
		view = append(view, b)
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
		view = append(view, back, forward)
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
	block = append(block, kvitui.Width(ui, kvitui.Px(130), t.kind))

	for _, f := range []struct{ label, name, marker string }{
		{"B", "Bold (Ctrl+B)", "**"}, {"I", "Italic (Ctrl+I)", "*"}, {"U", "Underline (Ctrl+U)", "++"},
		{"S", "Strikethrough (Ctrl+Shift+S)", "~~"}, {"<>", "Inline code (Ctrl+E)", "`"}, {"H", "Highlight", "=="},
		{"x²", "Superscript", "^"}, {"x₂", "Subscript", "~"},
	} {
		b := kvitui.NewButton(ui, f.label)
		b.Form = kvitui.ButtonFlat
		b.Explanation = f.name
		b.OnClick = func() { ed.Format(f.marker) }
		format = append(format, b)
	}

	if hooks.Link != nil {
		link := kvitui.NewButton(ui, "Link")
		link.Form = kvitui.ButtonFlat
		link.Explanation = "Link (Ctrl+K)"
		link.OnClick = hooks.Link
		format = append(format, link)
	}

	colour := kvitui.NewButton(ui, "A")
	colour.Form = kvitui.ButtonFlat
	colour.Explanation = "Text color"
	colour.OnClick = func() { ui.ShowMenu(colour, "Text color", ed.ColorItems()) }
	format = append(format, colour)

	for _, a := range []struct{ value, symbol, name string }{
		{"left", "text-align-left", "Align left"}, {"center", "text-align-center", "Align center"},
		{"right", "text-align-right", "Align right"},
	} {
		b := kvitui.NewIconButton(ui, a.symbol, a.name)
		b.OnClick = func() { ed.Align(a.value) }
		t.align = append(t.align, b)
		format = append(format, b)
	}

	insertBtn := kvitui.NewButton(ui, "+ Insert")
	insertBtn.Form = kvitui.ButtonFlat
	insertBtn.Explanation = "Insert a block below the caret"
	insertBtn.OnClick = func() { ui.ShowMenu(insertBtn, "Insert block", ed.InsertItems()) }
	t.menus[unison.KeyI] = insertBtn
	insert = append(insert, insertBtn)

	t.groups = []toolbarGroup{
		{key: "toolbar.showView", label: "Menus and history", parts: view, on: true},
		{key: "toolbar.showBlockType", label: "Block type", parts: block, on: true},
		{key: "toolbar.showFormatting", label: "Formatting", parts: format, on: true},
		{key: "toolbar.showInsert", label: "Insert", parts: insert, on: true},
	}
	row := kvitui.Row(ui, kvitui.SizeSpaceSnug)
	t.row = row
	t.rebuild()
	row.SetBorder(kvitui.Insets(ui, kvitui.Px(4), kvitui.Px(10), kvitui.Px(4), kvitui.Px(10)))
	row.DrawCallback = func(gc *unison.Canvas, _ geom.Rect) {
		tk := ui.Theme.Tokens()
		r := row.ContentRect(true)
		gc.DrawRect(r, kvitui.Color(tk.FooterBackground).Paint(gc, r, paintstyle.Fill))
		line := geom.NewRect(r.X, r.Bottom()-float32(ui.Interface.Hairline()), r.Width, float32(ui.Interface.Hairline()))
		gc.DrawRect(line, kvitui.Color(tk.Border).Paint(gc, line, paintstyle.Fill))
	}
	row.MouseDownCallback = func(where geom.Point, button, _ int, _ mod.Modifiers) bool {
		if button == unison.ButtonRight {
			t.customize()
			return true
		}
		return false
	}

	t.Panel = kvitui.Height(ui, kvitui.Px(ToolbarHeight), row)
	return t
}

// FocusFirst puts the keyboard focus on the strip's first button (File,
// View or Insert), for F6 pane cycling. It reports whether there is one.
func (t *Toolbar) FocusFirst() bool {
	for _, key := range []unison.KeyCode{unison.KeyF, unison.KeyV, unison.KeyI} {
		if b, ok := t.menus[key]; ok && b.Window() != nil {
			b.RequestFocus()
			return true
		}
	}
	return false
}

// FocusedChild reports whether one of the strip's buttons holds the
// keyboard focus.
func (t *Toolbar) FocusedChild() bool {
	for _, b := range t.menus {
		if b.Focused() {
			return true
		}
	}
	return false
}

// SetPrefs gives the strip the settings its group visibility is kept in,
// and applies them.
func (t *Toolbar) SetPrefs(p *prefs) {
	t.prefs = p
	for k := range t.groups {
		t.groups[k].on = p.bool(t.groups[k].key, true)
	}
	t.rebuild()
}

// GroupOn reports whether a customization group is shown, by settings key.
func (t *Toolbar) GroupOn(key string) bool {
	for _, g := range t.groups {
		if g.key == key {
			return g.on
		}
	}
	return false
}

// SetGroup shows or hides a customization group, keeping the setting, and
// reports whether there is one.
func (t *Toolbar) SetGroup(key string, on bool) bool {
	for k := range t.groups {
		if t.groups[k].key == key {
			t.groups[k].on = on
			if t.prefs != nil {
				t.prefs.set(key, on)
			}
			t.rebuild()
			return true
		}
	}
	return false
}

// rebuild lays the visible groups out again, with a rule between them.
func (t *Toolbar) rebuild() {
	t.row.RemoveAllChildren()
	for _, g := range t.groups {
		if !g.on || len(g.parts) == 0 {
			continue
		}
		if len(t.row.Children()) > 0 {
			sep := separator(t.ui)
			sep.AsPanel().SetLayoutData(&unison.FlexLayoutData{VAlign: align.Middle})
			t.row.AddChild(sep)
		}
		for _, p := range g.parts {
			p.AsPanel().SetLayoutData(&unison.FlexLayoutData{VAlign: align.Middle})
			t.row.AddChild(p)
		}
	}
	t.row.MarkForLayoutRecursively()
	t.MarkForRedraw()
}

// customize shows the show/hide menu for the strip's groups.
func (t *Toolbar) customize() {
	var items []kvitui.MenuItem
	for _, g := range t.groups {
		if len(g.parts) == 0 {
			continue
		}
		g := g
		items = append(items, kvitui.MenuItem{Text: g.label, Checked: g.on, OnSelect: func() {
			t.SetGroup(g.key, !g.on)
		}})
	}
	t.ui.ShowMenuAt(t, geom.NewRect(0, 0, 0, 0), "Toolbar", items)
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
