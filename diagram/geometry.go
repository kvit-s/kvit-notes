package diagram

// The geometry the layouts need from the QPointF, QRectF and QPainterPath,
// written to give the same answers as the qpointf.h, qrect.cpp,
// qpainterpath.cpp and qbezier.cpp for the paths layout builds: the fuzzy
// comparisons, a rectangle that touches another without overlapping it, the
// point and angle part way along a path, its length and bounds, and whether
// it meets a rectangle.

import "math"

// fuzzyCompare is qFuzzyCompare for doubles: equal to about 12 digits.
func fuzzyCompare(a, b float64) bool {
	return math.Abs(a-b)*1e12 <= min(math.Abs(a), math.Abs(b))
}

// fuzzyIsNull is qFuzzyIsNull for doubles.
func fuzzyIsNull(d float64) bool { return math.Abs(d) <= 1e-12 }

// fuzzyEqual compares one coordinate as 6 compares QPointF's.
func fuzzyEqual(a, b float64) bool {
	if a == 0 || b == 0 {
		return fuzzyIsNull(a - b)
	}
	return fuzzyCompare(a, b)
}

// Add is p + q.
func (p Point) Add(q Point) Point { return Point{p.X + q.X, p.Y + q.Y} }

// Sub is p - q.
func (p Point) Sub(q Point) Point { return Point{p.X - q.X, p.Y - q.Y} }

// Mul is p scaled by k.
func (p Point) Mul(k float64) Point { return Point{p.X * k, p.Y * k} }

// Div is p divided by k.
func (p Point) Div(k float64) Point { return Point{p.X / k, p.Y / k} }

// Neg is -p.
func (p Point) Neg() Point { return Point{-p.X, -p.Y} }

// Len is the distance from the origin to p.
func (p Point) Len() float64 { return math.Hypot(p.X, p.Y) }

// Equal compares two points as QPointF's == does, allowing for rounding.
func (p Point) Equal(q Point) bool { return fuzzyEqual(p.X, q.X) && fuzzyEqual(p.Y, q.Y) }

// unit is d scaled to length 1, or d itself when it is too short to have a
// direction; the layouts write this inline each time.
func unit(d Point) Point {
	if l := d.Len(); l > 0.001 {
		return d.Div(l)
	}
	return d
}

// rectAround is the size-by-size rectangle centred on c.
func rectAround(c Point, s Size) Rect {
	return Rect{c.X - s.W/2, c.Y - s.H/2, s.W, s.H}
}

// Left is the smallest x.
func (r Rect) Left() float64 { return r.X }

// Top is the smallest y.
func (r Rect) Top() float64 { return r.Y }

// Right is X + W.
func (r Rect) Right() float64 { return r.X + r.W }

// Bottom is Y + H.
func (r Rect) Bottom() float64 { return r.Y + r.H }

// Center is the middle of the rectangle.
func (r Rect) Center() Point { return Point{r.X + r.W/2, r.Y + r.H/2} }

// TopLeft is the corner at X, Y.
func (r Rect) TopLeft() Point { return Point{r.X, r.Y} }

// IsNull reports a rectangle with no width and no height, which is what an
// unset QRectF is.
func (r Rect) IsNull() bool { return r.W == 0 && r.H == 0 }

// Normalized is the same rectangle with a positive width and height.
func (r Rect) Normalized() Rect {
	if r.W < 0 {
		r.X, r.W = r.X+r.W, -r.W
	}
	if r.H < 0 {
		r.Y, r.H = r.Y+r.H, -r.H
	}
	return r
}

// Adjusted moves the left, top, right and bottom edges by the four amounts.
func (r Rect) Adjusted(dx1, dy1, dx2, dy2 float64) Rect {
	return Rect{r.X + dx1, r.Y + dy1, r.W + dx2 - dx1, r.H + dy2 - dy1}
}

// Translated is the rectangle moved by d.
func (r Rect) Translated(d Point) Rect { return Rect{r.X + d.X, r.Y + d.Y, r.W, r.H} }

