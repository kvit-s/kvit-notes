package editor

// Column widths as a browser sizes them (Embedding.FitTables), a table
// scrolling sideways when it cannot fit (Embedding.ScrollTables), and text
// narrower than the editor with tables and code past it
// (Embedding.TextWidth), over the tables agents write in their replies.

import (
	"fmt"
	"math"
	"strings"
	"testing"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// The five tables of the brief: a short report, a table with one column of
// prose, eight numeric columns, twelve columns of long words that cannot
// fit, and a column holding a long path with no spaces.
const (
	fitReport = "| Case | Before | After |\n| --- | --- | --- |\n" +
		"| Empty note | 12 ms | 3 ms |\n| 1,237 blocks | 410 ms | 95 ms |\n| Wide table | 38 ms | 9 ms |"
	fitProse = "| File | Lines | What changed | Tests |\n| --- | ---: | --- | --- |\n" +
		"| tablefit.go | +112 | Each column now starts at its longest word and takes a share of the room left in proportion to how much more it wants, so the short columns of a report keep their numbers and names on one line while the description wraps, as a browser lays a table out | passed |\n" +
		"| table.go | +31 | The grid is laid out in the room the embedding gives it rather than the text width | passed |\n" +
		"| seams.go | +38 | Three options | none |"
	fitNumbers = "| Run | p50 | p90 | p99 | Max | Mean | Count | Errors |\n| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n" +
		"| 1 | 12.4 | 18.9 | 31.2 | 44.0 | 14.1 | 1237 | 0 |\n| 2 | 11.8 | 17.5 | 29.9 | 41.3 | 13.6 | 1237 | 0 |\n| 3 | 12.1 | 19.2 | 35.0 | 52.8 | 14.4 | 1237 | 2 |"
	fitPath = "| Step | File | State | Note |\n| ---: | --- | --- | --- |\n" +
		"| 1 | docs/proposals/chat-look/kvit-notes-tables-brief.md | Draft | Written from Kvit Works for the agent working in kvit-notes, with the checks the owner asked for and what is left out |\n" +
		"| 2 | editor/tablefit.go | Done | The column sizing |"
	// fitTimes has short cells with a space in them beside a long sentence.
	fitTimes = "| Case | Before | After | Why |\n| --- | --- | --- | --- |\n" +
		"| Empty note | 410 ms | 95 ms | The grid was laid out twice on every draw because the cache key held the caret, which moved on every blink, " +
		"so nothing was ever reused and every frame measured every cell of every table in the note again from the start |\n" +
		"| Wide table | 38 ms | 9 ms | Fewer layouts |"
)

// fitWide is twelve columns of words too long for even 1,220 pixels.
var fitWide = func() string {
	words := []string{"Implementation", "Documentation", "Verification", "Reconciliation", "Synchronization", "Authentication",
		"Configuration", "Serialization", "Normalization", "Optimization", "Transformation", "Visualization"}
	var b strings.Builder
	b.WriteString("| " + strings.Join(words, " | ") + " |\n|" + strings.Repeat(" --- |", len(words)) + "\n")
	for r := range 3 {
		cells := make([]string, len(words))
		for c := range words {
			cells[c] = []string{"available", "pending", "unchecked"}[(r+c)%3]
		}
		b.WriteString("| " + strings.Join(cells, " | ") + " |\n")
	}
	return strings.TrimSuffix(b.String(), "\n")
}()

// fitRooms are the two rooms the brief asks about: the reading column, and
// the column with the room right of it on the owner's screen.
var fitRooms = []float32{760, 1220}

// fitted lays md out as a table at room design pixels, as an embedding with
// fitted, scrolling tables does.
func fitted(e *Editor, md string, room float32) *grid {
	return e.layOutGrid(ParseTable(md), e.px(room), e.px(room), nil, nil)
}

// naturalWidth is column c's widest cell on one line, with its padding: the
// widest a fitted column is made.
func naturalWidth(e *Editor, md string, c int) float32 {
	t := ParseTable(md)
	w := e.px(minColumn)
	for r, row := range append([][]string{t.Headers}, t.Rows...) {
		cw, _ := e.cellLayout(row[c], r == 0, 0).Size()
		w = max(w, float32(math.Ceil(float64(cw)))+1+2*e.px(cellPadX))
	}
	return w
}

// columnWraps reports whether any cell of column c takes more than one line.
func columnWraps(g *grid, c int) bool {
	for r := range g.cells {
		if g.cells[r][c].LineCount() > 1 {
			return true
		}
	}
	return false
}

// checkOneLine fails for each listed column whose cells wrap.
func checkOneLine(t *testing.T, name string, g *grid, cols ...int) {
	t.Helper()
	for _, c := range cols {
		if columnWraps(g, c) {
			t.Errorf("%s: column %d wraps at %.0f px (columns %v)", name, c, g.cols[c], g.cols)
		}
	}
}

// checkNotStretched fails when the last column is wider than its widest
// cell needs.
func checkNotStretched(t *testing.T, name string, e *Editor, md string, g *grid) {
	t.Helper()
	last := len(g.cols) - 1
	if nat := naturalWidth(e, md, last); g.cols[last] > nat+0.5 {
		t.Errorf("%s: the last column is %.0f px, its widest cell needs %.0f", name, g.cols[last], nat)
	}
}

func fitEditor(t *testing.T, emb Embedding) (*uitest.Session, *Editor) {
	t.Helper()
	s, e := openEditor(t, "Text.\n")
	s.Do(func() { e.Embed(emb) })
	return s, e
}

// A short report fits as it is: every cell on one line, each column as wide
// as its widest cell, and the table only as wide as they are.
func TestAFittedReportIsAsWideAsItsCells(t *testing.T) {
	s, e := fitEditor(t, Embedding{NoGutter: true, FitTables: true, ScrollTables: true})
	s.Do(func() {
		for _, room := range fitRooms {
			name := fmt.Sprintf("report at %v", room)
			g := fitted(e, fitReport, room)
			checkOneLine(t, name, g, 0, 1, 2)
			for c := range g.cols {
				if nat := naturalWidth(e, fitReport, c); g.cols[c] != nat {
					t.Errorf("%s: column %d is %v px, its widest cell needs %v", name, c, g.cols[c], nat)
				}
			}
			if g.width() >= e.px(room)/2 {
				t.Errorf("%s: %v px wide in %v", name, g.width(), e.px(room))
			}
		}
	})
}

// A column of prose wraps while the file names, numbers and words beside it
// stay on one line, and the table keeps within its room.
func TestAFittedProseColumnWrapsAndTheShortOnesDoNot(t *testing.T) {
	s, e := fitEditor(t, Embedding{NoGutter: true, FitTables: true, ScrollTables: true})
	s.Do(func() {
		for _, room := range fitRooms {
			name := fmt.Sprintf("prose at %v", room)
			g := fitted(e, fitProse, room)
			checkOneLine(t, name, g, 0, 1, 3)
			if !columnWraps(g, 2) {
				t.Errorf("%s: the prose column does not wrap at %v px", name, g.cols[2])
			}
			if g.width() > e.px(room) {
				t.Errorf("%s: %v px wide in %v", name, g.width(), e.px(room))
			}
			if g.width() < e.px(room)-float32(len(g.cols)) {
				t.Errorf("%s: %v px of %v used while the prose wraps", name, g.width(), e.px(room))
			}
			checkNotStretched(t, name, e, fitProse, g)
		}
	})
}

// Short cells with a space in them, such as "95 ms", stay on one line beside
// a long sentence, and of two prose columns the longer gets more of the room.
func TestShortCellsWithSpacesStayOnOneLineBesideProse(t *testing.T) {
	s, e := fitEditor(t, Embedding{NoGutter: true, FitTables: true, ScrollTables: true})
	why := ParseTable(fitTimes).Rows[0][3]
	times := fitTimes
	two := "| Step | Before | After |\n| --- | --- | --- |\n| 1 | " + why + " | " + why[:len(why)/3] + " |"
	s.Do(func() {
		for _, room := range fitRooms {
			name := fmt.Sprintf("times at %v", room)
			g := fitted(e, times, room)
			checkOneLine(t, name, g, 0, 1, 2)
			if !columnWraps(g, 3) {
				t.Errorf("%s: the sentence does not wrap at %v px", name, g.cols[3])
			}
			checkNotStretched(t, name, e, times, g)
			p := fitted(e, two, room)
			if p.cols[1] <= p.cols[2] {
				t.Errorf("two prose columns at %v: the longer has %v px, the shorter %v", room, p.cols[1], p.cols[2])
			}
		}
	})
}

// Eight numeric columns keep every number on one line.
func TestFittedNumbersStayOnOneLine(t *testing.T) {
	s, e := fitEditor(t, Embedding{NoGutter: true, FitTables: true, ScrollTables: true})
	s.Do(func() {
		for _, room := range fitRooms {
			name := fmt.Sprintf("numbers at %v", room)
			g := fitted(e, fitNumbers, room)
			checkOneLine(t, name, g, 0, 1, 2, 3, 4, 5, 6, 7)
			checkNotStretched(t, name, e, fitNumbers, g)
		}
	})
}

// Twelve columns of long words are wider than either room at their least
// widths, so the table keeps those widths and is wider than its room, for
// scrolling; without ScrollTables it is squeezed into the room as before.
func TestTwelveLongColumnsOverflowTheirRoom(t *testing.T) {
	s, e := fitEditor(t, Embedding{NoGutter: true, FitTables: true, ScrollTables: true})
	s.Do(func() {
		for _, room := range fitRooms {
			name := fmt.Sprintf("twelve at %v", room)
			g := fitted(e, fitWide, room)
			if g.width() <= e.px(room) {
				t.Errorf("%s: %v px wide, which fits %v", name, g.width(), e.px(room))
			}
			checkOneLine(t, name, g, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)
			checkNotStretched(t, name, e, fitWide, g)
		}
		e.Embed(Embedding{NoGutter: true, FitTables: true})
		for _, room := range fitRooms {
			if g := fitted(e, fitWide, room); g.width() > e.px(room)+float32(len(g.cols)) {
				t.Errorf("without scrolling the table is %v px wide in %v", g.width(), e.px(room))
			}
		}
	})
}

// A long path is one piece: its column is at least as wide as the path, so
// the path and the short columns beside it stay on one line while the note
// wraps.
func TestAFittedPathStaysWhole(t *testing.T) {
	s, e := fitEditor(t, Embedding{NoGutter: true, FitTables: true, ScrollTables: true})
	s.Do(func() {
		for _, room := range fitRooms {
			name := fmt.Sprintf("path at %v", room)
			g := fitted(e, fitPath, room)
			checkOneLine(t, name, g, 0, 1, 2)
			if !columnWraps(g, 3) {
				t.Errorf("%s: the note does not wrap at %v px", name, g.cols[3])
			}
			checkNotStretched(t, name, e, fitPath, g)
		}
	})
}

// A piece runs from one space to the next: a path or a hyphenated name is
// whole, while text written without spaces breaks between its characters.
func TestAPieceRunsFromSpaceToSpace(t *testing.T) {
	s, e := fitEditor(t, Embedding{FitTables: true})
	s.Do(func() {
		width := func(s string) float32 {
			w, _ := e.cellLayout(s, false, 0).Size()
			return w
		}
		path := "docs/proposals/chat-look/kvit-notes-tables-brief.md"
		if got, want := longestPiece(e.cellLayout(path, false, 0)), width(path); math.Abs(float64(got-want)) > 1 {
			t.Errorf("the path's widest piece is %v, the path %v", got, want)
		}
		if got, want := longestPiece(e.cellLayout("a kvit-notes table", false, 0)), width("kvit-notes"); math.Abs(float64(got-want)) > 1 {
			t.Errorf("the widest piece of a phrase is %v, its longest word %v", got, want)
		}
		chinese := "表格的列宽按内容决定"
		if got, whole := longestPiece(e.cellLayout(chinese, false, 0)), width(chinese); got <= 0 || got > whole/3 {
			t.Errorf("the widest piece of Chinese text is %v of %v", got, whole)
		}
		if got := longestPiece(e.cellLayout("one<br>longest<br>two", false, 0)); math.Abs(float64(got-width("longest"))) > 1 {
			t.Errorf("the widest piece across line breaks is %v, the longest line %v", got, width("longest"))
		}
	})
}

// Without the options, a table is laid out as the note lays it out: the
// short report's last column is widened to the text's width, and a table of
// prose is scaled down as a whole.
func TestTablesWithoutTheOptionsAreLaidOutAsBefore(t *testing.T) {
	s, e := openEditor(t, fitReport+"\n")
	s.Do(func() {
		g, ok := e.gridFor(0)
		if !ok {
			t.Fatal("no grid")
		}
		if w := g.width(); w != e.textWidth(&e.Doc.Blocks[0]) {
			t.Errorf("the report is %v px wide, the text %v", w, e.textWidth(&e.Doc.Blocks[0]))
		}
		if e.tableBarRoom(0) != 0 || e.wideExtra(0) != 0 || e.pageWidth() != e.width() {
			t.Error("a note's table scrolls or reaches past the text")
		}
	})
}

// A table that cannot fit scrolls sideways under its own scrollbar: Shift
// with the wheel moves it, a press lands on the cell drawn under it, and a
// cell made live is brought round into the view.
func TestATableWiderThanItsRoomScrollsSideways(t *testing.T) {
	var e *Editor
	s := uitest.Open(t, uitest.Options{Width: 900, Height: 700}, func(ui *kvitui.UI) unison.Paneler {
		e = New(ui, NewDoc(ParseMarkdown("Before.\n\n"+fitWide+"\n\nAfter.\n")))
		e.Embed(Embedding{NoGutter: true, FitTables: true, ScrollTables: true})
		return kvitui.Width(ui, kvitui.Px(760), e)
	})
	var bar geom.Rect
	var id int64
	s.Do(func() {
		id = e.Doc.Blocks[1].ID
		g, ok := e.tableScrolls(1)
		if !ok {
			t.Fatal("the twelve columns do not scroll")
		}
		r, ok := e.codeBarRect(1)
		if !ok || r.Width != e.tableRoom(1) {
			t.Fatalf("scrollbar %+v ok=%v, room %v", r, ok, e.tableRoom(1))
		}
		bar = e.RectToRoot(r)
		if want := e.px(codeRowTop+codeRowBottom) + g.height + e.px(tableBarGap+codeBarH); e.heights[1] != want {
			t.Errorf("row %v tall, want %v", e.heights[1], want)
		}
		if e.PartRect(1, "codebar").Width <= 0 {
			t.Error("no probe for the table's scrollbar")
		}
	})
	// Shift and the wheel over the grid scroll it.
	var over geom.Point
	s.Do(func() {
		c := e.cellRect(1, mustGrid(t, e, 1), 0, 0)
		over = e.PointToRoot(geom.NewPoint(c.X+10, c.Y+c.Height/2))
	})
	s.Screen.Wheel(over, geom.NewPoint(0, -3), mod.Shift)
	s.Sync()
	var scroll float32
	s.Do(func() {
		scroll = e.codeScrollOf(id)
		if scroll <= 0 {
			t.Fatalf("Shift and the wheel did not scroll the table")
		}
		// Scrolled past the first column, the view's left edge is over the
		// second, and a point left of the view is over no cell.
		g := mustGrid(t, e, 1)
		e.setCodeScroll(id, g.cols[0]+5)
		home := e.gridHome(1)
		y := home.Y + g.rowsH[0] + 5
		if _, col, ok := e.tableCellAt(1, g, geom.NewPoint(home.X+2, y)); !ok || col != 1 {
			t.Errorf("scrolled %v, the view's left edge is on column %d (ok=%v)", e.codeScrollOf(id), col, ok)
		}
		if _, _, ok := e.tableCellAt(1, g, geom.NewPoint(home.X-2, y)); ok {
			t.Error("a point left of the view hit a cell")
		}
		// The last column, made live, comes into view.
		e.setCodeScroll(id, 0)
		e.revealTableCell(1, len(g.cols)-1)
		view := e.tableView(1, g)
		if r := e.cellRect(1, g, 0, len(g.cols)-1); r.Right() > view.Right()+0.5 || r.X < view.X {
			t.Errorf("the last cell %+v is not in the view %+v", r, view)
		}
	})
	// A press on the scrollbar's start scrolls home.
	s.Screen.MouseDown(geom.NewPoint(bar.X+1, bar.Y+bar.Height/2), unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(geom.NewPoint(bar.X+1, bar.Y+bar.Height/2), unison.ButtonLeft, mod.None)
	s.Sync()
	s.Do(func() {
		if got := e.codeScrollOf(id); got != 0 {
			t.Errorf("a press at the scrollbar's start left the table at %v", got)
		}
	})
}

func mustGrid(t *testing.T, e *Editor, i int) *grid {
	t.Helper()
	g, ok := e.gridFor(i)
	if !ok {
		t.Fatal("no grid")
	}
	return g
}

// Under a TextWidth the text wraps in the column while a table and a code
// block use the room right of it, each only as far as its content needs.
func TestTextWrapsAtTheTextWidthAndTablesUseTheRoomPastIt(t *testing.T) {
	long := strings.Repeat("A sentence that keeps going so that it wraps. ", 12)
	md := long + "\n\n" + fitProse + "\n\n" + fitReport + "\n\n```go\nfunc f() { return \"" + strings.Repeat("x", 200) + "\" }\n```\n\n```go\nshort()\n```\n"
	var paraNarrow, codeNarrow float32
	t.Run("760", func(t *testing.T) {
		var e *Editor
		s := uitest.Open(t, uitest.Options{Width: 900, Height: 900}, func(ui *kvitui.UI) unison.Paneler {
			e = New(ui, NewDoc(ParseMarkdown(md)))
			e.Embed(Embedding{NoGutter: true, FitTables: true, ScrollTables: true})
			return kvitui.Width(ui, kvitui.Px(760), e)
		})
		s.Do(func() {
			paraNarrow = e.textWidth(&e.Doc.Blocks[0])
			codeNarrow = e.codeViewport(3)
		})
	})
	var e *Editor
	s := uitest.Open(t, uitest.Options{Width: 1400, Height: 900}, func(ui *kvitui.UI) unison.Paneler {
		e = New(ui, NewDoc(ParseMarkdown(md)))
		e.Embed(Embedding{NoGutter: true, FitTables: true, ScrollTables: true, TextWidth: 760})
		return kvitui.Width(ui, kvitui.Px(1220), e)
	})
	s.Do(func() {
		if w := e.textWidth(&e.Doc.Blocks[0]); w != paraNarrow {
			t.Errorf("the paragraph wraps at %v, in a 760 editor at %v", w, paraNarrow)
		}
		if e.width() != e.px(1220) {
			t.Fatalf("the editor is %v wide", e.width())
		}
		// The prose table takes the room past the text, and no more than it.
		g := mustGrid(t, e, 1)
		room := e.tableRoom(1)
		if room <= e.textWidth(&e.Doc.Blocks[1])+e.px(400) {
			t.Errorf("the table's room is %v, the text's %v", room, e.textWidth(&e.Doc.Blocks[1]))
		}
		if g.width() > room || g.width() < room-float32(len(g.cols)) {
			t.Errorf("the prose table is %v wide in %v", g.width(), room)
		}
		if body := e.bodyRect(1); body.Right() <= e.bodyRight() || body.Right() > e.wideRight() {
			t.Errorf("the table's body ends at %v, the text at %v, the room at %v", body.Right(), e.bodyRight(), e.wideRight())
		}
		// The report is as wide as its cells, inside the text column.
		if r := mustGrid(t, e, 2); r.width() >= e.textWidth(&e.Doc.Blocks[2]) || e.wideExtra(2) != 0 {
			t.Errorf("the report is %v wide, reaching %v past the text", r.width(), e.wideExtra(2))
		}
		// The long code line takes the room past the text, up to the
		// editor's margin, and scrolls past that; the short one keeps the
		// text's width.
		if v := e.codeViewport(3); v <= codeNarrow || v != e.wideWidth(&e.Doc.Blocks[3]) {
			t.Errorf("the long code block shows %v, at 760 %v, at most %v", v, codeNarrow, e.wideWidth(&e.Doc.Blocks[3]))
		}
		if e.codeMaxScroll(3) <= 0 {
			t.Error("the long code line does not scroll past the room")
		}
		if p := e.codePanel(3); p.Right() > e.wideRight() {
			t.Errorf("the code panel ends at %v, past %v", p.Right(), e.wideRight())
		}
		if v := e.codeViewport(4); v != e.textWidth(&e.Doc.Blocks[4]) {
			t.Errorf("the short code block shows %v, the text %v", v, e.textWidth(&e.Doc.Blocks[4]))
		}
	})
}

// In a table that scrolls, a cell made live is brought into the view with
// its field over the part of it shown, and a sweep dragged past the view's
// right edge scrolls the grid and reaches the column now at that edge.
func TestALiveCellAndASweepFollowAScrollingTable(t *testing.T) {
	var e *Editor
	s := uitest.Open(t, uitest.Options{Width: 900, Height: 700}, func(ui *kvitui.UI) unison.Paneler {
		e = New(ui, NewDoc(ParseMarkdown("Before.\n\n"+fitWide+"\n\nAfter.\n")))
		e.Embed(Embedding{FitTables: true, ScrollTables: true})
		return kvitui.Width(ui, kvitui.Px(760), e)
	})
	var id int64
	last := 11
	s.Do(func() {
		id = e.Doc.Blocks[1].ID
		if _, ok := e.tableScrolls(1); !ok {
			t.Fatal("the twelve columns do not scroll")
		}
		e.activateTableCell(1, 0, last, false)
	})
	s.Sync()
	s.Do(func() {
		g := mustGrid(t, e, 1)
		if e.codeScrollOf(id) <= 0 {
			t.Fatal("making the last cell live did not scroll the table")
		}
		view := e.tableView(1, g)
		if r := e.cellRect(1, g, 0, last); r.X < view.X || r.Right() > view.Right()+0.5 {
			t.Errorf("the live cell %+v is outside the view %+v", r, view)
		}
		if f := e.tableField; f == nil {
			t.Error("no field over the live cell")
		} else if r := e.RectFromRoot(f.AsPanel().RectToRoot(f.AsPanel().ContentRect(true))); r.X < view.X-1 || r.Right() > view.Right()+1 {
			t.Errorf("the field %+v is outside the view %+v", r, view)
		}
		e.commitTableField()
		e.setCodeScroll(id, 0)
	})
	s.Sync()
	// A sweep from the first cell dragged past the view's right edge.
	var from, past geom.Point
	s.Do(func() {
		g := mustGrid(t, e, 1)
		c := e.cellRect(1, g, 0, 0)
		from = e.PointToRoot(geom.NewPoint(c.X+10, c.Y+c.Height/2))
		v := e.tableView(1, g)
		past = e.PointToRoot(geom.NewPoint(v.Right()+40, c.Y+c.Height/2))
	})
	s.Screen.MouseDown(from, unison.ButtonLeft, mod.None)
	s.Screen.MouseMove(from.Add(geom.NewPoint(30, 0)), mod.None)
	s.Screen.MouseMove(past, mod.None)
	s.Sync()
	s.Do(func() {
		if e.codeScrollOf(id) <= 0 {
			t.Error("dragging past the view's edge did not scroll the table")
		}
		if _, _, _, left, right, ok := e.TableSelection(); !ok || left != 0 || right < 5 {
			t.Errorf("the sweep covers columns %d to %d (ok=%v)", left, right, ok)
		}
	})
	s.Screen.MouseUp(past, unison.ButtonLeft, mod.None)
}
