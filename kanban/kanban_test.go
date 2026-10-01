package kanban

// These tests check reading and writing task boards: columns and cards,
// round trips, every mutation, lines the model does not record, labels and
// their escapes, due dates, the comment holding a card's dates, and the
// text of a card's own line (Card.Line). A test that does not care about
// the day passes "" as today.

import (
	"fmt"
	"slices"
	"strings"
	"testing"
)

const cal = calendar

func titlesOf(col Column) []string {
	var titles []string
	for _, c := range col.Cards {
		titles = append(titles, c.Title)
	}
	return titles
}

func TestParseColumnsAndCards(t *testing.T) {
	md := "## To do\n- [ ] first #urgent " + cal + " 2026-07-15\n" +
		"  a description line\n- [x] done card\n## In progress\n- [ ] wip"
	b := Parse(md)
	if b.ColumnCount() != 2 {
		t.Fatalf("columns = %d, want 2", b.ColumnCount())
	}
	if b.Columns[0].Name != "To do" {
		t.Errorf("column 0 name = %q", b.Columns[0].Name)
	}
	if len(b.Columns[0].Cards) != 2 {
		t.Fatalf("column 0 cards = %d, want 2", len(b.Columns[0].Cards))
	}
	c0 := b.Columns[0].Cards[0]
	if c0.Title != "first" {
		t.Errorf("title = %q", c0.Title)
	}
	if c0.Done {
		t.Error("card 0 is done")
	}
	if !slices.Equal(c0.Labels, []string{"urgent"}) {
		t.Errorf("labels = %q", c0.Labels)
	}
	if c0.Due != "2026-07-15" {
		t.Errorf("due = %q", c0.Due)
	}
	if c0.Description != "a description line" {
		t.Errorf("description = %q", c0.Description)
	}
	if !b.Columns[0].Cards[1].Done {
		t.Error("card 1 is not done")
	}
	if b.Columns[1].Name != "In progress" {
		t.Errorf("column 1 name = %q", b.Columns[1].Name)
	}
	if len(b.Columns[1].Cards) != 1 {
		t.Errorf("column 1 cards = %d, want 1", len(b.Columns[1].Cards))
	}
}

func TestSerializeRoundTrips(t *testing.T) {
	md := "## To do\n- [ ] first #urgent " + cal + " 2026-07-15\n" +
		"  desc\n- [x] done\n## Done\n- [x] shipped #release"
	if got := Parse(md).Serialize(); got != md {
		t.Errorf("round trip:\n got %q\nwant %q", got, md)
	}
}

func TestLooksLikeBoard(t *testing.T) {
	if !LooksLikeBoard("## Column\n- [ ] card") {
		t.Error("a board with a column is not a board")
	}
	if LooksLikeBoard("just some code\nno headers") {
		t.Error("text with no column is a board")
	}
}

