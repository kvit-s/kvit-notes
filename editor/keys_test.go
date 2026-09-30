package editor

import (
	"strings"
	"testing"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// features.md 2.6: Ctrl+Home and Ctrl+End go to the start and the end of
// the note, and with Shift they select to there.
func TestCtrlHomeAndEndReachTheEndsOfTheNote(t *testing.T) {
	s, e := openEditor(t, accessNote)
	s.Do(func() { e.FocusBlock(1, 5) })
	s.Sync()
	s.Screen.KeyPress(unison.KeyEnd, mod.Control)
	s.Do(func() {
		d := e.Doc
		last := d.Blocks[len(d.Blocks)-1]
		if d.Caret.Block != last.ID || d.Caret.Off != len([]rune(last.Text)) {
			t.Errorf("Ctrl+End should reach the end of the last block: %v", d.Caret)
		}
	})
	s.Screen.KeyPress(unison.KeyHome, mod.Control|mod.Shift)
	s.Do(func() {
		d := e.Doc
		if d.Caret.Block != d.Blocks[0].ID || d.Caret.Off != 0 || !d.CrossBlock() {
			t.Errorf("Ctrl+Shift+Home should select back to the start of the note: %v to %v", d.Anchor, d.Caret)
		}
	})
}

// A read-only note moves its caret and copies, and changes nothing.
func TestReadOnlyRefusesEveryChange(t *testing.T) {
	s, e := openEditor(t, accessNote)
	s.Do(func() {
		e.Doc.ReadOnly = true
		e.FocusBlock(1, 0)
	})
	s.Sync()
	before := ""
	s.Do(func() { before = Serialize(e.Doc.Blocks) })
	s.Screen.Type("typed")
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Screen.KeyPress(unison.KeyBackspace, mod.None)
	s.Screen.KeyPress(unison.KeyD, mod.Control)
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	s.Screen.KeyPress(unison.KeyRight, mod.Shift)
	s.Screen.KeyPress(unison.KeyC, mod.Control)
	s.Do(func() {
		if after := Serialize(e.Doc.Blocks); after != before {
			t.Errorf("a read-only note changed:\n%s", after)
		}
		if e.Doc.Caret.Off != 1 || e.Doc.Anchor.Off != 0 {
			t.Errorf("the caret should still move and select: %v %v", e.Doc.Anchor, e.Doc.Caret)
		}
		if got := unison.ClipboardGetText(); got != "T" {
			t.Errorf("copy should still work: %q", got)
		}
	})
}

// Ctrl+Enter on selected blocks puts the caret in a new paragraph after
// them, the keyboard's way below a table or a board.
func TestCtrlEnterAfterSelectedBlocks(t *testing.T) {
	s, e := openEditor(t, "| a | b |\n| --- | --- |\n| 1 | 2 |\n\nAfter\n")
	s.Do(func() {
		e.FocusBlock(1, 0)
		e.blockSel[e.Doc.Blocks[0].ID] = true
		e.RequestFocus()
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		d := e.Doc
		if len(d.Blocks) != 3 || d.Blocks[1].Kind != Paragraph || d.Blocks[1].Text != "" || d.Caret.Block != d.Blocks[1].ID {
			t.Errorf("blocks %d, the second %v %q, caret in %d", len(d.Blocks), d.Blocks[1].Kind, d.Blocks[1].Text, d.Caret.Block)
		}
	})
}

// Ctrl+V of a crooked drawing into a code block straightens it and tags the
// block `diagram`, and Ctrl+Z takes both back in one step
// (tst_integration's test_69h4, through the keyboard).
func TestCtrlVStraightensADiagramPastedIntoCode(t *testing.T) {
	s, e := openEditor(t, "```\n```\n")
	s.Do(func() {
		unison.ClipboardSetText(crookedDrawing)
		e.FocusBlock(0, 0)
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		b := e.Doc.Blocks[0]
		if b.Lang != "diagram" || b.Text != straightDrawing {
			t.Errorf("after the paste: language %q, text:\n%s", b.Lang, b.Text)
		}
	})
	s.Screen.KeyPress(unison.KeyZ, mod.Control)
	s.Do(func() {
		if b := e.Doc.Blocks[0]; b.Lang != "" || b.Text != "" {
			t.Errorf("after one undo: language %q, text %q", b.Lang, b.Text)
		}
	})
}

// Pasting after a block selection inserts after it and selects the new
// blocks; plain pastes strip to paragraphs.
func TestPasteAfterSelectedBlocks(t *testing.T) {
	s, e := openEditor(t, "first\n\nsecond\n")
	s.Do(func() {
		unison.ClipboardSetText("# Title\n\nbody")
		e.SetBlockSelection([]int64{e.Doc.Blocks[0].ID})
		e.RequestFocus()
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		d := e.Doc
		if len(d.Blocks) != 4 {
			t.Fatalf("blocks: %s", texts(d))
		}
		if d.Blocks[1].Kind != Heading1 || d.Blocks[2].Kind != Paragraph {
			t.Errorf("inserted: %s", texts(d))
		}
		if len(e.SelectedBlocks()) != 2 {
			t.Errorf("the new blocks should be selected: %v", e.SelectedBlocks())
		}
	})
	// Plain pastes after a selection strip to paragraphs.
	s.Do(func() {
		unison.ClipboardSetText("**bold**\nplain")
		// reselect the first block; the paste above left the new blocks selected
		e.SetBlockSelection([]int64{e.Doc.Blocks[0].ID})
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyV, mod.Control|mod.Shift)
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:first | Paragraph:bold | Paragraph:plain | Heading 1:Title | Paragraph:body | Paragraph:second" {
			t.Errorf("plain after selection: %s", got)
		}
	})
}