// Contains reports whether p is inside the rectangle or on its border. A
// rectangle with no width or no height contains nothing.
func (r Rect) Contains(p Point) bool {
	n := r.Normalized()
	if n.W == 0 || n.H == 0 {
		return false
	}
	return p.X >= n.X && p.X <= n.Right() && p.Y >= n.Y && p.Y <= n.Bottom()
}

// Intersects reports whether the two rectangles overlap. Rectangles that only
// touch along an edge do not, and neither does one with no width or height.
func (r Rect) Intersects(o Rect) bool {
	a, b := r.Normalized(), o.Normalized()
	if a.W == 0 || b.W == 0 || a.H == 0 || b.H == 0 {
		return false
	}
	if a.X >= b.Right() || b.X >= a.Right() {
		return false
	}
	if a.Y >= b.Bottom() || b.Y >= a.Bottom() {
		return false
	}
	return true
}

// United is the smallest rectangle holding both. A null rectangle counts as
// nothing, so it does not stretch the union to the origin.
func (r Rect) United(o Rect) Rect {
	if r.IsNull() {
		return o
	}
	if o.IsNull() {
		return r
	}
	a, b := r.Normalized(), o.Normalized()
	left, top := min(a.X, b.X), min(a.Y, b.Y)
	right, bottom := max(a.Right(), b.Right()), max(a.Bottom(), b.Bottom())
	return Rect{left, top, right - left, bottom - top}
}

// ---- Outline builders (QPainterPath's) ----

// last is the point the outline ends at, or the origin for an empty one.
func (o *Outline) last() Point {
	if len(o.Segs) == 0 {
		return Point{}
	}
	return segEnd(o.Segs, len(o.Segs)-1)
}

// segEnd is where segment i leaves the pen. A Close returns it to the start
// of its subpath.
func segEnd(segs []Segment, i int) Point {
	s := segs[i]
	switch s.Kind {
	case QuadTo:
		return s.Pts[1]
	case CubicTo:
		return s.Pts[2]
	case Close:
		for j := i - 1; j >= 0; j-- {
			if segs[j].Kind == MoveTo {
				return segs[j].Pts[0]
			}
		}
		return Point{}
	}
	return s.Pts[0]
}

// startSubpath begins a subpath at the current point when the outline is
// empty or was just closed, as QPainterPath does before a line or a curve.
func (o *Outline) startSubpath() {
	if len(o.Segs) == 0 {
		o.Segs = append(o.Segs, Segment{Kind: MoveTo})
		return
	}
	if o.Segs[len(o.Segs)-1].Kind == Close {
		o.Segs = append(o.Segs, Segment{Kind: MoveTo, Pts: [3]Point{o.last()}})
	}
}

// MoveTo starts a new subpath at p. A MoveTo straight after another replaces
// it.
func (o *Outline) MoveTo(p Point) {
	if n := len(o.Segs); n > 0 && o.Segs[n-1].Kind == MoveTo {
		o.Segs[n-1].Pts[0] = p
		return
	}
	o.Segs = append(o.Segs, Segment{Kind: MoveTo, Pts: [3]Point{p}})
}

// LineTo draws a straight line to p. A line to the point the pen is already
// at adds nothing.
func (o *Outline) LineTo(p Point) {
	o.startSubpath()
	if p.Equal(o.last()) {
		return
	}
	o.Segs = append(o.Segs, Segment{Kind: LineTo, Pts: [3]Point{p}})
}

// QuadTo draws a quadratic curve with control point c to p.
func (o *Outline) QuadTo(c, p Point) {
	o.startSubpath()
	prev := o.last()
	if prev.Equal(c) && c.Equal(p) {
		return
	}
	o.Segs = append(o.Segs, Segment{Kind: QuadTo, Pts: [3]Point{c, p}})
}

// CubicTo draws a cubic curve with control points c1 and c2 to p.
func (o *Outline) CubicTo(c1, c2, p Point) {
	o.startSubpath()
	if o.last().Equal(c1) && c1.Equal(c2) && c2.Equal(p) {
		return
	}
	o.Segs = append(o.Segs, Segment{Kind: CubicTo, Pts: [3]Point{c1, c2, p}})
}

