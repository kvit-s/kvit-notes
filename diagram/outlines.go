package diagram

// The geometry of a drawn diagram, without the painting: each shape kind's
// outline, the actor's stick figure, and the markers at the ends of paths, as
// outlines the editor strokes and fills and hit tests against. Layout
// measures markers here too (MarkerLength), so the room it leaves for one is
// the room it takes.

// ShapeOutline is the outline of a shape, filled with its fill colour and
// stroked with its stroke. An actor has none; it is drawn as ActorFigure.
func ShapeOutline(s Shape) Outline {
	var o Outline
	r := s.Rect
	c := r.Center()
	poly := func(pts ...Point) {
		o.AddPolygon(pts)
		o.Close()
	}
	switch s.Kind {
	case KindRect, KindSubroutine:
		o.AddRect(r)
	case KindRoundRect:
		o.AddRoundedRect(r, 8, 8)
	case KindStadium:
		o.AddRoundedRect(r, r.H/2, r.H/2)
	case KindCircle, KindDoubleCircle, KindEllipse:
		o.AddEllipse(r)
	case KindRhombus:
		poly(Point{c.X, r.Top()}, Point{r.Right(), c.Y}, Point{c.X, r.Bottom()}, Point{r.Left(), c.Y})
	case KindHexagon:
		inset := min(r.W*0.25, r.H)
		poly(Point{r.Left(), c.Y}, Point{r.Left() + inset, r.Top()}, Point{r.Right() - inset, r.Top()},
			Point{r.Right(), c.Y}, Point{r.Right() - inset, r.Bottom()}, Point{r.Left() + inset, r.Bottom()})
	case KindParallelogram:
		skew := r.H * 0.3
		poly(Point{r.Left() + skew, r.Top()}, Point{r.Right(), r.Top()},
			Point{r.Right() - skew, r.Bottom()}, Point{r.Left(), r.Bottom()})
	case KindParallelogramAlt:
		skew := r.H * 0.3
		poly(Point{r.Left(), r.Top()}, Point{r.Right() - skew, r.Top()},
			Point{r.Right(), r.Bottom()}, Point{r.Left() + skew, r.Bottom()})
	case KindTrapezoid:
		inset := r.H * 0.3
		poly(Point{r.Left() + inset, r.Top()}, Point{r.Right() - inset, r.Top()},
			Point{r.Right(), r.Bottom()}, Point{r.Left(), r.Bottom()})
	case KindTrapezoidAlt:
		inset := r.H * 0.3
		poly(Point{r.Left(), r.Top()}, Point{r.Right(), r.Top()},
			Point{r.Right() - inset, r.Bottom()}, Point{r.Left() + inset, r.Bottom()})
	case KindOdd:
		// The `>text]` flag: a rectangle whose left side notches in to a
		// point halfway down.
		inset := min(r.H*0.4, r.W*0.3)
		poly(Point{r.Left() + inset, r.Top()}, Point{r.Right(), r.Top()}, Point{r.Right(), r.Bottom()},
			Point{r.Left() + inset, r.Bottom()}, Point{r.Left(), c.Y})
	case KindCylinder:
		ry := min(r.H*0.14, 12)
		o.MoveTo(Point{r.Left(), r.Top() + ry})
		o.ArcTo(Rect{r.Left(), r.Top(), r.W, 2 * ry}, 180, -180)
		o.LineTo(Point{r.Right(), r.Bottom() - ry})
		o.ArcTo(Rect{r.Left(), r.Bottom() - 2*ry, r.W, 2 * ry}, 0, -180)
		o.Close()
	}
	return o
}

