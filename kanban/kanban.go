// Package kanban reads and writes Kvit's task boards. It is a port of the
// app's src/content/kanbandata.h and kanbandata.cpp (namespace KanbanData)
// and writes the same text as that code for every input.
//
// A task board is a fenced code block whose language tag is `kanban`. The note
// stores nothing else about it: the body of the fence is ordinary Markdown that
// any editor can read and change, for example
//
//	## To do
//	- [ ] Ship the beta #release #"client work" 📅 2026-08-01 <!--kvit created=2026-07-20 modified=2026-07-26-->
//
// Lines indented under a card are its description.
//   - [x] A finished card
//     ## Done
//
// A line starting with `## ` opens a column named by the rest of the line. A
// line `- [ ] ` or `- [x] ` is a card, done when the box is checked; a `*`
// bullet and `[X]` also work. On a card's line:
//   - a `#label` token at the start of the text or after whitespace is a
//
// label, so a URL fragment such as https://example.com/#intro stays title
// text. A label containing a space, a hash, a quote or a backslash is
// written quoted, as `#"client work"`, with `\` and `"` escaped inside the
// quotes; the bare spelling is written whenever it fits.
//   - `📅 YYYY-MM-DD` is the due date. Only a day the calendar has counts;
//
// anything else after the marker is title text.
//   - a backslash before `#` or `📅` makes it literal title text, and a run of
//
// backslashes halves, so `\#` is a literal hash and `\\#tag` is a
// backslash followed by a label.
//   - an HTML comment `<!--kvit created=… modified=…-->` at the end of the line
//
// holds the day the card was added and the day it last changed. Other
// Markdown tools show nothing for it, and the text the board's inline
// editor shows is the line without it (Card.Line).
//
// Lines indented by two spaces or a tab under a card are its description,
// including blank lines between such lines.
//
// Everything else in the fence (an introductory paragraph, an HTML comment, a
// blank line, a stray list item) is not part of the board model. The C++ code
// calls these lines trivia, and so do the field names here. Each parsed Board,
// Column and Card keeps the source lines it came from and the trivia lines
// that followed it, and Serialize writes all of it back, so
// Parse(x).Serialize() == x for any x and a mutation rewrites only the lines
// it changes. Trivia belongs to a position rather than to a card: a card that
// moves leaves the trivia after it where it was, and trivia whose position is
// removed moves to the position before it.
//
// Every mutation takes the whole fence body and returns the whole new body,
// which the editor applies as one undo step. A mutation that changes a card
// also takes the day it happens on, as an ISO `YYYY-MM-DD` string, and writes
// it into the card's comment; an empty day writes nothing. The caller supplies
// the day (for example time.Now().Format(time.DateOnly)), so the functions
// stay pure and tests do not depend on the day they run on.
package kanban

import (
	"regexp"
	"slices"
	"strings"
	"unicode"
)

// Card is one card of a column (KanbanData::Card).
type Card struct {
	Title  string
	Done   bool
	Labels []string
	// Due is the due date as an ISO day, or empty.
	Due         string
	Description string

	// Created and Modified are the days the card was added and last changed,
	// as ISO days, read from the `<!--kvit …-->` comment at the end of the
	// card's line. Both are empty for a card written before the dates were
	// recorded. A comment with only created= reads as Modified == Created,
	// because the card has not changed since it was added.
	Created  string
	Modified string

	// The fields below keep the card's source text, so that Serialize writes
	// an unchanged card back byte for byte. They are not part of what the
	// board shows. A mutation that changes a modelled field clears the source
	// field holding it, and Serialize then writes that part from the modelled
	// fields.

	// StampExtras is every field of the `<!--kvit …-->` comment other than
	// created= and modified=, verbatim and in source order. An edit rewrites
	// the whole comment from Created and Modified, so without this a field
	// written by a later version of Kvit would be deleted by any edit of the
	// card.
	StampExtras string
	// RawLine is the card's exact source line, or empty for a card a
	// mutation created, whose line is written from the fields.
	RawLine string
	// DescriptionIndent is the indent of the description's first line. Only
	// this much comes off each description line when it is read, and it goes
	// back on when the description is written, so a list nested inside a
	// description keeps its shape.
	DescriptionIndent string
	// RawDescription is the exact source lines of the description.
	RawDescription []string
	// TrailingTrivia is the unmodelled lines that followed this card.
	TrailingTrivia []string
}