// Close draws a line back to the start of the current subpath.
func (o *Outline) Close() {
	if o.Empty() || o.Segs[len(o.Segs)-1].Kind == Close {
		return
	}
	o.Segs = append(o.Segs, Segment{Kind: Close})
}

// AddRect adds the rectangle as a closed subpath, clockwise from its top left.
func (o *Outline) AddRect(r Rect) {
	o.MoveTo(Point{r.X, r.Y})
	o.LineTo(Point{r.Right(), r.Y})
	o.LineTo(Point{r.Right(), r.Bottom()})
	o.LineTo(Point{r.X, r.Bottom()})
	o.Close()
}

// AddPolygon adds a subpath through the points, not closed.
func (o *Outline) AddPolygon(pts []Point) {
	if len(pts) == 0 {
		return
	}
	o.MoveTo(pts[0])
	for _, p := range pts[1:] {
		o.LineTo(p)
	}
}

// AddEllipse adds the ellipse filling r as a closed subpath.
func (o *Outline) AddEllipse(r Rect) {
	if r.IsNull() {
		return
	}
	o.ArcMoveTo(r, 0)
	o.ArcTo(r, 0, -360)
	o.Close()
}

// AddRoundedRect adds r with corners of radius rx across and ry down, each
// no more than half the side it is on.
func (o *Outline) AddRoundedRect(r Rect, rx, ry float64) {
	r = r.Normalized()
	if r.IsNull() {
		return
	}
	rx, ry = min(rx, r.W/2), min(ry, r.H/2)
	if rx <= 0 || ry <= 0 {
		o.AddRect(r)
		return
	}
	dx, dy := 2*rx, 2*ry
	o.ArcMoveTo(Rect{r.X, r.Y, dx, dy}, 180)
	o.ArcTo(Rect{r.X, r.Y, dx, dy}, 180, -90)
	o.ArcTo(Rect{r.Right() - dx, r.Y, dx, dy}, 90, -90)
	o.ArcTo(Rect{r.Right() - dx, r.Bottom() - dy, dx, dy}, 0, -90)
	o.ArcTo(Rect{r.X, r.Bottom() - dy, dx, dy}, 270, -90)
	o.Close()
}

// ellipsePoint is the point at angle degrees on the ellipse filling r,
// counting anticlockwise from three o'clock as does.
func ellipsePoint(r Rect, deg float64) Point {
	a := deg * math.Pi / 180
	c := r.Center()
	return Point{c.X + r.W/2*math.Cos(a), c.Y - r.H/2*math.Sin(a)}
}

// ArcMoveTo starts a new subpath at angle degrees on the ellipse filling r.
func (o *Outline) ArcMoveTo(r Rect, deg float64) {
	if r.IsNull() {
		return
	}
	o.MoveTo(ellipsePoint(r, deg))
}

// ArcTo draws part of the ellipse filling r, from start degrees through sweep
// degrees (anticlockwise when positive), with a line first from the current
// point to where the arc starts. Each quarter turn or less is one cubic
// curve, which gives the same curves as for arcs in whole quarter turns.
func (o *Outline) ArcTo(r Rect, start, sweep float64) {
	if r.IsNull() {
		return
	}
	sweep = max(-360, min(360, sweep))
	o.LineTo(ellipsePoint(r, start))
	n := int(math.Ceil(math.Abs(sweep)/90 - 1e-9))
	if n == 0 {
		return
	}
	step := sweep / float64(n)
	k := 4.0 / 3.0 * math.Tan(step*math.Pi/180/4)
	rx, ry := r.W/2, r.H/2
	for i := range n {
		a0 := (start + step*float64(i)) * math.Pi / 180
		a1 := (start + step*float64(i+1)) * math.Pi / 180
		p0 := ellipsePoint(r, start+step*float64(i))
		p3 := ellipsePoint(r, start+step*float64(i+1))
		// The tangent at angle a, in screen coordinates with y down, is
		// (-sin a, -cos a) scaled by the radii.
		c1 := Point{p0.X - k*rx*math.Sin(a0), p0.Y - k*ry*math.Cos(a0)}
		c2 := Point{p3.X + k*rx*math.Sin(a1), p3.Y + k*ry*math.Cos(a1)}
		o.CubicTo(c1, c2, p3)
	}
}

