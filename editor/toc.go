package editor

// The table of contents (features.md 17.2, Kvit's TocBlock.qml and
// TocFenceSync.qml): a code fence of language "toc". It is drawn as a
// "Contents" card listing the note's headings, indented by level, each a
// link that scrolls the note to its heading. Its body, which is what the
// file holds, is kept as a Markdown list of links to the headings' anchors,
// rewritten whenever the headings change, so the note reads as a table of
// contents in any other Markdown program too.

import (
	"strings"

	"github.com/kvit-s/kvit-notes/links"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// isToc reports whether a block is a table of contents.
func isToc(b *Block) bool { return b.Kind == Code && b.Lang == "toc" }

// tocEntry is one heading a table of contents lists.
type tocEntry struct {
	level int
	text  string
	block int
	slug  string
}

// tocEntries are the note's headings, with the anchors links reach them by.
func (d *Doc) tocEntries() []tocEntry {
	var out []tocEntry
	var hs []links.Heading
	for i := range d.Blocks {
		b := &d.Blocks[i]
		level := 0
		switch b.Kind {
		case Heading1:
			level = 1
		case Heading2:
			level = 2
		case Heading3:
			level = 3
		case Heading4:
			level = 4
		}
		if level == 0 {
			continue
		}
		t := PlainText(b.Text)
		out = append(out, tocEntry{level: level, text: t, block: i})
		hs = append(hs, links.Heading{Block: i, Text: t})
	}
	for i, a := range links.Anchors(hs) {
		out[i].slug = a
	}
	return out
}

// tocMarkdown is a table of contents' body: a list of links to the
// headings, indented two spaces a level below the highest
// (DocumentOutline::tocMarkdown).
func tocMarkdown(entries []tocEntry) string {
	if len(entries) == 0 {
		return ""
	}
	top := 4
	for _, en := range entries {
		top = min(top, en.level)
	}
	var lines []string
	for _, en := range entries {
		label := strings.NewReplacer("[", `\[`, "]", `\]`).Replace(en.text)
		if label == "" {
			label = "(untitled)"
		}
		lines = append(lines, strings.Repeat("  ", en.level-top)+"- ["+label+"](#"+en.slug+")")
	}
	return strings.Join(lines, "\n")
}

// syncTocs rewrites each table of contents that is out of date, outside the
// undo history, leaving alone one the caret is in.
func (e *Editor) syncTocs() {
	d := e.Doc
	var md string
	done := false
	for i := range d.Blocks {
		b := &d.Blocks[i]
		if !isToc(b) || (d.Focused && d.Caret.Block == b.ID) || d.ReadOnly {
			continue
		}
		if !done {
			md, done = tocMarkdown(d.tocEntries()), true
		}
		if b.Text != md {
			b.Text = md
			d.Dirty = true
		}
	}
}

// The card in design pixels: its inset, padding and corner, the space its
// "Contents" label takes, each entry's height and a level's indent.
const (
	tocInset  = 8
	tocPad    = 8
	tocRadius = 6
	tocLabel  = 18
	tocEntryH = 22
	tocIndent = 16
)

// tocShows reports whether a table of contents is drawn as its card: while
// the caret is elsewhere.
func (e *Editor) tocShows(i int) bool {
	b := &e.Doc.Blocks[i]
	return isToc(b) && !(e.Doc.Focused && e.Doc.Caret.Block == b.ID)
}

// tocHeight is the card's row height.
func (e *Editor) tocHeight() float32 {
	n := max(1, len(e.Doc.tocEntries()))
	return e.px(codeRowTop+2*tocPad+tocLabel+codeRowBottom) + float32(n)*e.px(tocEntryH)
}

// tocCard is the card of the table of contents in row i.
func (e *Editor) tocCard(i int) geom.Rect {
	body := e.bodyRect(i)
	return geom.NewRect(body.X+e.px(tocInset), e.tops[i]+e.px(codeRowTop), body.Width-2*e.px(tocInset),
		e.heights[i]-e.px(codeRowTop+codeRowBottom))
}

// tocEntryAt is the entry of the card in row i under a point, or -1.
func (e *Editor) tocEntryAt(i int, where geom.Point) int {
	c := e.tocCard(i)
	if !where.In(c) {
		return -1
	}
	k := int((where.Y - c.Y - e.px(tocPad+tocLabel)) / e.px(tocEntryH))
	if where.Y < c.Y+e.px(tocPad+tocLabel) || k >= len(e.Doc.tocEntries()) {
		return -1
	}
	return k
}

func (e *Editor) drawToc(gc *unison.Canvas, i int) {
	t := e.tok()
	c := e.tocCard(i)
	r := e.px(tocRadius)
	ground := t.PanelBackground
	if e.blockSel[e.Doc.Blocks[i].ID] {
		ground = t.BlockSelectionTint
	}
	gc.DrawRoundedRect(c, geom.NewSize(r, r), kvitui.Color(ground).Paint(gc, c, paintstyle.Fill))
	e.stroke(gc, c, r, e.px(1), t.Border)
	x, y := c.X+e.px(tocPad), c.Y+e.px(tocPad)
	e.label("Contents", e.chrome(kvitui.RoleSmall, text.Bold, t.TextMuted)).Draw(gc, x, y)
	y += e.px(tocLabel)
	entries := e.Doc.tocEntries()
	if len(entries) == 0 {
		e.label("No headings yet.", e.chrome(kvitui.RoleBody, text.Regular, t.TextFaint)).Draw(gc, x, y)
		return
	}
	top := 4
	for _, en := range entries {
		top = min(top, en.level)
	}
	if from, to, ok := e.drawnLineRange(e.Doc.Blocks[i].ID); ok {
		for k := from; k <= to && k < len(entries); k++ {
			ry := y + float32(k)*e.px(tocEntryH)
			e.fill(gc, geom.NewRect(x, ry, c.Right()-e.px(tocPad)-x, e.px(tocEntryH)), t.SelectionTint)
		}
	}
	for k, en := range entries {
		st := e.chrome(kvitui.RoleStrong, text.Regular, t.Link)
		if e.tocHover == k && e.hover == e.Doc.Blocks[i].ID {
			st.Color, st.Underline = colour(t.Accent), true
		}
		words := en.text
		if words == "" {
			words = "(untitled)"
		}
		ex := x + float32(en.level-top)*e.px(tocIndent)
		l := e.ui.Fonts.Layout([]text.Span{{Text: words, Style: st}}, text.Options{MaxWidth: max(1, c.Right()-e.px(tocPad)-ex), Elide: true})
		l.Draw(gc, ex, y+float32(k)*e.px(tocEntryH))
	}
}

// goToHeading puts the caret at the start of a heading, scrolled into view.
func (e *Editor) goToHeading(block int) {
	if block < 0 || block >= len(e.Doc.Blocks) {
		return
	}
	e.FocusBlock(block, 0)
	r := e.rowRect(block)
	e.ScrollRectIntoView(geom.NewRect(r.X, r.Y, r.Width, max(r.Height, e.px(200))))
}
