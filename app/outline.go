package app

// The outline pane: the open note's headings at the right of the editor,
// indented by level. Clicking one scrolls the note to it; the section the
// caret is in is highlighted; a heading with headings under it folds them
// away; and the H… menu chooses which of the four levels are listed
// ("view.outlineLevels", all four until the reader changes it).

import (
	"fmt"

	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/role"
)

// Outline row sizes in design pixels.
const (
	outlineRow    = 26
	outlineIndent = 14
	outlinePad    = 8
	outlineArrow  = 16
)

// outlineRowItem is one heading in the outline.
type outlineRowItem struct {
	block    int64 // the heading block's id
	index    int   // its place in the note
	level    int   // 1 to 4
	text     string
	depth    int  // how far it is indented: the headings above it it is under
	children bool // headings are under it
}

// Outline is the outline pane.
type Outline struct {
	*unison.Panel
	ui    *kvitui.UI
	ed    *editor.Editor
	list  *outlineList
	empty *kvitui.Label
	// emptyBox, holding empty, is shown in place of region when the note
	// has no headings.
	emptyBox *unison.Panel
	region   *kvitui.Region
	// Levels is which heading levels are listed, bit 0 for level 1.
	Levels int
	// OnLevels runs when the reader changes Levels.
	OnLevels func(levels int)
	// OnClose hides the pane.
	OnClose func()
	// OnGo scrolls the note to a block.
	OnGo   func(index int)
	folded map[int64]bool
}

// NewOutline returns the outline pane for an editor.
func NewOutline(ui *kvitui.UI, ed *editor.Editor) *Outline {
	o := &Outline{ui: ui, ed: ed, Levels: 0xF, folded: map[int64]bool{}}
	title := kvitui.NewLabel(ui, "Outline")
	title.Role, title.Ink = kvitui.RoleStrong, kvitui.InkTextSecondary
	levels := kvitui.NewButton(ui, "H…")
	levels.Form = kvitui.ButtonQuiet
	levels.Explanation = "Heading levels shown"
	levels.OnClick = func() { ui.ShowMenu(levels, "Heading levels", o.levelItems()) }
	closeIt := kvitui.NewIconButton(ui, "close", "Hide outline")
	closeIt.OnClick = func() {
		if o.OnClose != nil {
			o.OnClose()
		}
	}
	head := headerRow(ui, title, levels, closeIt)
	head.SetBorder(kvitui.Padding(ui, kvitui.SizeSpace))
	head.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	o.empty = kvitui.NewLabel(ui, "No headings yet. Add a heading to build the outline.")
	o.empty.Ink = kvitui.InkTextSecondary
	o.empty.Wrap = true
	o.list = newOutlineList(o)
	o.region = kvitui.NewRegion(ui, o.list)
	o.region.Padding = kvitui.Px(0)
	o.region.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	o.emptyBox = kvitui.Column(ui, kvitui.Px(0), o.empty)
	o.emptyBox.SetBorder(kvitui.Padding(ui, kvitui.SizeSpace))
	o.emptyBox.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	o.Panel = kvitui.Column(ui, kvitui.Px(0), head, o.emptyBox)
	o.Panel.DrawCallback = ground(ui, o.Panel, func() kvitui.Ink { return kvitui.InkPanelBackground })
	return o
}

// levelItems are the H… menu: a line for each level, ticked when listed.
func (o *Outline) levelItems() []kvitui.MenuItem {
	var items []kvitui.MenuItem
	for l := 1; l <= 4; l++ {
		bit := 1 << (l - 1)
		items = append(items, kvitui.MenuItem{Text: fmt.Sprintf("Heading %d", l), Checked: o.Levels&bit != 0,
			OnSelect: func() {
				next := o.Levels ^ bit
				if next == 0 {
					// At least one level stays listed.
					return
				}
				o.Levels = next
				if o.OnLevels != nil {
					o.OnLevels(next)
				}
				o.Refresh()
			}})
	}
	return items
}

// headingLevel is a block kind's heading level, 0 for a block that is not
// a heading.
func headingLevel(k editor.Kind) int {
	switch k {
	case editor.Heading1:
		return 1
	case editor.Heading2:
		return 2
	case editor.Heading3:
		return 3
	case editor.Heading4:
		return 4
	}
	return 0
}

