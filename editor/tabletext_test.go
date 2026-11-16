package editor

import (
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/role"
)

// Selecting the characters of a table's cell in a read-only document, and
// what Kvit Works asks of that selection: its range, its Markdown, where it
// ends, and a comment's wash over it.

const cellNote = "Intro paragraph.\n\n" +
	"| name | n | note |\n" +
	"| --- | --- | --- |\n" +
	"| alpha | 1 | the first of the rows here |\n" +
	"| **bold** words | 2 | a [link](https://example.com) here |\n" +
	"| line<br>two | 3 | x \\| y |\n\n" +
	"After the table.\n"

// cellTable is the table block's index in cellNote.
const cellTable = 1

// runeIndex is where sub starts in s, in runes.
func runeIndex(t *testing.T, s, sub string) int {
	t.Helper()
	k := strings.Index(s, sub)
	if k < 0 {
		t.Fatalf("%q is not in %q", sub, s)
	}
	return len([]rune(s[:k]))
}

// cellPoint is the point just right of where a caret before drawn offset d
// of cell (row, col) of block i's grid goes, in the middle of its line, in
// the editor's coordinates.
func cellPoint(t *testing.T, e *Editor, i, row, col, d int) geom.Point {
	t.Helper()
	g, ok := e.tableShowsGrid(i)
	if !ok {
		t.Fatalf("block %d draws no grid", i)
	}
	x, top, h := g.cells[row+1][col].CaretAt(d)
	o := e.cellTextOrigin(i, g, row, col)
	return geom.NewPoint(o.X+x+1, o.Y+top+h/2)
}

// dragInCell sweeps the pointer from drawn offset a to drawn offset b of one
// cell.
func dragInCell(t *testing.T, s *uitest.Session, e *Editor, i, row, col, a, b int) {
	t.Helper()
	var from, to geom.Point
	s.Do(func() {
		from = s.Screen.PanelPoint(e, cellPoint(t, e, i, row, col, a))
		to = s.Screen.PanelPoint(e, cellPoint(t, e, i, row, col, b))
	})
	s.Screen.Drag(from, to, 6)
}

// noLiveCell fails when a press left a live cell, a field or a gesture.
func noLiveCell(t *testing.T, e *Editor) {
	t.Helper()
	if _, _, _, ok := e.TableCell(); ok || e.tableField != nil || e.tableSweep != nil || e.cellPick != nil {
		t.Errorf("left behind: live cell %v, field %v, sweep %+v, press %+v", e.tableActive, e.tableField != nil, e.tableSweep, e.cellPick)
	}
}

func TestTableCellSpansReadCellsAsParseTableDoes(t *testing.T) {
	for _, md := range []string{
		cellNote[strings.Index(cellNote, "|"):strings.Index(cellNote, "\n\nAfter")],
		"| a | b |\n|---|---|\n| 1 | 2 |",
		"a | b\n--- | ---\n1 | 2",
		"|a|b|\n|:-|-:|\n|x||\n\n| lone |",
		"| one | two | three |\n|---|---|---|\n| short |\n| a | b | c | d |",
		"| x \\| y | z |\n|---|---|\n| a <br> b | c<BR/>d |\n|  é ü  | 漢字 |",
		"| a |\n|---|\n|   |\n| <br> |",
	} {
		tab := ParseTable(md)
		spans := tableCellSpans(md)
		if !tab.Valid || spans == nil || matchedCellSpans(md, tab) == nil {
			t.Fatalf("%q: valid %v, spans %v", md, tab.Valid, spans != nil)
		}
		src := []rune(md)
		for r, row := range append([][]string{tab.Headers}, tab.Rows...) {
			for c, v := range row {
				sp := spans[r][c]
				if string(sp.value) != v {
					t.Errorf("%q cell %d,%d reads %q, ParseTable %q", md, r-1, c, string(sp.value), v)
				}
				for k, ch := range sp.value {
					raw := string(src[sp.from[k]:sp.to[k]])
					switch {
					case ch == '\n' && reBrDelim.MatchString(raw):
					case ch == '|' && raw == `\|`:
					case raw == string(ch):
					default:
						t.Errorf("%q cell %d,%d: %q came from %q", md, r-1, c, ch, raw)
					}
				}
			}
		}
	}
	if tableCellSpans("not a table") != nil {
		t.Error("a paragraph has cells")
	}
}