// Column is one column of a board (KanbanData::Column).
type Column struct {
	Name  string
	Cards []Card
	// RawHeader is the exact source of the `## ` line, or empty for a column
	// a mutation created or renamed.
	RawHeader string
	// LeadingTrivia is the unmodelled lines between the header and the first
	// card.
	LeadingTrivia []string
}

// Board is the parsed body of a `kanban` fence (KanbanData::Board).
type Board struct {
	Columns []Column
	// Preamble is the unmodelled lines before the first column header.
	// Content with no header at all is kept here in full.
	Preamble []string
}

// ColumnCount returns the number of columns.
func (b *Board) ColumnCount() int { return len(b.Columns) }

// calendar is the due-date marker, U+1F4C5.
const calendar = "📅"

// The kanbandata.cpp patterns are compiled by the regular expression
// without its Unicode-properties option, so PCRE's \s there matches only the
// six ASCII whitespace characters. Go's \s leaves out the vertical tab, so the
// patterns below spell the class out. (\d and \w are ASCII-only in both.)
const (
	spaceClass    = `\t\n\v\f\r `
	space         = `[` + spaceClass + `]`
	notSpaceOrTag = `[^` + spaceClass + `#]`
)

// labelRe is a `#label` token (labelRe() in kanbandata.cpp). It is recognized
// only at a token boundary, the start of the text or right after whitespace,
// so a URL fragment stays part of the title. A run of backslashes may come
// before the hash: an odd-length run escapes the hash into a literal one, and
// the run itself halves, so `\#` is a literal `#` and `\\#tag` is a backslash
// followed by the label.
//
// The label has two spellings. The bare one, everything up to the next space
// or hash (group 4), is what boards written by hand and by earlier versions of
// Kvit contain; it cannot hold a space or a hash, so `client work` written bare
// reads as the label `client` and the title word `work`. The quoted one,
// `#"client work"` (group 3), can hold anything: inside it a backslash escapes
// the next character, so `"` and `\` survive too.
var labelRe = regexp.MustCompile(`(^|` + space + `)(\\*)#(?:"((?:\\.|[^"\\])*)"|(` + notSpaceOrTag + `*))`)

// dueRe is the due-date marker and its date (dueRe() in kanbandata.cpp), with
// the same backslash escape as the hash, so a title that reads
// "📅 2026-07-15" is written with the marker escaped and stays title text.
var dueRe = regexp.MustCompile(`(\\*)` + calendar + space + `*(\d{4}-\d{2}-\d{2})`)

// cardRe is a card line after trimming (the cardRe in KanbanData::parse):
// group 1 is the box's content, group 2 the text after it.
var cardRe = regexp.MustCompile(`^[-*] \[( |x|X)\] ?(.*)$`)

// cardPrefixRe is a card line's indent, bullet and checkbox (group 1) and the
// one space that may follow them (group 2), from cardPrefixRe() in
// kanbandata.cpp. Everything after is the card's text.
var cardPrefixRe = regexp.MustCompile(`^(` + space + `*[-*] \[[ xX]\])( ?)`)

// stampRe is the comment holding a card's dates at the end of its line
// (stampRe() in kanbandata.cpp). It is taken off the end of the line before
// anything else reads the text, so nothing inside it is read as a label, a
// due date or title words. Group 1 is the comment's fields.
var stampRe = regexp.MustCompile(space + `*<!--kvit((?:` + space + `+\w+=[0-9-]+)*)` + space + `*-->` + space + `*$`)

// stampFieldRe is one `name=value` field of that comment (the fieldRe in
// stampExtraFields()).
var stampFieldRe = regexp.MustCompile(`(\w+)=([0-9-]+)`)

