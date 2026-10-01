package diagram

// The state-diagram layout. Composite states are laid out from the inside
// out: the layered core places each composite's members in the composite's
// own coordinates, and the composite then takes part in its parent's
// placement as one node the size of its content. Transitions are routed
// afterwards between the finished boxes. A note joins its state's scope as a
// node of its own, tied to the state by a dashed line.

import (
	"fmt"
	"slices"

	"github.com/kvit-s/kvit-notes/mermaid"
)

const (
	stateRankGap   = 52.0
	stateNodeGap   = 36.0
	stateMargin    = 16.0
	statePadX      = 12.0
	stateTitleBand = 24.0 // the height of a composite's title band
)

// stateItem is one node of a scope's placement: a state, or a note.
type stateItem struct {
	stateIndex int // a state, or -1
	noteIndex  int // a note, or -1
	size       Size
	center     Point // in the coordinates of the scope that placed it
}

// stateLayout holds one state diagram's layout while it runs.
type stateLayout struct {
	ast   *mermaid.StateAst
	opts  LayoutOptions
	scene *Scene

	items       []stateItem
	itemOfState []int         // state index to item id
	itemOfNote  []int         // note index to item id
	children    map[int][]int // scope (a state index, -1 for the top) to item ids
	absRect     []Rect        // item id to its rectangle in the scene
	placed      []bool        // item id has been given absRect
	// The bend points of transitions that cross ranks, in the coordinates of
	// the scope that placed them, with that scope's origin in the scene.
	transitionBends map[int][]Point
	transitionScope map[int]int
	scopeOrigin     map[int]Point
}

func (l *stateLayout) textW(s string) float64 {
	var w float64
	for _, line := range LabelLines(s) {
		w = max(w, l.opts.Measure.Advance(line))
	}
	return w
}

func (l *stateLayout) textH(s string) float64 {
	if s == "" {
		return 0
	}
	return float64(len(LabelLines(s))) * l.opts.Measure.Height()
}

func (l *stateLayout) horizontal() bool { return horizontal(l.ast.Direction) }

// leafSize is the size of a state that is not composite.
func (l *stateLayout) leafSize(s *mermaid.StateNode) Size {
	switch s.Kind {
	case mermaid.StateStart:
		return Size{14, 14}
	case mermaid.StateEnd:
		return Size{18, 18}
	case mermaid.StateChoice:
		return Size{30, 30}
	case mermaid.StateFork, mermaid.StateJoin:
		if l.horizontal() {
			return Size{8, 64}
		}
		return Size{64, 8}
	}
	lineH := l.opts.Measure.Height()
	w := max(60, l.textW(s.Label)+2*statePadX)
	h := max(30, l.textH(s.Label)+14)
	if len(s.Descriptions) > 0 {
		for _, d := range s.Descriptions {
			w = max(w, l.textW(d)+2*statePadX)
		}
		h += float64(len(s.Descriptions))*lineH + 8
	}
	return Size{w, h}
}

// projectToScope is the item of the child of scope that holds stateIndex,
// the state itself or the composite it is inside, or -1 when the state is
// not inside scope.
func (l *stateLayout) projectToScope(stateIndex, scope int) int {
	for cur := stateIndex; cur >= 0; {
		parent := l.ast.States[cur].ParentIndex
		if parent == scope {
			return l.itemOfState[cur]
		}
		cur = parent
	}
	return -1
}

