package editor

// The rest of the task board: column dragging with its gap,
// the dragged card's ghost, label chips with the tag field, the due-date
// picker and the card-details dialog.

import (
	"strings"
	"testing"

	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

const metaBoard = "```kanban\n## To do\n- [ ] Design the API #backend 📅 2026-08-01\n  Sketch the endpoints\n- [ ] Write the spec #docs\n## Done\n- [x] Set up CI #infra\n```\n"

// boardPoint maps a board part to the screen, for presses and drags.
func boardPoint(t *testing.T, s *uitest.Session, e *Editor, part string, col, index int) geom.Point {
	t.Helper()
	var p geom.Point
	s.Do(func() { p = s.Screen.PanelPoint(e, e.BoardPart(0, part, col, index).Center()) })
	return p
}

// dragTo presses at from, moves through waypoints and lets go at the last.
func dragTo(s *uitest.Session, from geom.Point, through ...geom.Point) {
	s.Screen.MouseDown(from, unison.ButtonLeft, mod.None)
	for _, p := range through {
		s.Screen.MouseMove(p, mod.None)
	}
	s.Screen.MouseUp(through[len(through)-1], unison.ButtonLeft, mod.None)
	s.Sync()
}

// columnDropIndex turns an insert-before slot into a MoveColumn target.
func TestColumnDropIndex(t *testing.T) {
	if got := columnDropIndex(0, 3); got != 2 {
		t.Errorf("dragging column 0 past the end lands at 2, got %d", got)
	}
	if got := columnDropIndex(2, 0); got != 0 {
		t.Errorf("dragging column 2 to the front lands at 0, got %d", got)
	}
	if got := columnDropIndex(1, 1); got != 1 {
		t.Errorf("dropping a column where it was lands at 1, got %d", got)
	}
}

// Dragging a column header by its name moves the column to the gap.
func TestColumnDragMovesColumn(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	from := boardPoint(t, s, e, "name", 0, 0)
	var to geom.Point
	s.Do(func() {
		last := e.BoardPart(0, "name", 1, 0)
		to = s.Screen.PanelPoint(e, geom.NewPoint(last.Right()+e.px(60), last.Center().Y))
	})
	dragTo(s, from, from.Add(geom.NewPoint(30, 0)), to)
	var order string
	s.Do(func() { order = e.BoardText(0) })
	if !strings.HasPrefix(order, "Done:") {
		t.Errorf("after dragging To do past Done: %q", order)
	}
}

// A click on a header renames nothing by itself and moves no column.
func TestColumnClickRenamesWithoutMoving(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	var before string
	s.Do(func() { before = e.Doc.Blocks[0].Text })
	p := boardPoint(t, s, e, "name", 0, 0)
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Sync()
	var after string
	s.Do(func() { after = e.Doc.Blocks[0].Text })
	if after != before {
		t.Errorf("a click on a header should move nothing: %q", after)
	}
	s.Do(func() { e.colDrag = nil })
}

// While a card is dragged its ghost is drawn under the pointer; Escape
// drops the drag without writing anything.
func TestCardGhostFollowsPointer(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	// Drive the press and drag in editor coordinates, as the pointer
	// handlers would (the event routing itself is covered by the column
	// drag test and the scenarios).
	var pressed bool
	var move, at geom.Point
	var ok bool
	var before string
	s.Do(func() {
		before = e.Doc.Blocks[0].Text
		r := e.BoardPart(0, "card", 0, 0)
		press := geom.NewPoint(r.Center().X, r.Y+e.px(boardCardPad+2))
		pressed = e.boardPress(0, press, false)
		move = press.Add(geom.NewPoint(50, 30))
		e.dragCard(move)
		_, at, _, ok = e.CardGhost()
	})
	if !pressed {
		t.Fatalf("pressing the card's title should start a drag")
	}
	if !ok {
		t.Fatalf("the dragged card should have a ghost")
	}
	if at != move {
		t.Errorf("the ghost should be under the pointer: %v vs %v", at, move)
	}
	s.Screen.KeyPress(unison.KeyEscape, mod.None)
	s.Sync()
	var wrote bool
	s.Do(func() {
		_, _, _, ok = e.CardGhost()
		wrote = e.Doc.Blocks[0].Text != before
	})
	if ok {
		t.Errorf("Escape should drop the card drag")
	}
	if wrote {
		t.Errorf("Escape should write nothing")
	}
}

// Labels go on through the chip row and come off through their chips;
// the choices offer the board's other labels for reuse.
func TestLabelChipsAddRemoveAndOffer(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	s.Do(func() { e.AddCardLabel(id, 0, 1, "#backend") })
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; !strings.Contains(got, "Write the spec #docs #backend") {
			t.Errorf("adding a label: %q", got)
		}
	})
	s.Do(func() { e.AddCardLabel(id, 0, 1, "backend") })
	s.Do(func() { e.AddCardLabel(id, 0, 1, "  ") })
	s.Do(func() {
		if got := strings.Count(e.Doc.Blocks[0].Text, "#backend"); got != 2 {
			t.Errorf("duplicates and blanks are ignored, backend count %d", got)
		}
	})
	// Card (0,1) already carries docs and backend; only infra is offered.
	s.Do(func() {
		if got := e.TagChoices(id, 0, 1, ""); len(got) != 1 || got[0] != "infra" {
			t.Errorf("choices should offer the board's other labels: %q", got)
		}
		if got := e.TagChoices(id, 0, 1, "inf"); len(got) != 1 || got[0] != "infra" {
			t.Errorf("typing narrows the choices: %q", got)
		}
		if got := e.TagChoices(id, 0, 1, "#INF"); len(got) != 1 || got[0] != "infra" {
			t.Errorf("a leading hash and case are ignored: %q", got)
		}
	})
	s.Do(func() { e.RemoveCardLabel(id, 0, 1, "backend") })
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; strings.Contains(got, "Write the spec #docs #backend") {
			t.Errorf("removing a label: %q", got)
		}
	})
}

