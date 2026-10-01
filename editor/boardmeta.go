package editor

// The rest of a task board: dragging columns with a gap indicator, the
// dragged card drawn under the pointer, label chips that remove themselves
// with a chip adding one (the board's labels offered for reuse), a date chip
// opening a calendar for the due date, and the card-details dialog for the
// structured fields.
//
// Every write goes through the kanban package as one undo step, as the
// board's other gestures do.

import (
	"slices"
	"strconv"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/kanban"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// colDragState is a press on a column's header: a click renames the column,
// moving the pointer further than dragThreshold drags it to the gap nearest
// the pointer.
type colDragState struct {
	block  int64
	from   int
	start  geom.Point
	active bool
	slot   int // the insert-before gap, -1 for none
}

// columnSlotAt is the gap a dragged column would drop into at where: the
// gap before the first column whose middle the pointer has not reached, or
// past the last column.
func (e *Editor) columnSlotAt(i int, where geom.Point) int {
	bl := e.board(i)
	p := where.Sub(e.boardOrigin(i))
	for ci, cb := range bl.cols {
		if p.X < cb.r.X+cb.r.Width/2 {
			return ci
		}
	}
	return len(bl.cols)
}

// columnDropIndex turns an insert-before slot into the index MoveColumn
// takes, which counts the columns with the dragged one taken out: every
// column the dragged one passed on the way right shifts down by one.
func columnDropIndex(from, slot int) int {
	if slot > from {
		return slot - 1
	}
	return slot
}

// dragColumn follows the pointer while a column header is dragged.
func (e *Editor) dragColumn(where geom.Point) {
	cd := e.colDrag
	dv := where.Sub(cd.start)
	if !cd.active && dv.X*dv.X+dv.Y*dv.Y > e.px(dragThreshold)*e.px(dragThreshold) {
		cd.active = true
	}
	if !cd.active {
		return
	}
	i := e.Doc.Index(cd.block)
	if i < 0 {
		cd.slot = -1
		return
	}
	cd.slot = e.columnSlotAt(i, where)
}

// dropColumn ends a press on a column header: a click renames the column,
// a drag moves it to the gap it was dropped into, each as one undo step.
func (e *Editor) dropColumn() {
	cd := e.colDrag
	e.colDrag = nil
	i := e.Doc.Index(cd.block)
	if i < 0 {
		return
	}
	if !cd.active {
		e.openColumnRename(i, cd.from)
		return
	}
	if cd.slot < 0 {
		return
	}
	to := columnDropIndex(cd.from, cd.slot)
	if to == cd.from {
		return
	}
	b := &e.Doc.Blocks[i]
	if content := kanban.MoveColumn(b.Text, cd.from, to); content != b.Text && !e.Doc.ReadOnly {
		e.Doc.Edit("board", func() { b.Text = content })
	}
}

// openColumnRename opens the rename field over a column's header.
func (e *Editor) openColumnRename(i, ci int) {
	b := &e.Doc.Blocks[i]
	bl := e.board(i)
	if ci < 0 || ci >= len(bl.cols) {
		return
	}
	name := bl.board.Columns[ci].Name
	cb := bl.cols[ci]
	e.editBoardText(i, cb.name, "Column name", name, func(text string) string {
		if text == "" {
			return ""
		}
		return kanban.RenameColumn(e.Doc.Block(b.ID).Text, ci, text)
	})
}

// drawColumnGap draws where a dragged column would go: the accent line in
// the gap nearest the pointer.
func (e *Editor) drawColumnGap(gc *unison.Canvas, bl *boardLayout) {
	cd := e.colDrag
	if cd == nil || !cd.active || cd.slot < 0 || len(bl.cols) == 0 {
		return
	}
	gap := e.px(boardColGap)
	var x float32
	if cd.slot >= len(bl.cols) {
		last := bl.cols[len(bl.cols)-1]
		x = last.r.Right() + gap/2
	} else {
		x = bl.cols[cd.slot].r.X - gap/2
	}
	top := bl.cols[0].r.Y
	bottom := top
	for _, cb := range bl.cols {
		bottom = max(bottom, cb.r.Bottom())
	}
	e.fill(gc, geom.NewRect(x-e.px(1), top, e.px(2), bottom-top), e.tok().Accent)
}

// CardGhost reports the dragged card drawn under the pointer, for tests:
// its block, the pointer in editor coordinates, and the press offset inside
// the card in board coordinates.
func (e *Editor) CardGhost() (block int64, at, hot geom.Point, ok bool) {
	if e.cardDrag == nil || !e.cardDrag.active {
		return 0, geom.Point{}, geom.Point{}, false
	}
	return e.cardDrag.block, e.cardDrag.at, e.cardDrag.hot, true
}

// drawCardGhost draws the dragged card under the pointer: its panel and
// title at the press offset, over the drop line.
func (e *Editor) drawCardGhost(gc *unison.Canvas, i int, bl *boardLayout) {
	cd := e.cardDrag
	if cd == nil || !cd.active || cd.block != e.Doc.Blocks[i].ID {
		return
	}
	var box *cardBox
	for k := range bl.cards {
		if bl.cards[k].col == cd.col && bl.cards[k].index == cd.index {
			box = &bl.cards[k]
			break
		}
	}
	if box == nil {
		return
	}
	t := e.tok()
	p := cd.at.Sub(e.boardOrigin(i)).Sub(cd.hot)
	r := geom.NewRect(p.X, p.Y, box.r.Width, box.r.Height)
	gc.SaveWithOpacity(0.9)
	gc.DrawRoundedRect(r, geom.NewSize(e.px(4), e.px(4)), kvitui.Color(t.WindowBackground).Paint(gc, r, paintstyle.Fill))
	e.stroke(gc, r, e.px(4), e.px(1.5), t.Accent)
	box.title.Draw(gc, r.X+e.px(boardCardPad)+e.px(boardBox+6), r.Y+e.px(boardCardPad))
	gc.Restore()
}

// boardCardAt is the card under a point in row i, in the editor's
// coordinates.
func (e *Editor) boardCardAt(i int, where geom.Point) (int, int, bool) {
	bl := e.board(i)
	p := where.Sub(e.boardOrigin(i))
	for _, c := range bl.cards {
		if p.In(c.r) {
			return c.col, c.index, true
		}
	}
	return 0, 0, false
}

// updateBoardHover tracks the card under the pointer, whose add-tag and due
// affordances the layout shows.
func (e *Editor) updateBoardHover(where geom.Point) {
	id, col, idx := int64(0), -1, -1
	if i := e.rowAt(where); i >= 0 && e.boardShows(i) {
		if c, k, ok := e.boardCardAt(i, where); ok {
			id, col, idx = e.Doc.Blocks[i].ID, c, k
		}
	}
	if id != e.boardHoverID || col != e.boardHoverCol || idx != e.boardHoverIdx {
		e.boardHoverID, e.boardHoverCol, e.boardHoverIdx = id, col, idx
		e.MarkForRedraw()
	}
}

// rewriteCard applies change to one card's fields through SetCard, as one
// undo step. change sees the card as parsed; labels and due are what gets
// written, the title, state and description staying as they are.
func (e *Editor) rewriteCard(id int64, col, index int, change func(card *kanban.Card)) {
	b := e.Doc.Block(id)
	if b == nil || e.Doc.ReadOnly {
		return
	}
	board := kanban.Parse(b.Text)
	if col < 0 || col >= len(board.Columns) || index < 0 || index >= len(board.Columns[col].Cards) {
		return
	}
	card := board.Columns[col].Cards[index]
	change(&card)
	content := kanban.SetCard(b.Text, col, index, card.Title, card.Done, card.Labels, card.Due, card.Description,
		time.Now().Format(time.DateOnly))
	if content != b.Text {
		e.Doc.Edit("board", func() { b.Text = content })
		e.changed()
	}
}

// AddCardLabel adds a label to a card, as the chip row's + tag does. A
// leading hash comes off (the stored form does not carry it); empty and
// duplicate labels are ignored.
func (e *Editor) AddCardLabel(id int64, col, index int, name string) {
	clean := strings.TrimSpace(strings.TrimLeft(strings.TrimSpace(name), "#"))
	clean = strings.TrimSpace(strings.TrimLeft(clean, "#"))
	if clean == "" {
		return
	}
	e.rewriteCard(id, col, index, func(card *kanban.Card) {
		for _, l := range card.Labels {
			if l == clean {
				return
			}
		}
		card.Labels = append(card.Labels, clean)
	})
}

// RemoveCardLabel removes a label from a card, as its chip does.
func (e *Editor) RemoveCardLabel(id int64, col, index int, name string) {
	e.rewriteCard(id, col, index, func(card *kanban.Card) {
		kept := card.Labels[:0]
		for _, l := range card.Labels {
			if l != name {
				kept = append(kept, l)
			}
		}
		card.Labels = kept
	})
}

// SetCardDue sets a card's due date, as its date chip and the picker do. An
// invalid value clears it: the stored form only holds ISO days.
func (e *Editor) SetCardDue(id int64, col, index int, day string) {
	day = strings.TrimSpace(day)
	if day != "" && !kanban.IsValidDue(day) {
		day = ""
	}
	e.rewriteCard(id, col, index, func(card *kanban.Card) { card.Due = day })
}

// BoardLabels are the labels used anywhere on a board, in first-use order.
func (e *Editor) BoardLabels(id int64) []string {
	b := e.Doc.Block(id)
	if b == nil {
		return nil
	}
	var out []string
	for _, c := range kanban.Parse(b.Text).Columns {
		for _, card := range c.Cards {
			for _, l := range card.Labels {
				if !slices.Contains(out, l) {
					out = append(out, l)
				}
			}
		}
	}
	return out
}

// TagChoices are the board's labels a card does not already carry, narrowed
// by what has been typed (case-insensitive, a leading hash ignored): the
// list under the chip row's field, there to reuse a label rather than
// retype it.
func (e *Editor) TagChoices(id int64, col, index int, query string) []string {
	b := e.Doc.Block(id)
	if b == nil {
		return nil
	}
	board := kanban.Parse(b.Text)
	if col < 0 || col >= len(board.Columns) || index < 0 || index >= len(board.Columns[col].Cards) {
		return nil
	}
	card := board.Columns[col].Cards[index]
	q := strings.ToLower(strings.TrimLeft(strings.TrimSpace(query), "#"))
	var out []string
	for _, l := range e.BoardLabels(id) {
		if slices.Contains(card.Labels, l) {
			continue
		}
		if q != "" && !strings.Contains(strings.ToLower(l), q) {
			continue
		}
		out = append(out, l)
	}
	return out
}

// cardChip is which chip of a card a press landed on.
type cardChip struct {
	kind  string // "label", "due", "addtag", "adddue"
	label string // the label, for "label"
}

// cardChipAt reports the chip of card c under p, in board coordinates:
// one of its label or due chips, or the hovered card's + tag and due
// affordances.
func (e *Editor) cardChipAt(c cardBox, p geom.Point) (cardChip, bool) {
	for _, ch := range c.chips {
		if !p.In(ch.r) {
			continue
		}
		if ch.due {
			return cardChip{kind: "due"}, true
		}
		if ch.label != "" {
			return cardChip{kind: "label", label: ch.label}, true
		}
	}
	if c.hasAdd {
		if p.In(c.addTag) {
			return cardChip{kind: "addtag"}, true
		}
		if p.In(c.addDue) {
			return cardChip{kind: "adddue"}, true
		}
	}
	return cardChip{}, false
}

// actOnCardChip runs what a chip press does: a label chip removes itself,
// the due chip and + due open the calendar, + tag opens the label field.
func (e *Editor) actOnCardChip(i int, c cardBox, hit cardChip) {
	id := e.Doc.Blocks[i].ID
	switch hit.kind {
	case "label":
		e.RemoveCardLabel(id, c.col, c.index, hit.label)
	case "due", "adddue":
		e.OpenDuePicker(i, c.col, c.index)
	case "addtag":
		e.BeginTagEdit(i, c.col, c.index)
	}
}

// BoardChipRects are a card's label and due chips, in the editor, for
// tests: each chip's words and rectangle.
func (e *Editor) BoardChipRects(i, col, index int) (words []string, rects []geom.Rect) {
	o := e.boardOrigin(i)
	for _, c := range e.board(i).cards {
		if c.col != col || c.index != index {
			continue
		}
		for _, ch := range c.chips {
			words = append(words, ch.words)
			rects = append(rects, geom.NewRect(ch.r.X+o.X, ch.r.Y+o.Y, ch.r.Width, ch.r.Height))
		}
		return words, rects
	}
	return nil, nil
}

// tagEditState is a card's chip row taking a label: the field with what has
// been typed, and which of the board's existing labels that highlights (-1
// for none, so Enter takes the typed text).
type tagEditState struct {
	block        int64
	col, index   int
	query        string
	highlight    int
	hide         func()
	field        *kvitui.Field
	choicesPanel *unison.Panel
}

// TagEditOpen reports the tag field's card, query and choices, for tests.
func (e *Editor) TagEditOpen() (col, index int, query string, choices []string, ok bool) {
	t := e.tagEdit
	if t == nil {
		return 0, 0, "", nil, false
	}
	return t.col, t.index, t.query, e.TagChoices(t.block, t.col, t.index, t.query), true
}

// BeginTagEdit opens the label field in a card's chip row.
func (e *Editor) BeginTagEdit(i, col, index int) {
	w := e.ui.WindowOf(e)
	if w == nil || e.Doc.ReadOnly || i < 0 {
		return
	}
	id := e.Doc.Blocks[i].ID
	e.closeTagEdit()
	t := &tagEditState{block: id, col: col, index: index, highlight: -1}
	e.tagEdit = t
	field := kvitui.NewField(e.ui)
	field.Label = "Label"
	field.Placeholder = "#tag"
	t.field = field
	list := unison.NewPanel()
	list.SetLayout(&unison.FlexLayout{Columns: 1})
	t.choicesPanel = list
	e.refreshTagChoices()
	field.OnChange = func(text string) {
		if e.tagEdit != t {
			return
		}
		t.query = text
		t.highlight = -1
		e.refreshTagChoices()
	}
	edit := field.Edit()
	keys := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		switch key {
		case unison.KeyReturn, unison.KeyNumPadEnter:
			e.AcceptTag()
			return true
		case unison.KeyUp:
			e.MoveTagHighlight(-1)
			return true
		case unison.KeyDown:
			e.MoveTagHighlight(1)
			return true
		}
		return keys != nil && keys(key, mods, repeat)
	}
	body := kvitui.Column(e.ui, kvitui.Px(4), field, list)
	// Anchor to the card's chip row.
	var at geom.Rect
	o := e.boardOrigin(i)
	for _, c := range e.board(i).cards {
		if c.col == col && c.index == index && c.hasAdd {
			at = e.RectToRoot(geom.NewRect(c.addTag.X+o.X, c.addTag.Y+o.Y, e.px(110), c.addTag.Height))
			break
		}
	}
	if at.Width == 0 {
		for _, c := range e.board(i).cards {
			if c.col == col && c.index == index {
				at = e.RectToRoot(geom.NewRect(c.r.X+o.X, c.r.Bottom()+o.Y, c.r.Width, e.px(18)))
				break
			}
		}
	}
	t.hide = w.Show(&kvitui.Popup{Panel: body, Anchor: e,
		OnEscape:       func() { e.closeTagEdit() },
		OnPressOutside: func() { e.closeTagEdit() },
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			r := w.Content().RectFromRoot(at)
			return geom.NewRect(r.X, r.Y, max(r.Width, e.px(150)), size.Height)
		}})
	unison.InvokeTask(func() { field.Focus() })
}