func TestDraggingInACellSelectsItsCharacters(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, cellNote, 0)
	e := d.Editor
	var text string
	var want TextRange
	s.Do(func() {
		text = e.Doc.Blocks[cellTable].Text
		at := runeIndex(t, text, "first of")
		want = TextRange{StartIndex: cellTable, StartPos: at, EndIndex: cellTable, EndPos: at + len("first of")}
	})
	// "the first of the rows here": from before "first" to after "of".
	dragInCell(t, s, e, cellTable, 0, 2, 4, 12)
	s.Do(func() {
		if !d.HasSelection() || e.HasTableSelection() {
			t.Fatalf("selection %v, rectangle %v", d.HasSelection(), e.HasTableSelection())
		}
		if r := d.SelectedRange(); r != want {
			t.Errorf("the range is %+v, want %+v", r, want)
		}
		if got := d.SelectedMarkdown(); got != "first of" {
			t.Errorf("the selection quotes %q", got)
		}
		if got, md := d.CopiedText(); got != "first of" || md {
			t.Errorf("copying gives %q (Markdown %v)", got, md)
		}
		if _, ok := e.tableShowsGrid(cellTable); !ok {
			t.Error("the table's Markdown is shown with the selection in it")
		}
		noLiveCell(t, e)
		// Only the selected characters are tinted, inside their cell.
		g, _ := e.tableShowsGrid(cellTable)
		cell := e.cellRect(cellTable, g, 0, 2)
		rects := e.cellRangeRects(cellTable, g, want.StartPos, want.EndPos)
		if len(rects) != 1 || !cell.Contains(rects[0]) || rects[0].Width >= cell.Width-2*e.px(cellPadX) {
			t.Errorf("the tint is %v in the cell %v", rects, cell)
		}
		// The end of the selection is where the bar goes: after "of", one
		// line of the cell tall.
		end := d.CharRect(want.EndIndex, want.EndPos)
		after := e.fromEditor(cellPoint(t, e, cellTable, 0, 2, 12))
		if end.Height <= 0 || end.Height >= cell.Height || end.X != after.X-1 || after.Y < end.Y || after.Y > end.Bottom() {
			t.Errorf("the selection ends at %v, the character at %v, in the cell %v", end, after, cell)
		}
		if start := d.CharRect(want.StartIndex, want.StartPos); start.X >= end.X || start.Y != end.Y {
			t.Errorf("the selection starts at %v and ends at %v", start, end)
		}
	})
	// Swept the other way, the same characters are the same range.
	dragInCell(t, s, e, cellTable, 0, 2, 12, 4)
	s.Do(func() {
		if r := d.SelectedRange(); r != want || d.SelectedMarkdown() != "first of" {
			t.Errorf("swept right to left: %+v %q", r, d.SelectedMarkdown())
		}
	})
	// Ctrl+C copies the characters; Escape drops them.
	s.Do(func() { unison.ClipboardSetText("") })
	s.Screen.KeyPress(unison.KeyC, mod.Control)
	s.Do(func() {
		if got := unison.ClipboardGetText(); got != "first of" {
			t.Errorf("Ctrl+C copied %q", got)
		}
	})
	s.Screen.KeyPress(unison.KeyEscape, mod.None)
	s.Do(func() {
		if d.HasSelection() {
			t.Error("Escape kept the selection")
		}
		if _, ok := e.tableShowsGrid(cellTable); !ok {
			t.Error("the grid went with the selection")
		}
	})
	// ClearSelection and a press elsewhere drop it too.
	dragInCell(t, s, e, cellTable, 0, 2, 4, 12)
	s.Do(func() {
		if !d.HasSelection() {
			t.Fatal("the second sweep selected nothing")
		}
		d.ClearSelection()
		if d.HasSelection() || d.SelectedRange() != NoRange || d.CopySelection() {
			t.Error("ClearSelection kept the selection")
		}
	})
	dragInCell(t, s, e, cellTable, 0, 2, 4, 12)
	var elsewhere geom.Point
	s.Do(func() { elsewhere = s.Screen.PanelPoint(e, e.TextPoint(2, 3)) })
	s.Screen.Click(elsewhere)
	s.Do(func() {
		if d.HasSelection() {
			t.Error("a press on the next paragraph kept the selection")
		}
		if text != e.Doc.Blocks[cellTable].Text {
			t.Error("the table changed")
		}
	})
}

