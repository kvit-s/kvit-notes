package editor

// Document state and the structural operations the keyboard triggers. None
// of this knows about the toolkit, so the rules can be tested without a
// window.

import (
	"regexp"
	"slices"
	"strings"
	"time"
	"unicode"

	"github.com/kvit-s/kvit-notes/textdiagram"
)

// Pos is a caret position: a block and a source rune offset in it.
type Pos struct {
	Block int64
	Off   int
}

// DocState is what undo restores: every block, and where the caret was.
type DocState struct {
	Blocks []Block
	Caret  Pos
	Anchor Pos
}

type undoEntry struct {
	before, after DocState
	kind          string
	block         int64
	at            time.Time
	chars         int
}

const undoLimit = 200

type Doc struct {
	Blocks []Block
	Path   string
	Dirty  bool

	// The caret and the selection anchor. When Anchor is in another block
	// than Caret, the selection spans blocks.
	Caret  Pos
	Anchor Pos
	// Focused is false when no block has the caret (block selection, or
	// nothing clicked yet).
	Focused bool

	undo, redo []undoEntry
	// Now is the clock, replaceable in tests.
	Now func() time.Time
	// ReadOnly refuses every change: the caret moves and text can be
	// selected and copied, but nothing is edited. A note in a vault that
	// cannot be written, in the trash or in a backup is read-only.
	ReadOnly bool
}

func NewDoc(blocks []Block) *Doc {
	if len(blocks) == 0 {
		blocks = []Block{NewBlock(Paragraph, "")}
	}
	return &Doc{Blocks: blocks, Now: time.Now}
}

func (d *Doc) Index(id int64) int {
	for i := range d.Blocks {
		if d.Blocks[i].ID == id {
			return i
		}
	}
	return -1
}

func (d *Doc) Block(id int64) *Block {
	if i := d.Index(id); i >= 0 {
		return &d.Blocks[i]
	}
	return nil
}

func (d *Doc) CaretBlock() *Block { return d.Block(d.Caret.Block) }

func (d *Doc) snapshot() DocState {
	return DocState{Blocks: slices.Clone(d.Blocks), Caret: d.Caret, Anchor: d.Anchor}
}

func (d *Doc) restore(s DocState) {
	d.Blocks = slices.Clone(s.Blocks)
	d.Caret, d.Anchor = s.Caret, s.Anchor
	d.Focused = d.Block(d.Caret.Block) != nil
	d.Dirty = true
}

// Edit runs fn as one undoable step. Consecutive typing in one block merges
// into one step while the keystrokes are under 500 ms apart and each adds
// fewer than 20 characters (src/domain/textchangecommand.cpp).
func (d *Doc) Edit(kind string, fn func()) {
	if d.ReadOnly {
		return
	}
	before := d.snapshot()
	fn()
	d.Dirty = true
	now := d.Now()
	if kind == "typing" && len(d.undo) > 0 {
		top := &d.undo[len(d.undo)-1]
		if top.kind == "typing" && top.block == d.Caret.Block && now.Sub(top.at) < 500*time.Millisecond && top.chars < 20 {
			top.after = d.snapshot()
			top.at = now
			top.chars++
			d.redo = nil
			return
		}
	}
	d.undo = append(d.undo, undoEntry{before: before, after: d.snapshot(), kind: kind, block: d.Caret.Block, at: now, chars: 1})
	if len(d.undo) > undoLimit {
		d.undo = d.undo[1:]
	}
	d.redo = nil
}

// Seal ends typing merge, so the next keystroke starts a new undo step
// (a click elsewhere or a caret move does this).
func (d *Doc) Seal() {
	if len(d.undo) > 0 {
		d.undo[len(d.undo)-1].kind += "/sealed"
	}
}

// Begin and Commit bracket a gesture that changes the document several times
// but must undo as one step (a drag that reorders blocks live).
func (d *Doc) Begin() DocState { return d.snapshot() }

func (d *Doc) Commit(kind string, before DocState) {
	if d.ReadOnly {
		return
	}
	d.undo = append(d.undo, undoEntry{before: before, after: d.snapshot(), kind: kind, at: d.Now()})
	d.redo = nil
	d.Dirty = true
}

func (d *Doc) Undo() bool {
	if len(d.undo) == 0 || d.ReadOnly {
		return false
	}
	e := d.undo[len(d.undo)-1]
	d.undo = d.undo[:len(d.undo)-1]
	d.redo = append(d.redo, e)
	d.restore(e.before)
	return true
}

func (d *Doc) Redo() bool {
	if len(d.redo) == 0 || d.ReadOnly {
		return false
	}
	e := d.redo[len(d.redo)-1]
	d.redo = d.redo[:len(d.redo)-1]
	d.undo = append(d.undo, e)
	d.restore(e.after)
	return true
}

func runes(s string) []rune { return []rune(s) }

