// Package app is the Kvit Notes window: the sidebar, the note list and the
// editor pane, over one vault. cmd/kvit-notes opens it.
package app

// The note list (Kvit's NoteListPane): the notes in the current scope,
// each a row with its title, a snippet of its text, and its date and word
// count. One panel draws the rows in view, since a vault can hold thousands of
// notes.

import (
	"fmt"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/role"
)

// ListItem is one row of the note list.
type ListItem struct {
	Key      string // what the row stands for, such as the note's path
	Title    string
	Snippet  string // "" shows "Empty note"
	Details  string // the date and the word count
	Pinned   bool
	Favorite bool
}

// The row's geometry in design pixels (NoteListPane), and how far a
// press moves before it drags a note.
const (
	dragStart    = 5
	rowPadSide   = 12
	rowPadTop    = 7
	rowGap       = 2
	rowMarkGap   = 4
	rowRingInset = 1
	rowRing      = 2
	rowRadius    = 3
)

// NoteList is the list of notes.
type NoteList struct {
	unison.Panel
	ui *kvitui.UI
	// Items are the rows, in order.
	Items []ListItem
	// Current is the index of the row the keyboard is on and whose note is
	// open, or -1.
	Current int
	// OnOpen runs when a row is chosen by a press, the arrows, or a screen
	// reader.
	OnOpen func(i int)
	// OnDrag runs while a row is dragged, and OnDrop when it is let go,
	// with the pointer in the window's root coordinates.
	OnDrag, OnDrop func(i int, where geom.Point)
	// Selected are the rows picked with Ctrl or Shift for acting on
	// together; OnSelect runs when they change.
	Selected map[int]bool
	OnSelect func()
	// DropLine is the gap a dragged row would go into, drawn as a line:
	// before row DropLine, or -1 for none.
	DropLine int
	hover    int
	pressed  int        // the row a press started on, or -1
	pressAt  geom.Point // where it started
	dragging bool
}

// NewNoteList returns an empty note list.
func NewNoteList(ui *kvitui.UI) *NoteList {
	l := &NoteList{ui: ui, Current: -1, hover: -1, pressed: -1, Selected: map[int]bool{}, DropLine: -1}
	l.Self = l
	l.SetFocusable(true)
	l.SetSizer(l.sizes)
	l.DrawCallback = l.draw
	l.MouseDownCallback = func(where geom.Point, button, _ int, mods mod.Modifiers) bool {
		i := l.rowAt(where.Y)
		if i < 0 || button != unison.ButtonLeft {
			return true
		}
		l.RequestFocus()
		switch {
		case mods.OSMenuCommandDown():
			// Ctrl picks a row, or leaves it, keeping the others; the open
			// note is part of the pick.
			if len(l.Selected) == 0 && l.Current >= 0 {
				l.Selected[l.Current] = true
			}
			if l.Selected[i] {
				delete(l.Selected, i)
			} else {
				l.Selected[i] = true
			}
			l.selectionChanged()
			return true
		case mods.ShiftDown() && l.Current >= 0:
			clear(l.Selected)
			for k := min(i, l.Current); k <= max(i, l.Current); k++ {
				l.Selected[k] = true
			}
			l.selectionChanged()
			return true
		}
		if len(l.Selected) > 0 {
			clear(l.Selected)
			l.selectionChanged()
		}
		l.choose(i)
		l.pressed, l.pressAt, l.dragging = i, where, false
		return true
	}
	l.MouseDragCallback = func(where geom.Point, _ int, _ mod.Modifiers) bool {
		if l.pressed < 0 {
			return false
		}
		d := where.Sub(l.pressAt)
		if !l.dragging && d.X*d.X+d.Y*d.Y > l.px(dragStart)*l.px(dragStart) {
			l.dragging = true
		}
		if l.dragging && l.OnDrag != nil {
			l.OnDrag(l.pressed, l.PointToRoot(where))
		}
		return true
	}
	l.MouseUpCallback = func(where geom.Point, _ int, _ mod.Modifiers) bool {
		if l.dragging && l.OnDrop != nil {
			l.OnDrop(l.pressed, l.PointToRoot(where))
		}
		l.pressed, l.dragging = -1, false
		return true
	}
	l.MouseMoveCallback = func(where geom.Point, _ mod.Modifiers) bool { l.setHover(l.rowAt(where.Y)); return true }
	l.MouseEnterCallback = l.MouseMoveCallback
	l.MouseExitCallback = func() bool { l.setHover(-1); return true }
	l.KeyDownCallback = l.keyDown
	l.GainedFocusCallback = l.MarkForRedraw
	l.LostFocusCallback = l.MarkForRedraw
	l.Accessibility.Role = role.List
	l.Accessibility.Name = "Notes"
	return l
}