func TestMutations(t *testing.T) {
	md := "## A\n- [ ] one\n- [ ] two\n## B\n- [ ] three"

	// Move a card between columns to a target index.
	mb := Parse(MoveCard(md, 0, 0, 1, 1, ""))
	if len(mb.Columns[0].Cards) != 1 || len(mb.Columns[1].Cards) != 2 {
		t.Fatalf("after the move: %d and %d cards", len(mb.Columns[0].Cards), len(mb.Columns[1].Cards))
	}
	if mb.Columns[1].Cards[1].Title != "one" {
		t.Errorf("moved card = %q", mb.Columns[1].Cards[1].Title)
	}

	// Same-column reorder: dragging the first card down to sit before the
	// original third card (toIndex names the slot before the removal).
	three := "## A\n- [ ] one\n- [ ] two\n- [ ] three"
	down := Parse(MoveCard(three, 0, 0, 0, 2, ""))
	if len(down.Columns[0].Cards) != 3 {
		t.Fatalf("down: %d cards", len(down.Columns[0].Cards))
	}
	if got := titlesOf(down.Columns[0]); !slices.Equal(got, []string{"two", "one", "three"}) {
		t.Errorf("down: %q", got)
	}

	// Same-column upward move needs no index adjustment.
	up := Parse(MoveCard(three, 0, 2, 0, 0, ""))
	if got := titlesOf(up.Columns[0]); !slices.Equal(got, []string{"three", "one", "two"}) {
		t.Errorf("up: %q", got)
	}

	// Dropping on the column background appends (toIndex == card count).
	app := Parse(MoveCard(three, 0, 0, 0, 3, ""))
	if got := titlesOf(app.Columns[0]); !slices.Equal(got, []string{"two", "three", "one"}) {
		t.Errorf("append: %q", got)
	}

	// Toggle done.
	if !Parse(ToggleCardDone(md, 0, 0, "")).Columns[0].Cards[0].Done {
		t.Error("toggle did not check the card")
	}

	// Add card / column.
	if n := len(Parse(AddCard(md, 0, "new", "")).Columns[0].Cards); n != 3 {
		t.Errorf("after AddCard: %d cards", n)
	}
	if n := Parse(AddColumn(md, "C")).ColumnCount(); n != 3 {
		t.Errorf("after AddColumn: %d columns", n)
	}

	// Remove card / column.
	if n := len(Parse(RemoveCard(md, 0, 0)).Columns[0].Cards); n != 1 {
		t.Errorf("after RemoveCard: %d cards", n)
	}
	if n := Parse(RemoveColumn(md, 1)).ColumnCount(); n != 1 {
		t.Errorf("after RemoveColumn: %d columns", n)
	}

	// Move a column.
	if name := Parse(MoveColumn(md, 0, 1)).Columns[0].Name; name != "B" {
		t.Errorf("after MoveColumn: first column %q", name)
	}
}

func TestUnmodelledContentSurvivesMutation(t *testing.T) {
	md := "An introductory paragraph about this board.\n" +
		"\n" +
		"<!-- a note the parser does not model -->\n" +
		"## To do\n" +
		"- [ ] one\n" +
		"- [ ] two\n"
	// Only the toggled checkbox differs; everything else, including the
	// trailing newline, comes back byte for byte.
	expected := "An introductory paragraph about this board.\n" +
		"\n" +
		"<!-- a note the parser does not model -->\n" +
		"## To do\n" +
		"- [x] one\n" +
		"- [ ] two\n"
	if got := ToggleCardDone(md, 0, 0, ""); got != expected {
		t.Errorf("got %q\nwant %q", got, expected)
	}
}

func TestTriviaInsideAColumnSurvivesEveryMutation(t *testing.T) {
	md := "Intro prose.\n" +
		"## To do\n" +
		"<!-- why this column exists -->\n" +
		"- [ ] one\n" +
		"\n" +
		"> a quoted aside\n" +
		"- [ ] two\n" +
		"## Done\n" +
		"- [x] shipped\n"
	marks := []string{"Intro prose.", "<!-- why this column exists -->", "> a quoted aside"}
	cases := []struct {
		what string
		out  string
	}{
		{"toggle", ToggleCardDone(md, 0, 0, "")},
		{"removeCard", RemoveCard(md, 0, 0)},
		{"addCard", AddCard(md, 0, "new", "")},
		{"moveCard", MoveCard(md, 0, 0, 1, 0, "")},
		{"setCard", SetCard(md, 0, 1, "t", true, nil, "", "", "")},
		{"setCardLine", SetCardLine(md, 0, 1, "typed text #tag", "")},
		{"setCardDescription", SetCardDescription(md, 0, 1, "typed\nlines", "")},
		{"addColumn", AddColumn(md, "C")},
		{"renameColumn", RenameColumn(md, 0, "Backlog")},
		{"removeColumn", RemoveColumn(md, 0)},
		{"moveColumn", MoveColumn(md, 0, 1)},
	}
	for _, c := range cases {
		for _, mark := range marks {
			if !strings.Contains(c.out, mark) {
				t.Errorf("%s dropped %s:\n%s", c.what, mark, c.out)
			}
		}
	}
}

