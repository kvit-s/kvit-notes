package editor

// The math labels of Mermaid diagrams: a label that is one $$…$$ expression
// is typeset in display style at the optical size of the diagram's text, and
// drawn centred in the box the layout sized from the same measurement. The
// diagram block asks through Editor.DiagramMath, which the editor sets to
// this when the app has not set its own.

import (
	"math"
	"sync/atomic"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"

	"github.com/kvit-s/kvit-notes/diagram"
	"github.com/kvit-s/kvit-notes/mathtex"
)

// useMath gives the math library the editor's fonts, for the text in
// formulas no math font covers, and typesets diagrams' math labels unless
// the app has set DiagramMath itself. It runs when the editor is made.
func (e *Editor) useMath() {
	mathtex.UseFonts(e.ui.Fonts)
	if e.DiagramMath == nil {
		m := &diagramMath{e: e}
		m.refresh()
		e.DiagramMath = m
	}
}

// diagramMath typesets diagram labels with the math library. A diagram is
// laid out on a goroutine of its own, so what Size needs from the editor,
// the x-height of the note's text face per em, is measured on the interface
// thread and kept in xHeight.
type diagramMath struct {
	e       *Editor
	xHeight atomic.Uint64 // math.Float64bits of the text face's x-height per em
}

// refresh measures the x-height of the note's text face.
func (m *diagramMath) refresh() {
	const probe = 1000
	ty := m.e.ui.Typography
	st := text.Style{Family: ty.FontFamily(), Size: probe, Weight: text.Regular, Color: colour(m.e.tok().TextPrimary)}
	m.xHeight.Store(math.Float64bits(mathtex.TextXHeight(m.e.ui.Fonts, st) / probe))
}

// size is the math size beside diagram text of textSize pixels: its optical
// size, with the x-height rounded up to a whole pixel at that size, as
// TextXHeight gives it for the text of a note.
func (m *diagramMath) size(textSize float32) int {
	xh := math.Ceil(math.Float64frombits(m.xHeight.Load())*float64(textSize) - 1e-9)
	return mathtex.OpticalMathSize(int(math.Round(float64(textSize))), xh)
}

// Size is tex typeset beside text of textSize pixels, and false when math
// is off or the TeX does not typeset.
func (m *diagramMath) Size(tex string, textSize float32) (diagram.Size, bool) {
	if !mathtex.Available() {
		return diagram.Size{}, false
	}
	f, err := mathtex.Render(tex, m.size(textSize), true)
	if err != nil || f.Width <= 0 || f.Height <= 0 {
		return diagram.Size{}, false
	}
	return diagram.Size{W: f.Width, H: f.Height}, true
}

// Draw typesets tex with its top left at origin in colour c.
func (m *diagramMath) Draw(gc *unison.Canvas, tex string, textSize float32, origin geom.Point, c unison.Color) bool {
	m.refresh()
	if !mathtex.Available() {
		return false
	}
	f, err := mathtex.Render(tex, m.size(textSize), true)
	if err != nil || f.Width <= 0 {
		return false
	}
	f.Draw(gc, origin.X, origin.Y, c)
	return true
}
