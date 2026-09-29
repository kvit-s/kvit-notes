package editor

// The formatting bar (features.md 9.3, Kvit's FormattingBar): a strip
// of the inline formats above a text selection inside one block, for the
// pointer. It appears once the selection is made, not while it is being
// dragged out, and goes when the selection does. It never takes the
// keyboard: each button acts on the selection and hands the keyboard back
// to the note.

import (
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// formatBarGap is the space between the bar and the selection's line, in
// design pixels.
const formatBarGap = 6

// formatBarWanted reports whether the bar should show: a selection inside
// one block of inline text, made, in a note that can be changed.
func (e *Editor) formatBarWanted() bool {
	d := e.Doc
	b := d.CaretBlock()
	return e.FormatBar && b != nil && d.Focused && e.Focused() && d.HasSelection() && !d.CrossBlock() &&
		b.Kind.HasInline() && !d.ReadOnly && e.msel == nil && e.menu == nil && e.Window() != nil
}

// syncFormatBar shows or hides the bar after the selection changed.
func (e *Editor) syncFormatBar() {
	want := e.formatBarWanted()
	switch {
	case want && e.formatBar == nil:
		e.showFormatBar()
	case !want && e.formatBar != nil:
		e.formatBar()
		e.formatBar, e.formatBarRow = nil, nil
	case want:
		if w := e.ui.WindowOf(e); w != nil {
			w.Content().MarkForLayoutRecursively()
		}
	}
}

func (e *Editor) showFormatBar() {
	w := e.ui.WindowOf(e)
	if w == nil {
		return
	}
	var parts []unison.Paneler
	button := func(label, name string, act func()) {
		b := kvitui.NewButton(e.ui, label)
		b.Form = kvitui.ButtonFlat
		b.Explanation = name
		b.SetFocusable(false)
		b.OnClick = act
		parts = append(parts, b)
	}
	for _, f := range []struct{ label, name, marker string }{
		{"B", "Bold (Ctrl+B)", "**"}, {"I", "Italic (Ctrl+I)", "*"}, {"U", "Underline (Ctrl+U)", "++"},
		{"S", "Strikethrough (Ctrl+Shift+S)", "~~"}, {"<>", "Inline code (Ctrl+E)", "`"}, {"H", "Highlight", "=="},
		{"x²", "Superscript", "^"}, {"x₂", "Subscript", "~"},
	} {
		button(f.label, f.name, func() { e.Format(f.marker) })
	}
	if e.OnLink != nil {
		button("Link", "Link (Ctrl+K)", e.OnLink)
	}
	var colour *kvitui.Button
	button("A", "Text color", func() {
		// The menu belongs to the editor rather than to the bar, which goes
		// while the menu has the keyboard.
		at := e.RectFromRoot(colour.RectToRoot(colour.ContentRect(false)))
		e.ui.ShowMenuAt(e, at, "Text color", e.ColorItems())
	})
	colour = parts[len(parts)-1].(*kvitui.Button)
	row := kvitui.Row(e.ui, kvitui.Px(1), parts...)
	row.SetBorder(kvitui.Padding(e.ui, kvitui.Px(3)))
	row.DrawCallback = func(gc *unison.Canvas, _ geom.Rect) {
		t := e.tok()
		r := row.ContentRect(true)
		rad := e.px(6)
		gc.DrawRoundedRect(r, geom.NewSize(rad, rad), kvitui.Color(t.PopupBackground).Paint(gc, r, paintstyle.Fill))
		e.stroke(gc, r, rad, e.px(1), t.BorderStrong)
	}
	row.Accessibility.Name = "Formatting"
	e.formatBarRow = row
	e.formatBar = w.Show(&kvitui.Popup{Panel: row, Anchor: e, Place: e.placeFormatBar})
}

// placeFormatBar puts the bar above the start of the selection, or below
// its line when there is no room above.
func (e *Editor) placeFormatBar(bounds geom.Rect, size geom.Size) geom.Rect {
	d := e.Doc
	w := e.Window()
	from, _ := d.SelRange()
	i := d.Index(from.Block)
	if w == nil || i < 0 || i >= len(e.tops) {
		return geom.NewRect(bounds.X, bounds.Y, size.Width, size.Height)
	}
	l := e.layout(i)
	x, top := l.caretAt(l.drawn(from.Off))
	o := e.textOrigin(i)
	at := w.Content().RectFromRoot(e.RectToRoot(geom.NewRect(o.X+x, o.Y+top, 1, l.pitch)))
	bx := max(bounds.X, min(at.X-e.px(40), bounds.Right()-size.Width))
	by := at.Y - size.Height - e.px(formatBarGap)
	if by < bounds.Y {
		by = at.Bottom() + e.px(formatBarGap)
	}
	return geom.NewRect(bx, by, size.Width, size.Height)
}

// FormatBarButtons are the formatting bar's buttons while it is shown, for
// tests.
func (e *Editor) FormatBarButtons() []*unison.Panel {
	if e.formatBarRow == nil {
		return nil
	}
	return e.formatBarRow.Children()
}
