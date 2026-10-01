package editor

// Inline math: a $…$ span away from the caret is typeset in text style, the
// size TeX sets a formula in running prose, and drawn in place of its source
// with its baseline on the line's. With the caret in it, or a selection over
// it, the span shows its source with the dollars muted.
//
// The projection puts one character, U+FFFC, where the span was, and the
// text layout keeps a box of the formula's width there (text.Box). For a
// formula taller than the text, the box asks for the height of a larger size
// of the text's own font, the smallest whose ascent and descent hold the
// formula's, at the document's line spacing. The formula itself is drawn
// over the laid-out text, where the box is.
//
// TeX that does not typeset stays its source, and so does everything while
// the math library is missing.

import (
	"math"
	"sync"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"

	"github.com/kvit-s/kvit-notes/mathtex"
)

// mathPlaceholder is the character a typeset span is laid out as.
const mathPlaceholder = '￼'

// inlineBox is a typeset $…$ span in a projection: the span, its formula,
// the room the line keeps for it, and the colour the text there is drawn in,
// which the formula takes.
type inlineBox struct {
	span    span
	formula *mathtex.Formula
	box     text.Box
	color   text.Color
}

// inlineMathSize is the size inline math is set at beside text in style st:
// the text's size, matched to its face's x-height.
func (e *Editor) inlineMathSize(st text.Style) int {
	size := int(math.Round(float64(st.Size)))
	return mathtex.OpticalMathSize(size, mathtex.TextXHeight(e.ui.Fonts, st))
}

// typesetInline replaces every $…$ span of p whose markers are hidden, and
// whose TeX typesets, with one placeholder character, laid out as the
// formula's box. base is the style of the text around the spans.
func (e *Editor) typesetInline(p projection, base text.Style) projection {
	if !mathtex.Available() {
		return p
	}
	type typeset struct {
		sp  span
		box *inlineBox
	}
	var found []typeset
	for _, sp := range p.Spans {
		// A span shows its markers when the caret or a selection touches
		// it; then its opening dollar is drawn.
		if sp.Kind != sMath || p.S2D[sp.Start] != p.S2D[sp.CStart] {
			continue
		}
		f, err := mathtex.Render(string(p.Src[sp.CStart:sp.CEnd]), e.inlineMathSize(base), false)
		if err != nil || f.Width <= 0 {
			continue
		}
		found = append(found, typeset{sp, &inlineBox{span: sp, formula: f, box: e.mathBox(f, base)}})
	}
	if len(found) == 0 {
		return p
	}
	out := projection{Src: p.Src, Spans: p.Spans, S2D: make([]int, len(p.Src)+1), boxes: map[int]*inlineBox{}}
	drawn := func(i int) (int, bool) {
		d := p.S2D[i]
		return d, d < len(p.D2S) && p.D2S[d] == i
	}
	k := 0
	for i := 0; i < len(p.Src); {
		if k < len(found) && i == found[k].sp.Start {
			sp := found[k].sp
			d := len(out.Disp)
			for j := sp.Start; j < sp.End; j++ {
				out.S2D[j] = d
			}
			fl := fMathBox
			if c, ok := drawn(sp.CStart); ok {
				fl |= p.flags[c] &^ fMath
			}
			out.Disp = append(out.Disp, mathPlaceholder)
			out.flags = append(out.flags, fl)
			if p.colors != nil {
				color := ""
				if c, ok := drawn(sp.CStart); ok {
					color = p.colors[c]
				}
				out.colors = append(out.colors, color)
			}
			out.D2S = append(out.D2S, sp.Start)
			out.boxes[d] = found[k].box
			i = sp.End
			k++
			continue
		}
		out.S2D[i] = len(out.Disp)
		if d, ok := drawn(i); ok {
			out.Disp = append(out.Disp, p.Disp[d])
			out.flags = append(out.flags, p.flags[d])
			if p.colors != nil {
				out.colors = append(out.colors, p.colors[d])
			}
			out.D2S = append(out.D2S, i)
		}
		i++
	}
	out.S2D[len(p.Src)] = len(out.Disp)
	return out
}

