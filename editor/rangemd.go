package editor

// A selection as Markdown that stands on its own, as the Qt core's
// DocumentSelection::rangeMarkdown and InlineMarkdown::markdownForRange give
// it: what "comment", "send to chat" and "ask in" quote, and what a drawn
// document copies.

import (
	"slices"
	"strings"
)

// TextRange is a selection as block indexes and Markdown offsets, start before
// end. StartIndex and EndIndex are -1 when nothing is selected.
type TextRange struct {
	StartIndex, StartPos, EndIndex, EndPos int
}

// NoRange is what SelectionRange answers when nothing is selected.
var NoRange = TextRange{StartIndex: -1, EndIndex: -1}

// SelectionRange is the text selection as block indexes and Markdown
// offsets, NoRange when there is none.
func (d *Doc) SelectionRange() TextRange {
	if !d.HasSelection() {
		return NoRange
	}
	a, b := d.SelRange()
	ia, ib := d.Index(a.Block), d.Index(b.Block)
	if ia < 0 || ib < 0 {
		return NoRange
	}
	return TextRange{StartIndex: ia, StartPos: a.Off, EndIndex: ib, EndPos: b.Off}
}

// HasSelection reports whether the editor has text or whole blocks
// selected: what a host offering actions on a selection asks.
func (e *Editor) HasSelection() bool {
	return e.Doc.HasSelection() || len(e.blockSel) > 0
}

// SelectionRange is the editor's selection as block indexes and Markdown
// offsets: the text selection, or for blocks selected whole, the run from
// the start of the first to the end of the last; NoRange when nothing is
// selected.
func (e *Editor) SelectionRange() TextRange {
	if len(e.blockSel) > 0 {
		idx := e.IndexesOf(e.SelectedBlocks())
		if len(idx) == 0 {
			return NoRange
		}
		last := idx[len(idx)-1]
		return TextRange{StartIndex: idx[0], EndIndex: last, EndPos: len(runes(e.Doc.Blocks[last].Text))}
	}
	return e.Doc.SelectionRange()
}

// SelectionMarkdown is the editor's selection as Markdown that stands on its
// own (RangeMarkdown), "" when nothing is selected.
func (e *Editor) SelectionMarkdown() string {
	r := e.SelectionRange()
	if r.StartIndex < 0 {
		return ""
	}
	return e.Doc.markdownOf(r)
}

// ClearSelection drops the text selection and the block selection, and
// leaves the caret where it is.
func (e *Editor) ClearSelection() {
	if !e.HasSelection() {
		return
	}
	e.clearBlockSel()
	e.Doc.Anchor = e.Doc.Caret
	e.changed()
}

// RangeMarkdown is the selection as Markdown: a whole block written with its
// prefix, fence or number, and a partly selected block as a fragment whose
// inline markers are balanced, so a sweep starting inside a bold phrase
// comes back as "**phrase**" rather than "phrase**". Two whole list items
// are one line apart and everything else a blank line apart. It is "" when
// nothing is selected.
func (d *Doc) RangeMarkdown() string {
	r := d.SelectionRange()
	if r.StartIndex < 0 {
		return ""
	}
	return d.markdownOf(r)
}

// markdownOf writes a range of the document as RangeMarkdown does.
func (d *Doc) markdownOf(r TextRange) string {
	if r.StartIndex == r.EndIndex && r.StartPos == r.EndPos {
		return ""
	}
	var out strings.Builder
	have, prevList := false, false
	for i := r.StartIndex; i <= r.EndIndex && i < len(d.Blocks); i++ {
		blk := d.Blocks[i]
		src := runes(blk.Text)
		from, to := 0, len(src)
		if i == r.StartIndex {
			from = min(max(0, r.StartPos), len(src))
		}
		if i == r.EndIndex {
			to = min(max(0, r.EndPos), len(src))
		}
		if from >= to && len(src) > 0 {
			continue
		}
		whole := from == 0 && to == len(src)
		var text string
		switch {
		case whole:
			text = BlockMarkdown(blk, ListNumber(d.Blocks, i))
		case verbatim(blk.Kind):
			text = string(src[from:to])
		default:
			text = inlineFragment(src, from, to)
		}
		list := whole && blk.Kind.IsList()
		if have {
			if prevList && list {
				out.WriteString("\n")
			} else {
				out.WriteString("\n\n")
			}
		}
		out.WriteString(text)
		have, prevList = true, list
	}
	return out.String()
}

// inlineFragment is the characters of src between Markdown offsets from and
// to as Markdown of their own (InlineMarkdown::markdownForRange). Marker
// characters in the range never count: a span whose visible text is wholly
// inside the range is written as it stands, nested markers and all, and one
// only partly inside has its markers written around each piece of it, so
// every piece parses on its own.
func inlineFragment(src []rune, from, to int) string {
	spans := parseInline(src)
	marker := make([]bool, len(src))
	for _, sp := range spans {
		for k := sp.Start; k < sp.CStart; k++ {
			marker[k] = true
		}
		for k := sp.CEnd; k < sp.End; k++ {
			marker[k] = true
		}
	}
	// Each span's parent is the innermost span whose text holds it; spans
	// come outer first.
	parent := make([]int, len(spans))
	for j := range spans {
		parent[j] = -1
		for p := j - 1; p >= 0; p-- {
			if spans[p].CStart <= spans[j].Start && spans[j].End <= spans[p].CEnd {
				parent[j] = p
				break
			}
		}
	}
	root := func(j int) int {
		for parent[j] >= 0 {
			j = parent[j]
		}
		return j
	}
	// The span a visible character belongs to: the innermost whose text
	// holds it.
	owner := func(k int) int {
		o := -1
		for j, sp := range spans {
			if sp.CStart <= k && k < sp.CEnd {
				o = j
			}
		}
		return o
	}
	total := make([]int, len(spans))
	covered := make([]int, len(spans))
	for k := range src {
		if marker[k] {
			continue
		}
		if o := owner(k); o >= 0 {
			top := root(o)
			total[top]++
			if from <= k && k < to {
				covered[top]++
			}
		}
	}
	var out strings.Builder
	var open []int
	sync := func(target []int) {
		if slices.Equal(open, target) {
			return
		}
		for k := len(open) - 1; k >= 0; k-- {
			sp := spans[open[k]]
			out.WriteString(string(src[sp.CEnd:sp.End]))
		}
		for _, j := range target {
			sp := spans[j]
			out.WriteString(string(src[sp.Start:sp.CStart]))
		}
		open = target
	}
	lastWhole := -1
	for k := from; k < to; k++ {
		if marker[k] {
			continue
		}
		o := owner(k)
		if o < 0 {
			sync(nil)
			out.WriteRune(src[k])
			continue
		}
		top := root(o)
		if covered[top] == total[top] {
			sync(nil)
			if lastWhole != top {
				sp := spans[top]
				out.WriteString(string(src[sp.Start:sp.End]))
				lastWhole = top
			}
			continue
		}
		var chain []int
		for j := o; j >= 0; j = parent[j] {
			chain = append([]int{j}, chain...)
		}
		sync(chain)
		out.WriteRune(src[k])
	}
	sync(nil)
	return out.String()
}
