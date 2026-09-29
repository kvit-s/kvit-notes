package editor

import (
	"slices"
	"testing"

	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// The decoration registry, after the core's
// tests/test_documentdecorations.cpp and what Kvit Works asks of it: panels
// between blocks and beside lines, washes and outlines over characters, and
// where each is.

// sizedPanel is a decoration panel as tall as it says, which remembers the
// block it was told it is beside.
type sizedPanel struct {
	unison.Panel
	h     float32
	block int
}

func newSizedPanel(h float32) *sizedPanel {
	p := &sizedPanel{h: h, block: -1}
	p.Self = p
	p.SetSizer(func(hint geom.Size) (minSize, prefSize, maxSize geom.Size) {
		return geom.NewSize(1, p.h), geom.NewSize(max(1, hint.Width), p.h), geom.NewSize(unison.DefaultMaxSize, p.h)
	})
	return p
}

func (p *sizedPanel) SetDecorationBlock(block int) { p.block = block }

const decorNote = "One\n\nTwo\n\nThree\n"

func topsOf(s interface{ Do(func()) }, e *Editor) []float32 {
	var out []float32
	s.Do(func() { out = slices.Clone(e.tops) })
	return out
}

func TestAnEmptyRegistryDecoratesNothing(t *testing.T) {
	s, e := openEditor(t, accessNote)
	before := topsOf(s, e)
	s.Do(func() {
		d := e.Decorations()
		if d.Active() || d.MarginColumnReserved() || d.HasSpans() || d.ContainerCount() != 0 ||
			d.MarginItemCount() != 0 || d.SpanCount() != 0 || len(d.ContainersAfter(0)) != 0 ||
			len(d.MarginItemsForBlock(0)) != 0 || len(d.SpansForBlock(0)) != 0 {
			t.Error("a new registry holds something")
		}
		e.Refresh()
	})
	s.Sync()
	if after := topsOf(s, e); !slices.Equal(before, after) {
		t.Errorf("an empty registry moved the rows: %v, then %v", before, after)
	}
}

func TestAContainerIsDrawnAfterItsBlockAndPushesTheRestDown(t *testing.T) {
	s, e := openEditor(t, decorNote)
	before := topsOf(s, e)
	panel := newSizedPanel(50)
	var id string
	changes := 0
	s.Do(func() {
		d := e.Decorations()
		d.OnChange(func() { changes++ })
		id = d.AddContainer("demo", 0, panel)
	})
	s.Sync()
	s.Do(func() {
		d := e.Decorations()
		if id == "" || changes != 1 || d.ContainerCount() != 1 || len(d.ContainersAfter(0)) != 1 || len(d.ContainersAfter(1)) != 0 {
			t.Fatalf("id %q, %d changes, %d containers", id, changes, d.ContainerCount())
		}
		if got := d.ContainersAfter(0)[0]; got.ID != id || got.Owner != "demo" || got.Block != 0 || got.Panel != unison.Paneler(panel) {
			t.Errorf("entry %+v", got)
		}
		if e.tops[1] != before[1]+50 || e.tops[2] != before[2]+50 {
			t.Errorf("rows %v, before %v", e.tops, before)
		}
		want := geom.NewRect(e.side(), e.tops[0]+e.heights[0], e.containerWidth(), 50)
		if got := d.ContainerGeometry(id); got != want || panel.FrameRect() != want {
			t.Errorf("container at %v, want %v", got, want)
		}
		if g := d.BlockGeometry(0); g.Height != e.heights[0]+50 {
			t.Errorf("the block's geometry %v leaves out its container", g)
		}
		if panel.block != 0 {
			t.Errorf("the panel was told block %d", panel.block)
		}
		if len(e.Doc.Blocks) != 3 || Serialize(e.Doc.Blocks) != decorNote || e.Doc.UndoSteps() != 0 {
			t.Error("a container reached the document")
		}
		rev := d.Revision()
		if !d.SetContainerBlock(id, 2) || d.Revision() == rev {
			t.Error("the container did not move")
		}
		rev = d.Revision()
		if !d.SetContainerBlock(id, 2) || d.Revision() != rev || d.SetContainerBlock("no-such-id", 1) {
			t.Error("a move to where it is, or of nothing, counted")
		}
	})
	s.Sync()
	s.Do(func() {
		d := e.Decorations()
		if e.tops[1] != before[1] || e.tops[2] != before[2] || panel.FrameRect().Y != e.tops[2]+e.heights[2] || panel.block != 2 {
			t.Errorf("after the move: rows %v, container %v, told %d", e.tops, panel.FrameRect(), panel.block)
		}
		d.SetContainerBlock(id, 900)
		if !panel.Hidden || !d.ContainerGeometry(id).Empty() || e.tops[2] != before[2] {
			t.Error("a container after a block the document does not have is drawn")
		}
		if !d.RemoveContainer(id) || d.RemoveContainer(id) || panel.Parent() != nil || d.Active() {
			t.Error("the container was not taken out")
		}
	})
}

func TestAContainerThatGrowsMovesTheRowsBelow(t *testing.T) {
	s, e := openEditor(t, decorNote)
	before := topsOf(s, e)
	panel := newSizedPanel(40)
	s.Do(func() { e.Decorations().AddContainer("demo", 0, panel) })
	s.Sync()
	s.Do(func() {
		panel.h = 120
		panel.MarkForLayoutAndRedraw()
	})
	s.Sync()
	s.Sync()
	if after := topsOf(s, e); after[1] != before[1]+120 {
		t.Errorf("the rows did not follow the container: %v, before %v", after, before)
	}
}

// Asking where a block is between a container growing and the rows being
// measured again, as a scroll keeper does on every height change, neither
// moves anything nor keeps the growth from being laid out.
func TestAskingWhereABlockIsDoesNotHideAGrownContainer(t *testing.T) {
	s, e := openEditor(t, decorNote)
	before := topsOf(s, e)
	panel := newSizedPanel(40)
	s.Do(func() { e.Decorations().AddContainer("demo", 0, panel) })
	s.Sync()
	s.Do(func() {
		panel.h = 120
		rect := e.BlockRect(0)
		if b, _ := e.BlockAtY(e.tops[1] + 1); b != 1 || rect.Height != e.heights[0]+40 {
			t.Errorf("before the rows are measured again: block 0 is %v, the row under block 1's top is %d", rect, b)
		}
		e.Decorations().BlockGeometry(0)
		panel.MarkForLayoutAndRedraw()
	})
	s.Sync()
	s.Sync()
	s.Do(func() {
		if e.tops[1] != before[1]+120 || panel.FrameRect().Height != 120 || e.BlockRect(0).Height != e.heights[0]+120 {
			t.Errorf("the grown container is %v and the next row at %v, want 120 and %v", panel.FrameRect(), e.tops[1], before[1]+120)
		}
	})
}

func TestTwoOwnersContributeWithoutColliding(t *testing.T) {
	s, e := openEditor(t, decorNote)
	s.Do(func() {
		d := e.Decorations()
		first := d.AddContainer("first", 0, newSizedPanel(10))
		second := d.AddContainer("second", 0, newSizedPanel(20))
		d.AddMarginItem("second", 0, 0, newSizedPanel(5))
		d.AddSpan("second", 0, 0, 3, SpanWash, unison.Black)
		after := d.ContainersAfter(0)
		if len(after) != 2 || after[0].ID != first || after[1].ID != second {
			t.Fatalf("containers %+v", after)
		}
		e.Refresh()
		if c1, c2 := d.ContainerGeometry(first), d.ContainerGeometry(second); c2.Y != c1.Bottom() {
			t.Errorf("the second container %v is not stacked under the first %v", c2, c1)
		}
		d.RemoveAll("second")
		if after := d.ContainersAfter(0); len(after) != 1 || after[0].ID != first ||
			len(d.MarginItemsForBlock(0)) != 0 || len(d.SpansForBlock(0)) != 0 {
			t.Error("RemoveAll took the wrong entries")
		}
		d.AddMarginItem("b", 1, 1, newSizedPanel(5))
		d.AddSpan("c", 1, 0, 4, SpanWash, unison.Black)
		d.Clear()
		if d.Active() || d.ContainerCount()+d.MarginItemCount()+d.SpanCount() != 0 {
			t.Error("Clear left something")
		}
		if d.AddContainer("demo", 0, nil) != "" || d.AddMarginItem("demo", 0, 0, nil) != "" || d.Active() {
			t.Error("an entry with nothing to draw was registered")
		}
	})
}

func TestAMarginItemSitsBesideItsLine(t *testing.T) {
	s, e := openEditor(t, accessNote)
	last := 0
	var full float32
	s.Do(func() {
		last = len(e.Doc.Blocks) - 1
		full = e.laidWidth
	})
	item := newSizedPanel(5)
	var id string
	reserved := 0
	s.Do(func() {
		d := e.Decorations()
		d.OnChange(func() { reserved++ })
		d.SetMarginColumnReserved(true)
		d.SetMarginColumnReserved(true)
		id = d.AddMarginItem("demo", last, 1, item)
	})
	s.Sync()
	s.Do(func() {
		d := e.Decorations()
		col := d.MarginColumnWidth()
		if reserved != 2 || col <= 0 || d.MarginColumnEms() != 1.5 {
			t.Errorf("%d changes for reserving twice and adding, column %v", reserved, col)
		}
		if e.laidWidth != full-col {
			t.Errorf("the rows are %v wide, want %v less the column %v", e.laidWidth, full, col)
		}
		line := d.LineGeometry(last, 1)
		if line.Y <= d.LineGeometry(last, 0).Y {
			t.Fatalf("line 1 %v is not below line 0", line)
		}
		if got := item.FrameRect(); got != geom.NewRect(e.laidWidth, line.Y, col, line.Height) || item.Hidden || item.block != last {
			t.Errorf("the item is at %v (hidden %v), want beside %v", got, item.Hidden, line)
		}
		if past := d.LineGeometry(last, 1<<20); past.Y <= line.Y {
			t.Errorf("a line past the last %v is not the last", past)
		}
		if div := d.LineGeometry(3, 0); div != e.bodyRect(3) {
			t.Errorf("a divider's line %v is not its row %v", div, e.bodyRect(3))
		}
		if !d.SetMarginItemPosition(id, 0, -3) || d.MarginItemsForBlock(0)[0].Line != 0 {
			t.Error("a line below 0 is not line 0")
		}
		d.SetMarginColumnReserved(false)
		if !item.Hidden || e.laidWidth != full {
			t.Error("giving the column up left the item or the narrower rows")
		}
		if !d.RemoveMarginItem(id) || d.RemoveMarginItem(id) {
			t.Error("the item was not taken out once")
		}
	})
}

// Asking the editor its size with no width, as a row of panes does, keeps
// the width it was laid out at: the margin column is taken off once.
func TestSizingWithNoWidthKeepsTheMarginColumnOnce(t *testing.T) {
	s, e := openEditor(t, decorNote)
	panel := newSizedPanel(40)
	s.Do(func() {
		d := e.Decorations()
		d.SetMarginColumnReserved(true)
		d.AddContainer("demo", 0, panel)
	})
	s.Sync()
	var rows, box float32
	s.Do(func() {
		rows, box = e.laidWidth, panel.FrameRect().Width
		full := e.ContentRect(false).Width
		if rows != full-e.Decorations().MarginColumnWidth() {
			t.Errorf("the rows are %v wide in %v", rows, full)
		}
		for range 3 {
			_, pref, _ := e.Sizes(geom.Size{})
			if pref.Width != full || e.laidWidth != rows || panel.FrameRect().Width != box {
				t.Errorf("sized at %v: rows %v, container %v; want %v, %v, %v", pref.Width, e.laidWidth,
					panel.FrameRect().Width, full, rows, box)
			}
		}
	})
	s.Sync()
	s.Do(func() {
		if e.laidWidth != rows || panel.FrameRect().Width != box {
			t.Errorf("after drawing: rows %v, container %v", e.laidWidth, panel.FrameRect().Width)
		}
	})
}

func TestASpanMarksACharacterRunInDisplayText(t *testing.T) {
	s, e := openEditor(t, accessNote)
	var id string
	s.Do(func() {
		d := e.Decorations()
		// "The quick brown fox": "brown" is display 10 to 15, Markdown 12
		// to 17 behind the two asterisks.
		id = d.AddSpan("demo", 1, 10, 5, SpanWash|SpanOutline, unison.Black)
		if id == "" || !d.HasSpans() || len(d.SpansForBlock(0)) != 0 {
			t.Fatal("the span was not registered")
		}
		got := d.SpansForBlock(1)[0]
		if got.ID != id || got.Start != 10 || got.Length != 5 || got.Style != SpanWash|SpanOutline || got.Color != unison.Black {
			t.Errorf("entry %+v", got)
		}
		rects := d.SpanRects(id)
		if len(rects) != 1 {
			t.Fatalf("%d rectangles for a run on one line", len(rects))
		}
		from, to := e.CharRect(1, 12), e.CharRect(1, 17)
		if rects[0].X != from.X || rects[0].Right() != to.X || rects[0].Y != from.Y {
			t.Errorf("the run is at %v, the word from %v to %v", rects[0], from, to)
		}
	})
	s.Sync()
	// With the caret in the block its markers show, and the run still
	// covers the word, not the markers beside it.
	s.Do(func() { e.FocusBlock(1, 0) })
	s.Sync()
	s.Do(func() {
		r := e.Decorations().SpanRects(id)[0]
		if from, to := e.CharRect(1, 12), e.CharRect(1, 17); r.X != from.X || r.Right() != to.X {
			t.Errorf("with the markers shown the run is at %v, the word from %v to %v", r, from, to)
		}
	})
	// A run across a wrapped paragraph is one rectangle for each line.
	s.Do(func() {
		d := e.Decorations()
		last := len(e.Doc.Blocks) - 1
		wrapped := d.AddSpan("demo", last, 0, 400, SpanOutline, unison.Black)
		if n := len(d.SpanRects(wrapped)); n < 2 || n != e.layout(last).lines() {
			t.Errorf("%d rectangles for a run over %d lines", n, e.layout(last).lines())
		}
		divider := d.AddSpan("demo", 3, 0, 4, SpanWash, unison.Black)
		if len(d.SpanRects(divider)) != 0 {
			t.Error("a run on a divider was drawn")
		}
	})
}

func TestASpanMovesAndIsRestyledByItsID(t *testing.T) {
	s, e := openEditor(t, accessNote)
	s.Do(func() {
		d := e.Decorations()
		id := d.AddSpan("demo", 0, 4, 3, SpanWash, unison.Black)
		rev := d.Revision()
		if !d.SetSpanRange(id, 1, 10, 6) || d.Revision() == rev || len(d.SpansForBlock(0)) != 0 {
			t.Error("the span did not move")
		}
		rev = d.Revision()
		if !d.SetSpanRange(id, 1, 10, 6) || d.Revision() != rev {
			t.Error("a move to where it is counted")
		}
		if !d.SetSpanStyle(id, SpanOutline, unison.White) || d.SpansForBlock(1)[0].Style != SpanOutline {
			t.Error("the span was not restyled")
		}
		if !d.SetSpanRange(id, 1, 10, 0) || len(d.SpanRects(id)) != 0 {
			t.Error("a span may go to no characters, and then draws nothing")
		}
		if d.SetSpanRange("no-such-id", 0, 0, 1) || d.SetSpanStyle("no-such-id", SpanWash, unison.Black) {
			t.Error("an unknown id moved")
		}
		if !d.RemoveSpan(id) || d.RemoveSpan(id) || d.HasSpans() {
			t.Error("the span was not removed once")
		}
		if d.AddSpan("demo", 0, 0, 4, 0, unison.Black) != "" || d.AddSpan("demo", 0, 0, 4, SpanWash, unison.Transparent) != "" ||
			d.AddSpan("demo", 0, 0, 0, SpanWash, unison.Black) != "" || d.Active() {
			t.Error("a span with nothing to draw was registered")
		}
	})
}

func TestAWashIsPaintedOverTheCharactersNamed(t *testing.T) {
	s, e := openEditor(t, accessNote)
	var at geom.Point
	s.Do(func() {
		r := e.CharRect(1, 12)
		at = geom.NewPoint(r.X+1, r.Y+1)
	})
	pixel := func() (r, g, b uint32) {
		p := s.Screen.PanelPoint(e, at)
		img := s.Capture()
		sc := s.Screen.Scale()
		r, g, b, _ = img.At(int(p.X*sc), int(p.Y*sc)).RGBA()
		return r, g, b
	}
	r0, g0, b0 := pixel()
	s.Do(func() { e.Decorations().AddSpan("demo", 1, 10, 5, SpanWash, SpanColor(e.tok().Danger, 0.8)) })
	s.Sync()
	if r1, g1, b1 := pixel(); r1 == r0 && g1 == g0 && b1 == b0 {
		t.Error("the wash changed nothing under the word")
	}
}

func TestADecorationKeepsItsKeysAndPresses(t *testing.T) {
	s, e := openEditor(t, decorNote)
	panel := newSizedPanel(60)
	panel.SetFocusable(true)
	s.Do(func() {
		e.Decorations().AddContainer("demo", 0, panel)
		e.FocusBlock(2, 1)
	})
	s.Sync()
	var box geom.Rect
	s.Do(func() { box = panel.FrameRect() })
	s.Screen.Click(s.Screen.PanelPoint(e, box.Center()))
	s.Do(func() {
		if e.Doc.Caret != (Pos{e.Doc.Blocks[2].ID, 1}) {
			t.Errorf("a press on the container moved the caret to %v", e.Doc.Caret)
		}
		panel.RequestFocus()
	})
	s.Screen.Type("xy")
	s.Screen.KeyPress(unison.KeyBackspace, mod.None)
	s.Screen.KeyPress(unison.KeyReturn, mod.None)
	s.Screen.KeyPress(unison.KeyZ, mod.Control)
	s.Do(func() {
		if got := Serialize(e.Doc.Blocks); got != decorNote {
			t.Errorf("keys meant for the container edited the document:\n%s", got)
		}
	})
}
