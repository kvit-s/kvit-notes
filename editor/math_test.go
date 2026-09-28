package editor

// Math in the editor (features.md 1.2.15): display equations (mathblock.go),
// inline $…$ spans (mathinline.go) and diagram labels (mathdiagram.go). The
// Qt app's own checks of these are storyboards (tests/tst_visual.qml,
// test_39_math, test_49_inline_math, test_62_math_canary,
// test_36b_table_math_and_column_widths), replayed with their pictures by
// cmd/kvit-notes/scenarios_math.go; these check the geometry the pictures
// show. They need the math library build.sh builds, and are skipped without
// it.

import (
	"math"
	"strings"
	"testing"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"

	"github.com/kvit-s/kvit-notes/mathtex"
)

func needMath(t *testing.T) {
	t.Helper()
	if !mathtex.Available() {
		t.Skip(mathtex.LoadError())
	}
}

// mathEditor shows an editor on a headless screen with the caret nowhere.
func mathEditor(t *testing.T, md string) (*uitest.Session, *Editor) {
	t.Helper()
	var e *Editor
	s := uitest.Open(t, uitest.Options{Width: 800, Height: 700}, func(ui *kvitui.UI) unison.Paneler {
		e = New(ui, NewDoc(ParseMarkdown(md)))
		return e
	})
	return s, e
}

// Kvit's DocumentSerializer: a "$$" line opens an equation the next "$$"
// line closes, a "$$x$$" line is an equation on its own, an older Kvit's
// tagged closer still closes, the opening line's tag is the block's, and an
// equation is written back as a fence with the tag on its opening line.
func TestMathFencesAreEquations(t *testing.T) {
	cases := []struct{ md, tex, attrs, back string }{
		{"$$\nE = mc^2\n$$", "E = mc^2", "", "$$\nE = mc^2\n$$"},
		{"$$\n\\begin{aligned}\na &= b\\\\\nc &= d\n\\end{aligned}\n$$", "\\begin{aligned}\na &= b\\\\\nc &= d\n\\end{aligned}", "",
			"$$\n\\begin{aligned}\na &= b\\\\\nc &= d\n\\end{aligned}\n$$"},
		{"$$ x^2 $$", "x^2", "", "$$\nx^2\n$$"},
		{"$$  <!--kvit align=center-->\nx\n$$", "x", "align=center", "$$  <!--kvit align=center-->\nx\n$$"},
		{"$$\nx\n$$  <!--kvit align=right-->", "x", "align=right", "$$  <!--kvit align=right-->\nx\n$$"},
		{"$$\n\n$$", "", "", "$$\n\n$$"},
	}
	for _, c := range cases {
		bs := ParseMarkdown(c.md)
		if len(bs) != 1 || bs[0].Kind != Math || bs[0].Text != c.tex || bs[0].Attrs != c.attrs {
			t.Errorf("%q parsed as %+v", c.md, bs)
			continue
		}
		if got := strings.TrimSuffix(Serialize(bs), "\n"); got != c.back {
			t.Errorf("%q wrote back as %q, want %q", c.md, got, c.back)
		}
	}
	// "$$" with more on its line is not a fence.
	if bs := ParseMarkdown("$$x and more"); len(bs) != 1 || bs[0].Kind != Paragraph {
		t.Errorf("an unclosed $$ line parsed as %+v", bs)
	}
	// An equation is a block of its own between paragraphs.
	bs := ParseMarkdown("before\n\n$$\nx\n$$\n\nafter")
	if len(bs) != 3 || bs[1].Kind != Math || bs[2].Text != "after" {
		t.Errorf("parsed as %+v", bs)
	}
}