// ShapeDetail is what is drawn inside a shape after its outline, stroked
// with the shape's stroke and not filled: a subroutine's two inner bars and
// a double circle's inner ring. Other kinds have none.
func ShapeDetail(s Shape) Outline {
	var o Outline
	r := s.Rect
	switch s.Kind {
	case KindSubroutine:
		const inset = 6.0
		o.MoveTo(Point{r.Left() + inset, r.Top()})
		o.LineTo(Point{r.Left() + inset, r.Bottom()})
		o.MoveTo(Point{r.Right() - inset, r.Top()})
		o.LineTo(Point{r.Right() - inset, r.Bottom()})
	case KindDoubleCircle:
		o.AddEllipse(r.Adjusted(4, 4, -4, -4))
	}
	return o
}

// ActorStrokeWidth is the width the actor's figure is stroked at, with
// round caps and no fill.
const ActorStrokeWidth = 1.6

// ActorFigure is a sequence diagram actor's stick figure filling r: a head,
// a body, arms and two legs.
func ActorFigure(r Rect) Outline {
	var o Outline
	cx := r.Center().X
	headR := r.H * 0.18
	head := Point{cx, r.Top() + headR + 1}
	o.AddEllipse(Rect{head.X - headR, head.Y - headR, 2 * headR, 2 * headR})
	neckY := head.Y + headR
	hipY := r.Top() + r.H*0.68
	line := func(a, b Point) {
		o.MoveTo(a)
		o.LineTo(b)
	}
	line(Point{cx, neckY}, Point{cx, hipY}) // body
	armY := neckY + (hipY-neckY)*0.3
	line(Point{cx - r.W*0.32, armY}, Point{cx + r.W*0.32, armY}) // arms
	line(Point{cx, hipY}, Point{cx - r.W*0.28, r.Bottom()})      // legs
	line(Point{cx, hipY}, Point{cx + r.W*0.28, r.Bottom()})
	return o
}

// MarkerShape is a marker's outline and how it is painted, always in the
// colour of the path it ends.
type MarkerShape struct {
	Outline     Outline
	Filled      bool    // filled in the path's colour
	StrokeWidth float64 // stroked at this width; 0 for not stroked
	RoundCap    bool    // stroked with round caps
}

// MarkerOutline is the marker of kind with its tip at tip, pointing along
// dir (towards the node it touches, as a Path's EndDir does). It is empty
// for MarkerNone and for a direction too short to point anywhere.
func MarkerOutline(kind Marker, tip, dir Point) MarkerShape {
	var m MarkerShape
	l := dir.Len()
	if kind == MarkerNone || l < 0.001 {
		return m
	}
	d := dir.Div(l)
	perp := Point{-d.Y, d.X}
	o := &m.Outline
	line := func(a, b Point) {
		o.MoveTo(a)
		o.LineTo(b)
	}
	circle := func(c Point, radius float64) {
		o.AddEllipse(Rect{c.X - radius, c.Y - radius, 2 * radius, 2 * radius})
	}
	triangle := func(size, half float64) {
		base := tip.Sub(d.Mul(size))
		o.AddPolygon([]Point{tip, base.Add(perp.Mul(half)), base.Sub(perp.Mul(half))})
		o.Close()
	}
	switch kind {
	case MarkerArrow:
		triangle(9, 3.6)
		m.Filled = true
	case MarkerOpenArrow:
		base := tip.Sub(d.Mul(9))
		line(tip, base.Add(perp.Mul(4.2)))
		line(tip, base.Sub(perp.Mul(4.2)))
		m.StrokeWidth, m.RoundCap = 1.5, true
	case MarkerCross:
		// The `-x` head: an X just short of the end.
		c := tip.Sub(d.Mul(6))
		a1, a2 := d.Add(perp).Mul(4.2), d.Sub(perp).Mul(4.2)
		line(c.Add(a1), c.Sub(a1))
		line(c.Add(a2), c.Sub(a2))
		m.StrokeWidth, m.RoundCap = 1.8, true
	case MarkerDot:
		// The `-)` head: a small disc at the end.
		circle(tip.Sub(d.Mul(3.5)), 3.5)
		m.Filled = true
	case MarkerTriangleOpen:
		// UML extension: a hollow triangle the line stops at the base of.
		triangle(13, 6.5)
		m.StrokeWidth = 1.4
	case MarkerDiamondFilled, MarkerDiamondOpen:
		const size = 6.0
		mid := tip.Sub(d.Mul(size))
		o.AddPolygon([]Point{tip, mid.Add(perp.Mul(4)), tip.Sub(d.Mul(2 * size)), mid.Sub(perp.Mul(4))})
		o.Close()
		m.StrokeWidth = 1.4
		m.Filled = kind == MarkerDiamondFilled
	case MarkerCircleOpen:
		circle(tip.Sub(d.Mul(5)), 5)
		m.StrokeWidth = 1.4
	case MarkerErOne, MarkerErZeroOne, MarkerErMany, MarkerErZeroMany:
		// Crow's-foot notation, drawn back along the line from the tip on
		// the entity's border.
		bar := func(dist float64) {
			c := tip.Sub(d.Mul(dist))
			line(c.Add(perp.Mul(5)), c.Sub(perp.Mul(5)))
		}
		crow := func() {
			base := tip.Sub(d.Mul(11))
			line(base, tip.Add(perp.Mul(5)))
			line(base, tip.Sub(perp.Mul(5)))
			line(base, tip)
		}
		switch kind {
		case MarkerErOne:
			bar(6)
			bar(11)
		case MarkerErZeroOne:
			bar(6)
			circle(tip.Sub(d.Mul(15)), 4)
		case MarkerErMany:
			crow()
			bar(15)
		case MarkerErZeroMany:
			crow()
			circle(tip.Sub(d.Mul(16)), 4)
		}
		m.StrokeWidth, m.RoundCap = 1.4, true
	}
	return m
}