// The mutation an untouched card sees is no mutation at all: its source line
// is written back verbatim rather than rendered again from the model.
func TestUntouchedCardsKeepTheirSourceLine(t *testing.T) {
	md := "## A\n* [ ]  odd   spacing #a #b\n- [ ] plain"
	if got, want := ToggleCardDone(md, 0, 1, ""), "## A\n* [ ]  odd   spacing #a #b\n- [x] plain"; got != want {
		t.Errorf("got %q\nwant %q", got, want)
	}
	// Toggling the odd one edits only its checkbox.
	if got, want := ToggleCardDone(md, 0, 0, ""), "## A\n* [x]  odd   spacing #a #b\n- [ ] plain"; got != want {
		t.Errorf("got %q\nwant %q", got, want)
	}
}

func TestURLFragmentIsNotALabel(t *testing.T) {
	md := "## A\n- [ ] Read https://example.com/#intro"
	b := Parse(md)
	if got := b.Columns[0].Cards[0].Title; got != "Read https://example.com/#intro" {
		t.Errorf("title = %q", got)
	}
	if len(b.Columns[0].Cards[0].Labels) != 0 {
		t.Errorf("labels = %q", b.Columns[0].Cards[0].Labels)
	}
	if got := b.Serialize(); got != md {
		t.Errorf("round trip = %q", got)
	}
	// Rewriting the card through the editor keeps the fragment too.
	out := SetCard(md, 0, 0, "Read https://example.com/#intro", false, nil, "", "", "")
	if out != md {
		t.Errorf("SetCard = %q", out)
	}
}

func TestLabelsAreRecognizedOnlyAtTokenBoundaries(t *testing.T) {
	b := Parse("## A\n- [ ] #lead mid #tag C#sharp a#b end")
	c := b.Columns[0].Cards[0]
	if !slices.Equal(c.Labels, []string{"lead", "tag"}) {
		t.Errorf("labels = %q", c.Labels)
	}
	if c.Title != "mid C#sharp a#b end" {
		t.Errorf("title = %q", c.Title)
	}
}

func TestLiteralHashesRoundTripThroughTheEscape(t *testing.T) {
	titles := []string{
		"#hashtag as text",
		`\#already escaped`,
		`\\#two slashes`,
		"issue #42 and #43",
		"trailing hash #",
		"Read https://example.com/#intro",
	}
	for _, title := range titles {
		md := SetCard("## A\n- [ ] x", 0, 0, title, false, []string{"real"}, "", "", "")
		c := Parse(md).Columns[0].Cards[0]
		if c.Title != title {
			t.Errorf("title %s came back as %s (source %s)", title, c.Title, md)
		}
		if !slices.Equal(c.Labels, []string{"real"}) {
			t.Errorf("labels of %q = %q", title, c.Labels)
		}
	}
}

