package editor

// The / menu (Kvit's BlockMenu.qml, features.md 4.1–4.3): typing "/" in an
// empty block, or the gutter's +, opens a list of block kinds under the
// caret, grouped, with recently used kinds first. What is typed after the
// "/" filters it, the arrows move the highlight, and Enter or Tab turns the
// block into the highlighted kind. The editor keeps the keyboard throughout;
// the menu only shows the list and takes the pointer.

import (
	"slices"
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/pathop"
	"github.com/richardwilkes/unison/enums/role"
)

// menuItem is one kind the / menu offers.
type menuItem struct {
	group, name, desc, icon string
	kind                    Kind
	aliases                 string
}

var menuItems = []menuItem{
	{"Basic", "Text", "Plain paragraph", "¶", Paragraph, "paragraph p"},
	{"Basic", "Heading 1", "Largest heading, for titles", "H1", Heading1, "h1 title"},
	{"Basic", "Heading 2", "Section heading", "H2", Heading2, "h2"},
	{"Basic", "Heading 3", "Subsection heading", "H3", Heading3, "h3"},
	{"Basic", "Heading 4", "Minor heading", "H4", Heading4, "h4"},
	{"Lists", "Bulleted List", "Unordered list item", "•", Bullet, "ul bullet"},
	{"Lists", "Numbered List", "Ordered list item", "1.", Numbered, "ol number"},
	{"Lists", "To-do", "Checkbox item", "☐", Todo, "todo task check"},
	{"Advanced", "Quote", "Block quotation", "❝", Quote, "blockquote"},
	{"Advanced", "Code", "Code block with syntax colouring", "<>", Code, "pre fence"},
	{"Advanced", "Divider", "Horizontal rule", "—", Divider, "hr rule line"},
}

// slashMenu is the open / menu.
type slashMenu struct {
	unison.Panel
	e     *Editor
	block int64
	sel   int
	items []menuItem
	// slash is true when a typed "/" opened the menu and starts the query;
	// the + button opens it on an empty block whose whole text is the query.
	slash  bool
	scroll float32
	popup  *kvitui.Popup
	hide   func()
}

// MenuOpen reports whether the / menu is open.
func (e *Editor) MenuOpen() bool { return e.menu != nil }

// MenuEntries are the names of the / menu's entries, in order, and the
// index of the highlighted one; nil and -1 when the menu is closed.
func (e *Editor) MenuEntries() ([]string, int) {
	if e.menu == nil {
		return nil, -1
	}
	var names []string
	for _, it := range e.menu.items {
		names = append(names, it.name)
	}
	return names, e.menu.sel
}

func (e *Editor) openSlashMenu(block int64, slash bool) {
	e.closeMenu()
	m := &slashMenu{e: e, block: block, slash: slash}
	m.Self = m
	m.SetSizer(m.sizes)
	m.DrawCallback = m.draw
	m.MouseMoveCallback = func(where geom.Point, _ mod.Modifiers) bool {
		if k := m.entryAt(where); k >= 0 && k != m.sel {
			m.sel = k
			m.MarkForRedraw()
		}
		return true
	}
	m.MouseDownCallback = func(where geom.Point, _, _ int, _ mod.Modifiers) bool {
		if k := m.entryAt(where); k >= 0 {
			e.chooseMenu(m.items[k])
			e.changed()
		}
		return true
	}
	m.MouseWheelCallback = func(_, delta geom.Point, _ mod.Modifiers) bool {
		m.scrollTo(m.scroll - delta.Y*e.px(menuEntry))
		return true
	}
	m.Accessibility.Role = role.Menu
	m.Accessibility.Name = "Block type"
	e.menu = m
	e.syncMenu()
	if e.menu == nil {
		return
	}
	w := e.ui.WindowOf(e)
	if w == nil {
		return
	}
	m.popup = &kvitui.Popup{Panel: m, Place: m.place, Anchor: e,
		OnEscape:       func() { e.closeMenu(); e.changed() },
		OnPressOutside: func() { e.closeMenu(); e.changed() },
	}
	m.hide = w.Show(m.popup)
}

func (e *Editor) closeMenu() {
	if e.menu == nil {
		return
	}
	if e.menu.hide != nil {
		e.menu.hide()
	}
	e.menu = nil
}

// relayout places the menu again after the caret moved.
func (m *slashMenu) relayout() {
	if w := m.Window(); w != nil {
		w.Content().MarkForLayoutAndRedraw()
	}
}

// fuzzy reports whether every rune of q appears in s in order.
func fuzzy(q, s string) bool {
	q, s = strings.ToLower(q), strings.ToLower(s)
	j := 0
	sr := []rune(s)
	for _, c := range q {
		for j < len(sr) && sr[j] != c {
			j++
		}
		if j >= len(sr) {
			return false
		}
		j++
	}
	return true
}