func TestACellsInlineMarkdownIsQuotedBalancedAndCopiedAsDrawn(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, cellNote, 0)
	e := d.Editor
	var text string
	s.Do(func() { text = e.Doc.Blocks[cellTable].Text })
	// "bold" of "**bold** words", drawn as "bold words".
	dragInCell(t, s, e, cellTable, 1, 0, 0, 4)
	s.Do(func() {
		at := runeIndex(t, text, "bold**")
		if r := d.SelectedRange(); r != (TextRange{cellTable, at, cellTable, at + 4}) {
			t.Errorf("the range is %+v", r)
		}
		if got := d.SelectedMarkdown(); got != "**bold**" {
			t.Errorf("the selection quotes %q", got)
		}
		if got, _ := d.CopiedText(); got != "bold" {
			t.Errorf("copying gives %q", got)
		}
	})
	// "a link here": the link's words, written back as a link.
	dragInCell(t, s, e, cellTable, 1, 2, 2, 6)
	s.Do(func() {
		if got := d.SelectedMarkdown(); got != "[link](https://example.com)" {
			t.Errorf("the link quotes %q", got)
		}
		if got, _ := d.CopiedText(); got != "link" {
			t.Errorf("copying the link gives %q", got)
		}
	})
	// A triple-click takes a cell's whole text: a line break is "<br>" in
	// its Markdown and a new line in its copy, and the selection ends on
	// the cell's second line.
	var p geom.Point
	s.Do(func() { p = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 2, 0, 1)) })
	s.Screen.DoubleClick(p)
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Do(func() {
		at := runeIndex(t, text, "line<br>two")
		if r := d.SelectedRange(); r != (TextRange{cellTable, at, cellTable, at + len("line<br>two")}) {
			t.Errorf("the triple-click selected %+v", r)
		}
		if got := d.SelectedMarkdown(); got != "line<br>two" {
			t.Errorf("the cell quotes %q", got)
		}
		if got, _ := d.CopiedText(); got != "line\ntwo" {
			t.Errorf("copying the cell gives %q", got)
		}
		r := d.SelectedRange()
		if first, last := e.CharRect(cellTable, r.StartPos), e.CharRect(cellTable, r.EndPos); last.Y <= first.Y {
			t.Errorf("the cell's end %v is not below its start %v", last, first)
		}
		noLiveCell(t, e)
	})
	// An escaped pipe is one character drawn and "\|" quoted.
	s.Do(func() { p = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 2, 2, 0)) })
	s.Screen.DoubleClick(p)
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Do(func() {
		if got := d.SelectedMarkdown(); got != `x \| y` {
			t.Errorf("the escaped pipe quotes %q", got)
		}
		if got, _ := d.CopiedText(); got != "x | y" {
			t.Errorf("copying the escaped pipe gives %q", got)
		}
	})
}

