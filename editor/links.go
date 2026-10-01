package editor

// Links in the text: a press on a link follows it when Ctrl (Cmd on macOS) is
// held, or when its block is not the one being edited, as a note being read
// is clicked; any other press puts the caret in it. What following means,
// opening a note or a web page, is the application's. The link dialog
// (Ctrl+K) finds the link at the caret here and writes the one it is given.

import (
	"strings"

	"github.com/kvit-s/kvit-notes/links"
)

// LinkRef is a link a press follows.
type LinkRef struct {
	// Wiki is true for [[note]], [[note#heading|alias]] and [[#heading]];
	// Target is then what is between the brackets.
	Wiki bool
	// Target is a wiki link's inside, or a Markdown link's address: a web
	// address, a path, or "#heading".
	Target string
}

// linkAt is the link whose text holds a source offset of a block. A wiki
// link counts over its whole source, so a press on its hidden target while
// its source shows still follows it; the alias is what shows, the target
// what following resolves.
func linkAt(src string, off int) (LinkRef, span, bool) {
	r := []rune(src)
	for _, sp := range parseInline(r) {
		if sp.Kind != sLink && sp.Kind != sWiki {
			continue
		}
		if sp.Kind == sWiki {
			if off < sp.Start || off > sp.End {
				continue
			}
			if l, ok := links.MatchAt(r, sp.Start); ok && l.Start+l.Length == sp.End {
				return LinkRef{Wiki: true, Target: l.Target}, sp, true
			}
			return LinkRef{Wiki: true, Target: string(r[sp.CStart:sp.CEnd])}, sp, true
		}
		if off < sp.CStart || off > sp.CEnd {
			continue
		}
		if sp.Start == sp.CStart {
			// A bare address is its own link.
			return LinkRef{Target: string(r[sp.Start:sp.End])}, sp, true
		}
		// [text](address): the address is between "](" and ")".
		closing := string(r[sp.CEnd:sp.End])
		return LinkRef{Target: strings.TrimSuffix(strings.TrimPrefix(closing, "]("), ")")}, sp, true
	}
	return LinkRef{}, span{}, false
}

// followAt follows the link at a position a press landed on, when the
// press should follow it, and reports whether it did.
func (e *Editor) followAt(pos Pos, ctrl bool) bool {
	if e.FollowLink == nil {
		return false
	}
	d := e.Doc
	editing := d.Focused && d.Caret.Block == pos.Block
	if !ctrl && editing {
		return false
	}
	b := d.Block(pos.Block)
	if b == nil || !b.Kind.HasInline() {
		return false
	}
	ref, _, ok := linkAt(b.Text, pos.Off)
	if !ok {
		return false
	}
	e.FollowLink(ref)
	return true
}

// LinkAtCaret is the Markdown link, [text](address), the caret is in: its
// block, its range in the block's source, its text and its address.
func (e *Editor) LinkAtCaret() (id int64, start, end int, text, address string, ok bool) {
	d := e.Doc
	b := d.CaretBlock()
	if b == nil || !d.Focused || !b.Kind.HasInline() {
		return 0, 0, 0, "", "", false
	}
	ref, sp, found := linkAt(b.Text, d.Caret.Off)
	if !found || ref.Wiki || sp.Start == sp.CStart {
		return 0, 0, 0, "", "", false
	}
	r := []rune(b.Text)
	return b.ID, sp.Start, sp.End, string(r[sp.CStart:sp.CEnd]), ref.Target, true
}

// ReplaceRange replaces a range of a block's source, as one undo step, and
// puts the caret at an offset of the new text.
func (e *Editor) ReplaceRange(id int64, start, end int, replacement string, caret int) {
	d := e.Doc
	b := d.Block(id)
	if b == nil || d.ReadOnly {
		return
	}
	r := []rune(b.Text)
	start, end = max(0, min(start, len(r))), max(0, min(end, len(r)))
	d.Edit("link", func() {
		b.Text = string(r[:start]) + replacement + string(r[end:])
		d.SetCaret(id, min(caret, len([]rune(b.Text))))
	})
	e.done()
}

// RemoveLinkAt removes the link formatting at a position, keeping its text:
// "[text](address)" becomes "text". Only a Markdown link is removable; a bare
// address is its own text and a wiki link keeps its brackets. It reports
// whether it removed one.
func (e *Editor) RemoveLinkAt(pos Pos) bool {
	d := e.Doc
	b := d.Block(pos.Block)
	if b == nil || d.ReadOnly || !b.Kind.HasInline() {
		return false
	}
	r := []rune(b.Text)
	for _, sp := range parseInline(r) {
		if sp.Kind != sLink || pos.Off < sp.Start || pos.Off > sp.End {
			continue
		}
		if sp.Start == sp.CStart {
			return false
		}
		text := string(r[sp.CStart:sp.CEnd])
		at := sp.Start + len([]rune(text))
		d.Edit("remove link", func() {
			b.Text = string(r[:sp.Start]) + text + string(r[sp.End:])
			d.SetCaret(pos.Block, at)
		})
		e.clearBlockSel()
		e.RequestFocus()
		e.touched()
		e.changed()
		return true
	}
	return false
}

// Headings are the note's headings for the link dialog's heading list:
// each one's level, text and the anchor a link reaches it by.
func (e *Editor) Headings() (levels []int, texts, anchors []string) {
	for _, en := range e.Doc.tocEntries() {
		levels = append(levels, en.level)
		texts = append(texts, en.text)
		anchors = append(anchors, en.slug)
	}
	return
}

// GoToAnchor puts the caret at the heading a "#anchor" names, and reports
// whether there is one.
func (e *Editor) GoToAnchor(anchor string) bool {
	for _, en := range e.Doc.tocEntries() {
		if en.slug == anchor {
			e.goToHeading(en.block)
			return true
		}
	}
	return false
}

// GoToHeading puts the caret at the heading a heading's text names, as
// [[note#Heading]] does, and reports whether there is one.
func (e *Editor) GoToHeading(heading string) bool {
	entries := e.Doc.tocEntries()
	var hs []links.Heading
	for _, en := range entries {
		hs = append(hs, links.Heading{Block: en.block, Text: en.text})
	}
	if k := links.FindHeading(hs, heading); k >= 0 {
		e.goToHeading(entries[k].block)
		return true
	}
	return false
}
