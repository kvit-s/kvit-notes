package editor

// Display equations (features.md 1.2.15, Kvit's qml/MathBlock): a
// "$$ … $$" fence is a Math block holding the TeX between the fences. Away
// from the caret it shows the typeset equation, centred, in the text colour,
// with its number at the right when View, Equation numbers is on; blank it
// says so, and TeX that does not typeset shows its source and the error,
// never nothing. With the caret in it, its TeX is edited in a panel, the
// equation typeset from it in a second panel underneath as it is typed, and
// the key that leaves the block named under that. When the math library is
// not there, the block is its source in a code panel, as before math was
// typeset.

import (
	"math"
	"strconv"
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"

	"github.com/kvit-s/kvit-notes/mathtex"
)

// parseMathFence reads a display equation starting at line i, as Kvit's
// DocumentSerializer::parse reads one: a line that is only "$$" opens a
// fence that the next line that is only "$$" closes (or, as an older Kvit
// wrote it, "$$" with the attribute tag after it), and a line "$$x$$" is an
// equation on one line, which is written back as a fence. The block's text
// is the TeX between; the opening line's tag is the block's. next is the
// line after the equation, and ok false when line i does not start one.
func parseMathFence(lines []string, i int, line, attrs string) (b Block, next int, ok bool) {
	t := strings.TrimSpace(line)
	single := len(t) > 4 && strings.HasPrefix(t, "$$") && strings.HasSuffix(t, "$$")
	if t != "$$" && !single {
		return Block{}, i, false
	}
	b = NewBlock(Math, "")
	b.Attrs = attrs
	if single {
		b.Text = strings.TrimSpace(t[2 : len(t)-2])
		return b, i + 1, true
	}
	var tex []string
	j := i + 1
	for ; j < len(lines); j++ {
		if strings.TrimSpace(lines[j]) == "$$" {
			j++
			break
		}
		if bare, legacy := stripTag(lines[j]); legacy != "" && strings.TrimSpace(bare) == "$$" {
			if b.Attrs == "" {
				b.Attrs = legacy
			}
			j++
			break
		}
		tex = append(tex, lines[j])
	}
	b.Text = strings.Join(tex, "\n")
	return b, j, true
}

// The display block in design pixels (MathBlock): its content starts 8
// below the row's top and ends 8 above its bottom; the typeset equation takes
// at least 24; the source panel pads its text 10 at the side and 6 above and
// below; its parts are 6 apart; the preview panel is 12 taller than what it
// shows, and at least 28 plus that.
const (
	mathRowTop      = 8
	mathRowBottom   = 8
	mathMinRendered = 24
	mathSourcePadX  = 10
	mathSourcePadY  = 6
	mathSpacing     = 6
	mathPreviewPad  = 12
	mathPreviewMin  = 28
	mathErrorGap    = 2 // between an error's source and its message
	mathMessageSide = 24
	mathPanelRadius = 4
)

// mathOn reports whether block i is an equation the math library typesets.
func (e *Editor) mathOn(i int) bool {
	return e.Doc.Blocks[i].Kind == Math && mathtex.Available()
}

// mathEditing reports whether equation i shows its TeX: it holds the caret.
func (e *Editor) mathEditing(i int) bool {
	d := e.Doc
	return d.Focused && d.Caret.Block == d.Blocks[i].ID
}

// mathReads reports whether equation i shows only its typeset form.
func (e *Editor) mathReads(i int) bool { return e.mathOn(i) && !e.mathEditing(i) }

// displayMathSize is the size display equations are set at: the note's
// body size, matched to its text face's x-height (MathBlock,
// mathPixelSize), so the letters stay the size of the prose around them and
// display style supplies the large operators.
func (e *Editor) displayMathSize() int {
	ty := e.ui.Typography
	size := ty.BodySize()
	body := text.Style{Family: ty.FontFamily(), Size: float32(size), Weight: text.Regular, Color: colour(e.tok().TextPrimary)}
	return mathtex.OpticalMathSize(size, mathtex.TextXHeight(e.ui.Fonts, body))
}

// mathPadding is the room kept above and below a typeset equation, as
// Kvit's picture of one has it.
func mathPadding(size int) float32 {
	return float32(max(2, (size*12+99)/100))
}

// mathColumn is where display block i's parts are laid out, for its row at
// top: across the code panel's width, from the row's top padding down. The
// row's own top is given rather than read, because a row's height is worked
// out before its top is known.
func (e *Editor) mathColumn(top float32) geom.Rect {
	left := e.bodyLeft() + e.px(codeInset)
	return geom.NewRect(left, top+e.px(mathRowTop), e.bodyRight()-e.px(contentRight)-left, 0)
}

// mathShown is what display block i shows of a TeX source: the formula, or
// the error, or nothing for blank TeX.
type mathShown struct {
	formula *mathtex.Formula
	err     string
	blank   bool
}