// refreshTagChoices rebuilds the offered labels under the tag field.
func (e *Editor) refreshTagChoices() {
	t := e.tagEdit
	if t == nil || t.choicesPanel == nil {
		return
	}
	p := t.choicesPanel
	for _, ch := range p.Children() {
		p.RemoveChild(ch)
	}
	for k, choice := range e.TagChoices(t.block, t.col, t.index, t.query) {
		if k >= 6 {
			break
		}
		b := kvitui.NewButton(e.ui, "#"+choice)
		b.Form = kvitui.ButtonQuiet
		if k == t.highlight {
			b.Form = kvitui.ButtonPrimary
		}
		name := choice
		b.OnClick = func() { e.CommitTag(name) }
		p.AddChild(b)
	}
}

// MoveTagHighlight moves the highlighted offered label.
func (e *Editor) MoveTagHighlight(delta int) {
	t := e.tagEdit
	if t == nil {
		return
	}
	n := len(e.TagChoices(t.block, t.col, t.index, t.query))
	if n == 0 {
		return
	}
	next := t.highlight + delta
	if next < -1 {
		next = n - 1
	} else if next >= n {
		next = -1
	}
	t.highlight = next
	e.refreshTagChoices()
}

// CommitTag adds a label through the chip row's field.
func (e *Editor) CommitTag(name string) {
	t := e.tagEdit
	if t == nil {
		return
	}
	col, idx := t.col, t.index
	id := t.block
	e.closeTagEdit()
	e.AddCardLabel(id, col, idx, name)
}

