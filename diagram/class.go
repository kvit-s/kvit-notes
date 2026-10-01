package diagram

// The class-diagram layout: UML boxes with compartments placed by the
// layered core, relations drawn from border to border with UML end markers,
// cardinalities beside their ends, namespaces as frames, and notes below the
// diagram joined to their class by dashed lines.

import (
	"fmt"

	"github.com/kvit-s/kvit-notes/mermaid"
)

const (
	classPadX           = 12.0
	classMinW           = 84.0
	classRankGap        = 64.0
	classNodeGap        = 44.0
	classMargin         = 16.0
	classCompartmentPad = 5.0
)

// markerForEnd is the marker a relation end is drawn with.
func markerForEnd(end mermaid.ClassRelEnd) Marker {
	switch end {
	case mermaid.RelExtension:
		return MarkerTriangleOpen
	case mermaid.RelComposition:
		return MarkerDiamondFilled
	case mermaid.RelAggregation:
		return MarkerDiamondOpen
	case mermaid.RelDependency:
		return MarkerOpenArrow
	case mermaid.RelLollipop:
		return MarkerCircleOpen
	}
	return MarkerNone
}

// markerInset is how far a relation's line stops short of the tip of a
// hollow marker, so the line does not show through it. The reach is the
// marker's own, from MarkerLength.
func markerInset(end mermaid.ClassRelEnd) float64 {
	switch end {
	case mermaid.RelExtension, mermaid.RelAggregation, mermaid.RelLollipop:
		return MarkerLength(markerForEnd(end))
	}
	return 0
}

// guillemets is an annotation as it is shown, «interface».
func guillemets(annotation string) string { return "«" + annotation + "»" }