// MarkerLength is how far a marker reaches back along its line from the
// end it sits on. Layout stops a line inside a hollow marker by it, and
// keeps an end label such as a cardinality off the marker. Each number is
// the reach of the shape MarkerOutline draws.
func MarkerLength(kind Marker) float64 {
	switch kind {
	case MarkerArrow, MarkerOpenArrow:
		return 9
	case MarkerCross:
		return 10.2 // the centre at 6 and half the size, 4.2
	case MarkerDot:
		return 7
	case MarkerTriangleOpen:
		return 13
	case MarkerDiamondFilled, MarkerDiamondOpen:
		return 12 // twice the 6 of half the diagonal
	case MarkerCircleOpen:
		return 10
	case MarkerErOne:
		return 11
	case MarkerErZeroOne:
		return 19 // a circle of radius 4 centred at 15
	case MarkerErMany:
		return 15
	case MarkerErZeroMany:
		return 20 // a circle of radius 4 centred at 16
	}
	return 0
}

// GroupOutline is a group's frame: a rectangle with corners of radius 6,
// filled with its colour and, unless NoBorder, stroked with a one-pixel
// dashed line.
func GroupOutline(g Group) Outline {
	var o Outline
	o.AddRoundedRect(g.Rect, 6, 6)
	return o
}

// GroupTitleRect is where a group's title is drawn, at the left and centred
// from top to bottom; the editor draws the title in bold.
func GroupTitleRect(g Group) Rect {
	return Rect{g.Rect.Left() + 8, g.Rect.Top() + 2, g.Rect.W - 16, 16}
}

// LabelBackdrop is the backdrop under a Text with HasBackground: its
// rectangle with corners of radius 3.
func LabelBackdrop(t Text) Outline {
	var o Outline
	o.AddRoundedRect(t.Rect, 3, 3)
	return o
}

// MathOrigin is where a typeset formula of the given size goes in its
// Text's rectangle: centred in it, as layout sized the rectangle from the
// same formula.
func MathOrigin(t Text, size Size) Point {
	c := t.Rect.Center()
	return Point{c.X - size.W/2, c.Y - size.H/2}
}