// selectionChanged redraws the picked rows and says they changed.
func (l *NoteList) selectionChanged() {
	l.MarkForRedraw()
	if l.OnSelect != nil {
		l.OnSelect()
	}
}

// SelectedRows are the picked rows, in order.
func (l *NoteList) SelectedRows() []int {
	var out []int
	for i := range l.Items {
		if l.Selected[i] {
			out = append(out, i)
		}
	}
	return out
}

// ClearSelection drops every picked row.
func (l *NoteList) ClearSelection() {
	if len(l.Selected) > 0 {
		clear(l.Selected)
		l.selectionChanged()
	}
}

// SetItems replaces the rows, keeping the current row on the same key when
// it is still there, and the picked rows on theirs.
func (l *NoteList) SetItems(items []ListItem, current string) {
	picked := map[string]bool{}
	for i := range l.Selected {
		if i < len(l.Items) {
			picked[l.Items[i].Key] = true
		}
	}
	clear(l.Selected)
	for i, it := range items {
		if picked[it.Key] {
			l.Selected[i] = true
		}
	}
	l.Items = items
	l.Current = -1
	for i, it := range items {
		if it.Key == current {
			l.Current = i
		}
	}
	for p := l.AsPanel(); p != nil; p = p.Parent() {
		p.NeedsLayout = true
	}
	l.MarkForRedraw()
	l.reveal()
}

func (l *NoteList) px(design float32) float32 {
	return design * float32(l.ui.Interface.Px(100)) / 100
}

// styles are the three lines' styles: the bold title, the snippet and the
// details.
func (l *NoteList) styles() (title, snippet, details text.Style) {
	t := l.ui.Theme.Tokens()
	title = l.ui.Chrome(l.ui.Size(kvitui.RoleBody), text.Bold, t.TextPrimary)
	snippet = l.ui.Chrome(l.ui.Size(kvitui.RoleSmall), text.Regular, t.TextFaint)
	details = l.ui.Chrome(l.ui.Size(kvitui.RoleCaption), text.Regular, t.TextFaint)
	return title, snippet, details
}

// rowHeight is every row's height: three lines and their padding.
func (l *NoteList) rowHeight() float32 {
	a, b, c := l.styles()
	h := func(st text.Style) float32 {
		_, lh := l.ui.Fonts.Layout([]text.Span{{Text: "Hg", Style: st}}, text.Options{}).Size()
		return lh
	}
	return 2*l.px(rowPadTop) + h(a) + h(b) + h(c) + 2*l.px(rowGap)
}

func (l *NoteList) sizes(hint geom.Size) (minSize, prefSize, maxSize geom.Size) {
	h := float32(len(l.Items)) * l.rowHeight()
	return geom.NewSize(l.px(120), h), geom.NewSize(max(hint.Width, l.px(200)), h), geom.NewSize(unison.DefaultMaxSize, h)
}

func (l *NoteList) rowAt(y float32) int {
	if y < 0 {
		return -1
	}
	i := int(y / l.rowHeight())
	if i >= len(l.Items) {
		return -1
	}
	return i
}

func (l *NoteList) setHover(i int) {
	if i != l.hover {
		l.hover = i
		l.MarkForRedraw()
	}
}

func (l *NoteList) choose(i int) {
	l.Current = i
	l.MarkForRedraw()
	l.reveal()
	if l.OnOpen != nil {
		l.OnOpen(i)
	}
}

// reveal scrolls the current row into view.
func (l *NoteList) reveal() {
	if l.Current < 0 {
		return
	}
	h := l.rowHeight()
	l.ScrollRectIntoView(geom.NewRect(0, float32(l.Current)*h, 1, h))
}

func (l *NoteList) keyDown(key unison.KeyCode, _ mod.Modifiers, _ bool) bool {
	n := len(l.Items)
	if n == 0 {
		return false
	}
	switch key {
	case unison.KeyUp:
		l.choose(max(0, l.Current-1))
	case unison.KeyDown:
		l.choose(min(n-1, l.Current+1))
	case unison.KeyHome:
		l.choose(0)
	case unison.KeyEnd:
		l.choose(n - 1)
	default:
		return false
	}
	return true
}