// Parse(x).Serialize() == x for arbitrary content, and every unmodelled line
// survives any single mutation byte for byte. The boards come from
// mersenneTwister with a fixed seed (random_test.go).
func TestMutationPreservationProperty(t *testing.T) {
	rng := newMersenneTwister(0x4b616e62) // fixed seed: failures reproduce
	triviaShapes := []string{
		"<!-- note %1 -->",
		"Prose paragraph %1.",
		"> quoted aside %1",
		"1. an ordinary list item %1",
		"| a | table %1 |",
		"",
	}
	nextMark := 0

	for iter := 0; iter < 300; iter++ {
		var lines []string
		var marks []string // the identifiable trivia, in document order
		sprinkle := func() {
			n := rng.bounded(3)
			for i := 0; i < n; i++ {
				shape := triviaShapes[rng.bounded64(len(triviaShapes))]
				line := shape
				if strings.Contains(shape, "%1") {
					line = strings.ReplaceAll(shape, "%1", fmt.Sprint(nextMark))
					nextMark++
				}
				lines = append(lines, line)
				if line != "" {
					marks = append(marks, line)
				}
			}
		}

		sprinkle() // preamble
		cols := rng.bounded(4)
		var cardCounts []int
		for c := 0; c < cols; c++ {
			lines = append(lines, fmt.Sprintf("## Column %d", c))
			sprinkle()
			cards := rng.bounded(4)
			cardCounts = append(cardCounts, cards)
			for k := 0; k < cards; k++ {
				box := " "
				if rng.bounded(2) != 0 {
					box = "x"
				}
				lines = append(lines, fmt.Sprintf("- [%s] card %d-%d #tag%d", box, c, k, k))
				if rng.bounded(2) != 0 {
					lines = append(lines, fmt.Sprintf("  description of %d-%d", c, k))
				}
				sprinkle()
			}
		}
		md := strings.Join(lines, "\n")

		if got := Parse(md).Serialize(); got != md {
			t.Fatalf("iteration %d: round trip\n--- in ---\n%s\n--- out ---\n%s", iter, md, got)
		}

		var outs []string
		if cols > 0 {
			c := rng.bounded(cols)
			other := rng.bounded(cols)
			outs = append(outs,
				AddCard(md, c, "added", ""),
				AddColumn(md, "Added"),
				RenameColumn(md, c, "Renamed"),
				RemoveColumn(md, c),
				MoveColumn(md, c, other))
			if cardCounts[c] > 0 {
				k := rng.bounded(cardCounts[c])
				outs = append(outs,
					ToggleCardDone(md, c, k, ""),
					RemoveCard(md, c, k),
					SetCard(md, c, k, "rewritten", true, []string{"l"}, "2026-01-01", "d", ""),
					SetCardLine(md, c, k, "typed over #l", ""),
					SetCardLine(md, c, k, "stamped", "2026-07-26"),
					SetCardDescription(md, c, k, "typed\nover", ""),
					ToggleCardDone(md, c, k, "2026-07-26"),
					MoveCard(md, c, k, other, rng.bounded(cardCounts[other]+1), ""))
			}
		} else {
			outs = append(outs, AddColumn(md, "Added"))
		}

		for _, out := range outs {
			var seen []string
			for _, line := range strings.Split(out, "\n") {
				if slices.Contains(marks, line) {
					seen = append(seen, line)
				}
			}
			sortedSeen := slices.Sorted(slices.Values(seen))
			sortedMarks := slices.Sorted(slices.Values(marks))
			if !slices.Equal(sortedSeen, sortedMarks) {
				t.Fatalf("iteration %d: trivia lost\n--- in ---\n%s\n--- out ---\n%s", iter, md, out)
			}
			// Blank lines are unmodelled too; a mutation may move the run at
			// an insertion point but must not consume it.
			blanks := func(s string) int {
				if s == "" {
					return 0 // no lines at all, not one empty line
				}
				n := 0
				for _, l := range strings.Split(s, "\n") {
					if l == "" {
						n++
					}
				}
				return n
			}
			// A board that a removal emptied has nothing left to put a blank
			// line after, so that case is exempt.
			if out != "" && blanks(out) != blanks(md) {
				t.Fatalf("iteration %d: blank lines %d -> %d\n--- in ---\n%s\n--- out ---\n%s",
					iter, blanks(md), blanks(out), md, out)
			}
		}
	}
}

func TestSetCardOverwritesFields(t *testing.T) {
	md := "## A\n- [ ] one"
	out := SetCard(md, 0, 0, "renamed", true, []string{"x", "y"}, "2026-01-01", "notes", "")
	c := Parse(out).Columns[0].Cards[0]
	if c.Title != "renamed" {
		t.Errorf("title = %q", c.Title)
	}
	if !c.Done {
		t.Error("not done")
	}
	if !slices.Equal(c.Labels, []string{"x", "y"}) {
		t.Errorf("labels = %q", c.Labels)
	}
	if c.Due != "2026-01-01" {
		t.Errorf("due = %q", c.Due)
	}
	if c.Description != "notes" {
		t.Errorf("description = %q", c.Description)
	}
}

