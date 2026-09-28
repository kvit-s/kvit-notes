// The port of the Qt app's tests/test_mathrenderer.cpp: one test for each of
// its test functions, in the same order, with the same formulas, sizes and
// expected numbers. A Qt test that checked the QImage MathRenderer::render
// made checks the image Image draws here, headless through unison's raster
// canvas, and one that painted into a QPainter draws a Formula onto a canvas.
// Where Qt read the image's device pixel ratio, these read the image's size.
//
// The tests need the library build.sh builds into build/ (it needs zig); they
// are skipped, saying so, when it is not there.
//
// KVIT_SHOT_DIR, when set, is where the images the Qt tests saved are
// written: the four canonical formulas and the reference corpus sheet.
package mathtex

import (
	"errors"
	"fmt"
	"image"
	"image/png"
	"math"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// needEngine skips the test when the library was never built, and fails it
// when the library is there but math is off.
func needEngine(t *testing.T) {
	t.Helper()
	if err := LoadError(); err != nil {
		if errors.Is(err, errLibraryMissing) {
			t.Skipf("%v; ./build.sh builds it (it needs zig, see tools/build-mathlib.sh)", err)
		}
		t.Fatal(err)
	}
}

// newtxCharterMode mirrors the engine's default: the vendored NewTX/XCharter
// fonts are used unless KVIT_MATH_FONT=cm asks for Computer Modern.
func newtxCharterMode() bool {
	return strings.TrimSpace(os.Getenv("KVIT_MATH_FONT")) != "cm"
}

// shotDir is where the tests save images, or "" to save none.
func shotDir(t *testing.T) string {
	dir := os.Getenv("KVIT_SHOT_DIR")
	if dir != "" {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			t.Fatal(err)
		}
	}
	return dir
}

func savePNG(t *testing.T, img image.Image, path string) {
	t.Helper()
	f, err := os.Create(path)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if err := png.Encode(f, img); err != nil {
		t.Fatal(err)
	}
}

func pngVerticalPadding(textSize int) int {
	return max(2, int(math.Ceil(float64(textSize)*0.12)))
}

func alpha(img *image.NRGBA, x, y int) uint8 { return img.Pix[img.PixOffset(x, y)+3] }

func hasVisiblePixel(img *image.NRGBA) bool {
	if img == nil {
		return false
	}
	b := img.Bounds()
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			if alpha(img, x, y) > 0 {
				return true
			}
		}
	}
	return false
}

func hasVisiblePixelOnHorizontalEdge(img *image.NRGBA) bool {
	if img == nil {
		return false
	}
	b := img.Bounds()
	for x := b.Min.X; x < b.Max.X; x++ {
		if alpha(img, x, b.Min.Y) > 0 || alpha(img, x, b.Max.Y-1) > 0 {
			return true
		}
	}
	return false
}

func visibleBounds(img *image.NRGBA) image.Rectangle {
	var bounds image.Rectangle
	b := img.Bounds()
	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			if alpha(img, x, y) > 0 {
				bounds = bounds.Union(image.Rect(x, y, x+1, y+1))
			}
		}
	}
	return bounds
}

// widestHorizontalInkRun is the length of the longest run of inked pixels in
// any one row in [y0, y1); y1 < 0 means to the bottom. A fraction bar or a
// radical's vinculum is the widest run, which lets a test check that a rule
// spans its content rather than only the reported sizes.
func widestHorizontalInkRun(img *image.NRGBA, y0, y1 int) int {
	b := img.Bounds()
	y0 = min(max(y0, 0), b.Dy())
	if y1 < 0 {
		y1 = b.Dy()
	}
	y1 = min(max(y1, 0), b.Dy())
	widest := 0
	for y := y0; y < y1; y++ {
		run := 0
		for x := 0; x < b.Dx(); x++ {
			if alpha(img, x, y) > 0 {
				run++
				widest = max(widest, run)
			} else {
				run = 0
			}
		}
	}
	return widest
}

// solidRuleThickness is the thickness in pixels of the widest solid
// horizontal rule: the longest streak of rows whose widest run, counting
// pixels at least alphaThreshold opaque, reaches 90% of the image's widest.
func solidRuleThickness(img *image.NRGBA, alphaThreshold uint8) int {
	b := img.Bounds()
	runs := make([]int, b.Dy())
	globalWidest := 0
	for y := 0; y < b.Dy(); y++ {
		run := 0
		for x := 0; x < b.Dx(); x++ {
			if alpha(img, x, y) >= alphaThreshold {
				run++
				runs[y] = max(runs[y], run)
			} else {
				run = 0
			}
		}
		globalWidest = max(globalWidest, runs[y])
	}
	ruleWidth := int(math.Round(float64(globalWidest) * 0.9))
	thickness, streak := 0, 0
	for y := range runs {
		if runs[y] >= ruleWidth {
			streak++
			thickness = max(thickness, streak)
		} else {
			streak = 0
		}
	}
	return thickness
}

var (
	labelOnce  sync.Once
	labelFonts *text.Fonts
)

// fonts is the system's fonts, for the labels of the sheets the tests draw.
func fonts(t *testing.T) *text.Fonts {
	labelOnce.Do(func() { labelFonts, _ = text.NewFonts("") })
	if labelFonts == nil {
		t.Skip("no system fonts to label the sheet with")
	}
	return labelFonts
}

type corpusEntry struct{ title, tex string }

func newtxCharterReferenceCorpus() []corpusEntry {
	return []corpusEntry{
		{"Inline canary", `x^2`},
		{"Basic italic and superscript", `E = mc^2`},
		{"Subscripts and powers", `A_i^2 + B_i^2 = C_i^2`},
		{"Fraction", `\frac{a+b}{c+d}`},
		{"Nested fraction", `\frac{1}{1+\frac{x}{2}}`},
		{"Radical", `\sqrt{1 + x^2}`},
		{"Integral with limits", `\int_0^\infty e^{-x^2}\,dx`},
		{"Sum with limits", `\sum_{n=1}^{\infty} \frac{1}{n^2}`},
		{"Scaled delimiters", `\left(\frac{a}{b}\right)`},
		{"Greek variants", `\alpha\beta\Gamma\Delta\theta\vartheta\phi\varphi`},
		{"Relations and sets", `\leq \geq \neq \approx \in \notin \subseteq \cup \cap`},
		{"Operators", `\partial\quad\nabla\quad\infty`},
		{"Math alphabets", `\mathcal{F}\quad\mathscr{L}\quad\mathbb{R}\quad\mathfrak{g}`},
		// Stress rows: script and scriptscript symbols use the ntxsy7 and
		// ntxsy5 optical masters, and roman beside italic shows the
		// relative scale of the text and math fonts.
		{"Script sizes and optical masters", `x^{\in A}\quad y_{\oplus}\quad e^{x^{\leq 2}}`},
		{"Text style against math italic", `\mathrm{x}x\quad\mathrm{d}x\quad\sin(x) + \log(y)`},
	}
}

