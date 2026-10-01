package diagram

// Checks of the path and rectangle geometry in geometry.go and of the
// outlines and markers in outlines.go. The expected values of the geometry
// checks were recorded from the earlier Qt version of Kvit Notes.

import (
	"math"
	"testing"
)

func near(a, b float64) bool { return math.Abs(a-b) < 1e-6 }

func TestRectTouchingIsNotIntersecting(t *testing.T) {
	a := Rect{0, 0, 10, 10}
	if a.Intersects(Rect{10, 0, 5, 5}) {
		t.Error("rectangles sharing an edge intersect")
	}
	if !a.Intersects(Rect{9, 9, 5, 5}) {
		t.Error("overlapping rectangles do not intersect")
	}
	if a.Intersects(Rect{2, 2, 0, 5}) {
		t.Error("a rectangle with no width intersects")
	}
	if !a.Contains(Point{10, 10}) {
		t.Error("a corner is not contained")
	}
	if u := (Rect{}).United(a); u != a {
		t.Errorf("a null rectangle stretches the union: %v", u)
	}
}

func TestOutlinePercentAlongPolyline(t *testing.T) {
	var o Outline
	o.MoveTo(Point{0, 0})
	o.LineTo(Point{10, 0})
	o.LineTo(Point{10, 30})
	if !near(o.Length(), 40) {
		t.Errorf("length = %g", o.Length())
	}
	if p := o.PointAtPercent(0.5); !near(p.X, 10) || !near(p.Y, 10) {
		t.Errorf("middle = %v, want (10, 10)", p)
	}
	// Down the page is 270 degrees, measured with y up.
	if a := o.AngleAtPercent(0.5); !near(a, 270) {
		t.Errorf("angle = %g, want 270", a)
	}
	if a := o.AngleAtPercent(0.1); !near(a, 0) {
		t.Errorf("angle = %g, want 0", a)
	}
	// A line to where the pen is adds nothing.
	o.LineTo(Point{10, 30})
	if len(o.Segs) != 3 {
		t.Errorf("segments = %d, want 3", len(o.Segs))
	}
}

func TestOutlineBoundsAreTight(t *testing.T) {
	var o Outline
	o.MoveTo(Point{0, 0})
	o.QuadTo(Point{50, 100}, Point{100, 0})
	b := o.Bounds()
	// The curve peaks at half its control point's height.
	if !near(b.H, 50) || !near(b.W, 100) {
		t.Errorf("bounds = %v, want 100 x 50", b)
	}
	// The outline keeps a quadratic curve as the cubic whose control points
	// are two thirds of the way to the quadratic's, and the control bounds
	// measure those.
	if cb := o.ControlBounds(); !near(cb.H, 200.0/3) {
		t.Errorf("control bounds = %v", cb)
	}
}

func TestOutlineIntersectsCountsTheClosingLine(t *testing.T) {
	// An open path around a rectangle's corner: its lines miss the
	// rectangle, and the line that would close it runs through it, which
	// Intersects counts.
	var o Outline
	o.MoveTo(Point{0, 50})
	o.LineTo(Point{0, 0})
	o.LineTo(Point{50, 0})
	if !o.Intersects(Rect{20, 20, 5, 5}) {
		t.Error("the closing line does not count")
	}
	if o.Intersects(Rect{40, 40, 5, 5}) {
		t.Error("a rectangle beyond the closing line intersects")
	}
	var line Outline
	line.MoveTo(Point{0, 10})
	line.LineTo(Point{100, 10})
	if !line.Intersects(Rect{40, 0, 10, 20}) {
		t.Error("a line through a rectangle does not intersect it")
	}
	if line.Intersects(Rect{40, 20, 10, 20}) {
		t.Error("a line beside a rectangle intersects it")
	}
}

func TestOutlineContainsOddEven(t *testing.T) {
	var o Outline
	o.AddEllipse(Rect{0, 0, 100, 50})
	if !o.Contains(Point{50, 25}) {
		t.Error("the centre is outside the ellipse")
	}
	if o.Contains(Point{2, 2}) {
		t.Error("the corner is inside the ellipse")
	}
	// The ellipse passes through the middle of each side.
	b := o.Bounds()
	if !near(b.X, 0) || !near(b.W, 100) || !near(b.H, 50) {
		t.Errorf("bounds = %v", b)
	}
}

func TestShapeOutlinesStayInTheirRectangle(t *testing.T) {
	r := Rect{10, 20, 120, 60}
	for kind := KindRect; kind <= KindOdd; kind++ {
		o := ShapeOutline(Shape{Kind: kind, Rect: r})
		if o.Empty() {
			t.Errorf("kind %d has no outline", kind)
			continue
		}
		b := o.Bounds()
		if b.X < r.X-1e-9 || b.Y < r.Y-1e-9 || b.Right() > r.Right()+1e-9 || b.Bottom() > r.Bottom()+1e-9 {
			t.Errorf("kind %d outline %v leaves %v", kind, b, r)
		}
		if !o.Contains(r.Center()) {
			t.Errorf("kind %d does not hold its centre", kind)
		}
	}
	if !ShapeOutline(Shape{Kind: KindActor, Rect: r}).Empty() {
		t.Error("an actor has an outline")
	}
	if ActorFigure(r).Empty() {
		t.Error("an actor has no figure")
	}
}

func TestMarkersReachWhatMarkerLengthSays(t *testing.T) {
	tip := Point{100, 100}
	for kind := MarkerArrow; kind <= MarkerErZeroMany; kind++ {
		m := MarkerOutline(kind, tip, Point{1, 0})
		if m.Outline.Empty() {
			t.Errorf("marker %d has no outline", kind)
			continue
		}
		if !m.Filled && m.StrokeWidth == 0 {
			t.Errorf("marker %d is neither filled nor stroked", kind)
		}
		// Pointing right, a marker reaches back to the left of the tip by
		// its length, less at most its stroke.
		b := m.Outline.Bounds()
		if reach := tip.X - b.Left(); math.Abs(reach-MarkerLength(kind)) > 1 {
			t.Errorf("marker %d reaches %g, MarkerLength says %g", kind, reach, MarkerLength(kind))
		}
	}
	if !MarkerOutline(MarkerArrow, tip, Point{}).Outline.Empty() {
		t.Error("a marker with no direction has an outline")
	}
}
