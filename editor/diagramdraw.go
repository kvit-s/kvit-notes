package editor

// Drawing a Mermaid diagram block: the scene's roles given the theme's
// colours, then groups, lines with their markers, shapes and text; the
// selection rings; and the block's panels, controls and notes.

import (
	"fmt"
	"math"

	"github.com/kvit-s/kvit-notes/diagram"
	"github.com/kvit-s/kvit-notes/mermaid"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
	"github.com/richardwilkes/unison/enums/pathop"
	"github.com/richardwilkes/unison/enums/strokecap"
	"github.com/richardwilkes/unison/enums/strokejoin"
)

// roleColor is the theme's colour for a scene role. ok is false for the
// background, which is drawn as nothing so the panel shows through.
func (e *Editor) roleColor(r diagram.Role) (palette.Color, bool) {
	t := e.tok()
	switch r {
	case diagram.RoleBackground:
		return palette.Color{}, false
	case diagram.RoleNodeFill:
		return t.ChipBackground, true
	case diagram.RoleNodeStroke:
		return t.Accent, true
	case diagram.RoleEdgeStroke:
		return t.TextSecondary, true
	case diagram.RoleLabel:
		return t.TextPrimary, true
	case diagram.RoleEdgeLabel:
		return t.TextMuted, true
	case diagram.RoleSubgraphFill:
		return t.BlockHoverTint, true
	case diagram.RoleSubgraphStroke:
		return t.Border, true
	case diagram.RoleNoteFill:
		return t.HighlightBackground, true
	case diagram.RoleNoteStroke:
		return t.Warning, true
	case diagram.RoleActivation:
		return t.HoverTint, true
	}
	return t.TextPrimary, true
}

// sceneColor is a role's colour, or the colour a diagram's own classDef or
// style gives in its place.
func (e *Editor) sceneColor(r diagram.Role, override mermaid.Color) (unison.Color, bool) {
	if override.Set {
		return unison.ARGB(float32(override.A)/255, int(override.R), int(override.G), int(override.B)), true
	}
	c, ok := e.roleColor(r)
	return kvitui.Color(c), ok
}

func pt(p diagram.Point) geom.Point { return geom.NewPoint(float32(p.X), float32(p.Y)) }

func rect(r diagram.Rect) geom.Rect {
	return geom.NewRect(float32(r.X), float32(r.Y), float32(r.W), float32(r.H))
}

// toPath is an outline as a unison path.
func toPath(o diagram.Outline) *unison.Path {
	p := unison.NewPath()
	for _, s := range o.Segs {
		switch s.Kind {
		case diagram.MoveTo:
			p.MoveTo(pt(s.Pts[0]))
		case diagram.LineTo:
			p.LineTo(pt(s.Pts[0]))
		case diagram.QuadTo:
			p.QuadTo(pt(s.Pts[0]), pt(s.Pts[1]))
		case diagram.CubicTo:
			p.CubicTo(pt(s.Pts[0]), pt(s.Pts[1]), pt(s.Pts[2]))
		case diagram.Close:
			p.Close()
		}
	}
	return p
}

// pen is a stroke in a colour, width and line style, with round ends and
// joins when round is set. A dash is four widths long with two between, and
// a dot one with two between.
func pen(c unison.Color, width float32, style diagram.LineStyle, round bool) *unison.Paint {
	p := unison.NewPaint()
	p.SetAntialias(true)
	p.SetColor(c)
	p.SetStyle(paintstyle.Stroke)
	p.SetStrokeWidth(width)
	if round {
		p.SetStrokeCap(strokecap.Round)
		p.SetStrokeJoin(strokejoin.Round)
	}
	switch style {
	case diagram.LineDashed:
		p.SetPathEffect(unison.NewDashPathEffect([]float32{4 * width, 2 * width}, 0))
	case diagram.LineDotted:
		p.SetPathEffect(unison.NewDashPathEffect([]float32{width, 2 * width}, 0))
	}
	return p
}

func brush(c unison.Color) *unison.Paint {
	p := unison.NewPaint()
	p.SetAntialias(true)
	p.SetColor(c)
	p.SetStyle(paintstyle.Fill)
	return p
}

