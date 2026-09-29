package editor

// The block editor sized like a text field: a message box, a comment field
// ( core/qml/CompactEditor under Kvit Works' qml/agent/GrowingInput).
// What is typed is a document, drawn as it means: a heading is a heading, a
// list is a list, a picture is the picture. What leaves the box is the
// Markdown that document is written as.
//
// The box has no gutter, bars or scrollbar. It grows with what is typed from
// one line up to MaximumLines and scrolls inside itself past that. Enter
// sends (OnSubmit) and Shift+Enter starts a new line, except in a list item,
// where Enter makes the next item and on an empty item ends the list;
// Ctrl+Enter sends from any block. A picture pasted into the box is written
// where PasteImage writes it and becomes an image block, and the box keeps an
// empty paragraph under a picture at its foot so there is always somewhere to
// type.

import (
	"math"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/behavior"
	"github.com/richardwilkes/unison/enums/mod"
)

// CompactOptions say how a Compact is made. The zero value is Kvit Works'
// message box without pictures.
type CompactOptions struct {
	// MaximumLines is how many lines of body text tall the box grows before
	// it scrolls; 12 when 0.
	MaximumLines int
	// Margin is the space between the box's edges and its text, in design
	// pixels. Kvit Works draws the box's frame and padding itself and uses
	// 0; the CompactEditor on its own uses 6.
	Margin float32
	// Placeholder is what an empty paragraph says the box is for.
	Placeholder string
	// EnterMakesBlocks gives Enter back to the editor, where it makes the
	// next block, as a capture box wants ( returnSubmits false). Enter
	// sends when it is false.
	EnterMakesBlocks bool
	// ShowPictureLine shows an image block's Markdown line under the
	// picture while the caret is in it ( showImageEditPanel true). Kvit
	// Works leaves it off: the path is a file the box named itself, and a
	// click on the picture then opens it rather than moving the caret.
	ShowPictureLine bool
	// Pictures are the folders the box's pictures are looked up in: for
	// Kvit Works, Base is the folder pasted pictures are written under.
	Pictures PictureFolders
	// PasteImage, when set, writes a picture on the clipboard where the box
	// keeps its pictures and answers the path the image block names
	// (SavePastedPicture). Nil refuses a pasted picture, which is the answer
	// while there is no project to put it in.
	PasteImage func() (stored string, ok bool)
	// OpenPicture, when set, is asked to show a picture a press landed on.
	OpenPicture func(path, alt string)
}

// Compact is the editor sized like a field.
type Compact struct {
	unison.Panel
	// Editor edits the box's document. The box owns its OnChange.
	Editor *Editor
	// OnSubmit runs on Enter, or Ctrl+Enter, with the document as Markdown:
	// handed over rather than left to be fetched, because sending is
	// usually followed by clearing the box.
	OnSubmit func(markdown string)
	// OnEdited runs after every change to the document, for a host saving
	// a draft; Markdown walks the whole document, so debounce it.
	OnEdited func()
	// OnGrew runs after the box grew taller while the caret was in it, on
	// the next pass of the event loop, so the host can keep the box's foot,
	// and the send control beside it, on screen.
	OnGrew func()
	// OnCaretInside runs when the caret comes into the box or leaves it,
	// for a host that draws the box's frame in the focus colour.
	OnCaretInside func(inside bool)

	scroll            *unison.ScrollPanel
	maxLines          int
	last              string // the Markdown at the last check for an edit
	syncing           bool   // SetMarkdown is replacing the document
	tailPending       bool
	seenRows          int
	seenTailTakesText bool
	height            float32 // the height at the last check for growth
	inside            bool
}

// NewCompact makes an empty box: one empty paragraph.
func NewCompact(ui *kvitui.UI, o CompactOptions) *Compact {
	c := &Compact{maxLines: o.MaximumLines, seenTailTakesText: true}
	if c.maxLines <= 0 {
		c.maxLines = 12
	}
	c.Self = c
	e := New(ui, NewDoc(nil))
	e.Placeholder = o.Placeholder
	e.Accessibility.Name = o.Placeholder
	e.LoadImage = o.Pictures.Loader()
	e.PasteImage = o.PasteImage
	e.Embed(Embedding{NoGutter: true, Margin: o.Margin, HideImageLine: !o.ShowPictureLine, OpenPicture: o.OpenPicture})
	e.OnChange = c.changed
	c.Editor = e
	c.SetReturnSubmits(!o.EnterMakesBlocks)
	c.seenRows = len(e.Doc.Blocks)
	c.last = c.Markdown()

	c.scroll = unison.NewScrollPanel()
	for _, bar := range []*unison.ScrollBar{c.scroll.Bar(false), c.scroll.Bar(true)} {
		bar.Hidden = true
		bar.MinimumThickness = 0
	}
	c.scroll.DrawCallback = func(*unison.Canvas, geom.Rect) {}
	c.scroll.SetContent(e, behavior.Follow, behavior.Fill)
	c.scroll.MouseWheelCallback = c.wheel
	c.AddChild(c.scroll)
	c.SetLayout(compactLayout{c})
	c.FocusChangeInHierarchyCallback = func(_, _ *unison.Panel) { c.noteCaret() }
	return c
}

