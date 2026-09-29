package editor

// The blank space between two blocks takes a caret of its own (features.md
// 3.7, BlockGapCursor): pointing at that space draws a line in it,
// clicking turns the line into a blinking caret, and the next character typed
// becomes a paragraph in that space holding it.

import (
	"slices"
	"strings"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// The gutter column, which the seam stops short of, and how far from a
// seam's line the pointer still counts as being in it (BlockGapCursor).
const (
	gapLeftInset  = 40
	gapRightInset = 8
	gapReach      = 10
)

// gapSuspended reports whether a drag is drawing its own indicator in the
// seams, which dismisses the gap caret.
func (e *Editor) gapSuspended() bool { return e.drag != nil && e.drag.active }

// gapLineY is where seam g's line sits: half the spacing above block g, and
// that far below the final row for the space below the last block.
func (e *Editor) gapLineY(g int) float32 {
	n := len(e.Doc.Blocks)
	if n == 0 {
		return 0
	}
	if g >= n {
		return e.tops[n-1] + e.heights[n-1] + e.gap()/2
	}
	return e.tops[g] - e.gap()/2
}

// gapAt is the seam under a point, or -1. It mirrors BlockGapCursor's
// gapUnder: the pointer must be in the seam strip, and past the blank space
// itself a seam still yields to any text under the point.
func (e *Editor) gapAt(where geom.Point) int {
	d := e.Doc
	if d.ReadOnly || e.gapSuspended() || len(d.Blocks) == 0 {
		return -1
	}
	if where.X < e.side()+e.px(gapLeftInset) || where.X > e.width()-e.side()-e.px(gapRightInset) {
		return -1
	}
	n := len(d.Blocks)
	half := e.gap() / 2
	// Inside: the seam whose line is within half the spacing.
	best, bestD := -1, half+e.px(1)
	for g := 0; g <= n; g++ {
		y := e.gapLineY(g)
		dd := where.Y - y
		if dd < 0 {
			dd = -dd
		}
		if dd <= half && dd < bestD {
			best, bestD = g, dd
		}
	}
	if best < 0 {
		// The two ends reach a little past the rows so the space above the
		// first block and below the last one can be pointed at too.
		if where.Y < e.tops[0] && e.tops[0]-where.Y <= e.px(gapReach) {
			best = 0
		} else {
			last := e.tops[n-1] + e.heights[n-1]
			if where.Y > last && where.Y-last <= e.gap()/2+e.px(gapReach) {
				best = n
			} else {
				return -1
			}
		}
	}
	// Past the blank space the seam yields to any text under the point: a
	// press on a paragraph puts the caret in the paragraph.
	if bestD > half {
		if i := e.rowAt(where); i >= 0 && where.X >= e.bodyLeft() {
			if _, ok := e.posAtPoint(where); ok {
				return -1
			}
		}
	}
	return best
}

// placeGap arms the seam above block g, clearing any selection.
func (e *Editor) placeGap(g int) {
	d := e.Doc
	if g < 0 || g > len(d.Blocks) {
		return
	}
	d.Anchor, d.Caret = Pos{}, Pos{}
	d.Focused = false
	e.clearBlockSel()
	e.gapArmed = g
	e.RequestFocus()
	e.touched()
	e.MarkForRedraw()
}

// dismissGap turns the armed seam off.
func (e *Editor) dismissGap() {
	if e.gapArmed < 0 {
		return
	}
	e.gapArmed = -1
	e.MarkForRedraw()
}

// leaveGapToText leaves the armed seam for the text either side: the end of
// the block above, or the start of the first block from above it.
func (e *Editor) leaveGapToText() {
	g := e.gapArmed
	e.dismissGap()
	if len(e.Doc.Blocks) == 0 {
		return
	}
	if g > 0 {
		b := e.Doc.Blocks[min(g-1, len(e.Doc.Blocks)-1)]
		e.Doc.SetCaret(b.ID, len(runes(b.Text)))
	} else {
		e.Doc.SetCaret(e.Doc.Blocks[0].ID, 0)
	}
	e.RequestFocus()
	e.touched()
	e.MarkForRedraw()
}

// gapInsert puts a paragraph in the armed seam and leaves the caret in it.
// typed is the character that asked for the block, or "" when Enter did; it
// goes through typeText so "/" opens the block menu and prefixes convert.
func (e *Editor) gapInsert(typed string) {
	g := e.gapArmed
	if g < 0 || g > len(e.Doc.Blocks) {
		return
	}
	e.dismissGap()
	nb := NewBlock(Paragraph, "")
	e.Doc.Edit("insert block", func() {
		e.Doc.Blocks = slices.Insert(e.Doc.Blocks, g, nb)
		e.Doc.SetCaret(nb.ID, 0)
	})
	e.clearBlockSel()
	e.RequestFocus()
	e.measure()
	if typed != "" {
		e.typeText(typed)
	} else {
		e.touched()
	}
	e.MarkForRedraw()
}

// dragGapAt is the drop gap under a drag at height Y: before the first row
// above the list, after the last below it, and before or after the row in
// view by its middle (BlockDragController gapAt).
func (e *Editor) dragGapAt(y float32) int {
	n := len(e.Doc.Blocks)
	if n == 0 {
		return 0
	}
	if y <= e.tops[0] {
		return 0
	}
	last := e.tops[n-1] + e.heights[n-1]
	if y >= last {
		return n
	}
	for i := 0; i < n; i++ {
		if y >= e.tops[i] && y < e.tops[i]+e.heights[i] {
			mid := e.tops[i] + e.heights[i]/2
			if y < mid {
				return i
			}
			return i + 1
		}
	}
	for i := 0; i < n; i++ {
		if y < e.tops[i] {
			return i
		}
	}
	return n
}

// dragGapStep follows a multi-block drag: the gap under the pointer, drawn
// as the drop indicator.
func (e *Editor) dragGapStep(where geom.Point) {
	g := e.dragGapAt(where.Y)
	if g != e.drag.gap {
		e.drag.gap = g
		e.MarkForRedraw()
	}
}

// gapPaste pastes the clipboard at the armed seam: flat text makes
// paragraphs, structured content keeps its block types, and plain pastes the
// source's text as paragraphs with formatting removed.
func (e *Editor) gapPaste(plain bool) {
	g := e.gapArmed
	if g < 0 || g > len(e.Doc.Blocks) {
		return
	}
	if e.PasteImage != nil && !plain {
		if path, ok := e.PasteImage(); ok {
			nb := NewBlock(Image, "![]("+path+")")
			at := g
			e.dismissGap()
			e.Doc.Edit("insert", func() {
				e.Doc.Blocks = slices.Insert(e.Doc.Blocks, at, nb)
				e.Doc.SetCaret(nb.ID, 0)
			})
			e.measure()
			e.touched()
			e.MarkForRedraw()
			return
		}
	}
	if !unison.ClipboardHasText() {
		return
	}
	text := strings.ReplaceAll(unison.ClipboardGetText(), "\r\n", "\n")
	if text == "" {
		return
	}
	at := g
	e.dismissGap()
	var n int
	if plain {
		n = e.Doc.InsertPlainTextAt(at, text)
	} else if PasteOpensAFence(text) {
		n = e.Doc.InsertMarkdownAt(at, text)
	} else if e.PasteRich != nil {
		if md, ok := e.PasteRich(); ok {
			n = e.Doc.InsertMarkdownAt(at, md)
		} else if strings.Contains(text, "\n\n") {
			n = e.Doc.InsertMarkdownAt(at, text)
		} else if strings.Contains(text, "\n") {
			// Flat text makes a paragraph per line, as in the caret paste.
			lines := strings.Split(text, "\n")
			blocks := make([]Block, 0, len(lines))
			for _, line := range lines {
				blocks = append(blocks, NewBlock(Paragraph, line))
			}
			e.Doc.Edit("insert", func() {
				e.Doc.Blocks = slices.Insert(e.Doc.Blocks, at, blocks...)
				e.Doc.SetCaret(blocks[len(blocks)-1].ID, len(runes(blocks[len(blocks)-1].Text)))
			})
			n = len(blocks)
		} else {
			n = e.Doc.InsertMarkdownAt(at, text)
		}
	} else if strings.Contains(text, "\n\n") {
		n = e.Doc.InsertMarkdownAt(at, text)
	} else if strings.Contains(text, "\n") {
		lines := strings.Split(text, "\n")
		blocks := make([]Block, 0, len(lines))
		for _, line := range lines {
			blocks = append(blocks, NewBlock(Paragraph, line))
		}
		e.Doc.Edit("insert", func() {
			e.Doc.Blocks = slices.Insert(e.Doc.Blocks, at, blocks...)
			e.Doc.SetCaret(blocks[len(blocks)-1].ID, len(runes(blocks[len(blocks)-1].Text)))
		})
		n = len(blocks)
	} else {
		n = e.Doc.InsertMarkdownAt(at, text)
	}
	if n > 0 {
		// Land in the last new block, as pasting into an empty paragraph does.
		last := e.Doc.Blocks[at+n-1]
		e.Doc.SetCaret(last.ID, len(runes(last.Text)))
	}
	e.measure()
	e.afterStructural()
	e.MarkForRedraw()
}

// gapKey handles a key while a seam is armed. It reports whether the key was
// used.
func (e *Editor) gapKey(key unison.KeyCode, ctrl, shift, alt bool, text string) bool {
	if e.gapArmed < 0 {
		return false
	}
	d := e.Doc
	switch {
	case key == unison.KeyEscape:
		e.leaveGapToText()
		return true
	case key == unison.KeyUp || key == unison.KeyDown:
		next := e.gapArmed + 1
		if key == unison.KeyUp {
			next = e.gapArmed - 1
		}
		if next >= 0 && next <= len(d.Blocks) {
			e.gapArmed = next
			e.MarkForRedraw()
		}
		return true
	case key == unison.KeyReturn || key == unison.KeyNumPadEnter:
		e.gapInsert("")
		return true
	case (key == unison.KeyV) && (ctrl || false) && !alt && !d.ReadOnly:
		// Ctrl+V pastes at the seam; Shift is paste-as-plain. Meta (Cmd) is
		// handled by the caller mapping it to ctrl.
		e.gapPaste(shift)
		return true
	}
	// Anything that types a character starts a paragraph holding it. AltGr
	// arrives as Ctrl+Alt and is a typed character rather than a shortcut.
	if text != "" && ((!ctrl && !alt) || (ctrl && alt)) && !d.ReadOnly {
		if r := []rune(text); len(r) == 1 && r[0] >= 0x20 && r[0] != 0x7f {
			e.gapInsert(text)
			return true
		}
	}
	return false
}

var _ = mod.None
