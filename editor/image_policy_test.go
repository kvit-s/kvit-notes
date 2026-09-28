package editor

// Remote pictures need their origin approved (PARITY 10.3): nothing remote
// loads on sight, the card offers Load from its origin, and a press on it
// approves the origin and loads the picture.

import (
	"strings"
	"testing"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

type stubPolicy struct {
	allowed map[string]bool
}

func (s *stubPolicy) IsAllowed(url string) bool { return s.allowed[originForTest(url)] }
func (s *stubPolicy) Allow(url string)          { s.allowed[originForTest(url)] = true }
func (s *stubPolicy) CanRequest(url string) bool {
	return strings.HasPrefix(url, "https://") || strings.HasPrefix(url, "http://")
}
func (s *stubPolicy) Refusal(url string) string {
	if s.CanRequest(url) {
		return ""
	}
	return "Only http and https addresses load"
}
func (s *stubPolicy) Origin(url string) string { return originForTest(url) }

func originForTest(url string) string {
	// Minimal origin for the test: scheme + host.
	rest := strings.TrimPrefix(strings.TrimPrefix(url, "https://"), "http://")
	if i := strings.IndexAny(rest, "/?#"); i >= 0 {
		rest = rest[:i]
	}
	if strings.HasPrefix(url, "https://") {
		return "https://" + rest
	}
	return "http://" + rest
}

type stubDrawable struct{ w, h float32 }

func (d stubDrawable) LogicalSize() geom.Size { return geom.NewSize(d.w, d.h) }
func (d stubDrawable) DrawInRect(*unison.Canvas, geom.Rect, *unison.SamplingOptions, *unison.Paint) {
}

func TestRemotePictureNeedsApprovalThenLoads(t *testing.T) {
	s, e := openEditor(t, "![](https://example.com/pic.png)")
	policy := &stubPolicy{allowed: map[string]bool{}}
	var loaded string
	s.Do(func() {
		e.RemotePolicy = policy
		e.LoadImage = func(path string) (unison.Drawable, error) {
			loaded = path
			return stubDrawable{w: 100, h: 50}, nil
		}
		// The open laid the note out before the policy was set and cached
		// the unapproved card; drop it so the policy is read.
		e.ForgetPicture("https://example.com/pic.png")
	})
	var failed string
	s.Do(func() { failed = e.pictureFor(ImageRef{Path: "https://example.com/pic.png", Remote: true}).failed })
	if !strings.Contains(failed, "Load from https://example.com") {
		t.Fatalf("unapproved remote should offer Load, got %q", failed)
	}
	if loaded != "" {
		t.Fatalf("nothing may load before approval, loaded %q", loaded)
	}
	var origin string
	var needs bool
	s.Do(func() {
		origin, needs = e.pictureNeedsApproval(ImageRef{Path: "https://example.com/pic.png", Remote: true})
	})
	if !needs || origin != "https://example.com" {
		t.Fatalf("needs approval from its origin, got %q %v", origin, needs)
	}
	// A press on the card approves the origin and loads the picture.
	s.Do(func() {
		e.RemotePolicy.Allow("https://example.com/pic.png")
		e.ForgetPicture("https://example.com/pic.png")
	})
	s.Do(func() {
		p := e.pictureFor(ImageRef{Path: "https://example.com/pic.png", Remote: true})
		if p.img == nil {
			t.Fatalf("approved origin should load, got %q", p.failed)
		}
	})
	if loaded != "https://example.com/pic.png" {
		t.Errorf("loaded %q", loaded)
	}
}
