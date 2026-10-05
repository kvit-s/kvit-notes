package editor

// What changes about the editor when a program draws it somewhere other than
// the note pane: a markdown file or a transcript drawn read-only, a message
// box, a comment field. Kvit Works draws it in all three.
//
// An editor nobody embeds has no seams and every hook below gives the note
// editor's own answer, so Kvit Notes lays out, draws and answers keys exactly
// as it does without this file.

import (
	"slices"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// Embedding is how an editor drawn somewhere other than the note pane is laid
// out and what it does with the few keys and presses a host takes over. Pass
// it to Editor.Embed before the editor is first shown.
type Embedding struct {
	// NoGutter takes away the strip left of each row with the add button,
	// the drag handle and the block menu, and its width with it, so the text
	// starts near the editor's left edge.
	NoGutter bool
	// Margin is the space between the editor's edges and its rows, in design
	// pixels, in place of the note's page margin of 20: above the first row,
	// below the last, and at either side. Nothing is added below the last
	// row for scrolling past the end, so the editor is exactly as tall as its
	// rows and margins.
	Margin float32
	// BlockSpacing is the space between two rows in design pixels; 0 keeps
	// the reader's paragraph spacing.
	BlockSpacing int
	// TightRows takes away the room each row keeps for the gutter's buttons,
	// 10 design pixels above its text and 18 below, so that a row is as tall
	// as its text: BlockSpacing is then all the space between two blocks,
	// and ListSpacing all the space between two items of a list. It is for a
	// document read more than edited, such as a transcript.
	TightRows bool
	// ListSpacing, with TightRows, is the space between two items of a list
	// in design pixels, where BlockSpacing separates every other two rows. At
	// 0 an item's first line is under the last line of the item before it
	// as the lines of one paragraph are under each other.
	ListSpacing int
	// ReadOnlyLook draws no caret, no focus bar and no hover tint. It does
	// not refuse edits by itself: set Doc.ReadOnly for that.
	ReadOnlyLook bool
	// HideImageLine never shows an image block's Markdown line, even with the
	// caret in it: the picture keeps the keyboard as a whole, Backspace or
	// Delete removes it, the arrows leave it, and nothing can be typed into
	// it.
	HideImageLine bool
	// ReturnPressed, when set, takes Enter from the editor: it is called
	// with the index of the caret's block instead of the block being split.
	// Enter still takes the highlighted entry of an open menu, writes a new
	// line in a code block, makes the next item of a list with text in it,
	// ends a list or a quote on an empty item, and replaces a selection that
	// crosses blocks; Shift+Enter still breaks the line; Ctrl+Enter is
	// reported from every block, a list and a code block included.
	ReturnPressed func(blockIndex int)
	// OpenPicture, when set, is asked to show a picture a press landed on,
	// in place of the editor's own full-size view drawn inside its panel.
	OpenPicture func(path, alt string)
	// CopyFragments makes Ctrl+C and Ctrl+X put the selection on the
	// clipboard as RangeMarkdown gives it: a partly selected block as a
	// fragment whose inline markers are balanced.
	CopyFragments bool
	// Laid, when set, runs each time the rows have been measured again,
	// for a host that places things over them. It may ask where things are,
	// and must not change the editor.
	Laid func()
}

// seams is what an embedding and a decoration registry add to one editor.
type seams struct {
	emb  *Embedding
	deco *Decorations
	// relayoutPending is set while a re-measure asked for by a decoration
	// waits for the next pass of the event loop.
	relayoutPending bool
}

// Embed lays the editor out and has it answer as emb says. The editor keeps a
// copy; calling Embed again replaces it.
func (e *Editor) Embed(emb Embedding) {
	if e.seams == nil {
		e.seams = &seams{}
	}
	e.seams.emb = &emb
	e.laidWidth = 0
	clear(e.layouts)
	e.relayout()
}

// Embedded is the embedding the editor was given, and false for the note
// editor.
func (e *Editor) Embedded() (Embedding, bool) {
	if !e.seams.framed() {
		return Embedding{}, false
	}
	return *e.seams.emb, true
}

func (s *seams) framed() bool { return s != nil && s.emb != nil }

func (s *seams) noGutter() bool { return s.framed() && s.emb.NoGutter }

func (s *seams) tightRows() bool { return s.framed() && s.emb.TightRows }

func (s *seams) readOnlyLook() bool { return s.framed() && s.emb.ReadOnlyLook }

func (s *seams) hidesImageLine() bool { return s.framed() && s.emb.HideImageLine }

func (s *seams) copyFragments() bool { return s.framed() && s.emb.CopyFragments }

// margin is the space around the rows: the page margin, or the embedding's.
func (e *Editor) margin() float32 {
	if e.seams.framed() {
		return e.px(e.seams.emb.Margin)
	}
	return e.px(pageMargin)
}

// embeddedTail is the space below the last row of an embedded editor, which
// is its margin and nothing to scroll past the end into.
func (e *Editor) embeddedTail() (float32, bool) {
	if !e.seams.framed() {
		return 0, false
	}
	return e.margin(), true
}

// embeddedGap is an embedding's space between rows, when it sets one.
func (e *Editor) embeddedGap() (float32, bool) {
	if !e.seams.framed() || e.seams.emb.BlockSpacing <= 0 {
		return 0, false
	}
	return e.px(float32(e.seams.emb.BlockSpacing)), true
}

// rowPad is the space above and below a row's text: room for the gutter's
// buttons stacked two high, or none in an embedding with tight rows.
func (e *Editor) rowPad() (top, bottom float32) {
	if e.seams.tightRows() {
		return 0, 0
	}
	return e.px(rowPadTop), e.px(rowPadBottom)
}

// embeddedListGap is an embedding's space between row i and the row after
// it when its rows are tight and both are items of a list.
func (e *Editor) embeddedListGap(i int) (float32, bool) {
	bs := e.Doc.Blocks
	if !e.seams.tightRows() || i < 0 || i+1 >= len(bs) || !bs[i].Kind.IsList() || !bs[i+1].Kind.IsList() {
		return 0, false
	}
	return e.px(float32(e.seams.emb.ListSpacing)), true
}

// focusBarX is where the bar beside the caret's block is drawn: just past
// the gutter, or at the side margin when there is no gutter.
func (e *Editor) focusBarX() float32 {
	if e.seams.noGutter() {
		return e.side()
	}
	return e.side() + e.px(gutterWidth)
}

// showsCaret reports whether the caret is drawn at all: not in a document
// drawn read-only, and not on a picture that holds the keyboard as a whole.
func (e *Editor) showsCaret() bool {
	return !e.seams.readOnlyLook() && !e.pictureHoldsKeyboard()
}

// pictureHoldsKeyboard reports whether the caret is in an image block whose
// line is never shown, so the picture has the keyboard as a whole.
func (e *Editor) pictureHoldsKeyboard() bool {
	if !e.seams.hidesImageLine() || !e.Doc.Focused {
		return false
	}
	i := e.Doc.Index(e.Doc.Caret.Block)
	if i < 0 {
		return false
	}
	_, ok, _ := e.pictureBlock(i)
	return ok
}

// relayout measures the rows again after something beside the document
// changed their room (a decoration, an embedding), and asks the panels
// above for a new layout when the editor's height changed. Unlike changed it
// tells no one that the document or the caret changed.
func (e *Editor) relayout() {
	before := e.total()
	if w := e.ContentRect(false).Width; w > 0 {
		e.measureAt(w)
	}
	e.measure()
	if e.total() != before {
		for p := e.AsPanel(); p != nil; p = p.Parent() {
			p.NeedsLayout = true
		}
	}
	e.MarkForRedraw()
}

// relayoutSoon measures again on the next pass of the event loop, once for
// any number of requests made before it.
func (e *Editor) relayoutSoon() {
	if e.seams == nil || e.seams.relayoutPending {
		return
	}
	e.seams.relayoutPending = true
	unison.InvokeTask(func() {
		if e.seams != nil {
			e.seams.relayoutPending = false
		}
		e.relayout()
	})
}

// afterMeasure places the decorations' panels against the rows just
// measured and tells an embedding's host.
func (e *Editor) afterMeasure() {
	if e.seams == nil {
		return
	}
	if e.seams.deco != nil {
		e.seams.deco.place()
	}
	if e.seams.framed() && e.seams.emb.Laid != nil {
		e.seams.emb.Laid()
	}
}

// Hooks for the keyboard and the pointer.

// yieldsKey reports whether a key or a typed character belongs to a panel a
// decoration put inside the editor: keys it did not use travel up to the
// editor, and must not edit the document under it.
func (e *Editor) yieldsKey() bool {
	if e.seams == nil || e.seams.deco == nil {
		return false
	}
	w := e.Window()
	if w == nil {
		return false
	}
	focus := w.CurrentFocus()
	return focus != nil && focus != e.AsPanel() && e.seams.deco.holds(focus)
}

// yieldsPress reports whether a press is on a panel a decoration put inside
// the editor, which the press belongs to even when that panel did not use it.
func (e *Editor) yieldsPress(where geom.Point) bool {
	if e.seams == nil || e.seams.deco == nil {
		return false
	}
	return e.seams.deco.panelAt(where) != nil
}

// embeddedRune takes a typed character before the editor would type it:
// stop is true when the editor must not, and used is what to answer.
func (e *Editor) embeddedRune() (used, stop bool) {
	if e.yieldsKey() {
		return false, true
	}
	if e.pictureHoldsKeyboard() {
		// Letters go nowhere on a picture.
		return true, true
	}
	return false, false
}

// embeddedKey takes the keys an embedding changes, before the editor's own
// handling of them, and reports whether it used the key.
func (e *Editor) embeddedKey(key unison.KeyCode, ctrl, shift, alt bool) bool {
	d := e.Doc
	b := d.CaretBlock()
	if !e.seams.framed() || alt || !d.Focused || b == nil || len(e.blockSel) > 0 {
		return false
	}
	i := d.Index(b.ID)
	if e.pictureHoldsKeyboard() {
		return e.pictureKey(key, ctrl, shift, i)
	}
	report := e.seams.emb.ReturnPressed
	if report == nil || key != unison.KeyReturn {
		return false
	}
	if ctrl {
		report(i)
		return true
	}
	if shift && softBreaks(b.Kind) {
		return false
	}
	switch {
	case d.HasSelection() && d.CrossBlock():
		return false
	case b.Kind.isSource():
		return false
	case (b.Kind.IsList() || b.Kind == Quote) && b.Text == "":
		return false
	case b.Kind.IsList():
		return false
	}
	report(i)
	return true
}

// pictureKey is a key pressed while a picture holds the keyboard: Backspace
// and Delete take the picture away, the arrows leave it for the row above or
// below, Enter goes to the host or makes a paragraph under it, a pasted
// picture goes under it, and nothing reaches its hidden line.
func (e *Editor) pictureKey(key unison.KeyCode, ctrl, shift bool, i int) bool {
	d := e.Doc
	switch {
	case key == unison.KeyBackspace || key == unison.KeyDelete:
		d.Edit("delete block", func() {
			d.Blocks = slices.Delete(d.Blocks, i, i+1)
			switch {
			case i > 0 && d.Blocks[i-1].Kind.IsText():
				d.SetCaret(d.Blocks[i-1].ID, len(runes(d.Blocks[i-1].Text)))
			case i < len(d.Blocks) && d.Blocks[i].Kind.IsText():
				d.SetCaret(d.Blocks[i].ID, 0)
			default:
				d.Focused = false
			}
		})
	case key == unison.KeyLeft || key == unison.KeyUp:
		if i > 0 && d.Blocks[i-1].Kind.IsText() {
			d.SetCaret(d.Blocks[i-1].ID, len(runes(d.Blocks[i-1].Text)))
		}
	case key == unison.KeyRight || key == unison.KeyDown:
		if i+1 < len(d.Blocks) && d.Blocks[i+1].Kind.IsText() {
			d.SetCaret(d.Blocks[i+1].ID, 0)
		}
	case key == unison.KeyReturn:
		if report := e.seams.emb.ReturnPressed; report != nil && (ctrl || !shift) {
			report(i)
			return true
		}
		d.Edit("insert block", func() {
			nb := NewBlock(Paragraph, "")
			d.Blocks = slices.Insert(d.Blocks, i+1, nb)
			d.SetCaret(nb.ID, 0)
		})
	case ctrl && key == unison.KeyV:
		if e.PasteImage != nil && !d.ReadOnly {
			if path, ok := e.PasteImage(); ok {
				e.pasteImage(path)
			}
		}
	case ctrl || key == unison.KeyTab || key == unison.KeyEscape:
		// Undo, copy, select-all and the other shortcuts run as usual.
		return false
	}
	return true
}

// copiedSelection is what Ctrl+C puts on the clipboard: the note's source
// slices, or balanced fragments for an embedding that asks for them.
func (e *Editor) copiedSelection() string {
	if e.seams.copyFragments() {
		return e.Doc.RangeMarkdown()
	}
	return e.Doc.SelectedMarkdown()
}

// openPictureElsewhere hands a picture to the host that shows it, and
// reports whether there was one.
func (e *Editor) openPictureElsewhere(path, alt string) bool {
	if !e.seams.framed() || e.seams.emb.OpenPicture == nil {
		return false
	}
	e.seams.emb.OpenPicture(path, alt)
	return true
}
