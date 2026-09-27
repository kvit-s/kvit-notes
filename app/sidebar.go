package app

// The sidebar's list (Kvit's Sidebar.qml): the scopes a note list can show,
// in one column. All Notes and Favorites at the top, the folder tree under a
// "Folders" heading, the tags under a "Tags" heading, and the trash at the
// foot. Each row has a count; a folder row opens and closes.

import (
	"strconv"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/role"
)

// ScopeKind is what a scope of the note list is.
type ScopeKind int

// The scopes.
const (
	ScopeAll ScopeKind = iota
	ScopeFavorites
	ScopeFolder
	ScopeTag
	ScopeTrash
)

// Scope is what the note list shows: every note, the favourites, one folder
// (Path is its path in the vault, "" the top), the notes with one tag, or
// the trash.
type Scope struct {
	Kind ScopeKind
	Path string
}

// ScopeRow is one row of the sidebar's list.
type ScopeRow struct {
	Scope      Scope
	Label      string
	Count      int
	Depth      int  // a folder's depth
	Expandable bool // a folder with folders in it
	Expanded   bool
	Heading    bool // a heading such as "Folders", which is not a scope
	Mark       palette.Color
	HasMark    bool // a folder's or tag's colour, drawn before the label
}

// The rows' geometry in design pixels (Sidebar.qml).
const (
	scopeRow      = 28
	scopeHeading  = 26
	scopePadLeft  = 12
	scopePadRight = 12
	folderStep    = 14
	chevronWidth  = 10
	markWidth     = 10
	markHeight    = 8
	markRadius    = 2
	markGap       = 4
)

// ScopeList is the sidebar's list of scopes.
type ScopeList struct {
	unison.Panel
	ui *kvitui.UI
	// Rows are the rows, top to bottom; the trash is drawn at the foot of
	// whatever height the list is given.
	Rows []ScopeRow
	// Current is the scope the note list shows.
	Current Scope
	// OnChoose runs when a scope is chosen.
	OnChoose func(Scope)
	// OnToggle runs when a folder is opened or closed.
	OnToggle func(folder string, expanded bool)
	hover    int
	focusRow int
	// Target is the row a dragged note would be dropped on, or -1.
	Target int
}

// NewScopeList returns an empty list.
func NewScopeList(ui *kvitui.UI) *ScopeList {
	l := &ScopeList{ui: ui, hover: -1, focusRow: -1, Target: -1}
	l.Self = l
	l.SetFocusable(true)
	l.SetSizer(l.sizes)
	l.DrawCallback = l.draw
	l.MouseDownCallback = l.mouseDown
	l.MouseMoveCallback = func(where geom.Point, _ mod.Modifiers) bool { l.setHover(l.rowAt(where)); return true }
	l.MouseEnterCallback = l.MouseMoveCallback
	l.MouseExitCallback = func() bool { l.setHover(-1); return true }
	l.KeyDownCallback = l.keyDown
	l.GainedFocusCallback = l.MarkForRedraw
	l.LostFocusCallback = l.MarkForRedraw
	l.Accessibility.Role = role.Tree
	l.Accessibility.Name = "Folders and tags"
	return l
}

func (l *ScopeList) px(design float32) float32 {
	return design * float32(l.ui.Interface.Px(100)) / 100
}

// SetRows replaces the rows.
func (l *ScopeList) SetRows(rows []ScopeRow) {
	l.Rows = rows
	for p := l.AsPanel(); p != nil; p = p.Parent() {
		p.NeedsLayout = true
	}
	l.MarkForRedraw()
}

// geometry is each row's top and height, the trash row last at the foot.
func (l *ScopeList) geometry() (tops, heights []float32) {
	y := float32(0)
	for i, r := range l.Rows {
		h := l.px(scopeRow)
		if r.Heading {
			h = l.px(scopeHeading)
		}
		if r.Scope.Kind == ScopeTrash && i == len(l.Rows)-1 {
			y = max(y, l.ContentRect(false).Height-h)
		}
		tops = append(tops, y)
		heights = append(heights, h)
		y += h
	}
	return tops, heights
}

