package editor

// The grid-size picker for inserting a table: a hover grid up to 8x8, the
// arrows moving the selection the same way the pointer does, Enter accepting
// it (3x3 until something moves it). Choosing converts the target block to a
// table with an empty grid of that size; there is no seed text.

import (
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/role"
)

// The picker's bounds: the grid, its default and the cell squares.
const (
	tablePickerMaxCols = 8
	tablePickerMaxRows = 8
	tablePickerDefCols = 3
	tablePickerDefRows = 3
	tablePickerCell    = 18
	tablePickerGap     = 3
	tablePickerPad     = 8
	tablePickerRadius  = 2
)

// pickerSize clamps a hovered size into the grid.
func pickerSize(cols, rows int) (int, int) {
	return min(max(cols, 1), tablePickerMaxCols), min(max(rows, 1), tablePickerMaxRows)
}

// tablePicker is the open picker.
type tablePicker struct {
	unison.Panel
	e         *Editor
	target    int64
	hoverCols int
	hoverRows int
	onPick    func(cols, rows int)
	hide      func()
}

// TablePickerOpen reports whether the grid picker is open.
func (e *Editor) TablePickerOpen() bool { return e.picker != nil }

// pickerSizeText is what the picker shows and announces: "3 × 2".
func pickerSizeText(cols, rows int) string {
	return itoa(cols) + " × " + itoa(rows)
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	neg := n < 0
	if neg {
		n = -n
	}
	var b [20]byte
	i := len(b)
	for n > 0 {
		i--
		b[i] = byte('0' + n%10)
		n /= 10
	}
	if neg {
		i--
		b[i] = '-'
	}
	return string(b[i:])
}

// openTablePicker opens the grid picker for target: choosing converts it to
// a table with an empty grid of that size. Each opening starts at 3x3, so
// the keyboard route always begins from a known cell.
func (e *Editor) openTablePicker(target int64, onPick func(cols, rows int)) {
	w := e.ui.WindowOf(e)
	if w == nil || e.Doc.ReadOnly {
		return
	}
	e.closeTablePicker()
	p := &tablePicker{e: e, target: target, hoverCols: tablePickerDefCols, hoverRows: tablePickerDefRows, onPick: onPick}
	p.Self = p
	p.SetFocusable(true)
	p.SetSizer(p.sizes)
	p.DrawCallback = p.draw
	p.MouseMoveCallback = p.mouseMove
	p.MouseDownCallback = p.mouseDown
	p.KeyDownCallback = p.keyDown
	p.Accessibility.Role = role.Dialog
	p.Accessibility.Name = "Table size"
	p.Accessibility.Description = pickerSizeText(p.hoverCols, p.hoverRows)
	e.picker = p
	hide := w.Show(&kvitui.Popup{Panel: p, Anchor: e,
		OnEscape:       func() { e.closeTablePicker(); e.changed() },
		OnPressOutside: func() { e.closeTablePicker(); e.changed() },
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			// Centred in the window.
			x := bounds.X + (bounds.Width-size.Width)/2
			y := bounds.Y + (bounds.Height-size.Height)/2
			return geom.NewRect(x, y, size.Width, size.Height)
		}})
	p.hide = hide
	unison.InvokeTask(func() { p.RequestFocus() })
	e.changed()
}

// closeTablePicker shuts the picker without choosing.
func (e *Editor) closeTablePicker() {
	if e.picker == nil {
		return
	}
	if e.picker.hide != nil {
		hide := e.picker.hide
		e.picker.hide = nil
		e.picker = nil
		hide()
	} else {
		e.picker = nil
	}
	e.RequestFocus()
}

// pickTableSize takes the hovered size: converts the target and focuses it.
func (e *Editor) pickTableSize(cols, rows int) {
	p := e.picker
	if p == nil {
		return
	}
	cols, rows = pickerSize(cols, rows)
	onPick := p.onPick
	target := p.target
	e.closeTablePicker()
	if onPick != nil {
		onPick(cols, rows)
	} else {
		e.convertToTable(target, cols, rows)
	}
	e.changed()
}

// convertToTable turns block id into a table holding an empty grid of the
// size, and makes its first cell live.
func (e *Editor) convertToTable(id int64, cols, rows int) {
	d := e.Doc
	b := d.Block(id)
	if b == nil || d.ReadOnly {
		return
	}
	cols, rows = clampTableSize(cols, rows)
	md := EmptyTable(cols, rows)
	d.Edit("insert table", func() {
		b.Kind = Table
		b.Text = md
		b.Attrs = ""
		d.SetCaret(id, 0)
	})
	e.clearBlockSel()
	if i := d.Index(id); i >= 0 {
		e.measure()
		e.activateTableCell(i, -1, 0, true)
	}
}