// Refresh reads the headings again, after the note or the caret changed.
func (o *Outline) Refresh() {
	d := o.ed.Doc
	var all []outlineRowItem
	for i, b := range d.Blocks {
		level := headingLevel(b.Kind)
		if level == 0 || o.Levels&(1<<(level-1)) == 0 {
			continue
		}
		all = append(all, outlineRowItem{block: b.ID, index: i, level: level, text: editor.PlainText(b.Text)})
	}
	// Depth is the number of listed headings above a heading that it sits
	// under; a heading has children when the next one is deeper.
	var stack []int
	for i := range all {
		for len(stack) > 0 && stack[len(stack)-1] >= all[i].level {
			stack = stack[:len(stack)-1]
		}
		all[i].depth = len(stack)
		stack = append(stack, all[i].level)
		if i > 0 && all[i].level > all[i-1].level && all[i].depth > all[i-1].depth {
			all[i-1].children = true
		}
	}
	// Rows under a folded heading are left out.
	var rows []outlineRowItem
	hideBelow := -1
	for _, r := range all {
		if hideBelow >= 0 && r.depth > hideBelow {
			continue
		}
		hideBelow = -1
		rows = append(rows, r)
		if r.children && o.folded[r.block] {
			hideBelow = r.depth
		}
	}
	o.list.rows = rows
	o.list.current = -1
	if b := d.CaretBlock(); b != nil {
		at := d.Index(b.ID)
		for i, r := range rows {
			if r.index <= at {
				o.list.current = i
			}
		}
	}
	// The rows, or the line saying there are none, below the heading.
	shown, hide := o.region.AsPanel(), o.emptyBox
	if len(all) == 0 {
		shown, hide = hide, shown
	}
	if shown.Parent() == nil {
		hide.RemoveFromParent()
		o.AddChild(shown)
		o.MarkForLayoutRecursively()
	}
	for p := o.list.AsPanel(); p != nil; p = p.Parent() {
		p.NeedsLayout = true
	}
	o.MarkForRedraw()
}

// Rows are the headings listed, for tests: each one's text, indented two
// spaces a level.
func (o *Outline) Rows() []string {
	var out []string
	for _, r := range o.list.rows {
		s := ""
		for range r.depth {
			s += "  "
		}
		out = append(out, s+r.text)
	}
	return out
}

// Current is the row of the section the caret is in, -1 for none.
func (o *Outline) Current() int { return o.list.current }

// Go scrolls the note to a row's heading.
func (o *Outline) Go(row int) { o.list.choose(row) }

// outlineList draws the rows.
type outlineList struct {
	unison.Panel
	o       *Outline
	rows    []outlineRowItem
	current int
	hover   int
	focus   int // the row the keyboard is on
}

func newOutlineList(o *Outline) *outlineList {
	l := &outlineList{o: o, current: -1, hover: -1}
	l.Self = l
	l.SetFocusable(true)
	l.SetSizer(func(hint geom.Size) (geom.Size, geom.Size, geom.Size) {
		h := float32(len(l.rows)) * l.px(outlineRow)
		return geom.NewSize(0, h), geom.NewSize(hint.Width, h), geom.NewSize(unison.DefaultMaxSize, h)
	})
	l.DrawCallback = l.draw
	l.MouseDownCallback = func(where geom.Point, button, _ int, _ mod.Modifiers) bool {
		i := l.rowAt(where.Y)
		if i < 0 || button != unison.ButtonLeft {
			return true
		}
		r := l.rows[i]
		arrowX := l.px(outlinePad) + float32(r.depth)*l.px(outlineIndent)
		if r.children && where.X >= arrowX && where.X < arrowX+l.px(outlineArrow) {
			l.fold(i)
			return true
		}
		l.choose(i)
		return true
	}
	l.MouseMoveCallback = func(where geom.Point, _ mod.Modifiers) bool {
		if i := l.rowAt(where.Y); i != l.hover {
			l.hover = i
			l.MarkForRedraw()
		}
		return true
	}
	l.MouseEnterCallback = l.MouseMoveCallback
	l.MouseExitCallback = func() bool { l.hover = -1; l.MarkForRedraw(); return true }
	l.KeyDownCallback = l.keyDown
	l.GainedFocusCallback = func() {
		if l.focus < 0 || l.focus >= len(l.rows) {
			l.focus = max(0, l.current)
		}
		l.MarkForRedraw()
	}
	l.LostFocusCallback = l.MarkForRedraw
	l.Accessibility.Role = role.List
	l.Accessibility.Name = "Outline"
	return l
}

func (l *outlineList) px(design float32) float32 {
	return design * float32(l.o.ui.Interface.Px(100)) / 100
}

func (l *outlineList) rowAt(y float32) int {
	if y < 0 {
		return -1
	}
	i := int(y / l.px(outlineRow))
	if i >= len(l.rows) {
		return -1
	}
	return i
}

func (l *outlineList) fold(i int) {
	id := l.rows[i].block
	l.o.folded[id] = !l.o.folded[id]
	l.o.Refresh()
}

func (l *outlineList) choose(i int) {
	if i < 0 || i >= len(l.rows) {
		return
	}
	l.focus = i
	if l.o.OnGo != nil {
		l.o.OnGo(l.rows[i].index)
	}
}

