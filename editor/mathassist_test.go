package editor

// The math typing aids through the keyboard, on a headless screen. The tests
// named after tests/tst_integration's (test_zzq to test_zzy4) port them
// with the same notes and keys. The rest cover what those tests do not
// reach: the slot walk, Ctrl+Space, browsing the categories, a table cell
// and a code block.

import (
	"slices"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// mathNote opens an editor on md with the caret in block i at offset off.
func mathNote(t *testing.T, md string, i, off int) (*uitest.Session, *Editor) {
	t.Helper()
	s, e := openEditor(t, md)
	s.Do(func() { e.FocusBlock(i, off) })
	s.Sync()
	return s, e
}

// blockText is block i's source and the caret's offset.
func blockText(s *uitest.Session, e *Editor, i int) (text string, caret int) {
	s.Do(func() { text, caret = e.Doc.Blocks[i].Text, e.Doc.Caret.Off })
	return text, caret
}

func menuState(s *uitest.Session, e *Editor) (open bool, query string, names []string) {
	s.Do(func() { open, query, names, _ = e.MathMenuState() })
	return open, query, names
}

func key(s *uitest.Session, k unison.KeyCode, m mod.Modifiers) {
	s.Screen.KeyPress(k, m)
	s.Sync()
}

func typed(s *uitest.Session, text string) {
	s.Screen.Type(text)
	s.Sync()
}

// test_zzq: a bare backslash shows the categories, letters rank the
// commands with the best first, choosing hands over the template, the
// chosen command leads "Recently used", and a query nothing matches closes
// the menu, leaving the typed text.
func TestMathCommandMenuPopupModes(t *testing.T) {
	s, e := mathNote(t, "note", 0, 4)
	typed(s, `$\`)
	open, q, grid := menuState(s, e)
	if !open || q != "" || len(grid) == 0 {
		t.Fatalf("after a bare backslash: open %v, query %q, grid %q", open, q, grid)
	}
	var cats []string
	s.Do(func() { cats = e.math.menu.cats })
	if len(cats) < 15 {
		t.Errorf("categories: %q", cats)
	}
	typed(s, "fra")
	if open, q, rows := menuState(s, e); !open || q != "fra" || len(rows) == 0 || rows[0] != `\frac` {
		t.Fatalf("completion: open %v, query %q, rows %q", open, q, rows)
	}
	key(s, unison.KeyReturn, mod.None)
	if open, _, _ := menuState(s, e); open {
		t.Error("the menu stayed open after choosing")
	}
	if text, caret := blockText(s, e, 0); text != `note$\frac{}{}$` || caret != 5+6 {
		t.Errorf("after choosing: %q, caret %d", text, caret)
	}

	// The chosen command now leads the categories.
	key(s, unison.KeyEnd, mod.None)
	typed(s, ` $\`)
	s.Do(func() { cats = e.math.menu.cats })
	if _, _, grid := menuState(s, e); cats[0] != "Recently used" || len(grid) == 0 || grid[0] != `\frac` {
		t.Errorf("recent: categories %q, grid %q", cats[:2], grid)
	}

	// A query nothing matches closes the menu; the text stays.
	typed(s, "zzqqxy")
	if open, _, _ := menuState(s, e); open {
		t.Error("the menu stayed open for a query that matches nothing")
	}
	if text, _ := blockText(s, e, 0); !strings.Contains(text, `\zzqqxy`) {
		t.Errorf("the typed text went: %q", text)
	}
}

// test_zzr_mathCommandMenuInMathBlock: in a display block a backslash opens
// the menu, letters filter it, Enter puts the template in with the caret in
// its first slot, Tab moves to the second, and Escape closes only the menu.
func TestMathCommandMenuInMathBlock(t *testing.T) {
	s, e := mathNote(t, "$$\nx+1\n$$", 0, 3)
	typed(s, `\`)
	if open, q, _ := menuState(s, e); !open || q != "" {
		t.Fatalf("a bare backslash: open %v, query %q", open, q)
	}
	typed(s, "fr")
	if open, q, rows := menuState(s, e); !open || q != "fr" || len(rows) == 0 || rows[0] != `\frac` {
		t.Fatalf("fr: open %v, query %q, rows %q", open, q, rows)
	}
	key(s, unison.KeyReturn, mod.None)
	text, caret := blockText(s, e, 0)
	if open, _, _ := menuState(s, e); open || !strings.Contains(text, `\frac{}{}`) {
		t.Fatalf("after Enter: open %v, text %q", open, text)
	}
	r := []rune(text)
	if r[caret-1] != '{' || r[caret] != '}' {
		t.Errorf("the caret is not in the first slot: %q at %d", text, caret)
	}
	key(s, unison.KeyTab, mod.None)
	if _, c := blockText(s, e, 0); c != caret+2 {
		t.Errorf("Tab went to %d, want %d", c, caret+2)
	}

	typed(s, `\`)
	if open, _, _ := menuState(s, e); !open {
		t.Fatal("the menu did not open again")
	}
	key(s, unison.KeyEscape, mod.None)
	var focused bool
	s.Do(func() { focused = e.Focused() && e.Doc.Focused })
	if open, _, _ := menuState(s, e); open || !focused {
		t.Errorf("after Escape: open %v, the editor focused %v", open, focused)
	}
}

// test_zzs: the dollar pair (both go on Backspace, Delete leaves a literal
// dollar, a second dollar steps over the closing one), a backslash in the
// fresh pair opening the menu, and no pair in front of a letter.
func TestDollarAutoPairAndInlineMenu(t *testing.T) {
	s, e := mathNote(t, "note", 0, 4)
	typed(s, "$")
	if text, caret := blockText(s, e, 0); text != "note$$" || caret != 5 {
		t.Fatalf("$: %q, caret %d", text, caret)
	}
	key(s, unison.KeyBackspace, mod.None)
	if text, _ := blockText(s, e, 0); text != "note" {
		t.Errorf("Backspace on the empty pair: %q", text)
	}
	typed(s, "$")
	key(s, unison.KeyDelete, mod.None)
	if text, _ := blockText(s, e, 0); text != "note$" {
		t.Errorf("Delete in the pair: %q", text)
	}
	key(s, unison.KeyBackspace, mod.None)
	if text, _ := blockText(s, e, 0); text != "note" {
		t.Fatalf("Backspace after: %q", text)
	}

	typed(s, `$\`)
	if open, _, _ := menuState(s, e); !open {
		t.Fatal(`$\ did not open the menu`)
	}
	typed(s, "alpha")
	if open, q, _ := menuState(s, e); !open || q != "alpha" {
		t.Fatalf("alpha: open %v, query %q", open, q)
	}
	key(s, unison.KeyReturn, mod.None)
	if text, _ := blockText(s, e, 0); !strings.Contains(text, `$\alpha$`) {
		t.Fatalf("the span should hold the command: %q", text)
	}

	before, _ := blockText(s, e, 0)
	typed(s, "$")
	if text, caret := blockText(s, e, 0); text != before || caret != len([]rune(before)) {
		t.Errorf("$ should step over the closing dollar: %q, caret %d", text, caret)
	}

	s.Do(func() { e.FocusBlock(0, 0) })
	typed(s, "$")
	if text, _ := blockText(s, e, 0); !strings.HasPrefix(text, "$n") {
		t.Errorf("in front of a letter the dollar should stay single: %q", text)
	}
}

// test_zzt: \cdot leads its completion and goes in as itself.
func TestCdotAutocompleteInInlineMath(t *testing.T) {
	s, e := mathNote(t, "note", 0, 4)
	typed(s, `$\cdot`)
	if open, q, rows := menuState(s, e); !open || q != "cdot" || len(rows) == 0 || rows[0] != `\cdot` {
		t.Fatalf("cdot: open %v, query %q, rows %q", open, q, rows)
	}
	key(s, unison.KeyReturn, mod.None)
	if text, _ := blockText(s, e, 0); !strings.Contains(text, `$\cdot$`) {
		t.Errorf("after Enter: %q", text)
	}
}

// test_zzu: a TeX control symbol, \|, completes in inline math.
func TestControlSymbolAutocompleteInInlineMath(t *testing.T) {
	s, e := mathNote(t, "note", 0, 4)
	typed(s, `$\|`)
	if open, q, rows := menuState(s, e); !open || q != "|" || len(rows) == 0 || rows[0] != `\|` {
		t.Fatalf("|: open %v, query %q, rows %q", open, q, rows)
	}
	key(s, unison.KeyReturn, mod.None)
	if text, _ := blockText(s, e, 0); !strings.Contains(text, `$\|$`) {
		t.Errorf("after Enter: %q", text)
	}
}

// test_zzv: and in a display block.
func TestControlSymbolAutocompleteInDisplayMath(t *testing.T) {
	s, e := mathNote(t, "$$\nx\n$$", 0, 1)
	typed(s, `\|`)
	if open, q, rows := menuState(s, e); !open || q != "|" || len(rows) == 0 || rows[0] != `\|` {
		t.Errorf("|: open %v, query %q, rows %q", open, q, rows)
	}
}

// test_zzw: the menu follows the text at the caret, whatever changed before
// it; a space ends the command and closes the menu, and a backslash opens
// it again.
func TestDisplayMenuAfterPopulatedFormula(t *testing.T) {
	tex := `x^2 + 75=36 \alpha`
	s, e := mathNote(t, "$$\n"+tex+"\n$$", 0, strings.Index(tex, `\alpha`))
	typed(s, `\`)
	if open, q, _ := menuState(s, e); !open || q != "" {
		t.Fatalf("backslash: open %v, query %q", open, q)
	}
	// Text put in before the command moves it along; the menu stays.
	s.Do(func() {
		d := e.Doc
		caret := d.Caret.Off
		d.Edit("typing", func() { d.Blocks[0].Text = "z" + d.Blocks[0].Text })
		d.SetCaret(d.Blocks[0].ID, caret+1)
		e.changed()
	})
	if open, q, _ := menuState(s, e); !open || q != "" {
		t.Fatalf("after text before it: open %v, query %q", open, q)
	}
	typed(s, "c")
	if open, q, _ := menuState(s, e); !open || q != "c" {
		t.Fatalf("c: open %v, query %q", open, q)
	}
	typed(s, " ")
	if open, _, _ := menuState(s, e); open {
		t.Error("a space should close the menu")
	}
	typed(s, `\a`)
	if open, q, _ := menuState(s, e); !open || q != "a" {
		t.Errorf("\\a: open %v, query %q", open, q)
	}
}

// test_zzy4: in a display block Enter is a line break, and Ctrl+Enter
// leaves the block for a new paragraph below.
func TestDisplayMathCtrlEnterLeavesTheBlock(t *testing.T) {
	s, e := mathNote(t, "$$\nx^2\n$$", 0, 3)
	key(s, unison.KeyReturn, mod.None)
	var n int
	s.Do(func() { n = len(e.Doc.Blocks) })
	if text, _ := blockText(s, e, 0); text != "x^2\n" || n != 1 {
		t.Fatalf("Enter: %q in %d blocks", text, n)
	}
	key(s, unison.KeyReturn, mod.Control)
	s.Do(func() {
		d := e.Doc
		if len(d.Blocks) != 2 || d.Blocks[1].Kind != Paragraph || d.Caret.Block != d.Blocks[1].ID {
			t.Errorf("Ctrl+Enter: %s, caret in %d", texts(d), d.Caret.Block)
		}
		if d.Blocks[0].Text != "x^2\n" {
			t.Errorf("the equation lost what was typed: %q", d.Blocks[0].Text)
		}
	})
}

// A template's empty slots are walked with Tab and Shift+Tab; the walk ends
// after the last slot, and Tab then does what it does in the block.
func TestTabWalksTemplateSlots(t *testing.T) {
	s, e := mathNote(t, "x ", 0, 2)
	typed(s, `$\sqrt`)
	_, _, rows := menuState(s, e)
	k := slices.Index(rows, `\sqrt[n]`)
	if k < 0 {
		t.Fatalf("no \\sqrt[n]: %q", rows)
	}
	for range k {
		key(s, unison.KeyDown, mod.None)
	}
	key(s, unison.KeyTab, mod.None) // Tab chooses, as Enter does
	text, caret := blockText(s, e, 0)
	if text != `x $\sqrt[]{}$` || caret != 9 {
		t.Fatalf("after choosing: %q, caret %d", text, caret)
	}
	key(s, unison.KeyTab, mod.None)
	if _, c := blockText(s, e, 0); c != 11 {
		t.Errorf("Tab: caret %d, want 11", c)
	}
	key(s, unison.KeyTab, mod.Shift)
	if _, c := blockText(s, e, 0); c != 9 {
		t.Errorf("Shift+Tab: caret %d, want 9", c)
	}
	key(s, unison.KeyTab, mod.None)
	key(s, unison.KeyTab, mod.None) // no slot after the last: the walk ends
	s.Do(func() {
		if e.math.track.chainOn != nil {
			t.Error("the walk did not end after the last slot")
		}
		if e.Doc.Blocks[0].Text != `x $\sqrt[]{}$` {
			t.Errorf("the text changed: %q", e.Doc.Blocks[0].Text)
		}
	})
}

// Ctrl+Space opens the ranked list again for the backslash word at the
// caret, in math only.
func TestCtrlSpaceCompletesAgain(t *testing.T) {
	s, e := mathNote(t, `a $\fr$ b`, 0, 6)
	key(s, unison.KeySpace, mod.Control)
	if open, q, rows := menuState(s, e); !open || q != "fr" || len(rows) == 0 || rows[0] != `\frac` {
		t.Fatalf("Ctrl+Space: open %v, query %q, rows %q", open, q, rows)
	}
	if text, _ := blockText(s, e, 0); text != `a $\fr$ b` {
		t.Errorf("Ctrl+Space typed something: %q", text)
	}
	key(s, unison.KeyEscape, mod.None)
	s.Do(func() { e.FocusBlock(0, 1) })
	key(s, unison.KeySpace, mod.Control)
	if open, _, _ := menuState(s, e); open {
		t.Error("Ctrl+Space opened the menu outside math")
	}
}

// On a bare backslash the arrows move through the grid of the category,
// Left from its first column reaches the categories, Down there shows the
// next category, Right goes back to its grid, and Enter puts the
// highlighted command in.
func TestBrowseTheCategories(t *testing.T) {
	s, e := mathNote(t, "note", 0, 4)
	typed(s, `$\`)
	_, _, grid := menuState(s, e)
	if len(grid) == 0 || grid[0] != `\alpha` {
		t.Fatalf("Greek first: %q", grid)
	}
	key(s, unison.KeyRight, mod.None)
	key(s, unison.KeyDown, mod.None)
	s.Do(func() {
		if _, _, _, sel := e.MathMenuState(); sel != 1+mathGridCols {
			t.Errorf("Right then Down: cell %d", sel)
		}
	})
	key(s, unison.KeyUp, mod.None)
	key(s, unison.KeyLeft, mod.None)
	key(s, unison.KeyLeft, mod.None) // from the first column to the categories
	key(s, unison.KeyDown, mod.None) // the next category: Arrows
	key(s, unison.KeyRight, mod.None)
	_, _, grid = menuState(s, e)
	if len(grid) == 0 || grid[0] != `\to` {
		t.Fatalf("the second category: %q", grid)
	}
	key(s, unison.KeyReturn, mod.None)
	if text, _ := blockText(s, e, 0); text != `note$\to$` {
		t.Errorf("after Enter: %q", text)
	}
}

// A table cell's math has the aids too.
func TestTableCellMath(t *testing.T) {
	table := "| a | b |\n| - | - |\n| x | y |"
	s, e := mathNote(t, table, 0, strings.Index(table, "x |")+1)
	typed(s, " $")
	if text, _ := blockText(s, e, 0); !strings.Contains(text, "| x $$ |") {
		t.Fatalf("a dollar in a cell: %q", text)
	}
	typed(s, `\`)
	if open, _, _ := menuState(s, e); !open {
		t.Error("a backslash in a cell's math did not open the menu")
	}
}

// A code block types dollars and backslashes as they are.
func TestCodeBlockTypesMathLiterally(t *testing.T) {
	s, e := mathNote(t, "```\nx\n```", 0, 1)
	typed(s, `$\`)
	if text, _ := blockText(s, e, 0); text != `x$\` {
		t.Errorf("code: %q", text)
	}
	if open, _, _ := menuState(s, e); open {
		t.Error("the menu opened in a code block")
	}
}

// A dollar typed over a selection puts the selection between dollars.
func TestDollarWrapsASelection(t *testing.T) {
	s, e := mathNote(t, "a x b", 0, 2)
	s.Do(func() { e.Doc.Caret.Off = 3 })
	typed(s, "$")
	if text, caret := blockText(s, e, 0); text != "a $x$ b" || caret != 5 {
		t.Errorf("wrapped: %q, caret %d", text, caret)
	}
}
