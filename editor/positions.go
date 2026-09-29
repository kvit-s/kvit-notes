package editor

// Where things are in a block's text, and on the screen.
//
// A block's text has two coordinate systems. Its Markdown is what the block
// holds and what is saved: "This is **bold** text", 21 characters. Its
// display text is what a reader sees with every inline marker hidden: "This
// is bold text", 17. Something stored against a passage of a document (a
// comment, a conversation's anchor) is stored in Markdown offsets, which
// survive the file being opened again, while a range a reader points at
// arrives in display offsets. MarkdownPosition and DisplayPosition translate
// between the two, as the Qt core's src/domain/blockpositions.h does: a
// verbatim block (code, a table, an equation, a picture's line) is its own
// display text.

import (
	"github.com/richardwilkes/toolbox/v2/geom"
)

// verbatim reports whether a block's display text is its Markdown.
func verbatim(k Kind) bool { return !k.HasInline() }

// hiddenProjection is block text with every inline marker hidden, which is
// how a block is drawn when the caret is not in it.
func hiddenProjection(src []rune) projection {
	return project(src, parseInline(src), nil)
}

// MarkdownPosition is the offset in block i's Markdown that display offset
// displayPos stands for. displayPos is clamped to the display text first; a
// position at or past its end is the end of the Markdown, after any closing
// marker. Display offset 0 of "**bold**" is Markdown offset 2, where its
// first visible character is. An index the document does not hold answers 0.
func (d *Doc) MarkdownPosition(i, displayPos int) int {
	if i < 0 || i >= len(d.Blocks) {
		return 0
	}
	b := &d.Blocks[i]
	src := runes(b.Text)
	pos := max(0, displayPos)
	if verbatim(b.Kind) {
		return min(pos, len(src))
	}
	p := hiddenProjection(src)
	if pos >= len(p.D2S) {
		return len(src)
	}
	return p.D2S[pos]
}

// DisplayPosition is the display offset that Markdown offset mdPos of block i
// stands for: the reverse of MarkdownPosition. An offset inside a hidden
// marker goes to the nearest edge of the text the marker wraps.
func (d *Doc) DisplayPosition(i, mdPos int) int {
	if i < 0 || i >= len(d.Blocks) {
		return 0
	}
	b := &d.Blocks[i]
	src := runes(b.Text)
	pos := min(max(0, mdPos), len(src))
	if verbatim(b.Kind) {
		return pos
	}
	return hiddenProjection(src).S2D[pos]
}

// DisplayText is block i's text as a reader sees it with every marker
// hidden, which is what display offsets count in.
func (d *Doc) DisplayText(i int) string {
	if i < 0 || i >= len(d.Blocks) {
		return ""
	}
	b := &d.Blocks[i]
	if verbatim(b.Kind) {
		return b.Text
	}
	return string(hiddenProjection(runes(b.Text)).Disp)
}

// displayToSource turns a display range of block b into a Markdown range:
// from the first character's offset to just after the last character's, so
// a run ending at a closing marker does not take the marker in.
func displayToSource(b *Block, start, end int) (from, to int) {
	src := runes(b.Text)
	start, end = max(0, start), max(0, end)
	if verbatim(b.Kind) {
		return min(start, len(src)), min(end, len(src))
	}
	p := hiddenProjection(src)
	n := len(p.D2S)
	start, end = min(start, n), min(end, n)
	from = len(src)
	if start < n {
		from = p.D2S[start]
	}
	to = from
	if end > start {
		to = p.D2S[end-1] + 1
	}
	return from, to
}

// Where things are on the screen, in the editor's own coordinates.

// CharRect is where a caret before Markdown offset off of block i is drawn:
// its x, and the top and height of its line. It is what places something
// beside a character, such as the actions offered at the end of a selection.
// A block that draws no text answers its row; an index the document does not
// hold answers an empty rectangle.
func (e *Editor) CharRect(i, off int) geom.Rect {
	if i < 0 || i >= len(e.tops) {
		return geom.Rect{}
	}
	if !e.rowDrawsText(i) {
		return e.bodyRect(i)
	}
	l := e.layout(i)
	x, top, h := l.text.CaretAt(l.drawn(off))
	o := e.textOrigin(i)
	return geom.NewRect(o.X+x, o.Y+top, 1, h)
}

// BlockRect is block i's row with the containers drawn after it, which is
// the space the block takes in the document; empty for an index the document
// does not hold.
func (e *Editor) BlockRect(i int) geom.Rect {
	if i < 0 || i >= len(e.tops) {
		return geom.Rect{}
	}
	r := e.rowRect(i)
	r.Height += e.decorationSpace(i)
	return r
}

// BlockAtY is the block being read at height y of the editor, and how far
// below that block's top y is: the block whose row, with the containers
// after it, holds y. The space between two rows belongs to the one below,
// which is the one being read when the top of a view falls there. It is -1
// for an empty document.
func (e *Editor) BlockAtY(y float32) (block int, offset float32) {
	n := len(e.tops)
	if n == 0 {
		return -1, 0
	}
	for i := range n {
		if e.tops[i]+e.heights[i]+e.decorationSpace(i) > y {
			return i, max(0, y-e.tops[i])
		}
	}
	return n - 1, max(0, y-e.tops[n-1])
}

// BlockTop is the top of block i's row; 0 for an index the document does not
// hold.
func (e *Editor) BlockTop(i int) float32 {
	if i < 0 || i >= len(e.tops) {
		return 0
	}
	return e.tops[i]
}

// ContentHeight is the height of the rows, from the top of the first to the
// bottom of the last with the containers after it, without the margins.
func (e *Editor) ContentHeight() float32 {
	if len(e.tops) == 0 {
		return 0
	}
	return e.total() - e.tops[0]
}

// PictureSpot is a picture an image block draws, and where.
type PictureSpot struct {
	Block     int
	Path, Alt string
	Rect      geom.Rect
}

// PictureSpots are the pictures the document's image blocks draw, in order,
// for a host that lays something over them.
func (e *Editor) PictureSpots() []PictureSpot {
	var out []PictureSpot
	for i := range e.tops {
		ref, ok, _ := e.pictureBlock(i)
		if !ok || ref.Media {
			continue
		}
		if _, _, embed := e.embedCard(i); embed {
			continue
		}
		out = append(out, PictureSpot{Block: i, Path: ref.Path, Alt: ref.Alt, Rect: e.pictureRect(i)})
	}
	return out
}