// mathBox is the room a line keeps for formula f in text of style st. When
// the formula fits within the font's ascent and descent, the line keeps its
// height. Otherwise the line is as tall as the text's font at the smallest
// whole size whose ascent and descent hold the formula's, spaced by the
// document's line height, and at least deep enough for the formula.
func (e *Editor) mathBox(f *mathtex.Formula, st text.Style) text.Box {
	ascent, descent := float32(f.Baseline), float32(f.Height-f.Baseline)
	asc, desc := e.fontExtents(st)
	size := st.Size
	if ascent <= asc*size && descent <= desc*size {
		return text.Box{Width: float32(f.Width), Ascent: ascent, Descent: descent}
	}
	p := float32(math.Max(1, math.Round(float64(size))))
	for p < 256 && (asc*p < ascent || desc*p < descent) {
		p++
	}
	// Below the baseline, the room the line spacing leaves under that
	// size's ascent, and never less than the formula's own depth: with a
	// face whose ascent takes most of the line, such as Segoe UI, the
	// spacing alone left a fraction hanging into the line below.
	lead := float32(e.ui.Typography.LineHeight())
	return text.Box{Width: float32(f.Width), Ascent: asc * p, Descent: max(p*lead-asc*p, descent)}
}

// fontExtent is a face's ascent and descent per em.
type fontExtent struct{ ascent, descent float32 }

var (
	extentsMu sync.Mutex
	extents   = map[text.Style]fontExtent{}
)

// fontExtents is the ascent and descent per em of the face text in style st
// is set in, measured once per face from a line laid out at 1000 pixels.
func (e *Editor) fontExtents(st text.Style) (ascent, descent float32) {
	const probe = 1000
	key := text.Style{Family: st.Family, Weight: st.Weight, Italic: st.Italic}
	extentsMu.Lock()
	x, ok := extents[key]
	extentsMu.Unlock()
	if !ok {
		p := key
		p.Size = probe
		l := e.ui.Fonts.Layout([]text.Span{{Text: "x", Style: p}}, text.Options{LineHeight: 1})
		_, h := l.Size()
		b := l.Baseline()
		x = fontExtent{b / probe, (h - b) / probe}
		extentsMu.Lock()
		extents[key] = x
		extentsMu.Unlock()
	}
	return x.ascent, x.descent
}

// mathBoxStyle is the style of a placeholder's run: the box of its formula.
// The colour the run would be drawn in is kept for the formula.
func mathBoxStyle(p projection, d int, st text.Style) text.Style {
	if bx := p.boxes[d]; bx != nil {
		st.Box = bx.box
		bx.color = st.Color
	}
	return st
}

// drawInlineMath draws the typeset spans of a text laid out from p with its
// top-left corner at (x, y): each formula where its box landed, its baseline
// on its line's, on a whole device pixel.
func drawInlineMath(gc *unison.Canvas, l *text.Layout, p projection, x, y float32) {
	for d, bx := range p.boxes {
		cx, _, _ := l.CaretAt(d)
		line := 0
		for i := 0; i < l.LineCount(); i++ {
			if start, end, _, _ := l.LineBounds(i); d >= start && d < end {
				line = i
				break
			}
		}
		at := snapToDevice(gc, geom.NewPoint(x+cx, y+l.LineBaseline(line)-float32(bx.formula.Baseline)))
		bx.formula.Draw(gc, at.X, at.Y, bx.color.Unison())
	}
}

// inlineText lays out a line of inline Markdown outside a block, as a task
// board card's description is, markers hidden and math typeset, wrapped at
// width; the projection is what drawInlineMath draws the formulas from.
func (e *Editor) inlineText(src string, st text.Style, width float32) (*text.Layout, projection) {
	r := []rune(src)
	p := e.typesetInline(project(r, parseInline(r), nil), st)
	return e.ui.Fonts.Layout(e.runs(p, p.flags, st), text.Options{MaxWidth: max(1, width)}), p
}