// SetCaret puts the caret in a block with no selection.
func (d *Doc) SetCaret(id int64, off int) {
	d.Caret = Pos{id, off}
	d.Anchor = d.Caret
	d.Focused = true
}

func (d *Doc) HasSelection() bool { return d.Caret != d.Anchor }

func (d *Doc) CrossBlock() bool { return d.Caret.Block != d.Anchor.Block }

// SelRange returns the selection in document order.
func (d *Doc) SelRange() (Pos, Pos) {
	a, b := d.Anchor, d.Caret
	ia, ib := d.Index(a.Block), d.Index(b.Block)
	if ia > ib || (ia == ib && a.Off > b.Off) {
		a, b = b, a
	}
	return a, b
}

// SelectionIn returns the part of the selection inside block i, as source
// offsets, and whether any of it is there.
func (d *Doc) SelectionIn(i int) (int, int, bool) {
	if !d.HasSelection() {
		return 0, 0, false
	}
	a, b := d.SelRange()
	ia, ib := d.Index(a.Block), d.Index(b.Block)
	if i < ia || i > ib {
		return 0, 0, false
	}
	n := len(runes(d.Blocks[i].Text))
	from, to := 0, n
	if i == ia {
		from = a.Off
	}
	if i == ib {
		to = b.Off
	}
	return from, to, true
}

// SelectedMarkdown is what Copy puts on the clipboard: the partial first and
// last blocks as their source text, whole blocks between them as Markdown.
func (d *Doc) SelectedMarkdown() string {
	a, b := d.SelRange()
	ia, ib := d.Index(a.Block), d.Index(b.Block)
	if ia == ib {
		r := runes(d.Blocks[ia].Text)
		return string(r[a.Off:b.Off])
	}
	var parts []string
	for i := ia; i <= ib; i++ {
		blk := d.Blocks[i]
		from, to, _ := d.SelectionIn(i)
		r := runes(blk.Text)
		if from == 0 && to == len(r) {
			parts = append(parts, BlockMarkdown(blk, ListNumber(d.Blocks, i)))
		} else {
			blk.Text = string(r[from:to])
			if i == ia {
				parts = append(parts, blk.Text)
			} else {
				parts = append(parts, BlockMarkdown(blk, ListNumber(d.Blocks, i)))
			}
		}
	}
	return strings.Join(parts, "\n\n")
}

// deleteSelection removes the selected text. Across blocks, the first block
// keeps its text before the selection and takes the last block's text after
// it; the blocks in between go.
func (d *Doc) deleteSelection() {
	a, b := d.SelRange()
	ia, ib := d.Index(a.Block), d.Index(b.Block)
	first := &d.Blocks[ia]
	fr := runes(first.Text)
	lr := runes(d.Blocks[ib].Text)
	first.Text = string(fr[:a.Off]) + string(lr[b.Off:])
	if ib > ia {
		d.Blocks = slices.Delete(d.Blocks, ia+1, ib+1)
	}
	d.SetCaret(first.ID, a.Off)
}

func (d *Doc) DeleteSelection() {
	d.Edit("delete", d.deleteSelection)
}

// InsertText types text at the caret, replacing any selection. Pasted text
// with blank lines becomes blocks.
func (d *Doc) InsertText(text string) { d.insert(text, false, false) }

// Paste inserts pasted text at the caret, replacing any selection, as
// InsertText does, with the rules the Qt app applies to text arriving from
// outside the note (the paste in EditableBlock.qml):
//   - Into a text block, Markdown with blank lines becomes blocks, and so do
//     several lines opening a code fence, so the fence is read as one.
//     Several flat lines become a paragraph each, as Qt's in-block paste
//     splices them. A plain-text paste does the same split, with inline
//     formatting stripped from each line.
//   - Into a code block, the block's text after the paste goes through the
//     step a fence takes when a note is opened (textdiagram.Ingest): a
//     character diagram in an untagged or `text` block is tagged `diagram`,
//     and a diagram is straightened, in the same undo step as the paste.
//     Typing into the block never does this, since retagging a block while
//     someone types in it would change it under them.
func (d *Doc) Paste(text string, plain bool) { d.insert(text, !plain, true) }

// reOpensFence is a line of pasted text starting a code fence.
var reOpensFence = regexp.MustCompile("(^|\n)[ \t]*(```|~~~)")

// PasteOpensAFence reports whether pasted text opens a code fence, in which
// case the plain text is the structure source even when the clipboard also
// holds HTML (EditableBlock.qml pasteFromClipboard, BlockGapCursor.qml).
func PasteOpensAFence(text string) bool { return reOpensFence.MatchString(text) }

// reLoneURL is a bare URL and nothing else, what pasting over a text
// selection turns into a link (src/platform/clipboardhelper.cpp).
var reLoneURL = regexp.MustCompile(`(?i)\A(https?://|www\.)[^\s<>"]+\z`)

