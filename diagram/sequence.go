package diagram

// The sequence-diagram layout. Lifelines make the columns, messages, notes
// and fragments make the rows, and labels widen the columns before anything
// is placed. It follows the conventions a Mermaid user knows (activation
// bars, dashed return arrows, note boxes, labelled fragment frames) without
// copying Mermaid.js's pixel geometry.

import (
	"fmt"
	"slices"
	"strconv"

	"github.com/kvit-s/kvit-notes/mermaid"
)

const (
	seqColGapMin   = 46.0 // least room between lifeline centres
	seqBoxGap      = 24.0 // more between participants in different boxes
	seqRowGap      = 12.0 // room between rows
	seqActHalf     = 5.0  // half an activation bar's width
	seqSelfLoopW   = 40.0 // how far a message to oneself loops out
	seqFrameHeader = 22.0 // the height of a fragment's header band
	seqMargin      = 16.0
)

// blockName is the word a fragment's chip shows.
func blockName(b mermaid.SeqBlock) string {
	switch b {
	case mermaid.BlockLoop:
		return "loop"
	case mermaid.BlockAlt:
		return "alt"
	case mermaid.BlockOpt:
		return "opt"
	case mermaid.BlockPar:
		return "par"
	case mermaid.BlockCritical:
		return "critical"
	case mermaid.BlockBreak:
		return "break"
	case mermaid.BlockRect:
		return "rect"
	}
	return ""
}

// markerForHead is the marker a message's arrowhead is drawn with.
func markerForHead(h mermaid.SeqHead) Marker {
	switch h {
	case mermaid.HeadOpen:
		return MarkerOpenArrow
	case mermaid.HeadCross:
		return MarkerCross
	case mermaid.HeadPoint:
		return MarkerDot
	}
	return MarkerArrow
}