// Pasting a picture inserts an image block: converting an empty paragraph,
// else inserting below the caret's block.
func TestPastedImageInsertsAnImageBlock(t *testing.T) {
	s, e := openEditor(t, "")
	s.Do(func() {
		e.PasteImage = func() (string, bool) { return "assets/pic.png", true }
		e.FocusBlock(0, 0)
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		if len(e.Doc.Blocks) != 1 || e.Doc.Blocks[0].Kind != Image {
			t.Fatalf("empty becomes image: %s", texts(e.Doc))
		}
	})
	s.Do(func() {
		e.FocusBlock(0, 0)
		e.Doc.Blocks[0].Kind, e.Doc.Blocks[0].Text = Paragraph, "words"
		e.FocusBlock(0, 5)
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		if got := texts(e.Doc); got != "Paragraph:words | Image:![](assets/pic.png)" {
			t.Errorf("below text: %s", got)
		}
	})
}

// A fence in the plain text is the structure source even when HTML is also
// present, so a copied fence does not arrive wrapped in a second code block.
func TestFenceFromPlainTextBeatsHTML(t *testing.T) {
	s, e := openEditor(t, "")
	s.Do(func() {
		fence := "```mermaid\nflowchart LR\n  A --> B\n```"
		unison.ClipboardSetText(fence)
		e.PasteRich = func() (string, bool) {
			return "```\n" + fence + "\n```", true
		}
		e.FocusBlock(0, 0)
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		b := e.Doc.Blocks[0]
		if b.Kind != Code || b.Lang != "mermaid" || len(e.Doc.Blocks) != 1 {
			t.Errorf("fence should win over HTML: %s", texts(e.Doc))
		}
	})
}

// Pasting a lone URL over words links them, through the keyboard.
func TestPastedURLLinksThroughTheKeyboard(t *testing.T) {
	s, e := openEditor(t, "say hello here")
	s.Do(func() {
		unison.ClipboardSetText("https://example.com")
		e.FocusBlock(0, 4)
		d := e.Doc
		d.Anchor = Pos{d.Blocks[0].ID, 4}
		d.Caret = Pos{d.Blocks[0].ID, 9}
	})
	s.Sync()
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Do(func() {
		if got := e.Doc.Blocks[0].Text; got != "say [hello](https://example.com) here" {
			t.Errorf("linked: %q", got)
		}
	})
}