func (e *Editor) mathShow(tex string) mathShown {
	if strings.TrimSpace(tex) == "" {
		return mathShown{blank: true}
	}
	f, err := mathtex.Render(tex, e.displayMathSize(), true)
	if err != nil {
		return mathShown{err: err.Error()}
	}
	return mathShown{formula: f}
}

// formulaHeight is a typeset equation's height with its padding.
func (e *Editor) formulaHeight(f *mathtex.Formula) float32 {
	return float32(f.Height) + 2*mathPadding(e.displayMathSize())
}

// centred is text drawn centred across a box: laid out as wide as the
// box, each line centred in it.
type centred struct{ l *text.Layout }

// centre lays s out in st centred across width.
func (e *Editor) centre(s string, st text.Style, width float32) centred {
	return centred{e.ui.Fonts.Layout([]text.Span{{Text: s, Style: st}},
		text.Options{MaxWidth: max(1, width), Align: text.AlignMiddle})}
}

func (c centred) height() float32 {
	_, h := c.l.Size()
	return h
}

// mathReadLayouts are the texts of a display block's typeset view that are
// not the formula, laid out across its column: the blank block's note, or
// the source and message of an error.
func (e *Editor) mathReadLayouts(i int, s mathShown) []centred {
	t := e.tok()
	width := e.mathColumn(0).Width
	switch {
	case s.blank:
		st := e.chrome(roleStrong, text.Regular, t.TextFaint)
		st.Italic = true
		return []centred{e.centre("Empty equation — click to edit", st, width)}
	case s.formula == nil:
		src := e.chrome(roleStrong, text.Regular, t.TextPrimary)
		src.Family = e.monoFamily()
		return []centred{e.centre(e.Doc.Blocks[i].Text, src, width),
			e.centre("⚠ "+s.err, e.chrome(roleSmall, text.Regular, t.Danger), width)}
	}
	return nil
}

// mathReadHeight is the height of what display block i shows typeset.
func (e *Editor) mathReadHeight(i int) float32 {
	s := e.mathShow(e.Doc.Blocks[i].Text)
	h := e.px(mathMinRendered)
	if s.formula != nil {
		h = max(h, e.formulaHeight(s.formula))
	}
	var texts float32
	for k, c := range e.mathReadLayouts(i, s) {
		texts += c.height()
		if k > 0 {
			texts += e.px(mathErrorGap)
		}
	}
	return max(h, texts)
}

// mathSourcePanel is the panel display block i's TeX is edited in, for its
// row at top.
func (e *Editor) mathSourcePanel(i int, top float32) geom.Rect {
	c := e.mathColumn(top)
	c.Height = e.layout(i).height() + 2*e.px(mathSourcePadY)
	return c
}

// mathPreview is the preview panel under display block i's source, and the
// message it shows instead of a formula, laid out across the panel less its
// side margins.
func (e *Editor) mathPreview(i int, top float32) (geom.Rect, mathShown, *centred) {
	src := e.mathSourcePanel(i, top)
	s := e.mathShow(e.Doc.Blocks[i].Text)
	t := e.tok()
	var msg *centred
	h := e.px(mathPreviewMin)
	width := src.Width - e.px(mathMessageSide)
	switch {
	case s.blank:
		c := e.centre("Preview", e.chrome(roleBody, text.Regular, t.TextFaint), width)
		msg = &c
	case s.formula != nil:
		h = max(h, e.formulaHeight(s.formula))
	default:
		c := e.centre("⚠ "+s.err, e.chrome(roleBody, text.Regular, t.Danger), width)
		msg = &c
		h = max(h, c.height())
	}
	return geom.NewRect(src.X, src.Bottom()+e.px(mathSpacing), src.Width, h+e.px(mathPreviewPad)), s, msg
}

// mathHint names the key that leaves display block i, in the corner under
// its preview, as the code and diagram blocks name it.
func (e *Editor) mathHint(i int, top float32) (*text.Layout, geom.Point) {
	p, _, _ := e.mathPreview(i, top)
	hintSize := max(9, e.ui.Typography.MonoSize()-4)
	l := e.ui.Fonts.Layout([]text.Span{{Text: "Ctrl+Enter: new block",
		Style: text.Style{Family: e.ui.Typography.FontFamily(), Size: float32(hintSize), Color: colour(e.tok().TextFaint)}}}, text.Options{})
	w, _ := l.Size()
	return l, geom.NewPoint(p.Right()-w, p.Bottom()+e.px(mathSpacing))
}

// mathHeight is display block i's row height, and false for a block that
// is not one the library typesets.
func (e *Editor) mathHeight(i int) (float32, bool) {
	if !e.mathOn(i) {
		return 0, false
	}
	if !e.mathEditing(i) {
		return e.px(mathRowTop+mathRowBottom) + e.mathReadHeight(i), true
	}
	l, at := e.mathHint(i, 0)
	_, h := l.Size()
	return at.Y + h + e.px(mathRowBottom), true
}

