package diagram

// The layered core every family but the sequence diagram is placed with, and
// the helpers the family layouts share. The flowchart's own layout is in
// flowchart.go.
//
// Layout is deterministic: node index and edge order break every tie, so an
// edit never reorders unrelated nodes, and it does not depend on the width
// the diagram is shown at, because the editor scales the finished scene.

import (
	"math"
	"slices"
	"unicode/utf8"

	"github.com/kvit-s/kvit-notes/mermaid"
)

// LayeredEdge is an edge given to the layered core.
type LayeredEdge struct {
	U, V   int // indexes into the sizes
	MinLen int // ranks the edge spans at least; below 1 counts as 1
	// LabelMain is the room along the flow axis the edge's label needs. The
	// gap between two ranks grows to fit the widest label crossing it, so a
	// label is never squeezed between two boxes closer together than it is
	// long. Zero for an edge without a label.
	LabelMain float64
}

// LayeredLayout is one placement by the layered core.
type LayeredLayout struct {
	// Centers holds one centre per node, in input order.
	Centers []Point
	// EdgeBends holds, per input edge in input order, the points an edge
	// spanning more than one rank passes through: one per rank between its
	// ends, from U towards V, and none for an edge between neighbouring
	// ranks. Each is a placeholder node of no thickness that takes part in
	// ordering and takes its own slot in its rank, so an edge routed through
	// them passes between the boxes of that rank rather than over them, and
	// its label has somewhere clear to sit.
	EdgeBends [][]Point
}

// laneWidth is the room across the flow a placeholder takes in a rank an
// edge only passes through: enough for the line and its stroke to clear the
// boxes on either side without opening a gap that reads as an empty column.
const laneWidth = 10.0

// labelGapPad is the slack around a label that sets how far apart two ranks
// must be for it to fit between them.
const labelGapPad = 16.0

// horizontal reports whether ranks run across the page.
func horizontal(d mermaid.Direction) bool { return d == mermaid.LR || d == mermaid.RL }

