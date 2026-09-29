package main

// --bench: timings on a long note, to compare with the figures in Kvit's
// selection.md "Sizing" and with the Shirei prototype's: 1,237 blocks of
// Kvit's own documentation, opened, scrolled a wheel notch at a time, and
// typed into, on unison's headless screen with its software renderer.

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// longNote is Kvit Notes' own documentation, from its  repository.
func longNote(dir string) string {
	var parts []string
	for _, f := range []string{"features.md", "block-arch.md", "selection.md", "devel.md", "accessibility.md"} {
		if b, err := os.ReadFile(filepath.Join(dir, f)); err == nil {
			parts = append(parts, string(b))
		}
	}
	return strings.Join(parts, "\n\n")
}

func median(ds []time.Duration) time.Duration {
	s := slices.Clone(ds)
	slices.Sort(s)
	return s[len(s)/2]
}

func runBench(docs string) error {
	blocks := editor.ParseMarkdown(longNote(docs))
	if len(blocks) == 0 {
		return fmt.Errorf("no Kvit documentation found in %s", docs)
	}
	// Kvit's own measurement used 1,237 blocks.
	blocks = blocks[:min(len(blocks), 1237)]
	dr, err := startDriver("", "", storyWidth, storyHeight)
	if err != nil {
		return err
	}
	defer dr.stop()
	fmt.Printf("blocks: %d\n", len(blocks))

	t0 := time.Now()
	dr.do(func() { dr.ed().SetDoc(editor.NewDoc(blocks)) })
	fmt.Printf("open (parsing excluded): every row measured, the first screen laid out and drawn: %v\n", time.Since(t0).Round(time.Millisecond))

	centre := geom.NewPoint(storyWidth/2, storyHeight/2)
	dr.screen.MouseMove(centre, mod.None)
	var steps []time.Duration
	for range 60 {
		t := time.Now()
		dr.screen.Wheel(centre, geom.NewPoint(0, -1), mod.None)
		steps = append(steps, time.Since(t))
	}
	var y float32
	dr.do(func() { _, y = dr.n.region.Position() })
	fmt.Printf("wheel notch, scrolled, laid out and drawn: median %v (60 notches moved %.0f px)\n", median(steps).Round(10*time.Microsecond), y)

	mid := len(blocks) / 2
	dr.do(func() {
		d := dr.doc()
		for mid < len(d.Blocks) && d.Blocks[mid].Kind != editor.Paragraph {
			mid++
		}
	})
	dr.focus(mid, 0)
	steps = steps[:0]
	for range 40 {
		t := time.Now()
		dr.screen.KeyPress(unison.KeyX, mod.None)
		steps = append(steps, time.Since(t))
	}
	fmt.Printf("keystroke in block %d, applied, laid out and drawn: median %v\n", mid+1, median(steps).Round(10*time.Microsecond))
	var ms runtime.MemStats
	runtime.GC()
	runtime.ReadMemStats(&ms)
	fmt.Printf("Go heap in use after a collection: %.0f MB\n", float64(ms.HeapInuse)/(1<<20))
	return nil
}
