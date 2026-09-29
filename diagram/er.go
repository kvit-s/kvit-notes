package diagram

// The entity-relationship layout, a port of the app's
// src/content/diagrams/erlayout.cpp: entity tables (a title band, then
// columns for type, name, keys and comment) placed by the layered core, and
// relationships drawn from border to border with crow's-foot markers, dashed
// when not identifying, and labelled at the middle.

import (
	"fmt"
	"strings"

	"github.com/kvit-s/kvit-notes/mermaid"
)

const (
	erRankGap  = 70.0
	erNodeGap  = 48.0
	erMargin   = 16.0
	erCellPadX = 8.0
)

// markerForCard is the crow's-foot marker for one end of a relationship.
func markerForCard(c mermaid.ErCardinality) Marker {
	switch c {
	case mermaid.ZeroOrOne:
		return MarkerErZeroOne
	case mermaid.ZeroOrMore:
		return MarkerErZeroMany
	case mermaid.OneOrMore:
		return MarkerErMany
	}
	return MarkerErOne // only one, and an MD parent
}

// LayoutEr lays out an entity-relationship diagram in the direction it
// names.
func LayoutEr(ast *mermaid.ErAst, opts LayoutOptions) Scene {
	opts = prepared(opts)
	scene := Scene{AccTitle: ast.AccTitle, AccDescr: ast.AccDescr}
	if scene.AccTitle == "" {
		scene.AccTitle = ast.Title
	}
	n := len(ast.Entities)
	entities := "ies"
	if n == 1 {
		entities = "y"
	}
	scene.Summary = fmt.Sprintf("Mermaid ER diagram with %d entit%s and %d relationship%s",
		n, entities, len(ast.Relationships), plural(len(ast.Relationships), "s"))
	if n == 0 {
		return scene
	}
	fm := opts.Measure
	lineH := fm.Height()
	rowH := lineH + 6
	titleH := lineH + 12
	cellSize := max(10, opts.FontSize-1)

	idx := make(map[string]int, n)
	for i, e := range ast.Entities {
		idx[e.ID] = i
	}
	indexOf := func(id string) int {
		if i, ok := idx[id]; ok {
			return i
		}
		return -1
	}

	// ---- measure the tables ----
	type columns struct {
		typ, name, keys, comment float64
		hasKeys, hasComment      bool
	}
	cols := make([]columns, n)
	size := make([]Size, n)
	for i := range ast.Entities {
		e := &ast.Entities[i]
		var c columns
		for _, a := range e.Attributes {
			c.typ = max(c.typ, fm.Advance(a.Type))
			c.name = max(c.name, fm.Advance(a.Name))
			if len(a.Keys) > 0 {
				c.hasKeys = true
				c.keys = max(c.keys, fm.Advance(strings.Join(a.Keys, ",")))
			}
			if a.Comment != "" {
				c.hasComment = true
				c.comment = max(c.comment, fm.Advance(a.Comment))
			}
		}
		w := c.typ + c.name + 4*erCellPadX
		if c.hasKeys {
			w += c.keys + 2*erCellPadX
		}
		if c.hasComment {
			w += c.comment + 2*erCellPadX
		}
		w = max(w, fm.Advance(e.Label)+2*erCellPadX, 90)
		cols[i] = c
		size[i] = Size{w, titleH + float64(len(e.Attributes))*rowH}
	}

	// ---- place with the layered core ----
	across := horizontal(ast.Direction)
	var ledges []LayeredEdge
	ledgeOfRel := make([]int, len(ast.Relationships))
	for ri, r := range ast.Relationships {
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
	layered := PlaceLayered(size, ledges, ast.Direction, erRankGap, erNodeGap)
	center := layered.Centers

	// ---- entity tables ----
	rects := make([]Rect, n)
	for i := range ast.Entities {
		e := &ast.Entities[i]
		c := cols[i]
		r := rectAround(center[i], size[i])
		rects[i] = r

		var fill, stroke mermaid.Color
		for _, cls := range e.CSSClasses {
			d := ast.ClassDefs[cls]
			if d.HasFill {
				fill = d.Fill
			}
			if d.HasStroke {
				stroke = d.Stroke
			}
		}
		box := newShape(KindRect, r)
		box.NodeID = e.ID
		box.Src = e.SrcSpan
		if fill.Set {
			box.FillOverride = fill
		}
		if stroke.Set {
			box.StrokeOverride = stroke
		}
		scene.Shapes = append(scene.Shapes, box)

		title := newText(e.Label, Rect{r.Left(), r.Top(), r.W, titleH})
		title.Bold = true
		title.FontSize = opts.FontSize
		scene.Texts = append(scene.Texts, title)

		if len(e.Attributes) == 0 {
			continue
		}

		// The line under the title, one between rows, and one between
		// columns.
		rule := func(a, b Point) {
			sep := newPath(straight(a, b))
			sep.StrokeRole = RoleNodeStroke
			sep.StrokeWidth = 0.8
			scene.Paths = append(scene.Paths, sep)
		}
		hline := func(y float64) { rule(Point{r.Left(), y}, Point{r.Right(), y}) }
		vline := func(x float64) { rule(Point{x, r.Top() + titleH}, Point{x, r.Bottom()}) }
		hline(r.Top() + titleH)
		for row := 1; row < len(e.Attributes); row++ {
			hline(r.Top() + titleH + float64(row)*rowH)
		}
		colX := r.Left() + c.typ + 2*erCellPadX
		colStarts := []float64{r.Left(), colX}
		vline(colX)
		if c.hasKeys {
			colX += c.name + 2*erCellPadX
			colStarts = append(colStarts, colX)
			vline(colX)
		}
		if c.hasComment {
			if c.hasKeys {
				colX += c.keys + 2*erCellPadX
			} else {
				colX += c.name + 2*erCellPadX
			}
			colStarts = append(colStarts, colX)
			vline(colX)
		}

		for row, a := range e.Attributes {
			y := r.Top() + titleH + float64(row)*rowH
			cell := func(x, w float64, text string, italic bool) {
				if text == "" {
					return
				}
				t := newText(text, Rect{x + erCellPadX, y, w, rowH})
				t.FontSize = cellSize
				t.Italic = italic
				t.Align = AlignLeft
				scene.Texts = append(scene.Texts, t)
			}
			col := 0
			cell(colStarts[col], c.typ, a.Type, false)
			col++
			cell(colStarts[col], c.name, a.Name, false)
			col++
			if c.hasKeys {
				cell(colStarts[col], c.keys, strings.Join(a.Keys, ","), false)
				col++
			}
			if c.hasComment && col < len(colStarts) {
				cell(colStarts[col], c.comment, a.Comment, true)
			}
		}
	}

	// ---- relationships ----
	// The tables and the labels placed so far: what the next label keeps
	// clear of.
	taken := append([]Rect(nil), rects...)
	for ri := range ast.Relationships {
		rel := &ast.Relationships[ri]
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
		length := b.Sub(a).Len()
		d := unit(b.Sub(a))

		p := newPath(Outline{})
		p.EdgeIndex = ri
		p.Src = rel.SrcSpan
		if !rel.Identifying {
			p.Style = LineDashed
		}
		p.StrokeWidth = 1.4
		p.StartMarker = markerForCard(rel.FromCard)
		p.EndMarker = markerForCard(rel.ToCard)
		switch {
		case u == v:
			p.Outline = selfLoop(a, b, max(28, rects[u].W*0.35))
			p.StartDir, p.EndDir = Point{-1, 0}, Point{-1, 0}
		case len(way) > 0:
			// The relationship crosses other ranks: it follows the lane
			// kept for it in each of them rather than cutting over their
			// tables.
			p.Outline = laneRoute(a, b, way)
			p.StartDir = unit(a.Sub(way[0]))
			p.EndDir = unit(b.Sub(way[len(way)-1]))
		case length > 40:
			// Crow's feet are open shapes: the line stops short of them.
			p.Outline = straight(a.Add(d.Mul(16)), b.Sub(d.Mul(16)))
			p.StartDir, p.EndDir = d.Neg(), d
		default:
			p.Outline = straight(a, b)
			p.StartDir, p.EndDir = d.Neg(), d
		}
		p.StartPoint, p.EndPoint = a, b
		scene.Paths = append(scene.Paths, p)

		if rel.Label != "" {
			t := newText(rel.Label, PlaceEdgeLabel(p.Outline, Size{fm.Advance(rel.Label) + 8, lineH}, taken))
			t.Role = RoleEdgeLabel
			t.FontSize = max(10, opts.FontSize-1)
			t.HasBackground = true
			taken = append(taken, t.Rect)
			scene.Texts = append(scene.Texts, t)
		}
	}

	FinalizeSceneBounds(&scene, erMargin)
	return scene
}
