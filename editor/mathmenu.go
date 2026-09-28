package editor

// The math command menu (Kvit's MathCommandMenu.qml): the popup a backslash
// opens in math (mathassist.go). On a bare backslash it shows the
// categories of package mathcmd down its left side and the highlighted
// category's commands as a grid of pictures, with the highlighted command's
// name and meaning under the grid; once letters follow the backslash it is
// one ranked list of matching commands. The editor keeps the keyboard: the
// arrows move through the menu (Left and Right between the categories and
// the grid), Enter or Tab puts the highlighted command in, and Escape closes
// the menu. A query nothing matches closes it, leaving what was typed.

import (
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/accessibility"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/pathop"
	"github.com/richardwilkes/unison/enums/role"

	"github.com/kvit-s/kvit-notes/mathcmd"
	"github.com/kvit-s/kvit-notes/mathtex"
)

// The menu in design pixels (MathCommandMenu.qml).
const (
	mathListWidth   = 320 // completion mode
	mathListRow     = 36
	mathListMax     = 324 // the list's height before it scrolls
	mathListEmpty   = 40
	mathBrowseWidth = 470
	mathBrowseH     = 292
	mathCatWidth    = 132
	mathCatRow      = 26
	mathPaneGap     = 6
	mathGridCols    = 8
	mathCellH       = 44
	mathEchoH       = 26 // the line under the grid naming the highlighted command
	mathGlyphW      = 34 // a completion row's picture
	mathGlyphH      = 28
	mathRowGap      = 10
)

// mathMenu is the open command menu.
type mathMenu struct {
	unison.Panel
	e       *Editor
	surface mathSurface // what the menu was opened for
	track   *mathTrack
	display bool // a display equation: templates go in on several lines
	hide    func()
	query   string

	// Browse mode.
	cats       []string
	cat        int
	grid       []mathcmd.Entry
	cell       int
	inCats     bool // the keyboard is in the categories rather than the grid
	catScroll  float32
	gridScroll float32

	// Completion mode.
	rows   []mathcmd.Entry
	sel    int
	scroll float32
}

// openMathMenu opens the menu in browse mode for surface s; syncing s then
// gives it the query at the caret.
func (e *Editor) openMathMenu(s mathSurface, t *mathTrack, display bool) {
	e.closeMathMenu()
	w := e.ui.WindowOf(e)
	if w == nil {
		return
	}
	m := &mathMenu{e: e, surface: s, track: t, display: display}
	m.Self = m
	m.SetSizer(m.sizes)
	m.DrawCallback = m.draw
	m.MouseMoveCallback = func(where geom.Point, _ mod.Modifiers) bool {
		m.hover(where)
		return true
	}
	m.MouseDownCallback = func(where geom.Point, _, _ int, _ mod.Modifiers) bool {
		m.press(where)
		return true
	}
	m.MouseWheelCallback = func(where, delta geom.Point, _ mod.Modifiers) bool {
		m.wheel(where, delta.Y)
		return true
	}
	m.Accessibility.Role = role.Menu
	m.Accessibility.Name = "Math commands"
	m.cats = e.mathModel().Categories()
	m.reloadGrid()
	e.math.menu = m
	m.hide = w.Show(&kvitui.Popup{Panel: m, Place: m.place, Anchor: e,
		OnEscape:       func() { e.closeMathMenu(); e.changed() },
		OnPressOutside: func() { e.closeMathMenu(); e.changed() },
	})
}

func (e *Editor) closeMathMenu() {
	m := e.math.menu
	if m == nil {
		return
	}
	e.math.menu = nil
	if m.hide != nil {
		m.hide()
	}
}

// mathMenuKey takes the keys the open menu uses before the editor. Left and
// Right in the ranked list close the menu and move the caret.
func (e *Editor) mathMenuKey(key unison.KeyCode, shift bool) bool {
	m := e.math.menu
	switch {
	case key == unison.KeyDown:
		m.next()
	case key == unison.KeyUp:
		m.previous()
	case key == unison.KeyLeft || key == unison.KeyRight:
		var used bool
		if key == unison.KeyLeft {
			used = m.left()
		} else {
			used = m.right()
		}
		if !used {
			e.closeMathMenu()
			return false
		}
	case key == unison.KeyReturn || key == unison.KeyTab && !shift:
		m.choose()
	case key == unison.KeyEscape:
		e.closeMathMenu()
	default:
		return false
	}
	if e.math.menu != nil {
		e.math.menu.MarkForRedraw()
	}
	return true
}

func (m *mathMenu) completing() bool { return m.query != "" }