// ---- Outline queries ----

// element is one entry of QPainterPath's element list: a move, a line, or
// the three points of a cubic curve after the point before it.
type element struct {
	kind SegmentKind // MoveTo, LineTo or CubicTo
	pts  [3]Point
}

// elements turns the outline into QPainterPath's elements: a quadratic curve
// becomes the cubic  stores for it, and a Close becomes a line back to the
// start of its subpath, left out when the pen is already there.
func (o Outline) elements() []element {
	var out []element
	var start, cur Point
	for i, s := range o.Segs {
		switch s.Kind {
		case MoveTo:
			out = append(out, element{kind: MoveTo, pts: [3]Point{s.Pts[0]}})
			start, cur = s.Pts[0], s.Pts[0]
		case LineTo:
			if i == 0 {
				out = append(out, element{kind: MoveTo})
			}
			out = append(out, element{kind: LineTo, pts: [3]Point{s.Pts[0]}})
			cur = s.Pts[0]
		case QuadTo:
			if i == 0 {
				out = append(out, element{kind: MoveTo})
			}
			c, e := s.Pts[0], s.Pts[1]
			c1 := Point{(cur.X + 2*c.X) / 3, (cur.Y + 2*c.Y) / 3}
			c2 := Point{(e.X + 2*c.X) / 3, (e.Y + 2*c.Y) / 3}
			out = append(out, element{kind: CubicTo, pts: [3]Point{c1, c2, e}})
			cur = e
		case CubicTo:
			if i == 0 {
				out = append(out, element{kind: MoveTo})
			}
			out = append(out, element{kind: CubicTo, pts: s.Pts})
			cur = s.Pts[2]
		case Close:
			if !cur.Equal(start) {
				out = append(out, element{kind: LineTo, pts: [3]Point{start}})
			}
			cur = start
		}
	}
	return out
}

// elementEnd is where element e leaves the pen.
func (e element) end() Point {
	if e.kind == CubicTo {
		return e.pts[2]
	}
	return e.pts[0]
}

// Empty reports an outline with nothing to draw: no steps, or one move.
func (o Outline) Empty() bool {
	return len(o.Segs) == 0 || (len(o.Segs) == 1 && o.Segs[0].Kind == MoveTo)
}

// HasCurves reports whether any step is a curve.
func (o Outline) HasCurves() bool {
	for _, s := range o.Segs {
		if s.Kind == QuadTo || s.Kind == CubicTo {
			return true
		}
	}
	return false
}

// Translated is the outline moved by d.
func (o Outline) Translated(d Point) Outline {
	segs := make([]Segment, len(o.Segs))
	for i, s := range o.Segs {
		for k := range s.Pts {
			s.Pts[k] = s.Pts[k].Add(d)
		}
		segs[i] = s
	}
	return Outline{Segs: segs}
}

// Clone is a copy that shares nothing with o.
func (o Outline) Clone() Outline {
	if o.Segs == nil {
		return Outline{}
	}
	return Outline{Segs: append([]Segment(nil), o.Segs...)}
}

// bezier is a cubic curve through four points.
type bezier struct{ p1, p2, p3, p4 Point }

// lineBezier is the straight line from a to b as a cubic, as makes one to
// walk a line and a curve alike.
func lineBezier(a, b Point) bezier {
	d := b.Sub(a)
	return bezier{a, a.Add(d.Div(3)), a.Add(d.Mul(2).Div(3)), b}
}

