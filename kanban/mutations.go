package kanban

import (
	"slices"
	"strings"
)

// The mutations below are the KanbanData functions of the same names. Each
// takes the whole body of a `kanban` fence, parses it, changes the board and
// returns the whole new body; lines the change does not touch come back byte
// for byte. An index out of range returns content unchanged.
//
// A mutation that changes a card takes today, the day it happens on, as an
// ISO `YYYY-MM-DD`. The card's created= day is set when the card is added,
// and its modified= day whenever its text, its fields, its done state or the
// column holding it change. An empty or unreal today records nothing.

// cardInRange reports whether col and index name a card of b.
func cardInRange(b *Board, col, index int) bool {
	return col >= 0 && col < len(b.Columns) &&
		index >= 0 && index < len(b.Columns[col].Cards)
}

// AddColumn appends a column named name.
func AddColumn(content, name string) string {
	b := Parse(content)
	c := Column{Name: name}
	c.LeadingTrivia = takeTrailingBlanks(lastTriviaSlot(b))
	b.Columns = append(b.Columns, c)
	return b.Serialize()
}

// RenameColumn renames column col, rewriting its `## ` line.
func RenameColumn(content string, col int, name string) string {
	b := Parse(content)
	if col < 0 || col >= len(b.Columns) {
		return content
	}
	b.Columns[col].Name = name
	b.Columns[col].RawHeader = "" // the header line is what changed
	return b.Serialize()
}

// RemoveColumn removes column col with its header and cards. The trivia
// lines inside it move to the position before the column rather than being
// deleted.
func RemoveColumn(content string, col int) string {
	b := Parse(content)
	if col < 0 || col >= len(b.Columns) {
		return content
	}
	orphaned := slices.Clone(b.Columns[col].LeadingTrivia)
	for _, card := range b.Columns[col].Cards {
		orphaned = append(orphaned, card.TrailingTrivia...)
	}
	b.Columns = slices.Delete(b.Columns, col, col+1)
	if len(orphaned) > 0 {
		slot := triviaSlotBeforeColumn(b, col)
		*slot = append(*slot, orphaned...)
	}
	return b.Serialize()
}

// MoveColumn moves column fromCol to position toCol (QList::move).
func MoveColumn(content string, fromCol, toCol int) string {
	b := Parse(content)
	if fromCol < 0 || fromCol >= len(b.Columns) || toCol < 0 || toCol >= len(b.Columns) {
		return content
	}
	c := b.Columns[fromCol]
	b.Columns = slices.Delete(b.Columns, fromCol, fromCol+1)
	b.Columns = slices.Insert(b.Columns, toCol, c)
	return b.Serialize()
}

// AddCard appends a card titled title to column col, stamped as created on
// today.
func AddCard(content string, col int, title, today string) string {
	b := Parse(content)
	if col < 0 || col >= len(b.Columns) {
		return content
	}
	c := &b.Columns[col]
	card := Card{Title: title}
	stampCard(&card, today, true)
	card.TrailingTrivia = takeTrailingBlanks(lastSlotOf(c))
	c.Cards = append(c.Cards, card)
	return b.Serialize()
}

// RemoveCard removes card index of column col. The trivia lines after it move
// to the position before it.
func RemoveCard(content string, col, index int) string {
	b := Parse(content)
	if !cardInRange(b, col, index) {
		return content
	}
	c := &b.Columns[col]
	orphaned := c.Cards[index].TrailingTrivia
	c.Cards = slices.Delete(c.Cards, index, index+1)
	if len(orphaned) > 0 {
		slot := triviaSlotBeforeCard(c, index)
		*slot = append(*slot, orphaned...)
	}
	return b.Serialize()
}

// ToggleCardDone checks or unchecks card index of column col. Only the
// character inside the checkbox changes, so the rest of the line (bullet,
// spacing, label order) stays as written.
func ToggleCardDone(content string, col, index int, today string) string {
	b := Parse(content)
	if !cardInRange(b, col, index) {
		return content
	}
	card := &b.Columns[col].Cards[index]
	card.Done = !card.Done
	if m := boxRe.FindStringSubmatchIndex(card.RawLine); m != nil {
		box := " "
		if card.Done {
			box = "x"
		}
		card.RawLine = card.RawLine[:m[4]] + box + card.RawLine[m[5]:]
	} else {
		card.RawLine = ""
	}
	stampCard(card, today, false)
	return b.Serialize()
}