// setQuery follows the text typed after the backslash: letters rank the
// commands, and none shows the categories again. A query that matches
// nothing closes the menu.
func (m *mathMenu) setQuery(q string) {
	if q == m.query {
		return
	}
	m.query = q
	if q == "" {
		m.rows = nil
		m.reloadGrid()
	} else {
		m.rows = m.e.mathModel().ItemsFor(q)
		m.sel, m.scroll = 0, 0
		if len(m.rows) == 0 {
			m.e.closeMathMenu()
			return
		}
	}
	m.relayout()
}

func (m *mathMenu) reloadGrid() {
	m.grid = nil
	if len(m.cats) > 0 {
		m.grid = m.e.mathModel().ItemsForCategory(m.cats[m.cat])
	}
	m.cell, m.gridScroll = 0, 0
	if len(m.grid) == 0 {
		m.cell = -1
	}
}

func (m *mathMenu) selectCategory(k int) {
	if k < 0 || k >= len(m.cats) {
		return
	}
	m.cat = k
	m.reloadGrid()
	m.reveal()
}

// next and previous are Down and Up: along the ranked list, the categories,
// or the grid a row at a time, held at either end.
func (m *mathMenu) next() {
	switch {
	case m.completing():
		m.sel = min(m.sel+1, len(m.rows)-1)
	case m.inCats:
		m.selectCategory(min(m.cat+1, len(m.cats)-1))
	case len(m.grid) > 0:
		m.cell = min(m.cell+mathGridCols, len(m.grid)-1)
	}
	m.reveal()
}

func (m *mathMenu) previous() {
	switch {
	case m.completing():
		m.sel = max(0, m.sel-1)
	case m.inCats:
		m.selectCategory(max(m.cat-1, 0))
	case len(m.grid) > 0:
		m.cell = max(m.cell-mathGridCols, 0)
	}
	m.reveal()
}

// left and right move between the grid's cells, and from its first column
// to the categories and back. They are not used in the ranked list.
func (m *mathMenu) left() bool {
	switch {
	case m.completing():
		return false
	case m.inCats:
	case m.cell%mathGridCols == 0:
		m.inCats = true
	default:
		m.cell = max(m.cell-1, 0)
	}
	m.reveal()
	return true
}

func (m *mathMenu) right() bool {
	switch {
	case m.completing():
		return false
	case m.inCats:
		m.inCats = false
	case len(m.grid) > 0:
		m.cell = min(m.cell+1, len(m.grid)-1)
	}
	m.reveal()
	return true
}

// choose is Enter or Tab: the highlighted command goes in; in the
// categories, Enter moves to the grid.
func (m *mathMenu) choose() {
	switch {
	case m.completing():
		if m.sel >= 0 && m.sel < len(m.rows) {
			m.apply(m.rows[m.sel])
		}
	case m.inCats:
		m.inCats = false
	case m.cell >= 0 && m.cell < len(m.grid):
		m.apply(m.grid[m.cell])
	}
}

// apply records the command as recently used, closes the menu and puts the
// command in.
func (m *mathMenu) apply(row mathcmd.Entry) {
	e := m.e
	e.mathModel().NoteUsed(row.Name)
	e.closeMathMenu()
	e.applyMathCommand(row, m.surface, m.track, m.display)
}

// Geometry. Everything is in the menu's own coordinates.

func (m *mathMenu) sizes(geom.Size) (geom.Size, geom.Size, geom.Size) {
	e := m.e
	pad := e.px(menuPad)
	var s geom.Size
	if m.completing() {
		h := e.px(mathListEmpty)
		if len(m.rows) > 0 {
			h = min(float32(len(m.rows))*e.px(mathListRow), e.px(mathListMax))
		}
		s = geom.NewSize(e.px(mathListWidth), h+2*pad)
	} else {
		s = geom.NewSize(e.px(mathBrowseWidth), e.px(mathBrowseH)+2*pad)
	}
	return s, s, s
}

// place puts the menu under the caret, or above it when there is no room
// below.
func (m *mathMenu) place(bounds geom.Rect, size geom.Size) geom.Rect {
	e := m.e
	c, ok := m.surface.caretRect()
	if !ok {
		return geom.NewRect(bounds.X, bounds.Y, size.Width, size.Height)
	}
	edge := e.px(menuAboveCaret)
	x := max(bounds.X+edge, min(c.X, bounds.Right()-size.Width-edge))
	y := c.Bottom() + e.px(menuAboveCaret)
	if y+size.Height > bounds.Bottom() && c.Y-size.Height-edge >= bounds.Y {
		y = c.Y - size.Height - edge
	}
	return geom.NewRect(x, y, size.Width, size.Height)
}