func (l *ScopeList) sizes(hint geom.Size) (minSize, prefSize, maxSize geom.Size) {
	var h float32
	for _, r := range l.Rows {
		if r.Heading {
			h += l.px(scopeHeading)
		} else {
			h += l.px(scopeRow)
		}
	}
	return geom.NewSize(l.px(100), h), geom.NewSize(max(hint.Width, l.px(160)), h), geom.NewSize(unison.DefaultMaxSize, unison.DefaultMaxSize)
}

// RowAt is the row under a point in the list's own coordinates, or -1.
func (l *ScopeList) RowAt(where geom.Point) int { return l.rowAt(where) }

func (l *ScopeList) rowAt(where geom.Point) int {
	tops, heights := l.geometry()
	for i := range tops {
		if where.Y >= tops[i] && where.Y < tops[i]+heights[i] && !l.Rows[i].Heading {
			return i
		}
	}
	return -1
}

func (l *ScopeList) setHover(i int) {
	if i != l.hover {
		l.hover = i
		l.MarkForRedraw()
	}
}

// labelLeft is where row i's label starts.
func (l *ScopeList) labelLeft(r ScopeRow) float32 {
	x := l.px(scopePadLeft)
	if r.Scope.Kind == ScopeFolder {
		x = l.px(8) + float32(r.Depth)*l.px(folderStep) + l.px(chevronWidth+markGap)
	}
	if r.HasMark {
		x += l.px(markWidth + markGap)
	}
	return x
}

func (l *ScopeList) mouseDown(where geom.Point, button, _ int, _ mod.Modifiers) bool {
	i := l.rowAt(where)
	if i < 0 || button != unison.ButtonLeft {
		return true
	}
	l.RequestFocus()
	r := l.Rows[i]
	chevronRight := l.px(8) + float32(r.Depth)*l.px(folderStep) + l.px(chevronWidth+markGap)
	if r.Expandable && where.X < chevronRight {
		l.toggle(i)
		return true
	}
	l.choose(i)
	return true
}

func (l *ScopeList) toggle(i int) {
	r := &l.Rows[i]
	if !r.Expandable || l.OnToggle == nil {
		return
	}
	l.OnToggle(r.Scope.Path, !r.Expanded)
}

func (l *ScopeList) choose(i int) {
	l.focusRow = i
	l.Current = l.Rows[i].Scope
	l.MarkForRedraw()
	if l.OnChoose != nil {
		l.OnChoose(l.Current)
	}
}

// currentRow is the row of the current scope, or -1.
func (l *ScopeList) currentRow() int {
	for i, r := range l.Rows {
		if !r.Heading && r.Scope == l.Current {
			return i
		}
	}
	return -1
}

func (l *ScopeList) keyDown(key unison.KeyCode, _ mod.Modifiers, _ bool) bool {
	i := l.currentRow()
	step := func(by int) {
		for j := i + by; j >= 0 && j < len(l.Rows); j += by {
			if !l.Rows[j].Heading {
				l.choose(j)
				return
			}
		}
	}
	switch key {
	case unison.KeyUp:
		step(-1)
	case unison.KeyDown:
		step(1)
	case unison.KeyRight:
		if i >= 0 && l.Rows[i].Expandable && !l.Rows[i].Expanded {
			l.toggle(i)
		}
	case unison.KeyLeft:
		if i >= 0 && l.Rows[i].Expandable && l.Rows[i].Expanded {
			l.toggle(i)
		}
	default:
		return false
	}
	return true
}