// A press on a label chip removes it.
func TestChipPressRemovesLabel(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	var p geom.Point
	s.Do(func() {
		words, rects := e.BoardChipRects(0, 0, 0)
		for k, w := range words {
			if w == "#backend" {
				p = s.Screen.PanelPoint(e, rects[k].Center())
			}
		}
	})
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Sync()
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; strings.Contains(got, "Design the API #backend") {
			t.Errorf("pressing a label chip should remove it: %q", got)
		}
	})
}

// The hovered card offers + tag; the field takes the highlighted offer or
// the typed text.
func TestTagFieldTakesHighlightAndTyped(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	over := boardPoint(t, s, e, "card", 0, 1)
	s.Screen.MouseMove(over, mod.None)
	s.Sync()
	var add geom.Point
	s.Do(func() {
		r := e.BoardPart(0, "addtag", 0, 1)
		if r.Width == 0 {
			t.Fatalf("the hovered card should offer + tag")
		}
		add = s.Screen.PanelPoint(e, r.Center())
	})
	s.Screen.MouseDown(add, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(add, unison.ButtonLeft, mod.None)
	s.Sync()
	var ok bool
	s.Do(func() { _, _, _, _, ok = e.TagEditOpen() })
	if !ok {
		t.Fatalf("+ tag should open the label field")
	}
	s.Do(func() { e.MoveTagHighlight(1) })
	s.Do(func() { e.AcceptTag() })
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; !strings.Contains(got, "Write the spec #docs #backend") {
			t.Errorf("the highlighted offer should land on the card: %q", got)
		}
	})
}

// Due dates are set, cleared, and refused when invalid.
func TestSetCardDue(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	s.Do(func() { e.SetCardDue(id, 0, 1, "2026-10-01") })
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; !strings.Contains(got, "📅 2026-10-01") {
			t.Errorf("setting a due date: %q", got)
		}
	})
	s.Do(func() { e.SetCardDue(id, 0, 1, "next friday") })
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; strings.Contains(got, "📅") && !strings.Contains(got, "2026-08-01") {
			t.Errorf("an invalid due date clears it: %q", got)
		}
	})
}