// LayoutSequence lays out a sequence diagram. Its direction is always top
// to bottom.
func LayoutSequence(ast *mermaid.SequenceAst, opts LayoutOptions) Scene {
	opts = prepared(opts)
	scene := Scene{AccTitle: ast.AccTitle, AccDescr: ast.AccDescr}
	if scene.AccTitle == "" {
		scene.AccTitle = ast.Title
	}
	np := len(ast.Participants)
	nm := ast.MessageCount()
	scene.Summary = fmt.Sprintf("Mermaid sequence diagram with %d participant%s and %d message%s",
		np, plural(np, "s"), nm, plural(nm, "s"))
	if np == 0 {
		return scene
	}
	fm := opts.Measure
	lineH := fm.Height()

	textW := func(s string) float64 {
		var w float64
		for _, l := range LabelLines(s) {
			w = max(w, fm.Advance(l))
		}
		return w
	}
	textH := func(s string) float64 {
		if s == "" {
			return 0
		}
		return float64(len(LabelLines(s))) * lineH
	}
	// The measures of the labels Mermaid allows mathematics in: participant
	// names, messages and notes. Everything else (the title, a fragment's
	// condition, the block words) is measured as text, because text is what
	// is drawn for it.
	labelW := func(t string) float64 {
		if sz, ok := opts.mathSize(MathLabel(t)); ok {
			return sz.W
		}
		return textW(t)
	}
	labelH := func(t string) float64 {
		if sz, ok := opts.mathSize(MathLabel(t)); ok {
			return sz.H
		}
		return textH(t)
	}
	// A note wraps its text at 260 pixels. A formula cannot wrap, so it
	// takes the width it needs and the note grows around it.
	noteW := func(t string) float64 {
		if sz, ok := opts.mathSize(MathLabel(t)); ok {
			return sz.W
		}
		return min(textW(t), 260)
	}

	idx := make(map[string]int, np)
	for i, p := range ast.Participants {
		idx[p.ID] = i
	}
	lookup := func(id string, fallback int) int {
		if i, ok := idx[id]; ok {
			return i
		}
		return fallback
	}

	// ---- header boxes ----
	headW, headH := make([]float64, np), make([]float64, np)
	for i, p := range ast.Participants {
		if p.ActorFigure {
			headW[i] = max(38, labelW(p.Label)+4)
			headH[i] = 42 + labelH(p.Label) + 4
		} else {
			headW[i] = max(60, labelW(p.Label)+26)
			headH[i] = max(34, labelH(p.Label)+18)
		}
	}
	bandH := slices.Max(headH)

	// ---- columns: least gaps between neighbours, then wider for labels ----
	gap := make([]float64, max(0, np-1))
	for g := 0; g+1 < np; g++ {
		gap[g] = headW[g]/2 + headW[g+1]/2 + seqColGapMin
		if ast.Participants[g].BoxIndex != ast.Participants[g+1].BoxIndex {
			gap[g] += seqBoxGap
		}
	}
	var leftExtra, rightExtra float64

	type spanReq struct {
		a, b  int
		width float64
	}
	var spans []spanReq
	for _, e := range ast.Events {
		switch e.Kind {
		case mermaid.EventMessage:
			i, j := lookup(e.From, -1), lookup(e.To, -1)
			if i < 0 || j < 0 {
				continue
			}
			if i == j {
				need := seqSelfLoopW + textW(e.Text) + 24
				if i+1 < np {
					spans = append(spans, spanReq{i, i + 1, need + headW[i+1]/2})
				} else {
					rightExtra = max(rightExtra, need)
				}
			} else {
				spans = append(spans, spanReq{min(i, j), max(i, j), textW(e.Text) + 34})
			}
		case mermaid.EventNote:
			i := lookup(e.From, -1)
			if i < 0 {
				continue
			}
			w := noteW(e.Text) + 24
			switch e.Placement {
			case mermaid.PlaceLeftOf:
				if i > 0 {
					spans = append(spans, spanReq{i - 1, i, w + 16})
				} else {
					leftExtra = max(leftExtra, w+4)
				}
			case mermaid.PlaceRightOf:
				if i+1 < np {
					spans = append(spans, spanReq{i, i + 1, w + 16})
				} else {
					rightExtra = max(rightExtra, w+4)
				}
			case mermaid.PlaceOver:
				j := i
				if e.To != "" {
					j = lookup(e.To, i)
				}
				if j != i {
					spans = append(spans, spanReq{min(i, j), max(i, j), w})
				} else {
					if i > 0 {
						spans = append(spans, spanReq{i - 1, i, w/2 + 8})
					} else {
						leftExtra = max(leftExtra, w/2)
					}
					if i+1 < np {
						spans = append(spans, spanReq{i, i + 1, w/2 + 8})
					} else {
						rightExtra = max(rightExtra, w/2)
					}
				}
			}
		}
	}
	slices.SortStableFunc(spans, func(x, y spanReq) int { return (x.b - x.a) - (y.b - y.a) })
	for _, s := range spans {
		if s.b == s.a+1 {
			gap[s.a] = max(gap[s.a], s.width)
			continue
		}
		var total float64
		for k := s.a; k < s.b; k++ {
			total += gap[k]
		}
		if total < s.width {
			add := (s.width - total) / float64(s.b-s.a)
			for k := s.a; k < s.b; k++ {
				gap[k] += add
			}
		}
	}
	cx := make([]float64, np)
	cx[0] = leftExtra + headW[0]/2
	for g := 0; g+1 < np; g++ {
		cx[g+1] = cx[g] + gap[g]
	}
	firstX, lastX := cx[0], cx[np-1]

	// ---- walk down the events ----
	var y float64
	if ast.Title != "" {
		w := textW(ast.Title) + 20
		t := newText(ast.Title, Rect{(firstX+lastX)/2 - w/2, y, w, lineH + 4})
		t.FontSize = opts.FontSize + 2
		t.Bold = true
		scene.Texts = append(scene.Texts, t)
		y += lineH + 14
	}
	anyBoxTitle := false
	for _, b := range ast.Boxes {
		if b.Title != "" {
			anyBoxTitle = true
		}
	}
	boxTop := y
	if len(ast.Boxes) > 0 {
		if anyBoxTitle {
			y += lineH + 8
		} else {
			y += 6
		}
	}
	headerTop := y
	lifeTop := headerTop + bandH
	cursor := lifeTop + 14

	// Open activations per participant, and the bars already finished.
	act := make([][]float64, np)
	type actBar struct {
		p      int
		y0, y1 float64
		level  int
	}
	var bars []actBar
	barHalf := func(p int) float64 {
		if len(act[p]) == 0 {
			return 0
		}
		return seqActHalf + float64(len(act[p])-1)*3
	}
	popActivation := func(p int, atY float64) {
		if len(act[p]) == 0 {
			return
		}
		y0 := act[p][len(act[p])-1]
		act[p] = act[p][:len(act[p])-1]
		bars = append(bars, actBar{p, y0, atY, len(act[p])})
	}

	// Fragment frames.
	type divider struct {
		y     float64
		label string
	}
	type frame struct {
		block        mermaid.SeqBlock
		label        string
		startY, endY float64
		depth        int
		minX, maxX   float64
		dividers     []divider
	}
	var stack, closed []frame
	touchX := func(x0, x1 float64) {
		for k := range stack {
			stack[k].minX = min(stack[k].minX, x0)
			stack[k].maxX = max(stack[k].maxX, x1)
		}
	}

	type msg struct {
		i, j               int
		xFrom, xTo, yArrow float64
		label              string
		labelY             float64
		line               mermaid.SeqLine
		head               mermaid.SeqHead
		bidir, self        bool
		eventIndex         int // what selecting the message selects
		src                mermaid.Span
	}
	var msgs []msg
	type noteBox struct {
		rect Rect
		text string
	}
	var notes []noteBox

	autoOn, autoNum, autoStep := false, 1, 1

	for evIndex := range ast.Events {
		e := &ast.Events[evIndex]
		switch e.Kind {
		case mermaid.EventAutonumber:
			autoOn = e.AutonumberShown
			autoNum = e.AutonumberStart
			autoStep = e.AutonumberStep
		case mermaid.EventActivate:
			if p := lookup(e.From, -1); p >= 0 {
				act[p] = append(act[p], cursor)
			}
		case mermaid.EventDeactivate:
			if p := lookup(e.From, -1); p >= 0 {
				popActivation(p, cursor)
			}
		case mermaid.EventBlockStart:
			stack = append(stack, frame{
				block: e.Block, label: e.BlockLabel, startY: cursor,
				depth: len(stack), minX: 1e18, maxX: -1e18,
			})
			if e.Block == mermaid.BlockRect {
				cursor += 8
			} else {
				cursor += seqFrameHeader + 4
			}
		case mermaid.EventBlockDivider:
			if len(stack) > 0 {
				top := &stack[len(stack)-1]
				top.dividers = append(top.dividers, divider{cursor + 2, e.BlockLabel})
				cursor += lineH + 12
			}
		case mermaid.EventBlockEnd:
			if len(stack) == 0 {
				break
			}
			f := stack[len(stack)-1]
			stack = stack[:len(stack)-1]
			f.endY = cursor + 4
			cursor += 14
			if f.minX > f.maxX { // nothing inside: span every lifeline
				f.minX, f.maxX = firstX-20, lastX+20
			}
			if len(stack) > 0 {
				top := &stack[len(stack)-1]
				top.minX = min(top.minX, f.minX)
				top.maxX = max(top.maxX, f.maxX)
			}
			closed = append(closed, f)
		case mermaid.EventMessage:
			i, j := lookup(e.From, -1), lookup(e.To, -1)
			if i < 0 || j < 0 {
				break
			}
			label := e.Text
			if autoOn {
				if label == "" {
					label = strconv.Itoa(autoNum)
				} else {
					label = fmt.Sprintf("%d. %s", autoNum, label)
				}
			}
			autoNum += autoStep
			// The label's own height, so a formula gets the room it needs
			// and the arrow runs below it rather than through it.
			th := labelH(label)
			m := msg{
				i: i, j: j, line: e.Line, head: e.Head, bidir: e.Bidirectional,
				label: label, self: i == j, eventIndex: evIndex, src: e.SrcSpan,
				labelY: cursor,
			}
			if m.self {
				m.yArrow = cursor + max(th, 4) + 4
				m.xFrom = cx[i] + barHalf(i)
				m.xTo = m.xFrom
				touchX(cx[i]-12, cx[i]+seqSelfLoopW+labelW(label)+20)
				cursor = m.yArrow + 20 + seqRowGap
			} else {
				m.yArrow = cursor + th + 5
				dir := 1.0
				if j < i {
					dir = -1
				}
				m.xFrom = cx[i] + dir*barHalf(i)
				touchX(min(cx[i], cx[j])-12, max(cx[i], cx[j])+12)
				cursor = m.yArrow + seqRowGap
			}
			// The activation shorthand starts or ends a bar at the arrow.
			if e.ActivateTarget && !m.self {
				act[j] = append(act[j], m.yArrow)
			}
			if e.DeactivateSource && !m.self {
				popActivation(i, m.yArrow)
			}
			if !m.self {
				dir := 1.0
				if j < i {
					dir = -1
				}
				m.xTo = cx[j] - dir*barHalf(j)
			}
			msgs = append(msgs, m)
		case mermaid.EventNote:
			i := lookup(e.From, -1)
			if i < 0 {
				break
			}
			w := noteW(e.Text) + 20
			h := max(labelH(e.Text), lineH) + 12
			var r Rect
			switch e.Placement {
			case mermaid.PlaceLeftOf:
				r = Rect{cx[i] - 12 - w, cursor, w, h}
			case mermaid.PlaceRightOf:
				r = Rect{cx[i] + 12, cursor, w, h}
			case mermaid.PlaceOver:
				j := i
				if e.To != "" {
					j = lookup(e.To, i)
				}
				mid := (cx[i] + cx[j]) / 2
				left, right := min(cx[i], cx[j]), max(cx[i], cx[j])
				wOver := max(w, right-left+28)
				r = Rect{mid - wOver/2, cursor, wOver, h}
			}
			notes = append(notes, noteBox{r, e.Text})
			touchX(r.Left()-6, r.Right()+6)
			cursor += h + seqRowGap
		}
	}

	// Activations and frames still open close at the bottom.
	for p := range np {
		for len(act[p]) > 0 {
			popActivation(p, cursor)
		}
	}
	for len(stack) > 0 {
		f := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		f.endY = cursor + 4
		if f.minX > f.maxX {
			f.minX, f.maxX = firstX-20, lastX+20
		}
		closed = append(closed, f)
		cursor += 10
	}
	lifeBottom := cursor + 6

	// ---- boxes and rect blocks, behind everything ----
	boxRects := make([]Rect, len(ast.Boxes))
	if len(ast.Boxes) > 0 {
		bMin, bMax := make([]float64, len(ast.Boxes)), make([]float64, len(ast.Boxes))
		for b := range ast.Boxes {
			bMin[b], bMax[b] = 1e18, -1e18
		}
		for i, p := range ast.Participants {
			b := p.BoxIndex
			if b < 0 || b >= len(ast.Boxes) {
				continue
			}
			bMin[b] = min(bMin[b], cx[i]-headW[i]/2-10)
			bMax[b] = max(bMax[b], cx[i]+headW[i]/2+10)
		}
		for b, box := range ast.Boxes {
			if bMin[b] > bMax[b] {
				continue
			}
			g := Group{Rect: Rect{bMin[b], boxTop, bMax[b] - bMin[b], lifeBottom + bandH + 6 - boxTop}}
			boxRects[b] = g.Rect
			if box.Color.Set {
				c := box.Color
				c.A = 70
				g.FillOverride = c
			}
			scene.Groups = append(scene.Groups, g)
		}
	}
	maxDepth := 0
	for _, f := range closed {
		maxDepth = max(maxDepth, f.depth)
	}
	for _, f := range closed {
		if f.block != mermaid.BlockRect {
			continue
		}
		pad := 8 + float64(maxDepth-f.depth)*6
		g := Group{
			Rect:     Rect{f.minX - pad, f.startY, f.maxX - f.minX + 2*pad, f.endY - f.startY},
			NoBorder: true,
		}
		if c := parseCSSColor(f.label); c.Set {
			g.FillOverride = c
		}
		scene.Groups = append(scene.Groups, g)
	}

	// ---- lifelines ----
	for i := range np {
		a, b := Point{cx[i], lifeTop}, Point{cx[i], lifeBottom}
		p := newPath(straight(a, b))
		p.Style = LineDashed
		p.StrokeWidth = 1
		p.StartPoint, p.EndPoint = a, b
		scene.Paths = append(scene.Paths, p)
	}

	// ---- activation bars ----
	for _, b := range bars {
		xoff := float64(b.level) * 3
		s := newShape(KindRect, Rect{cx[b.p] - seqActHalf + xoff, b.y0, seqActHalf * 2, max(6, b.y1-b.y0)})
		s.FillRole = RoleActivation
		s.StrokeWidth = 1
		scene.Shapes = append(scene.Shapes, s)
	}

	// ---- messages ----
	for _, m := range msgs {
		p := newPath(Outline{})
		p.EdgeIndex = m.eventIndex
		p.Src = m.src
		if m.line == mermaid.SeqDotted {
			p.Style = LineDashed
		}
		p.StrokeWidth = 1.4
		if m.self {
			x0, yTop, yBot := m.xFrom, m.yArrow, m.yArrow+16
			p.Outline.MoveTo(Point{x0, yTop})
			p.Outline.LineTo(Point{x0 + seqSelfLoopW, yTop})
			p.Outline.LineTo(Point{x0 + seqSelfLoopW, yBot})
			p.Outline.LineTo(Point{x0 + 2, yBot})
			p.StartPoint, p.EndPoint = Point{x0, yTop}, Point{x0 + 2, yBot}
			p.StartDir, p.EndDir = Point{1, 0}, Point{-1, 0}
		} else {
			a, b := Point{m.xFrom, m.yArrow}, Point{m.xTo, m.yArrow}
			p.Outline = straight(a, b)
			p.StartPoint, p.EndPoint = a, b
			dir := 1.0
			if m.xTo < m.xFrom {
				dir = -1
			}
			p.StartDir, p.EndDir = Point{-dir, 0}, Point{dir, 0}
		}
		p.EndMarker = markerForHead(m.head)
		if m.bidir {
			p.StartMarker = p.EndMarker
		}
		scene.Paths = append(scene.Paths, p)

		if m.label != "" {
			w, h := labelW(m.label)+8, labelH(m.label)
			var r Rect
			if m.self {
				r = Rect{m.xFrom + seqSelfLoopW + 8, m.labelY, w, h + 4}
			} else {
				r = Rect{(m.xFrom+m.xTo)/2 - w/2, m.labelY, w, h + 2}
			}
			t := newText(m.label, r)
			t.Tex = opts.mathTex(m.label)
			t.Role = RoleEdgeLabel
			t.FontSize = max(10, opts.FontSize-1)
			if m.self {
				t.Align = AlignLeft
			}
			scene.Texts = append(scene.Texts, t)
		}
	}

	// ---- notes ----
	for _, n := range notes {
		s := newShape(KindRect, n.rect)
		s.FillRole, s.StrokeRole = RoleNoteFill, RoleNoteStroke
		s.StrokeWidth = 1
		scene.Shapes = append(scene.Shapes, s)
		t := newText(n.text, n.rect)
		t.Tex = opts.mathTex(n.text)
		t.FontSize = max(10, opts.FontSize-1)
		scene.Texts = append(scene.Texts, t)
	}

	// ---- fragment frames ----
	for _, f := range closed {
		if f.block == mermaid.BlockRect {
			continue
		}
		pad := 10 + float64(maxDepth-f.depth)*6
		r := Rect{f.minX - pad, f.startY, f.maxX - f.minX + 2*pad, f.endY - f.startY}
		var border Outline
		border.AddRect(r)
		bp := newPath(border)
		bp.StrokeRole = RoleSubgraphStroke
		bp.StrokeWidth = 1.2
		scene.Paths = append(scene.Paths, bp)

		// The chip naming the kind, at the frame's top left.
		kind := blockName(f.block)
		chipW := fm.Advance(kind) + 14
		chip := newShape(KindRect, Rect{r.Left(), r.Top(), chipW, seqFrameHeader - 4})
		chip.FillRole, chip.StrokeRole = RoleSubgraphFill, RoleSubgraphStroke
		chip.StrokeWidth = 1
		scene.Shapes = append(scene.Shapes, chip)
		chipText := newText(kind, chip.Rect)
		chipText.FontSize = max(9, opts.FontSize-3)
		chipText.Bold = true
		scene.Texts = append(scene.Texts, chipText)

		if f.label != "" {
			cond := newText("["+f.label+"]", Rect{r.Left() + chipW + 8, r.Top(), max(10, r.W-chipW-12), seqFrameHeader - 4})
			cond.Role = RoleEdgeLabel
			cond.Italic = true
			cond.FontSize = max(9, opts.FontSize-2)
			cond.Align = AlignLeft
			scene.Texts = append(scene.Texts, cond)
		}
		for _, d := range f.dividers {
			dp := newPath(straight(Point{r.Left(), d.y}, Point{r.Right(), d.y}))
			dp.Style = LineDashed
			dp.StrokeRole = RoleSubgraphStroke
			dp.StrokeWidth = 1
			scene.Paths = append(scene.Paths, dp)
			if d.label != "" {
				t := newText("["+d.label+"]", Rect{r.Left() + 8, d.y + 2, max(10, r.W-16), lineH})
				t.Role = RoleEdgeLabel
				t.Italic = true
				t.FontSize = max(9, opts.FontSize-2)
				t.Align = AlignLeft
				scene.Texts = append(scene.Texts, t)
			}
		}
	}

	// ---- participant headers, at the top and again at the bottom ----
	emitHeader := func(i int, top float64) {
		p := &ast.Participants[i]
		if p.ActorFigure {
			s := newShape(KindActor, Rect{cx[i] - 17, top, 34, 40})
			s.NodeID = p.ID
			s.Src = p.SrcSpan
			scene.Shapes = append(scene.Shapes, s)
			t := newText(p.Label, Rect{cx[i] - headW[i]/2, top + 42, headW[i], labelH(p.Label) + 2})
			t.Tex = opts.mathTex(p.Label)
			t.FontSize = opts.FontSize
			scene.Texts = append(scene.Texts, t)
			return
		}
		s := newShape(KindRect, Rect{cx[i] - headW[i]/2, top, headW[i], headH[i]})
		s.NodeID = p.ID
		s.Src = p.SrcSpan
		scene.Shapes = append(scene.Shapes, s)
		t := newText(p.Label, s.Rect)
		t.Tex = opts.mathTex(p.Label)
		t.FontSize = opts.FontSize
		scene.Texts = append(scene.Texts, t)
	}
	for i := range np {
		emitHeader(i, lifeTop-headH[i])
	}
	for i := range np {
		emitHeader(i, lifeBottom)
	}

	// Box titles, above the header band.
	for b, box := range ast.Boxes {
		if box.Title == "" || boxRects[b].IsNull() {
			continue
		}
		r := boxRects[b]
		t := newText(box.Title, Rect{r.Left(), r.Top() + 2, r.W, lineH})
		t.Bold = true
		t.FontSize = max(10, opts.FontSize-1)
		scene.Texts = append(scene.Texts, t)
	}

	if rightExtra > 0 {
		// Room on the right for a trailing message to oneself or note.
		scene.Texts = append(scene.Texts, newText("", Rect{lastX + rightExtra, lifeTop, 1, 1}))
	}

	FinalizeSceneBounds(&scene, seqMargin)
	return scene
}