// Away from the caret an equation shows only its typeset form, centred in
// a row as tall as it and its padding (MathBlock.qml); with the caret in it,
// its TeX in a panel, the typeset preview under that, and the exit key
// under the preview.
func TestDisplayEquationShowsTypesetUntilEdited(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "# Equations\n\n$$\n\\int_0^1 x^2\\,dx = \\frac{1}{3}\n$$\n\nafter")
	s.Do(func() {
		if !e.mathReads(1) {
			t.Fatal("the equation should show typeset")
		}
		f, err := mathtex.Render(e.Doc.Blocks[1].Text, e.displayMathSize(), true)
		if err != nil {
			t.Fatal(err)
		}
		want := e.px(mathRowTop+mathRowBottom) + max(e.px(mathMinRendered), float32(f.Height)+2*mathPadding(e.displayMathSize()))
		if h := e.rowHeight(1); math.Abs(float64(h-want)) > 0.01 {
			t.Errorf("the row is %v tall, want %v", h, want)
		}
		read := e.rowHeight(1)
		e.FocusBlock(1, 3)
		if e.mathReads(1) || !e.mathEditing(1) {
			t.Fatal("with the caret in it the equation should show its TeX")
		}
		src := e.mathSourcePanel(1, e.tops[1])
		prev, shown, _ := e.mathPreview(1, e.tops[1])
		if shown.formula == nil || prev.Y < src.Bottom() || e.rowHeight(1) <= read {
			t.Errorf("source %v, preview %v, formula %v", src, prev, shown.formula != nil)
		}
		o := e.textOrigin(1)
		if !geom.NewPoint(o.X, o.Y+1).In(src) {
			t.Errorf("the TeX at %v is outside its panel %v", o, src)
		}
	})
	// Enter is a line break in the TeX; Ctrl+Enter leaves the block.
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Do(func() {
		if b := e.Doc.Blocks[1]; b.Kind != Math || !strings.Contains(b.Text, "\n") {
			t.Errorf("Enter should break the TeX's line: %+v", b)
		}
	})
	s.Screen.KeyPress(unison.KeyReturn, mod.Control)
	s.Do(func() {
		if len(e.Doc.Blocks) != 4 || e.Doc.Caret.Block != e.Doc.Blocks[2].ID || e.Doc.Blocks[2].Kind != Paragraph {
			t.Errorf("Ctrl+Enter should start a paragraph under the equation: caret %v in %d blocks", e.Doc.Caret, len(e.Doc.Blocks))
		}
		if !e.mathReads(1) {
			t.Error("left, the equation should show typeset again")
		}
	})
}

// A press on a typeset equation opens its TeX with the caret at the end.
func TestPressOnEquationOpensItsTeX(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "# Equations\n\n$$\nE = mc^2\n$$\n\nafter")
	var p geom.Point
	s.Do(func() {
		r := e.RowRect(1)
		p = s.Screen.PanelPoint(e, geom.NewPoint(r.X+r.Width/2, r.Y+r.Height/2))
	})
	s.Screen.Click(p)
	s.Do(func() {
		if c := e.Doc.Caret; c.Block != e.Doc.Blocks[1].ID || c.Off != len("E = mc^2") {
			t.Errorf("the caret is at %v", c)
		}
	})
}

// Blank TeX says so, and TeX that does not typeset shows its source and the
// error, never nothing.
func TestEquationErrorsShowSourceAndMessage(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "$$\na & b\n$$\n\n$$\n\n$$")
	s.Do(func() {
		bad := e.mathShow(e.Doc.Blocks[0].Text)
		ls := e.mathReadLayouts(0, bad)
		if bad.formula != nil || len(ls) != 2 || !strings.Contains(ls[0].l.Text(), "a & b") ||
			!strings.Contains(ls[1].l.Text(), "array mode") {
			t.Errorf("an error shows %d texts", len(ls))
		}
		blank := e.mathShow(e.Doc.Blocks[1].Text)
		if ls := e.mathReadLayouts(1, blank); !blank.blank || len(ls) != 1 || !strings.Contains(ls[0].l.Text(), "Empty equation") {
			t.Error("a blank equation should say it is empty")
		}
	})
}

// Equations are numbered by position, counting display equations only.
func TestEquationNumbersCountDisplayEquations(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "$$\na\n$$\n\ntext $b$\n\n$$\nc\n$$")
	s.Do(func() {
		if e.mathNumber(0) != 1 || e.mathNumber(2) != 2 {
			t.Errorf("numbers %d and %d", e.mathNumber(0), e.mathNumber(2))
		}
	})
}

