package editor

// Editing a card of a task board (Kvit's KanbanBlock.qml card editor): a
// field over the card holds its line (the title with its "#labels" and
// "📅 date" as the file holds them) or its description. A press on the
// description edits the description, a press elsewhere on the card its
// line. Tab goes from the line to the description and from the description
// out; Shift+Tab goes back. Enter keeps what was typed, Shift+Enter breaks a
// description's line, and Escape leaves the card as it was.
//
// Both fields have the math typing aids a block of the note has
// (mathassist.go): `$` puts in a pair of dollars, `\` in math opens the
// command menu, Tab walks a template's slots, Ctrl+Space opens the menu
// again. Each field keeps what the aids remember in a mathTrack of its own.

import (
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/kanban"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// The two fields of a card.
const (
	cardLine        = "line"
	cardDescription = "description"
)

// fieldSurface is a card's field as a math surface.
type fieldSurface struct{ f *unison.Field }

func (s fieldSurface) id() any                 { return s.f }
func (s fieldSurface) text() []rune            { return []rune(s.f.Text()) }
func (s fieldSurface) selection() (int, int)   { return s.f.Selection() }
func (s fieldSurface) unitAt(off int) mathUnit { return mathUnit{unitProse, 0, len(s.text())} }
func (s fieldSurface) setCaret(off int)        { s.f.SetSelection(off, off) }

func (s fieldSurface) caret() int {
	_, end := s.f.Selection()
	return end
}

func (s fieldSurface) replace(start, end int, text string, caret int, _ bool) {
	r := s.text()
	s.f.SetText(string(r[:start]) + text + string(r[end:]))
	s.f.SetSelection(caret, caret)
}

func (s fieldSurface) caretRect() (geom.Rect, bool) {
	w := s.f.Window()
	if w == nil {
		return geom.Rect{}, false
	}
	p := s.f.FromSelectionIndex(s.caret())
	r := geom.NewRect(p.X, p.Y, 1, s.f.Font.LineHeight())
	return w.Content().RectFromRoot(s.f.RectToRoot(r)), true
}

// cardFieldAt is the field of a card a press at where, in the editor's
// coordinates, edits: the description when it is on the card's
// description, the line otherwise.
func (e *Editor) cardFieldAt(i, col, index int, where geom.Point) string {
	p := where.Sub(e.boardOrigin(i))
	for _, c := range e.board(i).cards {
		if c.col == col && c.index == index {
			if r, ok := e.cardDescriptionRect(c); ok && p.In(r) {
				return cardDescription
			}
		}
	}
	return cardLine
}

// cardDescriptionRect is where a card's description is drawn, in the
// board's coordinates.
func (e *Editor) cardDescriptionRect(c cardBox) (geom.Rect, bool) {
	if c.desc == nil {
		return geom.Rect{}, false
	}
	w, h := c.desc.Size()
	return geom.NewRect(c.r.X+e.px(boardCardPad), c.r.Bottom()-e.px(boardCardPad)-h, w, h), true
}

// editCardField opens a field over a card for its line or its description,
// and on Enter writes what was typed into the board, as one undo step.
func (e *Editor) editCardField(i, col, index int, which string) {
	w := e.ui.WindowOf(e)
	if w == nil || e.Doc.ReadOnly || i < 0 {
		return
	}
	id := e.Doc.Blocks[i].ID
	board := kanban.Parse(e.Doc.Blocks[i].Text)
	if col >= len(board.Columns) || index >= len(board.Columns[col].Cards) {
		return
	}
	card := board.Columns[col].Cards[index]
	var at geom.Rect
	for _, c := range e.board(i).cards {
		if c.col != col || c.index != index {
			continue
		}
		at = c.r
		if r, ok := e.cardDescriptionRect(c); ok && which == cardDescription {
			at = r
		} else if which == cardDescription {
			at = geom.NewRect(c.r.X, c.r.Bottom(), c.r.Width, 0)
		}
	}

	var field *kvitui.Field
	if which == cardDescription {
		area := kvitui.NewTextArea(e.ui)
		field = &area.Field
		field.Label = "Card description"
		field.SetText(card.Description)
	} else {
		field = kvitui.NewField(e.ui)
		field.Label = "Card"
		field.SetText(card.Line())
	}
	edit := field.Edit()
	surface := fieldSurface{edit}
	track := &mathTrack{}
	sync := func() {
		e.syncMathOn(surface, track)
		e.MarkForRedraw()
	}

	var hide func()
	// done closes the field, writing what was typed when keep is set, and
	// then opens next, the card's other field, when it is not "".
	done := func(keep bool, next string) {
		if hide == nil {
			return
		}
		hide()
		hide = nil
		if m := e.math.menu; m != nil && m.track == track {
			e.closeMathMenu()
		}
		if keep {
			e.writeCardField(id, col, index, which, card, strings.TrimSpace(field.Text()))
		}
		e.RequestFocus()
		e.changed()
		if next != "" {
			e.editCardField(e.Doc.Index(id), col, index, next)
		}
	}

	keys := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		ctrl, shift, alt := mods.OSMenuCommandDown(), mods.ShiftDown(), mods.OptionDown()
		if key == unison.KeyNumPadEnter {
			key = unison.KeyReturn
		}
		if m := e.math.menu; m != nil && m.track == track && !ctrl && !alt && e.mathMenuKey(key, shift) {
			sync()
			return true
		}
		if !alt && e.mathKeyOn(surface, track, key, ctrl, shift) {
			sync()
			return true
		}
		switch {
		case key == unison.KeyReturn && !(shift && which == cardDescription):
			done(true, "")
			return true
		case key == unison.KeyTab && shift:
			if which == cardDescription {
				done(true, cardLine)
			} else {
				done(true, "")
			}
			return true
		case key == unison.KeyTab:
			if which == cardLine {
				done(true, cardDescription)
			} else {
				done(true, "")
			}
			return true
		}
		used := keys != nil && keys(key, mods, repeat)
		sync()
		return used
	}
	runes := edit.RuneTypedCallback
	edit.RuneTypedCallback = func(ch rune) bool {
		if e.mathType(surface, track, string(ch)) {
			sync()
			return true
		}
		used := runes != nil && runes(ch)
		sync()
		return used
	}
	press := edit.MouseDownCallback
	edit.MouseDownCallback = func(where geom.Point, button, clicks int, mods mod.Modifiers) bool {
		used := press != nil && press(where, button, clicks, mods)
		sync()
		return used
	}

	o := e.boardOrigin(i)
	root := e.RectToRoot(geom.NewRect(at.X+o.X, at.Y+o.Y, at.Width, at.Height))
	hide = w.Show(&kvitui.Popup{Panel: field, Anchor: e,
		OnEscape:       func() { done(false, "") },
		OnPressOutside: func() { done(true, "") },
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			r := w.Content().RectFromRoot(root)
			return geom.NewRect(r.X, r.Y, max(r.Width, e.px(180)), size.Height)
		}})
	unison.InvokeTask(func() { field.Focus(); edit.SelectAll() })
}

// writeCardField writes a card's field into its board, as one undo step. A
// new card whose line is left empty is taken away.
func (e *Editor) writeCardField(id int64, col, index int, which string, card kanban.Card, text string) {
	blk := e.Doc.Block(id)
	if blk == nil || e.Doc.ReadOnly {
		return
	}
	today := time.Now().Format(time.DateOnly)
	var content string
	switch {
	case which == cardDescription:
		content = kanban.SetCardDescription(blk.Text, col, index, text, today)
	case text == "" && card.Title == "" && len(card.Labels) == 0:
		content = kanban.RemoveCard(blk.Text, col, index)
	default:
		content = kanban.SetCardLine(blk.Text, col, index, text, today)
	}
	if content != "" && content != blk.Text {
		e.Doc.Edit("board", func() { blk.Text = content })
	}
}
