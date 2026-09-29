package editor

// A Markdown document drawn by the editor where nothing can change it: a
// file on Kvit Works' stage, a conversation's transcript, the review's
// merged document, an agent's report (Qt core/qml/DocumentView.qml under
// Kvit Works' qml/agent/AgentReadOnlyDocument.qml). It is the editor, so a
// table is a table, a diagram is a drawing and an equation is set, with
// every edit refused, no gutter, no caret and no hover tint. The reader can
// sweep a selection across it and copy it out as Markdown.
//
// It is one of two sizes. By default it is as tall as its document, for a
// pane of the caller's that scrolls it along with other things; a wheel
// turned over it scrolls that pane. With Scrolls it is as tall as the caller
// makes it and scrolls the document itself, and it can say which block the
// reader is at and put them back there.

import (
	"time"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// DocumentOptions say how a Document is made.
type DocumentOptions struct {
	// Scrolls makes the document scroll itself inside the height the caller
	// gives it (Qt growsWithDocument false), rather than be as tall as its
	// document.
	Scrolls bool
	// Margin is the space between the document's edges and its text, in
	// design pixels. Kvit Works' surfaces use 0, since each is inside a pane
	// with margins of its own; Qt's DocumentView uses 8.
	Margin float32
	// BlockSpacing is the space between blocks in design pixels; 0 keeps the
	// reader's paragraph spacing.
	BlockSpacing int
	// Pictures are the folders the document's pictures are looked up in.
	Pictures PictureFolders
	// OpenPicture, when set, is asked to show a picture a press landed on,
	// in place of the editor's own full-size view (Kvit Works'
	// PictureOpeners.qml opens AgentImagePreview).
	OpenPicture func(path, alt string)
}

// ViewPosition is where the reader of a document that scrolls itself is:
// the block at the top of the view and how far its top is above the view's
// top. A block is the same block however the rows above it were measured,
// which a distance from the top of the document is not.
type ViewPosition struct {
	Block  int
	Offset float32
}

// Document is a read-only document drawn by the editor.
type Document struct {
	unison.Panel
	// Editor draws the document. Its Doc is read-only; a host may set its
	// OnChange, FollowLink, CopyRich and the other callbacks a note sets.
	Editor *Editor
	// OnViewMoved runs after the reader scrolls a document that scrolls
	// itself, for a host that remembers where they were.
	OnViewMoved func()

	region   *kvitui.Region
	markdown string
	lastY    float32
	pending  *ViewPosition
}

// NewDocument makes an empty read-only document.
func NewDocument(ui *kvitui.UI, o DocumentOptions) *Document {
	d := &Document{}
	d.Self = d
	e := New(ui, readOnlyDoc(""))
	e.Placeholder = ""
	e.Accessibility.Name = "Document"
	e.LoadImage = o.Pictures.Loader()
	e.Embed(Embedding{NoGutter: true, Margin: o.Margin, BlockSpacing: o.BlockSpacing, ReadOnlyLook: true,
		CopyFragments: true, OpenPicture: o.OpenPicture, FontLineSpacing: true, CentredPictures: true})
	d.Editor = e
	if o.Scrolls {
		d.region = kvitui.NewRegion(ui, e)
		d.region.Padding = kvitui.Px(0)
		d.AddChild(d.region)
	} else {
		d.AddChild(e)
	}
	d.SetLayout(documentLayout{d})
	d.DrawCallback = d.watch
	return d
}

func readOnlyDoc(md string) *Doc {
	return &Doc{Blocks: ParseMarkdown(md), Now: time.Now, ReadOnly: true}
}

// SetMarkdown replaces the document, dropping any selection.
func (d *Document) SetMarkdown(md string) {
	d.markdown = md
	d.Editor.SetDoc(readOnlyDoc(md))
}

// Markdown is the document as it was last set.
func (d *Document) Markdown() string { return d.markdown }

// Scrolls reports whether the document scrolls itself.
func (d *Document) Scrolls() bool { return d.region != nil }

// Region is the region a document that scrolls itself scrolls in, nil for
// one as tall as its document.
func (d *Document) Region() *kvitui.Region { return d.region }

// BlockCount is how many blocks the document has.
func (d *Document) BlockCount() int { return len(d.Editor.Doc.Blocks) }

// BlockMarkdown is one block written as Markdown, "" for an index the
// document does not hold.
func (d *Document) BlockMarkdown(i int) string {
	blocks := d.Editor.Doc.Blocks
	if i < 0 || i >= len(blocks) {
		return ""
	}
	return BlockMarkdown(blocks[i], ListNumber(blocks, i))
}

// Decorations is the document's own decorations: washes over the words a
// comment is on, marks beside blocks.
func (d *Document) Decorations() *Decorations { return d.Editor.Decorations() }

// The selection. The reader makes it by sweeping the pointer, by double or
// triple clicking, or by Ctrl+A, which selects a block's text and then every
// block.

// HasSelection reports whether anything is selected.
func (d *Document) HasSelection() bool { return d.Editor.HasSelection() }

// SelectedRange is the selection as block indexes and Markdown offsets,
// NoRange when nothing is selected.
func (d *Document) SelectedRange() TextRange { return d.Editor.SelectionRange() }

// SelectedMarkdown is the selection as Markdown that stands on its own
// (Doc.RangeMarkdown), "" when nothing is selected.
func (d *Document) SelectedMarkdown() string { return d.Editor.SelectionMarkdown() }

// SelectAll selects the whole document as text, and reports false when
// there is nothing to select.
func (d *Document) SelectAll() bool {
	e := d.Editor
	blocks := e.Doc.Blocks
	if len(blocks) == 0 {
		return false
	}
	e.clearBlockSel()
	last := blocks[len(blocks)-1]
	e.Doc.Anchor = Pos{blocks[0].ID, 0}
	e.Doc.Caret = Pos{last.ID, len(runes(last.Text))}
	e.Doc.Focused = true
	e.changed()
	return d.HasSelection()
}

// ClearSelection drops the selection.
func (d *Document) ClearSelection() { d.Editor.ClearSelection() }

// CopySelection puts the selection on the clipboard as Markdown, with HTML
// beside it when the editor's CopyRich is set, and reports false when
// nothing was selected.
func (d *Document) CopySelection() bool {
	md := d.SelectedMarkdown()
	if md == "" {
		return false
	}
	d.Editor.copyMarkdown(md)
	return true
}

// Geometry, in the document's own coordinates: what a host laying bands,
// marks, torn edges or buttons over the document places them by. Every row
// is measured, so a block scrolled out of view still has its place.

// BlockRect is where block i is drawn, with the containers after it; empty
// for an index the document does not hold.
func (d *Document) BlockRect(i int) geom.Rect {
	r := d.Editor.BlockRect(i)
	if r.Empty() {
		return r
	}
	return d.fromEditor(r)
}

// CharRect is where a caret before Markdown offset off of block i would be
// drawn: beside the character, one line tall.
func (d *Document) CharRect(i, off int) geom.Rect {
	r := d.Editor.CharRect(i, off)
	if r.Height == 0 {
		return r
	}
	return d.fromEditor(r)
}

// SpanRects is where a span of the document's decorations is painted, one
// rectangle for each line it crosses.
func (d *Document) SpanRects(id string) []geom.Rect {
	var out []geom.Rect
	for _, r := range d.Decorations().SpanRects(id) {
		out = append(out, d.fromEditor(r))
	}
	return out
}

// PictureSpots are the document's pictures and where they are drawn, for a
// host that lays a button over each.
func (d *Document) PictureSpots() []PictureSpot {
	spots := d.Editor.PictureSpots()
	for k := range spots {
		spots[k].Rect = d.fromEditor(spots[k].Rect)
	}
	return spots
}

// fromEditor turns a rectangle in the editor's coordinates into the
// document's.
func (d *Document) fromEditor(r geom.Rect) geom.Rect {
	for p := d.Editor.AsPanel(); p != nil && p != d.AsPanel(); p = p.Parent() {
		r.Point = r.Point.Add(p.FrameRect().Point)
	}
	return r
}

// Where the reader is, in a document that scrolls itself.

// ViewPosition is the block at the top of the view and how far into it the
// view starts; Block is -1 for a document that does not scroll itself or
// has no blocks.
func (d *Document) ViewPosition() ViewPosition {
	if d.region == nil {
		return ViewPosition{Block: -1}
	}
	_, y := d.region.Position()
	b, off := d.Editor.BlockAtY(y)
	return ViewPosition{Block: b, Offset: off}
}

// RestoreViewPosition puts the view back where ViewPosition found it. The
// offset is kept within the block's height, in case the block is shorter
// than it was. It does nothing for a document that does not scroll itself
// or a block it does not hold.
func (d *Document) RestoreViewPosition(p ViewPosition) {
	if d.region == nil || p.Block < 0 || p.Block >= d.BlockCount() {
		return
	}
	d.pending = &p
	d.applyView(p)
}

func (d *Document) applyView(p ViewPosition) {
	if d.region == nil || p.Block < 0 || p.Block >= d.BlockCount() {
		return
	}
	d.ValidateLayout()
	e := d.Editor
	y := e.BlockTop(p.Block) + min(max(0, p.Offset), e.heights[p.Block])
	d.region.ScrollTo(y)
	_, d.lastY = d.region.Position()
}

// RevealSpan brings a span of the document's decorations into view, margin
// pixels below the top of the view, and reports false for a document that
// does not scroll itself or a span it does not have.
func (d *Document) RevealSpan(id string, margin float32) bool {
	deco := d.Decorations()
	s := find(deco.spans, id)
	if d.region == nil || s == nil {
		return false
	}
	top := d.Editor.BlockTop(s.block)
	if rects := deco.SpanRects(id); len(rects) > 0 {
		top = rects[0].Y
	}
	d.ValidateLayout()
	d.region.ScrollTo(top - margin)
	return true
}

// watch applies a view position restored before the document had a size,
// and tells the host when the reader has scrolled.
func (d *Document) watch(*unison.Canvas, geom.Rect) {
	if d.region == nil {
		return
	}
	if p := d.pending; p != nil {
		// Once more, now that the document has the size it is drawn at.
		d.pending = nil
		unison.InvokeTask(func() { d.applyView(*p) })
	}
	if _, y := d.region.Position(); y != d.lastY {
		d.lastY = y
		if d.OnViewMoved != nil {
			unison.InvokeTask(d.OnViewMoved)
		}
	}
}

// documentLayout gives the editor, or the region holding it, the whole
// document. A document that scrolls itself asks for no height of its own:
// its host gives it one.
type documentLayout struct{ d *Document }

func (l documentLayout) LayoutSizes(_ *unison.Panel, hint geom.Size) (minSize, prefSize, maxSize geom.Size) {
	child := l.d.Children()[0]
	_, pref, _ := child.Sizes(hint)
	if l.d.region != nil {
		return geom.Size{}, geom.NewSize(pref.Width, 0), geom.NewSize(unison.DefaultMaxSize, unison.DefaultMaxSize)
	}
	return geom.NewSize(0, pref.Height), pref, geom.NewSize(unison.DefaultMaxSize, pref.Height)
}

func (l documentLayout) PerformLayout(target *unison.Panel) {
	target.Children()[0].SetFrameRect(target.ContentRect(false))
}