// stampValueRe holds, for each field this version reads, the pattern
// stampValue() in kanbandata.cpp builds from the field's name.
var stampValueRe = map[string]*regexp.Regexp{
	"created":  regexp.MustCompile(`created=(\d{4}-\d{2}-\d{2})`),
	"modified": regexp.MustCompile(`modified=(\d{4}-\d{2}-\d{2})`),
}

// boxRe is a card line up to its checkbox (the boxRe in
// KanbanData::toggleCardDone): group 2 is the character inside the box.
var boxRe = regexp.MustCompile(`^(` + space + `*[-*] \[)( |x|X)(\])`)

// isRealDate reports whether text is a day the calendar has, written
// YYYY-MM-DD (isRealDate() in kanbandata.cpp). The code asks
// QDate::fromString(text, "yyyy-MM-dd"), which in 6.10 accepts exactly ten
// characters, four ASCII digits, a hyphen, two digits, a hyphen and two
// digits, naming a day of the proleptic Gregorian calendar from 0001-01-01 to
// 9999-12-31. Year 0000 does not exist there. Reader and writer both ask this,
// so what Serialize writes after the marker is exactly what Parse reads back.
func isRealDate(text string) bool {
	if len(text) != 10 || text[4] != '-' || text[7] != '-' {
		return false
	}
	year, month, day := digits(text[0:4]), digits(text[5:7]), digits(text[8:10])
	if year < 1 || month < 1 || month > 12 || day < 1 {
		return false
	}
	days := [...]int{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}[month-1]
	if month == 2 && year%4 == 0 && (year%100 != 0 || year%400 == 0) {
		days = 29
	}
	return day <= days
}

// digits returns the value of a string of ASCII digits, or -1 if it holds
// anything else.
func digits(s string) int {
	n := 0
	for i := 0; i < len(s); i++ {
		if s[i] < '0' || s[i] > '9' {
			return -1
		}
		n = n*10 + int(s[i]-'0')
	}
	return n
}

// trimmed is string::trimmed(): QChar::isSpace and unicode.IsSpace, which
// strings.TrimSpace uses, are the same set of characters.
func trimmed(s string) string { return strings.TrimSpace(s) }

// simplified is string::simplified(): trimmed, with every inner run of
// whitespace replaced by one space.
func simplified(s string) string { return strings.Join(strings.Fields(s), " ") }

// unescapeLabel undoes the escaping writeLabel applies inside a quoted label:
// a backslash before `\` or `"` and no other, so a hand-written `#"a\b"`
// keeps its backslash.
func unescapeLabel(text string) string {
	var out strings.Builder
	out.Grow(len(text))
	for i := 0; i < len(text); i++ {
		c := text[i]
		if c == '\\' && i+1 < len(text) && (text[i+1] == '\\' || text[i+1] == '"') {
			i++
			out.WriteByte(text[i])
			continue
		}
		out.WriteByte(c)
	}
	return out.String()
}

// writeLabel returns a label as it goes on the card line, without the leading
// hash: bare when it has no whitespace (QChar::isSpace, the same set as
// unicode.IsSpace), hash, quote or backslash, and quoted otherwise. It
// returns "" for the empty label, which cannot be written, and
// renderedCardBody then leaves it out.
func writeLabel(label string) string {
	if label == "" {
		return ""
	}
	if !strings.ContainsFunc(label, func(r rune) bool {
		return unicode.IsSpace(r) || r == '#' || r == '"' || r == '\\'
	}) {
		return label
	}
	var out strings.Builder
	out.Grow(len(label) + 2)
	out.WriteByte('"')
	for i := 0; i < len(label); i++ {
		if label[i] == '"' || label[i] == '\\' {
			out.WriteByte('\\')
		}
		out.WriteByte(label[i])
	}
	out.WriteByte('"')
	return out.String()
}

// edit is one deferred text replacement. applyEdits applies a list of them
// back to front, so the byte offsets of the earlier ones stay valid.
type edit struct {
	start, length int
	replacement   string
}

func applyEdits(text string, edits []edit) string {
	for i := len(edits) - 1; i >= 0; i-- {
		e := edits[i]
		text = text[:e.start] + e.replacement + text[e.start+e.length:]
	}
	return text
}