// IsLoneURL reports whether s is a bare URL with nothing around it.
func IsLoneURL(s string) bool { return reLoneURL.MatchString(strings.TrimSpace(s)) }

// displayLine is the display text of one pasted plain-text line: its inline
// markers removed, as Qt's displayTextFor does. Markers are hidden by
// projecting with no reveal, keeping each span's content.
func displayLine(s string) string {
	r := []rune(s)
	if len(r) == 0 {
		return s
	}
	return string(project(r, parseInline(r), nil).Disp)
}

// insert is InsertText and Paste. fences reads several lines opening a
// fence as blocks; ingest runs the fence step over a code block pasted into.
// Flat multi-line text becomes a paragraph per line, as Qt's in-block paste
// does; a plain paste strips each line first.
func (d *Doc) insert(text string, fences, ingest bool) {
	kind := "typing"
	if d.HasSelection() || strings.Contains(text, "\n") || ingest && d.ingestChanges(text) {
		kind = "insert"
	}
	// plain is whether this insert strips formatting: only Paste asks with
	// fences false, which is Ctrl+Shift+V.
	plain := ingest && !fences
	d.Edit(kind, func() {
		if d.HasSelection() {
			d.deleteSelection()
		}
		b := d.CaretBlock()
		if b == nil {
			return
		}
		multi := strings.Contains(text, "\n")
		if b.Kind.HasInline() && (strings.Contains(text, "\n\n") || fences && multi && reOpensFence.MatchString(text)) {
			d.pasteBlocks(text)
			return
		}
		if b.Kind.HasInline() && multi {
			if plain {
				lines := strings.Split(text, "\n")
				for i := range lines {
					lines[i] = displayLine(lines[i])
				}
				text = strings.Join(lines, "\n")
			}
			d.pasteLines(text)
			return
		}
		r := runes(b.Text)
		off := min(d.Caret.Off, len(r))
		b.Text = string(r[:off]) + text + string(r[off:])
		off += len(runes(text))
		if ingest && b.Kind == Code {
			// The straightening moves nothing to the right of what it
			// fixes, so the caret stays after the pasted text; trailing
			// spaces it trims from a line above can shorten the text, which
			// the clamp covers.
			b.Lang, b.Text = textdiagram.Ingest(b.Lang, b.Text)
			off = min(off, len(runes(b.Text)))
		}
		d.SetCaret(b.ID, off)
	})
}

// ingestChanges reports whether pasting text at the caret, with no
// selection, would retag or straighten the code block the caret is in. Such
// a paste is its own undo step rather than one merged into typing.
func (d *Doc) ingestChanges(text string) bool {
	b := d.CaretBlock()
	if b == nil || b.Kind != Code || d.HasSelection() {
		return false
	}
	r := runes(b.Text)
	off := min(d.Caret.Off, len(r))
	after := string(r[:off]) + text + string(r[off:])
	lang, body := textdiagram.Ingest(b.Lang, after)
	return lang != b.Lang || body != after
}

// pasteBlocks splits the block at the caret and puts the pasted Markdown's
// blocks in between; the first pasted paragraph joins the text before the
// caret.
func (d *Doc) pasteBlocks(text string) {
	i := d.Index(d.Caret.Block)
	b := &d.Blocks[i]
	r := runes(b.Text)
	head, tail := string(r[:d.Caret.Off]), string(r[d.Caret.Off:])
	pasted := ParseMarkdown(text)
	if len(pasted) == 0 {
		return
	}
	switch {
	case pasted[0].Kind == Paragraph:
		b.Text = head + pasted[0].Text
		pasted = pasted[1:]
	case head == "" && b.Kind == Paragraph:
		// An empty paragraph, or the start of one, becomes the first block
		// pasted rather than staying empty above it.
		id := b.ID
		*b = pasted[0]
		b.ID = id
		pasted = pasted[1:]
	default:
		b.Text = head
	}
	last := b
	if len(pasted) > 0 {
		d.Blocks = slices.Insert(d.Blocks, i+1, pasted...)
		last = &d.Blocks[i+len(pasted)]
	}
	off := len(runes(last.Text))
	id := last.ID
	if tail != "" && !last.Kind.HasInline() {
		// Text after the caret does not run on into a pasted code block or
		// table: it follows as a paragraph of its own, as in the Qt app.
		d.Blocks = slices.Insert(d.Blocks, i+len(pasted)+1, NewBlock(Paragraph, tail))
	} else {
		last.Text += tail
	}
	d.SetCaret(id, off)
}