// SetReturnSubmits makes Enter send (true) or gives it back to the editor,
// where it makes the next block ( returnSubmits).
func (c *Compact) SetReturnSubmits(on bool) {
	emb, _ := c.Editor.Embedded()
	emb.ReturnPressed = nil
	if on {
		emb.ReturnPressed = func(int) { c.submit() }
	}
	c.Editor.Embed(emb)
}

// ReturnSubmits reports whether Enter sends.
func (c *Compact) ReturnSubmits() bool {
	emb, _ := c.Editor.Embedded()
	return emb.ReturnPressed != nil
}

// SetMaximumLines sets how many lines of body text tall the box grows
// before it scrolls; 12 when n is below 1.
func (c *Compact) SetMaximumLines(n int) {
	if n <= 0 {
		n = 12
	}
	c.maxLines = n
	c.MarkForLayoutRecursivelyUpward()
	c.MarkForRedraw()
}

// Markdown is the box's document as Markdown.
func (c *Compact) Markdown() string { return Serialize(c.Editor.Doc.Blocks) }

// SetMarkdown replaces the document, dropping the selection and the undo
// history, since an undo across the change would bring the old document
// back. A document that ends in a picture gets an empty paragraph under it.
// A caret that was in the box is put at the end of the new document, as a
// reader who was typing when a quotation or a stored draft arrived is still
// typing there.
func (c *Compact) SetMarkdown(md string) {
	inside := c.CaretInside()
	c.syncing = true
	c.Editor.SetDoc(NewDoc(ParseMarkdown(md)))
	c.addTailParagraph()
	c.seenRows = len(c.Editor.Doc.Blocks)
	c.seenTailTakesText = true
	c.last = c.Markdown()
	c.syncing = false
	c.Editor.changed()
	if inside {
		c.FocusBlock(len(c.Editor.Doc.Blocks)-1, true)
		c.announceGrowth()
	}
}

// Clear empties the box to one empty paragraph, which is a change a host
// storing a draft is told of.
func (c *Compact) Clear() {
	before := c.Markdown()
	c.SetMarkdown("")
	if c.Markdown() != before && c.OnEdited != nil {
		c.OnEdited()
	}
}

// Empty reports whether the box holds nothing: no blocks, or one block with
// no text. A picture on its own is not nothing.
func (c *Compact) Empty() bool {
	blocks := c.Editor.Doc.Blocks
	return len(blocks) == 0 || (len(blocks) == 1 && blocks[0].Text == "")
}

// CaretInside reports whether the caret is in the box, which is what a host
// asks rather than whether the box has the focus.
func (c *Compact) CaretInside() bool {
	e := c.Editor
	return e.Focused() && e.Doc.Focused && e.Doc.CaretBlock() != nil
}

// Focus puts the caret back in the row it was last in, or at the end of the
// last row when that row is a picture, where nothing can be typed.
func (c *Compact) Focus() {
	d := c.Editor.Doc
	rows := len(d.Blocks)
	if rows == 0 {
		return
	}
	remembered := max(0, d.Index(d.Caret.Block))
	if !c.rowTakesText(remembered) {
		c.FocusBlock(rows-1, true)
		return
	}
	off := min(d.Caret.Off, len(runes(d.Blocks[remembered].Text)))
	if d.Index(d.Caret.Block) < 0 {
		off = 0
	}
	c.Editor.FocusBlock(remembered, off)
}

// FocusBlock puts the caret in row i, at its end or its start.
func (c *Compact) FocusBlock(i int, atEnd bool) {
	d := c.Editor.Doc
	if i < 0 || i >= len(d.Blocks) {
		return
	}
	off := 0
	if atEnd {
		off = len(runes(d.Blocks[i].Text))
	}
	c.Editor.FocusBlock(i, off)
}

// RowCount is how many blocks the box's document has.
func (c *Compact) RowCount() int { return len(c.Editor.Doc.Blocks) }

// LineUnit is one line of body text, in pixels, which MaximumLines counts.
func (c *Compact) LineUnit() float32 {
	ty := c.Editor.ui.Typography
	return max(1, float32(math.Round(float64(ty.BodySize())*ty.LineHeight())))
}

// CapHeight is the tallest the box's text grows before it scrolls, without
// the margins.
func (c *Compact) CapHeight() float32 { return float32(c.maxLines) * c.LineUnit() }

// ContentHeight is how tall the box's rows are, without the margins.
func (c *Compact) ContentHeight() float32 { return c.Editor.ContentHeight() }

func (c *Compact) submit() {
	if c.OnSubmit != nil {
		c.OnSubmit(c.Markdown())
	}
}

