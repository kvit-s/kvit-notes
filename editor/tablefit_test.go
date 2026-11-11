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

// The same three options through a read-only document (DocumentOptions), as
// a host that shows a reply sets them: five tables in one document, each
// under a line of text, and a code block with a long line.
const (
	fitDocProse = "Columns nobody dragged are sized as a browser sizes them: a column of short cells keeps the width of " +
		"its widest cell, and a column of sentences takes what is left and wraps inside it, never narrower than its longest word."
	fitDocPath = "docs/proposals/chat-look/kvit-notes-tables-brief.md"
	fitDocNote = "A short report.\n\n" +
		"| Case | Before | After |\n|---|---|---|\n| Open a note | 412 ms | 96 ms |\n| Scroll to the end | 31 ms | 12 ms |\n| Type a line | 9 ms | 4 ms |\n\n" +
		"A column of prose.\n\n" +
		"| File | Lines | What changed | Tests |\n|---|---|---|---|\n" +
		"| `editor/tablefit.go` | +140 −0 | " + fitDocProse + " | 12 pass |\n" +
		"| `editor/table.go` | +38 −21 | The grid asks for the widths. | 4 pass |\n\n" +
		"Eight columns of numbers.\n\n" +
		"| Run | p50 | p90 | p99 | Max | Mean | Count | Errors |\n|---|---|---|---|---|---|---|---|\n" +
		"| 1 | 12.4 | 18.9 | 31.0 | 44.2 | 13.7 | 10,000 | 0 |\n| 2 | 12.1 | 19.4 | 29.8 | 51.6 | 13.5 | 10,000 | 2 |\n\n" +
		"Twelve columns of words.\n\n" +
		"| Configuration | Authentication | Serialization | Documentation | Accessibility | Compatibility | Implementation | Infrastructure | Notifications | Permissions | Observability | Localization |\n" +
		"|---|---|---|---|---|---|---|---|---|---|---|---|\n" +
		"| unchanged | rewritten | unchanged | incomplete | verified | unverified | finished | provisioned | disabled | restricted | instrumented | translated |\n\n" +
		"A long path.\n\n" +
		"| Document | State | Owner |\n|---|---|---|\n| " + fitDocPath + " | written | notes |\n| `CLAUDE.md` | read | notes |\n\n" +
		"```go\n// A code block's long line: it takes the room to the right of the text before it scrolls sideways, as a table does there.\nfunc fit() {}\n```\n"
)

// openFitDocument shows fitDocNote read-only in a document width design
// pixels wide.
func openFitDocument(t *testing.T, width float32, o DocumentOptions) (*uitest.Session, *Editor) {
	t.Helper()
	var d *Document
	s := uitest.Open(t, uitest.Options{Width: 1400, Height: 1200}, func(ui *kvitui.UI) unison.Paneler {
		d = NewDocument(ui, o)
		d.SetMarkdown(fitDocNote)
		return holdWide(width, d)
	})
	s.Sync()
	return s, d.Editor
}

// widePanel holds its child at a width of its own choosing, whatever the
// window gives it, and at the height the child takes at that width.
type widePanel struct {
	unison.Panel
	w float32
}

func holdWide(w float32, child unison.Paneler) *widePanel {
	p := &widePanel{w: w}
	p.Self = p
	p.AddChild(child)
	p.SetLayout(p)
	return p
}

func (p *widePanel) LayoutSizes(*unison.Panel, geom.Size) (minSize, prefSize, maxSize geom.Size) {
	_, pref, _ := p.Children()[0].Sizes(geom.NewSize(p.w, 0))
	size := geom.NewSize(p.w, pref.Height)
	return size, size, size
}

func (p *widePanel) PerformLayout(*unison.Panel) {
	p.Children()[0].SetFrameRect(geom.NewRect(0, 0, p.w, p.ContentRect(false).Height))
}

// tableBlocks are the indexes of the document's table blocks, in order.
func tableBlocks(e *Editor) []int {
	var out []int
	for i, b := range e.Doc.Blocks {
		if b.Kind == Table {
			out = append(out, i)
		}
	}
	return out
}

// cellLines is how many lines cell (row, col) of a grid takes, the header
// being row 0.
func cellLines(g *grid, row, col int) int { return g.cells[row][col].LineCount() }