// The language menu's "Text diagram" straightens the block's drawing, and
// its "Plain code" tags the block `plain` (test_69h5).
func TestLanguageMenuDeclaresAndOptsOut(t *testing.T) {
	crooked := "┌──────────┐\n│ ab    │\n│ cd    │\n└───────┘"
	straight := "┌───────┐\n│ ab    │\n│ cd    │\n└───────┘"
	s, e := openEditor(t, "```python\n"+crooked+"\n```\n")
	choose := func(name string) {
		s.Do(func() {
			for _, it := range e.languageItems(e.Doc.Blocks[0].ID) {
				if it.Text == name {
					it.OnSelect()
					return
				}
			}
			t.Errorf("no %q in the language menu", name)
		})
	}
	choose("Text diagram")
	s.Do(func() {
		if b := e.Doc.Blocks[0]; b.Lang != "diagram" || b.Text != straight {
			t.Errorf("Text diagram: language %q, text:\n%s", b.Lang, b.Text)
		}
	})
	choose("Plain code")
	s.Do(func() {
		if b := e.Doc.Blocks[0]; b.Lang != "plain" || b.Text != straight {
			t.Errorf("Plain code: language %q, text:\n%s", b.Lang, b.Text)
		}
		if got := Serialize(e.Doc.Blocks); !strings.HasPrefix(got, "```plain\n") {
			t.Errorf("written as:\n%s", got)
		}
	})
}

// Tab in a code block pads to the next four-column stop, Shift+Tab takes a
// stop back off, and over several lines they indent or outdent every line
// touched (the indentCodeLines).
func TestTabStopsInCode(t *testing.T) {
	const body = "  ab\ncd\n\n  ef\n"
	s, e := openEditor(t, "```\n"+body+"```")
	setCode := func(text string, anchor, caret int) {
		s.Do(func() {
			d := e.Doc
			id := d.Blocks[0].ID
			d.Edit("test", func() { d.Blocks[0].Text = text })
			d.Anchor = Pos{id, anchor}
			d.Caret = Pos{id, caret}
			d.Focused = true
		})
		s.Sync()
	}
	code := func() string {
		var text string
		s.Do(func() { text = e.Doc.Blocks[0].Text })
		return text
	}
	// Caret at column 2 pads two spaces to the next stop.
	setCode(body, 2, 2)
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	if got := code(); got != "    ab\ncd\n\n  ef\n" {
		t.Errorf("tab pads to the stop: %q", got)
	}
	s.Do(func() {
		if a, c := e.Doc.Anchor.Off, e.Doc.Caret.Off; a != 4 || c != 4 {
			t.Errorf("caret after the pad: %d/%d", a, c)
		}
	})
	// Shift+Tab takes one stop back off the line.
	setCode(body, 2, 2)
	s.Screen.KeyPress(unison.KeyTab, mod.Shift)
	if got := code(); got != "ab\ncd\n\n  ef\n" {
		t.Errorf("shift+tab outdents: %q", got)
	}
	// From column zero Tab and Shift+Tab round-trip.
	setCode(body, 0, 0)
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	if got := code(); got != "      ab\ncd\n\n  ef\n" {
		t.Errorf("tab at column zero: %q", got)
	}
	s.Screen.KeyPress(unison.KeyTab, mod.Shift)
	if got := code(); got != body {
		t.Errorf("shift+tab restores: %q", got)
	}
	// A selection over lines indents every non-blank line it touches.
	setCode(body, 0, 8)
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	if got := code(); got != "      ab\n    cd\n\n  ef\n" {
		t.Errorf("tab indents touched lines: %q", got)
	}
	s.Screen.KeyPress(unison.KeyTab, mod.Shift)
	if got := code(); got != body {
		t.Errorf("shift+tab outdents touched lines: %q", got)
	}
	// A selection inside one line is replaced by the pad.
	setCode(body, 2, 4)
	s.Screen.KeyPress(unison.KeyTab, mod.None)
	if got := code(); got != "    \ncd\n\n  ef\n" {
		t.Errorf("tab replaces an in-line selection: %q", got)
	}
}