// changed follows every change the editor reports: an edit is told to the
// host and looked at for a picture left at the foot, and growth and the
// caret coming or going are announced.
func (c *Compact) changed() {
	if c.syncing {
		return
	}
	if md := c.Markdown(); md != c.last {
		c.last = md
		if c.OnEdited != nil {
			c.OnEdited()
		}
		c.tailSoon()
	}
	if h := c.prefHeight(); h > c.height+0.5 && c.height > 0 && c.CaretInside() {
		c.height = h
		c.announceGrowth()
	} else {
		c.height = h
	}
	c.noteCaret()
}

func (c *Compact) announceGrowth() {
	if c.OnGrew != nil {
		unison.InvokeTask(func() {
			if c.OnGrew != nil {
				c.OnGrew()
			}
		})
	}
}

func (c *Compact) noteCaret() {
	if in := c.CaretInside(); in != c.inside {
		c.inside = in
		if c.OnCaretInside != nil {
			c.OnCaretInside(in)
		}
	}
}

// Somewhere to type. A picture is not a row the caret can type in, so a
// picture pasted into an empty box would leave a document nothing can be
// typed into, sent from or deleted from. The box keeps a row that takes text
// at its foot: an empty paragraph goes under a picture that arrives there,
// with the caret, and back into a document left with no rows. A picture
// left at the foot by the reader deleting the paragraph under it gets none,
// or it could never be deleted.

// rowTakesText reports whether the caret can be put in row i: a picture, a
// video and a divider draw themselves and take no text.
func (c *Compact) rowTakesText(i int) bool {
	blocks := c.Editor.Doc.Blocks
	if i < 0 || i >= len(blocks) {
		return true
	}
	k := blocks[i].Kind
	return k != Image && k != Media && k != Divider
}

// addTailParagraph adds the missing paragraph at the foot and answers its
// row, or -1 when the last row already takes text.
func (c *Compact) addTailParagraph() int {
	d := c.Editor.Doc
	rows := len(d.Blocks)
	if rows > 0 && c.rowTakesText(rows-1) {
		return -1
	}
	d.Blocks = append(d.Blocks, NewBlock(Paragraph, ""))
	return rows
}

// tailSoon looks at the foot on the next pass of the event loop, after the
// editor has settled the caret for the edit, once for a run of edits.
func (c *Compact) tailSoon() {
	if c.tailPending {
		return
	}
	c.tailPending = true
	unison.InvokeTask(func() {
		c.tailPending = false
		c.keepSomewhereToType()
	})
}

func (c *Compact) keepSomewhereToType() {
	if c.syncing {
		return
	}
	rows := len(c.Editor.Doc.Blocks)
	tailTakesText := rows > 0 && c.rowTakesText(rows-1)
	wanted := rows == 0 || (!tailTakesText && (rows > c.seenRows || (rows == c.seenRows && c.seenTailTakesText)))
	c.seenRows, c.seenTailTakesText = rows, tailTakesText
	if !wanted {
		return
	}
	added := c.addTailParagraph()
	c.seenRows, c.seenTailTakesText = len(c.Editor.Doc.Blocks), true
	if added < 0 {
		return
	}
	if c.Editor.Focused() {
		c.FocusBlock(added, true)
	} else {
		c.Editor.changed()
	}
}

// wheel scrolls the box's text when there is more than shows, and otherwise
// lets the wheel go on to the pane the box is in.
func (c *Compact) wheel(where, delta geom.Point, mods mod.Modifiers) bool {
	bar := c.scroll.Bar(false)
	if bar.Max() <= bar.Extent()+0.5 {
		return false
	}
	return c.scroll.DefaultMouseWheel(where, delta, mods)
}

// prefHeight is the box's height at its width: its rows up to the cap, and
// its margins.
func (c *Compact) prefHeight() float32 {
	w := c.ContentRect(false).Width
	if w <= 0 {
		w = c.Editor.fullWidth()
	}
	return c.heightAt(w)
}

func (c *Compact) heightAt(w float32) float32 {
	_, pref, _ := c.Editor.Sizes(geom.NewSize(w, 0))
	return min(pref.Height, c.CapHeight()+2*c.Editor.margin())
}

// compactLayout sizes the box to its rows, up to the cap, at the width it is
// given, and fills it with the scrolling view of the editor.
type compactLayout struct{ c *Compact }

func (l compactLayout) LayoutSizes(_ *unison.Panel, hint geom.Size) (minSize, prefSize, maxSize geom.Size) {
	w := hint.Width
	if w <= 0 {
		w = l.c.Editor.px(compactWidth)
	}
	h := l.c.heightAt(w)
	return geom.NewSize(l.c.Editor.px(compactMinWidth), h), geom.NewSize(w, h), geom.NewSize(unison.DefaultMaxSize, h)
}

func (l compactLayout) PerformLayout(target *unison.Panel) {
	l.c.scroll.SetFrameRect(target.ContentRect(false))
}

// The width a box is laid out at before it is given one ( implicitWidth),
// and the narrowest it lays out at.
const (
	compactWidth    = 240
	compactMinWidth = 40
)
