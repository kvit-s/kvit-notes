package mathtex

import (
	"errors"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/unison"
)

// These tests check what mathtex_test.go does not: finding and loading the
// library, the measure callback, colours, transforms, the cache and the
// optical size. The benchmarks give the numbers in the migration
// log:
//
//	go test -run TestLibraryAndResourcesAreFound -v ./mathtex/
//	go test -run '^$' -bench . -benchmem ./mathtex/

func TestLibraryAndResourcesAreFound(t *testing.T) {
	needEngine(t)
	if filepath.Base(LibraryPath()) != libraryName() {
		t.Errorf("library %s", LibraryPath())
	}
	if !isDir(filepath.Join(ResourceRoot(), "fonts")) {
		t.Errorf("resources %s have no fonts", ResourceRoot())
	}
	t.Logf("found, loaded and initialised %s with %s in %v", LibraryPath(), ResourceRoot(), loadTime)
}

func TestBlankTeXRendersNothing(t *testing.T) {
	needEngine(t)
	f, err := Render("   ", 20, true)
	if err != nil || !f.Valid || f.Width != 0 || len(f.cmds) != 0 {
		t.Errorf("%+v, %v", f, err)
	}
	if img, err := Image("  ", 20, unison.Black, 1, 0, 0, true); img != nil || err != nil {
		t.Errorf("image %v, error %v", img, err)
	}
}

func TestRenderIsCached(t *testing.T) {
	needEngine(t)
	a, _ := Render(`\sqrt{2}`, 20, true)
	b, _ := Render(`\sqrt{2}`, 20, true)
	c, _ := Render(`\sqrt{2}`, 20, false)
	if a != b || a == c {
		t.Error("the cache keys on TeX, size and style")
	}
}

// Text MicroTeX has no math font for is measured through the callback with
// the fonts UseFonts gives, and drawn with them. \Textit sets its argument
// in "Serif" italic at 10 points and scales it to the formula's size; its
// box is the text's advance plus 0.4, times the size over 10
// (TextRenderingBox::init in box/box_single.cpp).
func TestTextWithoutMathFontIsMeasuredAndDrawn(t *testing.T) {
	needEngine(t)
	fs := fonts(t)
	t.Cleanup(func() { UseFonts(nil) })
	const tex, size = `\Textit{Hello}`, 40

	UseFonts(nil)
	img, err := Image(tex, size, unison.Black, 1, 2, 2, true)
	if err != nil {
		t.Fatal(err)
	}
	if hasVisiblePixel(img) {
		t.Error("text was drawn without fonts to draw it with")
	}

	UseFonts(fs)
	f, err := Render(tex, size, true)
	if err != nil {
		t.Fatal(err)
	}
	l := fs.Layout([]text.Span{{Text: "Hello", Style: textStyle("Serif", 2, 10, text.Color{})}}, text.Options{})
	w, _ := l.Size()
	if want := math.Ceil((float64(w) + 0.4) * size / 10); f.Width != want {
		t.Errorf("width %v, the measured text gives %v", f.Width, want)
	}
	if want := float64(l.Baseline()) * size / 10; math.Abs(f.Baseline-want) > 0.01 {
		t.Errorf("baseline %v, the measured text gives %v", f.Baseline, want)
	}
	img, err = Image(tex, size, unison.Black, 1, 2, 2, true)
	if err != nil {
		t.Fatal(err)
	}
	if ink := visibleBounds(img); ink.Dx() < int(f.Width)/2 {
		t.Errorf("ink %v in a formula %v wide", ink, f.Width)
	}
}

// A colour the TeX sets is kept; everything else takes the foreground.
func TestColourSetInTheFormulaIsKept(t *testing.T) {
	needEngine(t)
	img, err := Image(`\textcolor{red}{x} + y`, 40, unison.RGB(0, 0, 255), 1, 2, 2, true)
	if err != nil {
		t.Fatal(err)
	}
	red, blue := 0, 0
	for i := 0; i < len(img.Pix); i += 4 {
		r, b, a := img.Pix[i], img.Pix[i+2], img.Pix[i+3]
		if a == 255 && r > 200 && b < 50 {
			red++
		}
		if a == 255 && b > 200 && r < 50 {
			blue++
		}
	}
	if red == 0 || blue == 0 {
		t.Errorf("%d red and %d blue pixels", red, blue)
	}
}