// corpusSheet lays the corpus out as the Qt test's composeCorpusSheet did:
// each entry's number, title and TeX on the left, its image on the right.
func corpusSheet(t *testing.T, images []*image.NRGBA, title string) *image.NRGBA {
	fs := fonts(t)
	const margin, gutter, labelWidth, rowGap, titleGap = 24, 24, 430, 18, 20
	ink := text.Color{A: 255}
	titleStyle := text.Style{Size: 24, Weight: text.Bold, Color: ink}
	labelStyle := text.Style{Size: 13, Color: ink}
	texStyle := text.Style{Family: text.Monospace, Size: 12, Color: ink}
	one := func(s string, st text.Style) *text.Layout {
		return fs.Layout([]text.Span{{Text: s, Style: st}}, text.Options{MaxWidth: labelWidth, Elide: true})
	}
	titleLayout := fs.Layout([]text.Span{{Text: title, Style: titleStyle}}, text.Options{})
	_, titleH := titleLayout.Size()
	_, labelH := one("X", labelStyle).Size()
	_, texH := one("X", texStyle).Size()
	textH := int(labelH+texH) + 6
	titleW, _ := titleLayout.Size()
	formulaW := 1
	height := margin + int(titleH) + titleGap + margin
	for _, img := range images {
		formulaW = max(formulaW, img.Bounds().Dx())
		height += max(img.Bounds().Dy(), textH) + rowGap
	}
	width := max(900, margin*2+labelWidth+gutter+formulaW, margin*2+int(math.Ceil(float64(titleW))))
	corpus := newtxCharterReferenceCorpus()
	sheet, err := unison.NewImageFromDrawing(width, height, 72, func(gc *unison.Canvas) {
		gc.DrawRect(geom.NewRect(0, 0, float32(width), float32(height)), unison.White.Paint(gc, geom.Rect{}, paintstyle.Fill))
		y := float32(margin)
		titleLayout.Draw(gc, margin, y)
		y += titleH + titleGap
		for i, img := range images {
			rowH := max(img.Bounds().Dy(), textH)
			one(fmt.Sprintf("%d. %s", i+1, corpus[i].title), labelStyle).Draw(gc, margin, y)
			one(corpus[i].tex, texStyle).Draw(gc, margin, y+labelH+6)
			formula, err := unison.NewImageFromPixels(img.Bounds().Dx(), img.Bounds().Dy(), img.Pix, geom.NewPoint(1, 1))
			if err == nil {
				gc.DrawImage(formula, geom.NewPoint(margin+labelWidth+gutter, y+float32(rowH-img.Bounds().Dy())/2), nil, nil)
			}
			y += float32(rowH + rowGap)
		}
	})
	if err != nil {
		t.Fatal(err)
	}
	out, err := sheet.ToNRGBA()
	if err != nil {
		t.Fatal(err)
	}
	return out
}

// MicroTeX sizes a formula to its advance width, which is not where its
// glyphs stop: the italic f carries its tail left of the origin and its hook
// right of the advance, and a script-size f overhangs by a fifth of the text
// size. Drawn into an image exactly the advance wide, those parts were cut off.
// The side-bearing margin keeps them inside the image.
func TestSideBearingMarginKeepsGlyphsInsideTheRaster(t *testing.T) {
	needEngine(t)
	corpus := []string{`f`, `f(x)`, `A_f`, `x^2`, `g`, `y`, `\frac{f}{g}`, `\int_0^\infty x^2 dx`}
	for _, tex := range corpus {
		for _, size := range []int{15, 17, 20, 32} {
			pad := SideBearingPadding(size)
			if pad <= 0 {
				t.Fatalf("padding %d at %d px", pad, size)
			}
			img, err := Image(tex, size, unison.Black, 1, 2, pad, true)
			if err != nil || img == nil {
				t.Fatalf("render failed for '%s': %v", tex, err)
			}
			leftInk, rightInk := 0, 0
			b := img.Bounds()
			for y := 0; y < b.Dy(); y++ {
				if alpha(img, 0, y) > 0 {
					leftInk++
				}
				if alpha(img, b.Dx()-1, y) > 0 {
					rightInk++
				}
			}
			if leftInk != 0 || rightInk != 0 {
				t.Errorf("'%s' at %dpx paints on its own edge (left %d, right %d pixels)", tex, size, leftInk, rightInk)
			}
		}
	}
}

// The reported baseline is the distance from the top of the formula down to
// the line it stands on, and the inline overlay places an equation by putting
// that line on the text's own baseline, so it has to locate the ink. Measured
// at a high pixel ratio, where an error of under a logical pixel is several
// device rows.
func TestBaselineLocatesTheFeetOfTheGlyphs(t *testing.T) {
	needEngine(t)
	// Upright letters and digits stand flat on the baseline with nothing
	// below it, so the lowest row of ink is the baseline itself.
	corpus := []string{`\mathrm{H}`, `\mathrm{HET}`, `\mathrm{A} + \mathrm{B}`, `1 + 11`}
	for _, size := range []int{15, 17, 20, 32} {
		for _, tex := range corpus {
			t.Run(fmt.Sprintf("size%d:%s", size, tex), func(t *testing.T) {
				const dpr = 4.0
				vpad := pngVerticalPadding(size)
				hpad := SideBearingPadding(size)
				m := Measure(tex, size, true)
				if !m.Valid {
					t.Fatal(m.Error)
				}
				if m.Depth >= 1 {
					t.Fatalf("'%s' was chosen for having nothing below the baseline, but reports depth %v", tex, m.Depth)
				}
				img, err := Image(tex, size, unison.Black, dpr, vpad, hpad, true)
				if err != nil || img == nil {
					t.Fatal(err)
				}
				if want := int(math.Round((m.Width + float64(2*hpad)) * dpr)); img.Bounds().Dx() != want {
					t.Fatalf("image %d wide, want %d: the ratio is not %v", img.Bounds().Dx(), want, dpr)
				}
				lowestInk := -1
				b := img.Bounds()
				for y := b.Dy() - 1; y >= 0 && lowestInk < 0; y-- {
					for x := 0; x < b.Dx(); x++ {
						if alpha(img, x, y) >= 32 {
							lowestInk = y
							break
						}
					}
				}
				if lowestInk < 0 {
					t.Fatal("the formula rendered nothing")
				}
				if lowestInk >= b.Dy()-1 {
					t.Fatal("the ink reaches the bottom edge, so the raster is cutting the formula off")
				}
				// The baseline runs along the bottom of the lowest inked row.
				// Two device pixels of slack cover antialiasing and the font's
				// own rounding; the defect this guards against is several times
				// that.
				baselineRow := (float64(vpad) + m.Baseline) * dpr
				if math.Abs(baselineRow-float64(lowestInk+1)) > 2 {
					t.Errorf("'%s' at %dpx: baseline %.2f puts the feet at row %.1f, ink ends at %d",
						tex, size, m.Baseline, baselineRow, lowestInk)
				}
			})
		}
	}
}

