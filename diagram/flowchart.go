package diagram

// The flowchart layout. It measures each node's label, places the nodes with
// the layered core (or where a `%% mermaid-flow:pos` line pins them), routes
// the edges, and wraps each subgraph's members in a frame.

import (
	"fmt"
	"strings"

	"github.com/kvit-s/kvit-notes/mermaid"
)

const (
	flowPadX        = 14.0
	flowPadY        = 9.0
	flowMinW        = 46.0
	flowMinH        = 30.0
	flowRankGap     = 58.0 // between ranks along the flow
	flowNodeGap     = 30.0 // between neighbours across the flow
	flowMargin      = 16.0
	flowSubgraphPad = 16.0
)

// sceneShape is the scene's outline for a node's shape.
func sceneShape(s mermaid.NodeShape) ShapeKind {
	switch s {
	case mermaid.ShapeRoundRect:
		return KindRoundRect
	case mermaid.ShapeStadium:
		return KindStadium
	case mermaid.ShapeSubroutine:
		return KindSubroutine
	case mermaid.ShapeCylinder:
		return KindCylinder
	case mermaid.ShapeCircle:
		return KindCircle
	case mermaid.ShapeDoubleCircle:
		return KindDoubleCircle
	case mermaid.ShapeEllipse:
		return KindEllipse
	case mermaid.ShapeRhombus:
		return KindRhombus
	case mermaid.ShapeHexagon:
		return KindHexagon
	case mermaid.ShapeParallelogram:
		return KindParallelogram
	case mermaid.ShapeParallelogramAlt:
		return KindParallelogramAlt
	case mermaid.ShapeTrapezoid:
		return KindTrapezoid
	case mermaid.ShapeTrapezoidAlt:
		return KindTrapezoidAlt
	case mermaid.ShapeOdd:
		return KindOdd
	}
	return KindRect
}

// resolvedStyle is what the classDef, class and style statements applied to
// an element add up to. A dashed stroke is not among it: a Shape has no
// dashes, so a node's border is never drawn dashed.
type resolvedStyle struct {
	fill, stroke mermaid.Color
	strokeWidth  float64
	bold         bool
}

// resolveStyle merges the classDefs named in classes in order, the last
// setting of each property winning.
func resolveStyle(classes []string, defs map[string]mermaid.ClassDef) resolvedStyle {
	var r resolvedStyle
	for _, cls := range classes {
		d := defs[cls]
		if d.HasFill {
			r.fill = d.Fill
		}
		if d.HasStroke {
			r.stroke = d.Stroke
		}
		if d.StrokeWidth > 0 {
			r.strokeWidth = d.StrokeWidth
		}
		if d.Bold {
			r.bold = true
		}
	}
	return r
}

// apply sets a shape's stroke width and colours from the style.
func (st resolvedStyle) apply(sh *Shape) {
	sh.StrokeWidth = 1.5
	if st.strokeWidth > 0 {
		sh.StrokeWidth = st.strokeWidth
	}
	if st.fill.Set {
		sh.FillOverride = st.fill
	}
	if st.stroke.Set {
		sh.StrokeOverride = st.stroke
	}
}

// plural is "" for one and s otherwise.
func plural(n int, s string) string {
	if n == 1 {
		return ""
	}
	return s
}