// AcceptTag takes the highlighted offered label, or the typed text.
func (e *Editor) AcceptTag() {
	t := e.tagEdit
	if t == nil {
		return
	}
	choices := e.TagChoices(t.block, t.col, t.index, t.query)
	if t.highlight >= 0 && t.highlight < len(choices) {
		e.CommitTag(choices[t.highlight])
		return
	}
	if strings.TrimSpace(strings.TrimLeft(strings.TrimSpace(t.query), "#")) != "" {
		e.CommitTag(t.query)
		return
	}
	e.closeTagEdit()
}

// closeTagEdit shuts the chip row's label field.
func (e *Editor) closeTagEdit() {
	if e.tagEdit != nil {
		if e.tagEdit.hide != nil {
			hide := e.tagEdit.hide
			e.tagEdit.hide = nil
			hide()
		}
		e.tagEdit = nil
	}
}

// duePickState is a card's date picker: the card and the visible month.
type duePickState struct {
	block       int64
	col, index  int
	year, month int // the visible month, month 1-12
	selected    string
	hide        func()
	grid        *unison.Panel
	title       *kvitui.Label
}

// DuePickerOpen reports the picker's card, visible month and day, for tests.
func (e *Editor) DuePickerOpen() (col, index, year, month int, selected string, ok bool) {
	d := e.duePick
	if d == nil {
		return 0, 0, 0, 0, "", false
	}
	return d.col, d.index, d.year, d.month, d.selected, true
}