// The margin is transparent padding, not a new layout: the formula keeps its
// width and gains the margin on each side, so a caller that shifts the image
// left by the padding puts it back where the unpadded one sat.
func TestSideBearingMarginOnlyWidensTheRaster(t *testing.T) {
	needEngine(t)
	tex := `\int_0^\infty x^2 dx`
	const size = 20
	pad := SideBearingPadding(size)
	bare, err1 := Image(tex, size, unison.Black, 1, 2, 0, true)
	padded, err2 := Image(tex, size, unison.Black, 1, 2, pad, true)
	if err1 != nil || err2 != nil {
		t.Fatal(err1, err2)
	}
	if padded.Bounds().Dx() != bare.Bounds().Dx()+2*pad || padded.Bounds().Dy() != bare.Bounds().Dy() {
		t.Fatalf("padded %v, bare %v, pad %d", padded.Bounds(), bare.Bounds(), pad)
	}
	// The formula is drawn identically, pad further right: every pixel of the
	// unpadded image reappears at the same offset.
	for y := 0; y < bare.Bounds().Dy(); y++ {
		for x := 0; x < bare.Bounds().Dx(); x++ {
			if padded.NRGBAAt(x+pad, y) != bare.NRGBAAt(x, y) {
				t.Fatalf("pixel %d,%d differs: %v against %v", x, y, padded.NRGBAAt(x+pad, y), bare.NRGBAAt(x, y))
			}
		}
	}
}

func TestOverlongFormulaIsRefusedNotRasterized(t *testing.T) {
	needEngine(t)
	start := time.Now()
	img, err := Image(strings.Repeat("x", 400000), 20, unison.Black, 1, 0, 0, true)
	elapsed := time.Since(start)
	if img != nil {
		t.Error("an over-long formula must not rasterize")
	}
	if err == nil {
		t.Error("the refusal must say why")
	}
	if elapsed >= time.Second {
		t.Errorf("took %v", elapsed)
	}
	// The validity check refuses the same source, so the block shows an error
	// rather than silently rendering nothing.
	if ErrorFor(strings.Repeat("x", 400000)) == "" {
		t.Error("ErrorFor accepted an over-long formula")
	}
}

func TestFormulaAtTheLengthLimitStillRenders(t *testing.T) {
	needEngine(t)
	img, err := Image(strings.Repeat("x", 4000), 20, unison.Black, 1, 0, 0, true)
	if err != nil || img == nil {
		t.Fatal(err)
	}
}

// Text size and pixel ratio multiply into the same image.
func TestHugeDevicePixelRatioIsBounded(t *testing.T) {
	needEngine(t)
	img, err := Image(`x^2`, 20, unison.Black, 1000, 0, 0, true)
	if err != nil {
		t.Fatal(err)
	}
	if px := img.Bounds().Dx() * img.Bounds().Dy(); px > MaxRasterPixels {
		t.Errorf("raster %v exceeds the budget", img.Bounds())
	}
}

func TestHugeTextSizeIsBounded(t *testing.T) {
	needEngine(t)
	img, err := Image(`x^2`, 100000, unison.Black, 1, 0, 0, true)
	if err != nil {
		t.Fatal(err)
	}
	if px := img.Bounds().Dx() * img.Bounds().Dy(); px > MaxRasterPixels {
		t.Errorf("raster %v exceeds the budget", img.Bounds())
	}
}

// The math-command menu completes from this list: symbols, built-in macros
// and the NewTX additions are in it, and internal names (matrix@@env and
// the like) are not.
func TestAvailableCommandsEnumerates(t *testing.T) {
	needEngine(t)
	commands := Commands()
	if len(commands) <= 300 {
		t.Fatalf("suspiciously small corpus: %d", len(commands))
	}
	for _, want := range []string{"frac", "alpha", "sum", "vv"} { // vv is a NewTX macro
		if !slices.Contains(commands, want) {
			t.Errorf("%s is missing", want)
		}
	}
	for _, name := range commands {
		if strings.Contains(name, "@") {
			t.Errorf("internal name leaked: %s", name)
		}
	}
	// Sorted and without duplicates.
	if !slices.IsSorted(commands) {
		t.Error("not sorted")
	}
	if len(slices.Compact(slices.Clone(commands))) != len(commands) {
		t.Error("duplicates")
	}
}

// Seen in the Qt app on Windows: after a failed parse of a half-typed
// command, a formula that rendered before stopped rendering for the rest of
// the session. A parse error must not spoil later renders. The cache is
// cleared before each render so each one reaches the engine.
func TestRenderRecoversAfterParseError(t *testing.T) {
	needEngine(t)
	good := `\intop_0^\infty`
	clearCache()
	before, err := Image(good, 20, unison.Black, 1, 2, 0, true)
	if err != nil || before == nil {
		t.Fatal(err)
	}
	// What the user had typed while "\exp" was being completed, then a few
	// classic invalid ones.
	invalids := []string{`\intop_0^\infty \e`, `\intop_0^\infty \ex`, `\intop_0^\infty \exp`,
		`\frac{1}`, `{unbalanced`, `\definitelynotacommand`}
	for _, bad := range invalids {
		clearCache()
		// Whether each fails is MicroTeX's business; the good formula must
		// render afterwards.
		_, _ = Image(bad, 20, unison.Black, 1, 2, 0, true)
		clearCache()
		after, err := Image(good, 20, unison.Black, 1, 2, 0, true)
		if err != nil || after == nil {
			t.Fatalf("'%s' poisoned the engine: %v", bad, err)
		}
		if after.Bounds() != before.Bounds() {
			t.Errorf("after '%s': %v, before %v", bad, after.Bounds(), before.Bounds())
		}
	}
}

