package mathtex

import (
	"math"
	"strings"
	"sync"
	"unsafe"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// MicroTeX has no math font for the characters of scripts it does not know
// (Han, Arabic, Devanagari and the rest) or for \Textit, \Textbf and
// \Textitbf. It sets those in a named family, "Serif" unless \externalFont
// names another, and asks the back end how wide and tall the text is
// (TextRenderingBox in box/box_single.cpp). The library passes the question
// to measureText here, which lays the text out with the app's text fonts;
// Draw then draws it with the same fonts where the formula put it.

var (
	textMu    sync.Mutex
	textFonts *text.Fonts
)

// UseFonts gives the fonts text MicroTeX has no math font for is measured
// and drawn with: the app's own, which fall back through the system's fonts
// for any script. Until it is called such text is laid out from an estimate
// and not drawn. Formulas rendered before with other fonts are forgotten,
// since their layout may change; giving the same fonts again changes nothing,
// so every editor an app opens can call it.
func UseFonts(fonts *text.Fonts) {
	textMu.Lock()
	same := textFonts == fonts
	textFonts = fonts
	textMu.Unlock()
	if !same {
		clearCache()
	}
}

func currentFonts() *text.Fonts {
	textMu.Lock()
	defer textMu.Unlock()
	return textFonts
}

var (
	callbackOnce sync.Once
	callbackFn   uintptr
)

// measureCallback is the C function pointer to measureText, made once: a
// process can make only a limited number of callbacks.
func measureCallback() uintptr {
	callbackOnce.Do(func() { callbackFn = newCallback(measureText) })
	return callbackFn
}

// textStyle is the text package's style for text MicroTeX sets in family at
// size with its style bits (1 bold, 2 italic). "Serif" and "SansSerif" are
// the Java-style generic names MicroTeX uses.
func textStyle(family string, style uint8, size float32, c text.Color) text.Style {
	switch strings.ToLower(family) {
	case "serif":
		family = "serif"
	case "sansserif", "sans-serif":
		family = text.SansSerif
	}
	st := text.Style{Family: family, Size: size, Italic: style&2 != 0, Color: c}
	if style&1 != 0 {
		st.Weight = text.Bold
	}
	return st
}

// measureText answers the library's question about text it has no font
// for: the text's bounds at its size with the origin on the baseline, as
// the QFontMetricsF::boundingRect gave them to the app: from the ascent
// above the baseline to the descent below it, and the advance width across.
func measureText(t *textRequest) uintptr {
	s := unsafe.String(t.text, t.textLen)
	// An estimate, used without fonts or if laying out fails: half an em
	// per character, and the usual ascent and descent.
	t.x, t.y, t.w, t.h = 0, -0.8*t.size, 0.5*t.size*float32(len([]rune(s))), t.size
	defer func() { _ = recover() }()
	fonts := currentFonts()
	if fonts == nil {
		return 0
	}
	family := unsafe.String(t.family, t.familyLen)
	l := fonts.Layout([]text.Span{{Text: s, Style: textStyle(family, uint8(t.style), t.size, text.Color{})}}, text.Options{})
	w, h := l.Size()
	t.x, t.y, t.w, t.h = 0, -l.Baseline(), w, h
	return 0
}

// drawText draws a text command: laid out at the size it ends up at on the
// canvas when the transform only enlarges and moves, else under the
// transform.
func drawText(gc *unison.Canvas, c *command, fg unison.Color) {
	fonts := currentFonts()
	if fonts == nil || c.text == "" {
		return
	}
	col := c.colour(fg)
	tc := text.Color{R: uint8(col.Red()), G: uint8(col.Green()), B: uint8(col.Blue()), A: uint8(col.Alpha())}
	m := c.m
	size := c.size
	at := m.TransformPoint(geom.NewPoint(c.x, c.y))
	s := uniformScale(m)
	if s == 0 {
		gc.Save()
		defer gc.Restore()
		gc.Concat(m)
		at = geom.NewPoint(c.x, c.y)
	} else {
		size *= s
	}
	l := fonts.Layout([]text.Span{{Text: c.text, Style: textStyle(c.family, c.style, size, tc)}}, text.Options{})
	l.Draw(gc, at.X, at.Y-l.Baseline())
}

var (
	xHeightMu sync.Mutex
	xHeights  = map[text.Style]float64{}
)

// TextXHeight is the x-height, in pixels, of text in style st with fonts:
// the height of the ink of a lowercase x in its family, weight and slant at
// its size, which OpticalMathSize matches the math to, rounded up to a whole
// pixel as the x-height the app read from the font metrics was. Kvit's
// pictures show the rounding: beside 15 px DejaVu Sans, whose x is 8.2 px
// tall, its display math is set at 20 px, which is what a 9 px x-height gives
// and 8.2 px does not.
//
// The height per em is measured once per face by drawing an x large, in the
// style's colour, and finding its ink; the drawing is never shown, but the
// colour has to be opaque for the x to leave ink. The result is 0 when the
// measuring fails.
func TextXHeight(fonts *text.Fonts, st text.Style) float64 {
	if fonts == nil || st.Size <= 0 || st.Color.A == 0 {
		return 0
	}
	face := text.Style{Family: st.Family, Weight: st.Weight, Italic: st.Italic}
	xHeightMu.Lock()
	perEm, ok := xHeights[face]
	xHeightMu.Unlock()
	if !ok {
		perEm = measureXHeight(fonts, st)
		xHeightMu.Lock()
		xHeights[face] = perEm
		xHeightMu.Unlock()
	}
	return math.Ceil(perEm*float64(st.Size) - 1e-9)
}

// measureXHeight draws an x of face st at 200 pixels and returns its ink
// height per em.
func measureXHeight(fonts *text.Fonts, st text.Style) float64 {
	const size = 200
	probe := text.Style{Family: st.Family, Weight: st.Weight, Italic: st.Italic, Size: size, Color: st.Color}
	l := fonts.Layout([]text.Span{{Text: "x", Style: probe}}, text.Options{})
	w, h := l.Size()
	img, err := unison.NewImageFromDrawing(int(math.Ceil(float64(w)))+20, int(math.Ceil(float64(h)))+20, 72,
		func(gc *unison.Canvas) { l.Draw(gc, 10, 10) })
	if err != nil {
		return 0
	}
	px, err := img.ToNRGBA()
	if err != nil {
		return 0
	}
	top, bottom := -1, -1
	b := px.Bounds()
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			if px.NRGBAAt(x, y).A >= 128 {
				if top < 0 {
					top = y
				}
				bottom = y
				break
			}
		}
	}
	if top < 0 {
		return 0
	}
	return float64(bottom-top+1) / size
}