// PlaceLayered places boxes of the given sizes in ranks along direction:
// it breaks cycles by dropping the edges a depth-first search finds going
// back, ranks by longest path, reserves a placeholder in every rank an edge
// passes over, orders each rank by the barycentre of its neighbours in four
// sweeps, and centres each rank against the widest. rankGap is the room
// between ranks and nodeGap between neighbours in a rank.
func PlaceLayered(sizesIn []Size, edges []LayeredEdge, direction mermaid.Direction, rankGap, nodeGap float64) LayeredLayout {
	n := len(sizesIn)
	out := LayeredLayout{
		Centers:   make([]Point, n),
		EdgeBends: make([][]Point, len(edges)),
	}
	if n == 0 {
		return out
	}
	size := slices.Clone(sizesIn)
	usable := func(e LayeredEdge) bool {
		return e.U >= 0 && e.V >= 0 && e.U < n && e.V < n && e.U != e.V
	}

	// ---- adjacency, cycle breaking, ranking ----
	type arc struct{ to, minLen int }
	adj := make([][]arc, n)
	for _, e := range edges {
		if usable(e) {
			adj[e.U] = append(adj[e.U], arc{e.V, max(1, e.MinLen)})
		}
	}

	// A depth-first search calls an edge to a node still on the stack a back
	// edge, which closes a cycle; those are left out of ranking so that the
	// longest-path ranking ends.
	color := make([]int, n) // 0 unvisited, 1 on the stack, 2 done
	backEdges := map[[2]int]bool{}
	iterPos := make([]int, n)
	var stack []int
	for s := range n {
		if color[s] != 0 {
			continue
		}
		stack = append(stack, s)
		color[s] = 1
		for len(stack) > 0 {
			u := stack[len(stack)-1]
			if iterPos[u] < len(adj[u]) {
				v := adj[u][iterPos[u]].to
				iterPos[u]++
				if color[v] == 1 {
					backEdges[[2]int{u, v}] = true
				} else if color[v] == 0 {
					color[v] = 1
					stack = append(stack, v)
				}
			} else {
				color[u] = 2
				stack = stack[:len(stack)-1]
			}
		}
	}

	// Longest-path ranking over what is left, in Kahn's order.
	rank := make([]int, n)
	indeg := make([]int, n)
	fadj := make([][]arc, n)
	for u := range n {
		for _, a := range adj[u] {
			if !backEdges[[2]int{u, a.to}] {
				fadj[u] = append(fadj[u], a)
				indeg[a.to]++
			}
		}
	}
	var queue []int
	for i := range n {
		if indeg[i] == 0 {
			queue = append(queue, i)
		}
	}
	for qh := 0; qh < len(queue); qh++ {
		u := queue[qh]
		for _, a := range fadj[u] {
			rank[a.to] = max(rank[a.to], rank[u]+a.minLen)
			indeg[a.to]--
			if indeg[a.to] == 0 {
				queue = append(queue, a.to)
			}
		}
	}
	maxRank := slices.Max(rank)

	// ---- placeholders for edges that span more than one rank ----
	// Such an edge crosses every rank between its ends. A slot of its own in
	// each of them keeps it, and its label, out of the boxes standing there:
	// the placeholders order alongside real nodes and take room across the
	// flow, so the ranks open up to let the edge past. They also let a long
	// edge take part in crossing reduction, which it cannot do while it is
	// invisible to the ranks it passes over.
	edgeSlots := make([][]int, len(edges))
	for ei, e := range edges {
		if !usable(e) {
			continue
		}
		ru, rv := rank[e.U], rank[e.V]
		if abs(ru-rv) < 2 {
			continue
		}
		step := 1
		if ru > rv {
			step = -1
		}
		for r := ru + step; r != rv; r += step {
			edgeSlots[ei] = append(edgeSlots[ei], len(size))
			size = append(size, Size{laneWidth, laneWidth})
			rank = append(rank, r)
		}
	}
	m := len(size)
	center := make([]Point, m)

	// ---- order within ranks (barycentre crossing reduction) ----
	layers := make([][]int, maxRank+1)
	for i := range m {
		layers[rank[i]] = append(layers[rank[i]], i)
	}
	undirected := make([][]int, m)
	link := func(a, b int) {
		undirected[a] = append(undirected[a], b)
		undirected[b] = append(undirected[b], a)
	}
	for ei, e := range edges {
		if !usable(e) {
			continue
		}
		// A split edge is adjacent through its placeholders rather than end
		// to end, so each rank it crosses sees it.
		lane := edgeSlots[ei]
		if len(lane) == 0 {
			link(e.U, e.V)
			continue
		}
		link(e.U, lane[0])
		for k := 0; k+1 < len(lane); k++ {
			link(lane[k], lane[k+1])
		}
		link(lane[len(lane)-1], e.V)
	}
	posInLayer := make([]int, m)
	refreshPos := func() {
		for _, layer := range layers {
			for k, node := range layer {
				posInLayer[node] = k
			}
		}
	}
	refreshPos()
	barycenter := func(node int) float64 {
		nb := undirected[node]
		if len(nb) == 0 {
			return float64(posInLayer[node])
		}
		var sum float64
		for _, other := range nb {
			sum += float64(posInLayer[other])
		}
		return sum / float64(len(nb))
	}
	for range 4 {
		for l := 0; l <= maxRank; l++ {
			slices.SortStableFunc(layers[l], func(a, b int) int {
				ba, bb := barycenter(a), barycenter(b)
				if !fuzzyCompare(ba, bb) {
					if ba < bb {
						return -1
					}
					return 1
				}
				return a - b // node order breaks a tie
			})
		}
		refreshPos()
	}

	// ---- coordinate assignment ----
	across := horizontal(direction)
	mainSize := func(i int) float64 {
		if across {
			return size[i].W
		}
		return size[i].H
	}
	crossSize := func(i int) float64 {
		if across {
			return size[i].H
		}
		return size[i].W
	}

	// Where each rank sits along the flow. Each gap opens wide enough for
	// the labels of the edges crossing it.
	gapAfter := make([]float64, maxRank+1)
	for l := range gapAfter {
		gapAfter[l] = rankGap
	}
	for _, e := range edges {
		if !usable(e) || e.LabelMain <= 0 {
			continue
		}
		lo, hi := min(rank[e.U], rank[e.V]), max(rank[e.U], rank[e.V])
		for l := lo; l < hi; l++ {
			gapAfter[l] = max(gapAfter[l], e.LabelMain+labelGapPad)
		}
	}
	layerMainStart := make([]float64, maxRank+1)
	layerMainExtent := make([]float64, maxRank+1)
	var mainCursor float64
	for l := 0; l <= maxRank; l++ {
		var extent float64
		for _, i := range layers[l] {
			extent = max(extent, mainSize(i))
		}
		layerMainStart[l] = mainCursor
		layerMainExtent[l] = extent
		mainCursor += extent + gapAfter[l]
	}
	var totalMain float64
	if mainCursor > 0 {
		totalMain = mainCursor - gapAfter[maxRank]
	}

	// Across the flow, each rank is centred against the widest.
	layerCrossTotal := make([]float64, maxRank+1)
	var maxCross float64
	for l := 0; l <= maxRank; l++ {
		var c float64
		for k, i := range layers[l] {
			c += crossSize(i)
			if k+1 < len(layers[l]) {
				c += nodeGap
			}
		}
		layerCrossTotal[l] = c
		maxCross = max(maxCross, c)
	}
	for l := 0; l <= maxRank; l++ {
		cross := (maxCross - layerCrossTotal[l]) / 2
		for _, i := range layers[l] {
			crossCenter := cross + crossSize(i)/2
			mainCenter := layerMainStart[l] + layerMainExtent[l]/2
			var p Point
			switch direction {
			case mermaid.TB:
				p = Point{crossCenter, mainCenter}
			case mermaid.BT:
				p = Point{crossCenter, totalMain - mainCenter}
			case mermaid.LR:
				p = Point{mainCenter, crossCenter}
			case mermaid.RL:
				p = Point{totalMain - mainCenter, crossCenter}
			}
			center[i] = p
			cross += crossSize(i) + nodeGap
		}
	}

	copy(out.Centers, center[:n])
	for ei := range edges {
		for _, slot := range edgeSlots[ei] {
			out.EdgeBends[ei] = append(out.EdgeBends[ei], center[slot])
		}
	}
	return out
}

