package editor

// Selections of their own over blocks that draw their text: the web embed
// card, a collection query's results and a table of contents have a
// character span over what they drew. Drag selects a span, a double-click a
// word, a third click a whole line, Ctrl+A the block before the document,
// Ctrl+C copies the text on screen as plain text (tabs between cells on a
// line, newlines between lines), and Escape drops it. The span stays inside
// the one block; a document-level range across such a block still takes it
// whole with its Markdown source.

import (
	"strings"
	"unicode"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// drawnSel is a span over one drawn block's on-screen text, in runes of
// that text.
type drawnSel struct {
	block      int64
	start, end int
}

// drawnAnchor is a drawn selection being made by dragging.
type drawnAnchor struct {
	block  int64
	anchor int
	words  bool
	line   bool
}

// drawnText is the on-screen plain text of a block that draws its text, as
// copied: tabs between cells on a line, newlines between lines.
func (e *Editor) drawnText(id int64) (string, bool) {
	idx := e.Doc.Index(id)
	if idx < 0 {
		return "", false
	}
	b := &e.Doc.Blocks[idx]
	switch {
	case isToc(b):
		entries := e.Doc.tocEntries()
		lines := make([]string, 0, len(entries))
		for _, en := range entries {
			lines = append(lines, en.text)
		}
		return strings.Join(lines, "\n"), true
	case isQuery(b):
		if e.RunQuery == nil {
			return "", false
		}
		ql := e.queryResult(idx)
		a := ql.answer
		if !a.OK {
			return a.Error, true
		}
		var lines []string
		for _, r := range a.Rows {
			lines = append(lines, strings.Join(r.Cells, "\t"))
		}
		for _, g := range a.Groups {
			for _, r := range g.Rows {
				lines = append(lines, strings.Join(r.Cells, "\t"))
			}
		}
		return strings.Join(lines, "\n"), true
	default:
		ref, ok, _ := e.pictureBlock(idx)
		if !ok || !isEmbed(ref) {
			return "", false
		}
		parts := []string{}
		title := ref.Path
		if ref.Alt != "" {
			title = ref.Alt
		}
		if p := e.previews[ref.Path]; p != nil {
			if p.Title != "" {
				title = p.Title
			}
			parts = append(parts, title)
			if p.Description != "" {
				parts = append(parts, p.Description)
			}
		} else {
			parts = append(parts, title)
		}
		parts = append(parts, siteOf(ref.Path))
		return strings.Join(parts, "\n"), true
	}
}

// drawnLen is the rune count of a block's drawn text.
func (e *Editor) drawnLen(id int64) int {
	s, ok := e.drawnText(id)
	if !ok {
		return 0
	}
	return len([]rune(s))
}

// setDrawn selects [start, end) of a drawn block's text, clamped and
// ordered.
func (e *Editor) setDrawn(id int64, start, end int) {
	n := e.drawnLen(id)
	start = max(0, min(start, n))
	end = max(0, min(end, n))
	if start > end {
		start, end = end, start
	}
	if start == end {
		e.drawn = nil
	} else {
		e.drawn = &drawnSel{block: id, start: start, end: end}
	}
	e.touched()
	e.changed()
}

// clearDrawn drops a drawn selection.
func (e *Editor) clearDrawn() {
	if e.drawn != nil || e.drawnDrag != nil {
		e.drawn, e.drawnDrag = nil, nil
		e.touched()
		e.changed()
	}
}

// drawnCopy is the drawn selection's text for Ctrl+C.
func (e *Editor) drawnCopy() (string, bool) {
	if e.drawn == nil {
		return "", false
	}
	s, ok := e.drawnText(e.drawn.block)
	if !ok {
		return "", false
	}
	r := []rune(s)
	start := max(0, min(e.drawn.start, len(r)))
	end := max(0, min(e.drawn.end, len(r)))
	if start > end {
		start, end = end, start
	}
	return string(r[start:end]), true
}

// drawnWordAt expands an offset to its word, as double-click does.
func drawnWordAt(r []rune, at int) (int, int) {
	at = max(0, min(at, len(r)))
	isWord := func(c rune) bool { return unicode.IsLetter(c) || unicode.IsDigit(c) || c == '_' }
	if at < len(r) && !isWord(r[at]) {
		// On a space, take the word before it when there is one.
		if at > 0 && isWord(r[at-1]) {
			at--
		} else {
			return at, at + 1
		}
	}
	start := at
	for start > 0 && isWord(r[start-1]) && r[start-1] != '\n' && r[start-1] != '\t' {
		start--
	}
	end := at
	for end < len(r) && isWord(r[end]) {
		end++
	}
	return start, end
}

// drawnLineAt expands an offset to its line, as a triple-click does.
func drawnLineAt(r []rune, at int) (int, int) {
	at = max(0, min(at, len(r)))
	start := at
	for start > 0 && r[start-1] != '\n' {
		start--
	}
	end := at
	for end < len(r) && r[end] != '\n' {
		end++
	}
	return start, end
}

// drawnKey handles keys for a drawn block's own selection: Ctrl+C copies
// its plain text, Escape drops it, and Ctrl+A selects the hovered drawn
// block before the document. Other keys clear the span and run normally.
func (e *Editor) drawnKey(key unison.KeyCode, ctrl, shift, alt bool) bool {
	if alt {
		return false
	}
	switch {
	case ctrl && key == unison.KeyC && e.drawn != nil:
		if s, ok := e.drawnCopy(); ok {
			unison.ClipboardSetText(s)
		}
		return true
	case key == unison.KeyEscape && e.drawn != nil:
		e.clearDrawn()
		return true
	case ctrl && key == unison.KeyA && e.drawn == nil && !shift:
		if id := e.hoverDrawn(); id != 0 {
			e.setDrawn(id, 0, e.drawnLen(id))
			return true
		}
		return false
	case e.drawn != nil:
		// Any other key ends the span; the key itself still runs.
		e.clearDrawn()
		return false
	}
	return false
}

// hoverDrawn is the drawn block under the pointer, 0 for none: the hovered
// row when it shows a table of contents, a query's results or an embed
// card.
func (e *Editor) hoverDrawn() int64 {
	i := e.Doc.Index(e.hover)
	if i < 0 {
		return 0
	}
	b := &e.Doc.Blocks[i]
	switch {
	case isToc(b) && e.tocShows(i):
		return b.ID
	case isQuery(b) && e.queryShows(i):
		return b.ID
	default:
		if _, ok, _ := e.pictureBlock(i); ok {
			if ref, _, _ := e.pictureBlock(i); isEmbed(ref) {
				if _, _, ok := e.embedCard(i); ok {
					return b.ID
				}
			}
		}
	}
	return 0
}

// drawnLines are a drawn block's on-screen lines, matching drawnText split
// on newlines.
func (e *Editor) drawnLines(id int64) []string {
	s, ok := e.drawnText(id)
	if !ok {
		return nil
	}
	return strings.Split(s, "\n")
}

// drawnBlockIn is the drawn block whose card is card, 0 for none.
func (e *Editor) drawnBlockIn(card geom.Rect) int64 {
	for i := range e.Doc.Blocks {
		if c, _, ok := e.embedCard(i); ok && c == card {
			return e.Doc.Blocks[i].ID
		}
	}
	return 0
}

// drawnLineRange is the lines of a drawn block's text its selection covers.
func (e *Editor) drawnLineRange(id int64) (int, int, bool) {
	if e.drawn == nil || e.drawn.block != id {
		return 0, 0, false
	}
	lines := e.drawnLines(id)
	start, end := e.drawn.start, e.drawn.end
	if start > end {
		start, end = end, start
	}
	from, to := -1, -1
	off := 0
	for k, ln := range lines {
		n := len([]rune(ln))
		if off+n >= start && off <= end && start != end {
			if from < 0 {
				from = k
			}
			to = k
		}
		off += n + 1
	}
	if from < 0 {
		return 0, 0, false
	}
	return from, to, true
}

// drawnOffsetAt is the global offset in a drawn block's text under a point
// in row i: a table of contents entry hit-tested in its own layout, a
// query's row by its line, an embed's title, description or site by theirs.
func (e *Editor) drawnOffsetAt(i int, where geom.Point) (int64, int, bool) {
	b := &e.Doc.Blocks[i]
	switch {
	case isToc(b) && e.tocShows(i):
		return e.tocOffsetAt(i, where)
	case isQuery(b) && e.queryShows(i):
		return e.queryOffsetAt(i, where)
	default:
		if ref, ok, _ := e.pictureBlock(i); ok && isEmbed(ref) {
			if _, _, ok := e.embedCard(i); ok {
				return e.embedOffsetAt(i, where)
			}
		}
	}
	return 0, 0, false
}

// lineBase is the global offset of lines[line]: the runes before it plus
// one newline each.
func lineBase(lines []string, line int) int {
	base := 0
	for k := 0; k < line && k < len(lines); k++ {
		base += len([]rune(lines[k])) + 1
	}
	return base
}

// tocOffsetAt hit-tests a table of contents card: the entry under the
// point, in its own layout for the offset within it.
func (e *Editor) tocOffsetAt(i int, where geom.Point) (int64, int, bool) {
	b := &e.Doc.Blocks[i]
	entries := e.Doc.tocEntries()
	k := e.tocEntryAt(i, where)
	if k < 0 || k >= len(entries) {
		return 0, 0, false
	}
	lines := e.drawnLines(b.ID)
	base := lineBase(lines, k)
	words := entries[k].text
	if words == "" {
		words = "(untitled)"
	}
	// The entry's layout, as drawToc draws it.
	c := e.tocCard(i)
	t := e.tok()
	top := 4
	for _, en := range entries {
		top = min(top, en.level)
	}
	ex := c.X + e.px(tocPad) + float32(entries[k].level-top)*e.px(tocIndent)
	ey := c.Y + e.px(tocPad+tocLabel) + float32(k)*e.px(tocEntryH)
	st := e.chrome(kvitui.RoleStrong, text.Regular, t.Link)
	l := e.ui.Fonts.Layout([]text.Span{{Text: words, Style: st}}, text.Options{MaxWidth: max(1, c.Right()-e.px(tocPad)-ex)})
	off := l.IndexAt(where.X-ex, where.Y-ey)
	off = max(0, min(off, len([]rune(words))))
	// Map the layout offset back onto the drawn line, which holds the
	// entry's plain text (or "(untitled)").
	line := ""
	if k < len(lines) {
		line = lines[k]
	}
	if line == "" {
		line = "(untitled)"
	}
	off = max(0, min(off, len([]rune(line))))
	return b.ID, base + off, true
}

// queryOffsetAt hit-tests a query block: the row under the point, by its
// line; the offset within the line comes from its joined cells laid out as
// one line.
func (e *Editor) queryOffsetAt(i int, where geom.Point) (int64, int, bool) {
	b := &e.Doc.Blocks[i]
	lines := e.drawnLines(b.ID)
	k := e.queryRowAt(i, where)
	if k < 0 || k >= len(lines) {
		return 0, 0, false
	}
	base := lineBase(lines, k)
	line := lines[k]
	ql := e.queryResult(i)
	var rr geom.Rect
	if k < len(ql.rows) {
		rr = ql.rows[k]
		rr.Point = rr.Point.Add(e.queryCard(i).Point)
	}
	st := e.chrome(kvitui.RoleBody, text.Regular, e.tok().TextPrimary)
	l := e.ui.Fonts.Layout([]text.Span{{Text: line, Style: st}}, text.Options{MaxWidth: max(1, rr.Width)})
	off := 0
	if rr.Width > 0 {
		off = l.IndexAt(where.X-rr.X, where.Y-rr.Y)
	}
	off = max(0, min(off, len([]rune(line))))
	return b.ID, base + off, true
}

// embedOffsetAt hit-tests an embed card: its title, description or site
// line, each laid out as drawn.
func (e *Editor) embedOffsetAt(i int, where geom.Point) (int64, int, bool) {
	b := &e.Doc.Blocks[i]
	ref, ok, _ := e.pictureBlock(i)
	if !ok {
		return 0, 0, false
	}
	lines := e.drawnLines(b.ID)
	card, _, ok := e.embedCard(i)
	if !ok {
		return 0, 0, false
	}
	title, load := e.embedParts(card)
	_ = ref
	// Lines are title, description (when present), site. Work out which
	// vertical band the point is in.
	t := e.tok()
	small := e.chrome(kvitui.RoleSmall, text.Regular, t.TextFaint)
	strong := e.chrome(kvitui.RoleStrong, text.Bold, t.TextPrimary)
	type band struct {
		rect geom.Rect
		line int
		st   text.Style
	}
	bands := []band{{rect: title, line: 0, st: strong}}
	y := load.Y
	if len(lines) == 3 {
		l := e.ui.Fonts.Layout([]text.Span{{Text: lines[1], Style: e.chrome(kvitui.RoleSmall, text.Regular, t.TextSecondary)}}, text.Options{MaxWidth: max(1, title.Width)})
		_, lh := l.Size()
		_ = lh
		bands = append(bands, band{rect: geom.NewRect(load.X, y, title.Width, e.px(22)), line: 1, st: e.chrome(kvitui.RoleSmall, text.Regular, t.TextSecondary)})
	}
	bands = append(bands, band{rect: geom.NewRect(title.X, load.Bottom()+e.px(4), title.Width, e.px(16)), line: len(lines) - 1, st: small})
	for _, bd := range bands {
		if where.In(bd.rect) && bd.line < len(lines) {
			l := e.ui.Fonts.Layout([]text.Span{{Text: lines[bd.line], Style: bd.st}}, text.Options{MaxWidth: max(1, bd.rect.Width)})
			off := l.IndexAt(where.X-bd.rect.X, where.Y-bd.rect.Y)
			off = max(0, min(off, len([]rune(lines[bd.line]))))
			return b.ID, lineBase(lines, bd.line) + off, true
		}
	}
	// Elsewhere on the card: the end of the nearest line.
	if len(lines) == 0 {
		return 0, 0, false
	}
	line := 0
	if where.Y > card.Y+card.Height/2 {
		line = len(lines) - 1
	}
	return b.ID, lineBase(lines, line) + len([]rune(lines[line])), true
}

// drawnPress starts a drawn block's own selection: a double-click selects
// its word under the point, a triple-click its line, and a single press
// anchors a drag (its click runs on release when no drag made a span).
func (e *Editor) drawnPress(i int, where geom.Point, clickCount int) bool {
	id, at, ok := e.drawnOffsetAt(i, where)
	if !ok {
		return false
	}
	s, ok := e.drawnText(id)
	if !ok {
		return false
	}
	r := []rune(s)
	switch {
	case clickCount >= 3:
		start, end := drawnLineAt(r, at)
		e.drawnDrag = nil
		e.setDrawn(id, start, end)
	case clickCount == 2:
		start, end := drawnWordAt(r, at)
		e.drawnDrag = nil
		e.setDrawn(id, start, end)
	default:
		e.drawn = nil
		e.drawnDrag = &drawnAnchor{block: id, anchor: at}
		e.touched()
	}
	return true
}

// drawnDragStep extends a drawn selection anchored by a press.
func (e *Editor) drawnDragStep(i int, where geom.Point) bool {
	if e.drawnDrag == nil {
		return false
	}
	id, at, ok := e.drawnOffsetAt(i, where)
	if !ok || id != e.drawnDrag.block {
		// Dragged outside the block: extend to its nearer end.
		id = e.drawnDrag.block
		if !ok {
			n := e.drawnLen(id)
			if at >= n {
				at = n
			} else {
				at = 0
			}
		} else {
			at = e.drawnDrag.anchor
		}
		_ = i
	}
	a := e.drawnDrag.anchor
	if at == a {
		e.drawn = nil
	} else {
		e.drawn = &drawnSel{block: id, start: min(a, at), end: max(a, at)}
	}
	e.touched()
	e.changed()
	return true
}

// drawnRelease finishes a press on a drawn block: with a span it keeps the
// selection (suppressing the click), without one it runs the click (going
// to a heading, opening a note, opening a page).
func (e *Editor) drawnRelease(i int, where geom.Point) bool {
	if e.drawnDrag == nil {
		return false
	}
	dragged := e.drawn != nil
	e.drawnDrag = nil
	if dragged {
		e.changed()
		return true
	}
	// A single click without a drag: the press's own click.
	b := &e.Doc.Blocks[i]
	switch {
	case isToc(b) && e.tocShows(i):
		if k := e.tocEntryAt(i, where); k >= 0 {
			e.goToHeading(e.Doc.tocEntries()[k].block)
		}
		return true
	case isQuery(b) && e.queryShows(i):
		if k := e.queryRowAt(i, where); k >= 0 && e.OpenNote != nil {
			e.OpenNote(e.queryResult(i).paths[k])
		}
		return true
	default:
		if card, ref, ok := e.embedCard(i); ok && e.OpenURL != nil {
			// Only the title opens; elsewhere on the card a click drops
			// any span (there is none) and does nothing.
			title, _ := e.embedParts(card)
			if where.In(title) {
				e.OpenURL(ref.Path)
			}
		}
		return true
	}
}
