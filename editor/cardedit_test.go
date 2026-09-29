package editor

// A task board card's fields, and the math typing aids in them (Kvit's
// KanbanBlock card editor, which has the same MathEntryAssist as a
// block). The field being edited is read through what a screen reader is
// told about the focused control.

import (
	"strings"
	"testing"

	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

const boardNote = "Intro\n\n```kanban\n## To do\n- [ ] First card\n  a description\n```\n"

// pressCard presses and lets go on a point of a card of the board in block
// 1: its description when desc is set, else its title. The pointer moves
// there first, as in real use: the hovered card grows its + tag row, so
// aiming before the hover would land in the wrong row.
func pressCard(t *testing.T, s *uitest.Session, e *Editor, desc bool) {
	t.Helper()
	var p geom.Point
	aim := func() geom.Point {
		var q geom.Point
		s.Do(func() {
			for _, c := range e.board(1).cards {
				if c.col != 0 || c.index != 0 {
					continue
				}
				r := geom.NewRect(c.r.X, c.r.Y, c.r.Width, e.px(boardCardPad+boardBox))
				if desc {
					r, _ = e.cardDescriptionRect(c)
				}
				q = s.Screen.PanelPoint(e, r.Center().Add(e.boardOrigin(1)))
			}
		})
		return q
	}
	p = aim()
	s.Screen.MouseMove(p, mod.None)
	s.Sync()
	p = aim()
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Sync()
}

// focusedField is the name and text of the control with the keyboard.
func focusedField(s *uitest.Session) (name, text string) {
	if n := s.Focused(); n != nil {
		return n.Name, n.Value
	}
	return "", ""
}

// A card's description has the dollar pair and the command menu, and what
// is typed goes into the board as one undo step.
func TestCardDescriptionHasTheMathAids(t *testing.T) {
	s, e := mathNote(t, boardNote, 0, 0)
	pressCard(t, s, e, true)
	if name, text := focusedField(s); name != "Card description" || text != "a description" {
		t.Fatalf("the field: %q holding %q", name, text)
	}
	key(s, unison.KeyEnd, mod.None)
	typed(s, " $")
	if _, text := focusedField(s); text != "a description $$" {
		t.Fatalf("$: %q", text)
	}
	key(s, unison.KeyBackspace, mod.None)
	if _, text := focusedField(s); text != "a description " {
		t.Fatalf("Backspace on the empty pair: %q", text)
	}
	typed(s, `$\`)
	if open, q, _ := menuState(s, e); !open || q != "" {
		t.Fatalf(`$\: open %v, query %q`, open, q)
	}
	typed(s, "alpha")
	if open, q, rows := menuState(s, e); !open || q != "alpha" || len(rows) == 0 || rows[0] != `\alpha` {
		t.Fatalf("alpha: open %v, query %q, rows %q", open, q, rows)
	}
	key(s, unison.KeyReturn, mod.None) // chooses \alpha
	if _, text := focusedField(s); text != `a description $\alpha$` {
		t.Fatalf("after choosing: %q", text)
	}
	if open, _, _ := menuState(s, e); open {
		t.Error("the menu stayed open")
	}
	key(s, unison.KeyReturn, mod.None) // keeps the description
	if text, _ := blockText(s, e, 1); !strings.HasSuffix(text, "\n  a description $\\alpha$") {
		t.Fatalf("the board: %q", text)
	}
	s.Do(func() { e.Doc.Undo() })
	if text, _ := blockText(s, e, 1); text != "## To do\n- [ ] First card\n  a description" {
		t.Errorf("one undo should take the description back: %q", text)
	}
}

// A card's line has the aids too; Tab goes from the line to the
// description, keeping the line, and Escape leaves the description as it
// was.
func TestCardLineHasTheMathAidsAndTabGoesToTheDescription(t *testing.T) {
	s, e := mathNote(t, boardNote, 0, 0)
	pressCard(t, s, e, false)
	if name, text := focusedField(s); name != "Card" || text != "First card" {
		t.Fatalf("the field: %q holding %q", name, text)
	}
	key(s, unison.KeyEnd, mod.None)
	typed(s, " $x")
	if _, text := focusedField(s); text != "First card $x$" {
		t.Fatalf("$x: %q", text)
	}
	typed(s, "$") // steps over the closing dollar
	if _, text := focusedField(s); text != "First card $x$" {
		t.Fatalf("a second $: %q", text)
	}
	key(s, unison.KeyTab, mod.None)
	if name, text := focusedField(s); name != "Card description" || text != "a description" {
		t.Fatalf("after Tab: %q holding %q", name, text)
	}
	if text, _ := blockText(s, e, 1); !strings.Contains(text, "- [ ] First card $x$ ") {
		t.Errorf("Tab should keep the line: %q", text)
	}
	typed(s, "changed")
	key(s, unison.KeyEscape, mod.None)
	if text, _ := blockText(s, e, 1); !strings.HasSuffix(text, "\n  a description") {
		t.Errorf("Escape should leave the description: %q", text)
	}
}