// MoveCard moves card fromIndex of column fromCol so that it sits before the
// card at toIndex of column toCol, or at the end when toIndex is the card
// count; this is what a drag and drop does. toIndex counts the cards of toCol
// as they were before the move. The trivia lines after the card stay at the
// position the card left. A move to another column is stamped with today; a
// move within one column is not, because it says nothing new about the card.
func MoveCard(content string, fromCol, fromIndex, toCol, toIndex int, today string) string {
	b := Parse(content)
	if fromCol < 0 || fromCol >= len(b.Columns) || toCol < 0 || toCol >= len(b.Columns) ||
		fromIndex < 0 || fromIndex >= len(b.Columns[fromCol].Cards) {
		return content
	}
	from := &b.Columns[fromCol]
	card := from.Cards[fromIndex]
	from.Cards = slices.Delete(from.Cards, fromIndex, fromIndex+1)
	orphaned := card.TrailingTrivia
	card.TrailingTrivia = nil
	if len(orphaned) > 0 {
		slot := triviaSlotBeforeCard(from, fromIndex)
		*slot = append(*slot, orphaned...)
	}
	// Removing the card first shifts every later position in the same column
	// down by one, so a move down within one column is adjusted; a move up or
	// to another column needs no adjustment.
	dest := toIndex
	if fromCol == toCol && fromIndex < toIndex {
		dest--
	}
	to := &b.Columns[toCol]
	dest = max(0, min(dest, len(to.Cards)))
	if fromCol != toCol {
		stampCard(&card, today, false)
	}
	to.Cards = slices.Insert(to.Cards, dest, card)
	return b.Serialize()
}

// SetCardLine replaces the text on the line of card index of column col (all
// of it after the checkbox) with text, character for character. This is what
// the board's inline editor writes: the reader edits the line's own source,
// such as `Ship the beta #release 📅 2026-08-01`, and the next parse turns
// `#release` back into a label and the marked day back into the due date.
// The indent, bullet and checkbox in front of the text are kept, and so is
// the dates comment after it. A line break in text becomes a space, because
// one card is one line.
func SetCardLine(content string, col, index int, text, today string) string {
	b := Parse(content)
	if !cardInRange(b, col, index) {
		return content
	}
	c := &b.Columns[col].Cards[index]
	// A line break would start a second card or a description line the next
	// time the board is read.
	body := strings.ReplaceAll(text, "\n", " ")
	// The dates were never part of what was typed, so they go back on the
	// end of the rebuilt line.
	c.RawLine = cardPrefix(c) + body + stampComment(c)
	// The title, labels and due date come from the line, so they are read
	// back out of the new text.
	c.Title = ""
	c.Labels = nil
	c.Due = ""
	parseCardBody(body, c)
	stampCard(c, today, false)
	return b.Serialize()
}

// SetCardDescription replaces the description of card index of column col
// with text. Each line of text is written indented under the card, so a
// description of several lines reads back as typed; an empty text removes the
// description.
func SetCardDescription(content string, col, index int, text, today string) string {
	b := Parse(content)
	if !cardInRange(b, col, index) {
		return content
	}
	c := &b.Columns[col].Cards[index]
	c.Description = text
	// The source lines of the old description are dropped and Serialize
	// writes the new one from the field.
	c.RawDescription = nil
	stampCard(c, today, false)
	return b.Serialize()
}

// SetCard overwrites every field of card index of column col; this is what
// the card-details editor, the label chips and the due-date picker write. The
// values are first reduced to what the stored form can hold, so the board in
// memory is the board the next parse reads: empty labels are dropped, and a
// due value IsValidDue refuses becomes no due date. The card's line and
// description are then written from the fields.
func SetCard(content string, col, index int, title string, done bool, labels []string, due, description, today string) string {
	b := Parse(content)
	if !cardInRange(b, col, index) {
		return content
	}
	c := &b.Columns[col].Cards[index]
	c.Title = title
	c.Done = done
	c.Labels = nil
	for _, label := range labels {
		if label != "" {
			c.Labels = append(c.Labels, label)
		}
	}
	c.Due = ""
	if IsValidDue(due) {
		c.Due = due
	}
	c.Description = description
	c.RawLine = ""
	c.RawDescription = nil
	stampCard(c, today, false)
	return b.Serialize()
}
