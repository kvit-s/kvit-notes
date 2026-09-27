package editor

// Laying out one block's text with its inline formatting, and the geometry
// the caret needs: where a drawn offset is, and which drawn offset is at a
// point. The text package shapes and wraps the text; this file decides what
// is drawn (the projection of the source) and in which style.

import (
	"math"
	"slices"

	"github.com/kvit-s/kvit-ui/text"
)

// blockLayout is one block's text as drawn: the projection of its source,
// laid out at a width. Offsets into the layout are drawn offsets; the
// projection maps them to and from source offsets.
type blockLayout struct {
	proj  projection
	text  *text.Layout
	style text.Style // the block's base style
	pitch float32
	width float32
}

// layoutKey is everything a block's layout depends on, so a layout is kept
// until one of them changes.
type layoutKey struct {
	text       string
	kind       Kind
	checked    bool
	width      float32
	caret      int // -1 when the block does not hold the caret
	selA, selB int // the selection inside the block, in source offsets
	generation int // bumped when the theme or typography changes
}

// styleFor is the style of a drawn character with the given inline flags.
func (e *Editor) styleFor(f runeFlags, base text.Style) text.Style {
	t := e.tok()
	st := base
	if f&fMarker != 0 {
		st.Color = colour(t.Marker)
		return withSelection(e, st, f)
	}
	if f&fBold != 0 {
		st.Weight = text.Bold
	}
	if f&fItalic != 0 {
		st.Italic = true
	}
	if f&fStrike != 0 {
		st.Strike = true
	}
	if f&fUnderline != 0 {
		st.Underline = true
	}
	if f&fHighlight != 0 {
		st.Background = colour(t.HighlightBackground)
	}
	if f&fCode != 0 {
		st.Family = e.monoFamily()
		st.Background = colour(t.InlineCodeBackground)
	}
	if f&fLink != 0 {
		st.Color = colour(t.Link)
		st.Underline = true
	}
	return withSelection(e, st, f)
}

// withSelection draws selected text in the accent's ground and label colour.
func withSelection(e *Editor, st text.Style, f runeFlags) text.Style {
	if f&fSelected != 0 {
		t := e.tok()
		st.Background = colour(t.Accent)
		st.Color = colour(t.OnAccent)
	}
	return st
}

// layOut lays a block's text out at a width for the given caret and
// selection. caret is -1 when the block does not hold the caret, and
// selA == selB when nothing in it is selected.
func (e *Editor) layOut(b *Block, width float32, caret, selA, selB int) *blockLayout {
	src := []rune(b.Text)
	var spans []span
	if b.Kind.HasInline() {
		spans = parseInline(src)
	}
	var reveal func(span) bool
	if caret >= 0 {
		// A selection reveals the spans it covers only inside one block, as
		// Kvit does; across blocks the markers stay hidden.
		a, c := selA, selB
		reveal = func(sp span) bool { return revealed(sp, caret, a, c) }
	}
	proj := project(src, spans, reveal)
	fl := proj.flags
	if b.Kind == Todo && b.Checked {
		fl = slices.Clone(fl)
		for k := range fl {
			fl[k] |= fStrike
		}
	}
	if selA != selB {
		fl = slices.Clone(fl)
		for k := proj.S2D[selA]; k < proj.S2D[selB] && k < len(fl); k++ {
			fl[k] |= fSelected
		}
	}
	base := e.blockStyle(b)
	var runs []text.Span
	for i := 0; i < len(fl); {
		j := i
		for j < len(fl) && fl[j] == fl[i] {
			j++
		}
		runs = append(runs, text.Span{Text: string(proj.Disp[i:j]), Style: e.styleFor(fl[i], base)})
		i = j
	}
	if len(runs) == 0 {
		runs = []text.Span{{Style: base}}
	}
	pitch := e.pitch(base)
	tl := e.ui.Fonts.Layout(runs, text.Options{MaxWidth: width, Pitch: pitch, KeepTrailingSpace: true})
	return &blockLayout{proj: proj, text: tl, style: base, pitch: pitch, width: width}
}

// height is the height of the laid-out text, down to a whole pixel, as Qt's
// text areas report theirs, so rows stack at whole pixels however many
// lines each holds.
func (l *blockLayout) height() float32 {
	_, h := l.text.Size()
	return float32(math.Floor(float64(h) + 1e-3))
}

// lines is the number of visual lines.
func (l *blockLayout) lines() int { return l.text.LineCount() }

// lineOf is the visual line a drawn offset is on. An offset at the end of a
// wrapped line belongs to the line after it, where the caret is drawn.
func (l *blockLayout) lineOf(d int) int {
	n := l.text.LineCount()
	for i := 0; i < n; i++ {
		_, end, _, _ := l.text.LineBounds(i)
		if d < end || (d == end && l.hardEnd(i)) || i == n-1 {
			return i
		}
	}
	return 0
}

// hardEnd reports whether line i ends at a line break or the end of the
// text rather than by wrapping.
func (l *blockLayout) hardEnd(i int) bool {
	_, end, _, _ := l.text.LineBounds(i)
	return end >= len(l.proj.Disp) || l.proj.Disp[end] == '\n'
}

// lineStart is the first drawn offset on line i.
func (l *blockLayout) lineStart(i int) int {
	start, _, _, _ := l.text.LineBounds(i)
	return start
}

// lineEnd is the last caret stop on line i: before the line break or the
// space it wrapped at.
func (l *blockLayout) lineEnd(i int) int {
	start, end, _, _ := l.text.LineBounds(i)
	if !l.hardEnd(i) && end > start {
		return end - 1
	}
	return end
}

// caretAt is where a caret before drawn offset d goes: its x and the top of
// its line, relative to the text's origin.
func (l *blockLayout) caretAt(d int) (x, top float32) {
	x, top, _ = l.text.CaretAt(d)
	return x, top
}

// lineMiddle is the vertical middle of line i, for finding a point on it.
func (l *blockLayout) lineMiddle(i int) float32 {
	_, _, top, h := l.text.LineBounds(i)
	return top + h/2
}

// hitTest is the drawn offset nearest to a point relative to the text's
// origin.
func (l *blockLayout) hitTest(x, y float32) int {
	return min(l.text.IndexAt(x, y), len(l.proj.D2S))
}

// drawn is the drawn offset of a source offset.
func (l *blockLayout) drawn(src int) int {
	return l.proj.S2D[min(max(src, 0), len(l.proj.S2D)-1)]
}