func TestADoubleClickSelectsAWordAndAHeaderIsNotSorted(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, cellNote, 0)
	e := d.Editor
	var text string
	var p geom.Point
	s.Do(func() {
		text = e.Doc.Blocks[cellTable].Text
		p = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 6)) // in "first"
	})
	s.Screen.DoubleClick(p)
	s.Do(func() {
		if got := d.SelectedMarkdown(); got != "first" {
			t.Errorf("the double-click selected %q", got)
		}
		noLiveCell(t, e)
	})
	// A double-click held and dragged extends by whole words, either way.
	var rows, the geom.Point
	s.Do(func() {
		rows = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 19)) // in "rows"
		the = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 1))   // in "the"
	})
	for _, c := range []struct {
		to   geom.Point
		want string
	}{{rows, "first of the rows"}, {the, "the first"}} {
		s.Screen.Click(p)
		s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
		s.Screen.MouseMove(c.to, mod.None)
		s.Screen.MouseUp(c.to, unison.ButtonLeft, mod.None)
		s.Do(func() {
			if got := d.SelectedMarkdown(); got != c.want {
				t.Errorf("a double-click dragged selected %q, want %q", got, c.want)
			}
		})
	}
	// The header's "note".
	s.Do(func() { p = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, -1, 2, 2)) })
	s.Screen.DoubleClick(p)
	s.Do(func() {
		if got := d.SelectedMarkdown(); got != "note" {
			t.Errorf("the double-click on the header selected %q", got)
		}
		if mark := e.tableSortMark(e.Doc.Blocks[cellTable].ID, 2); mark != "" || e.Doc.Blocks[cellTable].Text != text {
			t.Errorf("the header was sorted: mark %q", mark)
		}
		noLiveCell(t, e)
	})
}

func TestAPressInAReadOnlyCellSelectsNothing(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, cellNote, 0)
	e := d.Editor
	var p geom.Point
	s.Do(func() { p = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 6)) })
	s.Screen.Click(p)
	s.Do(func() {
		if d.HasSelection() || e.HasTableSelection() {
			t.Errorf("a press selected: text %v, cells %v", d.HasSelection(), e.HasTableSelection())
		}
		noLiveCell(t, e)
		if _, ok := e.tableShowsGrid(cellTable); !ok {
			t.Error("a press showed the table's Markdown")
		}
	})
}

// The same press in an editable note makes the cell live, with its field
// open, and selects nothing in the table's Markdown.
func TestAPressInAnEditableCellStillOpensItsField(t *testing.T) {
	s, e := openEditor(t, cellNote)
	var p geom.Point
	s.Do(func() { p = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 6)) })
	s.Screen.Click(p)
	s.Do(func() {
		if _, row, col, ok := e.TableCell(); !ok || row != 0 || col != 2 || e.tableField == nil {
			t.Errorf("the press made %d,%d live (%v), field %v", row, col, ok, e.tableField != nil)
		}
		if e.Doc.HasSelection() || e.cellPick != nil {
			t.Error("the press selected the table's Markdown")
		}
	})
}

func TestADragIntoAnotherCellBecomesTheRectangleAndComesBack(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, cellNote, 0)
	e := d.Editor
	var start, inCell, other geom.Point
	s.Do(func() {
		start = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 4))
		inCell = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 12))
		g, _ := e.tableShowsGrid(cellTable)
		other = s.Screen.PanelPoint(e, e.cellRect(cellTable, g, 1, 1).Center())
	})
	s.Screen.MouseDown(start, unison.ButtonLeft, mod.None)
	s.Screen.MouseMove(inCell, mod.None)
	s.Do(func() {
		if got := d.SelectedMarkdown(); got != "first of" || e.HasTableSelection() {
			t.Errorf("in the cell: %q, rectangle %v", got, e.HasTableSelection())
		}
	})
	s.Screen.MouseMove(other, mod.None)
	s.Do(func() {
		_, top, bottom, left, right, ok := e.TableSelection()
		if d.HasSelection() || !ok || top != 0 || bottom != 1 || left != 1 || right != 2 {
			t.Errorf("in another cell: text %v, rectangle %v %d..%d x %d..%d", d.HasSelection(), ok, top, bottom, left, right)
		}
	})
	s.Screen.MouseMove(inCell, mod.None)
	s.Do(func() {
		if got := d.SelectedMarkdown(); got != "first of" || e.HasTableSelection() {
			t.Errorf("back in the cell: %q, rectangle %v", got, e.HasTableSelection())
		}
	})
	s.Screen.MouseMove(other, mod.None)
	s.Screen.MouseUp(other, unison.ButtonLeft, mod.None)
	s.Do(func() {
		if d.HasSelection() || !e.HasTableSelection() {
			t.Errorf("let go in another cell: text %v, rectangle %v", d.HasSelection(), e.HasTableSelection())
		}
		if _, _, _, ok := e.TableCell(); ok || e.cellPick != nil {
			t.Error("the rectangle left a live cell or a press behind")
		}
	})
}