// monthGrid is the visible month as weeks starting Monday (the ISO week),
// 0 for a day of another month.
func monthGrid(year, month int) [][]int {
	first := time.Date(year, time.Month(month), 1, 0, 0, 0, 0, time.UTC)
	lead := (int(first.Weekday()) + 6) % 7 // days before the 1st, Monday-first
	days := 31
	for d := 31; d >= 28; d-- {
		if time.Date(year, time.Month(month), d, 0, 0, 0, 0, time.UTC).Month() == time.Month(month) {
			days = d
			break
		}
	}
	var weeks [][]int
	week := make([]int, 0, 7)
	for range lead {
		week = append(week, 0)
	}
	for d := 1; d <= days; d++ {
		week = append(week, d)
		if len(week) == 7 {
			weeks = append(weeks, week)
			week = make([]int, 0, 7)
		}
	}
	if len(week) > 0 {
		for len(week) < 7 {
			week = append(week, 0)
		}
		weeks = append(weeks, week)
	}
	return weeks
}

// dueKey is the ISO day for a grid cell.
func dueKey(year, month, day int) string {
	return time.Date(year, time.Month(month), day, 0, 0, 0, 0, time.UTC).Format(time.DateOnly)
}

// OpenDuePicker opens the calendar for a card's due date, on its month when
// it has one and on this month otherwise.
func (e *Editor) OpenDuePicker(i, col, index int) {
	w := e.ui.WindowOf(e)
	if w == nil || e.Doc.ReadOnly || i < 0 {
		return
	}
	b := &e.Doc.Blocks[i]
	board := kanban.Parse(b.Text)
	if col < 0 || col >= len(board.Columns) || index < 0 || index >= len(board.Columns[col].Cards) {
		return
	}
	card := board.Columns[col].Cards[index]
	e.closeDuePicker()
	year, monthNow, _ := time.Now().Date()
	month := int(monthNow)
	selected := strings.TrimSpace(card.Due)
	if y, m, _, ok := parseDueDay(selected); ok {
		year, month = y, m
	}
	d := &duePickState{block: b.ID, col: col, index: index, year: year, month: month, selected: selected}
	e.duePick = d
	title := kvitui.NewLabel(e.ui, "")
	d.title = title
	grid := unison.NewPanel()
	grid.SetLayout(&unison.FlexLayout{Columns: 1})
	d.grid = grid
	e.refreshDueGrid()
	prev := kvitui.NewButton(e.ui, "‹")
	prev.Explanation = "Previous month"
	prev.OnClick = func() { e.ShiftDueMonth(-1) }
	next := kvitui.NewButton(e.ui, "›")
	next.Explanation = "Next month"
	next.OnClick = func() { e.ShiftDueMonth(1) }
	head := kvitui.Row(e.ui, kvitui.Px(4), prev, title, next)
	clear := kvitui.NewButton(e.ui, "Clear")
	clear.Explanation = "Clear the due date"
	clear.OnClick = func() { e.ClearDue() }
	body := kvitui.Column(e.ui, kvitui.Px(4), head, grid, clear)
	var at geom.Rect
	o := e.boardOrigin(i)
	for _, c := range e.board(i).cards {
		if c.col == col && c.index == index {
			for _, ch := range c.chips {
				if ch.due {
					at = e.RectToRoot(geom.NewRect(ch.r.X+o.X, ch.r.Y+o.Y, ch.r.Width, ch.r.Height))
				}
			}
			if at.Width == 0 && c.hasAdd {
				at = e.RectToRoot(geom.NewRect(c.addDue.X+o.X, c.addDue.Y+o.Y, c.addDue.Width, c.addDue.Height))
			}
			if at.Width == 0 {
				at = e.RectToRoot(geom.NewRect(c.r.X+o.X, c.r.Y+o.Y, c.r.Width, e.px(18)))
			}
			break
		}
	}
	d.hide = w.Show(&kvitui.Popup{Panel: body, Anchor: e,
		OnEscape:       func() { e.closeDuePicker() },
		OnPressOutside: func() { e.closeDuePicker() },
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			r := w.Content().RectFromRoot(at)
			return geom.NewRect(r.X, r.Y, max(r.Width, e.px(200)), size.Height)
		}})
}

