package editor

import (
	"image/png"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-notes/mathtex"
	"github.com/kvit-s/kvit-notes/search"
	"github.com/kvit-s/kvit-ui/uitest"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// A backslash before ASCII punctuation, CommonMark's escape, is a marker
// like the others: hidden away from the caret, shown in the marker colour
// while the caret touches it, and never shown in a read-only document. The
// source keeps it.

// escapeRows are the brief's rows: a source and what is drawn of it away
// from the caret. math marks the formula, whose drawing is its typeset box.
var escapeRows = []struct {
	src, drawn string
	math       bool
}{
	{src: `history \$15, β = 13`, drawn: "history $15, β = 13"},
	{src: `\$0.79`, drawn: "$0.79"},
	{src: `2 \* 3`, drawn: "2 * 3"},
	{src: `a \\ b`, drawn: `a \ b`},
	{src: `C:\Users\sk`, drawn: `C:\Users\sk`},
	{src: "`\\$x`", drawn: `\$x`},
	{src: `$\$5$`, math: true},
}

// moreEscapes are the other escapes agents write, in a paragraph.
var moreEscapes = []struct{ src, drawn string }{
	{`\*not italic\*`, "*not italic*"},
	{`\_not italic\_`, "_not italic_"},
	{`\# not a heading`, "# not a heading"},
	{`\[not](a link)`, "[not](a link)"},
	{`a \| b`, "a | b"},
	{`**bold \* star**`, "bold * star"},
	{`\$5 and $x$`, "$5 and x"},
}

func TestAnEscapeHidesItsBackslash(t *testing.T) {
	for _, c := range escapeRows {
		if c.math {
			continue
		}
		if got := render(c.src, 0, false); got != c.drawn {
			t.Errorf("%q is drawn as %q, want %q", c.src, got, c.drawn)
		}
	}
	for _, c := range moreEscapes {
		if got := render(c.src, -1, false); got != c.drawn {
			t.Errorf("%q is drawn as %q, want %q", c.src, got, c.drawn)
		}
	}
	// Nothing opens behind an escape, and code and math keep theirs.
	kinds := func(src string) []spanKind {
		var out []spanKind
		for _, sp := range spansOf(src) {
			out = append(out, sp.Kind)
		}
		return out
	}
	for src, want := range map[string][]spanKind{
		`\*not italic\*`: {sEscape, sEscape},
		`\[not](a link)`: {sEscape},
		`C:\Users\sk`:    nil,
		"`\\$x`":         {sCode},
		`$\$5$`:          {sMath},
		`\$5 and $x$`:    {sEscape, sMath},
		`a \\ b`:         {sEscape},
		`a\–b \é`:        nil, // not ASCII punctuation
	} {
		if got := kinds(src); !slices.Equal(got, want) {
			t.Errorf("%q has spans %v, want %v", src, got, want)
		}
	}
	if sp := spansOf(`$\$5$`); len(sp) != 1 || sp[0].Start != 0 || sp[0].End != 5 {
		t.Errorf("the formula is read as %+v", sp)
	}
}

func TestTheCaretShowsAnEscapesBackslash(t *testing.T) {
	const src = `history \$15, β = 13` // the backslash is at 8
	for _, caret := range []int{8, 9, 10} {
		if got := render(src, caret, true); got != src {
			t.Errorf("caret at %d: drawn as %q", caret, got)
		}
	}
	if got := render(src, 3, true); got != "history $15, β = 13" {
		t.Errorf("caret away: drawn as %q", got)
	}
	r := []rune(src)
	p := project(r, parseInline(r), func(sp span) bool { return revealed(sp, 9, 0, 0) })
	if f := p.flags[p.S2D[8]]; f != fMarker {
		t.Errorf("the shown backslash has flags %b, want the marker's", f)
	}
	// A click before the "$" puts the caret before the backslash, so what
	// is typed there leaves the escape whole; after it, after the "$".
	h := project(r, parseInline(r), nil)
	if got := h.forClick(8); got != 8 {
		t.Errorf("a click before $ puts the caret at %d, want 8", got)
	}
	if got := h.forClick(9); got != 10 {
		t.Errorf("a click after $ puts the caret at %d, want 10", got)
	}
	// Inside a bold span, before an escaped star, the caret goes in the
	// span and before the backslash.
	b := []rune(`a **\*b**`)
	bp := project(b, parseInline(b), nil)
	if got := bp.forClick(2); got != 4 {
		t.Errorf("a click before the bold star puts the caret at %d, want 4", got)
	}
}

// escapeNote is every row as a paragraph of its own, then the rows as the
// cells of a table, one to a row.
func escapeNote() string {
	var sb strings.Builder
	for _, c := range escapeRows {
		sb.WriteString(c.src + "\n\n")
	}
	sb.WriteString("| value |\n| --- |\n")
	for _, c := range escapeRows {
		sb.WriteString("| " + c.src + " |\n")
	}
	return sb.String()
}

// escapeTableIndex is the table's block in escapeNote.
var escapeTableIndex = len(escapeRows)

// checkEscapesDrawn checks what every paragraph and cell of escapeNote
// draws away from the caret.
func checkEscapesDrawn(t *testing.T, e *Editor, where string) {
	t.Helper()
	for i, c := range escapeRows {
		p := e.layout(i).proj
		checkEscapeDrawn(t, where+" paragraph", c.src, c.drawn, c.math, p)
	}
	g, ok := e.tableShowsGrid(escapeTableIndex)
	if !ok {
		t.Fatalf("%s: the table draws no grid", where)
	}
	for r, c := range escapeRows {
		checkEscapeDrawn(t, where+" cell", c.src, c.drawn, c.math, g.projs[r+1][0])
	}
}

func checkEscapeDrawn(t *testing.T, where, src, drawn string, math bool, p projection) {
	t.Helper()
	got := string(p.Disp)
	switch {
	case !math:
		if got != drawn {
			t.Errorf("%s %q is drawn as %q, want %q", where, src, got, drawn)
		}
	case mathtex.Available():
		if len(p.boxes) != 1 || len(p.Disp) != 1 {
			t.Errorf("%s %q is drawn as %q with %d formulas, want the one formula", where, src, got, len(p.boxes))
		}
	default:
		if got != `\$5` {
			t.Errorf("%s %q without the math library is drawn as %q, want its TeX", where, src, got)
		}
	}
}

func TestAnEditableNoteHidesEscapesAwayFromTheCaret(t *testing.T) {
	note := escapeNote()
	s, e := openEditor(t, note)
	s.Do(func() {
		checkEscapesDrawn(t, e, "editable")
		if got := Serialize(e.Doc.Blocks); got != strings.TrimSuffix(note, "\n") && got != note {
			t.Errorf("the note is saved as\n%s", got)
		}
		// The caret just after a backslash shows it, in the marker colour.
		e.FocusBlock(0, 9)
	})
	s.Sync()
	s.Do(func() {
		p := e.layout(0).proj
		if got := string(p.Disp); got != escapeRows[0].src {
			t.Errorf("with the caret after the backslash the paragraph is drawn as %q", got)
		}
		if f := p.flags[p.S2D[8]]; f&fMarker == 0 {
			t.Errorf("the backslash is drawn with flags %b, not as a marker", f)
		}
		if got := string(e.layout(1).proj.Disp); got != escapeRows[1].drawn {
			t.Errorf("the paragraph without the caret is drawn as %q", got)
		}
		// In the table, the caret shows the table's Markdown, backslashes
		// and all.
		tb := e.Doc.Blocks[escapeTableIndex].Text
		e.FocusBlock(escapeTableIndex, len([]rune(tb[:strings.Index(tb, `\$0`)]))+1)
	})
	s.Sync()
	s.Do(func() {
		if _, ok := e.tableShowsGrid(escapeTableIndex); ok {
			t.Fatal("the table with the caret in it still draws its grid")
		}
		if got := string(e.layout(escapeTableIndex).proj.Disp); !strings.Contains(got, `| \$0.79 |`) {
			t.Errorf("the table with the caret is drawn as %q", got)
		}
	})
}

func TestAReadOnlyDocumentNeverShowsAnEscape(t *testing.T) {
	s, d := openDocument(t, DocumentOptions{}, escapeNote(), 0)
	e := d.Editor
	s.Do(func() {
		checkEscapesDrawn(t, e, "read-only")
		// The caret after the backslash, as a press there leaves it.
		e.FocusBlock(0, 9)
	})
	s.Sync()
	s.Do(func() {
		if got := string(e.layout(0).proj.Disp); got != escapeRows[0].drawn {
			t.Errorf("with the caret after the backslash the paragraph is drawn as %q", got)
		}
		tb := e.Doc.Blocks[escapeTableIndex].Text
		e.FocusBlock(escapeTableIndex, len([]rune(tb[:strings.Index(tb, `\$0`)]))+1)
	})
	s.Sync()
	s.Do(func() {
		if g, ok := e.tableShowsGrid(escapeTableIndex); !ok || string(g.projs[2][0].Disp) != "$0.79" {
			t.Errorf("with the caret after the cell's backslash the grid shows %v", ok)
		} else if got := string(g.projs[2][0].Disp); got != "$0.79" {
			t.Errorf("with the caret after the cell's backslash the cell draws %q", got)
		}
		// Copying any row gives what is drawn; quoting it gives its
		// Markdown.
		for i, c := range escapeRows {
			if c.math {
				continue
			}
			id := e.Doc.Blocks[i].ID
			e.Doc.Anchor, e.Doc.Caret = Pos{id, 0}, Pos{id, len([]rune(c.src))}
			if got, md := d.CopiedText(); got != c.drawn || md {
				t.Errorf("copying %q gives %q (Markdown %v)", c.src, got, md)
			}
			if got := d.SelectedMarkdown(); got != c.src {
				t.Errorf("%q quotes %q", c.src, got)
			}
		}
		// Part of a row: from the "$" on.
		id := e.Doc.Blocks[0].ID
		e.Doc.Anchor, e.Doc.Caret = Pos{id, 9}, Pos{id, 11}
		if got, _ := d.CopiedText(); got != "$1" {
			t.Errorf("copying the escaped $ and a digit gives %q", got)
		}
		if got := d.SelectedMarkdown(); got != `\$1` {
			t.Errorf("the escaped $ and a digit quote %q", got)
		}
		e.Doc.Anchor = e.Doc.Caret
	})
	// A sweep over the first paragraph copies what it draws.
	sweepCopy(t, s, d, 0, escapeRows[0].src, escapeRows[0].drawn)
	// A triple-click in the money cell selects it and copies it as drawn.
	p := cellPointOf(t, s, e, escapeTableIndex, 1, 0, 0)
	s.Screen.DoubleClick(p)
	s.Screen.MouseDown(p, unison.ButtonLeft, mod.None)
	s.Screen.MouseUp(p, unison.ButtonLeft, mod.None)
	s.Do(func() {
		if got, _ := d.CopiedText(); got != "$0.79" {
			t.Errorf("copying the cell gives %q", got)
		}
		if got := d.SelectedMarkdown(); got != `\$0.79` {
			t.Errorf("the cell quotes %q", got)
		}
		if g, ok := e.tableShowsGrid(escapeTableIndex); !ok || string(g.projs[2][0].Disp) != "$0.79" {
			t.Errorf("the selected cell draws %q", string(g.projs[2][0].Disp))
		}
	})
}

// sweepCopy drags over all of block i's text and checks what it copies and
// that the sweep showed no marker.
func sweepCopy(t *testing.T, s *uitest.Session, d *Document, i int, src, drawn string) {
	t.Helper()
	from, to := textPointOf(s, d, i, 0), textPointOf(s, d, i, len([]rune(src)))
	s.Screen.Drag(from, to, 6)
	s.Do(func() {
		if got, _ := d.CopiedText(); got != drawn {
			t.Errorf("the sweep copies %q, want %q", got, drawn)
		}
		if got := string(d.Editor.layout(i).proj.Disp); got != drawn {
			t.Errorf("after the sweep the line is drawn as %q", got)
		}
	})
}

// textPointOf is the screen point of Markdown offset off in block i's text.
func textPointOf(s *uitest.Session, d *Document, i, off int) (p geom.Point) {
	s.Do(func() { p = s.Screen.PanelPoint(d.Editor, d.Editor.TextPoint(i, off)) })
	return p
}

// cellPointOf is the screen point of drawn offset dd of cell (row, col).
func cellPointOf(t *testing.T, s *uitest.Session, e *Editor, i, row, col, dd int) (p geom.Point) {
	s.Do(func() { p = s.Screen.PanelPoint(e, cellPoint(t, e, i, row, col, dd)) })
	return p
}

func TestEscapesInFragmentsPastesAndFind(t *testing.T) {
	src := []rune(`**a \* b**`)
	if got := inlineFragment(src, 4, 8); got != `**\* b**` {
		t.Errorf("part of a bold span with an escaped star is %q", got)
	}
	if got := inlineFragment(src, 5, 6); got != `**\***` {
		t.Errorf("the escaped star alone is %q", got)
	}
	if got := render(`**\***`, -1, false); got != "*" {
		t.Errorf("which draws %q", got)
	}
	// A plain paste keeps escapes, so the pasted line draws as it did.
	for line, want := range map[string]string{
		`a \*b\* c`:   `a \*b\* c`,
		`**x** \$5`:   `x \$5`,
		`C:\Users\sk`: `C:\Users\sk`,
	} {
		if got := displayLine(line); got != want {
			t.Errorf("a plain paste of %q gives %q, want %q", line, got, want)
		}
	}
	// Find searches what is drawn.
	md := `history \$15, β = 13`
	var spans []search.Span
	for _, sp := range Spans(md) {
		spans = append(spans, search.Span{Start: sp.Start, End: sp.End, ContentStart: sp.ContentStart, ContentEnd: sp.ContentEnd})
	}
	b := search.Block{Markdown: md, Spans: spans}
	if got := b.Text(); got != "history $15, β = 13" {
		t.Errorf("find reads %q", got)
	}
	if got := PlainText(md); got != "history $15, β = 13" {
		t.Errorf("the plain text is %q", got)
	}
}

// escapeSample is a short reply with the escapes agents write, for the
// pictures.
const escapeSample = "A reply about prices: history \\$15, β = 13, and 2 \\* 3 = 6.\n\n" +
	"Kept as written: C:\\Users\\sk, the code `\\$x`, a backslash a \\\\ b, and the formula $\\$5$.\n\n" +
	"| item | price |\n| --- | --- |\n| tea | \\$0.79 |\n| \\*starred\\* | \\$1,200 |\n"

// TestEscapePictures saves pictures of escapeSample, read-only and with the
// caret just after the first backslash of an editable note, into the folder
// KVIT_ESCAPE_SHOTS names.
func TestEscapePictures(t *testing.T) {
	dir := os.Getenv("KVIT_ESCAPE_SHOTS")
	if dir == "" {
		t.Skip("KVIT_ESCAPE_SHOTS names no folder")
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
	t.Run("read-only", func(t *testing.T) {
		s, _ := openDocument(t, DocumentOptions{Margin: 16}, escapeSample, 0)
		save(s, "escapes-read-only.png")
	})
	t.Run("caret", func(t *testing.T) {
		s, e := openEditor(t, escapeSample)
		s.Do(func() { e.FocusBlock(0, len([]rune("A reply about prices: history \\"))) })
		s.Sync()
		save(s, "escapes-caret.png")
	})
}
