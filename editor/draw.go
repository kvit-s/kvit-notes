package editor

// Drawing the rows in view: each row's tint, the focus bar, the gutter, the
// kind-specific parts (list markers, the quote bar, the code panel, the
// divider) and the text, then the caret.

import (
	"github.com/kvit-s/kvit-notes/highlight"
	"strconv"
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/icons"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

func (e *Editor) fill(gc *unison.Canvas, r geom.Rect, c palette.Color) {
	gc.DrawRect(r, kvitui.Color(c).Paint(gc, r, paintstyle.Fill))
}

func (e *Editor) fillRound(gc *unison.Canvas, r geom.Rect, radius float32, c palette.Color) {
	gc.DrawRoundedRect(r, geom.NewSize(radius, radius), kvitui.Color(c).Paint(gc, r, paintstyle.Fill))
}

// stroke outlines a rounded rectangle whose outer edge is r.
func (e *Editor) stroke(gc *unison.Canvas, r geom.Rect, radius, width float32, c palette.Color) {
	p := kvitui.Color(c).Paint(gc, r, paintstyle.Stroke)
	p.SetStrokeWidth(width)
	inner := max(0, radius-width/2)
	gc.DrawRoundedRect(r.Inset(geom.NewUniformInsets(width/2)), geom.NewSize(inner, inner), p)
}

// label lays out a line of the editor's own chrome.
func (e *Editor) label(s string, st text.Style) *text.Layout {
	return e.ui.Fonts.Layout([]text.Span{{Text: s, Style: st}}, text.Options{})
}

func (e *Editor) draw(gc *unison.Canvas, dirty geom.Rect) {
	if w := e.ContentRect(false).Width; w > 0 {
		e.measureAt(w)
	}
	first, last := e.visibleRows(dirty)
	for i := first; i <= last; i++ {
		if e.drag != nil && e.drag.active && e.Doc.Blocks[i].ID == e.drag.id {
			// The row being dragged is drawn faded, gutter and all, where
			// it has got to, as Kvit draws it.
			gc.SaveWithOpacity(dragOpacity)
			e.drawGutter(gc, i)
			e.drawRow(gc, i)
			gc.Restore()
			continue
		}
		if e.Typewriter != nil && e.Doc.Focused && e.Doc.Blocks[i].ID != e.Doc.Caret.Block {
			// Typewriter mode fades every block but the caret's, as Kvit
			// does (EditableBlock.qml, typewriterDim).
			gc.SaveWithOpacity(typewriterOpacity)
			e.drawRow(gc, i)
			gc.Restore()
			continue
		}
		e.drawRow(gc, i)
	}
	if r, ok := e.caretRect(); ok && e.Focused() && !e.blinkOff {
		e.fill(gc, r, e.tok().TextPrimary)
	}
}

// dragOpacity is how strongly a row being dragged is drawn, and
// typewriterOpacity a block away from the caret in typewriter mode.
const (
	dragOpacity       = 0.35
	typewriterOpacity = 0.32
)

func (e *Editor) drawRow(gc *unison.Canvas, i int) {
	d := e.Doc
	b := &d.Blocks[i]
	t := e.tok()
	focused := d.Focused && d.Caret.Block == b.ID
	body := e.bodyRect(i)
	radius := e.px(4)
	switch {
	case e.blockSel[b.ID]:
		e.fillRound(gc, body, radius, t.BlockSelectionTint)
		e.stroke(gc, body, radius, e.px(1), t.Accent)
	case e.hover == b.ID && !focused && e.drag == nil && e.msel == nil:
		e.fillRound(gc, body, radius, t.BlockHoverTint)
	}
	if focused && !d.CrossBlock() {
		e.fill(gc, geom.NewRect(e.side()+e.px(gutterWidth), e.tops[i], e.px(focusBar), e.heights[i]), t.FocusRing)
	}
	if e.hover == b.ID && e.drag == nil && e.msel == nil && !d.ReadOnly {
		e.drawGutter(gc, i)
	}
	switch b.Kind {
	case Divider:
		e.drawDivider(gc, i)
		return
	case Code, Raw, Table:
		if g, ok := e.tableShowsGrid(i); ok {
			e.drawGrid(gc, i, g)
			return
		}
		if e.tocShows(i) {
			e.drawToc(gc, i)
			return
		}
		if e.boardShows(i) {
			e.drawBoard(gc, i)
			return
		}
		if e.queryShows(i) {
			e.drawQuery(gc, i)
			return
		}
		e.drawCodePanel(gc, i)
		e.drawLineNumbers(gc, i)
	case Callout:
		e.drawCallout(gc, i)
		if !e.calloutOpen(b) {
			return
		}
	case Quote:
		top := e.tops[i] + e.px(quoteBarTop)
		bar := geom.NewRect(body.X+e.markerLeft(b), top, e.px(quoteBarWidth), e.tops[i]+e.heights[i]-e.px(quoteBarBottom)-top)
		e.fillRound(gc, bar, e.px(quoteBarWidth)/2, t.QuoteBar)
	case Bullet, Numbered, Todo:
		e.drawMarker(gc, i)
	}
	l := e.layout(i)
	o := e.textOrigin(i)
	if _, ok, shows := e.pictureBlock(i); ok {
		if !shows {
			e.drawPicture(gc, i, o)
			return
		}
		l.text.Draw(gc, o.X, o.Y)
		e.drawPicture(gc, i, geom.NewPoint(o.X, o.Y+l.height()+e.px(pictureGap)))
		return
	}
	if b.Text == "" && b.Kind == Paragraph && e.Placeholder != "" {
		st := l.style
		st.Color = colour(t.TextDisabled)
		e.ui.Fonts.Layout([]text.Span{{Text: e.Placeholder, Style: st}}, text.Options{Pitch: l.pitch}).Draw(gc, o.X, o.Y)
	}
	l.text.Draw(gc, o.X, o.Y)
	e.drawDropCap(gc, i)
}

// drawDivider draws a divider in its style, thickness, colour and width
// (qml/DividerDelegate.qml): solid unless style says dashed, dotted or
// double, 2 px unless thickness says 1 to 12, the border colour unless
// color names one, the full width unless width gives a share.
func (e *Editor) drawDivider(gc *unison.Canvas, i int) {
	b := &e.Doc.Blocks[i]
	t := e.tok()
	body := e.bodyRect(i)
	c := t.Border
	if e.Doc.Focused && e.Doc.Caret.Block == b.ID {
		c = t.Accent
	}
	if v, ok := b.Attr("color"); ok {
		if p, err := palette.ParseHex(v); err == nil {
			c = p
		}
	}
	thick := float32(2)
	if v, ok := b.Attr("thickness"); ok {
		if n, err := strconv.Atoi(v); err == nil {
			thick = float32(min(12, max(1, n)))
		}
	}
	thick = e.px(thick)
	full := body.Width - e.px(listShift+contentRight)
	w := full
	if v, ok := b.Attr("width"); ok && v != "full" {
		if n, err := strconv.Atoi(strings.TrimSuffix(v, "%")); err == nil {
			w = full * float32(min(100, max(10, n))) / 100
		}
	}
	x := body.X + e.px(listShift) + (full-w)/2
	y := e.tops[i] + (e.heights[i]-thick)/2
	style, _ := b.Attr("style")
	switch style {
	case "dashed", "dotted":
		dash, gap := 3*thick, 2*thick
		if style == "dotted" {
			dash, gap = thick, thick
		}
		for px := x; px < x+w; px += dash + gap {
			e.fill(gc, geom.NewRect(px, y, min(dash, x+w-px), thick), c)
		}
	case "double":
		e.fill(gc, geom.NewRect(x, y-thick, w, thick), c)
		e.fill(gc, geom.NewRect(x, y+thick, w, thick), c)
	default:
		e.fill(gc, geom.NewRect(x, y, w, thick), c)
	}
}

// drawMarker draws a list item's bullet, number or check box.
func (e *Editor) drawMarker(gc *unison.Canvas, i int) {
	d := e.Doc
	b := &d.Blocks[i]
	t := e.tok()
	l := e.layout(i)
	x := e.bodyLeft() + e.markerLeft(b)
	y := e.textOrigin(i).Y
	st := l.style
	st.Color = colour(t.TextSecondary)
	st.Weight, st.Italic, st.Strike = text.Regular, false, false
	switch b.Kind {
	case Bullet:
		glyph := []string{"•", "◦", "▪"}[b.Indent%3]
		e.ui.Fonts.Layout([]text.Span{{Text: glyph, Style: st}}, text.Options{Pitch: l.pitch}).Draw(gc, x+e.px(bulletInset), y)
	case Numbered:
		n := strconv.Itoa(ListNumber(d.Blocks, i)) + "."
		e.ui.Fonts.Layout([]text.Span{{Text: n, Style: st}}, text.Options{Pitch: l.pitch}).Draw(gc, x+e.px(numberInset), y)
	case Todo:
		box := e.checkBox(i)
		r := e.px(checkBoxRadius)
		if b.Checked {
			e.fillRound(gc, box, r, t.Accent)
			if g, ok := icons.Glyph("check"); ok {
				mark := e.ui.Icon(int(e.px(checkGlyph)), t.OnAccent)
				mark.Weight = text.Bold
				gl := e.label(string(g), mark)
				w, h := gl.Size()
				gl.Draw(gc, box.X+(box.Width-w)/2, box.Y+(box.Height-h)/2)
			}
		} else {
			e.stroke(gc, box, r, e.px(checkBoxBorder), t.BorderStrong)
		}
	}
}

// Where a bullet and a number sit in their marker, and the check box's
// border and tick.
const (
	bulletInset    = 3
	numberInset    = 2
	checkBoxInset  = 3
	checkBoxBorder = 1.5
	checkGlyph     = 12
)

// checkBox is a to-do's check box.
func (e *Editor) checkBox(i int) geom.Rect {
	b := &e.Doc.Blocks[i]
	l := e.layout(i)
	s := e.px(checkBoxSize)
	y := e.textOrigin(i).Y + max(0, (l.pitch-s)/2-e.px(1))
	return geom.NewRect(e.bodyLeft()+e.markerLeft(b)+e.px(checkBoxInset), y, s, s)
}

// codePanel is a code block's panel.
func (e *Editor) codePanel(i int) geom.Rect {
	body := e.bodyRect(i)
	top := e.tops[i] + e.px(codeRowTop)
	return geom.NewRect(body.X+e.px(codeInset), top, body.Width-e.px(codeInset+contentRight),
		e.px(codeHeader+2*codeTextPad+codeFooter)+e.layout(i).height())
}

// copyButton is the code panel's Copy button, in its header.
func (e *Editor) copyButton(i int) geom.Rect {
	p := e.codePanel(i)
	l := e.label("Copy", e.chrome(kvitui.RoleCaption, text.Regular, e.tok().TextSecondary))
	w, h := l.Size()
	pad := e.px(codeCopyPad)
	return geom.NewRect(p.Right()-e.px(codePadSide)-w-2*pad, p.Y+(e.px(codeHeader)-h)/2-pad, w+2*pad, h+2*pad)
}

// codeCopyPad is the room around the Copy button's word that a press hits.
const codeCopyPad = 2

// lineNumberWidth is the room a code block's line numbers take before its
// text, 0 without them: two digits or as many as the last number has, and
// a gap.
func (e *Editor) lineNumberWidth(b *Block) float32 {
	if !e.LineNumbers || b.Kind != Code || isToc(b) {
		return 0
	}
	digits := len(strconv.Itoa(strings.Count(b.Text, "\n") + 1))
	w, _ := e.label("0", e.blockStyle(b)).Size()
	return float32(max(2, digits))*w + e.px(18)
}

// drawLineNumbers numbers each line of a code block where it starts.
func (e *Editor) drawLineNumbers(gc *unison.Canvas, i int) {
	b := &e.Doc.Blocks[i]
	if e.lineNumberWidth(b) == 0 {
		return
	}
	l := e.layout(i)
	o := e.textOrigin(i)
	x := e.codePanel(i).X + e.px(codePadSide)
	st := e.blockStyle(b)
	st.Color = colour(e.tok().TextFaint)
	off := 0
	for n, line := range strings.Split(b.Text, "\n") {
		_, top := l.caretAt(l.drawn(off))
		e.ui.Fonts.Layout([]text.Span{{Text: strconv.Itoa(n + 1), Style: st}}, text.Options{Pitch: l.pitch}).Draw(gc, x, o.Y+top)
		off += len([]rune(line)) + 1
	}
}

// languageLabel is what a code block's header says its language is.
func languageLabel(b *Block) string {
	switch {
	case b.Kind == Raw:
		return "Markdown kept as written"
	case b.Kind == Table:
		return "table"
	case b.Lang == "":
		return "plain text"
	}
	return b.Lang
}

// languageButton is the header's language label, which a press opens the
// language menu from.
func (e *Editor) languageButton(i int) geom.Rect {
	p := e.codePanel(i)
	l := e.label(languageLabel(&e.Doc.Blocks[i])+" ▾", e.chrome(kvitui.RoleCaption, text.Regular, e.tok().TextSecondary))
	w, h := l.Size()
	pad := e.px(codeCopyPad)
	return geom.NewRect(p.X+e.px(codePadSide)-pad, p.Y+(e.px(codeHeader)-h)/2-pad, w+2*pad, h+2*pad)
}

// languageItems are the language menu (LanguagePicker.qml): plain text,
// plain code, the two diagram kinds, then every language Kvit colours.
func (e *Editor) languageItems(id int64) []kvitui.MenuItem {
	b := e.Doc.Block(id)
	if b == nil {
		return nil
	}
	set := func(lang string) func() {
		return func() {
			if blk := e.Doc.Block(id); blk != nil && blk.Lang != lang {
				e.Doc.Edit("language", func() { blk.Lang = lang })
				e.changed()
			}
		}
	}
	item := func(name, lang string) kvitui.MenuItem {
		return kvitui.MenuItem{Text: kvitui.PlainMenuText(name), Checked: b.Lang == lang, OnSelect: set(lang)}
	}
	items := []kvitui.MenuItem{item("Plain text", ""), item("Plain code", "plain"), {Separator: true},
		item("Mermaid", "mermaid"), item("Text diagram", "diagram"), {Separator: true}}
	for _, l := range highlight.Languages() {
		items = append(items, item(l.Name, l.ID))
	}
	return items
}

func (e *Editor) drawCodePanel(gc *unison.Canvas, i int) {
	b := &e.Doc.Blocks[i]
	t := e.tok()
	p := e.codePanel(i)
	r := e.px(codePanelRadius)
	e.fillRound(gc, p, r, t.CodePanelBackground)
	e.stroke(gc, p, r, e.px(1), t.Border)
	hc := t.TextSecondary
	if e.hover == b.ID && e.part == partLanguage {
		hc = t.TextPrimary
	}
	head := e.label(languageLabel(b)+" ▾", e.chrome(kvitui.RoleCaption, text.Regular, hc))
	_, h := head.Size()
	y := p.Y + (e.px(codeHeader)-h)/2
	head.Draw(gc, p.X+e.px(codePadSide), y)
	if b.Kind == Code {
		c := t.TextSecondary
		if e.hover == b.ID && e.part == partCopy {
			c = t.TextPrimary
		}
		btn := e.copyButton(i)
		cp := e.label("Copy", e.chrome(kvitui.RoleCaption, text.Regular, c))
		cp.Draw(gc, btn.X+e.px(codeCopyPad), y)
	}
}

// gutterPart is one of the gutter's controls, or a part of a row that acts
// on a press.
type gutterPart int

const (
	partNone gutterPart = iota
	partAdd
	partHandle
	partDelete
	partMenu
	partCheck
	partCopy
	// A callout's header: its fold arrow, its type, its title and its
	// colour.
	partFold
	partCalloutType
	partCalloutTitle
	partCalloutColor
	partTocEntry // an entry of a table of contents
	// An embed card's title, which opens the page, and its Load preview
	// button.
	partEmbedOpen
	partEmbedLoad
	partLanguage // a code block's language, which opens the language menu
	partQueryRow // a query block's row or card
)

// gutterCellRect is one of the gutter's four controls for row i: the
// first column holds add over delete, the second the handle over the menu
// button.
func (e *Editor) gutterCellRect(i int, p gutterPart) geom.Rect {
	left := e.side() + (e.px(gutterWidth)-e.px(gutterButton+gutterColumnGap+gutterNarrow))/2
	x, w := left, e.px(gutterButton)
	if p == partHandle || p == partMenu {
		x, w = left+e.px(gutterButton+gutterColumnGap), e.px(gutterNarrow)
	}
	y := e.tops[i] + e.px(gutterTop)
	if p == partDelete || p == partMenu {
		y += e.px(gutterButton + gutterRowGap)
	}
	return geom.NewRect(x, y, w, e.px(gutterButton))
}

// gutterControls are the gutter's controls with what a screen reader calls
// them.
var gutterControls = []struct {
	part gutterPart
	name string
}{
	{partAdd, "Insert block below"},
	{partHandle, "Drag to move, click to select"},
	{partDelete, "Delete block"},
	{partMenu, "Block menu"},
}

func (e *Editor) drawGutter(gc *unison.Canvas, i int) {
	t := e.tok()
	radius := e.px(gutterRadius)
	for _, g := range []struct {
		part  gutterPart
		glyph string
		size  float32
	}{{partAdd, "+", gutterAddGlyph}, {partDelete, "×", gutterDelGlyph}} {
		r := e.gutterCellRect(i, g.part)
		c := t.TextMuted
		if e.part == g.part {
			ground := t.HoverTint
			c = t.TextPrimary
			if g.part == partDelete {
				ground, c = t.Danger, tokens.LabelOn(t.Danger)
			}
			e.fillRound(gc, r, radius, ground)
		}
		st := e.ui.Chrome(int(e.px(g.size)), text.Bold, c)
		l := e.label(g.glyph, st)
		w, h := l.Size()
		l.Draw(gc, r.X+(r.Width-w)/2, r.Y+(r.Height-h)/2)
	}
	// The handle: four dots.
	r := e.gutterCellRect(i, partHandle)
	if e.part == partHandle {
		e.fillRound(gc, r, radius, t.HoverTint)
	}
	dot, gap := e.px(handleDot), e.px(handleDotGap)
	x0, y0 := r.X+(r.Width-2*dot-gap)/2, r.Y+(r.Height-2*dot-gap)/2
	faint := kvitui.Color(t.TextFaint).SetAlphaIntensity(handleOpacity)
	for row := range 2 {
		for col := range 2 {
			d := geom.NewRect(x0+float32(col)*(dot+gap), y0+float32(row)*(dot+gap), dot, dot)
			gc.DrawRect(d, faint.Paint(gc, d, paintstyle.Fill))
		}
	}
	// The block-menu button: three bars.
	r = e.gutterCellRect(i, partMenu)
	c := t.TextMuted
	if e.part == partMenu {
		e.fillRound(gc, r, radius, t.HoverTint)
		c = t.TextPrimary
	}
	bw, bh, bg := e.px(menuBarWidth), e.px(menuBarHeight), e.px(menuBarGap)
	x, y := r.X+(r.Width-bw)/2, r.Y+(r.Height-3*bh-2*bg)/2
	for k := range 3 {
		e.fillRound(gc, geom.NewRect(x, y+float32(k)*(bh+bg), bw, bh), bh/2, c)
	}
}
