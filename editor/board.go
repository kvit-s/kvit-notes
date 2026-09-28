package editor

// Task boards (features.md 1.2.12, Kvit's KanbanBlock.qml): a code fence of
// language "kanban" is drawn, while the caret is elsewhere, as columns of
// cards. A card's box ticks it done; pressing its text edits its line in
// place, where "#label" and "📅 YYYY-MM-DD" are written as the file holds
// them; its menu moves it to another column or deletes it. A column's header
// folds it away, renames it, moves it left or right, adds a card to it and
// deletes it; "+ Column" adds one. The chips above the board show only the
// cards with a label, or hide the finished ones. Every change is the kanban
// package's rewrite of the fence's text, one undo step each. With the caret
// in the block its Markdown shows, as a table's does.

import (
	"fmt"
	"hash/fnv"
	"slices"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/kanban"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// isBoard reports whether a block is a task board.
func isBoard(b *Block) bool { return b.Kind == Code && strings.EqualFold(b.Lang, "kanban") }

// boardShows reports whether a board is drawn as its columns: while the
// caret is elsewhere.
func (e *Editor) boardShows(i int) bool {
	b := &e.Doc.Blocks[i]
	return isBoard(b) && !(e.Doc.Focused && e.Doc.Caret.Block == b.ID)
}

// The board in design pixels (KanbanBlock.qml).
const (
	boardPad      = 8
	boardFilterH  = 26
	boardColW     = 240
	boardColMin   = 150
	boardColGap   = 10
	boardAddColW  = 120
	boardHeaderH  = 30
	boardCardPad  = 6
	boardCardGap  = 6
	boardBox      = 14
	boardChipH    = 18
	boardAddCardH = 28
	boardRadius   = 6
	boardCtl      = 18 // a header control's width
	boardFootH    = 16 // a card's added/changed days at its foot
)

// cardFoot is the days a card shows at its foot: when it was added and
// when it last changed, from the HTML comment at the end of its line; one
// date when they are the same, "" when neither is known.
func cardFoot(card kanban.Card) string {
	switch {
	case card.Created != "" && card.Modified != "" && card.Modified != card.Created:
		return "added " + card.Created + " · changed " + card.Modified
	case card.Created != "":
		return "added " + card.Created
	case card.Modified != "":
		return "changed " + card.Modified
	}
	return ""
}

// boardView is what the reader has chosen for a board while it is open:
// folded columns and the filter. It is not written to the note.
type boardView struct {
	folded   map[string]bool
	label    string // the label the cards are narrowed to, "" for all
	hideDone bool
}

func (e *Editor) viewOf(id int64) *boardView {
	if e.boardViews == nil {
		e.boardViews = map[int64]*boardView{}
	}
	v := e.boardViews[id]
	if v == nil {
		v = &boardView{folded: map[string]bool{}}
		e.boardViews[id] = v
	}
	return v
}

// cardBox is one card as laid out: its place, and its title's, description's
// and chips' layouts.
type cardBox struct {
	col, index int
	r          geom.Rect
	box        geom.Rect // the done box
	title      *text.Layout
	desc       *text.Layout
	descProj   projection // the description as drawn, for its typeset math
	chips      []chip
	foot       string // the added/changed days at the card's foot, "" for none
}

// chip is a label or due date under a card's title, or a filter chip.
type chip struct {
	words string
	r     geom.Rect
	label string // the label it stands for; "" for a due date or Hide done
	due   bool
}

// colBox is one column as laid out, with its header's controls.
type colBox struct {
	r                                   geom.Rect
	fold, name, left, right, add, close geom.Rect
	addCard                             geom.Rect
	shown, total                        int
}

// boardLayout is a board laid out at a width.
type boardLayout struct {
	board   *kanban.Board
	filters []chip
	cols    []colBox
	cards   []cardBox
	addCol  geom.Rect
	height  float32
}

// labelColor is the colour a label is drawn in: one of the theme's palette
// colours, the same for a name every time.
func labelColor(name string) text.Color {
	pal := tokens.ColorPalette()
	h := fnv.New32a()
	h.Write([]byte(strings.ToLower(name)))
	return colour(pal[int(h.Sum32()%uint32(len(pal)))])
}

// layBoard lays a board out with its top left at the origin, in a width.
func (e *Editor) layBoard(b *Block, width float32) *boardLayout {
	v := e.viewOf(b.ID)
	bl := &boardLayout{board: kanban.Parse(b.Text)}
	t := e.tok()
	small := e.chrome(kvitui.RoleSmall, text.Regular, t.TextSecondary)
	y := e.px(boardPad)
	// The filter chips: every label on the board, and Hide done.
	var labels []string
	anyDone := false
	for _, c := range bl.board.Columns {
		for _, card := range c.Cards {
			for _, l := range card.Labels {
				if !slices.Contains(labels, l) {
					labels = append(labels, l)
				}
			}
			anyDone = anyDone || card.Done
		}
	}
	if len(labels) > 0 || anyDone {
		x := e.px(boardPad)
		fl := e.label("Filter", small)
		fw, _ := fl.Size()
		x += fw + e.px(8)
		for _, l := range labels {
			w, _ := e.label("#"+l, small).Size()
			bl.filters = append(bl.filters, chip{words: "#" + l, label: l, r: geom.NewRect(x, y, w+e.px(16), e.px(22))})
			x += w + e.px(22)
		}
		if anyDone {
			w, _ := e.label("Hide done", small).Size()
			bl.filters = append(bl.filters, chip{words: "Hide done", r: geom.NewRect(x, y, w+e.px(16), e.px(22))})
		}
		y += e.px(boardFilterH + 4)
	}
	n := len(bl.board.Columns)
	avail := width - 2*e.px(boardPad) - e.px(boardAddColW) - float32(n)*e.px(boardColGap)
	colW := e.px(boardColW)
	if n > 0 {
		colW = max(e.px(boardColMin), min(colW, avail/float32(n)))
	}
	top := y
	x := e.px(boardPad)
	tallest := float32(0)
	for ci, c := range bl.board.Columns {
		cb := colBox{total: len(c.Cards)}
		cy := top + e.px(boardHeaderH)
		ctl := e.px(boardCtl)
		right := x + colW - e.px(4)
		cb.close = geom.NewRect(right-ctl, top, ctl, e.px(boardHeaderH))
		cb.add = geom.NewRect(right-2*ctl, top, ctl, e.px(boardHeaderH))
		cb.right = geom.NewRect(right-3*ctl, top, ctl, e.px(boardHeaderH))
		cb.left = geom.NewRect(right-4*ctl, top, ctl, e.px(boardHeaderH))
		cb.fold = geom.NewRect(x, top, e.px(22), e.px(boardHeaderH))
		cb.name = geom.NewRect(x+e.px(22), top, max(1, cb.left.X-e.px(30)-(x+e.px(22))), e.px(boardHeaderH))
		if !v.folded[c.Name] {
			for k, card := range c.Cards {
				if (v.label != "" && !slices.Contains(card.Labels, v.label)) || (v.hideDone && card.Done) {
					continue
				}
				cb.shown++
				inner := colW - 2*e.px(boardPad) - 2*e.px(boardCardPad)
				titleSt := e.chrome(kvitui.RoleBody, text.Regular, t.TextPrimary)
				if card.Done {
					titleSt.Color, titleSt.Strike = colour(t.TextFaint), true
				}
				box := cardBox{col: ci, index: k}
				words := card.Title
				if words == "" {
					words = " "
				}
				box.title = e.ui.Fonts.Layout([]text.Span{{Text: words, Style: titleSt}},
					text.Options{MaxWidth: max(1, inner-e.px(boardBox+6))})
				_, th := box.title.Size()
				h := e.px(boardCardPad) + max(th, e.px(boardBox)) + e.px(4)
				cx := x + e.px(boardPad) + e.px(boardCardPad)
				chipX := cx
				chipY := cy + h
				for _, l := range card.Labels {
					w, _ := e.label("#"+l, small).Size()
					if chipX+w+e.px(10) > cx+inner && chipX > cx {
						chipX = cx
						chipY += e.px(boardChipH + 3)
					}
					box.chips = append(box.chips, chip{words: "#" + l, label: l, r: geom.NewRect(chipX, chipY, w+e.px(10), e.px(boardChipH))})
					chipX += w + e.px(14)
				}
				if card.Due != "" {
					words := "◷ " + card.Due
					w, _ := e.label(words, small).Size()
					if chipX+w+e.px(10) > cx+inner && chipX > cx {
						chipX = cx
						chipY += e.px(boardChipH + 3)
					}
					box.chips = append(box.chips, chip{words: words, due: true, r: geom.NewRect(chipX, chipY, w+e.px(10), e.px(boardChipH))})
				}
				if len(box.chips) > 0 {
					h = chipY - cy + e.px(boardChipH+4)
				}
				if card.Description != "" {
					// Drawn as a prose block draws its text: markers hidden,
					// math typeset (features.md 1.2.12).
					box.desc, box.descProj = e.inlineText(card.Description, small, inner)
					_, dh := box.desc.Size()
					h += dh + e.px(4)
				}
				// The days the card was added and last changed, at its foot.
				box.foot = cardFoot(card)
				if box.foot != "" {
					h += e.px(boardFootH)
				}
				h += e.px(boardCardPad) - e.px(4)
				box.r = geom.NewRect(x+e.px(boardPad), cy, colW-2*e.px(boardPad), h)
				box.box = geom.NewRect(cx, cy+e.px(boardCardPad)+2, e.px(boardBox), e.px(boardBox))
				bl.cards = append(bl.cards, box)
				cy += h + e.px(boardCardGap)
			}
			cb.addCard = geom.NewRect(x+e.px(boardPad), cy, colW-2*e.px(boardPad), e.px(boardAddCardH))
			cy += e.px(boardAddCardH) + e.px(boardPad)
		} else {
			cy += e.px(4)
		}
		cb.r = geom.NewRect(x, top, colW, cy-top)
		tallest = max(tallest, cb.r.Height)
		bl.cols = append(bl.cols, cb)
		x += colW + e.px(boardColGap)
	}
	bl.addCol = geom.NewRect(x, top, e.px(boardAddColW), e.px(40))
	tallest = max(tallest, bl.addCol.Height)
	bl.height = top + tallest + e.px(boardPad)
	return bl
}

// board is a board block's layout, from a cache.
func (e *Editor) board(i int) *boardLayout {
	b := &e.Doc.Blocks[i]
	width := e.bodyRight() - e.bodyLeft()
	v := e.viewOf(b.ID)
	key := fmt.Sprintf("%s|%v|%d|%v|%s|%v", b.Text, width, e.generation, v.hideDone, v.label, v.folded)
	if c, ok := e.boards[b.ID]; ok && c.key == key {
		return c.layout
	}
	if e.boards == nil {
		e.boards = map[int64]cachedBoard{}
	}
	bl := e.layBoard(b, width)
	e.boards[b.ID] = cachedBoard{key, bl}
	return bl
}

type cachedBoard struct {
	key    string
	layout *boardLayout
}

// boardOrigin is where a board's layout starts in the editor.
func (e *Editor) boardOrigin(i int) geom.Point {
	body := e.bodyRect(i)
	return geom.NewPoint(body.X, e.tops[i]+e.px(codeRowTop))
}

// boardHeight is a board's row height.
func (e *Editor) boardHeight(i int) float32 {
	return e.px(codeRowTop+codeRowBottom) + e.board(i).height
}

func (e *Editor) drawBoard(gc *unison.Canvas, i int) {
	b := &e.Doc.Blocks[i]
	bl := e.board(i)
	v := e.viewOf(b.ID)
	t := e.tok()
	o := e.boardOrigin(i)
	gc.Save()
	gc.Translate(o)
	defer gc.Restore()
	small := e.chrome(kvitui.RoleSmall, text.Regular, t.TextSecondary)
	faint := e.chrome(kvitui.RoleSmall, text.Regular, t.TextFaint)
	if len(bl.filters) > 0 {
		fl := e.label("Filter", faint)
		_, fh := fl.Size()
		fl.Draw(gc, e.px(boardPad), bl.filters[0].r.Y+(bl.filters[0].r.Height-fh)/2)
		for _, c := range bl.filters {
			on := (c.label != "" && v.label == c.label) || (c.label == "" && v.hideDone)
			st := small
			edge := t.Border
			if c.label != "" {
				st.Color = labelColor(c.label)
			}
			if on {
				e.fillRound(gc, c.r, c.r.Height/2, t.SelectionTint)
				edge = t.Accent
			}
			e.stroke(gc, c.r, c.r.Height/2, e.px(1), edge)
			l := e.label(c.words, st)
			w, h := l.Size()
			l.Draw(gc, c.r.X+(c.r.Width-w)/2, c.r.Y+(c.r.Height-h)/2)
		}
	}
	for ci, cb := range bl.cols {
		col := bl.board.Columns[ci]
		r := e.px(boardRadius)
		gc.DrawRoundedRect(cb.r, geom.NewSize(r, r), kvitui.Color(t.PanelBackground).Paint(gc, cb.r, paintstyle.Fill))
		e.stroke(gc, cb.r, r, e.px(1), t.Border)
		arrow := "▾"
		if v.folded[col.Name] {
			arrow = "▸"
		}
		center := func(s string, st text.Style, in geom.Rect) {
			l := e.label(s, st)
			w, h := l.Size()
			l.Draw(gc, in.X+(in.Width-w)/2, in.Y+(in.Height-h)/2)
		}
		center(arrow, small, cb.fold)
		nl := e.ui.Fonts.Layout([]text.Span{{Text: col.Name, Style: e.chrome(kvitui.RoleStrong, text.Bold, t.TextPrimary)}},
			text.Options{MaxWidth: cb.name.Width, Elide: true})
		_, nh := nl.Size()
		nl.Draw(gc, cb.name.X, cb.name.Y+(cb.name.Height-nh)/2)
		count := fmt.Sprint(cb.total)
		if cb.shown != cb.total && !v.folded[col.Name] {
			count = fmt.Sprintf("%d of %d", cb.shown, cb.total)
		}
		cl := e.label(count, faint)
		cw, ch := cl.Size()
		cl.Draw(gc, cb.left.X-e.px(4)-cw, cb.left.Y+(cb.left.Height-ch)/2)
		leftInk, rightInk := small, small
		if ci == 0 {
			leftInk = faint
			leftInk.Color.A = 90
		}
		if ci == len(bl.cols)-1 {
			rightInk = faint
			rightInk.Color.A = 90
		}
		center("‹", leftInk, cb.left)
		center("›", rightInk, cb.right)
		center("+", small, cb.add)
		center("×", small, cb.close)
		if !v.folded[col.Name] {
			center("+ Add card", faint, cb.addCard)
		}
	}
	for _, c := range bl.cards {
		card := bl.board.Columns[c.col].Cards[c.index]
		r := e.px(4)
		gc.DrawRoundedRect(c.r, geom.NewSize(r, r), kvitui.Color(t.WindowBackground).Paint(gc, c.r, paintstyle.Fill))
		e.stroke(gc, c.r, r, e.px(1), t.Border)
		if card.Done {
			e.fillRound(gc, c.box, e.px(checkBoxRadius), t.Accent)
			l := e.label("✓", e.chrome(kvitui.RoleSmall, text.Bold, t.OnAccent))
			w, h := l.Size()
			l.Draw(gc, c.box.X+(c.box.Width-w)/2, c.box.Y+(c.box.Height-h)/2)
		} else {
			e.stroke(gc, c.box, e.px(checkBoxRadius), e.px(1.5), t.BorderStrong)
		}
		c.title.Draw(gc, c.box.Right()+e.px(6), c.r.Y+e.px(boardCardPad))
		for _, ch := range c.chips {
			ink := labelColor(ch.label)
			if ch.due {
				ink = colour(t.TextSecondary)
			}
			e.fillRound(gc, ch.r, e.px(4), t.ChipBackground)
			st := small
			st.Color = ink
			l := e.label(ch.words, st)
			w, h := l.Size()
			l.Draw(gc, ch.r.X+(ch.r.Width-w)/2, ch.r.Y+(ch.r.Height-h)/2)
		}
		if c.desc != nil {
			_, dh := c.desc.Size()
			dy := c.r.Bottom() - e.px(boardCardPad) - dh
			if c.foot != "" {
				dy -= e.px(boardFootH)
			}
			c.desc.Draw(gc, c.r.X+e.px(boardCardPad), dy)
			drawInlineMath(gc, c.desc, c.descProj, c.r.X+e.px(boardCardPad), dy)
		}
		if c.foot != "" {
			l := e.label(c.foot, e.chrome(kvitui.RoleSmall, text.Regular, t.TextFaint))
			_, fh := l.Size()
			l.Draw(gc, c.r.X+e.px(boardCardPad), c.r.Bottom()-e.px(boardCardPad)-fh)
		}
	}
	if e.cardDrag != nil && e.cardDrag.block == b.ID {
		e.drawCardDrop(gc, bl)
	}
	e.fillRound(gc, bl.addCol, e.px(boardRadius), t.PanelBackground)
	e.stroke(gc, bl.addCol, e.px(boardRadius), e.px(1), t.Border)
	l := e.label("+ Column", small)
	w, h := l.Size()
	l.Draw(gc, bl.addCol.X+(bl.addCol.Width-w)/2, bl.addCol.Y+(bl.addCol.Height-h)/2)
}

// boardPress acts on a press on a board, and reports whether it was on one
// of its controls.
func (e *Editor) boardPress(i int, where geom.Point, right bool) bool {
	b := &e.Doc.Blocks[i]
	bl := e.board(i)
	v := e.viewOf(b.ID)
	p := where.Sub(e.boardOrigin(i))
	id := b.ID
	today := time.Now().Format(time.DateOnly)
	apply := func(content string) {
		if blk := e.Doc.Block(id); blk != nil && blk.Text != content && !e.Doc.ReadOnly {
			e.Doc.Edit("board", func() { blk.Text = content })
		}
	}
	for _, c := range bl.filters {
		if p.In(c.r) {
			if c.label == "" {
				v.hideDone = !v.hideDone
			} else if v.label == c.label {
				v.label = ""
			} else {
				v.label = c.label
			}
			return true
		}
	}
	for _, c := range bl.cards {
		if !p.In(c.r) {
			continue
		}
		if right {
			e.ui.ShowMenuAt(e, geom.NewRect(where.X, where.Y, 0, 0), "Card", e.cardItems(id, c.col, c.index))
			return true
		}
		if p.In(c.box.Inset(geom.NewUniformInsets(-e.px(3)))) {
			apply(kanban.ToggleCardDone(b.Text, c.col, c.index, today))
			return true
		}
		// A press on a card edits it when let go where it was, and moves it
		// when dragged (boardDrag).
		e.cardDrag = &cardDrag{block: id, col: c.col, index: c.index, start: where, toCol: -1}
		return true
	}
	for ci, cb := range bl.cols {
		if !p.In(cb.r) {
			continue
		}
		name := bl.board.Columns[ci].Name
		switch {
		case p.In(cb.fold):
			v.folded[name] = !v.folded[name]
		case p.In(cb.close):
			apply(kanban.RemoveColumn(b.Text, ci))
		case p.In(cb.add), !v.folded[name] && p.In(cb.addCard):
			apply(kanban.AddCard(b.Text, ci, "", today))
			e.changed()
			if blk := e.Doc.Block(id); blk != nil {
				nb := kanban.Parse(blk.Text)
				e.editCard(e.Doc.Index(id), ci, len(nb.Columns[ci].Cards)-1)
			}
		case p.In(cb.left) && ci > 0:
			apply(kanban.MoveColumn(b.Text, ci, ci-1))
		case p.In(cb.right) && ci < len(bl.cols)-1:
			apply(kanban.MoveColumn(b.Text, ci, ci+1))
		case p.In(cb.name):
			e.editBoardText(i, cb.name, "Column name", name, func(text string) string {
				if text == "" {
					return ""
				}
				return kanban.RenameColumn(e.Doc.Block(id).Text, ci, text)
			})
		}
		return true
	}
	if p.In(bl.addCol) {
		apply(kanban.AddColumn(b.Text, "New column"))
		e.changed()
		if nb := e.board(e.Doc.Index(id)); len(nb.cols) > 0 {
			last := len(nb.cols) - 1
			e.editBoardText(e.Doc.Index(id), nb.cols[last].name, "Column name", "New column", func(text string) string {
				if text == "" {
					return ""
				}
				return kanban.RenameColumn(e.Doc.Block(id).Text, last, text)
			})
		}
		return true
	}
	return false
}

// cardItems are a card's menu: edit it, move it to another column, delete
// it.
func (e *Editor) cardItems(id int64, col, index int) []kvitui.MenuItem {
	b := e.Doc.Block(id)
	if b == nil {
		return nil
	}
	board := kanban.Parse(b.Text)
	today := time.Now().Format(time.DateOnly)
	apply := func(content string) {
		if blk := e.Doc.Block(id); blk != nil && blk.Text != content && !e.Doc.ReadOnly {
			e.Doc.Edit("board", func() { blk.Text = content })
			e.changed()
		}
	}
	var moves []kvitui.MenuItem
	for ci, c := range board.Columns {
		if ci == col {
			continue
		}
		moves = append(moves, kvitui.MenuItem{Text: kvitui.PlainMenuText(c.Name), OnSelect: func() {
			apply(kanban.MoveCard(e.Doc.Block(id).Text, col, index, ci, len(board.Columns[ci].Cards), today))
		}})
	}
	return []kvitui.MenuItem{
		{Text: "&Edit card", OnSelect: func() { e.editCard(e.Doc.Index(id), col, index) }},
		{Text: "&Move to column", Items: moves, Disabled: len(moves) == 0},
		{Separator: true},
		{Text: "&Delete card", Danger: true, OnSelect: func() { apply(kanban.RemoveCard(e.Doc.Block(id).Text, col, index)) }},
	}
}

// editCard edits a card's line in place (cardedit.go).
func (e *Editor) editCard(i, col, index int) { e.editCardField(i, col, index, cardLine) }

// editBoardText opens a field over part of a board, and on Return writes
// what change makes of the text typed; Escape leaves the board as it was.
func (e *Editor) editBoardText(i int, part geom.Rect, name, current string, change func(text string) string) {
	w := e.ui.WindowOf(e)
	if w == nil || e.Doc.ReadOnly || i < 0 {
		return
	}
	id := e.Doc.Blocks[i].ID
	field := kvitui.NewField(e.ui)
	field.Label = name
	field.SetText(current)
	var hide func()
	done := func(keep bool) {
		if hide == nil {
			return
		}
		hide()
		hide = nil
		if keep {
			if content := change(strings.TrimSpace(field.Text())); content != "" {
				if blk := e.Doc.Block(id); blk != nil && blk.Text != content {
					e.Doc.Edit("board", func() { blk.Text = content })
				}
			}
		}
		e.RequestFocus()
		e.changed()
	}
	edit := field.Edit()
	keys := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		if key == unison.KeyReturn || key == unison.KeyNumPadEnter {
			done(true)
			return true
		}
		return keys != nil && keys(key, mods, repeat)
	}
	o := e.boardOrigin(i)
	at := e.RectToRoot(geom.NewRect(part.X+o.X, part.Y+o.Y, part.Width, part.Height))
	hide = w.Show(&kvitui.Popup{Panel: field, Anchor: e,
		OnEscape:       func() { done(false) },
		OnPressOutside: func() { done(true) },
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			r := w.Content().RectFromRoot(at)
			return geom.NewRect(r.X, r.Y, max(r.Width, e.px(180)), size.Height)
		}})
	unison.InvokeTask(func() { field.Focus(); field.Edit().SelectAll() })
}