// wrappedCells names the cells of a grid drawn on more than one line.
func wrappedCells(g *grid) []string {
	var out []string
	for r := range g.cells {
		for c := range g.cells[r] {
			if cellLines(g, r, c) > 1 {
				out = append(out, g.cells[r][c].Text())
			}
		}
	}
	return out
}

// naturalColumn is the width a column needs for its widest cell on one
// line, read from a grid none of whose cells wrap.
func naturalColumn(e *Editor, g *grid, c int) float32 {
	var widest float64
	for r := range g.cells {
		w, _ := g.cells[r][c].Size()
		widest = max(widest, float64(w))
	}
	return max(e.px(minColumn), float32(math.Ceil(widest))+1+2*e.px(cellPadX))
}

// near reports whether two widths are the same but for rounding.
func near(a, b float32) bool { return math.Abs(float64(a-b)) < 0.01 }

// The five tables of a document at the reading column's width and at the
// width a reply has with room to the right of the column: short columns stay
// on one line, the column of prose wraps, the twelve columns scroll sideways,
// and no table's last column is stretched to the right edge.
func TestADocumentFitsItsTablesAsABrowserFitsThem(t *testing.T) {
	for _, w := range []struct {
		name   string
		window float32
		o      DocumentOptions
	}{
		{"760", 760, DocumentOptions{FitTables: true, ScrollTables: true}},
		{"1220", 1220, DocumentOptions{FitTables: true, ScrollTables: true, TextWidth: 760}},
	} {
		t.Run(w.name, func(t *testing.T) {
			s, e := openFitDocument(t, w.window, w.o)
			s.Do(func() {
				tables := tableBlocks(e)
				if len(tables) != 5 {
					t.Fatalf("%d tables", len(tables))
				}
				grids := make([]*grid, len(tables))
				for k, i := range tables {
					grids[k] = mustGrid(t, e, i)
					room := e.tableRoom(i)
					if want := e.textWidth(&e.Doc.Blocks[i]) + e.width() - e.px(w.o.TextWidth); w.o.TextWidth > 0 && !near(room, want) {
						t.Errorf("table %d is laid out in %v, want %v", k+1, room, want)
					}
					if e.gridHome(i).X+room > e.width() {
						t.Errorf("table %d has room to %v in an editor %v wide", k+1, e.gridHome(i).X+room, e.width())
					}
				}
				// Tables 1, 3 and 5 fit as they are: nothing wraps, and each is
				// as wide as its cells, short of the room.
				for _, k := range []int{0, 2, 4} {
					g, room := grids[k], e.tableRoom(tables[k])
					if wrapped := wrappedCells(g); len(wrapped) > 0 {
						t.Errorf("table %d wraps %q", k+1, wrapped)
					}
					if g.width() >= room {
						t.Errorf("table %d is %v wide in %v: stretched", k+1, g.width(), room)
					}
					for c := range g.cols {
						if natural := naturalColumn(e, g, c); g.cols[c] != natural {
							t.Errorf("table %d column %d is %v wide, its widest cell needs %v", k+1, c+1, g.cols[c], natural)
						}
					}
					if _, scrolls := e.tableScrolls(tables[k]); scrolls {
						t.Errorf("table %d scrolls", k+1)
					}
				}
				// Table 2: the prose wraps inside the room, and the file, the
				// counts of two words and the tests stay on one line.
				g, room := grids[1], e.tableRoom(tables[1])
				if cellLines(g, 1, 2) < 2 {
					t.Errorf("the prose is on %d line", cellLines(g, 1, 2))
				}
				for r := range g.cells {
					for _, c := range []int{0, 1, 3} {
						if cellLines(g, r, c) != 1 {
							t.Errorf("table 2 wraps %q", g.cells[r][c].Text())
						}
					}
				}
				if g.width() > room || g.width() < room-float32(len(g.cols)) {
					t.Errorf("table 2 is %v wide in %v", g.width(), room)
				}
				// Table 4: every word whole, wider than the room, scrolled.
				g, room = grids[3], e.tableRoom(tables[3])
				if wrapped := wrappedCells(g); len(wrapped) > 0 {
					t.Errorf("table 4 wraps %q", wrapped)
				}
				if _, scrolls := e.tableScrolls(tables[3]); !scrolls || g.width() <= room || e.codeMaxScroll(tables[3]) != g.width()-room {
					t.Errorf("table 4 is %v wide in %v and scrolls %v", g.width(), room, e.codeMaxScroll(tables[3]))
				}
				if r := e.rowRect(tables[3]); r.Right() > e.width()+0.5 {
					t.Errorf("table 4's row ends at %v in a window %v wide", r.Right(), e.width())
				}
			})
		})
	}
}

