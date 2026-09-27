package app

// The strip across the top of a window (Kvit's Toolbar.qml, in part): the
// kind of block the caret is in, and the inline formats a button toggles.

import (
	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// ToolbarHeight is the strip's height in design pixels, as Kvit's toolbar.
const ToolbarHeight = 36

// Toolbar is the formatting strip for an editor.
type Toolbar struct {
	*unison.Panel
	ed   *editor.Editor
	kind *kvitui.Label
}

// NewToolbar returns the strip for an editor.
func NewToolbar(ui *kvitui.UI, ed *editor.Editor) *Toolbar {
	t := &Toolbar{ed: ed}
	t.kind = kvitui.NewLabel(ui, "Block type")
	t.kind.Role = kvitui.RoleBody
	parts := []unison.Paneler{kvitui.Width(ui, kvitui.Px(130), t.kind)}
	for _, f := range []struct{ label, name, marker string }{
		{"B", "Bold (Ctrl+B)", "**"}, {"I", "Italic (Ctrl+I)", "*"}, {"U", "Underline (Ctrl+U)", "++"},
		{"S", "Strikethrough (Ctrl+Shift+S)", "~~"}, {"<>", "Inline code (Ctrl+E)", "`"}, {"H", "Highlight", "=="},
	} {
		b := kvitui.NewButton(ui, f.label)
		b.Form = kvitui.ButtonQuiet
		b.Explanation = f.name
		b.OnClick = func() {
			ed.Doc.ToggleFormat(f.marker)
			ed.RequestFocus()
			ed.Refresh()
		}
		parts = append(parts, b)
	}
	row := kvitui.Row(ui, kvitui.SizeSpaceSnug, parts...)
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

// Update shows the kind of block the caret is in.
func (t *Toolbar) Update() {
	d := t.ed.Doc
	kind := "Block type"
	if b := d.CaretBlock(); b != nil && d.Focused {
		kind = b.Kind.String()
	}
	if kind != t.kind.Text {
		t.kind.Text = kind
		t.kind.MarkForRedraw()
	}
}