// placeScope places the items of scope in its own coordinates, their top
// left at the origin, and returns the size they take.
func (l *stateLayout) placeScope(scope int) Size {
	kids := l.children[scope]
	if len(kids) == 0 {
		return Size{80, 40}
	}

	// A composite sizes its own content first.
	sizes := make([]Size, 0, len(kids))
	for _, itemID := range kids {
		item := &l.items[itemID]
		if item.stateIndex >= 0 && l.ast.States[item.stateIndex].Composite {
			inner := l.placeScope(item.stateIndex)
			item.size = Size{inner.W + 2*statePadX, inner.H + stateTitleBand + statePadX}
		}
		sizes = append(sizes, item.size)
	}

	// The transitions seen from this scope.
	var ledges []LayeredEdge
	var transitionOfLedge []int
	for ti, t := range l.ast.Transitions {
		a := l.projectToScope(l.ast.IndexOfState(t.From), scope)
		b := l.projectToScope(l.ast.IndexOfState(t.To), scope)
		if a < 0 || b < 0 || a == b {
			continue
		}
		le := LayeredEdge{U: slices.Index(kids, a), V: slices.Index(kids, b), MinLen: 1}
		if t.Label != "" {
			if l.horizontal() {
				le.LabelMain = l.textW(t.Label) + 8
			} else {
				le.LabelMain = l.opts.Measure.Height()
			}
		}
		transitionOfLedge = append(transitionOfLedge, ti)
		ledges = append(ledges, le)
	}
	for ni, note := range l.ast.Notes {
		noteItem := l.itemOfNote[ni]
		if !slices.Contains(kids, noteItem) {
			continue
		}
		stItem := -1
		if st := l.ast.IndexOfState(note.StateID); st >= 0 {
			stItem = l.itemOfState[st]
		}
		if stItem >= 0 && slices.Contains(kids, stItem) {
			if note.LeftOf {
				ledges = append(ledges, LayeredEdge{U: slices.Index(kids, noteItem), V: slices.Index(kids, stItem), MinLen: 1})
			} else {
				ledges = append(ledges, LayeredEdge{U: slices.Index(kids, stItem), V: slices.Index(kids, noteItem), MinLen: 1})
			}
		}
	}

	layered := PlaceLayered(sizes, ledges, l.ast.Direction, stateRankGap, stateNodeGap)
	centers := layered.Centers

	// Move the content's top left to the origin.
	minX, minY, maxX, maxY := 1e18, 1e18, -1e18, -1e18
	for k := range kids {
		minX = min(minX, centers[k].X-sizes[k].W/2)
		minY = min(minY, centers[k].Y-sizes[k].H/2)
		maxX = max(maxX, centers[k].X+sizes[k].W/2)
		maxY = max(maxY, centers[k].Y+sizes[k].H/2)
	}
	shift := Point{minX, minY}
	for k, itemID := range kids {
		l.items[itemID].center = centers[k].Sub(shift)
	}
	// Keep each transition's bend points in this scope's coordinates. A
	// transition belongs to one scope, the innermost holding both its ends
	// as separate items, and routing, which works in the scene's
	// coordinates once every scope is placed, moves them by the origin that
	// scope was put at.
	for li, ti := range transitionOfLedge {
		way := layered.EdgeBends[li]
		if len(way) == 0 {
			continue
		}
		local := make([]Point, len(way))
		for k, pt := range way {
			local[k] = pt.Sub(shift)
		}
		l.transitionScope[ti] = scope
		l.transitionBends[ti] = local
	}
	return Size{maxX - minX, maxY - minY}
}

// emitLeafState adds a state that is not composite in r.
func (l *stateLayout) emitLeafState(s *mermaid.StateNode, r Rect) {
	st := resolveStyle(s.CSSClasses, l.ast.ClassDefs)
	lineH := l.opts.Measure.Height()
	switch s.Kind {
	case mermaid.StateStart:
		sh := newShape(KindCircle, r)
		sh.FillRole = RoleNodeStroke // a solid dark disc
		sh.NodeID = s.ID
		l.scene.Shapes = append(l.scene.Shapes, sh)
		return
	case mermaid.StateEnd:
		outer := newShape(KindCircle, r)
		outer.FillRole = RoleBackground
		outer.NodeID = s.ID
		inner := newShape(KindCircle, r.Adjusted(4, 4, -4, -4))
		inner.FillRole = RoleNodeStroke
		l.scene.Shapes = append(l.scene.Shapes, outer, inner)
		return
	case mermaid.StateChoice:
		sh := newShape(KindRhombus, r)
		sh.NodeID = s.ID
		l.scene.Shapes = append(l.scene.Shapes, sh)
		return
	case mermaid.StateFork, mermaid.StateJoin:
		sh := newShape(KindRect, r)
		sh.FillRole = RoleNodeStroke
		sh.NodeID = s.ID
		l.scene.Shapes = append(l.scene.Shapes, sh)
		return
	}

	box := newShape(KindRoundRect, r)
	box.NodeID = s.ID
	box.Src = s.SrcSpan
	st.apply(&box)
	l.scene.Shapes = append(l.scene.Shapes, box)

	titleH := l.textH(s.Label) + 14
	title := newText(s.Label, Rect{r.Left(), r.Top(), r.W, titleH})
	title.Bold = st.bold || len(s.Descriptions) > 0
	title.FontSize = l.opts.FontSize
	l.scene.Texts = append(l.scene.Texts, title)

	if len(s.Descriptions) > 0 {
		sep := newPath(straight(Point{r.Left(), r.Top() + titleH}, Point{r.Right(), r.Top() + titleH}))
		sep.StrokeRole = RoleNodeStroke
		sep.StrokeWidth = 1
		l.scene.Paths = append(l.scene.Paths, sep)
		y := r.Top() + titleH + 4
		for _, d := range s.Descriptions {
			tx := newText(d, Rect{r.Left() + statePadX, y, r.W - 2*statePadX, lineH})
			tx.FontSize = max(10, l.opts.FontSize-1)
			tx.Align = AlignLeft
			l.scene.Texts = append(l.scene.Texts, tx)
			y += lineH
		}
	}
}

