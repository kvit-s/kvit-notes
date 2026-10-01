// Package editor is Kvit's block editor as a unison widget: a Markdown note
// shown as a column of blocks (paragraphs, headings, list items, to-dos,
// quotes, code, dividers), edited in place with the inline formatting drawn
// and its Markdown markers shown only around the caret.
//
// The note is a flat list of blocks (Doc), each holding its Markdown source.
// The caret and the selection are positions in that source. The Editor panel
// lays every block out with kvit-ui's text package, draws the rows in view,
// turns keys and pointer input into operations on the Doc, and describes
// each block to screen readers as an editable text.
package editor

import (
	"github.com/kvit-s/kvit-notes/query"
	"math"
	"time"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// RemotePolicy gates remote pictures and pages: nothing remote loads until
// its origin is approved. The app implements it over its egress policy.
type RemotePolicy interface {
	IsAllowed(url string) bool
	Allow(url string)
	CanRequest(url string) bool
	Refusal(url string) string
	Origin(url string) string
}

// Editor is the block editor. Put it in a kvitui.Region to scroll it; it
// takes the width it is given and is as tall as its note.
type Editor struct {
	unison.Panel
	ui *kvitui.UI
	// Doc is the note being edited.
	Doc *Doc
	// OnChange runs after anything a status line would show has changed:
	// the note, the caret, the selection or the block selection.
	OnChange func()
	// Placeholder is shown in an empty paragraph.
	Placeholder string
	// PickImage, when set, is asked for a picture for a new image block:
	// the application shows a file dialog and writes the block's line.
	PickImage func(block int64)
	// LoadImage loads the picture an image block names, by the path as
	// written; nil draws every picture as a card naming it.
	LoadImage func(path string) (unison.Drawable, error)
	// RunQuery, when set, answers a query block's spec, and OpenNote opens a
	// note one of its rows names.
	RunQuery   func(body string) query.Answer
	OpenNote   func(path string)
	queryCache map[int64]*queryLayout
	queryFresh map[int64]bool // which query blocks were worked out since the last change
	queryHover int
	// boardViews are each task board's folded columns and filter, and
	// boards its layout.
	boardViews map[int64]*boardView
	boards     map[int64]cachedBoard
	cardDrag   *cardDrag     // a card pressed on a board, until let go
	colDrag    *colDragState // a column header pressed for dragging, until let go
	// boardHover is the card under the pointer, for its add-tag and due
	// affordances; -1 when none. The layout shows them on that card only.
	boardHoverID  int64
	boardHoverCol int
	boardHoverIdx int
	tagEdit       *tagEditState // a card's chip row taking a label
	duePick       *duePickState // a card's date picker, open or nil
	// marks are the find bar's matches by block, drawn behind the text.
	marks map[int64][]Mark
	// Printing lays the note out for pages: no gutter and no side margins.
	Printing bool
	// LineNumbers numbers the lines of code blocks (View, Code line numbers).
	LineNumbers bool
	// FormatBar shows the formatting bar over a text selection.
	FormatBar bool
	// CompleteLink, when set, is what typing after "[[" offers: the notes,
	// or after a "#", a note's headings, for what is typed.
	CompleteLink func(query string) []Completion
	wiki         *wikiMenu
	math         mathEntry // what the math typing aids remember (mathassist.go)
	// CopyRich, when set, puts Markdown on the clipboard with its HTML, and
	// PasteRich reads HTML off the clipboard as Markdown, reporting false
	// when there is none worth reading.
	CopyRich  func(markdown string)
	PasteRich func() (string, bool)
	// PasteImage, when set, saves a clipboard picture into the vault and
	// reports the stored path. Nil keeps image data from being pasted,
	// leaving the text path to run instead.
	PasteImage func() (string, bool)
	// SaveDroppedImage, when set, copies an image file dropped from another
	// application into the vault's picture folder and reports the stored
	// path. Nil keeps the file path as written.
	SaveDroppedImage func(sourcePath string) (stored string, ok bool)
	// BlocksHTML, when set, writes blocks as HTML for the block menu's
	// Copy as, HTML.
	BlocksHTML func(blocks []Block, indexes []int) string
	// FollowLink, when set, follows a link a press landed on.
	FollowLink func(LinkRef)
	// OnLink, when set, opens the link dialog, as Ctrl+K does.
	OnLink func()
	// OnEditEmbed, when set, edits an embed block's address, as the block
	// menu's Edit URL… does.
	OnEditEmbed func(id int64, current string)
	// OnExport, when set, exports blocks, as the block menu's Export does.
	OnExport     func(ids []int64)
	formatBar    func()        // hides the formatting bar while it is shown
	formatBarRow *unison.Panel // the bar's buttons
	// LoadPreview, when set, is asked to read a web page for its embed card,
	// and answers through SetPreview; OpenURL opens an address.
	LoadPreview func(address string)
	OpenURL     func(address string)
	previews    map[string]*Preview
	// RemotePolicy, when set, gates remote pictures and pages: nothing remote
	// loads until its origin is approved (opening a note is not consent).
	// Nil draws every remote picture as not loaded.
	RemotePolicy RemotePolicy
	// Centered keeps the text to a column in the middle, as focus mode does,
	// when the reader has set no width of their own.
	Centered bool
	// Typewriter, when set to the region the editor scrolls in, keeps the
	// caret's line in the middle of the view and fades the other blocks
	// (typewriter mode).
	Typewriter *kvitui.Region
	// tableActive is the one live cell: the grid cell being edited in
	// place, or nil. Row -1 is the header row, 0.. the data rows.
	tableActive *tableCellPos
	tableSweep  *tableSweep
	// tableHold keeps a table showing its grid while its caret is in it.
	// The grid is for the pointer: keyboard entry (FocusBlock) shows the
	// Markdown source instead and clears the hold.
	tableHold      map[int64]bool
	tableSort      map[int64]tableSortState
	tableResize    *tableResizeState
	tableField     *kvitui.Field
	hideTableField func()
	picker         *tablePicker
	pictures       map[string]picture
	grids          map[int64]cachedGrid
	// codeScroll is each code block's horizontal scroll offset by block id:
	// long lines do not wrap, so the text scrolls under its panel past the
	// viewport. codeDrag is a press on a code scrollbar, until let go.
	codeScroll map[int64]float32
	codeDrag   *codeDragState
	// DiagramMath, when set, typesets the $$…$$ labels of Mermaid diagrams
	// (diagram.go); without it they are drawn as their source.
	DiagramMath DiagramMath
	// EquationNumbers numbers the display equations at their right (View,
	// Equation numbers).
	EquationNumbers bool
	// SaveDiagramPNG, when set, is given a diagram's PNG by its PNG
	// control; without it a file dialog asks where to write it.
	SaveDiagramPNG func(png []byte)
	// OnStatus, when set, shows a short message about what was just done,
	// such as where a diagram was saved.
	OnStatus func(message string)
	diagrams map[int64]*diagramView // each Mermaid block's drawing and zoom
	diagPan  *diagramPan            // a press on a diagram, until let go

	layouts    map[int64]cachedLayout
	tops       []float32 // the top of each block's row, in the editor's coordinates
	heights    []float32 // the height of each block's row
	laidWidth  float32   // the width the rows were measured at
	generation int       // bumped when the theme or typography changes

	goalX   float32 // the x Up and Down keep to
	hasGoal bool

	blockSel    map[int64]bool // the blocks selected as whole blocks
	blockAnchor int64          // where Shift extends a block selection from
	selectAllN  int            // Ctrl+A presses in a row
	gapArmed    int            // the armed seam above block gapArmed, count is below last, -1 off
	gapHover    int            // the seam under the pointer, -1 for none
	dropIndex   int            // the row an external drag would insert before, -1 for none
	drawn       *drawnSel      // a drawn block's own selection, nil for none
	drawnDrag   *drawnAnchor   // a drawn selection being made by dragging
	light       *lightbox      // a picture opened full-size, nil for none

	menu  *slashMenu
	drag  *dragState
	msel  *mouseSel
	hover int64      // the block under the pointer, 0 for none
	part  gutterPart // the gutter control under the pointer
	// tocHover is the table of contents entry under the pointer, -1 for
	// none.
	tocHover int
	recent   []string // the / menu's recently used entries, newest first

	blinkOff  bool      // the caret is in the off half of its blink
	blinkFrom time.Time // when the caret last moved, which restarts the blink
	blinking  bool

	seams *seams // an embedding and decorations (seams.go), nil for a note
}

type cachedLayout struct {
	key    layoutKey
	layout *blockLayout
}

// New returns an editor showing doc.
func New(ui *kvitui.UI, doc *Doc) *Editor {
	e := &Editor{ui: ui, Doc: doc, Placeholder: "Type something...", layouts: map[int64]cachedLayout{},
		blockSel: map[int64]bool{}, pictures: map[string]picture{}, grids: map[int64]cachedGrid{}, tableHold: map[int64]bool{}, tocHover: -1, queryHover: -1,
		codeScroll: map[int64]float32{}, gapArmed: -1, gapHover: -1, dropIndex: -1, boardHoverCol: -1, boardHoverIdx: -1}
	e.Self = e
	e.SetFocusable(true)
	e.SetSizer(e.sizes)
	e.DrawCallback = e.draw
	e.KeyDownCallback = e.keyDown
	e.RuneTypedCallback = e.runeTyped
	e.MouseDownCallback = e.mouseDown
	e.MouseDragCallback = e.mouseDrag
	e.MouseUpCallback = e.mouseUp
	e.MouseEnterCallback = e.mouseMove
	e.MouseMoveCallback = e.mouseMove
	e.MouseExitCallback = e.mouseExit
	e.MouseWheelCallback = e.wheel
	e.UpdateCursorCallback = e.cursor
	e.GainedFocusCallback = func() { e.touched(); e.MarkForRedraw(); e.syncFormatBar() }
	e.LostFocusCallback = func() { e.MarkForRedraw(); e.syncFormatBar() }
	e.FrameChangeCallback = func() {
		if w := e.ContentRect(false).Width; w > 0 {
			e.measureAt(w)
		}
	}
	e.Accessibility.Name = "Note"
	e.useMath()
	e.enableExternalDrops()
	ui.OnChanged(func() {
		e.generation++
		e.changed()
	})
	return e
}

// SetDoc replaces the note being edited.
func (e *Editor) SetDoc(doc *Doc) {
	if e.hideTableField != nil {
		hide := e.hideTableField
		e.hideTableField = nil
		e.tableField = nil
		hide()
	}
	e.tableActive = nil
	e.tableSweep = nil
	e.tableResize = nil
	clear(e.tableHold)
	e.closeTablePicker()
	e.Doc = doc
	clear(e.layouts)
	clear(e.pictures)
	clear(e.grids)
	clear(e.diagrams)
	clear(e.blockSel)
	clear(e.codeScroll)
	e.codeDrag = nil
	e.closeMenu()
	e.drag, e.msel = nil, nil
	e.gapArmed, e.gapHover = -1, -1
	e.dropIndex = -1
	e.drawn, e.drawnDrag = nil, nil
	e.light = nil
	e.cardDrag, e.colDrag = nil, nil
	e.boardHoverID, e.boardHoverCol, e.boardHoverIdx = 0, -1, -1
	e.closeTagEdit()
	e.closeDuePicker()
	e.changed()
}

// UI is the interface the editor draws with.
func (e *Editor) UI() *kvitui.UI { return e.ui }

// FocusBlock puts the caret in block i at a source offset and gives the
// editor the keyboard focus. Focusing a table shows its Markdown: the grid
// is for the pointer, which makes a cell live.
func (e *Editor) FocusBlock(i, off int) {
	e.dismissGap()
	e.clearBlockSel()
	if e.hideTableField != nil {
		hide := e.hideTableField
		e.hideTableField = nil
		e.tableField = nil
		hide()
	}
	e.tableActive = nil
	e.tableSweep = nil
	delete(e.tableHold, e.Doc.Blocks[i].ID)
	e.Doc.SetCaret(e.Doc.Blocks[i].ID, off)
	e.RequestFocus()
	e.touched()
	e.changed()
}

// Refresh redraws the editor after its Doc was changed from outside, and
// brings the caret into view.
func (e *Editor) Refresh() {
	e.touched()
	e.changed()
}

// ClearFocus takes the caret out of every block, as a press outside the
// note does.
func (e *Editor) ClearFocus() {
	e.Doc.Focused = false
	e.Doc.Anchor = e.Doc.Caret
	e.clearBlockSel()
	e.changed()
}

// SelectedBlocks are the ids of the blocks selected as whole blocks, in
// document order.
func (e *Editor) SelectedBlocks() []int64 {
	var ids []int64
	for _, b := range e.Doc.Blocks {
		if e.blockSel[b.ID] {
			ids = append(ids, b.ID)
		}
	}
	return ids
}

// SetBlockSelection selects whole blocks by id, as a handle click does, for
// callers (such as the find bar's tests) that do not go through the pointer.
func (e *Editor) SetBlockSelection(ids []int64) {
	e.dismissGap()
	clear(e.blockSel)
	for _, id := range ids {
		if e.Doc.Block(id) != nil {
			e.blockSel[id] = true
		}
	}
	e.selectAllN = 0
	e.changed()
}

func (e *Editor) clearBlockSel() {
	clear(e.blockSel)
	e.selectAllN = 0
}

// changed re-measures the rows after the note, the caret or the selection
// changed, asks for a new layout when the note's height changed, and
// redraws.
func (e *Editor) changed() {
	e.syncTocs()
	clear(e.queryFresh)
	before := e.total()
	e.measure()
	if e.total() != before {
		for p := e.AsPanel(); p != nil; p = p.Parent() {
			p.NeedsLayout = true
		}
	}
	e.MarkForRedraw()
	if e.menu != nil {
		e.menu.relayout()
	}
	e.syncFormatBar()
	e.syncWikiMenu()
	e.syncMath()
	if e.OnChange != nil {
		e.OnChange()
	}
}

// touched restarts the caret's blink and brings the caret into view.
func (e *Editor) touched() {
	e.blinkFrom = time.Now()
	e.blinkOff = false
	e.startBlink()
	e.revealCaret()
}

// Geometry. Everything is in the editor's own coordinates.

// width is the width the rows were measured at: the editor's own width
// once it has one.
func (e *Editor) width() float32 {
	if e.laidWidth > 0 {
		return e.laidWidth
	}
	if w := e.ContentRect(false).Width; w > 0 {
		return w
	}
	return e.px(defaultWidth)
}

// defaultWidth is the width an editor is measured at before it is given one.
const defaultWidth = 800

// bodyLeft and bodyRight are the left and right edges of a row's body: the
// part after the gutter and the focus bar, which is tinted on hover.
func (e *Editor) bodyLeft() float32 {
	if e.Printing {
		return 0
	}
	if e.seams.noGutter() {
		return e.side() + e.px(focusBar)
	}
	return e.side() + e.px(gutterWidth+focusBar)
}

func (e *Editor) bodyRight() float32 { return e.width() - e.side() }

// focusColumn is the width focus mode keeps the text to when the reader has
// set no limit of their own.
const focusColumn = 760

// side is the space left and right of the rows: the page margin, and, when
// the reader limits the width of the text (Settings, Typography) or focus
// mode is on, half of the width left over, which centres the column.
func (e *Editor) side() float32 {
	if e.Printing {
		return 0
	}
	s := e.margin()
	limit := float32(e.ui.Typography.MaxContentWidth())
	if limit <= 0 && e.Centered {
		limit = focusColumn
	}
	if limit <= 0 {
		return s
	}
	return s + max(0, float32(math.Floor(float64(e.width()-2*s-e.px(limit))/2)))
}

// gap is the space between rows: the reader's block spacing.
func (e *Editor) gap() float32 {
	if g, ok := e.embeddedGap(); ok {
		return g
	}
	return e.px(float32(e.ui.Typography.ParagraphSpacing()))
}

// textLeft is how far a block's text starts from the body's left edge.
func (e *Editor) textLeft(b *Block) float32 {
	if x, ok := e.mathTextLeft(b); ok {
		return x
	}
	switch {
	case b.Kind.IsList():
		return e.markerLeft(b) + e.px(markerWidth(b.Kind))
	case b.Kind == Quote:
		return e.markerLeft(b) + e.px(quoteMarker)
	case b.Kind.isSource():
		return e.px(codeInset+codePadSide+codeTextInset) + e.lineNumberWidth(b)
	case b.Kind == Callout:
		return e.px(codeInset + calloutText)
	}
	return e.px(contentLeft) + e.dropCapWidth(b)
}

// markerLeft is where a list item's marker, or a quote's bar, starts from
// the body's left edge: a little left of a paragraph's text, and one step
// further in for each level of a list.
func (e *Editor) markerLeft(b *Block) float32 {
	switch {
	case b.Kind.IsList():
		return e.px(contentLeft-listShift) + float32(b.Indent)*e.px(indentStep)
	case b.Kind == Quote:
		return e.px(contentLeft - quoteShift)
	}
	return 0
}

// How far a list's markers and a quote's bar sit left of a paragraph's text.
const (
	listShift  = 8
	quoteShift = 10
)

func markerWidth(k Kind) float32 {
	switch k {
	case Bullet:
		return bulletMarker
	case Numbered:
		return numberMarker
	case Todo:
		return todoMarker
	case Quote:
		return quoteMarker
	}
	return 0
}

// textWidth is the width a block's text wraps at.
func (e *Editor) textWidth(b *Block) float32 {
	right := e.px(contentRight)
	switch b.Kind {
	case Code, Raw, Table, Math:
		right += e.px(codePadSide)
	case Callout:
		right += e.px(calloutBottom)
	}
	return max(e.bodyRight()-e.bodyLeft()-e.textLeft(b)-right, e.px(40))
}

// textTop is how far a block's text starts below its row's top.
func (e *Editor) textTop(b *Block) float32 {
	if y, ok := e.mathTextTop(b); ok {
		return y
	}
	if b.Kind.isSource() {
		return e.px(codeRowTop + codeHeader + codeTextPad)
	}
	if b.Kind == Callout {
		return e.px(codeRowTop + calloutHeader)
	}
	return e.px(rowPadTop)
}

// textOrigin is the top-left corner of block i's text: shifted left by a
// code block's horizontal scroll, so the caret, hit tests and drawing all
// follow it.
func (e *Editor) textOrigin(i int) geom.Point {
	b := &e.Doc.Blocks[i]
	return geom.NewPoint(e.bodyLeft()+e.textLeft(b)-e.codeScrollDX(i), e.tops[i]+e.textTop(b))
}

// rowRect is block i's whole row, the gutter included.
func (e *Editor) rowRect(i int) geom.Rect {
	return geom.NewRect(e.side(), e.tops[i], e.width()-2*e.side(), e.heights[i])
}

// bodyRect is block i's row after the gutter and the focus bar.
func (e *Editor) bodyRect(i int) geom.Rect {
	return geom.NewRect(e.bodyLeft(), e.tops[i], e.bodyRight()-e.bodyLeft(), e.heights[i])
}

// layout is block i's text laid out for the current caret and selection,
// from the cache when nothing it depends on has changed.
func (e *Editor) layout(i int) *blockLayout {
	d := e.Doc
	b := &d.Blocks[i]
	key := layoutKey{text: b.Text, attrs: b.Attrs, lang: b.Lang, kind: b.Kind, checked: b.Checked, width: e.textWidth(b), caret: -1,
		generation: e.generation, marks: marksKey(e.marks[b.ID])}
	if d.Focused && d.Caret.Block == b.ID {
		key.caret = min(d.Caret.Off, len([]rune(b.Text)))
	}
	if from, to, ok := d.SelectionIn(i); ok {
		key.selA, key.selB = from, to
		if d.CrossBlock() {
			// Across blocks the caret's block reveals nothing for the
			// selection, only for the caret.
			key.caret = -1
		}
	}
	if c, ok := e.layouts[b.ID]; ok && c.key == key {
		return c.layout
	}
	caret := key.caret
	l := e.layOut(b, key.width, caret, key.selA, key.selB)
	e.layouts[b.ID] = cachedLayout{key, l}
	return l
}

// rowHeight is the height of block i's row.
func (e *Editor) rowHeight(i int) float32 {
	b := &e.Doc.Blocks[i]
	if b.Kind == Divider {
		return e.px(dividerRow)
	}
	if g, ok := e.tableShowsGrid(i); ok {
		h := e.px(codeRowTop+codeRowBottom) + g.height
		if e.tableCellIn(e.Doc.Blocks[i].ID) {
			h += e.px(20 + tableAddH + tableAddGap)
		}
		return h
	}
	if e.tocShows(i) {
		return e.tocHeight()
	}
	if e.boardShows(i) {
		return e.boardHeight(i)
	}
	if e.queryShows(i) {
		return e.px(codeRowTop+codeRowBottom) + e.queryResult(i).height
	}
	if h, ok := e.diagramHeight(i); ok {
		return h
	}
	if h, ok := e.mathHeight(i); ok {
		return h
	}
	if b.Kind.isSource() {
		return e.px(codeRowTop+codeHeader+2*codeTextPad+codeFooter+codeRowBottom) + e.layout(i).height()
	}
	if b.Kind == Callout {
		h := e.px(codeRowTop + calloutHeader + codeRowBottom)
		if e.calloutOpen(b) {
			h += e.layout(i).height() + e.px(calloutBottom)
		}
		return h
	}
	if _, ok, shows := e.pictureBlock(i); ok {
		h := e.px(rowPadTop+rowPadBottom) + e.pictureHeight(i, shows)
		if shows {
			h += e.layout(i).height()
		}
		return h
	}
	return e.px(rowPadTop+rowPadBottom) + e.layout(i).height()
}

// measure works out the top and height of every row at the current width.
func (e *Editor) measure() {
	d := e.Doc
	n := len(d.Blocks)
	e.tops = e.tops[:0]
	e.heights = e.heights[:0]
	y := e.margin()
	for i := 0; i < n; i++ {
		h := e.rowHeight(i)
		e.tops = append(e.tops, y)
		e.heights = append(e.heights, h)
		y += h + e.measureDecorations(i) + e.gap()
	}
	e.pruneDiagrams()
	// Forget the layouts of blocks that are gone.
	if len(e.layouts) > 2*n+16 {
		live := make(map[int64]bool, n)
		for _, b := range d.Blocks {
			live[b.ID] = true
		}
		for id := range e.layouts {
			if !live[id] {
				delete(e.layouts, id)
			}
		}
	}
	// Forget the horizontal scroll of blocks that are gone.
	if len(e.codeScroll) > 0 {
		live := make(map[int64]bool, n)
		for _, b := range d.Blocks {
			live[b.ID] = true
		}
		for id := range e.codeScroll {
			if !live[id] {
				delete(e.codeScroll, id)
			}
		}
	}
	e.afterMeasure()
}

// total is the height of every row, with the space above the first.
func (e *Editor) total() float32 {
	if len(e.tops) == 0 {
		return 0
	}
	n := len(e.tops) - 1
	return e.tops[n] + e.heights[n] + e.decorationSpace(n)
}

// tail is the space below the last block, a third of the window, so the
// last lines of a note can be read in the middle of the screen, and a press
// there puts the caret at the end.
func (e *Editor) tail() float32 {
	if t, ok := e.embeddedTail(); ok {
		return t
	}
	h := float32(600)
	if w := e.Window(); w != nil {
		h = w.ContentRect().Height
	}
	return max(h/3, e.px(40))
}

func (e *Editor) sizes(hint geom.Size) (minSize, prefSize, maxSize geom.Size) {
	w := hint.Width
	if w <= 0 {
		w = e.fullWidth()
	}
	e.measureAt(w)
	h := e.total() + e.tail()
	return geom.NewSize(e.px(minWidth), h), geom.NewSize(w, h), geom.NewSize(unison.DefaultMaxSize, h)
}

// minWidth is the narrowest the editor lays out at.
const minWidth = 200

// measureAt measures the rows at a width, unless they already were.
func (e *Editor) measureAt(w float32) {
	w -= e.marginColumn()
	if w != e.laidWidth || len(e.tops) != len(e.Doc.Blocks) {
		e.laidWidth = w
		e.measure()
	}
}

// blockAt is the index of the row whose vertical extent holds y, the
// nearest row when y is between rows or past either end, or -1 for an
// empty note.
func (e *Editor) blockAt(y float32) int {
	best, bestD := -1, float32(1e9)
	for i := range e.tops {
		top, bottom := e.tops[i], e.tops[i]+e.heights[i]
		var dist float32
		switch {
		case y < top:
			dist = top - y
		case y > bottom:
			dist = y - bottom
		}
		if dist < bestD {
			best, bestD = i, dist
		}
		if top > y {
			break
		}
	}
	return best
}

// visibleRows is the range of rows that intersect r.
func (e *Editor) visibleRows(r geom.Rect) (first, last int) {
	first, last = len(e.tops), -1
	for i := range e.tops {
		if e.tops[i]+e.heights[i] < r.Y {
			continue
		}
		if e.tops[i] > r.Bottom() {
			break
		}
		first = min(first, i)
		last = i
	}
	return first, last
}

// caretRect is the caret's rectangle, when a block holds it.
func (e *Editor) caretRect() (geom.Rect, bool) {
	d := e.Doc
	i := d.Index(d.Caret.Block)
	if !d.Focused || i < 0 || i >= len(e.tops) {
		return geom.Rect{}, false
	}
	l := e.layout(i)
	// As tall as the caret's line: the pitch, or more where a formula has
	// grown the line (mathinline.go).
	x, top, h := l.text.CaretAt(l.drawn(d.Caret.Off))
	o := e.textOrigin(i)
	return geom.NewRect(o.X+x, o.Y+top, e.px(caretWidth), h), true
}

// revealCaret scrolls the caret into view, with a little room around it: a
// code block first scrolls horizontally so a caret on a long line shows,
// then the region scrolls vertically as before.
func (e *Editor) revealCaret() {
	if i := e.Doc.Index(e.Doc.Caret.Block); i >= 0 {
		e.ensureCodeCaretVisible(i)
	}
	r, ok := e.caretRect()
	if !ok {
		return
	}
	if e.Typewriter != nil {
		e.Typewriter.ScrollTo(r.Y + r.Height/2 - e.Typewriter.ViewHeight()/2)
		return
	}
	room := e.px(8)
	e.ScrollRectIntoView(geom.NewRect(r.X, r.Y-room, r.Width, r.Height+2*room))
}

// blinkPeriod is how long the caret stays on, and then off.
const blinkPeriod = 530 * time.Millisecond

// startBlink makes the caret blink while the editor has the keyboard. It
// stays on with motion reduced, which is also how tests and screenshots see
// it.
func (e *Editor) startBlink() {
	if e.blinking || e.ui.Theme.MotionScale() == 0 {
		return
	}
	e.blinking = true
	var tick func()
	tick = func() {
		if !e.Focused() || !e.Doc.Focused || e.Window() == nil || e.ui.Theme.MotionScale() == 0 {
			e.blinking, e.blinkOff = false, false
			e.MarkForRedraw()
			return
		}
		off := int(time.Since(e.blinkFrom)/blinkPeriod)%2 == 1
		if off != e.blinkOff {
			e.blinkOff = off
			e.MarkForRedraw()
		}
		unison.InvokeTaskAfter(tick, blinkPeriod/4)
	}
	unison.InvokeTaskAfter(tick, blinkPeriod/4)
}
