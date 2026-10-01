package editor

// The find bar's matches, which the editor draws behind the text: every
// match in the search colour and the current one in its own. The find bar
// works out the matches; the editor only draws them.

import (
	"fmt"
	"strings"

	"github.com/richardwilkes/toolbox/v2/geom"
)

// Mark is one match to draw, in rune offsets of a block's Markdown source.
type Mark struct {
	Block      int64
	Start, End int
	Current    bool
}

// SetMarks replaces the matches drawn; nil takes them all away.
func (e *Editor) SetMarks(marks []Mark) {
	e.marks = map[int64][]Mark{}
	for _, m := range marks {
		e.marks[m.Block] = append(e.marks[m.Block], m)
	}
	e.MarkForRedraw()
}

// RevealBlockRange scrolls the view to a range of a block's source.
func (e *Editor) RevealBlockRange(id int64, start int) {
	i := e.Doc.Index(id)
	if i < 0 || i >= len(e.tops) {
		return
	}
	l := e.layout(i)
	x, top := l.caretAt(l.drawn(start))
	o := e.textOrigin(i)
	room := e.px(40)
	e.ScrollRectIntoView(geom.NewRect(o.X+x, o.Y+top-room, 1, l.pitch+2*room))
}

// marksKey writes a block's marks as text, for the layout cache's key.
func marksKey(ms []Mark) string {
	if len(ms) == 0 {
		return ""
	}
	var b strings.Builder
	for _, m := range ms {
		fmt.Fprintf(&b, "%d-%d-%t;", m.Start, m.End, m.Current)
	}
	return b.String()
}

// SpanRange is an inline span of a block's source, in rune offsets: its
// opening marker is [Start, ContentStart) and its closing marker
// [ContentEnd, End).
type SpanRange struct {
	Start, End, ContentStart, ContentEnd int
}

// Spans are the inline spans of a block's source, as the editor reads
// them, for the find bar's mapping of what the reader sees to the source.
func Spans(src string) []SpanRange {
	var out []SpanRange
	for _, sp := range parseInline([]rune(src)) {
		out = append(out, SpanRange{sp.Start, sp.End, sp.CStart, sp.CEnd})
	}
	return out
}