// emitScope adds the items of scope with the scope's content at origin.
func (l *stateLayout) emitScope(scope int, origin Point) {
	l.scopeOrigin[scope] = origin
	for _, itemID := range l.children[scope] {
		item := l.items[itemID]
		r := Rect{
			origin.X + item.center.X - item.size.W/2, origin.Y + item.center.Y - item.size.H/2,
			item.size.W, item.size.H,
		}
		l.absRect[itemID] = r
		l.placed[itemID] = true
		switch {
		case item.stateIndex >= 0:
			s := &l.ast.States[item.stateIndex]
			if s.Composite {
				l.scene.Groups = append(l.scene.Groups, Group{Rect: r, Title: s.Label})
				l.emitScope(item.stateIndex, r.TopLeft().Add(Point{statePadX, stateTitleBand}))
			} else {
				l.emitLeafState(s, r)
			}
		case item.noteIndex >= 0:
			sh := newShape(KindRect, r)
			sh.FillRole, sh.StrokeRole = RoleNoteFill, RoleNoteStroke
			sh.StrokeWidth = 1
			l.scene.Shapes = append(l.scene.Shapes, sh)
			tx := newText(l.ast.Notes[item.noteIndex].Text, r)
			tx.FontSize = max(10, l.opts.FontSize-1)
			l.scene.Texts = append(l.scene.Texts, tx)
		}
	}
}