// parseDueDay reads an ISO day into parts.
func parseDueDay(day string) (year, month, d int, ok bool) {
	t, err := time.Parse(time.DateOnly, strings.TrimSpace(day))
	if err != nil {
		return 0, 0, 0, false
	}
	y, m, dd := t.Date()
	return y, int(m), dd, true
}

// refreshDueGrid rebuilds the picker's month grid.
func (e *Editor) refreshDueGrid() {
	d := e.duePick
	if d == nil || d.grid == nil {
		return
	}
	d.title.Text = time.Month(d.month).String() + " " + strconv.Itoa(d.year)
	for _, ch := range d.grid.Children() {
		d.grid.RemoveChild(ch)
	}
	wd := kvitui.Row(e.ui, kvitui.Px(2))
	for _, name := range []string{"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"} {
		l := kvitui.NewLabel(e.ui, name)
		wd.AddChild(kvitui.Width(e.ui, kvitui.Px(28), l))
	}
	d.grid.AddChild(wd)
	today := time.Now().Format(time.DateOnly)
	for _, week := range monthGrid(d.year, d.month) {
		row := kvitui.Row(e.ui, kvitui.Px(2))
		for _, day := range week {
			if day == 0 {
				row.AddChild(kvitui.Width(e.ui, kvitui.Px(28), kvitui.NewLabel(e.ui, "")))
				continue
			}
			b := kvitui.NewButton(e.ui, strconv.Itoa(day))
			b.Form = kvitui.ButtonQuiet
			key := dueKey(d.year, d.month, day)
			b.Explanation = key
			switch {
			case key == d.selected:
				b.Form = kvitui.ButtonPrimary
			case key == today:
				b.Form = kvitui.ButtonOrdinary
			}
			day := day
			b.OnClick = func() { e.PickDueDay(day) }
			row.AddChild(kvitui.Width(e.ui, kvitui.Px(28), b))
		}
		d.grid.AddChild(row)
	}
}