func (b bezier) pointAt(t float64) Point {
	mt := 1 - t
	blend := func(x1, x2, x3, x4 float64) float64 {
		a := x1*mt + x2*t
		b := x2*mt + x3*t
		c := x3*mt + x4*t
		a = a*mt + b*t
		b = b*mt + c*t
		return a*mt + b*t
	}
	return Point{blend(b.p1.X, b.p2.X, b.p3.X, b.p4.X), blend(b.p1.Y, b.p2.Y, b.p3.Y, b.p4.Y)}
}

// split cuts the curve in half.
func (b bezier) split() (bezier, bezier) {
	mid := func(a, c Point) Point { return Point{(a.X + c.X) / 2, (a.Y + c.Y) / 2} }
	m12, m23, m34 := mid(b.p1, b.p2), mid(b.p2, b.p3), mid(b.p3, b.p4)
	m123, m234 := mid(m12, m23), mid(m23, m34)
	m := mid(m123, m234)
	return bezier{b.p1, m12, m123, m}, bezier{m, m234, m34, b.p4}
}

// length is QBezier::length: the control polygon's length where it is within
// 0.01 of the chord, splitting the curve in half until it is.
func (b bezier) length() float64 {
	var total float64
	var add func(b bezier, depth int)
	add = func(b bezier, depth int) {
		poly := b.p2.Sub(b.p1).Len() + b.p3.Sub(b.p2).Len() + b.p4.Sub(b.p3).Len()
		chord := b.p4.Sub(b.p1).Len()
		// The depth limit only stops a curve with coordinates too large for
		// the tolerance from recursing without end.
		if poly-chord > 0.01 && depth < 40 {
			l, r := b.split()
			add(l, depth+1)
			add(r, depth+1)
			return
		}
		total += poly
	}
	add(b, 0)
	return total
}

// Length is the length of every line and curve of the outline.
func (o Outline) Length() float64 {
	els := o.elements()
	var total float64
	for i := 1; i < len(els); i++ {
		switch els[i].kind {
		case LineTo:
			total += els[i].pts[0].Sub(els[i-1].end()).Len()
		case CubicTo:
			total += bezier{els[i-1].end(), els[i].pts[0], els[i].pts[1], els[i].pts[2]}.length()
		}
	}
	return total
}

// bezierAtPercent is the bezierAtT: the line or curve where the fraction t
// of the outline's length falls, the length before it, and its own length.
func (o Outline) bezierAtPercent(t float64) (b bezier, before, own float64, ok bool) {
	els := o.elements()
	total := o.Length()
	last := len(els) - 1
	var cur float64
	for i := 0; i <= last; i++ {
		switch els[i].kind {
		case LineTo:
			a := els[i-1].end()
			l := els[i].pts[0].Sub(a).Len()
			cur += l
			if i == last || cur/total >= t {
				return lineBezier(a, els[i].pts[0]), before, l, true
			}
		case CubicTo:
			bz := bezier{els[i-1].end(), els[i].pts[0], els[i].pts[1], els[i].pts[2]}
			l := bz.length()
			cur += l
			if i == last || cur/total >= t {
				return bz, before, l, true
			}
		}
		before = cur
	}
	return bezier{}, before, 0, false
}

// PointAtPercent is the point the fraction t of the way along the outline by
// length, as QPainterPath::pointAtPercent finds it: the line or curve where
// that length falls, then that curve's point at the matching fraction of its
// own parameter.
func (o Outline) PointAtPercent(t float64) Point {
	if t < 0 || t > 1 {
		return Point{}
	}
	els := o.elements()
	if len(els) == 0 {
		return Point{}
	}
	if len(els) == 1 {
		return els[0].end()
	}
	b, before, own, ok := o.bezierAtPercent(t)
	if !ok {
		return Point{}
	}
	real := (o.Length()*t - before) / own
	if math.IsNaN(real) {
		real = 0
	}
	return b.pointAt(max(0, min(1, real)))
}