// A right-click in a read-only cell offers Copy alone: of the characters
// selected or of the rectangle the press is in. Outside them it drops them,
// and Copy is unavailable. An editable note's menu still edits the table.
func TestARightClickInAReadOnlyCellOffersCopy(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, cellNote, 0)
	e := d.Editor
	// rightClick opens the menu at p and reports its lines.
	rightClick := func(p geom.Point) string {
		t.Helper()
		var at geom.Point
		s.Do(func() {
			at = s.Screen.PanelPoint(e, p)
			unison.ClipboardSetText("")
		})
		s.Screen.ClickWith(at, unison.ButtonRight, mod.None)
		var names []string
		for _, n := range s.Nodes(role.MenuItem) {
			names = append(names, n.Name)
		}
		return strings.Join(names, ", ")
	}
	var on, off, corner geom.Point
	s.Do(func() {
		on = cellPoint(t, e, cellTable, 0, 2, 7) // in "first"
		off = cellPoint(t, e, cellTable, 0, 2, 20)
		g, _ := e.tableShowsGrid(cellTable)
		corner = e.cellRect(cellTable, g, 1, 1).Center()
	})
	dragInCell(t, s, e, cellTable, 0, 2, 4, 12)
	if names := rightClick(on); names != "Copy" {
		t.Errorf("on the selection the menu offers %q", names)
	}
	s.Screen.Type("c")
	s.Do(func() {
		if got := unison.ClipboardGetText(); got != "first of" || d.SelectedMarkdown() != "first of" {
			t.Errorf("Copy copied %q, the selection is %q", got, d.SelectedMarkdown())
		}
	})
	// Off the selection: it is dropped, and Copy is there but off.
	if names := rightClick(off); names != "Copy" {
		t.Errorf("off the selection the menu offers %q", names)
	}
	s.Screen.Type("c")
	s.Do(func() {
		if d.HasSelection() || unison.ClipboardGetText() != "" {
			t.Errorf("off the selection: selection %v, copied %q", d.HasSelection(), unison.ClipboardGetText())
		}
	})
	s.Screen.KeyPress(unison.KeyEscape, mod.None)
	// The rectangle of cells is copied as a table of its own, and kept.
	dragCell(t, s, e, cellTable, "cell", "cell:1:1")
	if names := rightClick(corner); names != "Copy" {
		t.Errorf("on the rectangle the menu offers %q", names)
	}
	s.Screen.Type("c")
	s.Do(func() {
		if got := unison.ClipboardGetText(); got != "| name | n |\n| --- | --- |\n| alpha | 1 |\n| **bold** words | 2 |" {
			t.Errorf("Copy copied %q", got)
		}
		if !e.HasTableSelection() {
			t.Error("the right-click dropped the rectangle")
		}
	})
}

// An editable note's right-click in a cell still opens the menu that edits
// the table.
func TestARightClickInAnEditableCellStillEditsTheTable(t *testing.T) {
	s, e := openEditor(t, cellNote)
	var at geom.Point
	s.Do(func() { at = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 6)) })
	s.Screen.ClickWith(at, unison.ButtonRight, mod.None)
	// "Insert row above" is the menu's first line; its access key is I.
	before := ""
	s.Do(func() { before = e.Doc.Blocks[cellTable].Text })
	s.Screen.Type("i")
	s.Do(func() {
		if t2 := ParseTable(e.Doc.Blocks[cellTable].Text); len(t2.Rows) != len(ParseTable(before).Rows)+1 {
			t.Errorf("the menu did not insert a row:\n%s", e.Doc.Blocks[cellTable].Text)
		}
	})
}

