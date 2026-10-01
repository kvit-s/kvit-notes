package editor

// Completing wiki links: typing "[[" opens a list under the caret of the
// notes whose names hold what is typed after it, and after a "#", of that
// note's headings. The arrows move through the list, Enter or Tab puts the
// choice in and closes the link with "]]", and Escape closes the list. The
// editor keeps the keyboard throughout. What the list holds is the
// application's, through CompleteLink.

import (
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/role"
)

// Completion is one choice of the list: what it shows, and what goes
// between the brackets when it is chosen.
type Completion struct {
	Label, Detail string
	Insert        string
}

// wikiMenu is the open completion list.
type wikiMenu struct {
	unison.Panel
	e     *Editor
	block int64
	start int // the offset after "[["
	items []Completion
	sel   int
	hide  func()
}

// wikiQuery is the text typed after an unclosed "[[" before the caret, on
// the caret's line, and where it starts.
func (e *Editor) wikiQuery() (string, int, bool) {
	d := e.Doc
	b := d.CaretBlock()
	if b == nil || !d.Focused || d.HasSelection() || !b.Kind.HasInline() || e.CompleteLink == nil || d.ReadOnly {
		return "", 0, false
	}
	r := []rune(b.Text)
	c := min(d.Caret.Off, len(r))
	// No completion inside inline math or code.
	for _, sp := range parseInline(r) {
		if (sp.Kind == sMath || sp.Kind == sCode) && c > sp.Start && c <= sp.End {
			return "", 0, false
		}
	}
	for i := c - 1; i >= 1; i-- {
		switch r[i] {
		case '\n', ']':
			return "", 0, false
		case '[':
			if r[i-1] == '[' {
				return string(r[i+1 : c]), i + 1, true
			}
			return "", 0, false
		}
	}
	return "", 0, false
}

// syncWikiMenu opens, updates or closes the list after the text or the
// caret changed.
func (e *Editor) syncWikiMenu() {
	q, start, ok := e.wikiQuery()
	if !ok {
		e.closeWikiMenu()
		return
	}
	items := e.CompleteLink(q)
	if e.wiki == nil {
		m := &wikiMenu{e: e}
		m.Self = m
		m.SetSizer(m.sizes)
		m.DrawCallback = m.draw
		m.MouseDownCallback = func(where geom.Point, _, _ int, _ mod.Modifiers) bool {
			if k := int(where.Y / e.px(wikiRow)); k >= 0 && k < len(m.items) {
				e.chooseWiki(k)
				e.changed()
			}
			return true
		}
		m.Accessibility.Role = role.Menu
		m.Accessibility.Name = "Link to note"
		w := e.ui.WindowOf(e)
		if w == nil {
			return
		}
		e.wiki = m
		m.hide = w.Show(&kvitui.Popup{Panel: m, Anchor: e, Place: m.place,
			OnEscape:       func() { e.closeWikiMenu() },
			OnPressOutside: func() { e.closeWikiMenu() }})
	}
	m := e.wiki
	if m.block != e.Doc.Caret.Block || m.start != start || len(m.items) != len(items) {
		m.sel = 0
	}
	m.block, m.start, m.items = e.Doc.Caret.Block, start, items
	m.sel = min(m.sel, max(0, len(items)-1))
	for p := m.AsPanel(); p != nil; p = p.Parent() {
		p.NeedsLayout = true
	}
	m.MarkForRedraw()
}

func (e *Editor) closeWikiMenu() {
	if e.wiki == nil {
		return
	}
	e.wiki.hide()
	e.wiki = nil
}

// wikiKey takes the keys the open list uses.
func (e *Editor) wikiKey(key unison.KeyCode) bool {
	m := e.wiki
	switch key {
	case unison.KeyEscape:
		e.closeWikiMenu()
	case unison.KeyUp:
		// Held at either end, the highlight stays there.
		m.sel = max(0, m.sel-1)
	case unison.KeyDown:
		m.sel = max(0, min(len(m.items)-1, m.sel+1))
	case unison.KeyReturn, unison.KeyTab:
		if len(m.items) == 0 {
			return false
		}
		e.chooseWiki(m.sel)
	default:
		return false
	}
	if e.wiki != nil {
		e.wiki.MarkForRedraw()
	}
	return true
}