// placeholderIn is the drawn offset of the first typeset span of block i,
// or -1.
func placeholderIn(e *Editor, i int) int {
	for d, r := range e.layout(i).proj.Disp {
		if r == mathPlaceholder {
			return d
		}
	}
	return -1
}

// Away from the caret a $…$ span is one character laid out as its
// formula's width, in text style; the caret touching it shows its source.
func TestInlineMathIsTypesetAwayFromTheCaret(t *testing.T) {
	needMath(t)
	const src = "The relation $E = mc^2$ ties mass to energy."
	s, e := mathEditor(t, src)
	s.Do(func() {
		l := e.layout(0)
		d := placeholderIn(e, 0)
		if d < 0 || strings.Contains(string(l.proj.Disp), "mc^2") {
			t.Fatalf("drawn %q", string(l.proj.Disp))
		}
		f, err := mathtex.Render("E = mc^2", e.inlineMathSize(l.style), false)
		if err != nil {
			t.Fatal(err)
		}
		x0, _, _ := l.text.CaretAt(d)
		x1, _, _ := l.text.CaretAt(d + 1)
		if math.Abs(float64(x1-x0)-f.Width) > 0.05 {
			t.Errorf("the box is %v wide, the formula %v", x1-x0, f.Width)
		}
		start := strings.Index(src, "$")
		end := start + len("$E = mc^2$")
		if got := l.proj.forClick(d); got != start {
			t.Errorf("a press before the formula puts the caret at %d, want %d", got, start)
		}
		if got := l.proj.forClick(d + 1); got != end {
			t.Errorf("a press after the formula puts the caret at %d, want %d", got, end)
		}
		if l.proj.afterPrev(d+1) != end || l.proj.beforeNext(d) != start {
			t.Error("the formula's character should stand for the whole span")
		}
		// The caret in the span shows its source, upright.
		e.FocusBlock(0, start+3)
		l = e.layout(0)
		if placeholderIn(e, 0) >= 0 || !strings.Contains(string(l.proj.Disp), "$E = mc^2$") {
			t.Errorf("with the caret in it the span should show its source: %q", string(l.proj.Disp))
		}
		if st := e.styleFor(fMath, l.style); st.Italic {
			t.Error("the source of a span should be upright while math typesets")
		}
	})
}

// A formula taller than the text grows its line the way Kvit's did, and
// sits on the line's baseline inside it; one that fits leaves the line at
// the pitch.
func TestTallInlineMathGrowsItsLine(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "Text before $x$ text after\n\nFraction inline $\\frac{a}{b}$ text after\n\nplain text")
	s.Do(func() {
		plain := e.layout(2)
		_, _, _, pitch := plain.text.LineBounds(0)
		_, _, _, small := e.layout(0).text.LineBounds(0)
		if small != pitch {
			t.Errorf("x made its line %v tall, the pitch is %v", small, pitch)
		}
		l := e.layout(1)
		_, _, top, tall := l.text.LineBounds(0)
		if tall <= pitch {
			t.Fatalf("the fraction's line is %v tall, no more than the pitch %v", tall, pitch)
		}
		bx := l.proj.boxes[placeholderIn(e, 1)]
		base := l.text.LineBaseline(0)
		if base-float32(bx.formula.Baseline) < top || base+float32(bx.formula.Height-bx.formula.Baseline) > top+tall {
			t.Errorf("the formula (%v above the baseline at %v, %v below) leaves its line %v to %v",
				bx.formula.Baseline, base, bx.formula.Height-bx.formula.Baseline, top, top+tall)
		}
		// Kvit's rule: the text's face at the smallest whole size holding
		// the formula, spaced by the document's line height.
		asc, desc := e.fontExtents(l.style)
		p := math.Round(float64(l.style.Size))
		for asc*float32(p) < float32(bx.formula.Baseline) || desc*float32(p) < float32(bx.formula.Height-bx.formula.Baseline) {
			p++
		}
		if want := float32(p) * float32(e.ui.Typography.LineHeight()); math.Abs(float64(tall-want)) > 1.5 {
			t.Errorf("the line is %v tall, Kvit's rule gives %v", tall, want)
		}
	})
}