func (l *ScopeList) draw(gc *unison.Canvas, _ geom.Rect) {
	t := l.ui.Theme.Tokens()
	w := l.ContentRect(false).Width
	tops, heights := l.geometry()
	current := l.currentRow()
	fill := func(r geom.Rect, c palette.Color) {
		gc.DrawRect(r, kvitui.Color(c).Paint(gc, r, paintstyle.Fill))
	}
	for i, r := range l.Rows {
		row := geom.NewRect(0, tops[i], w, heights[i])
		if r.Heading {
			st := l.ui.Chrome(l.ui.Size(kvitui.RoleCaption), text.Semibold, t.TextFaint)
			tl := l.ui.Fonts.Layout([]text.Span{{Text: r.Label, Style: st}}, text.Options{})
			_, h := tl.Size()
			tl.Draw(gc, l.px(scopePadLeft), row.Bottom()-h-l.px(2))
			continue
		}
		switch {
		case i == l.Target:
			fill(row, t.FocusTint)
			p := kvitui.Color(t.Accent).Paint(gc, row, paintstyle.Stroke)
			p.SetStrokeWidth(l.px(2))
			gc.DrawRect(row.Inset(geom.NewUniformInsets(l.px(1))), p)
		case i == current && l.Focused():
			fill(row, t.SelectionActiveTint)
		case i == current:
			fill(row, t.SelectionTint)
		case i == l.hover:
			fill(row, t.HoverTint)
		}
		body := l.ui.Chrome(l.ui.Size(kvitui.RoleBody), text.Regular, t.TextPrimary)
		if r.Scope.Kind == ScopeFolder && r.Expandable {
			glyph := "▸"
			if r.Expanded {
				glyph = "▾"
			}
			st := l.ui.Chrome(l.ui.Size(kvitui.RoleCaption), text.Regular, t.TextMuted)
			cl := l.ui.Fonts.Layout([]text.Span{{Text: glyph, Style: st}}, text.Options{})
			_, ch := cl.Size()
			cl.Draw(gc, l.px(8)+float32(r.Depth)*l.px(folderStep), row.Y+(row.Height-ch)/2)
		}
		x := l.labelLeft(r)
		if r.HasMark {
			m := geom.NewRect(x-l.px(markWidth+markGap), row.Y+(row.Height-l.px(markHeight))/2, l.px(markWidth), l.px(markHeight))
			rad := l.px(markRadius)
			gc.DrawRoundedRect(m, geom.NewSize(rad, rad), kvitui.Color(r.Mark).Paint(gc, m, paintstyle.Fill))
		}
		count := l.ui.Fonts.Layout([]text.Span{{Text: strconv.Itoa(r.Count), Style: l.ui.Chrome(l.ui.Size(kvitui.RoleSmall), text.Regular, t.TextMuted)}}, text.Options{})
		cw, chh := count.Size()
		count.Draw(gc, w-l.px(scopePadRight)-cw, row.Y+(row.Height-chh)/2)
		label := l.ui.Fonts.Layout([]text.Span{{Text: r.Label, Style: body}}, text.Options{MaxWidth: max(1, w-x-cw-l.px(scopePadRight+markGap)), Elide: true})
		_, lh := label.Size()
		label.Draw(gc, x, row.Y+(row.Height-lh)/2)
	}
}

// ProvideAccessibility describes each scope as a tree item, the current one
// holding the focus the list has.
func (l *ScopeList) ProvideAccessibility(b *unison.AccessibilityBuilder) {
	tops, heights := l.geometry()
	current := l.currentRow()
	var focus accessibility.NodeID
	for i, r := range l.Rows {
		if r.Heading {
			continue
		}
		id := b.AddVirtualChild(i, func(n *accessibility.Node) {
			n.Role = role.Row
			n.Name = r.Label
			n.Description = strconv.Itoa(r.Count) + " notes"
			n.Level = r.Depth + 1
			n.Selected = i == current
			n.Expandable = r.Expandable
			n.Expanded = r.Expanded
			n.Bounds = geom.NewRect(0, tops[i], l.ContentRect(false).Width, heights[i])
			n.Actions = n.Actions.With(accessibility.Select)
			if r.Expandable {
				n.Actions = n.Actions.With(accessibility.Expand).With(accessibility.Collapse)
			}
		})
		if i == current {
			focus = id
		}
	}
	if focus != 0 {
		b.FocusChild(focus)
	}
}

// PerformAccessibilityAction chooses, opens or closes a scope for a screen
// reader.
func (l *ScopeList) PerformAccessibilityAction(req accessibility.ActionRequest) bool {
	i, ok := req.Key.(int)
	if !ok || i < 0 || i >= len(l.Rows) || l.Rows[i].Heading {
		return false
	}
	switch req.Action {
	case accessibility.Select:
		l.choose(i)
	case accessibility.Expand, accessibility.Collapse:
		if l.Rows[i].Expanded != (req.Action == accessibility.Expand) {
			l.toggle(i)
		}
	default:
		return false
	}
	return true
}