// ShiftDueMonth moves the picker's visible month.
func (e *Editor) ShiftDueMonth(delta int) {
	d := e.duePick
	if d == nil {
		return
	}
	m := d.month + delta
	for m < 1 {
		m += 12
		d.year--
	}
	for m > 12 {
		m -= 12
		d.year++
	}
	d.month = m
	e.refreshDueGrid()
}

// PickDueDay writes the picker's day into the card and closes it.
func (e *Editor) PickDueDay(day int) {
	d := e.duePick
	if d == nil {
		return
	}
	e.SetCardDue(d.block, d.col, d.index, dueKey(d.year, d.month, day))
	e.closeDuePicker()
}

// ClearDue clears a card's due date through the picker.
func (e *Editor) ClearDue() {
	d := e.duePick
	if d == nil {
		return
	}
	e.SetCardDue(d.block, d.col, d.index, "")
	e.closeDuePicker()
}

// closeDuePicker shuts the date picker.
func (e *Editor) closeDuePicker() {
	if e.duePick != nil {
		if e.duePick.hide != nil {
			hide := e.duePick.hide
			e.duePick.hide = nil
			hide()
		}
		e.duePick = nil
	}
}

// CardDetailsState reads the card-details dialog's fields: the labels as
// typed and the due date.
func (e *Editor) CardDetailsState(id int64, col, index int) (labels, due string, ok bool) {
	b := e.Doc.Block(id)
	if b == nil {
		return "", "", false
	}
	board := kanban.Parse(b.Text)
	if col < 0 || col >= len(board.Columns) || index < 0 || index >= len(board.Columns[col].Cards) {
		return "", "", false
	}
	card := board.Columns[col].Cards[index]
	return strings.Join(card.Labels, ", "), card.Due, true
}