// LayoutFlowchart lays out a flowchart in opts.Direction; Render passes the
// direction the flowchart's header names.
func LayoutFlowchart(ast *mermaid.FlowchartAst, opts LayoutOptions) Scene {
	opts = prepared(opts)
	scene := Scene{AccTitle: ast.AccTitle, AccDescr: ast.AccDescr}
	n := len(ast.Nodes)
	scene.Summary = fmt.Sprintf("Mermaid flowchart with %d node%s and %d connection%s",
		n, plural(n, "s"), len(ast.Edges), plural(len(ast.Edges), "s"))
	if n == 0 {
		return scene
	}
	fm := opts.Measure
	lineH := fm.Height()

	idx := make(map[string]int, n)
	for i, node := range ast.Nodes {
		idx[node.ID] = i
	}
	indexOf := func(id string) int {
		if i, ok := idx[id]; ok {
			return i
		}
		return -1
	}

	// ---- measure node boxes ----
	size := make([]Size, n)
	lines := make([][]string, n)
	// Per node, the TeX of a label that is one whole expression, and ""
	// for every other label. It goes to the Text, so the editor typesets
	// exactly what was measured here.
	nodeTex := make([]string, n)
	for i, node := range ast.Nodes {
		ls := LabelLines(node.Label)
		tex := MathLabel(node.Label)
		mathSize, isMath := opts.mathSize(tex)
		if isMath {
			nodeTex[i] = tex
		}
		var tw float64
		for _, l := range ls {
			tw = max(tw, fm.Advance(l))
		}
		th := float64(max(1, len(ls))) * lineH
		if isMath {
			tw, th = mathSize.W, mathSize.H
		}
		w := max(flowMinW, tw+2*flowPadX)
		h := max(flowMinH, th+2*flowPadY)
		switch node.Shape {
		case mermaid.ShapeCircle:
			d := max(w, h) * 1.15
			w, h = d, d
		case mermaid.ShapeDoubleCircle:
			d := max(w, h) * 1.3
			w, h = d, d
		case mermaid.ShapeEllipse:
			w *= 1.35
			h *= 1.25
		case mermaid.ShapeRhombus:
			w *= 1.4
			h *= 1.55
		case mermaid.ShapeHexagon:
			w += h
		case mermaid.ShapeCylinder:
			h += 14
		case mermaid.ShapeSubroutine:
			w += 16
		case mermaid.ShapeParallelogram, mermaid.ShapeParallelogramAlt,
			mermaid.ShapeTrapezoid, mermaid.ShapeTrapezoidAlt:
			w += h * 0.6
		case mermaid.ShapeOdd:
			w += h * 0.4
		}
		size[i] = Size{w, h}
		lines[i] = ls
	}

	// ---- coordinates: pinned, or the layered core ----
	across := horizontal(opts.Direction)
	var center []Point
	arranged := false
	if ast.HasPosLine {
		pinned := map[string]Point{}
		for _, pe := range ast.PosEntries {
			if _, ok := idx[pe.ID]; ok {
				pinned[pe.ID] = Point{pe.X, pe.Y}
			}
		}
		if len(pinned) > 0 {
			arranged = true
			center = make([]Point, n)
			var maxMain float64
			for i, node := range ast.Nodes {
				p, ok := pinned[node.ID]
				if !ok {
					continue
				}
				center[i] = p
				if across {
					maxMain = max(maxMain, p.X+size[i].W/2)
				} else {
					maxMain = max(maxMain, p.Y+size[i].H/2)
				}
			}
			// Nodes without an entry, typed in after the arrangement was
			// saved, go beyond the pinned content along the flow, in source
			// order, without moving any pinned node.
			var crossCursor float64
			for i, node := range ast.Nodes {
				if _, ok := pinned[node.ID]; ok {
					continue
				}
				if across {
					center[i] = Point{maxMain + flowRankGap + size[i].W/2, crossCursor + size[i].H/2}
					crossCursor += size[i].H + flowNodeGap
				} else {
					center[i] = Point{crossCursor + size[i].W/2, maxMain + flowRankGap + size[i].H/2}
					crossCursor += size[i].W + flowNodeGap
				}
			}
		}
	}
	// Per edge, the points its route passes through between ranks. None for
	// an edge between neighbouring ranks, and none in arranged mode, where
	// the reader placed the nodes and there are no ranks.
	bends := make([][]Point, len(ast.Edges))
	if !arranged {
		var ledges []LayeredEdge
		ledgeOfEdge := make([]int, len(ast.Edges))
		for ei, e := range ast.Edges {
			ledgeOfEdge[ei] = -1
			u, v := indexOf(e.From), indexOf(e.To)
			if u < 0 || v < 0 {
				continue
			}
			le := LayeredEdge{U: u, V: v, MinLen: max(1, e.MinLen)}
			if e.Label != "" {
				ms, isMath := opts.mathSize(MathLabel(e.Label))
				switch {
				case across && isMath:
					le.LabelMain = ms.W
				case across:
					le.LabelMain = fm.Advance(e.Label) + 8
				case isMath:
					le.LabelMain = ms.H
				default:
					le.LabelMain = lineH
				}
			}
			ledgeOfEdge[ei] = len(ledges)
			ledges = append(ledges, le)
		}
		ll := PlaceLayered(size, ledges, opts.Direction, flowRankGap, flowNodeGap)
		center = ll.Centers
		for ei := range ast.Edges {
			if ledgeOfEdge[ei] >= 0 {
				bends[ei] = ll.EdgeBends[ledgeOfEdge[ei]]
			}
		}
	}

	// ---- node shapes and labels ----
	rects := make([]Rect, n)
	for i := range ast.Nodes {
		node := &ast.Nodes[i]
		r := rectAround(center[i], size[i])
		rects[i] = r
		st := resolveStyle(node.Classes, ast.ClassDefs)
		sh := newShape(sceneShape(node.Shape), r)
		sh.NodeID = node.ID
		if node.IDSpan.Valid() {
			sh.Src = node.IDSpan
			// A bracket construct right after the id belongs to the span.
			if node.ShapeSpan.Valid() && node.ShapeSpan.Start >= node.IDSpan.End() &&
				node.ShapeSpan.Start <= node.IDSpan.End()+4 {
				sh.Src.Length = node.ShapeSpan.End() - node.IDSpan.Start
			}
		}
		st.apply(&sh)
		scene.Shapes = append(scene.Shapes, sh)

		tx := newText(strings.Join(lines[i], "\n"), r)
		tx.Tex = nodeTex[i]
		tx.FontSize = opts.FontSize
		tx.Bold = st.bold
		scene.Texts = append(scene.Texts, tx)
	}

	// ---- edges ----
	// What an edge label keeps clear of: every node box, and every label
	// placed before it.
	taken := append([]Rect(nil), rects...)
	// Arranged mode bows parallel edges apart, so it counts the edges
	// between each pair of nodes.
	pairCount := map[[2]int]int{}
	pairSeen := map[[2]int]int{}
	if arranged {
		for _, e := range ast.Edges {
			u, v := indexOf(e.From), indexOf(e.To)
			if u < 0 || v < 0 || u == v || e.Invisible {
				continue
			}
			pairCount[[2]int{min(u, v), max(u, v)}]++
		}
	}
	for ei := range ast.Edges {
		e := &ast.Edges[ei]
		u, v := indexOf(e.From), indexOf(e.To)
		if u < 0 || v < 0 || e.Invisible {
			continue // a `~~~` link ranks like an edge and draws nothing
		}
		p := newPath(Outline{})
		p.EdgeIndex = ei
		if e.OpSpan.Valid() {
			p.Src = e.OpSpan
		}
		if e.Stroke == mermaid.StrokeThick {
			p.StrokeWidth = 3
		}
		if e.Stroke == mermaid.StrokeDotted {
			p.Style = LineDotted
		}
		if e.ArrowStart {
			p.StartMarker = MarkerArrow
		}
		if e.ArrowEnd {
			p.EndMarker = MarkerArrow
		}

		switch {
		case u == v:
			// A loop off the right side of the node.
			r := rects[u]
			a := Point{r.Right(), r.Center().Y - r.H*0.2}
			b := Point{r.Right(), r.Center().Y + r.H*0.2}
			p.Outline = selfLoop(a, b, max(24, r.W*0.4))
			p.StartPoint, p.EndPoint = a, b
			p.StartDir, p.EndDir = Point{-1, 0}, Point{-1, 0}
		case arranged:
			// A cubic curve leaves and enters square to the node sides
			// along the flow; parallel edges bow apart.
			key := [2]int{min(u, v), max(u, v)}
			count := pairCount[key]
			if count == 0 {
				count = 1
			}
			k := pairSeen[key]
			pairSeen[key]++
			bow := (float64(k) - float64(count-1)/2) * 16
			var flow, perp, a, b Point
			if across {
				forward := center[v].X >= center[u].X
				flow, perp = Point{-1, 0}, Point{0, 1}
				a = Point{rects[u].Left(), rects[u].Center().Y + bow*0.4}
				b = Point{rects[v].Right(), rects[v].Center().Y + bow*0.4}
				if forward {
					flow = Point{1, 0}
					a.X, b.X = rects[u].Right(), rects[v].Left()
				}
			} else {
				forward := center[v].Y >= center[u].Y
				flow, perp = Point{0, -1}, Point{1, 0}
				a = Point{rects[u].Center().X + bow*0.4, rects[u].Top()}
				b = Point{rects[v].Center().X + bow*0.4, rects[v].Bottom()}
				if forward {
					flow = Point{0, 1}
					a.Y, b.Y = rects[u].Bottom(), rects[v].Top()
				}
			}
			dist := max(24, b.Sub(a).Len())
			c1 := a.Add(flow.Mul(dist * 0.4)).Add(perp.Mul(bow))
			c2 := b.Sub(flow.Mul(dist * 0.4)).Add(perp.Mul(bow))
			p.Outline.MoveTo(a)
			p.Outline.CubicTo(c1, c2, b)
			p.StartPoint, p.EndPoint = a, b
			p.EndDir, p.StartDir = unit(b.Sub(c2)), unit(a.Sub(c1))
		case len(bends[ei]) > 0:
			// An edge crossing other ranks follows its own lane through each
			// of them, so it passes between their boxes rather than over.
			way := bends[ei]
			a := borderPoint(rects[u], way[0])
			b := borderPoint(rects[v], way[len(way)-1])
			p.Outline = laneRoute(a, b, way)
			p.StartPoint, p.EndPoint = a, b
			p.StartDir = unit(a.Sub(way[0]))
			p.EndDir = unit(b.Sub(way[len(way)-1]))
		default:
			a := borderPoint(rects[u], center[v])
			b := borderPoint(rects[v], center[u])
			p.Outline = straight(a, b)
			p.StartPoint, p.EndPoint = a, b
			dir := unit(b.Sub(a))
			p.EndDir, p.StartDir = dir, dir.Neg()
		}
		scene.Paths = append(scene.Paths, p)

		if e.Label != "" {
			tex := MathLabel(e.Label)
			lw, lh := fm.Advance(e.Label)+8, lineH
			ms, isMath := opts.mathSize(tex)
			if isMath {
				lw, lh = ms.W+8, ms.H
			} else {
				tex = ""
			}
			tx := newText(e.Label, PlaceEdgeLabel(p.Outline, Size{lw, lh}, taken))
			taken = append(taken, tx.Rect)
			tx.Tex = tex
			tx.Role = RoleEdgeLabel
			tx.FontSize = max(10, opts.FontSize-1)
			tx.HasBackground = true
			scene.Texts = append(scene.Texts, tx)
		}
	}

	// ---- subgraph frames ----
	for _, sg := range ast.Subgraphs {
		var box Rect
		any := false
		for _, id := range sg.NodeIDs {
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
		box = box.Adjusted(-flowSubgraphPad, -flowSubgraphPad-lineH, flowSubgraphPad, flowSubgraphPad)
		scene.Groups = append(scene.Groups, Group{Rect: box, Title: sg.Title})
	}

	FinalizeSceneBounds(&scene, flowMargin)
	return scene
}