// query is what filters the menu: the block's text after the "/" (or its
// whole text when + opened the menu), and false once the menu should close.
func (e *Editor) query() (string, bool) {
	b := e.Doc.Block(e.menu.block)
	if b == nil || e.Doc.Caret.Block != b.ID || strings.Contains(b.Text, "\n") || len(b.Text) > 30 {
		return "", false
	}
	if !e.menu.slash {
		return b.Text, true
	}
	if !strings.HasPrefix(b.Text, "/") {
		return "", false
	}
	return strings.TrimPrefix(b.Text, "/"), true
}

// syncMenu filters the entries by the query, and closes the menu when the
// "/" is gone.
func (e *Editor) syncMenu() {
	q, ok := e.query()
	if !ok {
		e.closeMenu()
		return
	}
	var items []menuItem
	for _, k := range e.recent {
		for _, it := range menuItems {
			if it.kind == k && (fuzzy(q, it.name) || fuzzy(q, it.aliases)) {
				it.group = "Recently used"
				items = append(items, it)
			}
		}
	}
	for _, it := range menuItems {
		if fuzzy(q, it.name) || fuzzy(q, it.aliases) {
			items = append(items, it)
		}
	}
	m := e.menu
	m.items = items
	m.sel = min(m.sel, max(0, len(items)-1))
	m.reveal()
	m.relayout()
}

// menuKey handles the keys the open menu takes before the editor.
func (e *Editor) menuKey(key unison.KeyCode) bool {
	m := e.menu
	switch key {
	case unison.KeyEscape:
		e.closeMenu()
	case unison.KeyUp:
		if len(m.items) > 0 {
			m.sel = (m.sel - 1 + len(m.items)) % len(m.items)
			m.reveal()
		}
	case unison.KeyDown:
		if len(m.items) > 0 {
			m.sel = (m.sel + 1) % len(m.items)
			m.reveal()
		}
	case unison.KeyReturn, unison.KeyTab:
		if len(m.items) > 0 {
			e.chooseMenu(m.items[m.sel])
		} else {
			e.closeMenu()
		}
	default:
		return false
	}
	if e.menu != nil {
		e.menu.MarkForRedraw()
	}
	return true
}

// chooseMenu turns the menu's block into a kind: its query text goes, as one
// undo step, and the conversion is another.
func (e *Editor) chooseMenu(it menuItem) {
	d := e.Doc
	id := e.menu.block
	e.closeMenu()
	b := d.Block(id)
	if b == nil {
		return
	}
	d.Edit("convert", func() {
		b.Text = ""
		d.SetCaret(id, 0)
	})
	d.Convert(id, it.kind)
	e.recent = slices.DeleteFunc(e.recent, func(k Kind) bool { return k == it.kind })
	e.recent = append([]Kind{it.kind}, e.recent...)
	if len(e.recent) > 3 {
		e.recent = e.recent[:3]
	}
	e.touched()
}

// rows are the menu's lines: a group heading before each group's first
// entry, then the entries. entry is -1 for a heading.
func (m *slashMenu) rows() (tops []float32, entries []int, height float32) {
	e := m.e
	y := e.px(menuPad)
	group := ""
	for k, it := range m.items {
		if it.group != group {
			group = it.group
			tops = append(tops, y)
			entries = append(entries, -1)
			y += e.px(menuGroup)
		}
		tops = append(tops, y)
		entries = append(entries, k)
		y += e.px(menuEntry)
	}
	return tops, entries, y + e.px(menuPad*2)
}

func (m *slashMenu) sizes(geom.Size) (minSize, prefSize, maxSize geom.Size) {
	e := m.e
	_, _, h := m.rows()
	if len(m.items) == 0 {
		h = e.px(menuEntry)
	}
	s := geom.NewSize(e.px(menuWidth), min(h, e.px(menuMaxHeight)))
	return s, s, s
}

// place puts the menu under the caret, or above it when there is no room
// below in the view the note scrolls in.
func (m *slashMenu) place(bounds geom.Rect, size geom.Size) geom.Rect {
	e := m.e
	caret, ok := e.caretRect()
	w := e.Window()
	if !ok || w == nil {
		return geom.NewRect(bounds.X, bounds.Y, size.Width, size.Height)
	}
	c := w.Content().RectFromRoot(e.RectToRoot(caret))
	view := bounds
	for p := e.Parent(); p != nil; p = p.Parent() {
		if _, ok := p.Self.(*unison.ScrollPanel); ok {
			view = w.Content().RectFromRoot(p.RectToRoot(p.ContentRect(false)))
			break
		}
	}
	x := min(c.X, bounds.Right()-size.Width)
	y := c.Bottom() + e.px(menuBelowCaret)
	if y+size.Height > view.Bottom() {
		y = c.Y - size.Height - e.px(menuAboveCaret)
	}
	return geom.NewRect(x, y, size.Width, size.Height)
}

// entryAt is the entry under a point, or -1.
func (m *slashMenu) entryAt(where geom.Point) int {
	tops, entries, _ := m.rows()
	y := where.Y + m.scroll
	for n, top := range tops {
		if entries[n] >= 0 && y >= top && y < top+m.e.px(menuEntry) {
			return entries[n]
		}
	}
	return -1
}

