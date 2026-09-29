package editor

import (
	"path/filepath"
	"strconv"
	"strings"
	"testing"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// The read-only document, after the core's tests/test_documentview.cpp
// and tests/test_readonlydocument.cpp and what Kvit Works'
// AgentReadOnlyDocument adds over them.

// heldPanel holds a panel at a fixed height, as the pane a document is the
// whole of does.
type heldPanel struct {
	unison.Panel
	h float32
}

func holdAt(h float32, child unison.Paneler) *heldPanel {
	p := &heldPanel{h: h}
	p.Self = p
	p.AddChild(child)
	p.SetLayout(p)
	return p
}

func (p *heldPanel) LayoutSizes(_ *unison.Panel, hint geom.Size) (minSize, prefSize, maxSize geom.Size) {
	return geom.NewSize(10, p.h), geom.NewSize(max(10, hint.Width), p.h), geom.NewSize(unison.DefaultMaxSize, p.h)
}

func (p *heldPanel) PerformLayout(*unison.Panel) {
	p.Children()[0].SetFrameRect(p.ContentRect(false))
}

func openDocument(t *testing.T, o DocumentOptions, md string, height float32) (*uitest.Session, *Document) {
	t.Helper()
	var d *Document
	s := uitest.Open(t, uitest.Options{Width: 700, Height: 700}, func(ui *kvitui.UI) unison.Paneler {
		d = NewDocument(ui, o)
		d.SetMarkdown(md)
		if height > 0 {
			return holdAt(height, d)
		}
		return d
	})
	return s, d
}

func TestADocumentRefusesEveryWrite(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, accessNote, 0)
	e := d.Editor
	before := ""
	s.Do(func() {
		before = Serialize(e.Doc.Blocks)
		e.FocusBlock(1, 4)
		unison.ClipboardSetText("pasted")
	})
	s.Screen.Type("typed")
	for _, k := range []struct {
		key unison.KeyCode
		m   mod.Modifiers
	}{{unison.KeyReturn, mod.None}, {unison.KeyReturn, mod.Shift}, {unison.KeyReturn, mod.Control}, {unison.KeyBackspace, mod.None},
		{unison.KeyDelete, mod.None}, {unison.KeyD, mod.Control}, {unison.KeyTab, mod.None}, {unison.KeyV, mod.Control},
		{unison.KeyB, mod.Control}} {
		s.Screen.KeyPress(k.key, k.m)
	}
	s.Do(func() {
		if got := Serialize(e.Doc.Blocks); got != before {
			t.Errorf("the document changed:\n%s", got)
		}
		if e.showsCaret() || e.bodyLeft() != e.px(focusBar) || e.side() != 0 {
			t.Errorf("caret shown %v, text from %v, side %v", e.showsCaret(), e.bodyLeft(), e.side())
		}
		if _, part := e.partAt(e.gutterCellRect(1, partAdd).Center()); part != partNone {
			t.Errorf("a gutter control answered: %v", part)
		}
	})
}

func TestADocumentIsAsTallAsWhatItDraws(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{Margin: 8}, accessNote, 0)
	s.Do(func() {
		e := d.Editor
		if got, want := d.FrameRect().Height, e.total()+e.px(8); got != want {
			t.Errorf("the document is %v tall, want %v", got, want)
		}
		if e.tops[0] != e.px(8) {
			t.Errorf("the first row is at %v", e.tops[0])
		}
		if d.Scrolls() || d.Region() != nil || d.ViewPosition().Block != -1 {
			t.Error("a document as tall as itself scrolls")
		}
	})
}

func TestTheWheelOverADocumentScrollsThePaneItIsIn(t *testing.T) {
	var d *Document
	var pane *kvitui.Region
	s := uitest.Open(t, uitest.Options{Width: 700, Height: 700}, func(ui *kvitui.UI) unison.Paneler {
		d = NewDocument(ui, DocumentOptions{})
		d.SetMarkdown(strings.Repeat("A paragraph in a pane that scrolls.\n\n", 40))
		pane = kvitui.NewRegion(ui, d)
		return holdAt(300, pane)
	})
	s.Screen.Wheel(s.Screen.PanelCenter(pane), geom.NewPoint(0, -3), mod.None)
	s.Do(func() {
		if _, y := pane.Position(); y <= 0 {
			t.Error("a wheel turned over the document did not reach its pane")
		}
	})
}