// The [[ list stays shut inside inline math and code, as Kvit's does.
func TestWikiMenuStaysShutInMath(t *testing.T) {
	s, e := openEditor(t, "cost $x + [[y]]$ here")
	s.Do(func() {
		e.CompleteLink = func(string) []Completion { return []Completion{{Label: "n", Insert: "n"}} }
		d := e.Doc
		id := d.Blocks[0].ID
		// Inside the math span.
		d.SetCaret(id, 9)
		d.Focused = true
		if _, _, ok := e.wikiQuery(); ok {
			t.Error("no completion inside $…$")
		}
		// Outside it, after "[[", it opens.
		d.Edit("test", func() { d.Blocks[0].Text = "see [[pl" })
		d.SetCaret(id, len([]rune(d.Blocks[0].Text)))
		d.Anchor = d.Caret
		if q, _, ok := e.wikiQuery(); !ok || q != "pl" {
			t.Errorf("plain text still completes: %q %v", q, ok)
		}
		// Inside a code span it stays shut.
		d.Edit("test", func() { d.Blocks[0].Text = "a `code [[z]]` end" })
		d.SetCaret(id, 11)
		d.Anchor = d.Caret
		if _, _, ok := e.wikiQuery(); ok {
			t.Error("no completion inside code")
		}
	})
}

// The code panel names Ctrl+Enter in its footer while the caret is in the
// block, as Kvit's BlockKeyHint does: the footer's pixels change with the
// caret.
func TestCodeFooterHint(t *testing.T) {
	s, e := openEditor(t, "```\nx = 1\n```")
	footer := func() geom.Rect {
		var r geom.Rect
		s.Do(func() {
			p := e.codePanel(0)
			r = geom.NewRect(p.Right()-e.px(150), p.Bottom()-e.px(codeFooter), e.px(150), e.px(codeFooter))
		})
		return r
	}
	shot := func() []uint8 {
		var out []uint8
		s.Do(func() {
			img := s.Screen.CaptureWindow(s.Window.Window)
			if img == nil {
				return
			}
			r := footer()
			off := s.Screen.PanelPoint(e, geom.NewPoint(0, 0))
			b := img.Bounds()
			for y := int(r.Y); y < int(r.Bottom()); y++ {
				for x := int(r.X); x < int(r.Right()); x++ {
					px, py := x+int(off.X), y+int(off.Y)
					if px < b.Min.X || py < b.Min.Y || px >= b.Max.X || py >= b.Max.Y {
						continue
					}
					c := img.NRGBAAt(px, py)
					out = append(out, c.R, c.G, c.B, c.A)
				}
			}
		})
		return out
	}
	s.Do(func() { e.FocusBlock(0, 0) })
	s.Sync()
	r := footer()
	with := shot()
	s.Do(func() { e.ClearFocus() })
	s.Sync()
	without := shot()
	if len(with) == 0 || len(without) == 0 || len(with) != len(without) {
		t.Fatalf("no footer pixels: %d vs %d in %v", len(with), len(without), r)
	}
	var diff int
	for k := range with {
		if with[k] != without[k] {
			diff++
		}
	}
	if diff < 100 {
		t.Errorf("the footer should name Ctrl+Enter with the caret in it: %d bytes differ", diff)
	}
}

// Ctrl+Backspace and Ctrl+Delete take a word and plain Backspace one
// character; on macOS Option takes the word.
func TestCtrlBackspaceAndDeleteTakeAWord(t *testing.T) {
	s, e := openEditor(t, "hello big world\n")
	text := func() (got string) {
		s.Do(func() { got = e.Doc.Blocks[0].Text })
		return got
	}
	s.Do(func() { e.FocusBlock(0, len("hello big world")) })
	s.Sync()
	s.Screen.KeyPress(unison.KeyBackspace, mod.Control)
	if got := text(); got != "hello big " {
		t.Errorf("Ctrl+Backspace left %q", got)
	}
	s.Screen.KeyPress(unison.KeyBackspace, mod.None)
	if got := text(); got != "hello big" {
		t.Errorf("Backspace left %q", got)
	}
	s.Do(func() { e.FocusBlock(0, 0) })
	s.Screen.KeyPress(unison.KeyDelete, mod.Control)
	if got := text(); got != " big" {
		t.Errorf("Ctrl+Delete left %q", got)
	}
	s.Do(func() {
		optionDeletesWord = true
		e.FocusBlock(0, len(" big"))
	})
	defer s.Do(func() { optionDeletesWord = false })
	s.Screen.KeyPress(unison.KeyBackspace, mod.Option)
	if got := text(); got != " " {
		t.Errorf("Option+Backspace on macOS left %q", got)
	}
}