// parseCardBody reads a card line's text into the card's title, labels and
// due date (parseCardBody() in kanbandata.cpp).
func parseCardBody(rest string, card *Card) {
	body := rest

	// The due date. The first unescaped marker followed by a real day is the
	// card's; an escaped marker keeps its text and halves its backslash run,
	// as the hash does. Later or unreal dates stay part of the title.
	var edits []edit
	for _, m := range dueRe.FindAllStringSubmatchIndex(body, -1) {
		slashes := m[3] - m[2]
		keptSlashes := strings.Repeat(`\`, slashes/2)
		if slashes%2 == 1 {
			edits = append(edits, edit{m[2], slashes, keptSlashes})
			continue
		}
		date := body[m[4]:m[5]]
		if card.Due != "" || !isRealDate(date) {
			continue
		}
		card.Due = date
		edits = append(edits, edit{m[2], m[1] - m[2], keptSlashes})
	}
	body = applyEdits(body, edits)
	edits = nil

	// The labels, collected in order and removed from the title.
	for _, m := range labelRe.FindAllStringSubmatchIndex(body, -1) {
		slashes := m[5] - m[4]
		var label string
		if m[6] >= 0 {
			label = unescapeLabel(body[m[6]:m[7]])
		} else if m[8] >= 0 {
			label = body[m[8]:m[9]]
		}
		// The backslash run always halves, whatever follows it.
		keptSlashes := strings.Repeat(`\`, slashes/2)
		if slashes%2 == 1 || label == "" {
			// Escaped, or a bare `#` with no label text: keep the hash.
			edits = append(edits, edit{m[4], slashes, keptSlashes})
			continue
		}
		card.Labels = append(card.Labels, label)
		edits = append(edits, edit{m[4], m[1] - m[4], keptSlashes})
	}
	body = applyEdits(body, edits)

	card.Title = simplified(body)
}