// The directory the engine starts on must exist.
func TestResourceRootResolves(t *testing.T) {
	needEngine(t)
	root := ResourceRoot()
	if root == "" {
		t.Fatal("no resource root")
	}
	if !isDir(root) {
		t.Fatalf("res root missing: %s", root)
	}
}

// canonicalExpressions are the formulas behind the Qt app's
// screenshots/math_render_0*.png.
var canonicalExpressions = []struct{ name, tex, file string }{
	{"power", `x^2`, "math_render_01_power.png"},
	{"fraction", `\frac{a}{b}`, "math_render_02_fraction.png"},
	{"integral", `\int_0^1 x\,dx`, "math_render_03_integral.png"},
	{"group", `\overgroup{AB}`, "math_render_04_group.png"},
}

func TestRendersCanonicalExpressions(t *testing.T) {
	needEngine(t)
	dir := shotDir(t)
	for _, c := range canonicalExpressions {
		t.Run(c.name, func(t *testing.T) {
			img, err := Image(c.tex, 48, unison.Black, 1, 0, 0, true)
			if err != nil || img == nil {
				t.Fatalf("render failed for '%s': %v", c.tex, err)
			}
			if img.Bounds().Dx() <= 4 || img.Bounds().Dy() <= 4 {
				t.Fatalf("image %v", img.Bounds())
			}
			if dir != "" {
				savePNG(t, img, filepath.Join(dir, c.file))
				savePNG(t, onWhite(img, max(1, 200/img.Bounds().Dy())), filepath.Join(dir, "white_"+c.file))
			}
		})
	}
}

// onWhite is img enlarged scale times on white, so black glyphs on a
// transparent image can be looked at.
func onWhite(img *image.NRGBA, scale int) *image.NRGBA {
	b := img.Bounds()
	out := image.NewNRGBA(image.Rect(0, 0, b.Dx()*scale, b.Dy()*scale))
	for y := 0; y < b.Dy()*scale; y++ {
		for x := 0; x < b.Dx()*scale; x++ {
			c := img.NRGBAAt(x/scale, y/scale)
			a := int(c.A)
			mix := func(v uint8) uint8 { return uint8((int(v)*a + 255*(255-a)) / 255) }
			i := out.PixOffset(x, y)
			out.Pix[i], out.Pix[i+1], out.Pix[i+2], out.Pix[i+3] = mix(c.R), mix(c.G), mix(c.B), 255
		}
	}
	return out
}

// Rendering the same formula in two colours must give different pixels.
func TestForegroundColorApplies(t *testing.T) {
	needEngine(t)
	red, err1 := Image("x", 48, unison.RGB(255, 0, 0), 1, 0, 0, true)
	blue, err2 := Image("x", 48, unison.RGB(0, 0, 255), 1, 0, 0, true)
	if err1 != nil || err2 != nil || red == nil || blue == nil {
		t.Fatal(err1, err2)
	}
	if red.Bounds() != blue.Bounds() {
		t.Fatalf("%v against %v", red.Bounds(), blue.Bounds())
	}
	if slices.Equal(red.Pix, blue.Pix) {
		t.Error("the colour changed nothing")
	}
}

func TestDprRenderUsesPhysicalPixels(t *testing.T) {
	needEngine(t)
	one, err1 := Image("x^2", 18, unison.Black, 1, 0, 0, true)
	two, err2 := Image("x^2", 18, unison.Black, 2, 0, 0, true)
	if err1 != nil || err2 != nil || one == nil || two == nil {
		t.Fatal(err1, err2)
	}
	if two.Bounds().Dx() != one.Bounds().Dx()*2 || two.Bounds().Dy() != one.Bounds().Dy()*2 {
		t.Fatalf("ratio 2 %v, ratio 1 %v", two.Bounds(), one.Bounds())
	}
	// The ratio-2 image holds the same drawing at twice the size, not an
	// enlarged corner of it.
	ink1, ink2 := visibleBounds(one), visibleBounds(two)
	const tolerance = 3
	absInt := func(v int) int { return max(v, -v) }
	if absInt(ink2.Dx()-ink1.Dx()*2) > tolerance {
		t.Errorf("dpr2 ink width %d vs dpr1 %d", ink2.Dx(), ink1.Dx())
	}
	if absInt(ink2.Dy()-ink1.Dy()*2) > tolerance {
		t.Errorf("dpr2 ink height %d vs dpr1 %d", ink2.Dy(), ink1.Dy())
	}
	if absInt(ink2.Min.X-ink1.Min.X*2) > tolerance || absInt(ink2.Min.Y-ink1.Min.Y*2) > tolerance {
		t.Errorf("dpr2 ink origin %v vs dpr1 %v", ink2.Min, ink1.Min)
	}
}

func TestVerticalPaddingAddsTransparentLogicalHeight(t *testing.T) {
	needEngine(t)
	plain, err1 := Image("x^2", 18, unison.Black, 1, 0, 0, true)
	padded, err2 := Image("x^2", 18, unison.Black, 1, 3, 0, true)
	if err1 != nil || err2 != nil || plain == nil || padded == nil {
		t.Fatal(err1, err2)
	}
	if padded.Bounds().Dx() != plain.Bounds().Dx() || padded.Bounds().Dy() != plain.Bounds().Dy()+6 {
		t.Errorf("padded %v, plain %v", padded.Bounds(), plain.Bounds())
	}
}

// drawn draws a formula onto a canvas of w × h pixels at (x, y), and returns
// the canvas's pixels.
func drawn(t *testing.T, f *Formula, w, h int, x, y float32) *image.NRGBA {
	t.Helper()
	img, err := unison.NewImageFromDrawing(w, h, 72, func(gc *unison.Canvas) { f.Draw(gc, x, y, unison.Black) })
	if err != nil {
		t.Fatal(err)
	}
	px, err := img.ToNRGBA()
	if err != nil {
		t.Fatal(err)
	}
	return px
}