// With the options off the same document's tables are laid out as a note's
// are: every table spans the room at least, its last column stretched to the
// right edge where the others leave room, and none scrolls.
func TestADocumentWithoutTheOptionsSpansTheRoom(t *testing.T) {
	s, e := openFitDocument(t, 760, DocumentOptions{})
	s.Do(func() {
		for k, i := range tableBlocks(e) {
			g, room := mustGrid(t, e, i), e.tableRoom(i)
			// Columns scaled down stop at the narrowest a column is made, so
			// a table may also come out wider than the room.
			if g.width() < room-1 {
				t.Errorf("table %d is %v wide in %v", k+1, g.width(), room)
			}
			if _, scrolls := e.tableScrolls(i); scrolls || e.tableBarRoom(i) != 0 || e.gridOrigin(i) != e.gridHome(i) {
				t.Errorf("table %d scrolls", k+1)
			}
			if e.rowRect(i).Right() != e.bodyRight() {
				t.Errorf("table %d's row ends at %v, the text at %v", k+1, e.rowRect(i).Right(), e.bodyRight())
			}
		}
		if e.wideRight() != e.bodyRight() || e.pageWidth() != e.width() {
			t.Errorf("text is kept to %v of %v", e.pageWidth(), e.width())
		}
	})
}

// The sharing itself, in numbers: natural widths that fit are kept; short
// columns keep theirs when the whole does not fit and the wide ones share
// the rest by what each still wants; a dragged column is left out; and when
// not even the least widths fit, they are used only where the table scrolls.
func TestFitColumnsSharesTheRoom(t *testing.T) {
	s, e := openEditor(t, "Text.\n")
	none := []bool{false, false, false, false}
	for _, c := range []struct {
		name          string
		natural       []float32
		least         []float32
		pinned        []bool
		room          float32
		fit, scroll   bool
		want          []float32
		wantFitted    bool
		wantUntouched bool
	}{
		{name: "fits", natural: []float32{100, 60, 300, 60}, least: []float32{100, 48, 80, 48}, pinned: none, room: 700,
			fit: true, want: []float32{100, 60, 300, 60}, wantFitted: true},
		{name: "fits, sizing off", natural: []float32{100, 60, 300, 60}, least: []float32{100, 48, 80, 48}, pinned: none, room: 700,
			scroll: true, wantUntouched: true},
		{name: "one prose column", natural: []float32{120, 70, 1500, 70}, least: []float32{120, 48, 90, 48}, pinned: none, room: 740,
			fit: true, want: []float32{120, 70, 480, 70}, wantFitted: true},
		{name: "two prose columns", natural: []float32{120, 600, 1500, 120}, least: []float32{120, 80, 90, 48}, pinned: none, room: 740,
			fit: true, want: []float32{120, 168, 331, 120}, wantFitted: true},
		{name: "a dragged column", natural: []float32{200, 70, 1500, 70}, least: []float32{120, 48, 90, 48},
			pinned: []bool{true, false, false, false}, room: 740, fit: true, want: []float32{200, 70, 400, 70}, wantFitted: true},
		{name: "too wide, scrolled", natural: []float32{300, 300, 300, 300}, least: []float32{250, 250, 250, 250}, pinned: none, room: 740,
			fit: true, scroll: true, want: []float32{250, 250, 250, 250}, wantFitted: true},
		{name: "too wide, not scrolled", natural: []float32{300, 300, 300, 300}, least: []float32{250, 250, 250, 250}, pinned: none,
			room: 740, fit: true, wantUntouched: true},
	} {
		s.Do(func() {
			e.Embed(Embedding{FitTables: c.fit, ScrollTables: c.scroll})
			// A column comes in at its dragged width, or at its natural one.
			cols := append([]float32(nil), c.natural...)
			fitted := e.fitColumns(cols, c.pinned, c.natural, c.least, c.room)
			want := c.want
			if c.wantUntouched {
				want = c.natural
			}
			if fitted != c.wantFitted {
				t.Errorf("%s: fitted %v", c.name, fitted)
			}
			for k := range cols {
				if cols[k] < want[k]-1 || cols[k] > want[k]+1 {
					t.Errorf("%s: columns %v, want %v", c.name, cols, want)
					break
				}
			}
		})
	}
}

