package app

// Image optimisation (PARITY 11.2): large pictures are downscaled to the
// display width on load, decoded pictures are kept over a byte budget, and
// loading is lazy (only when drawn).

import (
	"testing"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

func TestLargePicturesAreDownscaledOnLoad(t *testing.T) {
	nw, nh, ok := scaledSize(3000, 1500)
	if !ok || nw != maxImageWidth || nh != 700 {
		t.Errorf("scaled 3000x1500 = %dx%d ok=%v, want %dx700 true", nw, nh, ok, maxImageWidth)
	}
	if _, _, ok := scaledSize(100, 50); ok {
		t.Error("a small picture must keep its size")
	}
	if _, _, ok := scaledSize(maxImageWidth, 800); ok {
		t.Error("a picture at the limit must keep its size")
	}
	nw, nh, ok = scaledSize(5000, 10)
	if !ok || nw != maxImageWidth || nh < 1 {
		t.Errorf("a thin panorama must keep a line: %dx%d ok=%v", nw, nh, ok)
	}
}

type stubPicture struct{ w, h float32 }

func (d stubPicture) LogicalSize() geom.Size { return geom.NewSize(d.w, d.h) }
func (d stubPicture) DrawInRect(*unison.Canvas, geom.Rect, *unison.SamplingOptions, *unison.Paint) {
}

func TestImageCacheEvictsOverBudget(t *testing.T) {
	c := &imageCache{}
	// Each entry reports 32 MB; the third must evict the oldest.
	c.add("a", stubPicture{w: 10, h: 10}, 32<<20)
	c.add("b", stubPicture{w: 10, h: 10}, 32<<20)
	c.add("c", stubPicture{w: 10, h: 10}, 32<<20)
	if _, ok := c.get("a"); ok {
		t.Error("over budget: the oldest entry should be evicted")
	}
	if _, ok := c.get("c"); !ok {
		t.Error("the newest entry must stay")
	}
	if _, ok := c.get("b"); !ok {
		t.Error("only the oldest entry should go")
	}
}