// LayeredCenters is PlaceLayered's centres alone, for a caller that routes
// its own edges.
func LayeredCenters(sizes []Size, edges []LayeredEdge, direction mermaid.Direction, rankGap, nodeGap float64) []Point {
	return PlaceLayered(sizes, edges, direction, rankGap, nodeGap).Centers
}

func abs(x int) int {
	if x < 0 {
		return -x
	}
	return x
}

// PlaceEdgeLabel is where an edge label of the given size goes: on path, in
// open space. The middle of the path is preferred, because that is where a
// reader looks. When something is there already, the label slides along the
// path, and then steps sideways off it, until it clears every rectangle in
// obstacles; pass the node boxes and the labels placed so far, so labels do
// not stack either. When nothing clears, in a diagram with no room anywhere,
// the middle of the path is returned.
func PlaceEdgeLabel(path Outline, size Size, obstacles []Rect) Rect {
	clear := func(r Rect) bool {
		for _, o := range obstacles {
			if r.Intersects(o) {
				return false
			}
		}
		return true
	}
	if path.Empty() {
		return rectAround(Point{}, size)
	}
	preferred := rectAround(path.PointAtPercent(0.5), size)
	if clear(preferred) {
		return preferred
	}

	// Slide along the path, either side of the middle in turn, keeping clear
	// of the ends so the label never sits under an arrowhead.
	for step := 1; step <= 8; step++ {
		d := float64(step) * 0.05
		for _, t := range []float64{0.5 - d, 0.5 + d} {
			if t < 0.12 || t > 0.88 {
				continue
			}
			if r := rectAround(path.PointAtPercent(t), size); clear(r) {
				return r
			}
		}
	}

	// Still nothing: step sideways off the line, which keeps the label
	// beside its own edge rather than on a box.
	mid := path.PointAtPercent(0.5)
	angle := path.AngleAtPercent(0.5) * math.Pi / 180
	perp := Point{-math.Sin(angle), -math.Cos(angle)}
	for step := 1; step <= 4; step++ {
		d := float64(step) * (size.H + 4)
		for _, s := range []float64{1, -1} {
			if r := rectAround(mid.Add(perp.Mul(d*s)), size); clear(r) {
				return r
			}
		}
	}
	return preferred
}

// PlaceEndLabel is where a label belonging to one end of an edge goes, such
// as a UML cardinality, which means nothing unless the reader can tell which
// end it is on. tip is that end and dir points from it along the edge. The
// label sits beside the line past the markerLength the end's marker covers,
// so it is on neither the marker nor the line, and moves further out until
// it clears obstacles. When nothing clears, the first place tried is
// returned.
func PlaceEndLabel(tip, dir Point, markerLength float64, size Size, obstacles []Rect) Rect {
	d := Point{1, 0}
	if l := dir.Len(); l > 0.001 {
		d = dir.Div(l)
	}
	perp := Point{-d.Y, d.X}
	// The label's reach along and across the line, so the clearances hold
	// whichever way the edge runs.
	halfAlong := (math.Abs(d.X)*size.W + math.Abs(d.Y)*size.H) / 2
	halfAcross := (math.Abs(perp.X)*size.W + math.Abs(perp.Y)*size.H) / 2
	const pad = 3.0
	across := halfAcross + pad
	clear := func(r Rect) bool {
		for _, o := range obstacles {
			if r.Intersects(o) {
				return false
			}
		}
		return true
	}
	along0 := markerLength + halfAlong + pad
	var first Rect
	haveFirst := false
	for step := 0; step <= 6; step++ {
		along := along0 + float64(step)*7
		for _, side := range []float64{1, -1} {
			r := rectAround(tip.Add(d.Mul(along)).Add(perp.Mul(across*side)), size)
			if !haveFirst {
				first, haveFirst = r, true
			}
			if clear(r) {
				return r
			}
		}
	}
	return first
}