func TestDirectPaintProducesVisiblePixels(t *testing.T) {
	needEngine(t)
	m := Measure("x^2", 18, true)
	if !m.Valid {
		t.Fatal(m.Error)
	}
	f, err := Render("x^2", 18, true)
	if err != nil {
		t.Fatal(err)
	}
	img := drawn(t, f, int(math.Ceil(m.Width))+2, int(math.Ceil(m.Height))+2, 1, 1)
	if !hasVisiblePixel(img) {
		t.Error("nothing drawn")
	}
}

func TestDirectPaintReportsErrors(t *testing.T) {
	needEngine(t)
	f, err := Render("}", 18, true)
	if err == nil {
		t.Fatal("no error for '}'")
	}
	if img := drawn(t, f, 8, 8, 0, 0); hasVisiblePixel(img) {
		t.Error("a formula that failed drew something")
	}
}

func TestCorpusDimensions(t *testing.T) {
	needEngine(t)
	corpus := []string{`x^2`, `E = mc^2`, `a_i^2 + b_i^2`, `\frac{a}{b}`, `\frac{1}{1+\frac{x}{2}}`,
		`\int_0^1 x\,dx`, `\sum_{i=0}^n i^2`, `\sqrt{x^2 + y^2}`, `\alpha + \beta \leq \gamma`,
		`\left(\sum_{i=0}^{n} x_i\right)^2`}
	for _, size := range []int{15, 18, 26, 48} {
		for _, tex := range corpus {
			t.Run(fmt.Sprintf("size%d:%s", size, tex), func(t *testing.T) {
				m := Measure(tex, size, true)
				if !m.Valid || m.Error != "" {
					t.Fatalf("measure failed for '%s': %s", tex, m.Error)
				}
				if m.Width <= 0 || m.Height <= 0 || m.Baseline <= 0 || m.Baseline > m.Height ||
					m.Ascent <= 0 || m.Descent < 0 || m.Depth < 0 || m.Depth > m.Height ||
					math.Abs(m.Ascent+m.Descent-m.Height) > 0.5 {
					t.Fatalf("metrics %+v", m)
				}
				img, err := Image(tex, size, unison.Black, 1, 0, 0, true)
				if err != nil || img == nil {
					t.Fatalf("render failed for '%s': %v", tex, err)
				}
				if math.Abs(m.Width-float64(img.Bounds().Dx())) > 1 || math.Abs(m.Height-float64(img.Bounds().Dy())) > 1 {
					t.Errorf("metrics %vx%v, image %v", m.Width, m.Height, img.Bounds())
				}
				t.Logf("MATH_CORPUS_DIM size=%d tex=%q image=%dx%d baseline=%.2f depth=%.2f",
					size, tex, img.Bounds().Dx(), img.Bounds().Dy(), m.Baseline, m.Depth)
			})
		}
	}
}

func TestTallDelimiterAndRadicalGeometry(t *testing.T) {
	needEngine(t)
	const textSize = 42
	content := `\frac{1}{1+\frac{x}{2}}`
	fenced := `\left(` + content + `\right)`
	radical := `\sqrt{` + content + `}`
	contentM := Measure(content, textSize, true)
	smallFencedM := Measure(`\left(x\right)`, textSize, true)
	fencedM := Measure(fenced, textSize, true)
	radicalM := Measure(radical, textSize, true)
	for _, m := range []Metrics{contentM, smallFencedM, fencedM, radicalM} {
		if !m.Valid {
			t.Fatal(m.Error)
		}
	}
	if fencedM.Height < contentM.Height || fencedM.Width <= contentM.Width ||
		fencedM.Height <= smallFencedM.Height*1.25 {
		t.Errorf("fenced %+v, content %+v, small fenced %+v", fencedM, contentM, smallFencedM)
	}
	if radicalM.Height < contentM.Height || radicalM.Width <= contentM.Width {
		t.Errorf("radical %+v, content %+v", radicalM, contentM)
	}
	padding := pngVerticalPadding(textSize)
	for _, tex := range []string{fenced, radical} {
		img, err := Image(tex, textSize, unison.Black, 1, padding, 0, true)
		if err != nil || img == nil {
			t.Fatal(err)
		}
		if !hasVisiblePixel(img) || hasVisiblePixelOnHorizontalEdge(img) {
			t.Errorf("'%s': no ink, or ink on the top or bottom edge", tex)
		}
		if h := visibleBounds(img).Dy(); h < int(math.Round(contentM.Height*0.85)) {
			t.Errorf("'%s': ink %d tall, content %v", tex, h, contentM.Height)
		}
	}
}

// The fraction rule is at least as wide as the wider of numerator and
// denominator, checked on the drawn bar's ink rather than on the sizes.
func TestFractionBarSpansWiderRow(t *testing.T) {
	needEngine(t)
	const textSize = 48
	padding := pngVerticalPadding(textSize)
	// A numerator far wider than the denominator, so it sets the bar's width.
	numerator, denominator := `x+y+z+w`, `k`
	num, err1 := Image(numerator, textSize, unison.Black, 1, padding, 0, true)
	den, err2 := Image(denominator, textSize, unison.Black, 1, padding, 0, true)
	frac, err3 := Image(`\frac{`+numerator+`}{`+denominator+`}`, textSize, unison.Black, 1, padding, 0, true)
	if err1 != nil || err2 != nil || err3 != nil {
		t.Fatal(err1, err2, err3)
	}
	numInk, denInk := visibleBounds(num).Dx(), visibleBounds(den).Dx()
	barRun := widestHorizontalInkRun(frac, 0, -1)
	t.Logf("FRACTION_BAR num=%d den=%d bar=%d img=%d", numInk, denInk, barRun, frac.Bounds().Dx())
	if barRun < max(numInk, denInk) {
		t.Errorf("bar run %d < wider side %d", barRun, max(numInk, denInk))
	}
	if barRun > frac.Bounds().Dx() {
		t.Errorf("bar run %d wider than the image %d", barRun, frac.Bounds().Dx())
	}
}