func TestACommentWashIsDrawnOverTheCellsCharacters(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, cellNote, 0)
	e := d.Editor
	var at, from, to int
	var id, outline, across string
	s.Do(func() {
		text := e.Doc.Blocks[cellTable].Text
		at = runeIndex(t, text, "first of")
		id = d.Decorations().AddSpan("comments", cellTable, at, len("first of"), SpanWash, SpanColor(e.tok().Danger, 0.8))
		outline = d.Decorations().AddSpan("anchor", cellTable, runeIndex(t, text, "bold"), 4, SpanOutline, SpanColor(e.tok().Accent, 1))
		// From inside "alpha" to inside "the first": a part in each cell.
		from, to = runeIndex(t, text, "pha"), runeIndex(t, text, "first")
		across = d.Decorations().AddSpan("comments", cellTable, from, to-from, SpanWash, SpanColor(e.tok().Danger, 0.8))
	})
	s.Sync()
	var px geom.Point
	s.Do(func() {
		g, _ := e.tableShowsGrid(cellTable)
		cell := e.fromEditorRect(e.cellRect(cellTable, g, 0, 2))
		rects := d.SpanRects(id)
		if len(rects) != 1 || !cell.Contains(rects[0]) {
			t.Fatalf("the wash is at %v, the cell at %v", rects, cell)
		}
		start, end := d.CharRect(cellTable, at), d.CharRect(cellTable, at+len("first of"))
		if rects[0].X != start.X || rects[0].Right() != end.X || rects[0].Y != start.Y {
			t.Errorf("the wash %v is not from %v to %v", rects[0], start, end)
		}
		if r := d.SpanRects(outline); len(r) != 1 || !e.fromEditorRect(e.cellRect(cellTable, g, 1, 0)).Contains(r[0]) {
			t.Errorf("the outline is at %v", r)
		}
		if r := d.SpanRects(across); len(r) != 3 {
			t.Errorf("a span across three cells is drawn as %v", r)
		}
		r := e.Decorations().SpanRects(id)[0]
		px = geom.NewPoint(r.X+1, r.Y+1)
	})
	pixel := func() (r, g, b uint32) {
		p := s.Screen.PanelPoint(e, px)
		sc := s.Screen.Scale()
		r, g, b, _ = s.Capture().At(int(p.X*sc), int(p.Y*sc)).RGBA()
		return r, g, b
	}
	r1, g1, b1 := pixel()
	s.Do(func() { d.Decorations().RemoveSpan(id) })
	s.Sync()
	if r0, g0, b0 := pixel(); r1 == r0 && g1 == g0 && b1 == b0 {
		t.Error("the wash changed nothing under the words")
	}
}

func TestASelectionInAScrolledCellFollowsTheScroll(t *testing.T) {
	s, e := openFitDocument(t, 760, DocumentOptions{FitTables: true, ScrollTables: true})
	var i, col int
	var word string
	s.Do(func() {
		i = tableBlocks(e)[3]
		g := mustGrid(t, e, i)
		if _, ok := e.tableScrolls(i); !ok {
			t.Fatal("the twelve columns do not scroll")
		}
		// Scroll to the end, where the last column shows.
		col = len(g.cols) - 1
		e.revealTableCell(i, col)
		if e.tableScrollDX(i) <= 0 {
			t.Fatal("revealing the last column scrolled nothing")
		}
		word = string(g.projs[1][col].Disp)
	})
	dragInCell(t, s, e, i, 0, col, 1, 5)
	s.Do(func() {
		text := e.Doc.Blocks[i].Text
		at := runeIndex(t, text, word) + 1
		if r := e.SelectionRange(); r != (TextRange{i, at, i, at + 4}) {
			t.Errorf("the selection in %q is %+v, want from %d", word, r, at)
		}
		if got := e.SelectionMarkdown(); got != word[1:5] {
			t.Errorf("the selection quotes %q", got)
		}
		g := mustGrid(t, e, i)
		view := e.tableView(i, g)
		end := e.CharRect(i, at+4)
		if want := cellPoint(t, e, i, 0, col, 5); end.X != want.X-1 || end.X < view.X || end.X > view.Right() {
			t.Errorf("the selection ends at %v, the character at %v, the view %v", end, want, view)
		}
		for _, r := range e.cellRangeRects(i, g, at, at+4) {
			if r.X < view.X || r.Right() > view.Right() {
				t.Errorf("the tint %v is outside the view %v", r, view)
			}
		}
		// A character scrolled out of view is placed at the view's edge.
		e.revealTableCell(i, 0)
		if r := e.CharRect(i, at); r.X < view.X || r.X > view.Right() {
			t.Errorf("a character scrolled away is at %v, the view %v", r, view)
		}
	})
}