// mathTextTop is how far an edited equation's TeX starts below its row's
// top, and false when the block is not one.
func (e *Editor) mathTextTop(b *Block) (float32, bool) {
	if b.Kind != Math || !mathtex.Available() {
		return 0, false
	}
	return e.px(mathRowTop + mathSourcePadY), true
}

// mathTextLeft is how far an equation's TeX starts from the body's left
// edge, and false when the block is not one.
func (e *Editor) mathTextLeft(b *Block) (float32, bool) {
	if b.Kind != Math || !mathtex.Available() {
		return 0, false
	}
	return e.px(codeInset + mathSourcePadX), true
}

// mathNumber is display block i's equation number: how many display
// equations there are up to it (Kvit's BlockModel::mathNumber).
func (e *Editor) mathNumber(i int) int {
	n := 0
	for k := 0; k <= i && k < len(e.Doc.Blocks); k++ {
		if e.Doc.Blocks[k].Kind == Math {
			n++
		}
	}
	return n
}

// drawMath draws display block i and reports true when it drew all of it,
// its TeX included. Without the math library it draws the code panel the
// TeX is shown in and leaves the text to the caller.
func (e *Editor) drawMath(gc *unison.Canvas, i int) bool {
	if !e.mathOn(i) {
		e.drawCodePanel(gc, i)
		return false
	}
	if e.mathEditing(i) {
		e.drawMathEditing(gc, i)
		return true
	}
	e.drawMathRead(gc, i)
	return true
}

// drawFormula draws a typeset equation centred across the box c, from its
// top y, in fg.
func (e *Editor) drawFormula(gc *unison.Canvas, f *mathtex.Formula, c geom.Rect, y float32, fg unison.Color) {
	at := snapToDevice(gc, geom.NewPoint(c.X+(c.Width-float32(f.Width))/2, y+mathPadding(e.displayMathSize())))
	f.Draw(gc, at.X, at.Y, fg)
}

// snapToDevice moves a point to the nearest whole device pixel under the
// canvas's transform, as Kvit placed its pictures of equations: a formula
// then draws the same wherever its row lands, and its rules stay sharp.
func snapToDevice(gc *unison.Canvas, p geom.Point) geom.Point {
	m := gc.Matrix()
	if m.SkewX != 0 || m.SkewY != 0 || m.ScaleX <= 0 || m.ScaleY <= 0 {
		return p
	}
	x := float32(math.Round(float64(m.ScaleX*p.X + m.TransX)))
	y := float32(math.Round(float64(m.ScaleY*p.Y + m.TransY)))
	return geom.NewPoint((x-m.TransX)/m.ScaleX, (y-m.TransY)/m.ScaleY)
}

func (e *Editor) drawMathRead(gc *unison.Canvas, i int) {
	t := e.tok()
	c := e.mathColumn(e.tops[i])
	h := e.mathReadHeight(i)
	c.Height = h
	s := e.mathShow(e.Doc.Blocks[i].Text)
	if s.formula != nil {
		fh := e.formulaHeight(s.formula)
		e.drawFormula(gc, s.formula, c, c.Y+(h-fh)/2, kvitui.Color(t.TextPrimary))
	} else {
		ls := e.mathReadLayouts(i, s)
		var total float32
		for k, l := range ls {
			total += l.height()
			if k > 0 {
				total += e.px(mathErrorGap)
			}
		}
		y := c.Y + (h-total)/2
		for _, l := range ls {
			l.l.Draw(gc, c.X, y)
			y += l.height() + e.px(mathErrorGap)
		}
	}
	if e.EquationNumbers {
		n := e.label("("+strconv.Itoa(e.mathNumber(i))+")", e.chrome(roleStrong, text.Regular, t.TextMuted))
		w, nh := n.Size()
		n.Draw(gc, c.Right()-w, c.Y+(h-nh)/2)
	}
}

func (e *Editor) drawMathEditing(gc *unison.Canvas, i int) {
	t := e.tok()
	r := e.px(mathPanelRadius)
	src := e.mathSourcePanel(i, e.tops[i])
	e.fillRound(gc, src, r, t.CodePanelBackground)
	e.stroke(gc, src, r, e.px(1), t.Border)
	l := e.layout(i)
	o := e.textOrigin(i)
	l.text.Draw(gc, o.X, o.Y)

	p, s, msg := e.mathPreview(i, e.tops[i])
	e.fillRound(gc, p, r, t.PanelBackground)
	e.stroke(gc, p, r, e.px(1), t.Border)
	if s.formula != nil {
		fh := e.formulaHeight(s.formula)
		e.drawFormula(gc, s.formula, p, p.Y+(p.Height-fh)/2, kvitui.Color(t.TextPrimary))
	} else if msg != nil {
		msg.l.Draw(gc, p.X+e.px(mathMessageSide)/2, p.Y+(p.Height-msg.height())/2)
	}
	hint, at := e.mathHint(i, e.tops[i])
	hint.Draw(gc, at.X, at.Y)
}