// AngleAtPercent is the direction of the outline at the fraction t of its
// length, in degrees anticlockwise from three o'clock with y pointing up, as
// QPainterPath::angleAtPercent gives it.
func (o Outline) AngleAtPercent(t float64) float64 {
	if t < 0 || t > 1 || o.Empty() {
		return 0
	}
	b, before, own, ok := o.bezierAtPercent(t)
	if !ok {
		return 0
	}
	real := (o.Length()*t - before) / own
	slope := func(a, b, c, d float64) float64 {
		return 3*real*real*(d-3*c+3*b-a) + 6*real*(c-2*b+a) + 3*(b-a)
	}
	m1 := slope(b.p1.X, b.p2.X, b.p3.X, b.p4.X)
	m2 := slope(b.p1.Y, b.p2.Y, b.p3.Y, b.p4.Y)
	theta := math.Atan2(-m2, m1) * 180 / math.Pi
	if theta < 0 {
		theta += 360
	}
	if fuzzyCompare(theta, 360) {
		return 0
	}
	return theta
}

// extrema is the tight box around a cubic curve: its ends and every turning
// point between them.
func (b bezier) extrema() Rect {
	minX, maxX := min(b.p1.X, b.p4.X), max(b.p1.X, b.p4.X)
	minY, maxY := min(b.p1.Y, b.p4.Y), max(b.p1.Y, b.p4.Y)
	visit := func(t float64) {
		if t <= 0 || t >= 1 {
			return
		}
		p := b.pointAt(t)
		minX, maxX = min(minX, p.X), max(maxX, p.X)
		minY, maxY = min(minY, p.Y), max(maxY, p.Y)
	}
	roots := func(p1, p2, p3, p4 float64) {
		// The derivative is a t² + b t + c.
		a := 3 * (-p1 + 3*p2 - 3*p3 + p4)
		bb := 6 * (p1 - 2*p2 + p3)
		c := 3 * (p2 - p1)
		if fuzzyIsNull(a) {
			if !fuzzyIsNull(bb) {
				visit(-c / bb)
			}
			return
		}
		disc := bb*bb - 4*a*c
		if disc < 0 {
			return
		}
		sq := math.Sqrt(disc)
		visit((-bb + sq) / (2 * a))
		visit((-bb - sq) / (2 * a))
	}
	roots(b.p1.X, b.p2.X, b.p3.X, b.p4.X)
	roots(b.p1.Y, b.p2.Y, b.p3.Y, b.p4.Y)
	return Rect{minX, minY, maxX - minX, maxY - minY}
}

// Bounds is the smallest rectangle holding everything the outline draws, its
// curves measured exactly rather than by their control points.
func (o Outline) Bounds() Rect {
	els := o.elements()
	if len(els) == 0 {
		return Rect{}
	}
	p := els[0].end()
	minX, maxX, minY, maxY := p.X, p.X, p.Y, p.Y
	for i := 1; i < len(els); i++ {
		switch els[i].kind {
		case MoveTo, LineTo:
			q := els[i].pts[0]
			minX, maxX = min(minX, q.X), max(maxX, q.X)
			minY, maxY = min(minY, q.Y), max(maxY, q.Y)
		case CubicTo:
			r := bezier{els[i-1].end(), els[i].pts[0], els[i].pts[1], els[i].pts[2]}.extrema()
			minX, maxX = min(minX, r.X), max(maxX, r.Right())
			minY, maxY = min(minY, r.Y), max(maxY, r.Bottom())
		}
	}
	return Rect{minX, minY, maxX - minX, maxY - minY}
}

// ControlBounds is the smallest rectangle holding every point of the
// outline, the curves' control points included.
func (o Outline) ControlBounds() Rect {
	els := o.elements()
	if len(els) == 0 {
		return Rect{}
	}
	p := els[0].end()
	minX, maxX, minY, maxY := p.X, p.X, p.Y, p.Y
	for _, e := range els {
		n := 1
		if e.kind == CubicTo {
			n = 3
		}
		for _, q := range e.pts[:n] {
			minX, maxX = min(minX, q.X), max(maxX, q.X)
			minY, maxY = min(minY, q.Y), max(maxY, q.Y)
		}
	}
	return Rect{minX, minY, maxX - minX, maxY - minY}
}