func (m *mathMenu) relayout() {
	if w := m.Window(); w != nil {
		w.Content().MarkForLayoutAndRedraw()
	}
	m.MarkForRedraw()
}

// inner is the part of the menu inside its padding.
func (m *mathMenu) inner() geom.Rect {
	return m.ContentRect(false).Inset(geom.NewUniformInsets(m.e.px(menuPad)))
}

// panes are the browse panel's parts: the categories, the grid and the
// line naming the highlighted command.
func (m *mathMenu) panes() (cats, grid, echo geom.Rect) {
	e := m.e
	r := m.inner()
	cats = geom.NewRect(r.X, r.Y, e.px(mathCatWidth), r.Height)
	gx := cats.Right() + 2*e.px(mathPaneGap) + e.px(1)
	echoH := e.px(mathEchoH)
	grid = geom.NewRect(gx, r.Y, r.Right()-gx, r.Height-echoH)
	echo = geom.NewRect(gx, grid.Bottom(), grid.Width, echoH)
	return cats, grid, echo
}

// cellRect is grid cell k, before scrolling.
func (m *mathMenu) cellRect(grid geom.Rect, k int) geom.Rect {
	w := float32(int(grid.Width / mathGridCols))
	h := m.e.px(mathCellH)
	return geom.NewRect(grid.X+float32(k%mathGridCols)*w, grid.Y+float32(k/mathGridCols)*h, w-m.e.px(2), h-m.e.px(2))
}

// reveal scrolls the highlighted row, category and cell into view.
func (m *mathMenu) reveal() {
	e := m.e
	into := func(scroll *float32, top, height, view float32) {
		switch {
		case top < *scroll:
			*scroll = top
		case top+height > *scroll+view:
			*scroll = top + height - view
		}
	}
	if m.completing() {
		into(&m.scroll, float32(m.sel)*e.px(mathListRow), e.px(mathListRow), m.inner().Height)
		return
	}
	cats, grid, _ := m.panes()
	into(&m.catScroll, float32(m.cat)*e.px(mathCatRow), e.px(mathCatRow), cats.Height)
	if m.cell >= 0 {
		into(&m.gridScroll, float32(m.cell/mathGridCols)*e.px(mathCellH), e.px(mathCellH), grid.Height)
	}
}

// at is what lies under a point: a completion row, a category or a grid
// cell, as an index, or -1.
func (m *mathMenu) at(where geom.Point) (row, cat, cell int) {
	e := m.e
	row, cat, cell = -1, -1, -1
	r := m.inner()
	if !where.In(r) {
		return
	}
	if m.completing() {
		if k := int((where.Y - r.Y + m.scroll) / e.px(mathListRow)); k >= 0 && k < len(m.rows) {
			row = k
		}
		return
	}
	cats, grid, _ := m.panes()
	switch {
	case where.In(cats):
		if k := int((where.Y - cats.Y + m.catScroll) / e.px(mathCatRow)); k >= 0 && k < len(m.cats) {
			cat = k
		}
	case where.In(grid):
		p := geom.NewPoint(where.X, where.Y+m.gridScroll)
		for k := range m.grid {
			if p.In(m.cellRect(grid, k)) {
				cell = k
			}
		}
	}
	return
}

// hover highlights what the pointer is over; over a category it shows that
// category, as the Qt menu does.
func (m *mathMenu) hover(where geom.Point) {
	row, cat, cell := m.at(where)
	switch {
	case row >= 0 && row != m.sel:
		m.sel = row
	case cat >= 0 && cat != m.cat:
		m.selectCategory(cat)
	case cell >= 0 && (cell != m.cell || m.inCats):
		m.cell, m.inCats = cell, false
	default:
		return
	}
	m.MarkForRedraw()
}

func (m *mathMenu) press(where geom.Point) {
	row, cat, cell := m.at(where)
	switch {
	case row >= 0:
		m.apply(m.rows[row])
	case cell >= 0:
		m.apply(m.grid[cell])
	case cat >= 0:
		m.selectCategory(cat)
		m.inCats = false
		m.MarkForRedraw()
		return
	default:
		return
	}
	m.e.changed()
}

func (m *mathMenu) wheel(where geom.Point, dy float32) {
	e := m.e
	clamp := func(scroll *float32, step, content, view float32) {
		*scroll = max(0, min(*scroll-dy*step, content-view))
	}
	if m.completing() {
		clamp(&m.scroll, e.px(mathListRow), float32(len(m.rows))*e.px(mathListRow), m.inner().Height)
	} else {
		cats, grid, _ := m.panes()
		if where.In(cats) {
			clamp(&m.catScroll, e.px(mathCatRow), float32(len(m.cats))*e.px(mathCatRow), cats.Height)
		} else {
			rows := (len(m.grid) + mathGridCols - 1) / mathGridCols
			clamp(&m.gridScroll, e.px(mathCellH), float32(rows)*e.px(mathCellH), grid.Height)
		}
	}
	m.MarkForRedraw()
}