// pasteLines splices flat multi-line text at the caret, as Qt's in-block
// paste does: the first line joins the text before the caret, each middle
// line becomes a paragraph, and the last line joins the text after it.
func (d *Doc) pasteLines(text string) {
	i := d.Index(d.Caret.Block)
	b := &d.Blocks[i]
	r := runes(b.Text)
	head, tail := string(r[:d.Caret.Off]), string(r[d.Caret.Off:])
	lines := strings.Split(text, "\n")
	b.Text = head + lines[0]
	at := i + 1
	for _, line := range lines[1 : len(lines)-1] {
		d.Blocks = slices.Insert(d.Blocks, at, NewBlock(Paragraph, line))
		at++
	}
	lastLine := lines[len(lines)-1]
	d.Blocks = slices.Insert(d.Blocks, at, NewBlock(Paragraph, lastLine+tail))
	nb := &d.Blocks[at]
	d.SetCaret(nb.ID, len(runes(lastLine)))
}

// PasteLink replaces a single-block text selection with a link whose label
// is the selected text and whose address is url, as pasting a lone URL over
// words does in the Qt app. It reports whether there was such a selection.
func (d *Doc) PasteLink(url string) bool {
	if d.ReadOnly || !d.HasSelection() || d.CrossBlock() {
		return false
	}
	b := d.CaretBlock()
	if b == nil || !b.Kind.HasInline() || b.Kind == Code {
		return false
	}
	a, c := d.SelRange()
	if d.Index(a.Block) != d.Index(c.Block) {
		return false
	}
	r := runes(b.Text)
	label := string(r[a.Off:c.Off])
	if label == "" || strings.Contains(label, "\n") {
		return false
	}
	link := "[" + label + "](" + strings.TrimSpace(url) + ")"
	d.Edit("insert", func() {
		d.deleteSelection()
		cur := d.CaretBlock()
		if cur == nil {
			return
		}
		cr := runes(cur.Text)
		off := min(d.Caret.Off, len(cr))
		cur.Text = string(cr[:off]) + link + string(cr[off:])
		d.SetCaret(cur.ID, off+len(runes(link)))
	})
	return true
}

// InsertMarkdownAt inserts parsed Markdown blocks at index, as one undo
// step, returning how many were inserted (DocumentSerializer::insertMarkdownAt).
func (d *Doc) InsertMarkdownAt(index int, markdown string) int {
	pasted := ParseMarkdown(markdown)
	if len(pasted) == 0 {
		return 0
	}
	index = max(0, min(index, len(d.Blocks)))
	d.Edit("insert", func() {
		d.Blocks = slices.Insert(d.Blocks, index, pasted...)
		d.SetCaret(pasted[len(pasted)-1].ID, len(runes(pasted[len(pasted)-1].Text)))
	})
	return len(pasted)
}

// InsertPlainTextAt inserts each line of text as a paragraph at index, as
// one undo step, returning how many were inserted
// (DocumentSerializer::insertPlainTextAt). Each line is stripped to its
// display text, as Qt's paste-plain does.
func (d *Doc) InsertPlainTextAt(index int, text string) int {
	text = strings.ReplaceAll(text, "\r\n", "\n")
	lines := strings.Split(text, "\n")
	if len(lines) == 1 && lines[0] == "" {
		return 0
	}
	index = max(0, min(index, len(d.Blocks)))
	blocks := make([]Block, 0, len(lines))
	for _, line := range lines {
		blocks = append(blocks, NewBlock(Paragraph, displayLine(line)))
	}
	d.Edit("insert", func() {
		d.Blocks = slices.Insert(d.Blocks, index, blocks...)
		d.SetCaret(blocks[len(blocks)-1].ID, len(runes(blocks[len(blocks)-1].Text)))
	})
	return len(blocks)
}

// SetCodeLanguage gives a code block a language from the language menu, as
// one undo step. The block's text goes through textdiagram.Ingest with the
// new language, as the Qt app's setCodeLanguage does: choosing "Text
// diagram" straightens the drawing, and choosing plain text for a block
// holding a diagram tags it `diagram` again. "Plain code" (`plain`) is the
// language that keeps a block from being tagged.
func (d *Doc) SetCodeLanguage(id int64, lang string) {
	b := d.Block(id)
	if b == nil || b.Kind != Code {
		return
	}
	lang, text := textdiagram.Ingest(lang, b.Text)
	if lang == b.Lang && text == b.Text {
		return
	}
	d.Edit("language", func() {
		b.Lang, b.Text = lang, text
		if d.Caret.Block == id {
			d.Caret.Off = min(d.Caret.Off, len(runes(text)))
			d.Anchor.Off = min(d.Anchor.Off, len(runes(text)))
		}
	})
}

// AsCode is a rendered diagram's "As code" control (DiagramBlock.qml): the
// block is shown as its source in a code block tagged `plain`, which no
// later open or paste examines again. It is one undo step.
func (d *Doc) AsCode(id int64) {
	b := d.Block(id)
	if b == nil || b.Kind != Code || b.Lang == "plain" {
		return
	}
	d.Edit("as code", func() { d.Block(id).Lang = "plain" })
}