// A radical's sign widens it on the left and its vinculum is a rule across
// the top of the radicand.
func TestRadicalVinculumCoversRadicand(t *testing.T) {
	needEngine(t)
	const textSize = 48
	padding := pngVerticalPadding(textSize)
	radicand := `x^2 + y^2`
	radImg, err1 := Image(radicand, textSize, unison.Black, 1, padding, 0, true)
	sqrtImg, err2 := Image(`\sqrt{`+radicand+`}`, textSize, unison.Black, 1, padding, 0, true)
	if err1 != nil || err2 != nil {
		t.Fatal(err1, err2)
	}
	radB, sqrtB := visibleBounds(radImg), visibleBounds(sqrtImg)
	if sqrtB.Dx() <= radB.Dx() || sqrtB.Dy() <= radB.Dy() {
		t.Errorf("radical ink %v, radicand ink %v", sqrtB, radB)
	}
	band := max(1, int(math.Round(float64(sqrtB.Dy())*0.30)))
	vinculum := widestHorizontalInkRun(sqrtImg, sqrtB.Min.Y, sqrtB.Min.Y+band)
	t.Logf("RADICAL rad=%dx%d sqrt=%dx%d vinculum=%d band=%d", radB.Dx(), radB.Dy(), sqrtB.Dx(), sqrtB.Dy(), vinculum, band)
	if want := int(math.Round(float64(radB.Dx()) * 0.9)); vinculum < want {
		t.Errorf("vinculum run %d < 0.9*radicand %d", vinculum, want)
	}
}

// TeX takes the rule thickness from the fonts, so the NewTX fonts' 0.056 em
// (ntxexx's defaultrulethickness) rather than Computer Modern's 0.040 em; the
// drawn fraction bar is the witness.
func TestFractionRuleThicknessTracksFontConstants(t *testing.T) {
	needEngine(t)
	const textSize = 192
	img, err := Image(`\frac{x+y+z+w}{k}`, textSize, unison.Black, 1, pngVerticalPadding(textSize), 0, true)
	if err != nil {
		t.Fatal(err)
	}
	// Fonts are at one point and scaled by the text size, one pixel per
	// point, so an em is textSize pixels.
	em := 0.04
	if newtxCharterMode() {
		em = 0.056
	}
	expected := em * textSize
	measured := solidRuleThickness(img, 128)
	t.Logf("FRACTION_RULE thickness=%d expected=%.2f", measured, expected)
	if math.Abs(float64(measured)-expected) > 2 {
		t.Errorf("rule thickness %dpx, expected %.2fpx", measured, expected)
	}
}

// The NewTX symbol fonts have optical masters for script (ntxsy7) and
// scriptscript (ntxsy5) sizes, drawn wider than ntxsy scaled down: \in is
// 0.556 em in ntxsy and 0.596 em in both. Plain scaling would give widths of
// 0.70 and 0.50 of the text-style one; the masters give 0.750 and 0.536.
func TestGeneratedOpticalScriptMastersWidenScriptSymbols(t *testing.T) {
	needEngine(t)
	if !newtxCharterMode() {
		t.Skip("NewTX charter mode disabled (KVIT_MATH_FONT=cm)")
	}
	const textSize = 128
	text := Measure(`\in`, textSize, true)
	script := Measure(`{\scriptstyle\in}`, textSize, true)
	scriptScript := Measure(`{\scriptscriptstyle\in}`, textSize, true)
	for _, m := range []Metrics{text, script, scriptScript} {
		if !m.Valid {
			t.Fatal(m.Error)
		}
	}
	scriptRatio := script.Width / text.Width
	scriptScriptRatio := scriptScript.Width / text.Width
	t.Logf("OPTICAL_IN text=%.2f script=%.2f (%.4f) ss=%.2f (%.4f)", text.Width, script.Width, scriptRatio,
		scriptScript.Width, scriptScriptRatio)
	if math.Abs(scriptRatio-0.750) > 0.01 {
		t.Errorf("script \\in width ratio %.4f, ntxsy7 predicts 0.750", scriptRatio)
	}
	if math.Abs(scriptScriptRatio-0.536) > 0.01 {
		t.Errorf("scriptscript \\in width ratio %.4f, ntxsy5 predicts 0.536", scriptScriptRatio)
	}
}

// The TeX slots below 33 (\Gamma is zchmia 0, the minus ntxsy 0, \beta zchmi
// 12) are drawn through their copies at U+E000 + slot; each must leave ink.
func TestGeneratedLowSlotGlyphsProduceInk(t *testing.T) {
	needEngine(t)
	if !newtxCharterMode() {
		t.Skip("NewTX charter mode disabled (KVIT_MATH_FONT=cm)")
	}
	const textSize = 48
	padding := pngVerticalPadding(textSize)
	for _, tex := range []string{`\Gamma`, `\beta`, `\gamma`, `\Psi`, `\Omega`, `{\scriptstyle-1}`} {
		img, err := Image(tex, textSize, unison.Black, 1, padding, 0, true)
		if err != nil || img == nil {
			t.Fatalf("render failed for '%s': %v", tex, err)
		}
		if !hasVisiblePixel(img) {
			t.Errorf("no ink for '%s': low-slot glyph dropped", tex)
		}
	}
	// The exponent's minus: e^{-x^2} is wider than e^{x^2} by it.
	with, err1 := Image(`e^{-x^2}`, textSize, unison.Black, 1, padding, 0, true)
	without, err2 := Image(`e^{x^2}`, textSize, unison.Black, 1, padding, 0, true)
	if err1 != nil || err2 != nil {
		t.Fatal(err1, err2)
	}
	if visibleBounds(with).Dx() <= visibleBounds(without).Dx() {
		t.Error("script-style minus leaves no ink in e^{-x^2}")
	}
}

// Every family loads at its natural size in the LaTeX preamble Kvit's math
// reproduces, so the ink height of a math italic x (zchmi) against an
// XCharter roman x is their TFM heights' ratio, 0.4470 / 0.4865 = 0.919. A
// scale factor between text and math fonts would move it.
func TestGeneratedTextToMathRelativeScaleMatchesTfm(t *testing.T) {
	needEngine(t)
	if !newtxCharterMode() {
		t.Skip("NewTX charter mode disabled (KVIT_MATH_FONT=cm)")
	}
	const textSize = 160
	padding := pngVerticalPadding(textSize)
	italic, err1 := Image(`x`, textSize, unison.Black, 1, padding, 0, true)
	roman, err2 := Image(`\mathrm{x}`, textSize, unison.Black, 1, padding, 0, true)
	if err1 != nil || err2 != nil {
		t.Fatal(err1, err2)
	}
	italicInk, romanInk := visibleBounds(italic).Dy(), visibleBounds(roman).Dy()
	if romanInk <= 0 {
		t.Fatal("no roman ink")
	}
	ratio := float64(italicInk) / float64(romanInk)
	t.Logf("TEXT_MATH_SCALE zchmi=%d xcharter=%d ratio=%.4f", italicInk, romanInk, ratio)
	if math.Abs(ratio-0.919) > 0.035 {
		t.Errorf("zchmi/XCharter x-height ratio %.4f, TFM predicts 0.919 at scale 1.0", ratio)
	}
}

