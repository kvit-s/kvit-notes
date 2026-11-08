package editor

import (
	"testing"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/kvit-s/kvit-ui/uitest"
)

// An embedding's ink: the colour of the ordinary text of a document drawn
// among softer interface text, as kvit-hub draws its conversations.

const inkNote = `Words of a paragraph.

# A heading of words

- An item of a list

| One | Two |
| --- | --- |
| Three | Four |

> Words of a quote.

- [x] A finished task
`

// The rows of inkNote, and whether each takes the embedding's ink.
var inkRows = []struct {
	kind  Kind
	inked bool
}{
	{Paragraph, true}, {Heading1, false}, {Bullet, true}, {Table, true}, {Quote, false}, {Todo, false},
}

// reddish reports whether a colour of 8-bit channels leans to red: text
// drawn in the theme's Danger, or blended from it at a glyph's edge, and
// nothing grey.
func reddish(r, g, b uint32) bool { return r > g+48 && r > b+48 }

// redPixels counts the reddish pixels of block i's row as the window draws it.
func redPixels(s *uitest.Session, e *Editor, i int) int {
	var x0, y0, x1, y1 int
	s.Do(func() {
		r := e.rowRect(i)
		a := s.Screen.PanelPoint(e, r.Point)
		sc := s.Screen.Scale()
		x0, y0 = int(a.X*sc), int(a.Y*sc)
		x1, y1 = int((a.X+r.Width)*sc), int((a.Y+r.Height)*sc)
	})
	img := s.Capture()
	n := 0
	for y := y0; y < y1; y++ {
		for x := x0; x < x1; x++ {
			if r, g, b, _ := img.At(x, y).RGBA(); reddish(r>>8, g>>8, b>>8) {
				n++
			}
		}
	}
	return n
}

func TestAnEmbeddingDrawsOrdinaryTextInItsInk(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, inkNote, 0)
	e := d.Editor
	s.Sync()
	s.Do(func() {
		if len(e.Doc.Blocks) != len(inkRows) {
			t.Fatalf("the note has %d blocks, want %d:\n%s", len(e.Doc.Blocks), len(inkRows), Serialize(e.Doc.Blocks))
		}
		for i, row := range inkRows {
			if b := e.Doc.Blocks[i]; b.Kind != row.kind || (b.Kind == Todo && !b.Checked) {
				t.Fatalf("block %d is a %v (checked %v), want a %v", i, b.Kind, b.Checked, row.kind)
			}
		}
	})
	// With no ink of its own the document draws nothing red, so every red
	// pixel later is the ink's.
	for i := range inkRows {
		if n := redPixels(s, e, i); n != 0 {
			t.Fatalf("with no ink, the %v row has %d red pixels", inkRows[i].kind, n)
		}
	}

	// Set after the rows were first laid out, as a host does through
	// Embedded and Embed, so the laid-out rows and the table's grid must be
	// laid out again.
	s.Do(func() {
		emb, _ := e.Embedded()
		emb.Ink = kvitui.InkDanger
		e.Embed(emb)
	})
	s.Sync()
	for _, theme := range tokens.BuiltInThemes() {
		s.SetTheme(theme)
		var red bool
		s.Do(func() {
			c := colour(e.tok().Danger)
			red = reddish(uint32(c.R), uint32(c.G), uint32(c.B))
			for i, row := range inkRows {
				want := e.tok().TextPrimary
				switch {
				case row.inked:
					want = e.tok().Danger
				case row.kind == Quote:
					want = e.tok().TextSecondary
				case row.kind == Todo:
					want = e.tok().TextFaint
				}
				if row.kind == Table {
					continue // a grid has no one layout; its pixels are counted below
				}
				if got := e.layout(i).style.Color; got != colour(want) {
					t.Errorf("%s: the %v row is laid out in %v, want %v", theme, row.kind, got, colour(want))
				}
			}
		})
		if !red {
			t.Logf("%s: the theme's Danger is not red enough to count, so its pixels are not checked", theme)
			continue
		}
		for i, row := range inkRows {
			n := redPixels(s, e, i)
			if row.inked && n < 20 {
				t.Errorf("%s: the %v row has %d pixels in the ink", theme, row.kind, n)
			}
			if !row.inked && n != 0 {
				t.Errorf("%s: the %v row has %d pixels in the ink, and should have none", theme, row.kind, n)
			}
		}
	}

	// Taking the ink away again draws the document as before.
	s.Do(func() {
		emb, _ := e.Embedded()
		emb.Ink = nil
		e.Embed(emb)
	})
	s.Sync()
	for i := range inkRows {
		if n := redPixels(s, e, i); n != 0 {
			t.Errorf("with the ink taken away, the %v row has %d red pixels", inkRows[i].kind, n)
		}
	}
}

// A message box takes the ink as a document does.
func TestAMessageBoxTakesTheInk(t *testing.T) {
	s, c := openCompact(t, CompactOptions{})
	s.Do(func() { c.SetMarkdown("one\n\n- two\n\n## three") })
	s.Sync()
	s.Do(func() {
		e := c.Editor
		emb, _ := e.Embedded()
		emb.Ink = kvitui.InkTextSecondary
		e.Embed(emb)
		tk := e.tok()
		for i, want := range []palette.Color{tk.TextSecondary, tk.TextSecondary, tk.TextPrimary} {
			if got := e.layout(i).style.Color; got != colour(want) {
				t.Errorf("block %d of the message box is in %v, want %v", i, got, colour(want))
			}
		}
	})
}

// A note editor, which nobody embeds, draws its text in the theme's
// TextPrimary.
func TestANoteDrawsItsTextInTextPrimary(t *testing.T) {
	s, e := openEditor(t, inkNote)
	s.Do(func() {
		if _, ok := e.Embedded(); ok {
			t.Fatal("the note editor is embedded")
		}
		for i, row := range inkRows {
			if row.inked && row.kind != Table {
				if got, want := e.layout(i).style.Color, colour(e.tok().TextPrimary); got != want {
					t.Errorf("the note's %v row is in %v, want TextPrimary %v", row.kind, got, want)
				}
			}
		}
	})
}
