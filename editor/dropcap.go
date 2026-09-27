package editor

// Drop caps (features.md 1.2.16): a paragraph with the attribute
// dropcap=<lines> draws its first letter enlarged beside its text while the
// caret is elsewhere, in bold, in the colour of dropcapcolor and the family
// of dropcapfont when they are set. As in the Qt app (EditableBlock.qml and
// DropCapOverlay.qml), the whole paragraph is indented by the letter's
// width and its own first letter left blank in place, and the paragraph
// shows as plain text while it is being edited.

import (
	"math"
	"strconv"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/unison"
)

// dropCap reports the lines a paragraph's drop cap spans, and whether it
// is drawn now: two lines or more, some text, and the caret elsewhere.
func (e *Editor) dropCap(b *Block) (int, bool) {
	if b.Kind != Paragraph || b.Text == "" {
		return 0, false
	}
	v, ok := b.Attr("dropcap")
	if !ok {
		return 0, false
	}
	lines, err := strconv.Atoi(v)
	if err != nil || lines < 2 {
		return 0, false
	}
	return lines, !(e.Doc.Focused && e.Doc.Caret.Block == b.ID)
}

// dropCapStyle is the enlarged letter's style.
func (e *Editor) dropCapStyle(b *Block, lines int) text.Style {
	st := e.blockStyle(b)
	st.Size = float32(math.Round(float64(st.Size) * float64(lines) * 1.15))
	st.Weight = text.Bold
	if v, ok := b.Attr("dropcapcolor"); ok {
		if c, ok := parseColor(v); ok {
			st.Color = c
		}
	}
	if v, ok := b.Attr("dropcapfont"); ok && v != "" {
		st.Family = v
	}
	return st
}

// dropCapWidth is how far a paragraph's text moves right for its drop cap.
func (e *Editor) dropCapWidth(b *Block) float32 {
	lines, on := e.dropCap(b)
	if !on {
		return 0
	}
	return float32(math.Round(float64(e.dropCapStyle(b, lines).Size)*0.72)) + 6
}

// drawDropCap draws a paragraph's enlarged first letter where its text
// would start without the drop cap.
func (e *Editor) drawDropCap(gc *unison.Canvas, i int) {
	b := &e.Doc.Blocks[i]
	lines, on := e.dropCap(b)
	if !on {
		return
	}
	letter := string([]rune(PlainText(b.Text))[:1])
	o := e.textOrigin(i)
	e.label(letter, e.dropCapStyle(b, lines)).Draw(gc, o.X-e.dropCapWidth(b)+2, o.Y)
}