// A no-break space keeps the two words around it in one piece, so a column
// is never made narrower than both together.
func TestANoBreakSpaceKeepsAPieceWhole(t *testing.T) {
	s, e := fitEditor(t, Embedding{FitTables: true})
	s.Do(func() {
		width := func(s string) float32 {
			w, _ := e.cellLayout(s, false, 0).Size()
			return w
		}
		for _, joined := range []string{"95 ms", "10 000", "12 345"} {
			if got, want := longestPiece(e.cellLayout("in "+joined+" at most", false, 0)), width(joined); math.Abs(float64(got-want)) > 1 {
				t.Errorf("the widest piece around %q is %v, the joined words %v", joined, got, want)
			}
		}
	})
}

// A document's table wider than its room scrolls as a code block's long
// line does: the wheel and the scrollbar move it, a point is the cell that
// is drawn there, and nothing outside its room is a cell.
func TestADocumentsWideTableScrollsSideways(t *testing.T) {
	s, e := openFitDocument(t, 760, DocumentOptions{FitTables: true, ScrollTables: true})
	var i int
	var g *grid
	var room float32
	var bar geom.Point
	s.Do(func() {
		i = tableBlocks(e)[3]
		g, room = mustGrid(t, e, i), e.tableRoom(i)
		if e.heights[i] != e.px(codeRowTop+codeRowBottom)+g.height+e.px(tableBarGap+codeBarH) {
			t.Errorf("the row is %v tall for a grid %v tall", e.heights[i], g.height)
		}
		home := e.cellRect(i, g, 0, 0)
		if row, col, ok := e.tableCellAt(i, g, home.Center()); !ok || row != 0 || col != 0 {
			t.Errorf("the first cell reads as %d, %d, %v", row, col, ok)
		}
		// Shift and the wheel scroll it; a turn without Shift is the pane's.
		if e.wheel(home.Center(), geom.NewPoint(0, -1), mod.None) {
			t.Error("a vertical wheel turn scrolled the table")
		}
		if !e.wheel(home.Center(), geom.NewPoint(0, -1), mod.Shift) || !e.wheel(home.Center(), geom.NewPoint(-1, 0), mod.None) {
			t.Error("the wheel did not scroll the table")
		}
		dx := e.tableScrollDX(i)
		if dx <= 0 || e.cellRect(i, g, 0, 0).X != home.X-dx || e.gridOrigin(i).X != e.gridHome(i).X-dx {
			t.Errorf("scrolled %v, the first cell from %v to %v", dx, home.X, e.cellRect(i, g, 0, 0).X)
		}
		// The point where the first cell was is now the cell drawn there.
		at := geom.NewPoint(e.gridHome(i).X+e.px(4), home.Center().Y)
		row, col, ok := e.tableCellAt(i, g, at)
		if !ok || !at.In(e.cellRect(i, g, row, col)) {
			t.Errorf("the cell at %v reads as %d, %d, %v", at, row, col, ok)
		}
		// Past the room nothing is a cell, though the grid runs on there.
		past := geom.NewPoint(e.gridHome(i).X+room+e.px(20), home.Center().Y)
		if _, _, ok := e.tableCellAt(i, g, past); ok {
			t.Error("a point right of the table's room is a cell")
		}
		r := e.PartRect(i, "codebar")
		if r.Width != room || r.Y < e.gridHome(i).Y+g.height {
			t.Fatalf("the scrollbar is at %v", r)
		}
		bar = s.Screen.PanelPoint(e, geom.NewPoint(r.Right()-2, r.Y+r.Height/2))
	})
	// A press at the track's end scrolls to the table's end.
	s.Screen.MouseDown(bar, unison.ButtonLeft, mod.None)
	s.Sync()
	s.Screen.MouseUp(bar, unison.ButtonLeft, mod.None)
	s.Sync()
	s.Do(func() {
		if dx, most := e.tableScrollDX(i), g.width()-room; dx < most-1 {
			t.Errorf("scrolled %v of %v", dx, most)
		}
		last := e.cellRect(i, g, 0, len(g.cols)-1)
		if last.Right() > e.gridHome(i).X+room+1 {
			t.Errorf("the last cell ends at %v, the room at %v", last.Right(), e.gridHome(i).X+room)
		}
		if row, col, ok := e.tableCellAt(i, g, last.Center()); !ok || row != 0 || col != len(g.cols)-1 {
			t.Errorf("the last cell reads as %d, %d, %v", row, col, ok)
		}
		// Bringing a cell into view scrolls back to it.
		e.revealTableCell(i, 0)
		if e.tableScrollDX(i) != 0 {
			t.Errorf("showing the first cell left the table at %v", e.tableScrollDX(i))
		}
	})
}

