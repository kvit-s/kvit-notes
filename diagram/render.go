package diagram

// Parsing and layout behind a cache. Render is safe to call from any
// goroutine, so the editor can lay a diagram out away from the event loop.
// Results are kept by the source, the font, the direction and whether
// formulas are typeset, in one bounded cache for the process that drops the
// result used longest ago, so scrolling back to a diagram, or drawing it
// again after an unrelated edit, lays nothing out. Layout does not depend
// on the width the diagram is shown at, so the width is not part of the key.

import (
	"container/list"
	"crypto/sha1"
	"fmt"
	"sync"

	"github.com/kvit-s/kvit-notes/mermaid"
)

// RenderResult is what Render makes of a fence's text.
type RenderResult struct {
	// Valid is set when the source parsed without errors into a diagram with
	// something in it. The editor shows a new scene only while Valid is
	// set, and keeps the last valid one otherwise, so a typo in the middle
	// of an edit does not replace the working diagram with the fragment that
	// happened to parse.
	Valid             bool
	UnsupportedFamily bool   // a family Kvit can name but does not draw
	FamilyName        string // the header's keyword, for that message
	Family            mermaid.DiagramType
	HasArrangement    bool // a flowchart with a `%% mermaid-flow:pos` line
	Diagnostics       []mermaid.Diagnostic
	FirstError        mermaid.Diagnostic
	HasError          bool
	Scene             Scene
}

// renderCacheSize is how many results the cache keeps. Scenes are small, so
// the cache counts results rather than bytes.
const renderCacheSize = 96

type renderKey [sha1.Size]byte

type renderEntry struct {
	key    renderKey
	result RenderResult
}

// renderCache is the process's cache: a list from the result used last to
// the one used longest ago, and the list's elements by key.
var renderCache struct {
	sync.Mutex
	order *list.List
	byKey map[renderKey]*list.Element
}

func cacheKey(source string, opts LayoutOptions) renderKey {
	h := sha1.New()
	h.Write([]byte(source))
	h.Write([]byte(opts.FontFamily))
	fmt.Fprintf(h, "|%g|%d|%t", opts.FontSize, opts.Direction, opts.Math != nil)
	var k renderKey
	h.Sum(k[:0])
	return k
}

// Render parses source and lays out the diagram it holds, through the
// cache. The returned scene shares nothing with the cache, so the caller
// may change it. opts.Direction is not used for a flowchart, which is laid
// out in the direction its header names; the other families ignore it too,
// but it is part of the cache's key.
//
// FontFamily must name the font Measure measures in, because the cache
// tells two fonts apart by it, and Measure and Math must be safe to call
// from the goroutine Render runs on.
func Render(source string, opts LayoutOptions) RenderResult {
	opts = prepared(opts)
	key := cacheKey(source, opts)
	renderCache.Lock()
	if renderCache.order != nil {
		if el, ok := renderCache.byKey[key]; ok {
			renderCache.order.MoveToFront(el)
			hit := el.Value.(*renderEntry).result
			renderCache.Unlock()
			return hit.clone()
		}
	}
	renderCache.Unlock()

	result := compute(source, opts)

	renderCache.Lock()
	defer renderCache.Unlock()
	if renderCache.order == nil {
		renderCache.order = list.New()
		renderCache.byKey = map[renderKey]*list.Element{}
	}
	if el, ok := renderCache.byKey[key]; ok {
		// Another goroutine laid the same source out meanwhile.
		renderCache.order.MoveToFront(el)
		el.Value.(*renderEntry).result = result.clone()
	} else {
		renderCache.byKey[key] = renderCache.order.PushFront(&renderEntry{key, result.clone()})
		for renderCache.order.Len() > renderCacheSize {
			oldest := renderCache.order.Back()
			renderCache.order.Remove(oldest)
			delete(renderCache.byKey, oldest.Value.(*renderEntry).key)
		}
	}
	return result
}

// ClearCache empties the cache.
func ClearCache() {
	renderCache.Lock()
	defer renderCache.Unlock()
	renderCache.order, renderCache.byKey = nil, nil
}

// CacheCount is the number of results in the cache.
func CacheCount() int {
	renderCache.Lock()
	defer renderCache.Unlock()
	if renderCache.order == nil {
		return 0
	}
	return renderCache.order.Len()
}

// compute parses and lays out without the cache.
func compute(source string, opts LayoutOptions) RenderResult {
	pr := mermaid.Parse(source)
	r := RenderResult{
		Diagnostics:    pr.Diagnostics,
		FamilyName:     pr.FamilyName,
		Family:         pr.Type,
		HasArrangement: pr.Flowchart.HasPosLine,
		HasError:       pr.HasErrors(),
	}
	if r.HasError {
		r.FirstError = pr.FirstError()
	}
	clean := !r.HasError
	switch {
	case pr.Type == mermaid.Flowchart:
		o := opts
		o.Direction = pr.Flowchart.Direction
		r.Scene = LayoutFlowchart(&pr.Flowchart, o)
		r.Valid = clean && len(pr.Flowchart.Nodes) > 0
	case pr.Type == mermaid.Sequence && pr.Supported:
		r.Scene = LayoutSequence(&pr.Sequence, opts)
		r.Valid = clean && len(pr.Sequence.Participants) > 0
	case pr.Type == mermaid.Class && pr.Supported:
		r.Scene = LayoutClass(&pr.Class, opts)
		r.Valid = clean && len(pr.Class.Classes) > 0
	case pr.Type == mermaid.State && pr.Supported:
		r.Scene = LayoutState(&pr.State, opts)
		r.Valid = clean && len(pr.State.States) > 0
	case pr.Type == mermaid.Er && pr.Supported:
		r.Scene = LayoutEr(&pr.Er, opts)
		r.Valid = clean && len(pr.Er.Entities) > 0
	default:
		r.UnsupportedFamily = pr.Type != mermaid.Unknown && !pr.Supported
	}
	return r
}

// clone is a copy of the result that shares no slice with r.
func (r RenderResult) clone() RenderResult {
	r.Diagnostics = append([]mermaid.Diagnostic(nil), r.Diagnostics...)
	r.Scene = r.Scene.Clone()
	return r
}

// Clone is a copy of the scene that shares nothing with s.
func (s Scene) Clone() Scene {
	c := s
	c.Groups = append([]Group(nil), s.Groups...)
	c.Shapes = append([]Shape(nil), s.Shapes...)
	c.Texts = append([]Text(nil), s.Texts...)
	c.Paths = make([]Path, len(s.Paths))
	for i, p := range s.Paths {
		p.Outline = p.Outline.Clone()
		c.Paths[i] = p
	}
	if s.Paths == nil {
		c.Paths = nil
	}
	return c
}