// The inline editor edits a card's own line, so what it writes back is what
// was typed, spacing and all, and the labels and the due date are whatever
// the next read finds in it. Nothing is rendered again from the fields, which
// is the difference from SetCard above.
func TestInlineLineEditsKeepWhatWasTyped(t *testing.T) {
	md := "## A\n* [x]  odd   spacing #a\n- [ ] plain"
	// The text goes on the line character for character; the indent, bullet
	// and checkbox in front of it stay as they were.
	if got, want := SetCardLine(md, 0, 0, "still   odd   #a #b", ""), "## A\n* [x] still   odd   #a #b\n- [ ] plain"; got != want {
		t.Errorf("got %q\nwant %q", got, want)
	}
	// And the fields come back out of that line.
	c := Parse(SetCardLine(md, 0, 1, "Ship it #release "+cal+" 2026-08-01", "")).Columns[0].Cards[1]
	if c.Title != "Ship it" {
		t.Errorf("title = %q", c.Title)
	}
	if !slices.Equal(c.Labels, []string{"release"}) {
		t.Errorf("labels = %q", c.Labels)
	}
	if c.Due != "2026-08-01" {
		t.Errorf("due = %q", c.Due)
	}
	if c.Done {
		t.Error("done")
	}
	// A line break would open a second card, so it becomes a space.
	if got, want := SetCardLine("## A\n- [ ] x", 0, 0, "one\ntwo", ""), "## A\n- [ ] one two"; got != want {
		t.Errorf("got %q\nwant %q", got, want)
	}
	// An out-of-range card leaves the board alone.
	if got := SetCardLine(md, 0, 9, "nope", ""); got != md {
		t.Errorf("card 9: %q", got)
	}
	if got := SetCardLine(md, 3, 0, "nope", ""); got != md {
		t.Errorf("column 3: %q", got)
	}
}

// A card's description is the indented run under it: the inline editor
// writes every line of it back, so a description with several lines
// (formulas, wiki-links, anything) reads back as it was typed.
func TestInlineDescriptionEditsKeepTheirLines(t *testing.T) {
	md := "## A\n- [ ] one\n  old note\n- [ ] two"
	out := SetCardDescription(md, 0, 0, "See [[Design notes]]\nand $E = mc^2$", "")
	if want := "## A\n- [ ] one\n  See [[Design notes]]\n  and $E = mc^2$\n- [ ] two"; out != want {
		t.Errorf("got %q\nwant %q", out, want)
	}
	if got := Parse(out).Columns[0].Cards[0].Description; got != "See [[Design notes]]\nand $E = mc^2$" {
		t.Errorf("description = %q", got)
	}
	// The card's own line is untouched by a description edit.
	if got := Parse(out).Columns[0].Cards[0].Title; got != "one" {
		t.Errorf("title = %q", got)
	}
	// Emptying it removes the run rather than leaving a blank line.
	if got, want := SetCardDescription(md, 0, 0, "", ""), "## A\n- [ ] one\n- [ ] two"; got != want {
		t.Errorf("got %q\nwant %q", got, want)
	}
	if got := SetCardDescription(md, 0, 9, "nope", ""); got != md {
		t.Errorf("card 9: %q", got)
	}
}

// A description is prose, and prose has paragraphs and indented structure.
// Ending the description at the first blank line would cut such a
// description in half, and trimming each line would flatten whatever is
// nested inside one.
func TestDescriptionsKeepTheirBlankLinesAndTheirNesting(t *testing.T) {
	md := "## A\n- [ ] one\n" +
		"  first paragraph\n" +
		"\n" +
		"  second paragraph\n" +
		"  - a nested item\n" +
		"    - deeper still\n" +
		"- [ ] two"
	board := Parse(md)
	if len(board.Columns[0].Cards) != 2 {
		t.Fatalf("cards = %d, want 2", len(board.Columns[0].Cards))
	}
	want := "first paragraph\n\nsecond paragraph\n- a nested item\n  - deeper still"
	if got := board.Columns[0].Cards[0].Description; got != want {
		t.Errorf("description = %q\nwant %q", got, want)
	}

	// Untouched, the board is byte-identical.
	if got := board.Serialize(); got != md {
		t.Errorf("round trip = %q", got)
	}

	// And a description written back from the field keeps the shape it was
	// read with, so the next parse reads the same thing again.
	if out := SetCardDescription(md, 0, 0, board.Columns[0].Cards[0].Description, ""); out != md {
		t.Errorf("rewritten = %q", out)
	}
}