// chooseWiki puts a choice in place of what was typed after "[[", with the
// closing "]]" unless one follows already; a heading choice keeps the list
// open for nothing more, so the link is done either way.
func (e *Editor) chooseWiki(k int) {
	m := e.wiki
	d := e.Doc
	b := d.Block(m.block)
	if b == nil {
		e.closeWikiMenu()
		return
	}
	r := []rune(b.Text)
	c := min(d.Caret.Off, len(r))
	insert := m.items[k].Insert
	closing := "]]"
	if strings.HasPrefix(string(r[c:]), "]]") {
		closing = ""
	}
	d.Edit("insert", func() {
		b.Text = string(r[:m.start]) + insert + closing + string(r[c:])
		d.SetCaret(b.ID, m.start+len([]rune(insert))+2)
	})
	e.closeWikiMenu()
	e.touched()
}

// The list in design pixels.
const (
	wikiRow   = 30
	wikiWidth = 320
	wikiMax   = 8 // rows shown
)

func (m *wikiMenu) sizes(geom.Size) (geom.Size, geom.Size, geom.Size) {
	e := m.e
	n := max(1, min(len(m.items), wikiMax))
	s := geom.NewSize(e.px(wikiWidth), float32(n)*e.px(wikiRow))
	return s, s, s
}

func (m *wikiMenu) place(bounds geom.Rect, size geom.Size) geom.Rect {
	e := m.e
	caret, ok := e.caretRect()
	w := e.Window()
	if !ok || w == nil {
		return geom.NewRect(bounds.X, bounds.Y, size.Width, size.Height)
	}
	c := w.Content().RectFromRoot(e.RectToRoot(caret))
	x := min(c.X, bounds.Right()-size.Width)
	y := c.Bottom() + e.px(menuBelowCaret)
	if y+size.Height > bounds.Bottom() {
		y = c.Y - size.Height - e.px(menuAboveCaret)
	}
	return geom.NewRect(x, y, size.Width, size.Height)
}

func (m *wikiMenu) draw(gc *unison.Canvas, _ geom.Rect) {
	e := m.e
	t := e.tok()
	r := m.ContentRect(false)
	radius := e.px(menuRadius)
	e.fillRound(gc, r, radius, t.PopupBackground)
	if len(m.items) == 0 {
		l := e.label("No matching notes", e.chrome(kvitui.RoleBody, text.Regular, t.TextSecondary))
		_, h := l.Size()
		l.Draw(gc, e.px(10), (r.Height-h)/2)
	}
	for k, it := range m.items {
		if k >= wikiMax {
			break
		}
		row := geom.NewRect(r.X+e.px(3), float32(k)*e.px(wikiRow), r.Width-e.px(6), e.px(wikiRow))
		if k == m.sel {
			e.fillRound(gc, row.Inset(geom.NewUniformInsets(e.px(1))), e.px(menuLitRadius), t.FocusTint)
		}
		name := e.label(it.Label, e.chrome(kvitui.RoleBody, text.Regular, t.TextPrimary))
		nw, nh := name.Size()
		name.Draw(gc, row.X+e.px(8), row.Y+(row.Height-nh)/2)
		if it.Detail != "" {
			dl := e.ui.Fonts.Layout([]text.Span{{Text: it.Detail, Style: e.chrome(kvitui.RoleSmall, text.Regular, t.TextFaint)}},
				text.Options{MaxWidth: max(1, row.Width-nw-e.px(24)), Elide: true})
			dw, dh := dl.Size()
			dl.Draw(gc, row.Right()-e.px(8)-dw, row.Y+(row.Height-dh)/2)
		}
	}
	e.stroke(gc, r, radius, e.px(1), t.BorderStrong)
}

// WikiMenuEntries are the completion list's labels and the highlighted one,
// for tests; nil when the list is closed.
func (e *Editor) WikiMenuEntries() ([]string, int) {
	if e.wiki == nil {
		return nil, -1
	}
	var out []string
	for _, it := range e.wiki.items {
		out = append(out, it.Label)
	}
	return out, e.wiki.sel
}
