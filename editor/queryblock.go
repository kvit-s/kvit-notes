package editor

// Collection query blocks (features.md 1.2.18, Kvit's QueryBlock): a
// code fence of language "query" holds a spec, and while the caret is
// elsewhere the block shows the notes it selects, as a table with a row a
// note or, with "view: board", as columns of cards grouped by a field. A
// spec that does not read shows why, in place of the results. Pressing a
// row or a card opens its note. The answers are the application's, from the
// query package, through RunQuery.

import (
	"fmt"

	"github.com/kvit-s/kvit-notes/query"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// isQuery reports whether a block is a collection query.
func isQuery(b *Block) bool { return b.Kind == Code && b.Lang == "query" }

// queryShows reports whether a query block shows its results: while the
// caret is elsewhere and there is a vault to ask.
func (e *Editor) queryShows(i int) bool {
	b := &e.Doc.Blocks[i]
	return isQuery(b) && e.RunQuery != nil && !(e.Doc.Focused && e.Doc.Caret.Block == b.ID)
}

// The results in design pixels.
const (
	queryPad    = 8
	queryHeadH  = 26
	queryRowH   = 24
	queryCardH  = 28
	queryColW   = 200
	queryColGap = 10
	queryLabelH = 20
)

// queryLayout is where a query block's rows or cards are, and what each
// opens.
type queryLayout struct {
	answer query.Answer
	rows   []geom.Rect // a table's rows, or a board's cards
	paths  []string
	height float32
}

func (e *Editor) layQuery(i int) *queryLayout {
	b := &e.Doc.Blocks[i]
	ql := &queryLayout{answer: e.RunQuery(b.Text)}
	width := e.bodyRight() - e.bodyLeft() - 2*e.px(tocInset)
	pad := e.px(queryPad)
	y := pad + e.px(queryLabelH)
	switch {
	case !ql.answer.OK:
		y += e.px(queryRowH)
	case ql.answer.View == query.ViewBoard:
		tallest := float32(0)
		x := pad
		for _, g := range ql.answer.Groups {
			cy := y + e.px(queryHeadH)
			for _, r := range g.Rows {
				ql.rows = append(ql.rows, geom.NewRect(x+pad, cy, e.px(queryColW)-2*pad, e.px(queryCardH)))
				ql.paths = append(ql.paths, r.Path)
				cy += e.px(queryCardH) + e.px(4)
			}
			tallest = max(tallest, cy-y+pad)
			x += e.px(queryColW + queryColGap)
		}
		y += max(tallest, e.px(queryRowH))
	default:
		y += e.px(queryHeadH)
		for _, r := range ql.answer.Rows {
			ql.rows = append(ql.rows, geom.NewRect(pad, y, width-2*pad, e.px(queryRowH)))
			ql.paths = append(ql.paths, r.Path)
			y += e.px(queryRowH)
		}
		if len(ql.answer.Rows) == 0 {
			y += e.px(queryRowH)
		}
	}
	ql.height = y + pad
	return ql
}

// queryResult is a query block's layout, worked out once per draw.
func (e *Editor) queryResult(i int) *queryLayout {
	b := &e.Doc.Blocks[i]
	if e.queryCache == nil {
		e.queryCache = map[int64]*queryLayout{}
	}
	if ql := e.queryCache[b.ID]; ql != nil && e.queryFresh[b.ID] {
		return ql
	}
	ql := e.layQuery(i)
	e.queryCache[b.ID] = ql
	if e.queryFresh == nil {
		e.queryFresh = map[int64]bool{}
	}
	e.queryFresh[b.ID] = true
	return ql
}

// RefreshQueries works the query blocks out again, after the vault changed.
func (e *Editor) RefreshQueries() {
	clear(e.queryFresh)
	e.changed()
}

// queryCard is the card of a query block in row i.
func (e *Editor) queryCard(i int) geom.Rect {
	body := e.bodyRect(i)
	return geom.NewRect(body.X+e.px(tocInset), e.tops[i]+e.px(codeRowTop), body.Width-2*e.px(tocInset),
		e.heights[i]-e.px(codeRowTop+codeRowBottom))
}

func (e *Editor) drawQuery(gc *unison.Canvas, i int) {
	t := e.tok()
	ql := e.queryResult(i)
	c := e.queryCard(i)
	r := e.px(tocRadius)
	gc.DrawRoundedRect(c, geom.NewSize(r, r), kvitui.Color(t.PanelBackground).Paint(gc, c, paintstyle.Fill))
	e.stroke(gc, c, r, e.px(1), t.Border)
	gc.Save()
	gc.Translate(c.Point)
	defer gc.Restore()
	pad := e.px(queryPad)
	a := ql.answer
	head := "Query"
	if a.OK {
		head = fmt.Sprintf("Query · %d notes", len(a.Rows))
	}
	e.label(head, e.chrome(kvitui.RoleSmall, text.Bold, t.TextMuted)).Draw(gc, pad, pad)
	y := pad + e.px(queryLabelH)
	body := e.chrome(kvitui.RoleBody, text.Regular, t.TextPrimary)
	bold := e.chrome(kvitui.RoleBody, text.Bold, t.TextPrimary)
	cell := func(s string, st text.Style, x, y, w, h float32) {
		l := e.ui.Fonts.Layout([]text.Span{{Text: s, Style: st}}, text.Options{MaxWidth: max(1, w), Elide: true})
		_, lh := l.Size()
		l.Draw(gc, x, y+(h-lh)/2)
	}
	selFrom, selTo, selOK := e.drawnLineRange(e.Doc.Blocks[i].ID)
	switch {
	case !a.OK:
		cell(a.Error, e.chrome(kvitui.RoleBody, text.Regular, t.Danger), pad, y, c.Width-2*pad, e.px(queryRowH))
	case a.View == query.ViewBoard:
		x := pad
		for _, g := range a.Groups {
			col := geom.NewRect(x, y, e.px(queryColW), c.Height-y-pad)
			e.fillRound(gc, col, e.px(4), t.ChipBackground)
			cell(fmt.Sprintf("%s  %d", g.Name, len(g.Rows)), bold, x+pad, y, col.Width-2*pad, e.px(queryHeadH))
			x += e.px(queryColW + queryColGap)
		}
		k := 0
		for _, g := range a.Groups {
			for _, row := range g.Rows {
				rr := ql.rows[k]
				e.fillRound(gc, rr, e.px(4), t.WindowBackground)
				e.stroke(gc, rr, e.px(4), e.px(1), t.Border)
				if selOK && k >= selFrom && k <= selTo {
					e.fillRound(gc, rr, e.px(4), t.SelectionTint)
				}
				words := row.Path
				if len(row.Cells) > 0 {
					words = row.Cells[0]
				}
				cell(words, body, rr.X+e.px(6), rr.Y, rr.Width-e.px(12), rr.Height)
				k++
			}
		}
	default:
		n := max(1, len(a.Columns))
		colW := (c.Width - 2*pad) / float32(n)
		for k, name := range a.Columns {
			cell(name, bold, pad+float32(k)*colW+e.px(4), y, colW-e.px(8), e.px(queryHeadH))
		}
		e.fill(gc, geom.NewRect(pad, y+e.px(queryHeadH)-e.px(1), c.Width-2*pad, e.px(1)), t.Border)
		if len(a.Rows) == 0 {
			cell("No notes match", e.chrome(kvitui.RoleBody, text.Regular, t.TextFaint), pad+e.px(4),
				y+e.px(queryHeadH), c.Width, e.px(queryRowH))
		}
		for ri, row := range a.Rows {
			rr := ql.rows[ri]
			if e.queryHover == ri && e.hover == e.Doc.Blocks[i].ID {
				e.fill(gc, rr, t.HoverTint)
			}
			if selOK && ri >= selFrom && ri <= selTo {
				e.fill(gc, rr, t.SelectionTint)
			}
			for k, v := range row.Cells {
				st := body
				if k == 0 {
					st.Color = colour(t.Link)
				}
				cell(v, st, pad+float32(k)*colW+e.px(4), rr.Y, colW-e.px(8), rr.Height)
			}
		}
	}
}

// queryRowAt is the row or card of the query block in row i under a point,
// or -1.
func (e *Editor) queryRowAt(i int, where geom.Point) int {
	ql := e.queryResult(i)
	p := where.Sub(e.queryCard(i).Point)
	for k, r := range ql.rows {
		if p.In(r) {
			return k
		}
	}
	return -1
}

// QueryText is a query block's answer as text, for tests: the error, or
// each row's cells.
func (e *Editor) QueryText(i int) string {
	a := e.queryResult(i).answer
	if !a.OK {
		return "error: " + a.Error
	}
	out := fmt.Sprint(a.Columns) + "\n"
	for _, r := range a.Rows {
		out += fmt.Sprint(r.Cells) + "\n"
	}
	return out
}