// paintScene draws a scene in its own coordinates, with the selection and
// the highlighted element of c when c is given.
func (e *Editor) paintScene(gc *unison.Canvas, s *diagram.Scene, c *diagramCanvas) {
	t := e.tok()
	family := e.ui.Typography.FontFamily()

	// Groups, behind everything.
	for _, g := range s.Groups {
		fill, _ := e.sceneColor(diagram.RoleSubgraphFill, g.FillOverride)
		o := diagram.GroupOutline(g)
		gc.DrawPath(toPath(o), brush(fill))
		if !g.NoBorder {
			gc.DrawPath(toPath(o), pen(kvitui.Color(t.Border), 1, diagram.LineDashed, false))
		}
		if g.Title != "" {
			st := e.chrome(roleBody, text.Bold, e.groupTitleColor())
			st.Family = family
			tr := rect(diagram.GroupTitleRect(g))
			l := e.ui.Fonts.Layout([]text.Span{{Text: g.Title, Style: st}}, text.Options{})
			_, h := l.Size()
			l.Draw(gc, tr.X, tr.Y+(tr.Height-h)/2)
		}
	}

	// Lines, under the shapes so arrowheads meet the borders cleanly. A line
	// that divides a box (a class's compartments, an entity's rows, a
	// state's descriptions) waits until the boxes are drawn, since under the
	// box the box's fill would hide it.
	var dividers []diagram.Path
	for _, p := range s.Paths {
		if isDivider(p) {
			dividers = append(dividers, p)
			continue
		}
		e.drawScenePath(gc, p)
	}

	// Shapes.
	for _, sh := range s.Shapes {
		stroke, hasStroke := e.sceneColor(sh.StrokeRole, sh.StrokeOverride)
		if sh.Kind == diagram.KindActor {
			gc.DrawPath(toPath(diagram.ActorFigure(sh.Rect)), pen(stroke, diagram.ActorStrokeWidth, diagram.LineSolid, true))
			continue
		}
		path := toPath(diagram.ShapeOutline(sh))
		if fill, ok := e.sceneColor(sh.FillRole, sh.FillOverride); ok {
			gc.DrawPath(path, brush(fill))
		}
		if hasStroke {
			p := pen(stroke, float32(sh.StrokeWidth), diagram.LineSolid, false)
			gc.DrawPath(path, p)
			if detail := diagram.ShapeDetail(sh); !detail.Empty() {
				gc.DrawPath(toPath(detail), p)
			}
		}
	}

	for _, p := range dividers {
		e.drawScenePath(gc, p)
	}

	// Text: node labels and edge labels.
	for _, tx := range s.Texts {
		if tx.Text == "" {
			continue
		}
		r := rect(tx.Rect)
		if tx.HasBackground {
			gc.DrawPath(toPath(diagram.LabelBackdrop(tx)), brush(kvitui.Color(t.PanelBackground)))
		}
		col, _ := e.roleColor(tx.Role)
		col = labelOnFill(s, tx, col)
		// A formula is typeset into the box layout sized from it; when it
		// stopped typesetting since, its source is drawn instead.
		if tx.Tex != "" && e.DiagramMath != nil {
			if size, ok := e.DiagramMath.Size(tx.Tex, float32(tx.FontSize)); ok {
				if e.DiagramMath.Draw(gc, tx.Tex, float32(tx.FontSize), pt(diagram.MathOrigin(tx, size)), kvitui.Color(col)) {
					continue
				}
			}
		}
		st := text.Style{Family: family, Size: float32(tx.FontSize), Weight: text.Regular, Italic: tx.Italic, Color: colour(col)}
		if tx.Bold {
			st.Weight = text.Bold
		}
		opt := text.Options{MaxWidth: max(1, r.Width), Align: text.AlignMiddle}
		if tx.Align == diagram.AlignLeft {
			opt.Align = text.AlignStart
		}
		l := e.ui.Fonts.Layout([]text.Span{{Text: tx.Text, Style: st}}, opt)
		_, h := l.Size()
		l.Draw(gc, r.X, r.Y+(r.Height-h)/2)
	}

	if c == nil {
		return
	}
	// The element the source's caret is in, then the selection, as rings.
	ring := func(node string, edge int, width, alpha float32) {
		col := kvitui.Color(t.FocusRing).SetAlphaIntensity(alpha)
		if node != "" {
			for _, sh := range s.Shapes {
				if sh.NodeID == node {
					r := rect(sh.Rect).Inset(geom.NewUniformInsets(-3))
					p := pen(col, width, diagram.LineSolid, false)
					gc.DrawRoundedRect(r, geom.NewSize(5, 5), p)
				}
			}
			return
		}
		for _, p := range s.Paths {
			if edge >= 0 && p.EdgeIndex == edge {
				gc.DrawPath(toPath(p.Outline), pen(col, width, diagram.LineSolid, true))
			}
		}
	}
	if c.hlNode != "" && c.hlNode != c.selNode {
		ring(c.hlNode, -1, 1.6, 130.0/255)
	}
	if c.hlEdge >= 0 && c.hlEdge != c.selEdge {
		ring("", c.hlEdge, 4, 90.0/255)
	}
	if c.selNode != "" {
		ring(c.selNode, -1, 2.2, 1)
	}
	if c.selEdge >= 0 {
		ring("", c.selEdge, 4.5, 150.0/255)
	}
}