// Flatten turns the outline into polylines, one per subpath, each curve cut
// into straight pieces no longer than about tolerance.
func (o Outline) Flatten(tolerance float64) [][]Point {
	if tolerance <= 0 {
		tolerance = 0.5
	}
	var out [][]Point
	var cur []Point
	for _, e := range o.elements() {
		switch e.kind {
		case MoveTo:
			if len(cur) > 0 {
				out = append(out, cur)
			}
			cur = []Point{e.pts[0]}
		case LineTo:
			cur = append(cur, e.pts[0])
		case CubicTo:
			b := bezier{cur[len(cur)-1], e.pts[0], e.pts[1], e.pts[2]}
			poly := b.p2.Sub(b.p1).Len() + b.p3.Sub(b.p2).Len() + b.p4.Sub(b.p3).Len()
			n := max(4, min(256, int(math.Ceil(poly/tolerance))))
			for k := 1; k <= n; k++ {
				cur = append(cur, b.pointAt(float64(k)/float64(n)))
			}
		}
	}
	if len(cur) > 0 {
		out = append(out, cur)
	}
	return out
}

// Contains reports whether p is inside the area the outline encloses, each
// subpath closed and the odd-even rule deciding, as QPainterPath::contains
// does with its default fill rule. Curves are followed as fine polylines.
func (o Outline) Contains(p Point) bool {
	if o.Empty() || !o.ControlBounds().Contains(p) {
		return false
	}
	winding := 0
	cross := func(a, b Point) {
		x1, y1, x2, y2 := a.X, a.Y, b.X, b.Y
		dir := 1
		if fuzzyCompare(y1, y2) {
			return // a horizontal line counts for nothing
		} else if y2 < y1 {
			x1, x2 = x2, x1
			y1, y2 = y2, y1
			dir = -1
		}
		if p.Y >= y1 && p.Y < y2 {
			if x := x1 + (x2-x1)/(y2-y1)*(p.Y-y1); x <= p.X {
				winding += dir
			}
		}
	}
	for _, poly := range o.Flatten(0.25) {
		for i := 1; i < len(poly); i++ {
			cross(poly[i-1], poly[i])
		}
		if last := poly[len(poly)-1]; !last.Equal(poly[0]) {
			cross(last, poly[0])
		}
	}
	return winding%2 != 0
}

// lineCrossesRect is the qt_painterpath_isect_line_rect: whether the line
// from a to b crosses the border of r. A line wholly inside r does not.
func lineCrossesRect(a, b Point, r Rect) bool {
	const (
		left = 1 << iota
		right
		top
		bottom
	)
	x1, y1, x2, y2 := a.X, a.Y, b.X, b.Y
	l, t, rr, bt := r.Left(), r.Top(), r.Right(), r.Bottom()
	code := func(x, y float64) int {
		c := 0
		if x < l {
			c |= left
		}
		if x > rr {
			c |= right
		}
		if y < t {
			c |= top
		}
		if y > bt {
			c |= bottom
		}
		return c
	}
	p1, p2 := code(x1, y1), code(x2, y2)
	if p1&p2 != 0 {
		return false // both beyond the same side
	}
	if p1|p2 == 0 {
		return false // both inside
	}
	dx, dy := x2-x1, y2-y1
	if x1 < l {
		y1 += dy / dx * (l - x1)
		x1 = l
	} else if x1 > rr {
		y1 -= dy / dx * (x1 - rr)
		x1 = rr
	}
	if x2 < l {
		y2 += dy / dx * (l - x2)
		x2 = l
	} else if x2 > rr {
		y2 -= dy / dx * (x2 - rr)
		x2 = rr
	}
	ycode := func(y float64) int {
		c := 0
		if y < t {
			c |= top
		}
		if y > bt {
			c |= bottom
		}
		return c
	}
	if ycode(y1)&ycode(y2) != 0 {
		return false
	}
	if y1 < t {
		x1 += dx / dy * (t - y1)
		y1 = t
	} else if y1 > bt {
		x1 -= dx / dy * (y1 - bt)
		y1 = bt
	}
	if y2 < t {
		x2 += dx / dy * (t - y2)
		y2 = t
	} else if y2 > bt {
		x2 -= dx / dy * (y2 - bt)
		y2 = bt
	}
	xcode := func(x float64) int {
		c := 0
		if x < l {
			c |= left
		}
		if x > rr {
			c |= right
		}
		return c
	}
	return xcode(x1)&xcode(x2) == 0
}