// A document's text kept to a width is laid out as in a document that wide,
// while a table and a code block reach past it: the table as far as its
// columns need, the code block as far as its longest line needs, and neither
// past the editor's right margin. A row ends where its own block does.
func TestADocumentsTextWidthLetsTablesAndCodeRunOn(t *testing.T) {
	var widths []float32
	var textRight float32
	t.Run("760", func(t *testing.T) {
		s, e := openFitDocument(t, 760, DocumentOptions{FitTables: true, ScrollTables: true})
		s.Do(func() {
			textRight = e.bodyRight()
			for i, b := range e.Doc.Blocks {
				if b.Kind == Paragraph {
					widths = append(widths, e.textWidth(&e.Doc.Blocks[i]))
				}
			}
			code := len(e.Doc.Blocks) - 1
			if e.codeViewport(code) != e.textWidth(&e.Doc.Blocks[code]) || e.codeMaxScroll(code) <= 0 {
				t.Errorf("without room the code shows %v and scrolls %v", e.codeViewport(code), e.codeMaxScroll(code))
			}
		})
	})
	t.Run("1220", func(t *testing.T) {
		s, e := openFitDocument(t, 1220, DocumentOptions{FitTables: true, ScrollTables: true, TextWidth: 760})
		s.Do(func() {
			if e.bodyRight() != textRight || !near(e.wideRight()-e.bodyRight(), e.px(1220-760)) {
				t.Errorf("text ends at %v, want %v, with %v past it", e.bodyRight(), textRight, e.wideRight()-e.bodyRight())
			}
			k := 0
			for i, b := range e.Doc.Blocks {
				if b.Kind != Paragraph {
					continue
				}
				if e.textWidth(&e.Doc.Blocks[i]) != widths[k] || e.rowRect(i).Right() != textRight {
					t.Errorf("paragraph %d wraps at %v and its row ends at %v", i, e.textWidth(&e.Doc.Blocks[i]), e.rowRect(i).Right())
				}
				k++
			}
			tables := tableBlocks(e)
			// The prose table is wider than the text and inside the editor.
			prose := tables[1]
			g := mustGrid(t, e, prose)
			if right := e.gridHome(prose).X + g.width(); right <= textRight || right > e.width() || e.rowRect(prose).Right() < right {
				t.Errorf("the prose table ends at %v, the text at %v, its row at %v", right, textRight, e.rowRect(prose).Right())
			}
			// A table that fits in the text's width keeps its row there.
			if e.rowRect(tables[0]).Right() != textRight {
				t.Errorf("the short table's row ends at %v", e.rowRect(tables[0]).Right())
			}
			// The code block's panel reaches as far as its long line, with
			// room for the caret after it.
			code := len(e.Doc.Blocks) - 1
			b := &e.Doc.Blocks[code]
			if b.Kind != Code {
				t.Fatalf("the last block is %v", b.Kind)
			}
			content := e.codeContent(code)
			if view := e.codeViewport(code); view < content || view > content+1+e.px(2*caretWidth) || view <= e.textWidth(b) || e.codeMaxScroll(code) != 0 {
				t.Errorf("the code shows %v of a line %v wide, text wraps at %v", view, content, e.textWidth(b))
			}
			if p := e.codePanel(code); p.Right() <= textRight || p.Right() > e.width() {
				t.Errorf("the code panel ends at %v", p.Right())
			}
		})
	})
}
