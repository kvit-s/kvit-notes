package diagram

// Not a port: the Qt tests do not check the cache's bound or calls from
// several threads at once, which Render promises, nor the colour of a
// sequence diagram's rect block.

import (
	"fmt"
	"reflect"
	"sync"
	"testing"

	"github.com/kvit-s/kvit-notes/mermaid"
)

func TestRenderCacheKeepsTheNewest(t *testing.T) {
	ClearCache()
	for i := range renderCacheSize + 10 {
		Render(fmt.Sprintf("flowchart LR\nA%d-->B", i), testOpts())
	}
	if n := CacheCount(); n != renderCacheSize {
		t.Errorf("cache count = %d, want %d", n, renderCacheSize)
	}
	// Another font is another entry.
	o := testOpts()
	o.FontSize = 20
	Render("flowchart LR\nA0-->B", o)
	if n := CacheCount(); n != renderCacheSize {
		t.Errorf("cache count = %d, want %d", n, renderCacheSize)
	}
	ClearCache()
	if n := CacheCount(); n != 0 {
		t.Errorf("cache count after clearing = %d", n)
	}
}

func TestRenderResultIsTheCallersOwn(t *testing.T) {
	ClearCache()
	src := "flowchart LR\nA-->B-->C"
	r1 := Render(src, testOpts())
	r1.Scene.Shapes[0].NodeID = "changed"
	r1.Scene.Paths[0].Outline.Segs[0].Pts[0] = Point{-1, -1}
	r2 := Render(src, testOpts())
	if r2.Scene.Shapes[0].NodeID != "A" || r2.Scene.Paths[0].Outline.Segs[0].Pts[0] == (Point{-1, -1}) {
		t.Error("changing a result changed the cache")
	}
}

func TestRenderFromManyGoroutines(t *testing.T) {
	ClearCache()
	sources := []string{
		"flowchart TD\nA-->B\nB-->C\nC-->A",
		"sequenceDiagram\nA->>B: hi\nB-->>A: bye",
		"classDiagram\nA <|-- B",
		"stateDiagram-v2\n[*] --> S\nS --> [*]",
		"erDiagram\nA ||--o{ B : has",
	}
	want := make([]RenderResult, len(sources))
	for i, src := range sources {
		want[i] = compute(src, prepared(testOpts()))
	}
	var wg sync.WaitGroup
	errs := make(chan string, 64)
	for g := range 16 {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for k := range 20 {
				i := (g + k) % len(sources)
				if got := Render(sources[i], testOpts()); !reflect.DeepEqual(got.Scene, want[i].Scene) {
					errs <- fmt.Sprintf("source %d laid out differently", i)
					return
				}
			}
		}()
	}
	wg.Wait()
	close(errs)
	for e := range errs {
		t.Error(e)
	}
}

// Not a port: the colour of a sequence diagram's rect block, as the Qt
// app's parseCssColor reads it.
func TestRectBlockColour(t *testing.T) {
	for in, want := range map[string]mermaid.Color{
		"rgb(200, 150, 255)":   {R: 200, G: 150, B: 255, A: 255, Set: true},
		"RGBA(0,0,255,0.5)":    {R: 0, G: 0, B: 255, A: 128, Set: true},
		"rgba(0,0,255,nan)":    {R: 0, G: 0, B: 255, A: 0, Set: true},
		"rgba(0,0,255,0x1p-1)": {R: 0, G: 0, B: 255, A: 255, Set: true},
		"rgb(300, 0, 0)":       {},
		"rgb(1, 2)":            {},
		"  transparent ":       {},
		"":                     {},
		"#eef":                 {R: 0xee, G: 0xee, B: 0xff, A: 255, Set: true},
		"aqua":                 {R: 0, G: 255, B: 255, A: 255, Set: true},
	} {
		if got := parseCSSColor(in); got != want {
			t.Errorf("parseCSSColor(%q) = %+v, want %+v", in, got, want)
		}
	}
}