// Glyphs under a rotation are drawn under it.
func TestRotatedBoxIsDrawnRotated(t *testing.T) {
	needEngine(t)
	flat, err1 := Image(`\mathrm{ABC}`, 40, unison.Black, 1, 0, 0, true)
	turned, err2 := Image(`\rotatebox{90}{\mathrm{ABC}}`, 40, unison.Black, 1, 0, 0, true)
	if err1 != nil || err2 != nil {
		t.Fatal(err1, err2)
	}
	f, t2 := visibleBounds(flat), visibleBounds(turned)
	if f.Dx() <= f.Dy() || t2.Dy() <= t2.Dx() {
		t.Errorf("flat ink %v, turned ink %v", f, t2)
	}
	if math.Abs(float64(f.Dx()-t2.Dy())) > 3 {
		t.Errorf("turned ink %d tall, flat %d wide", t2.Dy(), f.Dx())
	}
}

// The math font's x-height is the box of a math italic x, 117/256 em with its
// small depth.
func TestOpticalMathSize(t *testing.T) {
	needEngine(t)
	for _, c := range []struct {
		size    int
		xHeight float64
		want    int
	}{
		{15, 0, 15},   // no x-height: the text size
		{15, 5, 15},   // never smaller than the text
		{15, 7.5, 16}, // an interface sans-serif's 0.5 em: 15 × 0.5 / 0.457
		{15, 15, 23},  // at most 1.5 times
		{0, 0, 15},    // no size: 15
		{20, 8.8, 20}, // below the math's x-height: the text size
		{40, 20, 44},  // 40 × 0.5 / 0.457 = 43.8
	} {
		if got := OpticalMathSize(c.size, c.xHeight); got != c.want {
			t.Errorf("OpticalMathSize(%d, %v) = %d, want %d", c.size, c.xHeight, got, c.want)
		}
	}
}

func TestTextXHeight(t *testing.T) {
	fs := fonts(t)
	x := TextXHeight(fs, text.Style{Size: 100, Color: text.Color{A: 255}})
	if x < 40 || x > 65 {
		t.Errorf("the interface font's x-height is %v of 100 px", x)
	}
	if TextXHeight(nil, text.Style{Size: 100, Color: text.Color{A: 255}}) != 0 {
		t.Error("an x-height without fonts")
	}
}

const benchTeX = `\int_0^\infty e^{-x^2}\,dx = \frac{\sqrt{\pi}}{2}`

// BenchmarkLayout lays a typical formula out and records its drawing, as
// Render does for a formula not in the cache.
func BenchmarkLayout(b *testing.B) {
	e, err := load()
	if err != nil {
		b.Skip(err)
	}
	for b.Loop() {
		_, data, err := e.layout(benchTeX, 20, true, true)
		if err != nil {
			b.Fatal(err)
		}
		if _, err := parseCommands(data); err != nil {
			b.Fatal(err)
		}
	}
}

// BenchmarkMeasureOnly lays the formula out without recording its drawing.
func BenchmarkMeasureOnly(b *testing.B) {
	e, err := load()
	if err != nil {
		b.Skip(err)
	}
	for b.Loop() {
		if _, _, err := e.layout(benchTeX, 20, true, false); err != nil {
			b.Fatal(err)
		}
	}
}

// BenchmarkDraw draws the formula onto a raster canvas.
func BenchmarkDraw(b *testing.B) {
	f, err := Render(benchTeX, 20, true)
	if err != nil {
		b.Skip(err)
	}
	_, _ = unison.NewImageFromDrawing(int(f.Width)+4, int(f.Height)+4, 72, func(gc *unison.Canvas) {
		for b.Loop() {
			f.Draw(gc, 2, 2, unison.Black)
		}
	})
}

