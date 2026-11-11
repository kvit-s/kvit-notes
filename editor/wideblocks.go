package editor

// Text narrower than the editor (Embedding.TextWidth): the text blocks are
// laid out as if the editor were TextWidth wide, while a table's grid and a
// code block's panel may reach past that to the editor's right margin. Kvit
// Works uses it to keep a reply's lines to a reading column while a wide
// table in the reply grows rightward towards the conversation's edge.
//
// Without a TextWidth every function here gives the text column's own
// answer, so the note editor lays out exactly as it does without this file.

import "math"

// pageWidth is the width the text blocks are laid out in: the editor's, or
// an embedding's TextWidth when that is narrower. On paper it is the page.
func (e *Editor) pageWidth() float32 {
	w := e.width()
	if tw := e.seams.textWidth(); tw > 0 && !e.Printing {
		w = min(w, e.px(tw))
	}
	return w
}

// widens reports whether tables and code blocks may reach past the text.
func (e *Editor) widens() bool {
	return e.seams.textWidth() > 0 && !e.Printing
}

// wideRight is the right edge a table or a code panel may reach, with the
// gap kept right of either: the editor's right margin under a TextWidth, the
// text's right edge otherwise.
func (e *Editor) wideRight() float32 {
	if !e.widens() {
		return e.bodyRight()
	}
	return max(e.bodyRight(), e.width()-e.margin())
}

// wideWidth is the most a table's grid or a code block's text may take: its
// width in the text column and the room right of it.
func (e *Editor) wideWidth(b *Block) float32 {
	return e.textWidth(b) + e.wideRight() - e.bodyRight()
}

// codeTextShows reports whether block i is drawn as a code panel with its
// text in it, rather than as a table of contents, a board, a query's results
// or a diagram.
func (e *Editor) codeTextShows(i int) bool {
	return e.codeNoWrap(&e.Doc.Blocks[i]) && !e.tocShows(i) && !e.boardShows(i) && !e.queryShows(i) && !e.diagramReads(i)
}

// wideCodeViewport is the width block i's code text is shown in: the width
// other blocks wrap at, or under a TextWidth as wide as its longest line,
// from that width up to the most the block may take.
func (e *Editor) wideCodeViewport(i int) float32 {
	b := &e.Doc.Blocks[i]
	narrow := e.textWidth(b)
	if !e.widens() || !e.codeTextShows(i) {
		return narrow
	}
	// Room for the caret after the longest line's last character.
	content := float32(math.Ceil(float64(e.codeContent(i)))) + e.px(2*caretWidth)
	return min(max(content, narrow), e.wideWidth(b))
}

// wideExtra is how far block i's body reaches past the text's right edge: a
// table's grid or a code panel wider than the text column, so the row's
// tint, its selection and what a screen reader is told of it cover the
// whole block.
func (e *Editor) wideExtra(i int) float32 {
	if !e.widens() || i < 0 || i >= len(e.Doc.Blocks) {
		return 0
	}
	b := &e.Doc.Blocks[i]
	var w float32
	switch {
	case b.Kind == Table:
		g, ok := e.tableShowsGrid(i)
		if !ok {
			return 0
		}
		w = min(g.width(), e.wideWidth(b))
	case e.codeTextShows(i):
		w = e.wideCodeViewport(i)
	default:
		return 0
	}
	return max(0, w-e.textWidth(b))
}
