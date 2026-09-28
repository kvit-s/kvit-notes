package diagram

// What the ported tests share. The Qt tests measure with a real font,
// "sans-serif" at 14 pixels; these measure with fixedMeasurer, 8 pixels a
// character and 17 a line, which is close to DejaVu Sans at that size, so
// a test comparing positions checks the relation the Qt test checks rather
// than a pixel value. Mathematics is measured by fakeMath, which stands in
// for MicroTeX: it rejects what MicroTeX rejects in the Qt tests, an
// unmatched brace and a bare alignment `&`, and makes a fraction two lines
// tall.

import (
	"strings"
	"unicode/utf8"

	"github.com/kvit-s/kvit-notes/mermaid"
)

type fixedMeasurer struct{}

func (fixedMeasurer) Advance(s string) float64 { return 8 * float64(utf8.RuneCountInString(s)) }
func (fixedMeasurer) Height() float64          { return 17 }

type fakeMath struct{}

func (fakeMath) Size(tex string) (Size, bool) {
	depth := 0
	for i, r := range tex {
		switch r {
		case '{':
			depth++
		case '}':
			depth--
			if depth < 0 {
				return Size{}, false
			}
		case '&':
			if i == 0 || tex[i-1] != '\\' {
				return Size{}, false
			}
		}
	}
	if depth != 0 || strings.TrimSpace(tex) == "" {
		return Size{}, false
	}
	h := 20.0
	if strings.Contains(tex, `\frac`) {
		h = 42
	}
	return Size{6*float64(utf8.RuneCountInString(tex)) + 4, h}, true
}

// testOpts is the Qt tests' opts(): sans-serif at 14 pixels, top to bottom.
func testOpts() LayoutOptions {
	return LayoutOptions{
		FontFamily: "sans-serif",
		FontSize:   14,
		Measure:    fixedMeasurer{},
		Math:       fakeMath{},
	}
}

func parse(src string) mermaid.ParseResult { return mermaid.Parse(src) }

func parseFlow(src string) *mermaid.FlowchartAst {
	r := mermaid.Parse(src)
	return &r.Flowchart
}

// layoutAny lays out whichever family the source names, so a fixture can
// be written as a user would type it.
func layoutAny(src string) Scene {
	r := mermaid.Parse(src)
	o := testOpts()
	switch r.Type {
	case mermaid.Flowchart:
		o.Direction = r.Flowchart.Direction
		return LayoutFlowchart(&r.Flowchart, o)
	case mermaid.Sequence:
		return LayoutSequence(&r.Sequence, o)
	case mermaid.Class:
		return LayoutClass(&r.Class, o)
	case mermaid.State:
		return LayoutState(&r.State, o)
	case mermaid.Er:
		return LayoutEr(&r.Er, o)
	}
	return Scene{}
}

// shapeRect is the rectangle of the last shape drawing node id.
func shapeRect(s Scene, id string) Rect {
	var r Rect
	for _, sh := range s.Shapes {
		if sh.NodeID == id {
			r = sh.Rect
		}
	}
	return r
}

// sameShapePositions reports whether two scenes put their shapes at the
// same places, as the Qt tests' layoutDeterministic functions check.
func sameShapePositions(a, b Scene) bool {
	if len(a.Shapes) != len(b.Shapes) {
		return false
	}
	for i := range a.Shapes {
		if a.Shapes[i].Rect.X != b.Shapes[i].Rect.X || a.Shapes[i].Rect.Y != b.Shapes[i].Rect.Y {
			return false
		}
	}
	return true
}
