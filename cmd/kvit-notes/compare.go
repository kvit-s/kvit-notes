package main

// --compare: after the scenarios, stack each screenshot under Kvit's
// reference image of the same name (Kvit on top), so the pairs can be
// looked through side by side.

import (
	"image"
	"image/color"
	"image/draw"
	"image/png"
	"os"
	"path/filepath"
)

func loadPNG(path string) (image.Image, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	return png.Decode(f)
}

func writeComparisons(shots, kvitDir, out string) (int, error) {
	if err := os.MkdirAll(out, 0o755); err != nil {
		return 0, err
	}
	names, _ := filepath.Glob(filepath.Join(shots, "visual_*.png"))
	n := 0
	for _, p := range names {
		name := filepath.Base(p)
		ref, err := loadPNG(filepath.Join(kvitDir, name))
		if err != nil {
			continue // no Kvit reference of that name
		}
		mine, err := loadPNG(p)
		if err != nil {
			return n, err
		}
		w := max(ref.Bounds().Dx(), mine.Bounds().Dx())
		h1, h2 := ref.Bounds().Dy(), mine.Bounds().Dy()
		img := image.NewRGBA(image.Rect(0, 0, w, h1+h2+4))
		draw.Draw(img, img.Bounds(), &image.Uniform{color.RGBA{220, 40, 40, 255}}, image.Point{}, draw.Src)
		draw.Draw(img, image.Rect(0, 0, w, h1), ref, image.Point{}, draw.Src)
		draw.Draw(img, image.Rect(0, h1+4, w, h1+4+h2), mine, image.Point{}, draw.Src)
		f, err := os.Create(filepath.Join(out, name))
		if err != nil {
			return n, err
		}
		err = png.Encode(f, img)
		f.Close()
		if err != nil {
			return n, err
		}
		n++
	}
	return n, nil
}