// makeLayout creates the files of a package layout under root: each path
// ending in / is a folder, the others empty files.
func makeLayout(t *testing.T, root string, paths ...string) {
	t.Helper()
	for _, p := range paths {
		full := filepath.Join(root, filepath.FromSlash(p))
		if strings.HasSuffix(p, "/") {
			if err := os.MkdirAll(full, 0o755); err != nil {
				t.Fatal(err)
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(full, nil, 0o644); err != nil {
			t.Fatal(err)
		}
	}
}

// Each package's layout is found where locate's comment says, before this
// repository's own build.
func TestLocateFindsEachPackageLayout(t *testing.T) {
	lib := libraryName()
	noEnv := func(string) string { return "" }
	for _, c := range []struct {
		name          string
		files         []string
		exe           string
		wantLib, want string
	}{
		{"windows installer and zip", []string{"app/" + lib, "app/math-res/fonts/"}, "app",
			"app/" + lib, "app/math-res"},
		{"macOS bundle", []string{"Kvit Notes.app/Contents/MacOS/", "Kvit Notes.app/Contents/Frameworks/" + lib,
			"Kvit Notes.app/Contents/Resources/math-res/fonts/"}, "Kvit Notes.app/Contents/MacOS",
			"Kvit Notes.app/Contents/Frameworks/" + lib, "Kvit Notes.app/Contents/Resources/math-res"},
		{"linux FHS", []string{"usr/bin/", "usr/lib/kvit-notes/" + lib, "usr/share/kvit-notes/math-res/fonts/"},
			"usr/bin", "usr/lib/kvit-notes/" + lib, "usr/share/kvit-notes/math-res"},
		{"beside the program before the bundle", []string{"a/b/" + lib, "a/b/math-res/fonts/", "a/Frameworks/" + lib,
			"a/Resources/math-res/fonts/"}, "a/b", "a/b/" + lib, "a/b/math-res"},
	} {
		t.Run(c.name, func(t *testing.T) {
			root := t.TempDir()
			makeLayout(t, root, c.files...)
			gotLib, gotRes, err := locateFrom(filepath.Join(root, c.exe), root, noEnv)
			if err != nil {
				t.Fatal(err)
			}
			if gotLib != filepath.Join(root, c.wantLib) || gotRes != filepath.Join(root, c.want) {
				t.Errorf("found %s and %s", gotLib, gotRes)
			}
		})
	}
}

func TestLocateOverridesAndFailures(t *testing.T) {
	lib := libraryName()
	root := t.TempDir()
	makeLayout(t, root, "app/"+lib, "app/math-res/fonts/", "other/"+lib, "other/res/fonts/", "repo/mathtex/native/kvitmath.h",
		"repo/third_party/microtex/res/fonts/", "repo/build/"+lib, "repo/dist/pkg/", "empty/")
	env := func(vars map[string]string) func(string) string { return func(k string) string { return vars[k] } }

	gotLib, gotRes, err := locateFrom(filepath.Join(root, "app"), root, env(map[string]string{
		"KVIT_MATH_LIB": filepath.Join(root, "other", lib), "KVIT_MATH_RES": filepath.Join(root, "other", "res")}))
	if err != nil || gotLib != filepath.Join(root, "other", lib) || gotRes != filepath.Join(root, "other", "res") {
		t.Errorf("overrides: %s, %s, %v", gotLib, gotRes, err)
	}
	if _, _, err := locateFrom(filepath.Join(root, "app"), root, env(map[string]string{
		"KVIT_MATH_LIB": filepath.Join(root, "missing")})); err == nil || errors.Is(err, errLibraryMissing) {
		t.Errorf("an override naming nothing: %v", err)
	}
	if _, _, err := locateFrom(filepath.Join(root, "app"), root, env(map[string]string{
		"KVIT_MATH_RES": filepath.Join(root, "empty")})); err == nil {
		t.Error("a resource override without fonts was accepted")
	}

	// The checkout is found from the working directory, and from the folder
	// above the program, but a package staged deeper inside it is not
	// helped by it.
	gotLib, gotRes, err = locateFrom(filepath.Join(root, "empty"), filepath.Join(root, "repo", "mathtex"), env(nil))
	if err != nil || gotLib != filepath.Join(root, "repo", "build", lib) ||
		gotRes != filepath.Join(root, "repo", "third_party", "microtex", "res") {
		t.Errorf("checkout from the working directory: %s, %s, %v", gotLib, gotRes, err)
	}
	if gotLib, _, err = locateFrom(filepath.Join(root, "repo", "build"), root, env(nil)); err != nil ||
		gotLib != filepath.Join(root, "repo", "build", lib) {
		t.Errorf("checkout from build/: %s, %v", gotLib, err)
	}
	_, _, err = locateFrom(filepath.Join(root, "repo", "dist", "pkg"), root, env(nil))
	if !errors.Is(err, errLibraryMissing) {
		t.Errorf("a staged package found %v", err)
	}
	if err != nil && !strings.Contains(err.Error(), filepath.Join(root, "repo", "dist", "pkg", lib)) {
		t.Errorf("the error does not say where it looked: %v", err)
	}
}

func TestSelfTest(t *testing.T) {
	needEngine(t)
	var out strings.Builder
	code := SelfTest(&out)
	t.Log("\n" + out.String())
	if code != 0 || !strings.Contains(out.String(), "selftest: OK (") ||
		!strings.Contains(out.String(), "math-lib: "+LibraryPath()) {
		t.Errorf("exit %d:\n%s", code, out.String())
	}
}

// Text style is set on the whole formula (kvitmath.cpp, parse) rather than by
// wrapping the TeX in \textstyle{…}, which would drop the argument's parse
// errors. For TeX that typesets, the two lay out the same.
func TestTextStyleLaysOutAsTheQtAppsWrapping(t *testing.T) {
	needEngine(t)
	corpus := []string{`x^2`, `E = mc^2`, `\frac{a}{b}`, `\int_0^\infty e^{-x^2}\,dx`, `\sum_{i=1}^{n} i`,
		`\sqrt{x^2+y^2}`, `\left(\frac{1}{2}\right)`, `\lim_{x\to 0} f(x)`, `\alpha\beta\gamma`, `\mathbb{R}^n`}
	for _, tex := range corpus {
		for _, size := range []int{15, 18, 26} {
			got, err1 := Render(tex, size, false)
			qt, err2 := Render(`\textstyle{`+tex+`}`, size, true)
			if err1 != nil || err2 != nil {
				t.Fatalf("%s: %v %v", tex, err1, err2)
			}
			if got.Metrics != qt.Metrics || len(got.cmds) != len(qt.cmds) {
				t.Errorf("%s at %d: %+v, wrapped %+v", tex, size, got.Metrics, qt.Metrics)
			}
		}
	}
	// And the errors are kept: '&' outside an array is one in both styles.
	for _, display := range []bool{true, false} {
		if _, err := Render(`a & b`, 18, display); err == nil {
			t.Errorf("display %v: a & b typeset", display)
		}
	}
}

// A style command whose argument does not parse typesets as nothing; it
// used to read a missing atom and crash the program (core/formula.cpp).
func TestStyleCommandsWithBadArgumentsDoNotCrash(t *testing.T) {
	needEngine(t)
	for _, tex := range []string{`\displaystyle{a & b}`, `\textstyle{a & b}`, `\scriptstyle{a&b}`,
		`\scriptscriptstyle{a&b}`, `\textstyle{}`, `\displaystyle`} {
		for _, display := range []bool{true, false} {
			clearCache()
			if _, err := Render(tex, 18, display); err != nil {
				t.Logf("%s: %v", tex, err)
			}
		}
	}
}