// Drawing.

func (m *mathMenu) draw(gc *unison.Canvas, _ geom.Rect) {
	e := m.e
	t := e.tok()
	r := m.ContentRect(false)
	radius := e.px(menuRadius)
	e.fillRound(gc, r, radius, t.PopupBackground)
	if m.completing() {
		m.drawList(gc)
	} else {
		m.drawBrowse(gc)
	}
	e.stroke(gc, r, radius, e.px(1), t.BorderStrong)
}

func (m *mathMenu) drawList(gc *unison.Canvas) {
	e := m.e
	t := e.tok()
	in := m.inner()
	if len(m.rows) == 0 {
		l := e.label("No matches", e.chrome(kvitui.RoleStrong, text.Regular, t.TextFaint))
		w, h := l.Size()
		l.Draw(gc, in.X+(in.Width-w)/2, in.Y+(in.Height-h)/2)
		return
	}
	gc.Save()
	gc.ClipRect(in, pathop.Intersect, true)
	rowH := e.px(mathListRow)
	for k, row := range m.rows {
		y := in.Y + float32(k)*rowH - m.scroll
		if y > in.Bottom() || y+rowH < in.Y {
			continue
		}
		box := geom.NewRect(in.X, y, in.Width, rowH)
		if k == m.sel {
			e.fillRound(gc, box, e.px(menuLitRadius), t.FocusTint)
		}
		x := box.X + e.px(8)
		glyph := geom.NewRect(x, y+(rowH-e.px(mathGlyphH))/2, e.px(mathGlyphW), e.px(mathGlyphH))
		m.drawGlyph(gc, row, glyph)
		x = glyph.Right() + e.px(mathRowGap)
		name := e.label(row.Name, m.monoStyle(kvitui.RoleStrong))
		nw, nh := name.Size()
		name.Draw(gc, x, y+(rowH-nh)/2)
		if row.Category != "" {
			c := e.label(row.Category, e.chrome(kvitui.RoleSmall, text.Regular, t.TextFaint))
			_, ch := c.Size()
			c.Draw(gc, x+nw+e.px(mathRowGap), y+(rowH-ch)/2)
		}
	}
	gc.Restore()
}

func (m *mathMenu) drawBrowse(gc *unison.Canvas) {
	e := m.e
	t := e.tok()
	cats, grid, echo := m.panes()

	gc.Save()
	gc.ClipRect(cats, pathop.Intersect, true)
	rowH := e.px(mathCatRow)
	for k, name := range m.cats {
		y := cats.Y + float32(k)*rowH - m.catScroll
		if y > cats.Bottom() || y+rowH < cats.Y {
			continue
		}
		row := geom.NewRect(cats.X, y, cats.Width, rowH)
		c := t.TextSecondary
		if k == m.cat {
			tint := t.HoverTint
			if m.inCats {
				tint = t.FocusTint
			}
			e.fillRound(gc, row, e.px(menuLitRadius), tint)
			c = t.TextPrimary
		}
		l := e.ui.Fonts.Layout([]text.Span{{Text: name, Style: e.chrome(kvitui.RoleBody, text.Regular, c)}},
			text.Options{MaxWidth: max(1, row.Width-e.px(12)), Elide: true})
		_, h := l.Size()
		l.Draw(gc, row.X+e.px(8), y+(rowH-h)/2)
	}
	gc.Restore()

	line := geom.NewRect(cats.Right()+e.px(mathPaneGap), cats.Y, e.px(1), cats.Height)
	e.fillRound(gc, line, 0, t.Border)

	gc.Save()
	gc.ClipRect(grid, pathop.Intersect, true)
	for k, row := range m.grid {
		cell := m.cellRect(grid, k)
		cell.Y -= m.gridScroll
		if cell.Y > grid.Bottom() || cell.Bottom() < grid.Y {
			continue
		}
		if k == m.cell {
			if !m.inCats {
				e.fillRound(gc, cell, e.px(menuLitRadius), t.FocusTint)
			}
			e.stroke(gc, cell, e.px(menuLitRadius), e.px(1), t.Accent)
		}
		m.drawGlyph(gc, row, cell.Inset(geom.NewUniformInsets(e.px(3))))
	}
	gc.Restore()

	if m.cell >= 0 && m.cell < len(m.grid) {
		row := m.grid[m.cell]
		s := row.Name
		if row.Description != "" {
			s += "  —  " + row.Description
		}
		l := e.ui.Fonts.Layout([]text.Span{{Text: s, Style: e.chrome(kvitui.RoleBody, text.Regular, t.TextMuted)}},
			text.Options{MaxWidth: echo.Width, Elide: true})
		w, h := l.Size()
		l.Draw(gc, echo.X+(echo.Width-w)/2, echo.Y+(echo.Height-h)/2)
	}
}

