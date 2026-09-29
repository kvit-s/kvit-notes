package editor

import (
	"image"
	"image/color"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// The message box, after the Qt core's tests/test_embeddededitor.cpp and
// Kvit Works' tests/agent/test_composerassets.cpp.

func openCompact(t *testing.T, o CompactOptions) (*uitest.Session, *Compact) {
	t.Helper()
	var c *Compact
	s := uitest.Open(t, uitest.Options{Width: 700, Height: 700}, func(ui *kvitui.UI) unison.Paneler {
		c = NewCompact(ui, o)
		return c
	})
	return s, c
}

// writePicture writes a small PNG and answers its path.
func writePicture(t *testing.T, path string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		t.Fatal(err)
	}
	img := image.NewNRGBA(image.Rect(0, 0, 40, 30))
	for x := range 40 {
		for y := range 30 {
			img.Set(x, y, color.NRGBA{R: 200, A: 255})
		}
	}
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if err := png.Encode(f, img); err != nil {
		t.Fatal(err)
	}
}

// pictureBox is a message box whose paste stores a picture in a folder of
// its own, as Kvit Works' box stores one in the project's pasted folder.
func pictureBox(t *testing.T) (*uitest.Session, *Compact) {
	dir := t.TempDir()
	writePicture(t, filepath.Join(dir, "assets", "message-shot.png"))
	return openCompact(t, CompactOptions{
		Placeholder: "Message",
		Pictures:    PictureFolders{Base: dir},
		PasteImage:  func() (string, bool) { return "assets/message-shot.png", true },
	})
}

func (c *Compact) testText() string { return strings.TrimSpace(c.Markdown()) }

func sent(c *Compact) *[]string {
	var out []string
	c.OnSubmit = func(md string) { out = append(out, strings.TrimSpace(md)) }
	return &out
}

func TestEnterSendsInsteadOfMakingTheNextBlock(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	got := sent(c)
	s.Do(func() {
		c.SetMarkdown("a message")
		c.Focus()
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if len(*got) != 1 || (*got)[0] != "a message" || c.RowCount() != 1 {
			t.Errorf("sent %q, %d rows", *got, c.RowCount())
		}
		c.SetReturnSubmits(false)
		c.Focus()
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if c.RowCount() != 2 || len(*got) != 1 {
			t.Errorf("with Enter given back: %d rows, %d sent", c.RowCount(), len(*got))
		}
	})
}