// The month grid starts weeks on Monday.
func TestMonthGridMondayFirst(t *testing.T) {
	weeks := monthGrid(2026, 9) // 1 September 2026 is a Tuesday
	if len(weeks) < 5 || weeks[0][0] != 0 || weeks[0][1] != 1 || weeks[0][6] != 6 {
		t.Errorf("first week of September 2026: %v", weeks[0])
	}
	found := false
	for _, w := range weeks {
		for _, d := range w {
			if d == 30 {
				found = true
			}
		}
	}
	if !found {
		t.Errorf("September should hold 30 days: %v", weeks)
	}
	weeks = monthGrid(2024, 2) // leap February
	found = false
	for _, w := range weeks {
		for _, d := range w {
			if d == 29 {
				found = true
			}
		}
	}
	if !found {
		t.Errorf("February 2024 should hold 29 days: %v", weeks)
	}
}

// The picker opens on the card's month, writes the picked day, and clears.
func TestDuePickerPickAndClear(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	s.Do(func() { e.OpenDuePicker(0, 0, 0) })
	var year, month int
	var selected string
	var ok bool
	s.Do(func() { _, _, year, month, selected, ok = e.DuePickerOpen() })
	if !ok || year != 2026 || month != 8 || selected != "2026-08-01" {
		t.Fatalf("the picker should open on the card's month: %d-%d %q", year, month, selected)
	}
	s.Do(func() { e.ShiftDueMonth(1) })
	s.Do(func() { _, _, _, month, _, _ = e.DuePickerOpen() })
	if month != 9 {
		t.Fatalf("stepping forward a month: %d", month)
	}
	s.Do(func() { e.PickDueDay(15) })
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; !strings.Contains(got, "📅 2026-09-15") {
			t.Errorf("picking a day: %q", got)
		}
		if _, _, _, _, _, ok = e.DuePickerOpen(); ok {
			t.Errorf("picking a day should close the picker")
		}
	})
	s.Do(func() { e.OpenDuePicker(0, 0, 0) })
	s.Do(func() { e.ClearDue() })
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; strings.Contains(got, "2026-09-15") {
			t.Errorf("clearing the due date: %q", got)
		}
	})
}

// A press on a due chip opens the picker.
func TestDueChipOpensPicker(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	var p geom.Point
	s.Do(func() {
		_, rects := e.BoardChipRects(0, 0, 0)
		if len(rects) == 0 {
			t.Fatalf("the card should have chips")
		}
		p = s.Screen.PanelPoint(e, rects[len(rects)-1].Center())
	})
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Sync()
	var ok bool
	s.Do(func() { _, _, _, _, _, ok = e.DuePickerOpen() })
	if !ok {
		t.Fatalf("pressing the due chip should open the picker")
	}
	s.Do(func() { e.closeDuePicker() })
}

// The details dialog reads the card, writes valid fields, and refuses an
// invalid due date without writing anything.
func TestCardDetailsApply(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	var id int64
	s.Do(func() { id = e.Doc.Blocks[0].ID })
	var labels, due string
	var ok bool
	s.Do(func() { labels, due, ok = e.CardDetailsState(id, 0, 0) })
	if !ok || labels != "backend" || due != "2026-08-01" {
		t.Fatalf("details state: %q %q %v", labels, due, ok)
	}
	var applied bool
	s.Do(func() { applied = e.ApplyCardDetails(id, 0, 0, []string{"backend", "ui"}, "2026-09-01") })
	if !applied {
		t.Fatalf("valid details should apply")
	}
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; !strings.Contains(got, "📅 2026-09-01") {
			t.Errorf("applying details: %q", got)
		}
	})
	var before string
	s.Do(func() { before = e.Doc.Blocks[0].Text })
	s.Do(func() { applied = e.ApplyCardDetails(id, 0, 0, []string{"x"}, "tomorrow") })
	if applied {
		t.Errorf("an invalid due date should be refused")
	}
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; got != before {
			t.Errorf("a refused apply should write nothing: %q", got)
		}
	})
	if got := splitDetailLabels("a, ,b,, c "); len(got) != 3 || got[0] != "a" || got[2] != "c" {
		t.Errorf("labels split: %q", got)
	}
}

// The card menu reaches the details dialog.
func TestCardMenuHasDetails(t *testing.T) {
	s, e := openEditor(t, metaBoard)
	var found bool
	s.Do(func() {
		for _, item := range e.cardItems(e.Doc.Blocks[0].ID, 0, 0) {
			if strings.Contains(item.Text, "bels and due date") {
				found = true
			}
		}
	})
	if !found {
		t.Errorf("the card menu should offer labels and due date")
	}
}