func TestGeneratedNewtxSupplementalSymbolsRender(t *testing.T) {
	needEngine(t)
	if !newtxCharterMode() {
		t.Skip("NewTX charter mode disabled (KVIT_MATH_FONT=cm)")
	}
	rows := []struct{ name, tex string }{
		{"active-relation-defaults", `\notin\quad\neq\quad\coloneq\quad\eqcolon\quad\Perp\quad\nPerp`},
		{"direct-relation-aliases", `\ne\quad\neq\quad\notin\quad\colonequals\quad\equalscolon`},
		{"direct-colon-aliases", `\colonapprox\quad\coloncolonsim\quad\coloncolonequals\quad\equalscoloncolon`},
		{"enhanced-letter-symbols", `\hslash\quad\hbar\quad\lambdaslash\quad\lambdabar\quad\transp\quad\hermtransp`},
		{"newtx-let-aliases", `\circledplus\quad\circledminus\quad\circledtimes\quad\circledslash\quad\circleddot`},
		{"symbols-c-arrows", `\mappedfromchar\quad\Mapstochar\quad\Mmapstochar\quad\dashleftarrow\quad\dashrightarrow`},
		{"symbols-c-arrow-aliases", `\mapsfrom\quad\Mapsfrom\quad\dasharrow\quad\lrJoin`},
		{"symbols-c-newtx-mapped-arrows", `\mappedfrom\quad\Mappedfrom\quad\mmapsto\quad\mmappedfrom\quad\Mmapsto\quad\Mmappedfrom`},
		{"symbols-c-newtx-long-mapped-arrows", `\longmappedfrom\quad\Longmappedfrom\quad\longmmapsto\quad\longmmappedfrom\quad\Longmmapsto\quad\Longmmappedfrom`},
		{"symbols-c-delimiters", `\left\lbag\frac{a}{b}\right\rbag\quad\Lbag\quad\Rbag`},
		{"symbols-c-operators", `\circledless\quad\circledgtr\quad\sqcupplus\quad\sqcapplus\quad\boxright`},
		{"newtx-letterA-declarations", `\Zbar\quad\Angstrom\quad\Euler`},
		{"newtx-direct-style-declarations", `\frakdotlessi\quad\jmathfrak\quad\bbdotlessi\quad\jmathbb\quad\imathup\quad\jmathup`},
		{"newtx-upright-greek-aliases", `\Gammaup\quad\upDelta\quad\alphaup\quad\upbeta\quad\upvartheta\quad\upvarphi\quad\upvarkappa\quad\uppartial`},
		{"newtx-italic-greek-aliases", `\Gammait\quad\itDelta\quad\alphait\quad\itbeta\quad\itvartheta\quad\itvarphi\quad\varkappait\quad\itvarkappa`},
		{"newtx-oml-letter-symbols", `\leftharpoonup\quad\rightharpoondown\quad\lhook\quad\rhook\quad\triangleleft\quad\triangleright\quad\flat\quad\natural\quad\sharp\quad\smile\quad\frown\quad\ell\quad\wp\quad\star`},
		{"newtx-alt-ordinary-symbols", `\forallAlt\quad\existsAlt\quad\nexists\quad\nexistsAlt\quad\emptysetAlt\quad\varnothing\quad\varg\quad\vary\quad\upvarkappa\quad\itvarkappa`},
		{"newtx-ams-let-and-hexbox-aliases", `\Join\quad\Box\quad\checkmark\quad\circledR\quad\maltese\quad\nni`},
		{"newtx-ams-class-helpers", `\textsquare\quad\openbox\quad\widebar{AB}`},
		{"newtx-ams-lower-declarations", `\lvertneqq\quad\nleq\quad\nparallel\quad\nleftarrow\quad\divideontimes\quad\Finv\quad\mho\quad\Bbbk\quad\daleth\quad\ltimes\quad\rtimes\quad\backepsilon\quad\triangleq`},
		{"newtx-double-bracket-aliases", `\left\lBrack\frac{a}{b}\right\rBrack\quad\left\dlb\frac{c}{d}\right\drb`},
		{"newtx-small-brace-delimiters", `\left\smlbrace\frac{a}{b}\right\smrbrace\quad\smlbrace x\smrbrace`},
		{"newtx-large-integral-operators", `\iint\quad\iiint\quad\iiiint\quad\oiint\quad\oiiint`},
		{"newtx-integral-operator-internals", `\intop\quad\iintop\quad\iiintop\quad\iiiintop\quad\ointop\quad\oiintop\quad\oiiintop`},
		{"newtx-integral-variants", `\sumint\quad\fint\quad\sqint\quad\varointclockwise\quad\ointctrclockwise`},
		{"newtx-integral-style-internals", `\intslop\quad\iintslop\quad\iiiintslop\quad\varointclockwiseslop\quad\intupop\quad\iintupop\quad\iiiintupop\quad\varointclockwiseupop`},
		{"newtx-small-operators", `\smallint\quad\smalliint\quad\smalliiint\quad\smallprod\quad\smallsum\quad\smallcoprod`},
		{"newtx-large-operator-aliases", `\bigcupdot\quad\bignplus\quad\bigcapplus\quad\bigsqcupplus\quad\bigsqcapplus\quad\bigtimes\quad\varprod`},
		{"newtx-large-operator-internals", `\sumop\quad\prodop\quad\coprodop\quad\bigcupop\quad\bigcapop\quad\bigwedgeop\quad\bigveeop\quad\bigcapplusop\quad\bigsqcupplusop\quad\bigsqcapplusop\quad\bigtimesop`},
		{"newtx-generated-accents", `\dddot{x}\quad\ddddot{x}\quad\lvec{AB}\quad\lrvec{AB}\quad\harpoonacc{x}\quad\widearc{AB}\quad\wideOarc{AB}\quad\barhat{x}\quad\hathat{x}`},
		{"newtx-adaptive-vector", `\vv{AB}\quad\txvec{xyz}\quad\vv*{v}{i}\quad\vv*{AB}{n+1}`},
		{"newtx-widering-and-small-marks", `\widering{ABC}\quad\cdotB\quad\cdotBB\quad\circS\quad\bulletSSS\quad\bulletSS\quad\bulletS\quad\primeS`},
		{"newtx-group-macros", `\overgroup{AB}\quad\undergroup{AB}\quad\overgroupra{AB}\quad\undergroupra{AB}\quad\overgroupla{AB}\quad\undergroupla{AB}\quad\groupld\quad\grouprd\quad\grouplua\quad\grouprua`},
		{"newtx-over-under-brace", `\overbrace{AB}^{n}\quad\underbrace{xy}_{m}`},
		{"newtx-brace-fill-pieces", `\braceld\quad\bracerd\quad\bracelu\quad\braceru\quad\makeatletter\br@cext\makeatother`},
		{"newtx-script-and-variant-blackboard", `\mathscr{FLx}\quad\scrdotlessi\quad\jmathscr\quad\vmathbb{R2z}\quad\vmathbb{\Gamma}\quad\vmathbb{\pi}\quad\vvmathbb{R2z}\quad\vvmathbb{\Pi}\quad\vvmathbb{\gamma}`},
		{"newtx-upright-script-command", `\mathuscr{FLx}\quad\mathslscr{FLx}\quad\mathscr{FLx}`},
		{"generated-text-styles", `\mathrm{sin}\quad\mathit{Rate}\quad\mathbf{Ab1}\quad\boldsymbol{x+\alpha\leq\sum}\quad\mathscr{Lx}`},
	}
	for _, row := range rows {
		t.Run(row.name, func(t *testing.T) {
			img, err := Image(row.tex, 32, unison.Black, 1, pngVerticalPadding(32), 0, true)
			if err != nil || img == nil {
				t.Fatalf("render failed for '%s': %v", row.tex, err)
			}
			if !hasVisiblePixel(img) {
				t.Error("no ink")
			}
			if hasVisiblePixelOnHorizontalEdge(img) {
				t.Error("ink on the top or bottom edge")
			}
		})
	}
}