// Backspace at a caret with no selection. Kvit's order at the start of a
// block: a list item, to-do or quote outdents, then becomes a paragraph; an
// empty block is deleted; a non-empty one merges into the block above.
func (d *Doc) Backspace() {
	if d.HasSelection() {
		d.DeleteSelection()
		return
	}
	i := d.Index(d.Caret.Block)
	if i < 0 {
		return
	}
	b := &d.Blocks[i]
	if d.Caret.Off > 0 {
		d.Edit("typing", func() {
			r := runes(b.Text)
			b.Text = string(r[:d.Caret.Off-1]) + string(r[d.Caret.Off:])
			d.SetCaret(b.ID, d.Caret.Off-1)
		})
		return
	}
	if b.Kind.IsList() || b.Kind == Quote {
		d.Edit("outdent", func() {
			if b.Indent > 0 {
				b.Indent--
			} else {
				b.Kind = Paragraph
			}
		})
		return
	}
	if i == 0 {
		return
	}
	prev := &d.Blocks[i-1]
	if b.Text == "" {
		d.Edit("delete block", func() {
			d.Blocks = slices.Delete(d.Blocks, i, i+1)
			if !prev.Kind.IsText() {
				d.Focused = false
				return
			}
			d.SetCaret(prev.ID, len(runes(prev.Text)))
		})
		return
	}
	switch {
	case prev.Kind == Divider:
		d.Edit("delete block", func() { d.Blocks = slices.Delete(d.Blocks, i-1, i) })
	case prev.Kind.isSource():
		d.SetCaret(prev.ID, len(runes(prev.Text)))
	default:
		d.Edit("merge", func() {
			off := len(runes(prev.Text))
			prev.Text += b.Text
			id := prev.ID
			d.Blocks = slices.Delete(d.Blocks, i, i+1)
			d.SetCaret(id, off)
		})
	}
}

// DeleteForward at a caret with no selection: at the end of a block it pulls
// the next block's text in.
func (d *Doc) DeleteForward() {
	if d.HasSelection() {
		d.DeleteSelection()
		return
	}
	i := d.Index(d.Caret.Block)
	b := &d.Blocks[i]
	r := runes(b.Text)
	if d.Caret.Off < len(r) {
		d.Edit("typing", func() {
			b.Text = string(r[:d.Caret.Off]) + string(r[d.Caret.Off+1:])
		})
		return
	}
	if i+1 >= len(d.Blocks) {
		return
	}
	next := d.Blocks[i+1]
	switch {
	case next.Kind == Divider:
		d.Edit("delete block", func() { d.Blocks = slices.Delete(d.Blocks, i+1, i+2) })
	case next.Kind.isSource() || b.Kind.isSource():
	default:
		d.Edit("merge", func() {
			b.Text += next.Text
			d.Blocks = slices.Delete(d.Blocks, i+1, i+2)
		})
	}
}

// continuation is the kind a new block takes after Enter in a block of kind k.
func continuation(k Kind) Kind {
	if k.IsList() || k == Quote {
		return k
	}
	return Paragraph
}

// Enter splits the block at the caret. On an empty list item or quote it
// turns the block into a paragraph instead (leaving the list). In a code
// block it starts a new line with the current line's indentation.
func (d *Doc) Enter() {
	i := d.Index(d.Caret.Block)
	if i < 0 {
		return
	}
	b := &d.Blocks[i]
	if b.Kind.isSource() {
		// The new line takes the indentation the current line opens with,
		// up to the caret (or the start of a selection, which the line
		// break replaces), so Enter inside that indentation never makes
		// more of it (EditableBlock.qml's leadingIndentAt).
		r := runes(b.Text)
		pos := d.Caret.Off
		if d.Anchor.Block == d.Caret.Block && d.Anchor.Off < pos {
			pos = d.Anchor.Off
		}
		lineStart := pos
		for lineStart > 0 && r[lineStart-1] != '\n' {
			lineStart--
		}
		ws := 0
		for lineStart+ws < pos && (r[lineStart+ws] == ' ' || r[lineStart+ws] == '\t') {
			ws++
		}
		d.InsertText("\n" + string(r[lineStart:lineStart+ws]))
		return
	}
	if (b.Kind.IsList() || b.Kind == Quote) && b.Text == "" && !d.HasSelection() {
		d.Edit("convert", func() {
			b.Kind = Paragraph
			b.Indent = 0
		})
		return
	}
	d.Edit("split", func() {
		if d.HasSelection() {
			d.deleteSelection()
		}
		i := d.Index(d.Caret.Block)
		b := &d.Blocks[i]
		r := runes(b.Text)
		off := d.Caret.Off
		nb := NewBlock(continuation(b.Kind), string(r[off:]))
		nb.Indent = b.Indent
		if !nb.Kind.IsList() && nb.Kind != Quote {
			nb.Indent = 0
		}
		if off == 0 && len(r) > 0 {
			// Enter at the start of a non-empty block opens an empty block
			// above and keeps the caret with the text
			nb.Text = ""
			d.Blocks = slices.Insert(d.Blocks, i, nb)
			d.SetCaret(b.ID, 0)
			return
		}
		b.Text = string(r[:off])
		d.Blocks = slices.Insert(d.Blocks, i+1, nb)
		d.SetCaret(nb.ID, 0)
	})
}