// BoardText is a task board's cards as text, for tests: each column's name
// and its cards shown, "[x]" for a finished one.
func (e *Editor) BoardText(i int) string {
	bl := e.board(i)
	var b strings.Builder
	shown := map[[2]int]bool{}
	for _, c := range bl.cards {
		shown[[2]int{c.col, c.index}] = true
	}
	for ci, c := range bl.board.Columns {
		fmt.Fprintf(&b, "%s:", c.Name)
		for k, card := range c.Cards {
			if !shown[[2]int{ci, k}] {
				continue
			}
			box := "[ ]"
			if card.Done {
				box = "[x]"
			}
			fmt.Fprintf(&b, " %s %s;", box, card.Title)
		}
		b.WriteString("\n")
	}
	return b.String()
}

// BoardPart is where a part of a task board is, in the editor: "column N"
// parts ("fold", "name", "left", "right", "add", "close", "addcard"), a
// card's "box" or "card", "addcolumn", or a filter chip by its words.
func (e *Editor) BoardPart(i int, part string, col, index int) geom.Rect {
	bl := e.board(i)
	o := e.boardOrigin(i)
	shift := func(r geom.Rect) geom.Rect { return geom.NewRect(r.X+o.X, r.Y+o.Y, r.Width, r.Height) }
	switch part {
	case "addcolumn":
		return shift(bl.addCol)
	case "box", "card":
		for _, c := range bl.cards {
			if c.col == col && c.index == index {
				if part == "box" {
					return shift(c.box)
				}
				return shift(c.r)
			}
		}
	case "fold", "name", "left", "right", "add", "close", "addcard":
		if col < len(bl.cols) {
			cb := bl.cols[col]
			return shift(map[string]geom.Rect{"fold": cb.fold, "name": cb.name, "left": cb.left, "right": cb.right,
				"add": cb.add, "close": cb.close, "addcard": cb.addCard}[part])
		}
	default:
		for _, c := range bl.filters {
			if c.words == part {
				return shift(c.r)
			}
		}
	}
	return geom.Rect{}
}

