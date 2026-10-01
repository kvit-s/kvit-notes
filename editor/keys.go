package editor

// The keyboard: shortcuts, editing keys, caret movement, and the keys a
// block selection and the / menu take.

import (
	"runtime"
	"slices"
	"strings"

	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

func (e *Editor) keyDown(key unison.KeyCode, mods mod.Modifiers, _ bool) bool {
	if e.yieldsKey() {
		return false
	}
	used := e.handleKey(key, mods.OSMenuCommandDown(), mods.ShiftDown(), mods.OptionDown())
	if used {
		e.changed()
	}
	return used
}

// handleKey applies one key press and reports whether the editor used it.
func (e *Editor) handleKey(key unison.KeyCode, ctrl, shift, alt bool) bool {
	d := e.Doc
	if key == unison.KeyNumPadEnter {
		key = unison.KeyReturn
	}
	if e.menu != nil && e.menuKey(key) {
		return true
	}
	if e.wiki != nil && !ctrl && !alt && e.wikiKey(key) {
		return true
	}
	if e.math.menu != nil && !ctrl && !alt && e.mathMenuKey(key, shift) {
		return true
	}
	if !alt && e.diagramKey(key, ctrl, shift) {
		return true
	}
	if (key == unison.KeyF10 && shift) || key == unison.KeyMenu {
		return e.openBlockMenuForCaret()
	}
	if e.drag != nil && e.drag.active && key == unison.KeyEscape {
		d.restore(e.drag.before)
		d.Dirty = len(d.undo) > 0
		e.drag = nil
		return true
	}

	if e.light != nil && key == unison.KeyEscape {
		e.CloseLightbox()
		return true
	}

	// A press started on a board that turned into a drag is dropped
	// without writing anything.
	if key == unison.KeyEscape && e.cardDrag != nil {
		e.cardDrag = nil
		e.changed()
		return true
	}
	if key == unison.KeyEscape && e.colDrag != nil {
		e.colDrag = nil
		e.changed()
		return true
	}

	if e.tableSweepKey(key, ctrl, shift, alt) {
		return true
	}

	if e.drawnKey(key, ctrl, shift, alt) {
		return true
	}

	if e.tableGridKeys(key, ctrl, shift, alt) {
		return true
	}

	if e.gapArmed >= 0 {
		if e.gapKey(key, ctrl, shift, alt, "") {
			e.changed()
			return true
		}
		// Typing reaches the seam through runeTyped, not here: a printable
		// KeyDown (or AltGr as Ctrl+Alt) must not disarm before its rune
		// arrives.
		if (!ctrl && !alt) || (ctrl && alt) {
			return false
		}
		// Any other shortcut ends the seam caret and runs normally below.
		e.dismissGap()
	}
	if e.embeddedKey(key, ctrl, shift, alt) {
		e.afterStructural()
		return true
	}

	// Shortcuts that act on the whole note.
	switch {
	case ctrl && key == unison.KeyZ && !shift:
		d.Undo()
		e.afterStructural()
		return true
	case ctrl && (key == unison.KeyY || (key == unison.KeyZ && shift)):
		d.Redo()
		e.afterStructural()
		return true
	case ctrl && key == unison.KeyV && !d.ReadOnly:
		if len(e.blockSel) > 0 {
			return e.blockSelectionKey(key, ctrl, shift, alt)
		}
		if shift {
			if unison.ClipboardHasText() {
				d.Paste(strings.ReplaceAll(unison.ClipboardGetText(), "\r\n", "\n"), true)
				e.afterStructural()
			}
			return true
		}
		e.pasteClipboard()
		return true
	}

	if len(e.blockSel) > 0 {
		return e.blockSelectionKey(key, ctrl, shift, alt)
	}
	b := d.CaretBlock()
	if !d.Focused || b == nil {
		return false
	}
	id := b.ID
	if !alt && e.mathKey(key, ctrl, shift) {
		e.afterStructural()
		return true
	}

	switch {
	case ctrl && key == unison.KeyA:
		e.selectAllN++
		r := runes(b.Text)
		if e.selectAllN == 1 && !(d.Anchor.Off == 0 && d.Caret.Off == len(r) && !d.CrossBlock()) {
			d.Anchor = Pos{id, 0}
			d.Caret = Pos{id, len(r)}
		} else {
			d.Focused = false
			for _, x := range d.Blocks {
				e.blockSel[x.ID] = true
			}
		}
		return true
	case ctrl && (key == unison.KeyC || key == unison.KeyX):
		if d.HasSelection() {
			e.copyMarkdown(e.copiedSelection())
			if key == unison.KeyX {
				d.DeleteSelection()
				e.afterStructural()
			}
		}
		return true
	case ctrl && key == unison.KeyB && !shift:
		d.ToggleFormat("**")
	case ctrl && key == unison.KeyI:
		d.ToggleFormat("*")
	case ctrl && key == unison.KeyU:
		d.ToggleFormat("++")
	case ctrl && key == unison.KeyE:
		d.ToggleFormat("`")
	case ctrl && shift && key == unison.KeyS:
		d.ToggleFormat("~~")
	case ctrl && !shift && key >= unison.Key0 && key <= unison.Key4:
		d.Convert(id, []Kind{Paragraph, Heading1, Heading2, Heading3, Heading4}[key-unison.Key0])
	case ctrl && key == unison.KeyT:
		if shift {
			d.Convert(id, Quote)
		} else {
			d.Convert(id, Todo)
		}
	case ctrl && key == unison.KeyD:
		if shift {
			d.DeleteBlocks([]int64{id})
		} else {
			ids := d.Duplicate([]int64{id})
			d.SetCaret(ids[0], d.Caret.Off)
		}
	case alt && (key == unison.KeyUp || key == unison.KeyDown):
		d.Move([]int64{id}, direction(key))
	case ctrl && shift && (key == unison.KeyUp || key == unison.KeyDown):
		d.Focused = false
		e.blockSel[id] = true
		e.blockAnchor = id
	case key == unison.KeyReturn && ctrl:
		switch b.Kind {
		case Todo:
			d.ToggleTodo(id)
		case Code, Raw, Table, Math:
			d.LeaveBlock()
		case Callout:
			// Ctrl+Enter folds or unfolds a callout.
			d.Edit("fold", func() { b.Checked = !b.Checked })
		}
	case key == unison.KeyReturn && shift:
		// A line break inside the block where the block holds one; a
		// heading, code and the source kinds take Enter's meaning.
		if softBreaks(b.Kind) {
			e.softBreak(b)
		} else {
			d.Enter()
		}
	case key == unison.KeyReturn:
		d.Enter()
	case key == unison.KeyBackspace && deletesWord(ctrl, alt):
		d.DeleteWordBack()
	case key == unison.KeyBackspace:
		d.Backspace()
	case key == unison.KeyDelete && deletesWord(ctrl, alt):
		d.DeleteWordForward()
	case key == unison.KeyDelete:
		d.DeleteForward()
	case key == unison.KeyTab:
		switch {
		case b.Kind == Code && shift:
			e.indentCodeLines(true)
		case isMermaid(b):
			// Mermaid source indents by two spaces.
			d.InsertText("  ")
		case b.Kind == Code:
			e.indentCodeLines(false)
		case shift:
			d.Indent([]int64{id}, -1)
		default:
			d.Indent([]int64{id}, 1)
		}
	case key == unison.KeyEscape:
		if d.HasSelection() {
			d.Anchor = d.Caret
		} else {
			d.Focused = false
			e.blockSel[id] = true
			e.blockAnchor = id
		}
	default:
		if e.moveKey(key, ctrl, shift) {
			e.touched()
			return true
		}
		return false
	}
	if key != unison.KeyA {
		e.selectAllN = 0
	}
	e.afterStructural()
	return true
}

// softBreak is Shift+Enter in a block that holds line breaks: a line break
// at the caret, replacing any selection inside the block. A list item takes
// no empty line, which would read back as a second block, so a break that
// would make one is refused.
func (e *Editor) softBreak(b *Block) {
	d := e.Doc
	d.Edit("insert", func() {
		if d.HasSelection() {
			d.deleteSelection()
		}
		b = d.CaretBlock()
		r := runes(b.Text)
		off := min(d.Caret.Off, len(r))
		empty := off == 0 || r[off-1] == '\n' || off < len(r) && r[off] == '\n'
		if b.Kind.IsList() && empty {
			return
		}
		b.Text = string(r[:off]) + "\n" + string(r[off:])
		d.SetCaret(b.ID, off+1)
	})
}

// softBreaks reports whether Shift+Enter writes a line break inside a block
// of kind k: a heading is one line, so there Shift+Enter is Enter.
func softBreaks(k Kind) bool {
	return k == Paragraph || k == Quote || k == Callout || k.IsList()
}

// direction is -1 for Up and 1 for Down.
func direction(key unison.KeyCode) int {
	if key == unison.KeyDown {
		return 1
	}
	return -1
}

// afterStructural follows any change that is not a caret movement: the goal
// column is forgotten, the caret comes into view and the / menu follows the
// text.
func (e *Editor) afterStructural() {
	e.hasGoal = false
	e.touched()
	if e.menu != nil {
		e.syncMenu()
	}
}

func (e *Editor) outdentCodeLine() {
	d := e.Doc
	b := d.CaretBlock()
	r := runes(b.Text)
	ls := d.Caret.Off
	for ls > 0 && r[ls-1] != '\n' {
		ls--
	}
	n := 0
	for n < 4 && ls+n < len(r) && r[ls+n] == ' ' {
		n++
	}
	if n == 0 {
		return
	}
	d.Edit("typing", func() {
		b.Text = string(r[:ls]) + string(r[ls+n:])
		d.SetCaret(b.ID, max(ls, d.Caret.Off-n))
	})
}

// codeIndentWidth is the columns of one indent stop in code.
const codeIndentWidth = 4

// indentCodeLines indents code by Tab's rule: without a multi-line selection
// Tab pads to the next four-column stop, replacing a selection inside the
// line, and Shift+Tab takes one stop back off; a selection spanning lines
// indents or outdents every line it touches, blank lines gaining nothing.
func (e *Editor) indentCodeLines(outdent bool) {
	d := e.Doc
	b := d.CaretBlock()
	if b == nil || b.Kind != Code {
		return
	}
	r := runes(b.Text)
	a, c := d.Anchor.Off, d.Caret.Off
	if d.CrossBlock() {
		a = c
	}
	selStart, selEnd := min(a, c), max(a, c)
	lineStart := selStart
	for lineStart > 0 && r[lineStart-1] != '\n' {
		lineStart--
	}
	nextBreak := -1
	for k := selStart; k < len(r); k++ {
		if r[k] == '\n' {
			nextBreak = k
			break
		}
	}
	spansLines := selEnd > selStart && nextBreak >= 0 && nextBreak < selEnd
	if !spansLines && !outdent {
		column := selStart - lineStart
		pad := codeIndentWidth - (column % codeIndentWidth)
		d.Edit("typing", func() {
			b.Text = string(r[:selStart]) + strings.Repeat(" ", pad) + string(r[selEnd:])
			d.SetCaret(b.ID, selStart+pad)
			d.Anchor = d.Caret
		})
		return
	}
	if !spansLines {
		strip := 0
		for strip < codeIndentWidth && lineStart+strip < len(r) && r[lineStart+strip] == ' ' {
			strip++
		}
		if strip == 0 {
			return
		}
		back := max(lineStart, selStart-strip)
		d.Edit("typing", func() {
			b.Text = string(r[:lineStart]) + string(r[lineStart+strip:])
			d.SetCaret(b.ID, back)
			d.Anchor = d.Caret
		})
		return
	}
	lastEnd := selEnd
	for lastEnd < len(r) && r[lastEnd] != '\n' {
		lastEnd++
	}
	lines := strings.Split(string(r[lineStart:lastEnd]), "\n")
	delta := 0
	unit := strings.Repeat(" ", codeIndentWidth)
	for k := range lines {
		if outdent {
			off := 0
			for off < codeIndentWidth && off < len(lines[k]) && lines[k][off] == ' ' {
				off++
			}
			lines[k] = lines[k][off:]
			delta -= off
		} else if len(lines[k]) > 0 {
			lines[k] = unit + lines[k]
			delta += codeIndentWidth
		}
	}
	d.Edit("typing", func() {
		b.Text = string(r[:lineStart]) + strings.Join(lines, "\n") + string(r[lastEnd:])
		d.Anchor = Pos{b.ID, lineStart}
		d.Caret = Pos{b.ID, lastEnd + delta}
		d.Focused = true
	})
	e.RequestFocus()
	e.touched()
}

// optionDeletesWord is whether Option, rather than Ctrl, makes Backspace and
// Delete take a word, as on macOS. The tests set it.
var optionDeletesWord = runtime.GOOS == "darwin"

// deletesWord reports whether Backspace or Delete with these modifiers takes
// a word: Ctrl on Windows and Linux, Option on macOS (where ctrl is
// Command). AltGr, which arrives as Ctrl with Alt, does not.
func deletesWord(ctrl, alt bool) bool {
	if optionDeletesWord {
		return alt && !ctrl
	}
	return ctrl && !alt
}

// moveKey moves the caret. Within a block it follows the drawn layout; at a
// block's edge Up goes to the end of the block above and Down to the start of
// the block below. With Shift the selection extends, across blocks when it
// reaches an edge.
func (e *Editor) moveKey(key unison.KeyCode, ctrl, shift bool) bool {
	d := e.Doc
	b := d.CaretBlock()
	i := d.Index(b.ID)
	l := e.layout(i)
	disp := l.drawn(d.Caret.Off)
	r := runes(b.Text)
	off := d.Caret.Off
	newPos := d.Caret
	vertical := key == unison.KeyUp || key == unison.KeyDown

	prevText := func() (Pos, bool) {
		for j := i - 1; j >= 0; j-- {
			if d.Blocks[j].Kind.IsText() {
				return Pos{d.Blocks[j].ID, len(runes(d.Blocks[j].Text))}, true
			}
		}
		return Pos{}, false
	}
	nextText := func() (Pos, bool) {
		for j := i + 1; j < len(d.Blocks); j++ {
			if d.Blocks[j].Kind.IsText() {
				return Pos{d.Blocks[j].ID, 0}, true
			}
		}
		return Pos{}, false
	}

	switch key {
	case unison.KeyLeft:
		switch {
		case !shift && d.HasSelection() && !d.CrossBlock():
			lo, _ := d.SelRange()
			newPos = lo
		case ctrl:
			newPos.Off = wordLeft(r, off)
		case off > 0:
			// Stepping into a span lands inside it, next to its content;
			// inside revealed markers the step is one character.
			newPos.Off = l.proj.forClick(disp - 1)
			if newPos.Off >= off {
				newPos.Off = off - 1
			}
		default:
			if p, ok := prevText(); ok {
				newPos = p
			}
		}
	case unison.KeyRight:
		switch {
		case !shift && d.HasSelection() && !d.CrossBlock():
			_, hi := d.SelRange()
			newPos = hi
		case ctrl:
			newPos.Off = wordRight(r, off)
		case off < len(r):
			newPos.Off = l.proj.forClick(disp + 1)
			if newPos.Off <= off {
				newPos.Off = off + 1
			}
		default:
			if p, ok := nextText(); ok {
				newPos = p
			}
		}
	case unison.KeyHome, unison.KeyEnd:
		li := l.lineOf(disp)
		switch {
		case ctrl && key == unison.KeyHome:
			// The start of the note.
			for j := 0; j < len(d.Blocks); j++ {
				if d.Blocks[j].Kind.IsText() {
					newPos = Pos{d.Blocks[j].ID, 0}
					break
				}
			}
		case ctrl:
			// The end of the note.
			for j := len(d.Blocks) - 1; j >= 0; j-- {
				if d.Blocks[j].Kind.IsText() {
					newPos = Pos{d.Blocks[j].ID, len(runes(d.Blocks[j].Text))}
					break
				}
			}
		case key == unison.KeyHome:
			newPos.Off = l.proj.afterPrev(l.lineStart(li))
		default:
			newPos.Off = l.proj.beforeNext(l.lineEnd(li))
		}
	case unison.KeyUp, unison.KeyDown:
		x, _ := l.caretAt(disp)
		if !e.hasGoal {
			e.goalX, e.hasGoal = x, true
		}
		li := l.lineOf(disp)
		switch {
		case key == unison.KeyUp && li > 0:
			newPos.Off = l.proj.forClick(l.hitTest(e.goalX, l.lineMiddle(li-1)))
		case key == unison.KeyDown && li < l.lines()-1:
			newPos.Off = l.proj.forClick(l.hitTest(e.goalX, l.lineMiddle(li+1)))
		case key == unison.KeyUp:
			p, ok := prevText()
			switch {
			case !ok:
				newPos.Off = 0
			case shift:
				newPos = e.posAtX(p.Block, e.goalX, false)
			default:
				newPos = p
			}
		default:
			p, ok := nextText()
			switch {
			case !ok:
				newPos.Off = len(r)
			case shift:
				newPos = e.posAtX(p.Block, e.goalX, true)
			default:
				newPos = p
			}
		}
	default:
		return false
	}
	if !vertical {
		e.hasGoal = false
	}
	if newPos.Block != d.Caret.Block || !shift {
		d.Seal()
	}
	d.Caret = newPos
	if !shift {
		d.Anchor = newPos
	}
	return true
}

// posAtX is the caret offset at x on the first or last line of a block.
func (e *Editor) posAtX(id int64, x float32, firstLine bool) Pos {
	i := e.Doc.Index(id)
	if i < 0 || !e.Doc.Blocks[i].Kind.IsText() {
		return Pos{id, 0}
	}
	l := e.layout(i)
	li := 0
	if !firstLine {
		li = l.lines() - 1
	}
	return Pos{id, l.proj.forClick(l.hitTest(x, l.lineMiddle(li)))}
}

// blockSelectionKey handles a key while whole blocks are selected.
func (e *Editor) blockSelectionKey(key unison.KeyCode, ctrl, shift, alt bool) bool {
	d := e.Doc
	ids := e.SelectedBlocks()
	if len(ids) == 0 {
		return false
	}
	switch {
	case key == unison.KeyEscape:
		e.clearBlockSel()
	case ctrl && key == unison.KeyReturn && !d.ReadOnly:
		// The keyboard's way to the space after a table, a code block or a
		// board, whose own Enter edits them: a paragraph after the blocks
		// selected, with the caret in it.
		last := d.Index(ids[len(ids)-1])
		nb := NewBlock(Paragraph, "")
		d.Edit("insert block", func() {
			d.Blocks = slices.Insert(d.Blocks, last+1, nb)
			d.SetCaret(nb.ID, 0)
		})
		e.clearBlockSel()
		e.touched()
	case key == unison.KeyBackspace || key == unison.KeyDelete || (ctrl && shift && key == unison.KeyD):
		d.DeleteBlocks(ids)
		e.clearBlockSel()
	case ctrl && key == unison.KeyD:
		dup := d.Duplicate(ids)
		e.clearBlockSel()
		for _, id := range dup {
			e.blockSel[id] = true
		}
	case ctrl && (key == unison.KeyC || key == unison.KeyX):
		e.copyMarkdown(e.blocksMarkdown(ids))
		if key == unison.KeyX {
			d.DeleteBlocks(ids)
			e.clearBlockSel()
		}
	case ctrl && key == unison.KeyV && !d.ReadOnly:
		// Paste after the selection, selecting the new blocks.
		if e.PasteImage != nil {
			if path, ok := e.PasteImage(); ok {
				last := d.Index(ids[len(ids)-1])
				nb := NewBlock(Image, "![]("+path+")")
				d.Edit("insert", func() {
					d.Blocks = slices.Insert(d.Blocks, last+1, nb)
				})
				e.clearBlockSel()
				e.blockSel[nb.ID] = true
				break
			}
		}
		if !unison.ClipboardHasText() {
			break
		}
		last := d.Index(ids[len(ids)-1])
		at := last + 1
		if shift {
			text := unison.ClipboardGetText()
			if n := d.InsertPlainTextAt(at, text); n > 0 {
				e.clearBlockSel()
				for _, b := range d.Blocks[at : at+n] {
					e.blockSel[b.ID] = true
				}
			}
		} else {
			text := unison.ClipboardGetText()
			if PasteOpensAFence(text) {
				if n := d.InsertMarkdownAt(at, text); n > 0 {
					e.clearBlockSel()
					for _, b := range d.Blocks[at : at+n] {
						e.blockSel[b.ID] = true
					}
				}
			} else if e.PasteRich != nil {
				if md, ok := e.PasteRich(); ok {
					if n := d.InsertMarkdownAt(at, md); n > 0 {
						e.clearBlockSel()
						for _, b := range d.Blocks[at : at+n] {
							e.blockSel[b.ID] = true
						}
					}
				} else if n := d.InsertMarkdownAt(at, text); n > 0 {
					e.clearBlockSel()
					for _, b := range d.Blocks[at : at+n] {
						e.blockSel[b.ID] = true
					}
				}
			} else if n := d.InsertMarkdownAt(at, text); n > 0 {
				e.clearBlockSel()
				for _, b := range d.Blocks[at : at+n] {
					e.blockSel[b.ID] = true
				}
			}
		}
	case alt && (key == unison.KeyUp || key == unison.KeyDown):
		d.Move(ids, direction(key))
	case key == unison.KeyTab:
		if shift {
			d.Indent(ids, -1)
		} else {
			d.Indent(ids, 1)
		}
	case key == unison.KeyUp || key == unison.KeyDown:
		// Shift (or Ctrl+Shift) extends from the anchor; a plain arrow
		// selects the next block alone.
		edge := ids[0]
		if key == unison.KeyDown {
			edge = ids[len(ids)-1]
		}
		i := d.Index(edge)
		if key == unison.KeyUp && i > 0 {
			i--
		} else if key == unison.KeyDown && i < len(d.Blocks)-1 {
			i++
		}
		if !shift {
			e.clearBlockSel()
			e.blockAnchor = d.Blocks[i].ID
		}
		e.selectRange(e.blockAnchor, d.Blocks[i].ID)
	case key == unison.KeyReturn:
		if len(ids) == 1 && d.Block(ids[0]).Kind.IsText() {
			e.clearBlockSel()
			d.SetCaret(ids[0], len(runes(d.Block(ids[0]).Text)))
		}
	default:
		return false
	}
	e.touched()
	return true
}

// blocksMarkdown is blocks written as Markdown, a blank line between them.
func (e *Editor) blocksMarkdown(ids []int64) string {
	d := e.Doc
	var parts []string
	for _, id := range ids {
		i := d.Index(id)
		parts = append(parts, BlockMarkdown(d.Blocks[i], ListNumber(d.Blocks, i)))
	}
	return strings.Join(parts, "\n\n")
}

func (e *Editor) selectRange(from, to int64) {
	d := e.Doc
	i, j := d.Index(from), d.Index(to)
	if i > j {
		i, j = j, i
	}
	clear(e.blockSel)
	for k := i; k <= j; k++ {
		e.blockSel[d.Blocks[k].ID] = true
	}
}

func (e *Editor) runeTyped(ch rune) bool {
	if used, stop := e.embeddedRune(); stop {
		return used
	}
	if e.gapArmed >= 0 && !e.Doc.ReadOnly {
		if ch >= 0x20 && ch != 0x7f {
			e.gapInsert(string(ch))
			e.changed()
			return true
		}
		return false
	}
	if e.caretInTableGrid() {
		// The grid is showing, not the source: typing drops any rectangle
		// instead of writing into the table's Markdown.
		e.clearTableSweep()
		return true
	}
	if e.HasTableSelection() {
		// The grid is showing, not the source: typing drops the rectangle
		// instead of writing into the table's Markdown.
		e.clearTableSweep()
		return true
	}
	if ch < 0x20 && ch != '\n' && ch != '\t' || ch == 0x7f {
		return false
	}
	if !e.typeText(string(ch)) {
		return false
	}
	e.changed()
	return true
}

// typeText types text at the caret; it reports whether there was anywhere to
// type it.
func (e *Editor) typeText(s string) bool {
	d := e.Doc
	if len(e.blockSel) > 0 {
		ids := e.SelectedBlocks()
		if len(ids) != 1 || !d.Block(ids[0]).Kind.IsText() {
			return false
		}
		e.clearBlockSel()
		d.SetCaret(ids[0], len(runes(d.Block(ids[0]).Text)))
	}
	b := d.CaretBlock()
	if !d.Focused || b == nil {
		return false
	}
	if d.ReadOnly {
		return false
	}
	if e.mathTyped(s) {
		return true
	}
	openMenu := s == "/" && b.Text == "" && b.Kind != Code && b.Kind != Raw
	d.InsertText(s)
	e.selectAllN = 0
	if openMenu {
		e.openSlashMenu(b.ID, true)
	}
	if !d.ApplyTypedConversion() && e.menu != nil {
		e.syncMenu()
	}
	e.hasGoal = false
	e.touched()
	return true
}

// copyMarkdown puts Markdown on the clipboard, with its HTML beside it when
// the application gives the editor a way to make it, and the internal type
// beside those, so pasting it back uses the text as it is.
func (e *Editor) copyMarkdown(md string) {
	if e.CopyRich != nil {
		e.CopyRich(md)
		return
	}
	unison.ClipboardSetText(md)
}

// pasteClipboard pastes what the clipboard holds: HTML turned into
// Markdown when the application reads it, else the text. A copy carrying
// the internal type pastes as its text, never through the converter. A
// payload whose plain text opens a fence uses that text even when HTML is
// present, so a copied fence does not arrive wrapped in a second code block.
func (e *Editor) pasteClipboard() {
	if e.PasteImage != nil {
		if path, ok := e.PasteImage(); ok {
			e.pasteImage(path)
			return
		}
	}
	if unison.ClipboardHasText() && PasteOpensAFence(unison.ClipboardGetText()) {
		e.paste(unison.ClipboardGetText())
		return
	}
	if e.PasteRich != nil {
		if md, ok := e.PasteRich(); ok {
			e.paste(md)
			return
		}
	}
	if unison.ClipboardHasText() {
		e.paste(unison.ClipboardGetText())
	}
}

// pasteImage inserts a stored picture as an image block: converting the
// caret's empty paragraph, else inserting below it.
func (e *Editor) pasteImage(stored string) {
	d := e.Doc
	if !d.Focused {
		return
	}
	md := "![](" + stored + ")"
	d.Edit("insert", func() {
		b := d.CaretBlock()
		if b != nil && b.Kind == Paragraph && b.Text == "" {
			b.Kind, b.Text = Image, md
			d.SetCaret(b.ID, 0)
		} else if b != nil {
			i := d.Index(b.ID)
			nb := NewBlock(Image, md)
			d.Blocks = slices.Insert(d.Blocks, i+1, nb)
			d.SetCaret(nb.ID, 0)
		}
	})
	e.afterStructural()
}

// paste inserts text from the clipboard at the caret: Markdown with blank
// lines in it, lines opening a code fence, or several flat lines becomes
// blocks (Doc.Paste). A lone URL over words links them instead.
func (e *Editor) paste(s string) {
	d := e.Doc
	s = strings.ReplaceAll(s, "\r\n", "\n")
	if !d.Focused {
		return
	}
	if IsLoneURL(s) && d.PasteLink(s) {
		e.afterStructural()
		return
	}
	d.Paste(s, false)
	e.afterStructural()
}