func TestASweepSelectsAcrossBlocksAndCopiesMarkdown(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, accessNote, 0)
	var from, to geom.Point
	s.Do(func() {
		// From "Kvit" in the heading to inside "brown" in the paragraph.
		from = d.Editor.TextPoint(0, 11)
		to = d.Editor.TextPoint(1, 14)
	})
	s.Screen.Drag(s.Screen.PanelPoint(d.Editor, from), s.Screen.PanelPoint(d.Editor, to), 6)
	s.Do(func() {
		if !d.HasSelection() {
			t.Fatal("the sweep selected nothing")
		}
		r := d.SelectedRange()
		if r.StartIndex != 0 || r.EndIndex != 1 {
			t.Errorf("range %+v", r)
		}
		md := d.SelectedMarkdown()
		if !strings.HasPrefix(md, "Kvit Notes\n\nThe quick **b") || !strings.HasSuffix(md, "**") {
			t.Errorf("the selection reads %q", md)
		}
		if !d.CopySelection() || unison.ClipboardGetText() != md {
			t.Errorf("copied %q", unison.ClipboardGetText())
		}
		unison.ClipboardSetText("")
	})
	s.Screen.KeyPress(unison.KeyC, mod.Control)
	s.Do(func() {
		if got := unison.ClipboardGetText(); got != d.SelectedMarkdown() {
			t.Errorf("Ctrl+C copied %q", got)
		}
		d.ClearSelection()
		if d.HasSelection() || d.SelectedMarkdown() != "" || d.SelectedRange() != NoRange || d.CopySelection() {
			t.Error("the selection was not dropped")
		}
	})
}

func TestSelectAllTakesTheWholeDocument(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, accessNote, 0)
	s.Do(func() {
		if !d.SelectAll() {
			t.Fatal("nothing to select")
		}
		if got, want := d.SelectedMarkdown()+"\n", Serialize(d.Editor.Doc.Blocks); got != want {
			t.Errorf("select all reads\n%q\nwant\n%q", got, want)
		}
		d.ClearSelection()
		d.Editor.FocusBlock(1, 0)
	})
	// Ctrl+A takes the block's text and then every block, which is a
	// selection of the whole document too.
	s.Screen.KeyPress(unison.KeyA, mod.Control)
	s.Screen.KeyPress(unison.KeyA, mod.Control)
	s.Do(func() {
		if r := d.SelectedRange(); r.StartIndex != 0 || r.EndIndex != d.BlockCount()-1 {
			t.Errorf("Ctrl+A twice selected %+v", r)
		}
		if got, want := d.SelectedMarkdown()+"\n", Serialize(d.Editor.Doc.Blocks); got != want {
			t.Errorf("Ctrl+A twice reads\n%q", got)
		}
		d.SetMarkdown("")
		if d.BlockCount() != 0 || d.SelectAll() || d.HasSelection() {
			t.Errorf("an empty document has %d blocks and selects", d.BlockCount())
		}
	})
	// A document of no blocks is drawn and read without trouble.
	s.Sync()
	s.Screen.Click(s.Screen.PanelCenter(d))
	s.Screen.KeyPress(unison.KeyA, mod.Control)
	s.Do(func() {
		if len(s.Screen.Errors()) != 0 || d.FrameRect().Height != 0 {
			t.Errorf("errors %v, height %v", s.Screen.Errors(), d.FrameRect().Height)
		}
	})
}