func (l *NoteList) draw(gc *unison.Canvas, dirty geom.Rect) {
	t := l.ui.Theme.Tokens()
	w := l.ContentRect(false).Width
	h := l.rowHeight()
	ts, ss, ds := l.styles()
	first := max(0, int(dirty.Y/h))
	last := min(len(l.Items)-1, int(dirty.Bottom()/h))
	fill := func(r geom.Rect, c unison.Color) { gc.DrawRect(r, c.Paint(gc, r, paintstyle.Fill)) }
	for i := first; i <= last; i++ {
		it := l.Items[i]
		row := geom.NewRect(0, float32(i)*h, w, h)
		switch {
		case l.Selected[i]:
			fill(row, kvitui.Color(t.SelectionActiveTint))
		case i == l.Current && l.Focused():
			fill(row, kvitui.Color(t.SelectionActiveTint))
		case i == l.Current:
			fill(row, kvitui.Color(t.SelectionTint))
		case i == l.hover:
			fill(row, kvitui.Color(t.HoverTint))
		}
		if l.DropLine == i || (l.DropLine == len(l.Items) && i == len(l.Items)-1) {
			y := row.Y
			if l.DropLine == len(l.Items) {
				y = row.Bottom() - l.px(2)
			}
			fill(geom.NewRect(0, y, w, l.px(2)), kvitui.Color(t.Accent))
		}
		fill(geom.NewRect(row.X, row.Bottom()-l.px(1), w, l.px(1)), kvitui.Color(t.Border))
		x, y := l.px(rowPadSide), row.Y+l.px(rowPadTop)
		width := w - 2*l.px(rowPadSide)
		mark := func(s string, c text.Color) {
			st := ss
			st.Color = c
			m := l.ui.Fonts.Layout([]text.Span{{Text: s, Style: st}}, text.Options{})
			mw, _ := m.Size()
			m.Draw(gc, x, y)
			x += mw + l.px(rowMarkGap)
		}
		if it.Pinned {
			mark("⚑", kvitui.TextColor(t.Accent))
		}
		if it.Favorite {
			mark("★", kvitui.TextColor(t.PinColor))
		}
		line := func(s string, st text.Style, from float32) float32 {
			tl := l.ui.Fonts.Layout([]text.Span{{Text: s, Style: st}}, text.Options{MaxWidth: max(1, width-(from-l.px(rowPadSide))), Elide: true})
			tl.Draw(gc, from, y)
			_, lh := tl.Size()
			return lh
		}
		y += line(it.Title, ts, x) + l.px(rowGap)
		snippet := it.Snippet
		if snippet == "" {
			snippet = "Empty note"
		}
		y += line(snippet, ss, l.px(rowPadSide)) + l.px(rowGap)
		line(it.Details, ds, l.px(rowPadSide))
		if i == l.Current && l.Focused() && len(l.Selected) == 0 {
			ring := row.Inset(geom.NewUniformInsets(l.px(rowRingInset)))
			p := kvitui.Color(t.FocusRing).Paint(gc, ring, paintstyle.Stroke)
			p.SetStrokeWidth(l.px(rowRing))
			r := l.px(rowRadius)
			gc.DrawRoundedRect(ring.Inset(geom.NewUniformInsets(l.px(rowRing)/2)), geom.NewSize(r, r), p)
		}
	}
}

// ProvideAccessibility describes the rows in view, and the current row,
// which holds the keyboard focus the list has.
func (l *NoteList) ProvideAccessibility(b *unison.AccessibilityBuilder) {
	h := l.rowHeight()
	vis := b.VisibleRect()
	var focus accessibility.NodeID
	for i, it := range l.Items {
		top := float32(i) * h
		if (top+h < vis.Y || top > vis.Bottom()) && i != l.Current {
			continue
		}
		name := it.Title
		if it.Pinned {
			name += ", pinned"
		}
		if it.Favorite {
			name += ", favourite"
		}
		id := b.AddVirtualChild(i, func(n *accessibility.Node) {
			n.Role = role.ListItem
			n.Name = name
			n.Description = fmt.Sprintf("%s. %s", it.Snippet, it.Details)
			n.Selected = i == l.Current
			n.Bounds = geom.NewRect(0, top, l.ContentRect(false).Width, h)
			n.Actions = n.Actions.With(accessibility.Select)
		})
		if i == l.Current {
			focus = id
		}
	}
	if focus != 0 {
		b.FocusChild(focus)
	}
}

// PerformAccessibilityAction opens the note a screen reader selects.
func (l *NoteList) PerformAccessibilityAction(req accessibility.ActionRequest) bool {
	i, ok := req.Key.(int)
	if !ok || i < 0 || i >= len(l.Items) || req.Action != accessibility.Select {
		return false
	}
	l.choose(i)
	return true
}

// gapAt is the gap between rows nearest to a point in the list: 0 before
// the first row, len(Items) after the last.
func (l *NoteList) gapAt(y float32) int {
	h := l.rowHeight()
	return max(0, min(len(l.Items), int((y+h/2)/h)))
}