// escapeTitle is the inverse of parseCardBody's escapes (escapeTitle() in
// kanbandata.cpp): a hash or a due marker in a title that would be read back
// as a label or a due date is escaped, so the title survives as text.
// Doubling the backslash run and adding one leaves an odd run, which
// parseCardBody reads as literal, and its halving restores the original run.
func escapeTitle(title string) string {
	var edits []edit
	for _, m := range dueRe.FindAllStringSubmatchIndex(title, -1) {
		slashes := m[3] - m[2]
		if !isRealDate(title[m[4]:m[5]]) {
			continue // the reader leaves this as text, so it is left alone
		}
		edits = append(edits, edit{m[2], slashes + len(calendar),
			strings.Repeat(`\`, slashes*2) + `\` + calendar})
	}
	for _, m := range labelRe.FindAllStringSubmatchIndex(title, -1) {
		slashes := m[5] - m[4]
		edits = append(edits, edit{m[4], slashes + 1,
			strings.Repeat(`\`, slashes*2) + `\#`})
	}
	// applyEdits works back to front, so the two sets, which never overlap,
	// have to be in source order.
	slices.SortFunc(edits, func(a, b edit) int { return a.start - b.start })
	return applyEdits(title, edits)
}

// stampValue returns one `name=value` date from the comment's fields, or ""
// when the first such field is not a real day (stampValue() in
// kanbandata.cpp).
func stampValue(fields, name string) string {
	m := stampValueRe[name].FindStringSubmatch(fields)
	if m != nil && isRealDate(m[1]) {
		return m[1]
	}
	return ""
}

// stampExtraFields returns the comment's fields other than created= and
// modified=, verbatim, in source order and joined by single spaces
// (stampExtraFields() in kanbandata.cpp).
func stampExtraFields(fields string) string {
	var kept []string
	for _, m := range stampFieldRe.FindAllStringSubmatch(fields, -1) {
		if m[1] != "created" && m[1] != "modified" {
			kept = append(kept, m[0])
		}
	}
	return strings.Join(kept, " ")
}

// leadingIndent returns the run of spaces and tabs a line starts with.
func leadingIndent(line string) string {
	i := 0
	for i < len(line) && (line[i] == ' ' || line[i] == '\t') {
		i++
	}
	return line[:i]
}

// withoutStamp returns a line without the dates comment at its end.
func withoutStamp(line string) string {
	if loc := stampRe.FindStringIndex(line); loc != nil {
		return line[:loc[0]]
	}
	return line
}

// stampComment returns the comment for a card that has dates, and "" for one
// that has none, so an untouched board gains no comments (stampComment() in
// kanbandata.cpp). modified= is written only when it differs from created=,
// so a card that was added and not changed since has one date.
func stampComment(card *Card) string {
	var fields []string
	if card.Created != "" {
		fields = append(fields, "created="+card.Created)
	}
	if card.Modified != "" && card.Modified != card.Created {
		fields = append(fields, "modified="+card.Modified)
	}
	if card.StampExtras != "" {
		fields = append(fields, card.StampExtras)
	}
	if len(fields) == 0 {
		return ""
	}
	return " <!--kvit " + strings.Join(fields, " ") + "-->"
}

// stampCard records that a card changed on today: the day it was added if it
// has none yet and creating is set, and the day it last changed either way
// (stampCard() in kanbandata.cpp). An empty or unreal today records nothing.
// The card's source line is edited in place rather than dropped, so a card
// whose dates changed keeps its spacing, its bullet and its label order.
func stampCard(card *Card, today string, creating bool) {
	if today == "" || !isRealDate(today) {
		return
	}
	if creating && card.Created == "" {
		card.Created = today
	}
	card.Modified = today
	if card.RawLine != "" {
		card.RawLine = withoutStamp(card.RawLine) + stampComment(card)
	}
}

// renderedCardBody returns the card's text as the fields describe it, which
// is what Serialize writes for a card with no source line.
func renderedCardBody(card *Card) string {
	body := escapeTitle(card.Title)
	for _, label := range card.Labels {
		if token := writeLabel(label); token != "" {
			body += " #" + token
		}
	}
	// Only a real day goes after the marker: the reader reads nothing else
	// back, so writing "📅 tomorrow" would move the text into the title and
	// clear the field. SetCard refuses such a value; this is the same rule
	// where the line is built.
	if isRealDate(card.Due) {
		body += " " + calendar + " " + card.Due
	}
	return body
}

// Line returns the text on the card's own line: everything after the
// checkbox and before the comment holding the dates, as the file has it. For
// a card a mutation created, which has no line yet, it is the text Serialize
// is about to write. This is the text the board's inline editor shows, and
// passing it unchanged to SetCardLine leaves the board as it was. It is the
// `line` value of KanbanTools::parse (cardBody() in kanbandata.cpp).
func (c *Card) Line() string {
	if c.RawLine == "" {
		return renderedCardBody(c)
	}
	if m := cardPrefixRe.FindStringIndex(c.RawLine); m != nil {
		return withoutStamp(c.RawLine[m[1]:])
	}
	return withoutStamp(c.RawLine)
}

// cardPrefix returns what goes in front of the card's text when it is
// rewritten: its own indent, bullet and checkbox, so a `*` bullet or an
// indented card keeps its form and the done state survives an edit of the
// text (cardPrefix() in kanbandata.cpp).
func cardPrefix(card *Card) string {
	if m := cardPrefixRe.FindStringSubmatch(card.RawLine); m != nil {
		return m[1] + " "
	}
	if card.Done {
		return "- [x] "
	}
	return "- [ ] "
}

func isBlank(line string) bool { return trimmed(line) == "" }

// takeTrailingBlanks removes the run of blank lines at the end of trivia and
// returns it. The run at an insertion point goes after whatever is appended
// there, so a board whose source ends in a newline still ends in one, rather
// than gaining a blank line before each card added.
func takeTrailingBlanks(trivia *[]string) []string {
	t := *trivia
	keep := len(t)
	for keep > 0 && isBlank(t[keep-1]) {
		keep--
	}
	if keep == len(t) {
		return nil
	}
	blanks := slices.Clone(t[keep:])
	*trivia = t[:keep]
	return blanks
}

// lastTriviaSlot returns where an append at the end of the board goes. The
// pointer is valid until the board changes again.
func lastTriviaSlot(b *Board) *[]string {
	if len(b.Columns) == 0 {
		return &b.Preamble
	}
	return lastSlotOf(&b.Columns[len(b.Columns)-1])
}

// lastSlotOf returns the last trivia position of a column: after its last
// card, or after its header when it has no cards.
func lastSlotOf(col *Column) *[]string {
	if len(col.Cards) == 0 {
		return &col.LeadingTrivia
	}
	return &col.Cards[len(col.Cards)-1].TrailingTrivia
}

// triviaSlotBeforeColumn returns the trivia position just before column col.
func triviaSlotBeforeColumn(b *Board, col int) *[]string {
	if col <= 0 {
		return &b.Preamble
	}
	return lastSlotOf(&b.Columns[col-1])
}

// triviaSlotBeforeCard returns the trivia position just before card index of
// col.
func triviaSlotBeforeCard(col *Column, index int) *[]string {
	if index <= 0 {
		return &col.LeadingTrivia
	}
	return &col.Cards[index-1].TrailingTrivia
}

// IsValidDue reports whether a due value is one the stored form can hold: an
// ISO `YYYY-MM-DD` naming a day that exists. SetCard drops any other value
// rather than writing a marker the next parse would read as title text, and
// the card editor asks this to refuse the value while the user can still fix
// it.
func IsValidDue(due string) bool { return isRealDate(due) }

// LooksLikeBoard reports whether the body of a `kanban` fence is worth
// drawing as a board: it has at least one `## ` line. A board with no cards is
// still a board.
func LooksLikeBoard(content string) bool {
	for _, line := range strings.Split(content, "\n") {
		if strings.HasPrefix(line, "## ") {
			return true
		}
	}
	return false
}

// Parse reads the body of a `kanban` fence (KanbanData::parse).
func Parse(content string) *Board {
	board := &Board{}
	// Splitting "" gives one empty line, which would come back out as an
	// extra newline as soon as anything is appended to an empty board.
	var lines []string
	if content != "" {
		lines = strings.Split(content, "\n")
	}
	// Indices rather than pointers, because appending to a slice can move it.
	colIdx := -1      // the current column
	descIdx := -1     // the card still collecting description lines
	lastCardIdx := -1 // the last card seen, which trivia after it belongs to

	// Every line the board model does not represent is kept verbatim, with
	// the last thing before it, so that Serialize puts it back where it was.
	keepTrivia := func(raw string) {
		switch {
		case colIdx < 0:
			board.Preamble = append(board.Preamble, raw)
		case lastCardIdx < 0:
			col := &board.Columns[colIdx]
			col.LeadingTrivia = append(col.LeadingTrivia, raw)
		default:
			card := &board.Columns[colIdx].Cards[lastCardIdx]
			card.TrailingTrivia = append(card.TrailingTrivia, raw)
		}
	}

	// A blank line between two description lines is part of the description,
	// while a blank line after the last one separates the card from what
	// follows. Only the next line tells the two apart, so blank lines are held
	// here until an indented line takes them into the description or another
	// line sends them to trivia.
	var pendingBlanks []string
	flushBlanksToTrivia := func() {
		for _, blank := range pendingBlanks {
			keepTrivia(blank)
		}
		pendingBlanks = nil
	}

	for _, raw := range lines {
		if descIdx >= 0 && isBlank(raw) {
			pendingBlanks = append(pendingBlanks, raw)
			continue
		}
		if strings.HasPrefix(raw, "## ") {
			flushBlanksToTrivia()
			board.Columns = append(board.Columns, Column{
				Name:      trimmed(raw[3:]),
				RawHeader: raw,
			})
			colIdx = len(board.Columns) - 1
			descIdx = -1
			lastCardIdx = -1
			continue
		}
		if colIdx < 0 {
			flushBlanksToTrivia()
			keepTrivia(raw)
			continue
		}
		cm := cardRe.FindStringSubmatch(trimmed(raw))
		// A card line has no indent; an indented line is a description.
		indented := strings.HasPrefix(raw, "  ") || strings.HasPrefix(raw, "\t")
		if cm != nil && !indented {
			flushBlanksToTrivia()
			c := Card{Done: cm[1] != " ", RawLine: raw}
			// The dates come off the end of the line first, so only what the
			// reader typed is read as title, labels and due date.
			body := cm[2]
			if sm := stampRe.FindStringSubmatchIndex(body); sm != nil {
				fields := body[sm[2]:sm[3]]
				c.Created = stampValue(fields, "created")
				c.Modified = stampValue(fields, "modified")
				if c.Modified == "" {
					c.Modified = c.Created
				}
				c.StampExtras = stampExtraFields(fields)
				body = body[:sm[0]]
			}
			parseCardBody(body, &c)
			col := &board.Columns[colIdx]
			col.Cards = append(col.Cards, c)
			descIdx = len(col.Cards) - 1
			lastCardIdx = descIdx
			continue
		}
		if indented && descIdx >= 0 {
			c := &board.Columns[colIdx].Cards[descIdx]
			if len(pendingBlanks) > 0 && len(c.RawDescription) == 0 {
				// A blank line between the card and this line means this is
				// not the card's description, which starts on the line after
				// the card. Keeping it as trivia also keeps the blank line
				// where it was written: trivia is written after the
				// description, so taking this line in would move the blank
				// line past it.
				flushBlanksToTrivia()
				descIdx = -1
				keepTrivia(raw)
				continue
			}
			// The blank lines held back belong to this description.
			for _, blank := range pendingBlanks {
				c.Description += "\n"
				c.RawDescription = append(c.RawDescription, blank)
			}
			pendingBlanks = nil
			if len(c.RawDescription) == 0 {
				c.DescriptionIndent = leadingIndent(raw)
			}
			// Only the description's own indent comes off, so whatever is
			// nested inside it (a list, an indented quotation) keeps its
			// shape.
			text := raw
			if c.DescriptionIndent != "" && strings.HasPrefix(text, c.DescriptionIndent) {
				text = text[len(c.DescriptionIndent):]
			} else {
				text = trimmed(text)
			}
			if c.Description == "" {
				c.Description = text
			} else {
				c.Description += "\n" + text
			}
			c.RawDescription = append(c.RawDescription, raw)
			continue
		}
		// Any other line ends the current card's description and is kept as
		// trivia.
		flushBlanksToTrivia()
		descIdx = -1
		keepTrivia(raw)
	}
	flushBlanksToTrivia()
	return board
}

// Serialize writes the board back as the body of a `kanban` fence
// (KanbanData::serialize). A card, column or description with a source line
// is written as that line, so what the model does not record (spacing, `*`
// bullets, label order) is only rewritten on the lines a mutation changed.
func (b *Board) Serialize() string {
	var out []string
	out = append(out, b.Preamble...)
	for ci := range b.Columns {
		col := &b.Columns[ci]
		if col.RawHeader == "" {
			out = append(out, "## "+col.Name)
		} else {
			out = append(out, col.RawHeader)
		}
		out = append(out, col.LeadingTrivia...)
		for i := range col.Cards {
			card := &col.Cards[i]
			if card.RawLine != "" {
				out = append(out, card.RawLine)
			} else {
				box := "- [ ] "
				if card.Done {
					box = "- [x] "
				}
				out = append(out, box+renderedCardBody(card)+stampComment(card))
			}
			if len(card.RawDescription) > 0 {
				out = append(out, card.RawDescription...)
			} else if card.Description != "" {
				// The indent the description was read with, so a description
				// rewritten from the field goes where the old one was; two
				// spaces for one the editor created.
				indent := card.DescriptionIndent
				if indent == "" {
					indent = "  "
				}
				for _, d := range strings.Split(card.Description, "\n") {
					if d == "" {
						out = append(out, "")
					} else {
						out = append(out, indent+d)
					}
				}
			}
			out = append(out, card.TrailingTrivia...)
		}
	}
	return strings.Join(out, "\n")
}