func TestADocumentThatScrollsRemembersTheBlockNotTheOffset(t *testing.T) {
	var md strings.Builder
	for i := range 60 {
		md.WriteString("Paragraph " + strconv.Itoa(i) + " of a long file.\n\n")
	}
	s, d := openDocument(t, DocumentOptions{Scrolls: true}, md.String(), 300)
	moved := 0
	d.OnViewMoved = func() { moved++ }
	var at ViewPosition
	s.Do(func() {
		e := d.Editor
		d.Region().ScrollTo(e.tops[20] + 7)
	})
	s.Sync()
	s.Do(func() {
		at = d.ViewPosition()
		if at.Block != 20 || at.Offset != 7 {
			t.Errorf("the view is at %+v", at)
		}
		// The same file read again, and the view elsewhere.
		d.SetMarkdown(md.String())
		d.Region().ScrollTo(0)
	})
	s.Sync()
	s.Do(func() { d.RestoreViewPosition(at) })
	s.Sync()
	s.Do(func() {
		if got := d.ViewPosition(); got != at {
			t.Errorf("restored to %+v, want %+v", got, at)
		}
		if moved == 0 {
			t.Error("scrolling was not told")
		}
		d.RestoreViewPosition(ViewPosition{Block: 30, Offset: 1e6})
		if got := d.ViewPosition(); got.Block != 30 && got.Block != 31 {
			t.Errorf("an offset past the block's height went to %+v", got)
		}
		d.RestoreViewPosition(ViewPosition{Block: 999})
	})
}

func TestADocumentsGeometryIsInItsOwnCoordinates(t *testing.T) {
	var md strings.Builder
	for i := range 40 {
		md.WriteString("Line " + strconv.Itoa(i) + " with **bold** words.\n\n")
	}
	s, d := openDocument(t, DocumentOptions{Scrolls: true}, md.String(), 300)
	var before geom.Rect
	var span string
	s.Do(func() {
		before = d.BlockRect(10)
		span = d.Decorations().AddSpan("works-annotations", 30, 7, 4, SpanWash, unison.Black)
		if d.BlockRect(99).Height != 0 || d.CharRect(99, 0).Height != 0 {
			t.Error("a block the document does not hold has a place")
		}
	})
	s.Do(func() { d.Region().ScrollTo(100) })
	s.Sync()
	s.Do(func() {
		if after := d.BlockRect(10); after.Y != before.Y-100 || after.Height != before.Height {
			t.Errorf("block 10 was at %v and is at %v after scrolling 100", before, after)
		}
		if c := d.CharRect(10, 7); c.Y < d.BlockRect(10).Y || c.Y > d.BlockRect(10).Bottom() {
			t.Errorf("a character of block 10 is at %v, outside %v", c, d.BlockRect(10))
		}
		if !d.RevealSpan(span, 12) {
			t.Fatal("the span was not revealed")
		}
		if r := d.SpanRects(span); len(r) != 1 || r[0].Y != 12 {
			t.Errorf("the revealed span is at %v", r)
		}
		if d.RevealSpan("no-such-id", 0) {
			t.Error("a span the document does not have was revealed")
		}
	})
}

func TestADocumentsPicturesAreFoundInTheFoldersGivenAndOpenedByTheHost(t *testing.T) {
	root := t.TempDir()
	writePicture(t, filepath.Join(root, "notes", "beside.png"))
	writePicture(t, filepath.Join(root, "assets", "rooted.png"))
	writePicture(t, filepath.Join(root, "static", "img", "site.png"))
	var opened []string
	md := "![](beside.png)\n\n![](assets/rooted.png)\n\n![](/img/site.png)\n\n![](missing.png)\n"
	s, d := openDocument(t, DocumentOptions{
		Pictures:    PictureFolders{Base: filepath.Join(root, "notes"), Root: root, Site: filepath.Join(root, "static")},
		OpenPicture: func(path, _ string) { opened = append(opened, path) },
	}, md, 0)
	var spot geom.Rect
	s.Do(func() {
		e := d.Editor
		for i, want := range []bool{true, true, true, false} {
			ref, ok, _ := e.pictureBlock(i)
			if !ok || (e.pictureFor(ref).img != nil) != want {
				t.Errorf("picture %d (%s) found %v, want %v", i, ref.Path, e.pictureFor(ref).img != nil, want)
			}
		}
		spots := d.PictureSpots()
		if len(spots) != 4 || spots[2].Path != "/img/site.png" || spots[1].Block != 1 {
			t.Fatalf("spots %+v", spots)
		}
		spot = spots[1].Rect
	})
	s.Screen.Click(s.Screen.PanelPoint(d, spot.Center()))
	s.Do(func() {
		if len(opened) != 1 || opened[0] != "assets/rooted.png" {
			t.Errorf("the host was asked to open %q", opened)
		}
		if _, _, open := d.Editor.Lightbox(); open {
			t.Error("the editor opened its own view")
		}
	})
}