// drawScenePath strokes one of a scene's lines with its markers.
func (e *Editor) drawScenePath(gc *unison.Canvas, p diagram.Path) {
	col, _ := e.sceneColor(p.StrokeRole, mermaid.Color{})
	gc.DrawPath(toPath(p.Outline), pen(col, float32(p.StrokeWidth), p.Style, true))
	e.drawEndMarker(gc, p.EndMarker, p.EndPoint, p.EndDir, col)
	e.drawEndMarker(gc, p.StartMarker, p.StartPoint, p.StartDir, col)
}

// isDivider reports a line drawn inside a box to divide it: in the node
// stroke's colour, not selectable, and with no markers.
func isDivider(p diagram.Path) bool {
	return p.StrokeRole == diagram.RoleNodeStroke && p.EdgeIndex < 0 &&
		p.StartMarker == diagram.MarkerNone && p.EndMarker == diagram.MarkerNone
}

// groupTitleColor is the colour of a subgraph's or a composite state's title:
// the border darkened on a light theme, and the muted text colour on a dark
// one, where the darkened border would be dark on dark.
func (e *Editor) groupTitleColor() palette.Color {
	t := e.tok()
	if palette.RelativeLuminance(t.WindowBackground) < 0.5 {
		return t.TextMuted
	}
	return t.Border.Darker(1.4)
}

// labelOnFill is the colour a label inside a box is drawn in: col, unless
// the diagram's own style filled the box with a colour col reads poorly on,
// such as a light fill on a dark theme; then the theme's colour for text on
// that fill.
func labelOnFill(s *diagram.Scene, tx diagram.Text, col palette.Color) palette.Color {
	if tx.Role != diagram.RoleLabel {
		return col
	}
	c := tx.Rect.Center()
	for k := len(s.Shapes) - 1; k >= 0; k-- {
		sh := &s.Shapes[k]
		if !sh.Rect.Contains(c) {
			continue
		}
		if !sh.FillOverride.Set {
			return col
		}
		fill := palette.RGB8(sh.FillOverride.R, sh.FillOverride.G, sh.FillOverride.B)
		if palette.ContrastRatio(col, fill) < 4.5 {
			return tokens.LabelOn(fill)
		}
		return col
	}
	return col
}

// drawEndMarker draws a marker at the end of a line, filled or stroked in the
// line's colour.
func (e *Editor) drawEndMarker(gc *unison.Canvas, kind diagram.Marker, tip, dir diagram.Point, c unison.Color) {
	m := diagram.MarkerOutline(kind, tip, dir)
	if m.Outline.Empty() {
		return
	}
	path := toPath(m.Outline)
	if m.Filled {
		gc.DrawPath(path, brush(c))
	}
	if m.StrokeWidth > 0 {
		gc.DrawPath(path, pen(c, float32(m.StrokeWidth), diagram.LineSolid, m.RoundCap))
	}
}

// drawCanvas draws a canvas's scene at a scale with its top left at origin,
// cut to a window.
func (e *Editor) drawCanvas(gc *unison.Canvas, c *diagramCanvas, window geom.Rect, origin geom.Point, scale float32, over func()) {
	gc.Save()
	defer gc.Restore()
	gc.ClipRect(window, pathop.Intersect, true)
	gc.Translate(origin)
	gc.Scale(geom.NewPoint(scale, scale))
	e.paintScene(gc, &c.scene, c)
	if over != nil {
		over()
	}
}