func TestShiftEnterBreaksTheLineInsideTheMessage(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	got := sent(c)
	s.Do(func() {
		c.SetMarkdown("first line")
		c.Focus()
		c.FocusBlock(0, true)
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Shift)
	s.Screen.Type("second line")
	s.Do(func() {
		if len(*got) != 0 || c.RowCount() != 1 || !strings.Contains(c.Markdown(), "second line") {
			t.Errorf("sent %q, %d rows, %q", *got, c.RowCount(), c.Markdown())
		}
	})
}

func TestEnterInAListMakesTheNextItem(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	got := sent(c)
	s.Do(func() {
		c.SetMarkdown("- one")
		c.FocusBlock(0, true)
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Screen.Type("two")
	s.Do(func() {
		if len(*got) != 0 || c.testText() != "- one\n- two" {
			t.Errorf("sent %q, box %q", *got, c.testText())
		}
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if c.RowCount() != 3 {
			t.Errorf("%d rows after Enter on an item", c.RowCount())
		}
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if len(*got) != 0 || c.testText() != "- one\n- two" {
			t.Errorf("Enter on the empty item: sent %q, box %q", *got, c.testText())
		}
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if len(*got) != 1 || !strings.Contains((*got)[0], "- two") {
			t.Errorf("Enter on the paragraph after the list sent %q", *got)
		}
	})
}

func TestCtrlEnterSendsFromAnyBlock(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	got := sent(c)
	s.Do(func() {
		c.SetMarkdown("- an item")
		c.FocusBlock(0, true)
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		if len(*got) != 1 || c.RowCount() != 1 {
			t.Errorf("from a list item: %d sent, %d rows", len(*got), c.RowCount())
		}
		c.SetMarkdown("```\ncode\n```")
		c.FocusBlock(0, true)
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		if len(*got) != 2 || c.RowCount() != 1 {
			t.Errorf("from a code block: %d sent, %d rows", len(*got), c.RowCount())
		}
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if len(*got) != 2 || c.Editor.Doc.Blocks[0].Text != "code\n" {
			t.Errorf("Enter in a code block: %d sent, %q", len(*got), c.Editor.Doc.Blocks[0].Text)
		}
		c.SetReturnSubmits(false)
		c.FocusBlock(0, true)
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		if c.RowCount() != 2 || len(*got) != 2 {
			t.Errorf("with Enter given back, Ctrl+Enter leaves the code: %d rows, %d sent", c.RowCount(), len(*got))
		}
	})
}

func TestTheBoxGrowsWithItsContentAndStopsAtItsCap(t *testing.T) {
	s, c := openCompact(t, CompactOptions{MaximumLines: 6, Margin: 6})
	height := func(md string) float32 {
		var h float32
		s.Do(func() { c.SetMarkdown(md) })
		s.Sync()
		s.Do(func() { h = c.FrameRect().Height })
		return h
	}
	one := height("one line")
	three := height("one line\n\ntwo\n\nthree")
	if one <= 0 || three <= one {
		t.Errorf("one line %v, three blocks %v", one, three)
	}
	var many strings.Builder
	for i := range 40 {
		many.WriteString("line ")
		many.WriteString(strings.Repeat("x", i%5+1))
		many.WriteString("\n\n")
	}
	capped := height(many.String())
	s.Do(func() {
		if want := 6*c.LineUnit() + 2*c.Editor.px(6); capped != want {
			t.Errorf("capped at %v, want %v", capped, want)
		}
		if c.ContentHeight() <= capped {
			t.Errorf("forty blocks (%v) fit under the cap (%v)", c.ContentHeight(), capped)
		}
	})
}

func TestTheBoxDrawsNoGutter(t *testing.T) {
	s, c := openCompact(t, CompactOptions{Placeholder: "Message the agent..."})
	s.Do(func() {
		e := c.Editor
		if e.bodyLeft() != e.side()+e.px(focusBar) || e.side() != 0 {
			t.Errorf("the text starts %v in, the side is %v", e.bodyLeft(), e.side())
		}
		if e.Placeholder != "Message the agent..." || e.Accessibility.Name != "Message the agent..." {
			t.Errorf("placeholder %q, name %q", e.Placeholder, e.Accessibility.Name)
		}
		if !c.Empty() || c.RowCount() != 1 {
			t.Errorf("a new box holds %d rows", c.RowCount())
		}
	})
	var row geom.Rect
	s.Do(func() { row = c.Editor.RowRect(0) })
	s.Screen.MouseMove(s.Screen.PanelPoint(c.Editor, geom.NewPoint(row.X+4, row.Y+8)), mod.None)
	s.Do(func() {
		if _, part := c.Editor.partAt(geom.NewPoint(row.X+4, row.Y+8)); part != partNone {
			t.Errorf("the pointer found gutter control %v", part)
		}
	})
}

func TestMarkdownGoesInAndComesBackOut(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	message := "# A heading\n\nA paragraph with **bold** in it.\n\n- one\n- two\n\n```py\nprint(\"hello\")\n```\n"
	s.Do(func() {
		c.SetMarkdown(message)
		if got := c.Markdown(); got != message {
			t.Errorf("came back as\n%s", got)
		}
		if c.Editor.Doc.UndoSteps() != 0 {
			t.Error("loading a document left an undo step")
		}
	})
}

func TestAPastedPictureLeavesSomewhereToType(t *testing.T) {
	s, c := pictureBox(t)
	s.Do(func() { c.Focus() })
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Sync()
	s.Do(func() {
		if c.RowCount() != 2 || !c.CaretInside() || c.Empty() {
			t.Errorf("%d rows, caret inside %v, empty %v", c.RowCount(), c.CaretInside(), c.Empty())
		}
		if c.Editor.Doc.Caret.Block != c.Editor.Doc.Blocks[1].ID {
			t.Error("the caret is on the picture, where nothing can be typed")
		}
	})
	s.Screen.Type("what is wrong here?")
	s.Do(func() {
		if md := c.testText(); !strings.Contains(md, ".png)") || !strings.HasSuffix(md, "what is wrong here?") {
			t.Errorf("the message is %q", md)
		}
	})
}

func TestThePictureMessageIsSentWithEnter(t *testing.T) {
	s, c := pictureBox(t)
	got := sent(c)
	s.Do(func() { c.Focus() })
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Sync()
	s.Screen.Type("look")
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	if len(*got) != 1 || !strings.Contains((*got)[0], ".png)") || !strings.HasSuffix((*got)[0], "look") {
		t.Errorf("sent %q", *got)
	}
}

// A picture on its own is a message too: Enter in the empty paragraph under
// it sends it.
func TestAPictureAloneIsSentWithEnter(t *testing.T) {
	s2, alone := pictureBox(t)
	gotAlone := sent(alone)
	s2.Do(func() { alone.Focus() })
	s2.Screen.KeyPress(unison.KeyV, mod.Control)
	s2.Sync()
	s2.Screen.KeyPress(unison.KeyReturn, mod.None)
	if len(*gotAlone) != 1 || !strings.Contains((*gotAlone)[0], ".png)") {
		t.Errorf("the picture alone sent %q", *gotAlone)
	}
}

func TestThePictureCanBeDeletedAgain(t *testing.T) {
	s, c := pictureBox(t)
	s.Do(func() { c.Focus() })
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Sync()
	s.Screen.KeyPress(unison.KeyBackspace, mod.None)
	s.Sync()
	s.Do(func() {
		if c.RowCount() != 1 || !c.Editor.pictureHoldsKeyboard() {
			t.Errorf("after the first Backspace: %d rows, the picture holds the keyboard %v", c.RowCount(), c.Editor.pictureHoldsKeyboard())
		}
	})
	s.Screen.KeyPress(unison.KeyBackspace, mod.None)
	s.Sync()
	s.Do(func() {
		if c.RowCount() != 1 || !c.Empty() || !c.CaretInside() {
			t.Errorf("after the second: %d rows, empty %v, caret inside %v", c.RowCount(), c.Empty(), c.CaretInside())
		}
	})
	s.Screen.Type("never mind")
	s.Do(func() {
		if c.testText() != "never mind" {
			t.Errorf("the box holds %q", c.testText())
		}
	})
}

func TestAPicturePastedBetweenTwoRowsAddsNoParagraph(t *testing.T) {
	s, c := pictureBox(t)
	s.Do(func() {
		c.SetMarkdown("one\n\ntwo")
		c.FocusBlock(0, false)
	})
	s.Screen.KeyPress(unison.KeyV, mod.Control)
	s.Sync()
	s.Do(func() {
		if c.RowCount() != 3 || !c.Editor.pictureHoldsKeyboard() {
			t.Errorf("%d rows, the picture holds the keyboard %v", c.RowCount(), c.Editor.pictureHoldsKeyboard())
		}
		// No line under the picture for its path and alt text.
		if _, ok, shows := c.Editor.pictureBlock(1); !ok || shows {
			t.Error("the picture with the keyboard shows its line")
		}
	})
	s.Screen.KeyPress(unison.KeyBackspace, mod.None)
	s.Sync()
	s.Do(func() {
		if c.RowCount() != 2 || c.testText() != "one\n\ntwo" {
			t.Errorf("%d rows: %q", c.RowCount(), c.testText())
		}
	})
}

func TestAPictureHoldingTheKeyboardTakesNoLetters(t *testing.T) {
	s, c := pictureBox(t)
	s.Do(func() {
		c.SetMarkdown("one\n\n![](assets/message-shot.png)\n\ntwo")
		c.FocusBlock(1, true)
	})
	s.Screen.Type("abc")
	s.Do(func() {
		if c.testText() != "one\n\n![](assets/message-shot.png)\n\ntwo" {
			t.Errorf("letters reached the picture: %q", c.testText())
		}
		if c.Editor.showsCaret() {
			t.Error("a text caret is drawn on the picture")
		}
	})
	s.Screen.KeyPress(unison.KeyDown, mod.None)
	s.Do(func() {
		if c.Editor.Doc.Caret.Block != c.Editor.Doc.Blocks[2].ID || !c.Editor.showsCaret() {
			t.Error("Down did not leave the picture for the row below")
		}
	})
}

func TestADraftThatIsOnlyAPictureStillTakesWords(t *testing.T) {
	s, c := pictureBox(t)
	s.Do(func() {
		c.SetMarkdown("![](assets/held.png)")
		if c.RowCount() != 2 {
			t.Errorf("%d rows", c.RowCount())
		}
		c.Focus()
	})
	s.Screen.Type("and now?")
	s.Do(func() {
		if !strings.HasSuffix(c.testText(), "and now?") {
			t.Errorf("the box holds %q", c.testText())
		}
	})
}

func TestLoadingADraftKeepsTheCaret(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	edits := 0
	c.OnEdited = func() { edits++ }
	s.Do(func() { c.Focus() })
	s.Screen.Type("typing")
	s.Do(func() {
		if edits == 0 {
			t.Error("typing was not told as an edit")
		}
		c.SetMarkdown("a quotation\n\nstored")
		d := c.Editor.Doc
		if !c.CaretInside() || d.Caret != (Pos{d.Blocks[1].ID, len("stored")}) {
			t.Errorf("the caret is at %v, inside %v", d.Caret, c.CaretInside())
		}
		before := edits
		c.Clear()
		if edits != before+1 || !c.Empty() || c.RowCount() != 1 {
			t.Errorf("clearing: %d edits told, empty %v", edits-before, c.Empty())
		}
	})
}

func TestAClickOnAPictureOpensItAndLeavesTheCaret(t *testing.T) {
	dir := t.TempDir()
	writePicture(t, filepath.Join(dir, "shot.png"))
	var opened []string
	s, c := openCompact(t, CompactOptions{
		Pictures:    PictureFolders{Base: dir},
		OpenPicture: func(path, _ string) { opened = append(opened, path) },
	})
	s.Do(func() {
		c.SetMarkdown("![](shot.png)\n\nunder it")
		c.FocusBlock(1, true)
	})
	s.Sync()
	var pic geom.Rect
	s.Do(func() { pic = c.Editor.pictureRect(0) })
	s.Screen.Click(s.Screen.PanelPoint(c.Editor, pic.Center()))
	s.Do(func() {
		if len(opened) != 1 || opened[0] != "shot.png" {
			t.Errorf("opened %q", opened)
		}
		if _, _, open := c.Editor.Lightbox(); open {
			t.Error("the editor opened its own view as well")
		}
		d := c.Editor.Doc
		if d.Caret.Block != d.Blocks[1].ID {
			t.Error("the click moved the caret onto the picture")
		}
	})
}

func TestTheBoxAnnouncesGrowthWhileTheCaretIsIn(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	grew := 0
	c.OnGrew = func() { grew++ }
	s.Do(func() { c.Focus() })
	s.Sync()
	s.Screen.Type("one")
	s.Screen.KeyPress(unison.KeyReturn, mod.Shift)
	s.Screen.Type("two")
	s.Sync()
	if grew == 0 {
		t.Error("a second line was not announced")
	}
}

// The box spaces its lines as every editor does, the font's own line height
// times the line height, so an empty box is one such line and its row's
// padding tall (50 px at 14 px and 1.3), as Qt's is.
func TestTheBoxSpacesLinesAsTheTranscriptDoes(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	s.Sync()
	s.Do(func() {
		e := c.Editor
		st := e.blockStyle(&e.Doc.Blocks[0])
		_, natural := e.ui.Fonts.Layout([]text.Span{{Text: " ", Style: st}}, text.Options{}).Size()
		pitch := natural * float32(e.ui.Typography.LineHeight())
		if got := e.pitch(st); got != pitch {
			t.Errorf("lines %v apart, want %v", got, pitch)
		}
		want := e.px(rowPadTop+rowPadBottom) + float32(math.Floor(float64(pitch)+1e-3))
		if got := c.FrameRect().Height; got != want || c.ContentHeight() != want {
			t.Errorf("an empty box is %v tall (content %v), want %v", got, c.ContentHeight(), want)
		}
	})
}