func TestTwoDocumentsMarkTheirOwnText(t *testing.T) {
	var a, b *Document
	uitest.Open(t, uitest.Options{Width: 700, Height: 700}, func(ui *kvitui.UI) unison.Paneler {
		a, b = NewDocument(ui, DocumentOptions{}), NewDocument(ui, DocumentOptions{})
		a.SetMarkdown("First document")
		b.SetMarkdown("Second document")
		col := kvitui.Column(ui, kvitui.SizeSpace, a, b)
		a.Decorations().AddSpan("works-annotations", 0, 0, 5, SpanWash, unison.Black)
		return col
	}).Do(func() {
		if a.Decorations().SpanCount() != 1 || b.Decorations().SpanCount() != 0 || len(b.SpanRects("span-1")) != 0 {
			t.Error("a mark on one document reached the other")
		}
		if a.Editor.Doc.UndoSteps() != 0 || a.Markdown() != "First document" {
			t.Error("the mark reached the document")
		}
	})
}

// A read-only document spaces a paragraph's lines as the text documents do:
// the line height times the font's own line height, rather than times its
// size.
func TestADocumentSpacesLinesByTheFontsLineHeight(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, accessNote, 0)
	s.Do(func() {
		e := d.Editor
		last := len(e.Doc.Blocks) - 1
		l := e.layout(last)
		if l.lines() < 2 {
			t.Fatal("the paragraph does not wrap")
		}
		_, _, top0, _ := l.text.LineBounds(0)
		_, _, top1, _ := l.text.LineBounds(1)
		st := e.blockStyle(&e.Doc.Blocks[last])
		_, natural := e.ui.Fonts.Layout([]text.Span{{Text: " ", Style: st}}, text.Options{}).Size()
		lh := float32(e.ui.Typography.LineHeight())
		if want := natural * lh; top1-top0 != want {
			t.Errorf("lines are %v apart, want %v (%v × %v)", top1-top0, want, natural, lh)
		}
		if top1-top0 <= st.Size*lh {
			t.Errorf("lines %v apart are no wider than the note's %v", top1-top0, st.Size*lh)
		}
	})
}

// A picture is drawn in the middle of its row unless its block is aligned
// left or right, as the image block draws it.
func TestADocumentCentresItsPictures(t *testing.T) {
	root := t.TempDir()
	writePicture(t, filepath.Join(root, "p.png"))
	md := "![](p.png)\n\n![](p.png)  <!--kvit align=left-->\n\n![](p.png)  <!--kvit align=right-->\n"
	s, d := openDocument(t, DocumentOptions{Pictures: PictureFolders{Base: root}}, md, 0)
	s.Do(func() {
		e := d.Editor
		if e.Doc.Blocks[1].Attrs != "align=left" || e.Doc.Blocks[2].Attrs != "align=right" {
			t.Fatalf("attributes %q, %q", e.Doc.Blocks[1].Attrs, e.Doc.Blocks[2].Attrs)
		}
		o, w := e.textOrigin(0).X, e.textWidth(&e.Doc.Blocks[0])
		centre, left, right := e.pictureRect(0), e.pictureRect(1), e.pictureRect(2)
		if centre.Width >= w || centre.X != o+(w-centre.Width)/2 {
			t.Errorf("the default picture is at %v in text from %v, %v wide", centre, o, w)
		}
		if left.X != o || right.Right() != o+w {
			t.Errorf("left %v and right %v in text from %v to %v", left, right, o, o+w)
		}
		if spots := d.PictureSpots(); spots[0].Rect.X != d.fromEditor(centre).X {
			t.Errorf("the spot %v is not where the picture is drawn", spots[0].Rect)
		}
	})
}