func TestASweepFromProseIntoAReadOnlyTableKeepsTheGrid(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, cellNote, 0)
	e := d.Editor
	var from, to geom.Point
	var at int
	s.Do(func() {
		at = runeIndex(t, e.Doc.Blocks[cellTable].Text, "first")
		from = s.Screen.PanelPoint(e, e.TextPoint(0, 6))
		to = s.Screen.PanelPoint(e, cellPoint(t, e, cellTable, 0, 2, 4))
	})
	s.Screen.Drag(from, to, 6)
	s.Do(func() {
		if _, ok := e.tableShowsGrid(cellTable); !ok {
			t.Fatal("a sweep into the table showed its Markdown")
		}
		if r := d.SelectedRange(); r.StartIndex != 0 || r.EndIndex != cellTable || r.EndPos != at {
			t.Errorf("the sweep selected %+v, want to %d of the table", r, at)
		}
		// The header row is wholly selected, the first row up to "first".
		g, _ := e.tableShowsGrid(cellTable)
		if n := len(e.cellRangeRects(cellTable, g, 0, at)); n != 6 {
			t.Errorf("the sweep tints %d pieces of cells", n)
		}
		if !d.SelectAll() {
			t.Fatal("nothing to select")
		}
		if _, ok := e.tableShowsGrid(cellTable); !ok {
			t.Error("selecting everything showed the table's Markdown")
		}
	})
}

// fromEditorRect is a rectangle in the editor's coordinates in its
// document's.
func (e *Editor) fromEditorRect(r geom.Rect) geom.Rect {
	for p := e.AsPanel(); p != nil; p = p.Parent() {
		if d, ok := p.Self.(*Document); ok {
			return d.fromEditor(r)
		}
	}
	return r
}

// fromEditor is a point in the editor's coordinates in its document's.
func (e *Editor) fromEditor(p geom.Point) geom.Point {
	return e.fromEditorRect(geom.NewRect(p.X, p.Y, 0, 0)).Point
}

// TestTableCellPictures saves pictures of a read-only document with
// characters of a cell selected and with a comment's wash in a cell into the
// folder KVIT_TABLE_SHOTS names.
func TestTableCellPictures(t *testing.T) {
	dir := os.Getenv("KVIT_TABLE_SHOTS")
	if dir == "" {
		t.Skip("KVIT_TABLE_SHOTS names no folder")
	}
	save := func(s *uitest.Session, name string) {
		f, err := os.Create(filepath.Join(dir, name))
		if err != nil {
			t.Fatal(err)
		}
		defer f.Close()
		if err := png.Encode(f, s.Capture()); err != nil {
			t.Fatal(err)
		}
	}
	s, d := openDocument(t, DocumentOptions{Margin: 16}, cellNote, 0)
	e := d.Editor
	dragInCell(t, s, e, cellTable, 0, 2, 4, 12)
	save(s, "table-cell-selection.png")
	s.Do(func() {
		d.ClearSelection()
		text := e.Doc.Blocks[cellTable].Text
		d.Decorations().AddSpan("comments", cellTable, runeIndex(t, text, "first of"), len("first of"), SpanWash, SpanColor(e.tok().Warning, 0.45))
		d.Decorations().AddSpan("comments", cellTable, runeIndex(t, text, "bold"), 4, SpanWash|SpanOutline, SpanColor(e.tok().Accent, 0.35))
	})
	s.Sync()
	save(s, "table-cell-comment.png")
}
