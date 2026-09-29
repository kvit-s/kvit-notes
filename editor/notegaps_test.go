package editor

import (
	"math"
	"path/filepath"
	"testing"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// Three places where the note editor followed a different rule from  Kvit
// Notes: a line break typed with Shift+Enter, the line that closes a code
// fence, and the spacing of a paragraph's lines with the placement of a
// picture.

// features.md 1.2.1: Shift+Enter breaks the line inside a paragraph rather
// than starting a block, and typing a line break never splits a block, while
// pasting several lines still makes a paragraph of each.
func TestShiftEnterBreaksTheLineInsideTheBlock(t *testing.T) {
	s, e := openEditor(t, "first line\n\n# Heading\n\n- one\n")
	s.Do(func() { e.FocusBlock(0, len("first line")) })
	s.Screen.KeyPress(unison.KeyReturn, mod.Shift)
	s.Screen.Type("second line")
	s.Do(func() {
		if len(e.Doc.Blocks) != 3 || e.Doc.Blocks[0].Text != "first line\nsecond line" {
			t.Errorf("Shift+Enter in a paragraph: %d blocks, the first %q", len(e.Doc.Blocks), e.Doc.Blocks[0].Text)
		}
		// A heading is one line, so there Shift+Enter is Enter.
		e.FocusBlock(1, len("Heading"))
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Shift)
	s.Do(func() {
		if len(e.Doc.Blocks) != 4 || e.Doc.Blocks[1].Text != "Heading" || e.Doc.Blocks[2].Kind != Paragraph {
			t.Errorf("Shift+Enter on a heading: %s", Serialize(e.Doc.Blocks))
		}
		e.FocusBlock(3, len("one"))
	})
	// A list item takes a continuation line, and no empty one, which would
	// read back as a second block.
	s.Screen.KeyPress(unison.KeyReturn, mod.Shift)
	s.Screen.KeyPress(unison.KeyReturn, mod.Shift)
	s.Do(func() {
		if len(e.Doc.Blocks) != 4 || e.Doc.Blocks[3].Text != "one\n" {
			t.Errorf("Shift+Enter twice in a list item: %d blocks, %q", len(e.Doc.Blocks), e.Doc.Blocks[3].Text)
		}
	})

	d := NewDoc(ParseMarkdown("ab\n"))
	d.SetCaret(d.Blocks[0].ID, 1)
	d.InsertText("\n")
	if len(d.Blocks) != 1 || d.Blocks[0].Text != "a\nb" {
		t.Errorf("a typed line break split the block: %s", Serialize(d.Blocks))
	}
	d.SetCaret(d.Blocks[0].ID, 1)
	d.Paste("x\ny", false)
	if len(d.Blocks) != 2 {
		t.Errorf("pasted lines stayed in one block: %s", Serialize(d.Blocks))
	}
}

// A code fence closes only on a line of the opener's character alone, at
// least as long as the opener, with nothing but spaces around it
// (DocumentSerializer's isClosingFence), so a line that only starts with the
// fence is code.
func TestAFenceClosesOnlyOnALineOfFenceCharacters(t *testing.T) {
	for _, c := range []struct {
		name, md string
		want     []string // each block's kind and text
	}{
		{"a line starting with the fence is code", "```\n``` | y |\n```\n",
			[]string{"code:``` | y |"}},
		{"a shorter run does not close a longer fence", "````\n```python\nprint(1)\n```\n````\n",
			[]string{"code:```python\nprint(1)\n```"}},
		{"a longer run closes a shorter fence", "```\ncode\n`````\nafter\n",
			[]string{"code:code", "para:after"}},
		{"the other character does not close", "~~~\n```\n~~~\n",
			[]string{"code:```"}},
		{"spaces around the closer", "```go\nx := 1\n  ```  \nafter\n",
			[]string{"code:x := 1", "para:after"}},
		{"an unclosed fence runs to the end of the file, its last line break included", "```\nopen\n``` not a closer\n",
			[]string{"code:open\n``` not a closer\n"}},
		{"a backtick info string with a backtick is no fence", "```a`b\n",
			[]string{"para:```a`b"}},
		{"a tag after the closer still closes it", "```go\nx\n```  <!--kvit wrap-->\nafter\n",
			[]string{"code:x", "para:after"}},
	} {
		blocks := ParseMarkdown(c.md)
		var got []string
		for _, b := range blocks {
			k := "para"
			if b.Kind == Code {
				k = "code"
			}
			got = append(got, k+":"+b.Text)
		}
		if len(got) != len(c.want) {
			t.Errorf("%s: %q", c.name, got)
			continue
		}
		for i := range got {
			if got[i] != c.want[i] {
				t.Errorf("%s: block %d is %q, want %q", c.name, i, got[i], c.want[i])
			}
		}
	}
	// The legacy tag after a closer is the fence's when the opener has none.
	if b := ParseMarkdown("```go\nx\n```  <!--kvit wrap-->\n")[0]; b.Attrs != "wrap" {
		t.Errorf("the legacy closer's tag was not kept: %q", b.Attrs)
	}
	// Content that holds a fence-shaped line comes back from a save.
	md := "```\n``` | y |\n```\n"
	if got := Serialize(ParseMarkdown(md)); got != md {
		t.Errorf("the fence came back as\n%s", got)
	}
}

// the text documents space a block's lines by the line height times the
// font's own line height (QTextBlockFormat's ProportionalHeight): 22.1 px at
// 14 px and 1.3, 18 px at 15 px and 1.0, where the font size rule gave 18.2
// and 15.
func TestANoteSpacesLinesByTheFontsLineHeight(t *testing.T) {
	s, e := openEditor(t, accessNote)
	s.Do(func() {
		for _, c := range []struct {
			size int
			lh   float64
		}{{14, 1.3}, {15, 1.0}, {20, 1.0}} {
			e.ui.Typography.SetBaseSize(c.size)
			e.ui.Typography.SetLineHeight(c.lh)
			st := e.blockStyle(&e.Doc.Blocks[1])
			_, natural := e.ui.Fonts.Layout([]text.Span{{Text: " ", Style: st}}, text.Options{}).Size()
			want := natural * float32(c.lh)
			if got := e.pitch(st); math.Abs(float64(got-want)) > 1e-3 {
				t.Errorf("at %d px and %v the lines are %v apart, want %v", c.size, c.lh, got, want)
			}
			if natural != float32(math.Ceil(float64(natural))) || natural <= float32(c.size) {
				t.Errorf("at %d px the font's line height is %v", c.size, natural)
			}
		}
	})
}

// features.md 9.2: a picture is centred unless its block is aligned left or
// right (ImageBlock, imageAlign).
func TestANoteCentresItsPictures(t *testing.T) {
	root := t.TempDir()
	writePicture(t, filepath.Join(root, "p.png"))
	s, e := openEditor(t, "![](p.png)\n\n![](p.png)  <!--kvit align=left-->\n\n![](p.png)  <!--kvit align=right-->\n")
	s.Do(func() {
		e.LoadImage = PictureFolders{Base: root}.Loader()
		e.ClearPictures()
		o, w := e.textOrigin(0).X, e.textWidth(&e.Doc.Blocks[0])
		centre, left, right := e.pictureRect(0), e.pictureRect(1), e.pictureRect(2)
		if centre.Width >= w || centre.X != o+(w-centre.Width)/2 {
			t.Errorf("the picture is at %v in text from %v, %v wide", centre, o, w)
		}
		if left.X != o || right.Right() != o+w {
			t.Errorf("left %v, right %v in text from %v to %v", left, right, o, o+w)
		}
	})
}