// LayoutClass lays out a class diagram in the direction it names.
func LayoutClass(ast *mermaid.ClassAst, opts LayoutOptions) Scene {
	opts = prepared(opts)
	scene := Scene{AccTitle: ast.AccTitle, AccDescr: ast.AccDescr}
	if scene.AccTitle == "" {
		scene.AccTitle = ast.Title
	}
	n := len(ast.Classes)
	scene.Summary = fmt.Sprintf("Mermaid class diagram with %d class%s and %d relationship%s",
		n, plural(n, "es"), len(ast.Relations), plural(len(ast.Relations), "s"))
	if n == 0 {
		return scene
	}
	fm := opts.Measure
	lineH := fm.Height()
	memberSize := max(10, opts.FontSize-1)

	idx := make(map[string]int, n)
	for i, c := range ast.Classes {
		idx[c.ID] = i
	}
	indexOf := func(id string) int {
		if i, ok := idx[id]; ok {
			return i
		}
		return -1
	}

	// ---- measure the boxes ----
	type boxMetrics struct {
		w      float64
		titleH float64 // the annotation and name band
		attrH  float64
		methH  float64
	}
	metrics := make([]boxMetrics, n)
	size := make([]Size, n)
	for i := range ast.Classes {
		c := &ast.Classes[i]
		var m boxMetrics
		w := fm.Advance(c.Label) + 2*classPadX
		m.titleH = lineH + 2*classCompartmentPad
		if c.Annotation != "" {
			w = max(w, fm.Advance(guillemets(c.Annotation))+2*classPadX)
			m.titleH += lineH
		}
		for _, t := range c.Attributes {
			w = max(w, fm.Advance(t)+2*classPadX)
		}
		for _, t := range c.Methods {
			w = max(w, fm.Advance(t)+2*classPadX)
		}
		m.attrH = float64(len(c.Attributes))*lineH + 2*classCompartmentPad
		m.methH = float64(len(c.Methods))*lineH + 2*classCompartmentPad
		m.w = max(classMinW, w)
		metrics[i] = m
		size[i] = Size{m.w, m.titleH + m.attrH + m.methH}
	}

	// ---- place with the layered core ----
	across := horizontal(ast.Direction)
	var ledges []LayeredEdge
	ledgeOfRel := make([]int, len(ast.Relations))
	for ri, r := range ast.Relations {
		ledgeOfRel[ri] = -1
		u, v := indexOf(r.From), indexOf(r.To)
		if u < 0 || v < 0 {
			continue
		}
		le := LayeredEdge{U: u, V: v, MinLen: 1}
		if r.Label != "" {
			if across {
				le.LabelMain = fm.Advance(r.Label) + 8
			} else {
				le.LabelMain = lineH
			}
		}
		ledgeOfRel[ri] = len(ledges)
		ledges = append(ledges, le)
	}
	layered := PlaceLayered(size, ledges, ast.Direction, classRankGap, classNodeGap)
	center := layered.Centers

	// ---- class boxes ----
	rects := make([]Rect, n)
	for i := range ast.Classes {
		c := &ast.Classes[i]
		m := metrics[i]
		r := rectAround(center[i], size[i])
		rects[i] = r
		st := resolveStyle(c.CSSClasses, ast.ClassDefs)

		box := newShape(KindRect, r)
		box.NodeID = c.ID
		box.Src = c.SrcSpan
		st.apply(&box)
		scene.Shapes = append(scene.Shapes, box)

		// The compartment lines, drawn under empty compartments too.
		for _, yLine := range []float64{r.Top() + m.titleH, r.Top() + m.titleH + m.attrH} {
			sep := newPath(straight(Point{r.Left(), yLine}, Point{r.Right(), yLine}))
			sep.StrokeRole = RoleNodeStroke
			sep.StrokeWidth = 1
			scene.Paths = append(scene.Paths, sep)
		}

		y := r.Top() + classCompartmentPad
		if c.Annotation != "" {
			ann := newText(guillemets(c.Annotation), Rect{r.Left(), y, r.W, lineH})
			ann.Italic = true
			ann.FontSize = memberSize
			scene.Texts = append(scene.Texts, ann)
			y += lineH
		}
		title := newText(c.Label, Rect{r.Left(), y, r.W, lineH})
		title.Bold = true
		title.FontSize = opts.FontSize
		scene.Texts = append(scene.Texts, title)

		member := func(t string, y float64) {
			tx := newText(t, Rect{r.Left() + classPadX, y, r.W - 2*classPadX, lineH})
			tx.FontSize = memberSize
			tx.Align = AlignLeft
			scene.Texts = append(scene.Texts, tx)
		}
		y = r.Top() + m.titleH + classCompartmentPad
		for _, t := range c.Attributes {
			member(t, y)
			y += lineH
		}
		y = r.Top() + m.titleH + m.attrH + classCompartmentPad
		for _, t := range c.Methods {
			member(t, y)
			y += lineH
		}
	}

	// ---- relations ----
	// The class boxes and the labels placed so far: what the next label
	// keeps clear of.
	taken := append([]Rect(nil), rects...)
	for ri := range ast.Relations {
		rel := &ast.Relations[ri]
		u, v := indexOf(rel.From), indexOf(rel.To)
		if u < 0 || v < 0 {
			continue
		}
		var way []Point
		if ledgeOfRel[ri] >= 0 {
			way = layered.EdgeBends[ledgeOfRel[ri]]
		}
		var a, b Point
		switch {
		case u == v:
			r := rects[u]
			a = Point{r.Right(), r.Center().Y - r.H*0.2}
			b = Point{r.Right(), r.Center().Y + r.H*0.2}
		case len(way) > 0:
			a = borderPoint(rects[u], way[0])
			b = borderPoint(rects[v], way[len(way)-1])
		default:
			a = borderPoint(rects[u], rects[v].Center())
			b = borderPoint(rects[v], rects[u].Center())
		}
		d := unit(b.Sub(a))

		p := newPath(Outline{})
		p.EdgeIndex = ri
		p.Src = rel.SrcSpan
		if rel.Dotted {
			p.Style = LineDashed
		}
		p.StrokeWidth = 1.4
		p.StartMarker = markerForEnd(rel.FromEnd)
		p.EndMarker = markerForEnd(rel.ToEnd)
		lineStart := a.Add(d.Mul(markerInset(rel.FromEnd)))
		lineEnd := b.Sub(d.Mul(markerInset(rel.ToEnd)))
		switch {
		case u == v:
			p.Outline = selfLoop(a, b, max(28, rects[u].W*0.35))
			p.StartDir, p.EndDir = Point{-1, 0}, Point{-1, 0}
		case len(way) > 0:
			// The relation crosses other ranks: it follows the lane kept
			// for it in each of them rather than cutting over their boxes.
			p.Outline = laneRoute(a, b, way)
			p.StartDir = unit(a.Sub(way[0]))
			p.EndDir = unit(b.Sub(way[len(way)-1]))
		default:
			p.Outline = straight(lineStart, lineEnd)
			p.StartDir, p.EndDir = d.Neg(), d
		}
		p.StartPoint, p.EndPoint = a, b
		scene.Paths = append(scene.Paths, p)

		// A cardinality belongs to one end of the relation, so it is placed
		// from that end, past the marker there and beside the line. The
		// path's end directions point out of it, so the way back along the
		// edge is their negation, for a straight relation, one routed
		// through lanes and a loop alike.
		cardSize := max(9, opts.FontSize-3)
		endLabel := func(text string, tip, dir Point, marker Marker) {
			t := newText(text, PlaceEndLabel(tip, dir, MarkerLength(marker), Size{fm.Advance(text) + 6, lineH}, taken))
			t.Role = RoleEdgeLabel
			t.FontSize = cardSize
			t.HasBackground = true
			taken = append(taken, t.Rect)
			scene.Texts = append(scene.Texts, t)
		}
		if rel.FromCard != "" {
			endLabel(rel.FromCard, a, p.StartDir.Neg(), p.StartMarker)
		}
		if rel.ToCard != "" {
			endLabel(rel.ToCard, b, p.EndDir.Neg(), p.EndMarker)
		}
		if rel.Label != "" {
			t := newText(rel.Label, PlaceEdgeLabel(p.Outline, Size{fm.Advance(rel.Label) + 8, lineH}, taken))
			t.Role = RoleEdgeLabel
			t.FontSize = max(10, opts.FontSize-1)
			t.HasBackground = true
			taken = append(taken, t.Rect)
			scene.Texts = append(scene.Texts, t)
		}
	}

	// ---- namespaces as frames ----
	for _, ns := range ast.Namespaces {
		var box Rect
		any := false
		for _, id := range ns.ClassIDs {
			i := indexOf(id)
			if i < 0 {
				continue
			}
			if any {
				box = box.United(rects[i])
			} else {
				box = rects[i]
			}
			any = true
		}
		if !any {
			continue
		}
		scene.Groups = append(scene.Groups, Group{Rect: box.Adjusted(-14, -14-lineH, 14, 14), Title: ns.Name})
	}

	// ---- notes, below the diagram, with a dashed line to their class ----
	if len(ast.Notes) > 0 {
		var bottom float64
		for _, r := range rects {
			bottom = max(bottom, r.Bottom())
		}
		var x float64
		noteY := bottom + 42
		for _, note := range ast.Notes {
			ls := LabelLines(note.Text)
			var tw float64
			for _, l := range ls {
				tw = max(tw, fm.Advance(l))
			}
			w := min(tw, 280) + 20
			h := float64(len(ls))*lineH + 12
			r := Rect{x, noteY, w, h}
			s := newShape(KindRect, r)
			s.FillRole, s.StrokeRole = RoleNoteFill, RoleNoteStroke
			s.StrokeWidth = 1
			scene.Shapes = append(scene.Shapes, s)
			t := newText(note.Text, r)
			t.FontSize = memberSize
			scene.Texts = append(scene.Texts, t)
			if target := indexOf(note.ForClass); target >= 0 {
				from := Point{r.Center().X, r.Top()}
				to := borderPoint(rects[target], from)
				link := newPath(straight(from, to))
				link.Style = LineDashed
				link.StrokeRole = RoleNoteStroke
				link.StrokeWidth = 1
				link.StartPoint, link.EndPoint = from, to
				scene.Paths = append(scene.Paths, link)
			}
			x += w + 24
		}
	}

	FinalizeSceneBounds(&scene, classMargin)
	return scene
}