// TeX that does not typeset stays its source.
func TestInvalidInlineMathStaysSource(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "bad $a & b$ here")
	s.Do(func() {
		if placeholderIn(e, 0) >= 0 || !strings.Contains(string(e.layout(0).proj.Disp), "a & b") {
			t.Errorf("drawn %q", string(e.layout(0).proj.Disp))
		}
	})
}

// Table cells and task-board card descriptions typeset their math as the
// prose blocks do.
func TestCellsAndCardsTypesetMath(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "| Quantity $q$ | Definition |\n| --- | --- |\n| Area | circle $\\pi r^2$ |")
	s.Do(func() {
		g, ok := e.gridFor(0)
		if !ok || len(g.projs) != 2 || len(g.projs[0][0].boxes) != 1 || len(g.projs[1][1].boxes) != 1 {
			t.Fatalf("the grid's cells should hold typeset math: %v", ok)
		}
		_, p := e.inlineText("the mean $\\frac{1}{n}\\sum x_i$ of **all**", e.blockStyle(&Block{}), 300)
		if len(p.boxes) != 1 || strings.Contains(string(p.Disp), "**") {
			t.Errorf("a card description draws %q", string(p.Disp))
		}
	})
}

// Printing draws the typeset forms: the ink of a page is where the
// equation and the inline formula are.
func TestMathPrints(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "$$\n\\sum_{i=1}^{n} i^2\n$$\n\nsee $\\frac{a}{b}$")
	s.Do(func() {
		pages := e.Paginate(600, 800)
		if len(pages) != 1 {
			t.Fatalf("%d pages", len(pages))
		}
		img, err := unison.NewImageFromDrawing(600, int(pages[0].To-pages[0].From)+1, 72, func(gc *unison.Canvas) {
			e.DrawPage(gc, pages[0])
		})
		if err != nil {
			t.Fatal(err)
		}
		px, err := img.ToNRGBA()
		if err != nil {
			t.Fatal(err)
		}
		inked := func(r geom.Rect) bool {
			for y := int(r.Y); y < int(r.Bottom()); y++ {
				for x := int(r.X); x < int(r.Right()); x++ {
					if px.NRGBAAt(x, y).A > 0 {
						return true
					}
				}
			}
			return false
		}
		eq := e.RowRect(0)
		mid := geom.NewRect(eq.X+eq.Width/2-10, eq.Y+eq.Height/2-5, 20, 10)
		if !inked(mid) {
			t.Error("the printed equation left no ink in the middle of its row")
		}
		l := e.layout(1)
		d := placeholderIn(e, 1)
		x0, _, _ := l.text.CaretAt(d)
		x1, _, _ := l.text.CaretAt(d + 1)
		o := e.textOrigin(1)
		if !inked(geom.NewRect(o.X+x0, o.Y, x1-x0, l.height())) {
			t.Error("the printed inline formula left no ink in its box")
		}
	})
}

// The diagram block's math labels are typeset by the editor unless the app
// set its own typesetter, at the optical size of the diagram's text.
func TestDiagramMathLabels(t *testing.T) {
	needMath(t)
	s, e := mathEditor(t, "x")
	s.Do(func() {
		m, ok := e.DiagramMath.(*diagramMath)
		if !ok {
			t.Fatalf("DiagramMath is %T", e.DiagramMath)
		}
		size, ok := m.Size(`\frac{a}{b}`, 14)
		f, err := mathtex.Render(`\frac{a}{b}`, m.size(14), true)
		if !ok || err != nil || size.W != f.Width || size.H != f.Height {
			t.Errorf("size %v %v, formula %v", size, ok, err)
		}
		if m.size(14) != e.displayMathSize() {
			t.Errorf("a label at the body size is set at %d, a display equation at %d", m.size(14), e.displayMathSize())
		}
		if _, ok := m.Size(`}`, 14); ok {
			t.Error("TeX that does not typeset should be drawn as its source")
		}
	})
}