func (m *slashMenu) scrollTo(y float32) {
	_, _, h := m.rows()
	m.scroll = max(0, min(y, h-m.FrameRect().Height))
	m.MarkForRedraw()
}

// reveal scrolls the highlighted entry into the menu's view.
func (m *slashMenu) reveal() {
	tops, entries, _ := m.rows()
	view := m.FrameRect().Height
	if view <= 0 {
		view = m.e.px(menuMaxHeight)
	}
	for n, top := range tops {
		if entries[n] != m.sel {
			continue
		}
		// Keep the group heading above the first entry of a group in view.
		if n > 0 && entries[n-1] < 0 {
			top = tops[n-1]
		}
		bottom := tops[n] + m.e.px(menuEntry+menuPad)
		switch {
		case top < m.scroll:
			m.scrollTo(top - m.e.px(menuPad))
		case bottom > m.scroll+view:
			m.scrollTo(bottom - view)
		}
	}
}

func (m *slashMenu) draw(gc *unison.Canvas, _ geom.Rect) {
	e := m.e
	t := e.tok()
	r := m.ContentRect(false)
	radius := e.px(menuRadius)
	e.fillRound(gc, r, radius, t.PopupBackground)
	if len(m.items) == 0 {
		l := e.label("No matching blocks", e.chrome(kvitui.RoleBody, text.Regular, t.TextSecondary))
		_, h := l.Size()
		l.Draw(gc, e.px(menuGap), (r.Height-h)/2)
		e.stroke(gc, r, radius, e.px(1), t.BorderStrong)
		return
	}
	gc.Save()
	gc.ClipRect(r.Inset(geom.NewUniformInsets(e.px(1))), pathop.Intersect, true)
	tops, entries, _ := m.rows()
	pad, inner := e.px(menuPad), e.px(8)
	for n, top := range tops {
		y := top - m.scroll
		if y > r.Bottom() || y+e.px(menuEntry) < 0 {
			continue
		}
		k := entries[n]
		if k < 0 {
			g := e.label(strings.ToUpper(m.items[entries[n+1]].group), e.chrome(kvitui.RoleCaption, text.Bold, t.TextFaint))
			_, h := g.Size()
			g.Draw(gc, pad+inner, y+(e.px(menuGroup)-h)/2)
			continue
		}
		it := m.items[k]
		row := geom.NewRect(pad, y, r.Width-2*pad, e.px(menuEntry))
		if k == m.sel {
			e.fillRound(gc, row, e.px(menuLitRadius), t.FocusTint)
		}
		icon := geom.NewRect(row.X+inner, row.Y+(row.Height-e.px(menuIcon))/2, e.px(menuIcon), e.px(menuIcon))
		e.fillRound(gc, icon, e.px(menuIconRadius), t.ChipBackground)
		e.stroke(gc, icon, e.px(menuIconRadius), e.px(1), t.Border)
		gl := e.label(it.icon, e.chrome(kvitui.RoleBody, text.Bold, t.TextSecondary))
		gw, gh := gl.Size()
		gl.Draw(gc, icon.X+(icon.Width-gw)/2, icon.Y+(icon.Height-gh)/2)
		name := e.label(it.name, e.chrome(kvitui.RoleStrong, text.Regular, t.TextPrimary))
		desc := e.label(it.desc, e.chrome(kvitui.RoleSmall, text.Regular, t.TextFaint))
		_, nh := name.Size()
		_, dh := desc.Size()
		x := icon.Right() + e.px(menuGap)
		ty := row.Y + (row.Height-nh-dh)/2
		name.Draw(gc, x, ty)
		desc.Draw(gc, x, ty+nh)
	}
	gc.Restore()
	e.stroke(gc, r, radius, e.px(1), t.BorderStrong)
}

// ProvideAccessibility describes the menu's entries, the highlighted one
// selected, since the keyboard stays in the editor while the menu is open.
func (m *slashMenu) ProvideAccessibility(b *unison.AccessibilityBuilder) {
	tops, entries, _ := m.rows()
	for n, k := range entries {
		if k < 0 {
			continue
		}
		it := m.items[k]
		top := tops[n] - m.scroll
		b.AddVirtualChild(k, func(node *accessibility.Node) {
			node.Role = role.MenuItem
			node.Name = it.name
			node.Description = it.desc
			node.Selected = k == m.sel
			node.Bounds = geom.NewRect(0, top, m.FrameRect().Width, m.e.px(menuEntry))
			node.Actions = node.Actions.With(accessibility.Press)
		})
	}
}

// PerformAccessibilityAction chooses an entry a screen reader pressed.
func (m *slashMenu) PerformAccessibilityAction(req accessibility.ActionRequest) bool {
	k, ok := req.Key.(int)
	if !ok || req.Action != accessibility.Press || k < 0 || k >= len(m.items) {
		return false
	}
	m.e.chooseMenu(m.items[k])
	m.e.changed()
	return true
}