// monoStyle is a command name's style: the code font at a type role's size.
func (m *mathMenu) monoStyle(r kvitui.TypeRole) text.Style {
	st := m.e.chrome(r, text.Regular, m.e.tok().TextPrimary)
	st.Family = m.e.monoFamily()
	return st
}

// drawGlyph draws an entry's picture in box: its Preview TeX rendered by
// the math engine, shrunk to fit and centred. When the engine is not
// loaded, the TeX does not render, or the entry has no picture (\\ and &),
// the entry's name is drawn as text instead.
func (m *mathMenu) drawGlyph(gc *unison.Canvas, row mathcmd.Entry, box geom.Rect) {
	e := m.e
	if row.Preview != "" && mathtex.Available() {
		if f, err := mathtex.Render(row.Preview, e.ui.Size(kvitui.RoleTitle), false); err == nil && f.Width > 0 && f.Height > 0 {
			fw, fh := float32(f.Width), float32(f.Height)
			scale := min(1, box.Width/fw, box.Height/fh)
			gc.Save()
			gc.Translate(geom.NewPoint(box.X+(box.Width-fw*scale)/2, box.Y+(box.Height-fh*scale)/2))
			gc.Scale(geom.NewPoint(scale, scale))
			f.Draw(gc, 0, 0, kvitui.Color(e.tok().TextPrimary))
			gc.Restore()
			return
		}
	}
	l := e.ui.Fonts.Layout([]text.Span{{Text: row.Name, Style: m.monoStyle(kvitui.RoleBody)}},
		text.Options{MaxWidth: box.Width, Elide: true})
	w, h := l.Size()
	l.Draw(gc, box.X+(box.Width-w)/2, box.Y+(box.Height-h)/2)
}

// mathMenuKey is an accessibility key: a category or an entry.
type mathMenuKey struct {
	cat bool
	k   int
}

// ProvideAccessibility describes the menu's lines and cells, the
// highlighted ones selected, since the keyboard stays in the editor.
func (m *mathMenu) ProvideAccessibility(b *unison.AccessibilityBuilder) {
	e := m.e
	add := func(key mathMenuKey, name, desc string, selected bool, bounds geom.Rect) {
		b.AddVirtualChild(key, func(n *accessibility.Node) {
			n.Role = role.MenuItem
			n.Name = name
			n.Description = desc
			n.Selected = selected
			n.Bounds = bounds
			n.Actions = n.Actions.With(accessibility.Press)
		})
	}
	if m.completing() {
		in := m.inner()
		for k, row := range m.rows {
			top := in.Y + float32(k)*e.px(mathListRow) - m.scroll
			add(mathMenuKey{k: k}, row.Name, row.Category, k == m.sel, geom.NewRect(in.X, top, in.Width, e.px(mathListRow)))
		}
		return
	}
	cats, grid, _ := m.panes()
	for k, name := range m.cats {
		top := cats.Y + float32(k)*e.px(mathCatRow) - m.catScroll
		add(mathMenuKey{cat: true, k: k}, name, "", k == m.cat, geom.NewRect(cats.X, top, cats.Width, e.px(mathCatRow)))
	}
	for k, row := range m.grid {
		cell := m.cellRect(grid, k)
		cell.Y -= m.gridScroll
		add(mathMenuKey{k: k}, row.Name, row.Description, k == m.cell, cell)
	}
}

// PerformAccessibilityAction chooses what a screen reader pressed.
func (m *mathMenu) PerformAccessibilityAction(req accessibility.ActionRequest) bool {
	key, ok := req.Key.(mathMenuKey)
	if !ok || req.Action != accessibility.Press {
		return false
	}
	switch {
	case key.cat && key.k < len(m.cats):
		m.selectCategory(key.k)
		m.inCats = false
		m.MarkForRedraw()
	case !key.cat && m.completing() && key.k < len(m.rows):
		m.apply(m.rows[key.k])
		m.e.changed()
	case !key.cat && !m.completing() && key.k < len(m.grid):
		m.apply(m.grid[key.k])
		m.e.changed()
	default:
		return false
	}
	return true
}
