package mathtex

import (
	"image"
	"image/draw"
	"image/png"
	"os"
	"path/filepath"
	"testing"

	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// TestCompareWithQt writes, when KVIT_MATH_COMPARE names a PNG file, the
// formulas behind the Qt app's screenshots/math_render_0*.png and its
// reference corpus sheet as the Qt app drew them (left, read from the Qt
// repository's screenshots) beside the same drawn here (right), to be looked
// at side by side.
func TestCompareWithQt(t *testing.T) {
	out := os.Getenv("KVIT_MATH_COMPARE")
	if out == "" {
		t.Skip("KVIT_MATH_COMPARE names no output file")
	}
	needEngine(t)
	qtShots := filepath.Join(qtRepo(), "screenshots")
	var left, right []*image.NRGBA
	for _, c := range canonicalExpressions {
		qt := readPNG(t, filepath.Join(qtShots, c.file))
		goImg, err := Image(c.tex, 48, unison.Black, 1, 0, 0, true)
		if err != nil {
			t.Fatal(err)
		}
		left = append(left, onWhite(qt, 3))
		right = append(right, onWhite(goImg, 3))
	}
	left = append(left, readPNG(t, filepath.Join(qtShots, "newtx_charter_generated_prototype_corpus_png.png")))
	var images []*image.NRGBA
	for _, entry := range newtxCharterReferenceCorpus() {
		img, err := Image(entry.tex, 26, unison.Black, 1, pngVerticalPadding(26), 0, true)
		if err != nil {
			t.Fatal(err)
		}
		images = append(images, img)
	}
	right = append(right, corpusSheet(t, images, "NewTX/XCharter reference corpus - vendored NewTX/XCharter"))

	// Two columns under their headings, row by row, each row as tall as its
	// taller image.
	const margin, gap, headH = 24, 24, 40
	colW := 0
	for i := range left {
		colW = max(colW, left[i].Bounds().Dx(), right[i].Bounds().Dx())
	}
	height := margin + headH
	for i := range left {
		height += max(left[i].Bounds().Dy(), right[i].Bounds().Dy()) + gap
	}
	width := margin*2 + colW*2 + gap
	fs := fonts(t)
	head := func(s string) *text.Layout {
		return fs.Layout([]text.Span{{Text: s, Style: text.Style{Size: 20, Weight: text.Bold, Color: text.Color{A: 255}}}},
			text.Options{})
	}
	page, err := unison.NewImageFromDrawing(width, height, 72, func(gc *unison.Canvas) {
		gc.DrawRect(geom.NewRect(0, 0, float32(width), float32(height)), unison.White.Paint(gc, geom.Rect{}, paintstyle.Fill))
		head("Qt app: MicroTeX painted by QPainter").Draw(gc, margin, margin/2)
		head("Go: the kvitmath library, drawn on a unison canvas").Draw(gc, float32(margin+colW+gap), margin/2)
	})
	if err != nil {
		t.Fatal(err)
	}
	sheet, err := page.ToNRGBA()
	if err != nil {
		t.Fatal(err)
	}
	y := margin + headH
	for i := range left {
		draw.Draw(sheet, left[i].Bounds().Add(image.Pt(margin, y)), left[i], image.Point{}, draw.Over)
		draw.Draw(sheet, right[i].Bounds().Add(image.Pt(margin+colW+gap, y)), right[i], image.Point{}, draw.Over)
		y += max(left[i].Bounds().Dy(), right[i].Bounds().Dy()) + gap
	}
	if err := os.MkdirAll(filepath.Dir(out), 0o755); err != nil {
		t.Fatal(err)
	}
	savePNG(t, sheet, out)
}

func readPNG(t *testing.T, path string) *image.NRGBA {
	t.Helper()
	f, err := os.Open(path)
	if err != nil {
		t.Skipf("the Qt app's image is not there: %v", err)
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		t.Fatal(err)
	}
	out := image.NewNRGBA(img.Bounds().Sub(img.Bounds().Min))
	draw.Draw(out, out.Bounds(), img, img.Bounds().Min, draw.Src)
	return out
}