// The `<!--kvit …-->` comment is this application's own namespace, and an
// edit rewrites the whole comment from the two fields this version knows. A
// field it does not know has to survive that, or opening a board in an older
// build and touching one card deletes what a newer one recorded.
func TestUnknownStampFieldsSurviveAnEdit(t *testing.T) {
	md := "## A\n- [ ] Ship it <!--kvit created=2026-07-20 " +
		"sprint=2026-31 modified=2026-07-26-->"
	board := Parse(md)
	if got := board.Columns[0].Cards[0].Created; got != "2026-07-20" {
		t.Errorf("created = %q", got)
	}
	if got := board.Columns[0].Cards[0].Modified; got != "2026-07-26" {
		t.Errorf("modified = %q", got)
	}

	out := ToggleCardDone(md, 0, 0, "2026-08-01")
	if !strings.Contains(out, "sprint=2026-31") {
		t.Errorf("a field this version does not interpret was deleted by an edit that had nothing to do with it: %s", out)
	}
	if got := Parse(out).Columns[0].Cards[0].Modified; got != "2026-08-01" {
		t.Errorf("modified after the edit = %q", got)
	}
}

// The text the inline editor is given for a card's line: what the file has,
// for a card that has a line, and what Serialize is about to write for one a
// mutation just created. Either way, passing it straight back through
// SetCardLine changes nothing.
func TestCardLineTextIsWhatTheFileHas(t *testing.T) {
	md := "## A\n* [x]  odd   spacing #a\n- [ ] plain"
	lineOf := func(board string, col, idx int) string {
		return Parse(board).Columns[col].Cards[idx].Line()
	}
	if got := lineOf(md, 0, 0); got != " odd   spacing #a" {
		t.Errorf("line = %q", got)
	}
	if got := SetCardLine(md, 0, 0, lineOf(md, 0, 0), ""); got != md {
		t.Errorf("written back = %q", got)
	}
	// A card with no source line renders its text from the fields.
	added := AddCard(md, 0, "fresh", "")
	if got := lineOf(added, 0, 2); got != "fresh" {
		t.Errorf("added line = %q", got)
	}
	if got := SetCardLine(added, 0, 2, lineOf(added, 0, 2), ""); got != added {
		t.Errorf("added written back = %q", got)
	}
}

// A card's dates are in an HTML comment at the end of its line, so they stay
// out of the text the reader types and out of every other Markdown tool's
// way. What the reader edits is the line without them.
func TestCardDatesRideInACommentOffTheLine(t *testing.T) {
	md := "## A\n- [ ] Ship it #release <!--kvit created=2026-07-20 " +
		"modified=2026-07-26-->\n- [ ] plain"
	b := Parse(md)
	c := b.Columns[0].Cards[0]
	if c.Created != "2026-07-20" || c.Modified != "2026-07-26" {
		t.Errorf("created %q, modified %q", c.Created, c.Modified)
	}
	// Nothing in the comment reaches the title, the labels or the due date.
	if c.Title != "Ship it" {
		t.Errorf("title = %q", c.Title)
	}
	if !slices.Equal(c.Labels, []string{"release"}) {
		t.Errorf("labels = %q", c.Labels)
	}
	if c.Due != "" {
		t.Errorf("due = %q", c.Due)
	}
	if got := b.Columns[0].Cards[1].Created; got != "" {
		t.Errorf("plain card created = %q", got)
	}
	// And the board comes back byte for byte.
	if got := b.Serialize(); got != md {
		t.Errorf("round trip = %q", got)
	}

	// A card written with only the day it was added reads that day as both,
	// because it has not been changed since.
	once := Parse("## A\n- [ ] new <!--kvit created=2026-07-20-->").Columns[0].Cards[0]
	if once.Created != "2026-07-20" || once.Modified != "2026-07-20" {
		t.Errorf("created %q, modified %q", once.Created, once.Modified)
	}

	// The editor is given the line without the comment, and putting that
	// text straight back leaves the line as it was.
	card := Parse(md).Columns[0].Cards[0]
	if got := card.Line(); got != "Ship it #release" {
		t.Errorf("line = %q", got)
	}
	if card.Created != "2026-07-20" {
		t.Errorf("created = %q", card.Created)
	}
	if got := SetCardLine(md, 0, 0, "Ship it #release", ""); got != md {
		t.Errorf("written back = %q", got)
	}
}

