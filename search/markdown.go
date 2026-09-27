package search

// A block's Markdown against the text the reader sees, which is what a
// replacement needs: a match is found in the display text, and replacing it
// has to change the Markdown. The rules are the Qt app's, from
// src/content/inlinemarkdown.cpp (segmentsFor, documentToMarkdown,
// markdownToDocument and cutRangeResult) with no span revealed, which is
// how the find bar sees a block.

import (
	"slices"
	"unicode/utf8"
)

// Span is one inline span of a block's Markdown, in rune offsets of that
// Markdown: [Start, ContentStart) is its opening marker, [ContentEnd, End)
// its closing marker and [ContentStart, ContentEnd) its content. For a link
// the closing marker is "](url)". Spans may nest; a nested span lies inside
// its parent's content. The editor's parser finds them.
type Span struct {
	Start, End               int
	ContentStart, ContentEnd int
}

// Block is one block of a note as replacing needs it: its Markdown, the
// inline spans in it, and whether it is verbatim (a code block, whose text
// is its Markdown and whose markers are ordinary characters).
type Block struct {
	Markdown string
	Spans    []Span
	Verbatim bool
}

type segKind uint8

const (
	segPlain   segKind = iota // text outside every span
	segContent                // a span's own content, between its children
	segMarker                 // a hidden marker: in the Markdown, not displayed
)

// seg is one piece of the Markdown and where it is displayed
// (inlinemarkdown.cpp Seg). Plain and content pieces are displayed as they
// are; a marker takes no room. There is always a plain piece at the end,
// possibly empty, so positions past the end extend from it.
type seg struct {
	kind        segKind
	doc, docLen int
	md, mdLen   int
	owner       int // the span a content piece belongs to, -1 for plain text
}

// node is a span in the tree of nested spans.
type node struct {
	Span
	parent   int
	children []int
}

// layout is a block's Markdown cut into pieces.
type layout struct {
	src   []rune
	segs  []seg
	nodes []node
}

// valid reports whether a span's offsets are in order and inside n runes.
func (s Span) valid(n int) bool {
	return 0 <= s.Start && s.Start <= s.ContentStart && s.ContentStart <= s.ContentEnd &&
		s.ContentEnd <= s.End && s.End <= n
}

// layoutOf builds the pieces of a block's Markdown. Spans that overlap
// without nesting, or reach outside the text, are ignored.
func layoutOf(markdown string, spans []Span) *layout {
	l := &layout{src: []rune(markdown)}
	order := slices.Clone(spans)
	slices.SortFunc(order, func(a, b Span) int {
		if a.Start != b.Start {
			return a.Start - b.Start
		}
		return b.End - a.End
	})
	var top []int
	var stack []int
	topEnd := 0
	for _, s := range order {
		if !s.valid(len(l.src)) {
			continue
		}
		for len(stack) > 0 {
			p := l.nodes[stack[len(stack)-1]]
			if p.ContentStart <= s.Start && s.End <= p.ContentEnd {
				break
			}
			stack = stack[:len(stack)-1]
		}
		parent := -1
		if len(stack) > 0 {
			parent = stack[len(stack)-1]
			if kids := l.nodes[parent].children; len(kids) > 0 && l.nodes[kids[len(kids)-1]].End > s.Start {
				continue
			}
		} else if s.Start < topEnd {
			continue
		}
		i := len(l.nodes)
		l.nodes = append(l.nodes, node{Span: s, parent: parent})
		if parent >= 0 {
			l.nodes[parent].children = append(l.nodes[parent].children, i)
		} else {
			top = append(top, i)
			topEnd = s.End
		}
		stack = append(stack, i)
	}

	doc, md := 0, 0
	var emit func(i int)
	emit = func(i int) {
		n := l.nodes[i]
		l.segs = append(l.segs, seg{kind: segMarker, doc: doc, md: n.Start, mdLen: n.ContentStart - n.Start, owner: i})
		at := n.ContentStart
		content := func(to int) {
			if to > at {
				l.segs = append(l.segs, seg{kind: segContent, doc: doc, docLen: to - at, md: at, mdLen: to - at, owner: i})
				doc += to - at
			}
		}
		for _, c := range n.children {
			content(l.nodes[c].Start)
			emit(c)
			at = l.nodes[c].End
		}
		content(n.ContentEnd)
		l.segs = append(l.segs, seg{kind: segMarker, doc: doc, md: n.ContentEnd, mdLen: n.End - n.ContentEnd, owner: i})
	}
	for _, i := range top {
		if s := l.nodes[i].Start; s > md {
			l.segs = append(l.segs, seg{kind: segPlain, doc: doc, docLen: s - md, md: md, mdLen: s - md, owner: -1})
			doc += s - md
		}
		emit(i)
		md = l.nodes[i].End
	}
	rest := len(l.src) - md
	l.segs = append(l.segs, seg{kind: segPlain, doc: doc, docLen: rest, md: md, mdLen: rest, owner: -1})
	return l
}

// text is the display text: the Markdown without its markers.
func (l *layout) text() string {
	out := make([]rune, 0, len(l.src))
	for _, s := range l.segs {
		if s.docLen > 0 {
			out = append(out, l.src[s.md:s.md+s.docLen]...)
		}
	}
	return string(out)
}