func (l *stateLayout) run() {
	lineH := l.opts.Measure.Height()
	// An item for each state.
	l.itemOfState = make([]int, len(l.ast.States))
	for i := range l.ast.States {
		s := &l.ast.States[i]
		item := stateItem{stateIndex: i, noteIndex: -1}
		if !s.Composite {
			item.size = l.leafSize(s)
		}
		l.items = append(l.items, item)
		l.itemOfState[i] = len(l.items) - 1
		l.children[s.ParentIndex] = append(l.children[s.ParentIndex], len(l.items)-1)
	}
	// An item for each note, in its state's scope.
	l.itemOfNote = make([]int, len(l.ast.Notes))
	for ni, note := range l.ast.Notes {
		w := min(l.textW(note.Text), 260) + 20
		h := max(l.textH(note.Text), lineH) + 12
		l.items = append(l.items, stateItem{stateIndex: -1, noteIndex: ni, size: Size{w, h}})
		l.itemOfNote[ni] = len(l.items) - 1
		scope := -1
		if st := l.ast.IndexOfState(note.StateID); st >= 0 {
			scope = l.ast.States[st].ParentIndex
		}
		l.children[scope] = append(l.children[scope], len(l.items)-1)
	}
	l.absRect = make([]Rect, len(l.items))
	l.placed = make([]bool, len(l.items))

	l.placeScope(-1)
	l.emitScope(-1, Point{})

	// ---- transitions, routed between the boxes in the scene ----
	// What a transition label keeps clear of: every state box and note, and
	// the labels placed so far. A composite's own box is not in the way,
	// because the transitions between its members belong inside it.
	var taken []Rect
	for itemID, item := range l.items {
		if !l.placed[itemID] {
			continue
		}
		if item.stateIndex >= 0 && l.ast.States[item.stateIndex].Composite {
			continue
		}
		taken = append(taken, l.absRect[itemID])
	}
	itemOf := func(id string) int {
		if s := l.ast.IndexOfState(id); s >= 0 {
			return l.itemOfState[s]
		}
		return -1
	}
	for ti := range l.ast.Transitions {
		t := &l.ast.Transitions[ti]
		a, b := itemOf(t.From), itemOf(t.To)
		if a < 0 || b < 0 {
			continue
		}
		ra, rb := l.absRect[a], l.absRect[b]
		p := newPath(Outline{})
		p.EdgeIndex = ti
		p.Src = t.SrcSpan
		p.StrokeWidth = 1.4
		p.EndMarker = MarkerArrow
		// The bend points, moved from the scope that placed this transition
		// into the scene's coordinates.
		var way []Point
		if local, ok := l.transitionBends[ti]; ok {
			origin := l.scopeOrigin[l.transitionScope[ti]]
			for _, pt := range local {
				way = append(way, origin.Add(pt))
			}
		}
		var pa, pb Point
		switch {
		case a == b:
			pa = Point{ra.Right(), ra.Center().Y - ra.H*0.2}
			pb = Point{ra.Right(), ra.Center().Y + ra.H*0.2}
			p.Outline = selfLoop(pa, pb, max(24, ra.W*0.4))
			p.StartDir, p.EndDir = Point{-1, 0}, Point{-1, 0}
		case len(way) > 0:
			// The transition crosses other ranks, as one back to an earlier
			// state usually does, so it follows the lane kept for it in each
			// of them instead of cutting across their boxes.
			pa = borderPoint(ra, way[0])
			pb = borderPoint(rb, way[len(way)-1])
			p.Outline = laneRoute(pa, pb, way)
			p.StartDir = unit(pa.Sub(way[0]))
			p.EndDir = unit(pb.Sub(way[len(way)-1]))
		default:
			pa = borderPoint(ra, rb.Center())
			pb = borderPoint(rb, ra.Center())
			p.Outline = straight(pa, pb)
			d := unit(pb.Sub(pa))
			p.StartDir, p.EndDir = d.Neg(), d
		}
		p.StartPoint, p.EndPoint = pa, pb
		l.scene.Paths = append(l.scene.Paths, p)

		if t.Label != "" {
			tx := newText(t.Label, PlaceEdgeLabel(p.Outline, Size{l.textW(t.Label) + 8, lineH}, taken))
			tx.Role = RoleEdgeLabel
			tx.FontSize = max(10, l.opts.FontSize-1)
			tx.HasBackground = true
			taken = append(taken, tx.Rect)
			l.scene.Texts = append(l.scene.Texts, tx)
		}
	}

	// ---- the dashed lines tying notes to their states ----
	for ni, note := range l.ast.Notes {
		st := l.ast.IndexOfState(note.StateID)
		if st < 0 {
			continue
		}
		rn, rs := l.absRect[l.itemOfNote[ni]], l.absRect[l.itemOfState[st]]
		from, to := borderPoint(rn, rs.Center()), borderPoint(rs, rn.Center())
		p := newPath(straight(from, to))
		p.Style = LineDashed
		p.StrokeRole = RoleNoteStroke
		p.StrokeWidth = 1
		p.StartPoint, p.EndPoint = from, to
		l.scene.Paths = append(l.scene.Paths, p)
	}
}

// LayoutState lays out a state diagram in the direction it names.
func LayoutState(ast *mermaid.StateAst, opts LayoutOptions) Scene {
	opts = prepared(opts)
	scene := Scene{AccTitle: ast.AccTitle, AccDescr: ast.AccDescr}
	if scene.AccTitle == "" {
		scene.AccTitle = ast.Title
	}
	// The [*] start and end states are not counted.
	visible := 0
	for _, s := range ast.States {
		if s.Kind == mermaid.StateNormal {
			visible++
		}
	}
	scene.Summary = fmt.Sprintf("Mermaid state diagram with %d state%s and %d transition%s",
		visible, plural(visible, "s"), len(ast.Transitions), plural(len(ast.Transitions), "s"))
	if len(ast.States) == 0 {
		return scene
	}
	l := stateLayout{
		ast: ast, opts: opts, scene: &scene,
		children:        map[int][]int{},
		transitionBends: map[int][]Point{},
		transitionScope: map[int]int{},
		scopeOrigin:     map[int]Point{},
	}
	l.run()
	FinalizeSceneBounds(&scene, stateMargin)
	return scene
}