// The day comes from the caller, so these functions stay pure and their tests
// do not depend on the day they run on. No day means no dates, which is why
// every test above still reads the boards it wrote.
func TestMutationsStampTheDayTheyHappenOn(t *testing.T) {
	added := AddCard("## A", 0, "one", "2026-07-20")
	if want := "## A\n- [ ] one <!--kvit created=2026-07-20-->"; added != want {
		t.Errorf("added = %q", added)
	}
	if got, want := AddCard("## A", 0, "one", ""), "## A\n- [ ] one"; got != want {
		t.Errorf("added without a day = %q", got)
	}

	// A later edit keeps the day the card was added and records its own.
	edited := SetCardLine(added, 0, 0, "one two", "2026-07-26")
	if want := "## A\n- [ ] one two <!--kvit created=2026-07-20 modified=2026-07-26-->"; edited != want {
		t.Errorf("edited = %q", edited)
	}
	// Editing again the same day rewrites nothing but the text.
	if got, want := SetCardLine(edited, 0, 0, "one three", "2026-07-26"),
		"## A\n- [ ] one three <!--kvit created=2026-07-20 modified=2026-07-26-->"; got != want {
		t.Errorf("edited again = %q", got)
	}

	// The description, the fields and the checkbox are changes too.
	if got := SetCardDescription(added, 0, 0, "note", "2026-07-26"); !strings.Contains(got, "modified=2026-07-26") {
		t.Errorf("description edit = %q", got)
	}
	if got := SetCard(added, 0, 0, "t", true, nil, "", "", "2026-07-26"); !strings.Contains(got, "modified=2026-07-26") {
		t.Errorf("SetCard = %q", got)
	}
	toggled := ToggleCardDone(added, 0, 0, "2026-07-26")
	if !strings.HasPrefix(toggled, "## A\n- [x] one ") {
		t.Error(toggled)
	}
	if !strings.Contains(toggled, "modified=2026-07-26") {
		t.Errorf("toggled = %q", toggled)
	}

	// Moving a card to another column is a change; sliding it inside the
	// column it is already in is not.
	two := "## A\n- [ ] one <!--kvit created=2026-07-20-->\n- [ ] two\n## B"
	if got := MoveCard(two, 0, 0, 1, 0, "2026-07-26"); !strings.Contains(got, "modified=2026-07-26") {
		t.Errorf("moved to another column = %q", got)
	}
	if got := MoveCard(two, 0, 0, 0, 2, "2026-07-26"); strings.Contains(got, "modified=") {
		t.Errorf("moved within its column = %q", got)
	}

	// A day the calendar does not have stamps nothing, the same test the due
	// date is held to.
	if got, want := AddCard("## A", 0, "one", "2026-02-30"), "## A\n- [ ] one"; got != want {
		t.Errorf("added on 2026-02-30 = %q", got)
	}

	// A card that predates the dates keeps an empty created: the board shows
	// what it knows rather than claiming the card was made today.
	old := Parse(SetCardLine("## A\n- [ ] old", 0, 0, "older", "2026-07-26")).Columns[0].Cards[0]
	if old.Created != "" {
		t.Errorf("created = %q", old.Created)
	}
	if old.Modified != "2026-07-26" {
		t.Errorf("modified = %q", old.Modified)
	}
}

// What the card editor accepts has to survive being written to the board and
// read back: a label with a space or a hash is written quoted, and the bare
// spelling is still used wherever it fits.
func TestLabelsAcceptedByTheEditorRoundTrip(t *testing.T) {
	rows := []struct {
		name   string
		labels []string
	}{
		{"plain", []string{"urgent"}},
		{"space", []string{"client work"}},
		{"hash inside", []string{"a#b"}},
		{"leading hash", []string{"#nested"}},
		{"quote", []string{`say "hi"`}},
		{"backslash", []string{`a\b`}},
		{"escaped quote", []string{`a\"b`}},
		{"tab", []string{"a\tb"}},
		{"several", []string{"client work", "plain", `a "b" c`}},
	}
	for _, row := range rows {
		t.Run(row.name, func(t *testing.T) {
			md := SetCard("## A\n- [ ] x", 0, 0, "the title", false, row.labels, "", "", "")
			c := Parse(md).Columns[0].Cards[0]
			if !slices.Equal(c.Labels, row.labels) {
				t.Errorf("labels came back as [%s] (source %s)", strings.Join(c.Labels, "|"), md)
			}
			if c.Title != "the title" {
				t.Errorf("title = %q", c.Title)
			}
			// And the board text itself round-trips unchanged.
			if got := Parse(md).Serialize(); got != md {
				t.Errorf("round trip = %q", got)
			}
		})
	}
}