// LeaveBlock is Ctrl+Enter in a code block: a paragraph below it.
func (d *Doc) LeaveBlock() {
	i := d.Index(d.Caret.Block)
	d.Edit("insert block", func() {
		nb := NewBlock(Paragraph, "")
		d.Blocks = slices.Insert(d.Blocks, i+1, nb)
		d.SetCaret(nb.ID, 0)
	})
}

func (d *Doc) ToggleTodo(id int64) {
	b := d.Block(id)
	if b == nil || b.Kind != Todo {
		return
	}
	d.Edit("toggle", func() { b.Checked = !b.Checked })
}

// Convert changes a block's kind, keeping its text.
func (d *Doc) Convert(id int64, k Kind) {
	b := d.Block(id)
	if b == nil || b.Kind == k {
		return
	}
	d.Edit("convert", func() {
		if b.Kind == Code || b.Kind == Callout {
			b.Lang = ""
		}
		b.Kind = k
		if k == Callout {
			b.Lang = "info"
		}
		if !k.IsList() && k != Quote {
			b.Indent = 0
		}
		if k == Divider {
			b.Text = ""
			i := d.Index(id)
			if i == len(d.Blocks)-1 {
				nb := NewBlock(Paragraph, "")
				d.Blocks = append(d.Blocks, nb)
			}
			d.SetCaret(d.Blocks[i+1].ID, 0)
		}
	})
}

// SetAttr sets one presentation attribute of blocks, "" removing it, as one
// undo step: the alignment a menu chooses, a divider's style.
func (d *Doc) SetAttr(ids []int64, key, value string) {
	d.Edit("attributes", func() {
		for _, id := range ids {
			b := d.Block(id)
			if b == nil {
				continue
			}
			var keep []string
			for _, tok := range strings.Fields(b.Attrs) {
				if k, _, _ := strings.Cut(tok, "="); k != key {
					keep = append(keep, tok)
				}
			}
			if value != "" {
				keep = append(keep, key+"="+value)
			}
			b.Attrs = canonicalAttrs(strings.Join(keep, " "))
		}
	})
}

func (d *Doc) Indent(ids []int64, delta int) {
	d.Edit("indent", func() {
		for _, id := range ids {
			if b := d.Block(id); b != nil && b.Kind != Code {
				b.Indent = max(0, min(MaxIndent, b.Indent+delta))
			}
		}
	})
}

// Move shifts the given blocks up (-1) or down (+1) as a group.
func (d *Doc) Move(ids []int64, dir int) bool {
	idx := make([]int, 0, len(ids))
	for _, id := range ids {
		if i := d.Index(id); i >= 0 {
			idx = append(idx, i)
		}
	}
	slices.Sort(idx)
	if len(idx) == 0 || (dir < 0 && idx[0] == 0) || (dir > 0 && idx[len(idx)-1] == len(d.Blocks)-1) {
		return false
	}
	d.Edit("move", func() {
		if dir < 0 {
			for _, i := range idx {
				d.Blocks[i-1], d.Blocks[i] = d.Blocks[i], d.Blocks[i-1]
			}
		} else {
			for k := len(idx) - 1; k >= 0; k-- {
				i := idx[k]
				d.Blocks[i+1], d.Blocks[i] = d.Blocks[i], d.Blocks[i+1]
			}
		}
	})
	return true
}

// MoveBlocksTo moves the blocks at sorted indexes to the gap before
// targetGap, as one undo step, preserving their order (BlockModel::
// moveBlocksTo). targetGap counts in the list before the move. A gap inside
// a contiguous dragged run moves nothing.
func (d *Doc) MoveBlocksTo(indexes []int, targetGap int) bool {
	if len(indexes) == 0 || targetGap < 0 || targetGap > len(d.Blocks) {
		return false
	}
	idx := append([]int(nil), indexes...)
	slices.Sort(idx)
	uniq := idx[:0]
	for _, i := range idx {
		if i < 0 || i >= len(d.Blocks) {
			return false
		}
		if len(uniq) == 0 || uniq[len(uniq)-1] != i {
			uniq = append(uniq, i)
		}
	}
	idx = uniq
	contig := true
	for k := 1; k < len(idx); k++ {
		if idx[k] != idx[0]+k {
			contig = false
			break
		}
	}
	before := 0
	for _, i := range idx {
		if i < targetGap {
			before++
		}
	}
	at := targetGap - before
	if contig && at == idx[0] {
		return false
	}
	moving := make([]Block, 0, len(idx))
	for _, i := range idx {
		moving = append(moving, d.Blocks[i])
	}
	d.Edit("move", func() {
		// Remove from the end so earlier indexes stay valid.
		for k := len(idx) - 1; k >= 0; k-- {
			d.Blocks = slices.Delete(d.Blocks, idx[k], idx[k]+1)
		}
		at = max(0, min(at, len(d.Blocks)))
		d.Blocks = slices.Insert(d.Blocks, at, moving...)
	})
	return true
}