// The reference corpus renders with ink inside its images. With
// KVIT_SHOT_DIR set the sheet is saved as the Qt test saved it; the Qt test
// then compared it with pages typeset by LaTeX, which are optional and not
// in the Qt repository, so that part is skipped as it was there.
func TestNewtxCharterReferenceCorpusArtifacts(t *testing.T) {
	needEngine(t)
	const textSize = 26
	var images []*image.NRGBA
	for _, entry := range newtxCharterReferenceCorpus() {
		img, err := Image(entry.tex, textSize, unison.Black, 1, pngVerticalPadding(textSize), 0, true)
		if err != nil || img == nil {
			t.Fatalf("PNG render failed for '%s': %v", entry.tex, err)
		}
		if !hasVisiblePixel(img) {
			t.Errorf("no ink for '%s'", entry.tex)
		}
		if hasVisiblePixelOnHorizontalEdge(img) {
			t.Errorf("PNG render touches top/bottom edge for '%s'", entry.tex)
		}
		images = append(images, img)
	}
	stem, label := "newtx_charter_generated_prototype", "vendored NewTX/XCharter"
	if !newtxCharterMode() {
		stem, label = "newtx_charter_cm", "Computer Modern MicroTeX"
	}
	if dir := shotDir(t); dir != "" {
		sheet := corpusSheet(t, images, "NewTX/XCharter reference corpus - "+label)
		savePNG(t, sheet, filepath.Join(dir, stem+"_corpus_png.png"))
	}
	refDir := filepath.Join(qtRepo(), "docs", "math-render-experiments", "2026-07-09-newtx-charter-reference")
	if !isFile(filepath.Join(refDir, "refs-1.png")) || !isFile(filepath.Join(refDir, "refs-2.png")) {
		t.Skipf("optional NewTX/XCharter reference PNGs are not installed in %s", refDir)
	}
}

// qtRepo is the Qt app's repository: KVIT_QT_REPO, or ~/kvit-notes.
func qtRepo() string {
	if dir := os.Getenv("KVIT_QT_REPO"); dir != "" {
		return dir
	}
	home, _ := os.UserHomeDir()
	return filepath.Join(home, "kvit-notes")
}

func TestMalformedExpressionMetricsReportError(t *testing.T) {
	needEngine(t)
	if !Measure("   ", 18, true).Valid {
		t.Error("blank TeX is not valid")
	}
	for _, tex := range []string{"}", "a & b"} {
		m := Measure(tex, 18, true)
		if m.Valid || m.Error == "" {
			t.Errorf("'%s': %+v", tex, m)
		}
	}
}

func TestValidExpressionHasNoError(t *testing.T) {
	needEngine(t)
	for _, tex := range []string{"x^2", `\sum_{i=0}^n i^2`, "   "} {
		if e := ErrorFor(tex); e != "" {
			t.Errorf("'%s': %s", tex, e)
		}
	}
}

// U+202F, U+00A0, U+2009 and U+200A become ASCII spaces before MicroTeX sees
// the TeX, for math typed or edited in the editor.
func TestUnicodeSpacesNormalizeBeforeRender(t *testing.T) {
	needEngine(t)
	unicode := "x + y - z"
	ascii := "x + y - z"
	if e := ErrorFor(unicode); e != "" {
		t.Fatal(e)
	}
	u, a := Measure(unicode, 18, true), Measure(ascii, 18, true)
	if !u.Valid || u.Width != a.Width || u.Height != a.Height {
		t.Errorf("unicode %+v, ascii %+v", u, a)
	}
	// Only Unicode spaces is blank, not an error.
	if e := ErrorFor("  "); e != "" {
		t.Error(e)
	}
}

// MicroTeX is lenient (unknown commands and unclosed groups render as text),
// but tokens that cannot stand where they are raise a parse error, which the
// block shows with the source.
func TestMalformedExpressionReportsError(t *testing.T) {
	needEngine(t)
	if ErrorFor("}") == "" {
		t.Error("a closing brace with no opener should report an error")
	}
	if ErrorFor("a & b") == "" {
		t.Error("an alignment '&' outside array mode should report an error")
	}
}