// cardDrag is a card being pressed or dragged on a board.
type cardDrag struct {
	block        int64
	col, index   int
	start        geom.Point
	active       bool
	toCol, toIdx int // where it would go: a column and a place in it
}

// dragCard follows the pointer while a card is dragged: the column under it
// and the gap between that column's cards nearest to it.
func (e *Editor) dragCard(where geom.Point) {
	cd := e.cardDrag
	dv := where.Sub(cd.start)
	if !cd.active && dv.X*dv.X+dv.Y*dv.Y > e.px(dragThreshold)*e.px(dragThreshold) {
		cd.active = true
	}
	if !cd.active {
		return
	}
	i := e.Doc.Index(cd.block)
	if i < 0 {
		return
	}
	bl := e.board(i)
	p := where.Sub(e.boardOrigin(i))
	cd.toCol = -1
	for ci, cb := range bl.cols {
		if p.X < cb.r.X || p.X >= cb.r.Right()+e.px(boardColGap) {
			continue
		}
		cd.toCol, cd.toIdx = ci, 0
		for _, c := range bl.cards {
			if c.col == ci && p.Y > c.r.Y+c.r.Height/2 {
				cd.toIdx = c.index + 1
			}
		}
	}
}

// dropCard ends a press on a card: an edit when it did not move, a move to
// where it was dragged when it did, as one undo step.
func (e *Editor) dropCard() {
	cd := e.cardDrag
	e.cardDrag = nil
	i := e.Doc.Index(cd.block)
	if i < 0 {
		return
	}
	if !cd.active {
		e.editCardField(i, cd.col, cd.index, e.cardFieldAt(i, cd.col, cd.index, cd.start))
		return
	}
	if cd.toCol < 0 {
		return
	}
	to := cd.toIdx
	if cd.toCol == cd.col && to > cd.index {
		to--
	}
	if cd.toCol == cd.col && to == cd.index {
		return
	}
	b := &e.Doc.Blocks[i]
	content := kanban.MoveCard(b.Text, cd.col, cd.index, cd.toCol, to, time.Now().Format(time.DateOnly))
	if content != b.Text && !e.Doc.ReadOnly {
		e.Doc.Edit("board", func() { b.Text = content })
	}
}

// drawCardDrop draws where a dragged card would go.
func (e *Editor) drawCardDrop(gc *unison.Canvas, bl *boardLayout) {
	cd := e.cardDrag
	if cd == nil || !cd.active || cd.toCol < 0 || cd.toCol >= len(bl.cols) {
		return
	}
	cb := bl.cols[cd.toCol]
	y := cb.r.Y + e.px(boardHeaderH)
	for _, c := range bl.cards {
		if c.col == cd.toCol && c.index < cd.toIdx {
			y = c.r.Bottom() + e.px(boardCardGap)/2
		}
	}
	e.fill(gc, geom.NewRect(cb.r.X+e.px(boardPad), y-e.px(1), cb.r.Width-2*e.px(boardPad), e.px(2)), e.tok().Accent)
}