// drawDiagram draws diagram block i as its drawing: the panel, the diagram
// or why there is none, the zoom level, and the controls while hovered.
func (e *Editor) drawDiagram(gc *unison.Canvas, i int) {
	b := &e.Doc.Blocks[i]
	v := e.diagramFor(b)
	c := v.read
	t := e.tok()
	p := e.diagramReadParts(i)
	r := e.px(diagramRadius)
	e.fillRound(gc, p.panel, r, t.PanelBackground)
	e.stroke(gc, p.panel, r, e.px(1), t.Border)
	if c.hasScene {
		e.clampPan(i)
		e.drawCanvas(gc, c, p.window, p.window.Point.Sub(v.pan), p.scale, func() { e.drawDiagramGestures(gc, i) })
		e.drawDiagramAnchors(gc, i)
	}
	if p.note != nil {
		p.note.Draw(gc, p.noteAt.X, p.noteAt.Y)
	}
	if c.hasScene && !e.Printing {
		// The scale on screen, which also says what Fit chose.
		zl := e.label(zoomText(p.scale), e.chrome(roleCaption, text.Regular, t.TextFaint))
		w, h := zl.Size()
		w += 2 * e.px(diagramZoomPad)
		m := e.px(diagramZoomMargin)
		box := geom.NewRect(p.panel.Right()-m-w, p.panel.Bottom()-m-e.px(diagramZoomH), w, e.px(diagramZoomH))
		e.fillRound(gc, box, e.px(diagramChipRadius), t.ChipBackground)
		e.stroke(gc, box, e.px(diagramChipRadius), e.px(1), t.Border)
		zl.Draw(gc, box.X+(box.Width-w)/2+e.px(diagramZoomPad), box.Y+(box.Height-h)/2)
	}
	if e.hover == b.ID && !e.Printing && e.drag == nil && e.msel == nil {
		chips, word := e.diagramChips(i)
		st := e.chrome(roleCaption, text.Regular, t.TextSecondary)
		wl := e.label("Mermaid", e.chrome(roleCaption, text.Regular, t.TextFaint))
		_, wh := wl.Size()
		wl.Draw(gc, word.X, word.Y+(e.px(diagramChipH)-wh)/2)
		for _, ch := range chips {
			ground := t.ChipBackground
			switch {
			case e.part == ch.part:
				ground = t.HoverTint
			case ch.active:
				ground = t.SelectionTint
			}
			rr := e.px(diagramChipRadius)
			e.fillRound(gc, ch.rect, rr, ground)
			e.stroke(gc, ch.rect, rr, e.px(1), t.Border)
			l := e.label(ch.label, st)
			w, h := l.Size()
			l.Draw(gc, ch.rect.X+(ch.rect.Width-w)/2, ch.rect.Y+(ch.rect.Height-h)/2)
		}
	}
}

// drawDiagramPreview draws the preview under diagram block i's source, its
// notes, and the key that leaves the block.
func (e *Editor) drawDiagramPreview(gc *unison.Canvas, i int) {
	if !e.diagramEditing(i) {
		return
	}
	v := e.diagramFor(&e.Doc.Blocks[i])
	c := v.preview
	t := e.tok()
	p := e.diagramPreviewParts(i)
	r := e.px(diagramPreviewRad)
	e.fillRound(gc, p.panel, r, t.PanelBackground)
	e.stroke(gc, p.panel, r, e.px(1), t.Border)
	switch {
	case c.hasScene:
		e.drawCanvas(gc, c, p.window, p.window.Point, p.scale, nil)
	case !c.hasError:
		word := "Preview"
		if c.rendering {
			word = "Rendering…"
		}
		l := e.label(word, e.chrome(roleBody, text.Regular, t.TextFaint))
		w, h := l.Size()
		l.Draw(gc, p.panel.X+(p.panel.Width-w)/2, p.panel.Y+(p.panel.Height-h)/2)
	}
	y := p.notesAt.Y
	for _, n := range p.notes {
		n.Draw(gc, p.notesAt.X, y)
		_, h := n.Size()
		y += h + e.px(1)
	}
	p.hint.Draw(gc, p.hintAt.X, p.hintAt.Y)
}

// zoomText is the zoom level a scale is shown as, such as "85%".
func zoomText(scale float32) string { return fmt.Sprintf("%d%%", int(math.Round(float64(scale)*100))) }

// The type roles the diagram's chrome is set in.
const (
	roleBody    = kvitui.RoleBody
	roleCaption = kvitui.RoleCaption
	roleSmall   = kvitui.RoleSmall
	roleStrong  = kvitui.RoleStrong
)