// MoveTo puts one block at index `to` without recording undo; a drag calls
// it on every step and records a single step when it ends.
func (d *Doc) MoveTo(id int64, to int) {
	if d.ReadOnly {
		return
	}
	from := d.Index(id)
	if from < 0 || from == to {
		return
	}
	b := d.Blocks[from]
	d.Blocks = slices.Delete(d.Blocks, from, from+1)
	to = max(0, min(to, len(d.Blocks)))
	d.Blocks = slices.Insert(d.Blocks, to, b)
}

func (d *Doc) Duplicate(ids []int64) []int64 {
	var out []int64
	d.Edit("duplicate", func() {
		idx := make([]int, 0, len(ids))
		for _, id := range ids {
			idx = append(idx, d.Index(id))
		}
		slices.Sort(idx)
		last := idx[len(idx)-1]
		var copies []Block
		for _, i := range idx {
			c := d.Blocks[i]
			c.ID = newID()
			copies = append(copies, c)
			out = append(out, c.ID)
		}
		d.Blocks = slices.Insert(d.Blocks, last+1, copies...)
	})
	return out
}

func (d *Doc) DeleteBlocks(ids []int64) {
	d.Edit("delete blocks", func() {
		first := len(d.Blocks)
		for _, id := range ids {
			if i := d.Index(id); i >= 0 {
				first = min(first, i)
				d.Blocks = slices.Delete(d.Blocks, i, i+1)
			}
		}
		if len(d.Blocks) == 0 {
			d.Blocks = []Block{NewBlock(Paragraph, "")}
		}
		first = min(first, len(d.Blocks)-1)
		d.Focused = false
		d.Caret = Pos{d.Blocks[first].ID, 0}
		d.Anchor = d.Caret
	})
}

// JoinLines replaces the line breaks inside each block with spaces, as one
// undo step: Kvit's "Remove line breaks".
func (d *Doc) JoinLines(ids []int64) {
	d.Edit("join lines", func() {
		for _, id := range ids {
			if b := d.Block(id); b != nil && b.Kind.HasInline() {
				b.Text = strings.ReplaceAll(b.Text, "\n", " ")
			}
		}
		if b := d.CaretBlock(); b != nil {
			d.Caret.Off = min(d.Caret.Off, len(runes(b.Text)))
			d.Anchor = d.Caret
		}
	})
}

// typedConversions are the prefixes that turn a paragraph into another kind
// as they are typed (usage.md "Block types").
var typedConversions = []struct {
	prefix string
	kind   Kind
}{
	{"#### ", Heading4}, {"### ", Heading3}, {"## ", Heading2}, {"# ", Heading1},
	{"- [ ] ", Todo}, {"- ", Bullet}, {"* ", Bullet},
	{"1. ", Numbered}, {"> ", Quote}, {"```", Code},
}

// ApplyTypedConversion runs after typing. It is a separate undo step, so one
// Ctrl+Z brings back the literal "# ".
func (d *Doc) ApplyTypedConversion() bool {
	b := d.CaretBlock()
	if b == nil {
		return false
	}
	if b.Kind == Paragraph && (b.Text == "---" || b.Text == "***") {
		d.Edit("convert", func() {
			b.Text = ""
			b.Kind = Divider
			i := d.Index(b.ID)
			nb := NewBlock(Paragraph, "")
			d.Blocks = slices.Insert(d.Blocks, i+1, nb)
			d.SetCaret(nb.ID, 0)
		})
		return true
	}
	if b.Kind == Bullet && strings.HasPrefix(b.Text, "[ ] ") && d.Caret.Off == 4 {
		d.Edit("convert", func() {
			b.Kind = Todo
			b.Text = b.Text[4:]
			d.SetCaret(b.ID, 0)
		})
		return true
	}
	if b.Kind != Paragraph {
		return false
	}
	for _, c := range typedConversions {
		n := len(runes(c.prefix))
		if strings.HasPrefix(b.Text, c.prefix) && d.Caret.Off == n {
			d.Edit("convert", func() {
				b.Kind = c.kind
				b.Text = string(runes(b.Text)[n:])
				d.SetCaret(b.ID, 0)
			})
			return true
		}
	}
	return false
}