// onRectEdge reports whether p lies on the border of r.
func onRectEdge(r Rect, p Point) bool {
	if (p.X == r.Left() || p.X == r.Right()) && p.Y >= r.Top() && p.Y <= r.Bottom() {
		return true
	}
	return (p.Y == r.Top() || p.Y == r.Bottom()) && p.X >= r.Left() && p.X <= r.Right()
}

// Intersects reports whether any part of the outline meets r, as
// QPainterPath::intersects decides it: a line or curve crosses r's border, a
// subpath runs from outside r to inside it, r's centre is inside the area the
// outline encloses (each subpath closed), or r holds a subpath's start. The
// closing line of an open outline counts, as it does in .
func (o Outline) Intersects(r Rect) bool {
	els := o.elements()
	if len(els) == 1 && r.Contains(els[0].end()) {
		return true
	}
	if o.Empty() {
		return false
	}
	cp := o.ControlBounds()
	rn := r.Normalized()
	if max(rn.Left(), cp.Left()) > min(rn.Right(), cp.Right()) ||
		max(rn.Top(), cp.Top()) > min(rn.Bottom(), cp.Bottom()) {
		return false
	}
	if o.crossesRect(rn) {
		return true
	}
	if o.Contains(rn.Center()) {
		return true
	}
	for _, e := range els {
		if e.kind == MoveTo && rn.Contains(e.pts[0]) {
			return true
		}
	}
	return false
}

// crossesRect is the qt_painterpath_check_crossing over the outline as
// polylines.
func (o Outline) crossesRect(r Rect) bool {
	const (
		onRect = iota
		insideRect
		outsideRect
	)
	status := onRect
	var lastPt, lastStart Point
	for _, poly := range o.Flatten(0.25) {
		lastPt, lastStart = poly[0], poly[0]
		for i, p := range poly {
			if i > 0 {
				if lineCrossesRect(lastPt, p, r) {
					return true
				}
				lastPt = p
			}
			if !onRectEdge(r, lastPt) {
				in := r.Contains(lastPt)
				switch status {
				case outsideRect:
					if in {
						return true
					}
				case insideRect:
					if !in {
						return true
					}
				default:
					if in {
						status = insideRect
					} else {
						status = outsideRect
					}
				}
			} else if lastPt.Equal(lastStart) {
				status = onRect
			}
		}
	}
	return !lastPt.Equal(lastStart) && lineCrossesRect(lastPt, lastStart, r)
}

// DistanceTo is the shortest distance from p to any line or curve of the
// outline, curves followed as fine polylines. Hit testing a path uses it.
func (o Outline) DistanceTo(p Point) float64 {
	best := math.Inf(1)
	for _, poly := range o.Flatten(0.5) {
		if len(poly) == 1 {
			best = min(best, p.Sub(poly[0]).Len())
		}
		for i := 1; i < len(poly); i++ {
			best = min(best, segmentDistance(p, poly[i-1], poly[i]))
		}
	}
	return best
}

// segmentDistance is the distance from p to the line from a to b.
func segmentDistance(p, a, b Point) float64 {
	d := b.Sub(a)
	l2 := d.X*d.X + d.Y*d.Y
	if l2 == 0 {
		return p.Sub(a).Len()
	}
	t := max(0, min(1, ((p.X-a.X)*d.X+(p.Y-a.Y)*d.Y)/l2))
	return p.Sub(a.Add(d.Mul(t))).Len()
}