func (l *outlineList) keyDown(key unison.KeyCode, _ mod.Modifiers, _ bool) bool {
	n := len(l.rows)
	if n == 0 {
		return false
	}
	move := func(to int) {
		l.focus = max(0, min(n-1, to))
		h := l.px(outlineRow)
		l.ScrollRectIntoView(geom.NewRect(0, float32(l.focus)*h, 1, h))
		l.MarkForRedraw()
	}
	switch key {
	case unison.KeyUp:
		move(l.focus - 1)
	case unison.KeyDown:
		move(l.focus + 1)
	case unison.KeyHome:
		move(0)
	case unison.KeyEnd:
		move(n - 1)
	case unison.KeyLeft, unison.KeyRight:
		if l.focus >= 0 && l.focus < n && l.rows[l.focus].children &&
			l.o.folded[l.rows[l.focus].block] == (key == unison.KeyRight) {
			l.fold(l.focus)
		}
	case unison.KeyReturn, unison.KeyNumPadEnter, unison.KeySpace:
		l.choose(l.focus)
	default:
		return false
	}
	return true
}

func (l *outlineList) draw(gc *unison.Canvas, dirty geom.Rect) {
	ui := l.o.ui
	t := ui.Theme.Tokens()
	w := l.ContentRect(false).Width
	h := l.px(outlineRow)
	fill := func(r geom.Rect, c unison.Color) { gc.DrawRect(r, c.Paint(gc, r, paintstyle.Fill)) }
	for i := max(0, int(dirty.Y/h)); i < len(l.rows) && float32(i)*h < dirty.Bottom(); i++ {
		r := l.rows[i]
		row := geom.NewRect(0, float32(i)*h, w, h)
		switch {
		case i == l.current:
			fill(row, kvitui.Color(t.SelectionTint))
			fill(geom.NewRect(0, row.Y, 2, h), kvitui.Color(t.Accent))
		case i == l.hover:
			fill(row, kvitui.Color(t.HoverTint))
		}
		x := l.px(outlinePad) + float32(r.depth)*l.px(outlineIndent)
		small := ui.Chrome(ui.Size(kvitui.RoleSmall), text.Regular, t.TextSecondary)
		if r.children {
			arrow := "▾"
			if l.o.folded[r.block] {
				arrow = "▸"
			}
			a := ui.Fonts.Layout([]text.Span{{Text: arrow, Style: small}}, text.Options{})
			aw, ah := a.Size()
			a.Draw(gc, x+(l.px(outlineArrow)-aw)/2, row.Y+(h-ah)/2)
		}
		x += l.px(outlineArrow) + l.px(2)
		size := 11
		weight := text.Regular
		ink := t.TextSecondary
		if r.level == 1 {
			size, weight = 12, text.Bold
		}
		if i == l.current {
			ink = t.TextPrimary
		}
		st := ui.Chrome(ui.Interface.Px(size), weight, ink)
		tl := ui.Fonts.Layout([]text.Span{{Text: r.text, Style: st}},
			text.Options{MaxWidth: max(1, w-x-l.px(6)), Elide: true})
		_, th := tl.Size()
		tl.Draw(gc, x, row.Y+(h-th)/2)
		if i == l.focus && l.Focused() {
			ring := row.Inset(geom.NewUniformInsets(l.px(2)))
			p := kvitui.Color(t.FocusRing).Paint(gc, ring, paintstyle.Stroke)
			p.SetStrokeWidth(l.px(2))
			gc.DrawRect(ring, p)
		}
	}
}

// ProvideAccessibility describes each heading as a list item.
func (l *outlineList) ProvideAccessibility(b *unison.AccessibilityBuilder) {
	h := l.px(outlineRow)
	var focus accessibility.NodeID
	for i, r := range l.rows {
		id := b.AddVirtualChild(i, func(n *accessibility.Node) {
			n.Role = role.ListItem
			n.Name = fmt.Sprintf("Heading level %d: %s", r.level, r.text)
			n.Selected = i == l.current
			n.Bounds = geom.NewRect(0, float32(i)*h, l.ContentRect(false).Width, h)
			n.Actions = n.Actions.With(accessibility.Press)
		})
		if i == l.focus {
			focus = id
		}
	}
	if focus != 0 && l.Focused() {
		b.FocusChild(focus)
	}
}

// PerformAccessibilityAction goes to a heading pressed through a screen
// reader.
func (l *outlineList) PerformAccessibilityAction(req accessibility.ActionRequest) bool {
	i, ok := req.Key.(int)
	if !ok || req.Action != accessibility.Press {
		return false
	}
	l.choose(i)
	return true
}