// FinalizeSceneBounds moves everything in the scene so that it starts at
// (margin, margin), and sets Bounds to it plus the margin on every side.
func FinalizeSceneBounds(scene *Scene, margin float64) {
	var bounds Rect
	grow := func(r Rect) {
		if bounds.IsNull() {
			bounds = r
		} else {
			bounds = bounds.United(r)
		}
	}
	for _, g := range scene.Groups {
		grow(g.Rect)
	}
	for _, s := range scene.Shapes {
		grow(s.Rect)
	}
	for _, t := range scene.Texts {
		grow(t.Rect)
	}
	for _, p := range scene.Paths {
		grow(p.Outline.Bounds())
	}

	shift := Point{margin - bounds.X, margin - bounds.Y}
	for i := range scene.Groups {
		scene.Groups[i].Rect = scene.Groups[i].Rect.Translated(shift)
	}
	for i := range scene.Shapes {
		scene.Shapes[i].Rect = scene.Shapes[i].Rect.Translated(shift)
	}
	for i := range scene.Texts {
		scene.Texts[i].Rect = scene.Texts[i].Rect.Translated(shift)
	}
	for i := range scene.Paths {
		p := &scene.Paths[i]
		p.Outline = p.Outline.Translated(shift)
		p.StartPoint = p.StartPoint.Add(shift)
		p.EndPoint = p.EndPoint.Add(shift)
	}
	scene.Bounds = Rect{0, 0, bounds.W + 2*margin, bounds.H + 2*margin}
}

// borderPoint is where the line from the centre of r towards toward leaves
// r's rectangle, which is near enough the outline for every shape.
func borderPoint(r Rect, toward Point) Point {
	c := r.Center()
	d := toward.Sub(c)
	if fuzzyIsNull(d.X) && fuzzyIsNull(d.Y) {
		return c
	}
	tx, ty := 1e9, 1e9
	if !fuzzyIsNull(d.X) {
		tx = r.W / 2 / math.Abs(d.X)
	}
	if !fuzzyIsNull(d.Y) {
		ty = r.H / 2 / math.Abs(d.Y)
	}
	return c.Add(d.Mul(min(tx, ty)))
}

// laneRoute is the route through an edge's placeholders from a to b: a
// quadratic curve around each, meeting the next halfway, so the corners are
// rounded rather than sharp.
func laneRoute(a, b Point, way []Point) Outline {
	var o Outline
	o.MoveTo(a)
	for k, w := range way {
		next := b
		if k+1 < len(way) {
			next = w.Add(way[k+1]).Div(2)
		}
		o.QuadTo(w, next)
	}
	return o
}

// selfLoop is the small loop an edge from a node to itself makes off the
// node's right side, bulging out by bulge.
func selfLoop(a, b Point, bulge float64) Outline {
	var o Outline
	o.MoveTo(a)
	o.CubicTo(a.Add(Point{bulge, -bulge * 0.3}), b.Add(Point{bulge, bulge * 0.3}), b)
	return o
}

// straight is the line from a to b.
func straight(a, b Point) Outline {
	var o Outline
	o.MoveTo(a)
	o.LineTo(b)
	return o
}

// estimateMeasurer measures text when LayoutOptions.Measure is not set, with
// an average advance per character and a line height typical of a sans-serif
// font. A caller that shows the diagram always sets Measure.
type estimateMeasurer struct{ size float64 }

func (m estimateMeasurer) Advance(s string) float64 {
	return float64(utf8.RuneCountInString(s)) * m.size * 0.55
}

func (m estimateMeasurer) Height() float64 { return m.size * 1.2 }

// prepared is the options with the defaults filled in: a font size of 14
// and a measurer.
func prepared(opts LayoutOptions) LayoutOptions {
	if opts.FontSize <= 0 {
		opts.FontSize = 14
	}
	if opts.Measure == nil {
		opts.Measure = estimateMeasurer{opts.FontSize}
	}
	return opts
}

// newShape is a Shape with the default node colours, a 1.5-pixel stroke
// and no source span.
func newShape(kind ShapeKind, r Rect) Shape {
	return Shape{
		Kind: kind, Rect: r,
		FillRole: RoleNodeFill, StrokeRole: RoleNodeStroke, StrokeWidth: 1.5,
		Src: mermaid.NoSpan,
	}
}

// newPath is a Path with the default edge colour, a 1.5-pixel solid
// stroke, not selectable and no source span.
func newPath(o Outline) Path {
	return Path{Outline: o, StrokeRole: RoleEdgeStroke, StrokeWidth: 1.5, EdgeIndex: -1, Src: mermaid.NoSpan}
}

// newText is a Text with the default label colour at 14 pixels,
// centred.
func newText(text string, r Rect) Text {
	return Text{Text: text, Rect: r, Role: RoleLabel, FontSize: 14}
}