// splitDetailLabels splits the details dialog's labels field.
func splitDetailLabels(s string) []string {
	var out []string
	for _, part := range strings.Split(s, ",") {
		if t := strings.TrimSpace(part); t != "" {
			out = append(out, t)
		}
	}
	return out
}

// ApplyCardDetails writes the details dialog's fields: the labels and the due
// date. A due value the stored form cannot hold is refused: it returns false
// and writes nothing, so an invalid date never comes back as title text with
// the due date cleared.
func (e *Editor) ApplyCardDetails(id int64, col, index int, labels []string, due string) bool {
	due = strings.TrimSpace(due)
	if due != "" && !kanban.IsValidDue(due) {
		return false
	}
	b := e.Doc.Block(id)
	if b == nil || e.Doc.ReadOnly {
		return false
	}
	board := kanban.Parse(b.Text)
	if col < 0 || col >= len(board.Columns) || index < 0 || index >= len(board.Columns[col].Cards) {
		return false
	}
	card := board.Columns[col].Cards[index]
	clean := labels[:0]
	for _, l := range labels {
		if t := strings.TrimSpace(l); t != "" {
			clean = append(clean, t)
		}
	}
	content := kanban.SetCard(b.Text, col, index, card.Title, card.Done, clean, due, card.Description,
		time.Now().Format(time.DateOnly))
	if content == b.Text {
		return true
	}
	e.Doc.Edit("board", func() { b.Text = content })
	e.changed()
	return true
}