func (p *tablePicker) sizes(geom.Size) (minSize, prefSize, maxSize geom.Size) {
	e := p.e
	cell := e.px(tablePickerCell)
	gap := e.px(tablePickerGap)
	pad := e.px(tablePickerPad)
	w := float32(tablePickerMaxCols)*cell + float32(tablePickerMaxCols-1)*gap + 2*pad
	// Grid plus the size line under it.
	h := float32(tablePickerMaxRows)*cell + float32(tablePickerMaxRows-1)*gap + 2*pad + e.px(24)
	s := geom.NewSize(w, h)
	return s, s, s
}

func (p *tablePicker) gridOrigin() geom.Point {
	e := p.e
	return geom.NewPoint(e.px(tablePickerPad), e.px(tablePickerPad))
}

func (p *tablePicker) cellAt(where geom.Point) (cols, rows int, ok bool) {
	e := p.e
	cell := float64(e.px(tablePickerCell) + e.px(tablePickerGap))
	o := p.gridOrigin()
	relX, relY := float64(where.X-o.X), float64(where.Y-o.Y)
	if relX < 0 || relY < 0 {
		return 0, 0, false
	}
	c, r := int(relX/cell)+1, int(relY/cell)+1
	if c < 1 || c > tablePickerMaxCols || r < 1 || r > tablePickerMaxRows {
		return 0, 0, false
	}
	return c, r, true
}

func (p *tablePicker) mouseMove(where geom.Point, _ mod.Modifiers) bool {
	if c, r, ok := p.cellAt(where); ok && (c != p.hoverCols || r != p.hoverRows) {
		p.hoverCols, p.hoverRows = c, r
		p.Accessibility.Description = pickerSizeText(c, r)
		p.MarkForRedraw()
	}
	return true
}

func (p *tablePicker) mouseDown(where geom.Point, button, _ int, _ mod.Modifiers) bool {
	if button != unison.ButtonLeft {
		return false
	}
	if c, r, ok := p.cellAt(where); ok {
		p.e.pickTableSize(c, r)
		return true
	}
	return false
}

func (p *tablePicker) keyDown(key unison.KeyCode, mods mod.Modifiers, _ bool) bool {
	e := p.e
	switch key {
	case unison.KeyEscape:
		e.closeTablePicker()
		e.changed()
		return true
	case unison.KeyReturn:
		e.pickTableSize(p.hoverCols, p.hoverRows)
		return true
	case unison.KeyLeft:
		if c, _ := pickerSize(p.hoverCols-1, p.hoverRows); true {
			p.hoverCols = c
			p.Accessibility.Description = pickerSizeText(p.hoverCols, p.hoverRows)
			p.MarkForRedraw()
		}
		return true
	case unison.KeyRight:
		c, _ := pickerSize(p.hoverCols+1, p.hoverRows)
		p.hoverCols = c
		p.Accessibility.Description = pickerSizeText(p.hoverCols, p.hoverRows)
		p.MarkForRedraw()
		return true
	case unison.KeyUp:
		_, r := pickerSize(p.hoverCols, p.hoverRows-1)
		p.hoverRows = r
		p.Accessibility.Description = pickerSizeText(p.hoverCols, p.hoverRows)
		p.MarkForRedraw()
		return true
	case unison.KeyDown:
		_, r := pickerSize(p.hoverCols, p.hoverRows+1)
		p.hoverRows = r
		p.Accessibility.Description = pickerSizeText(p.hoverCols, p.hoverRows)
		p.MarkForRedraw()
		return true
	}
	_ = mods
	return false
}

func (p *tablePicker) draw(gc *unison.Canvas, _ geom.Rect) {
	e := p.e
	t := e.tok()
	o := p.gridOrigin()
	cell := e.px(tablePickerCell)
	gap := e.px(tablePickerGap)
	for r := 0; r < tablePickerMaxRows; r++ {
		for c := 0; c < tablePickerMaxCols; c++ {
			x := o.X + float32(c)*(cell+gap)
			y := o.Y + float32(r)*(cell+gap)
			rect := geom.NewRect(x, y, cell, cell)
			rad := e.px(tablePickerRadius)
			if c < p.hoverCols && r < p.hoverRows {
				gc.DrawRect(rect, kvitui.Color(t.Accent).Paint(gc, rect, paintstyle.Fill))
			} else {
				gc.DrawRect(rect, kvitui.Color(t.ChipBackground).Paint(gc, rect, paintstyle.Fill))
			}
			_ = rad
			gc.DrawRect(rect, kvitui.Color(t.BorderStrong).Paint(gc, rect, paintstyle.Stroke))
		}
	}
	st := e.ui.Chrome(e.ui.Size(kvitui.RoleBody), text.Regular, t.TextMuted)
	l := e.ui.Fonts.Layout([]text.Span{{Text: pickerSizeText(p.hoverCols, p.hoverRows), Style: st}}, text.Options{})
	lw, _ := l.Size()
	rect := p.ContentRect(false)
	l.Draw(gc, (rect.Width-lw)/2, o.Y+float32(tablePickerMaxRows)*(cell+gap)+e.px(4))
}