// ToggleFormat is Ctrl+B and friends: wrap the selection in the marker, or
// unwrap it when the selection is exactly a span of that marker. With no
// selection it inserts an empty pair with the caret between them.
func (d *Doc) ToggleFormat(marker string) {
	b := d.CaretBlock()
	if b == nil || !b.Kind.HasInline() || d.CrossBlock() {
		return
	}
	a, c := d.Anchor.Off, d.Caret.Off
	if a > c {
		a, c = c, a
	}
	r := runes(b.Text)
	m := runes(marker)
	n := len(m)
	d.Edit("format", func() {
		if a >= n && c+n <= len(r) && string(r[a-n:a]) == marker && string(r[c:c+n]) == marker {
			b.Text = string(r[:a-n]) + string(r[a:c]) + string(r[c+n:])
			d.Anchor = Pos{b.ID, a - n}
			d.Caret = Pos{b.ID, c - n}
			return
		}
		b.Text = string(r[:a]) + marker + string(r[a:c]) + marker + string(r[c:])
		if a == c {
			d.SetCaret(b.ID, a+n)
			return
		}
		d.Anchor = Pos{b.ID, a + n}
		d.Caret = Pos{b.ID, c + n}
	})
}

// Word boundaries for Ctrl+Left / Ctrl+Right, on the source text.
func wordLeft(r []rune, off int) int {
	for off > 0 && !isWord(r[off-1]) {
		off--
	}
	for off > 0 && isWord(r[off-1]) {
		off--
	}
	return off
}

func wordRight(r []rune, off int) int {
	for off < len(r) && !isWord(r[off]) {
		off++
	}
	for off < len(r) && isWord(r[off]) {
		off++
	}
	return off
}

func wordAt(r []rune, off int) (int, int) {
	a, b := off, off
	for a > 0 && (isWord(r[a-1]) || r[a-1] == '_') {
		a--
	}
	for b < len(r) && (isWord(r[b]) || r[b] == '_') {
		b++
	}
	if a == b && b < len(r) && !unicode.IsSpace(r[b]) {
		b++
	}
	return a, b
}

func (d *Doc) Words() (words, chars int) {
	for _, b := range d.Blocks {
		chars += len(runes(b.Text))
		words += len(strings.Fields(b.Text))
	}
	return
}

// colorSpanAt is the innermost colour span holding the selection, or false.
func (d *Doc) colorSpanAt() (span, bool) {
	b := d.CaretBlock()
	if b == nil || d.CrossBlock() {
		return span{}, false
	}
	a, c := d.Anchor.Off, d.Caret.Off
	if a > c {
		a, c = c, a
	}
	var found span
	ok := false
	for _, sp := range parseInline(runes(b.Text)) {
		if sp.Kind == sColor && sp.CStart <= a && c <= sp.CEnd {
			found, ok = sp, true
		}
	}
	return found, ok
}

// CurrentColor is the text colour of the colour span holding the selection,
// "" for none.
func (d *Doc) CurrentColor() string {
	sp, _ := d.colorSpanAt()
	return sp.Color
}

// SetColor gives the selected text a colour, as Kvit writes one:
// <span style="color:VALUE">…</span>. Inside a colour span it changes that
// span's colour, and an empty value takes the span away, keeping its text.
func (d *Doc) SetColor(value string) {
	b := d.CaretBlock()
	if b == nil || !b.Kind.HasInline() || d.CrossBlock() {
		return
	}
	a, c := d.Anchor.Off, d.Caret.Off
	if a > c {
		a, c = c, a
	}
	r := runes(b.Text)
	open := `<span style="color:` + value + `">`
	if sp, ok := d.colorSpanAt(); ok && (a == c || a == sp.CStart && c == sp.CEnd) {
		d.Edit("format", func() {
			inner := string(r[sp.CStart:sp.CEnd])
			if value == "" {
				b.Text = string(r[:sp.Start]) + inner + string(r[sp.End:])
				d.Anchor = Pos{b.ID, sp.Start + a - sp.CStart}
				d.Caret = Pos{b.ID, sp.Start + c - sp.CStart}
				return
			}
			b.Text = string(r[:sp.Start]) + open + inner + string(r[sp.CEnd:])
			shift := len(runes(open)) - (sp.CStart - sp.Start)
			d.Anchor = Pos{b.ID, a + shift}
			d.Caret = Pos{b.ID, c + shift}
		})
		return
	}
	if a == c || value == "" {
		return
	}
	d.Edit("format", func() {
		b.Text = string(r[:a]) + open + string(r[a:c]) + "</span>" + string(r[c:])
		n := len(runes(open))
		d.Anchor = Pos{b.ID, a + n}
		d.Caret = Pos{b.ID, c + n}
	})
}