// The bare spelling stays the default, so boards written by hand or by
// earlier versions are not rewritten into quoted syntax.
func TestOrdinaryLabelsKeepTheBareSpelling(t *testing.T) {
	md := SetCard("## A\n- [ ] x", 0, 0, "t", false, []string{"urgent", "home"}, "", "", "")
	if want := "## A\n- [ ] t #urgent #home"; md != want {
		t.Errorf("got %q\nwant %q", md, want)
	}
}

// A board written before quoting existed still reads exactly as it did.
func TestQuotedLabelSyntaxDoesNotDisturbOlderBoards(t *testing.T) {
	b := Parse("## A\n- [ ] t #urgent #a\\b #c\"d")
	if got, want := b.Columns[0].Cards[0].Labels, []string{"urgent", `a\b`, `c"d`}; !slices.Equal(got, want) {
		t.Errorf("labels = %q, want %q", got, want)
	}
}

// A due value the stored form cannot hold is refused when the card is saved,
// and IsValidDue lets the editor say so before the user loses it. Reader and
// writer apply the same test, so a value one accepts is a value the other
// accepts.
func TestInvalidDueValuesAreRefusedRatherThanCorrupted(t *testing.T) {
	rows := []struct {
		name  string
		due   string
		valid bool
	}{
		{"iso", "2026-07-15", true},
		{"word", "tomorrow", false},
		{"us order", "07/15/2026", false},
		{"no such day", "2026-02-30", false},
		{"month 13", "2026-13-01", false},
		{"empty", "", false},
	}
	for _, row := range rows {
		t.Run(row.name, func(t *testing.T) {
			if got := IsValidDue(row.due); got != row.valid {
				t.Errorf("IsValidDue(%q) = %v", row.due, got)
			}
			want := ""
			if row.valid {
				want = row.due
			}

			md := SetCard("## A\n- [ ] x", 0, 0, "the title", false, nil, row.due, "", "")
			c := Parse(md).Columns[0].Cards[0]
			if c.Due != want {
				t.Errorf("due = %q, want %q", c.Due, want)
			}
			// Either way the title is the title: a rejected value never
			// leaks into it.
			if c.Title != "the title" {
				t.Errorf("title = %q", c.Title)
			}

			// And a card line holding that value directly reads back the same
			// way, so what the writer refuses is exactly what the reader
			// refuses.
			direct := Parse("## A\n- [ ] the title " + cal + " " + row.due).Columns[0].Cards[0]
			if direct.Due != want {
				t.Errorf("due read directly = %q, want %q", direct.Due, want)
			}
		})
	}
}

// The mirror of the hash rule: a title that reads like a due date keeps its
// text, because the marker is escaped on the way out.
func TestDueMarkersInTitlesRoundTripThroughTheEscape(t *testing.T) {
	titles := []string{
		cal + " 2026-07-15",
		"due " + cal + " 2026-07-15 for real",
		`\` + cal + " 2026-07-15",
		cal + " not a date",
		// Shape of a date, but not a day the calendar has. The reader leaves
		// it as text, so the writer must not escape it either.
		cal + " 2026-02-30",
	}
	for _, title := range titles {
		md := SetCard("## A\n- [ ] x", 0, 0, title, false, nil, "2026-01-01", "", "")
		c := Parse(md).Columns[0].Cards[0]
		if c.Title != title {
			t.Errorf("title %s came back as %s (source %s)", title, c.Title, md)
		}
		if c.Due != "2026-01-01" {
			t.Errorf("due of %q = %q", title, c.Due)
		}
	}
}