// OpenCardDetails opens the card-details dialog: the labels, the due date
// the storage grammar is strict about, moving to another column, and
// deleting the card.
func (e *Editor) OpenCardDetails(i, col, index int) {
	w := e.ui.WindowOf(e)
	if w == nil || e.Doc.ReadOnly || i < 0 {
		return
	}
	id := e.Doc.Blocks[i].ID
	labels, due, ok := e.CardDetailsState(id, col, index)
	if !ok {
		return
	}
	labelsField := kvitui.NewField(e.ui)
	labelsField.Label = "Labels"
	labelsField.SetText(labels)
	dueField := kvitui.NewField(e.ui)
	dueField.Label = "Due date"
	dueField.Placeholder = "YYYY-MM-DD"
	dueField.SetText(due)
	hint := kvitui.NewLabel(e.ui, "")
	var d *kvitui.Dialog
	var moveRow *unison.Panel
	settled := false // a move or delete already wrote; Accept must not save
	accept := func() {
		if d != nil {
			d.Accept()
		}
	}
	{
		panels := []unison.Paneler{kvitui.NewLabel(e.ui, "Move to")}
		for ci, c := range kanban.Parse(e.Doc.Blocks[i].Text).Columns {
			if ci == col {
				continue
			}
			ci := ci
			name := c.Name
			b := kvitui.NewButton(e.ui, name)
			b.Explanation = "Move this card to " + name
			b.OnClick = func() {
				settled = true
				today := time.Now().Format(time.DateOnly)
				if blk := e.Doc.Block(id); blk != nil {
					n := len(kanban.Parse(blk.Text).Columns[ci].Cards)
					if content := kanban.MoveCard(blk.Text, col, index, ci, n, today); content != blk.Text {
						e.Doc.Edit("board", func() { blk.Text = content })
						e.changed()
					}
				}
				accept()
			}
			panels = append(panels, b)
		}
		moveRow = kvitui.Row(e.ui, kvitui.Px(4), panels...)
	}
	del := kvitui.NewButton(e.ui, "Delete card")
	del.Danger = true
	del.Explanation = "Remove this card (Ctrl+Z undoes it)"
	del.OnClick = func() {
		settled = true
		if blk := e.Doc.Block(id); blk != nil {
			if content := kanban.RemoveCard(blk.Text, col, index); content != blk.Text {
				e.Doc.Edit("board", func() { blk.Text = content })
				e.changed()
			}
		}
		accept()
	}
	save := kvitui.NewButton(e.ui, "Save")
	save.Form = kvitui.ButtonPrimary
	save.Explanation = "Write the labels and due date to the card"
	save.OnClick = func() {
		if e.ApplyCardDetails(id, col, index, splitDetailLabels(labelsField.Text()), dueField.Text()) {
			settled = true
			accept()
			return
		}
		hint.Text = "Use YYYY-MM-DD for the due date, or empty for none."
	}
	foot := kvitui.Row(e.ui, kvitui.Px(4), save, del)
	d = kvitui.NewDialog(e.ui, "Card details",
		kvitui.NewLabel(e.ui, "Labels, separated by commas"),
		labelsField,
		kvitui.NewLabel(e.ui, "Due date, empty for none"),
		dueField, hint, moveRow, foot)
	// No default confirm button: saving goes through Save above, which
	// refuses an invalid due date by staying open.
	d.ConfirmText = ""
	d.CancelText = "Cancel"
	d.OnAccept = func() {
		if settled {
			return
		}
		// A programmatic accept (Return in a field) still validates: an
		// invalid due date is refused by dropping the dialog's answer.
		e.ApplyCardDetails(id, col, index, splitDetailLabels(labelsField.Text()), dueField.Text())
	}
	dueField.OnChange = func(text string) {
		t := strings.TrimSpace(text)
		if t != "" && !kanban.IsValidDue(t) {
			hint.Text = "Use YYYY-MM-DD for the due date, or empty for none."
		} else {
			hint.Text = ""
		}
	}
	d.Open(w)
}