// toMarkdown maps a display offset to the Markdown (documentToMarkdown). At
// the left edge of a span's content it lands after the opening marker, and
// at the right edge after the closing marker.
func (l *layout) toMarkdown(d int) int {
	for _, s := range l.segs {
		if s.docLen > 0 && d < s.doc+s.docLen {
			return s.md + d - s.doc
		}
	}
	last := l.segs[len(l.segs)-1]
	return last.md + last.mdLen + d - (last.doc + last.docLen)
}

// toDisplay maps a Markdown offset to the display text
// (markdownToDocument). An offset inside a marker lands on the nearest edge
// of the content.
func (l *layout) toDisplay(m int) int {
	for _, s := range l.segs {
		if s.mdLen > 0 && m < s.md+s.mdLen {
			if s.docLen == 0 {
				return s.doc
			}
			return s.doc + m - s.md
		}
	}
	last := l.segs[len(l.segs)-1]
	return last.doc + last.docLen + m - (last.md + last.mdLen)
}

// cut removes the display range [ds, de) from the Markdown as cutting it in
// the editor would (cutRangeResult): plain text goes character for
// character; a span whose whole content is in the range goes with its
// markers, the outermost such span winning; a span only partly in the range
// loses those characters and keeps its markers, so what is left keeps its
// formatting. It returns the Markdown and the Markdown offset where the
// removed text was.
func (l *layout) cut(ds, de int) ([]rune, int) {
	total := make([]int, len(l.nodes))
	covered := make([]int, len(l.nodes))
	overlap := func(s seg) (int, int) { return max(ds, s.doc), min(de, s.doc+s.docLen) }
	for _, s := range l.segs {
		if s.kind != segContent {
			continue
		}
		a, b := overlap(s)
		for k := s.owner; k != -1; k = l.nodes[k].parent {
			total[k] += s.docLen
			covered[k] += max(0, b-a)
		}
	}
	whole := func(k int) int {
		best := -1
		for ; k != -1; k = l.nodes[k].parent {
			if total[k] > 0 && covered[k] == total[k] {
				best = k
			}
		}
		return best
	}

	type rng struct{ a, b int }
	var removals []rng
	for _, s := range l.segs {
		if s.docLen <= 0 || s.kind == segMarker {
			continue
		}
		a, b := overlap(s)
		if a >= b {
			continue
		}
		if s.kind == segContent {
			if h := whole(s.owner); h != -1 {
				r := rng{l.nodes[h].Start, l.nodes[h].End}
				if len(removals) == 0 || removals[len(removals)-1] != r {
					removals = append(removals, r)
				}
				continue
			}
		}
		removals = append(removals, rng{s.md + a - s.doc, s.md + b - s.doc})
	}

	cursor := l.toMarkdown(ds)
	if len(removals) > 0 {
		cursor = removals[0].a
	}
	out := slices.Clone(l.src)
	for i := len(removals) - 1; i >= 0; i-- {
		out = slices.Delete(out, removals[i].a, removals[i].b)
	}
	return out, cursor
}

// Text is the block's display text: its Markdown without the markers of its
// spans, or the Markdown itself for a verbatim block. It is the text Find
// searches.
func (b Block) Text() string {
	if b.Verbatim {
		return b.Markdown
	}
	return layoutOf(b.Markdown, b.Spans).text()
}

// MarkdownPos maps an offset in the display text to the Markdown, as the Qt
// app does to put the caret at a match (BlockPositions::markdownPosition,
// and CollectionSearch::markdownPosition for a search result). The answer
// is kept inside the Markdown.
func (b Block) MarkdownPos(display int) int {
	n := utf8.RuneCountInString(b.Markdown)
	d := max(0, display)
	if b.Verbatim {
		return min(d, n)
	}
	return max(0, min(layoutOf(b.Markdown, b.Spans).toMarkdown(d), n))
}

// DisplayPos maps an offset in the Markdown to the display text, as the Qt
// app does with the caret where a search starts and with the ends of the
// selection a search is kept inside (BlockPositions::displayPosition).
func (b Block) DisplayPos(markdown int) int {
	m := max(0, min(markdown, utf8.RuneCountInString(b.Markdown)))
	if b.Verbatim {
		return m
	}
	return layoutOf(b.Markdown, b.Spans).toDisplay(m)
}

// Replace replaces the display range [start, end) with replacement, as
// selecting that text and typing would (DocumentSearch::replaceRange): the
// range is cut by the rule of cut and the replacement goes in where it was.
// The replacement is Markdown, so "**loud**" arrives bold. A verbatim block
// is spliced directly. It returns the new Markdown and the Markdown offset
// just after the replacement.
func (b Block) Replace(start, end int, replacement string) (markdown string, after int) {
	if b.Verbatim {
		src := []rune(b.Markdown)
		start = max(0, min(start, len(src)))
		end = max(start, min(end, len(src)))
		out := slices.Concat(src[:start], []rune(replacement), src[end:])
		return string(out), start + utf8.RuneCountInString(replacement)
	}
	out, at := layoutOf(b.Markdown, b.Spans).cut(start, end)
	at = max(0, min(at, len(out)))
	out = slices.Insert(out, at, []rune(replacement)...)
	return string(out), at + utf8.RuneCountInString(replacement)
}
