package editor

// Document statistics: words, characters with and without spaces, paragraphs,
// blocks and reading time, for the whole note or the selected text. They are
// counted in the text as the reader sees it: a formatted span's markers are
// left out, a code block's text is taken as written, and a divider has none.

import (
	"math"
	"strings"
	"unicode"
)

// Stats are counts of a note or a selection.
type Stats struct {
	Words, Chars, CharsNoSpaces, Paragraphs, Blocks, ReadingMinutes int
}

// countChars counts code points, leaving out zero-width joiners and
// variation selectors, which are invisible glue, and spaces when asked to.
func countChars(s string, withSpaces bool) int {
	n := 0
	for _, r := range s {
		if r == 0x200D || r >= 0xFE00 && r <= 0xFE0F {
			continue
		}
		if !withSpaces && unicode.IsSpace(r) {
			continue
		}
		n++
	}
	return n
}

// readingMinutes is the time to read a number of words at 200 a minute,
// rounded, and never rounding a real read to nothing.
func readingMinutes(words int) int {
	if words <= 0 {
		return 0
	}
	return max(1, int(math.Round(float64(words)/200)))
}

// statisticsText is a block's text as it is counted.
func statisticsText(b *Block) string {
	switch {
	case b.Kind == Divider:
		return ""
	case b.Kind.HasInline():
		return PlainText(b.Text)
	}
	return b.Text
}

// Stats counts the whole note: a paragraph is a block with a word in it.
func (d *Doc) Stats() Stats {
	var s Stats
	s.Blocks = len(d.Blocks)
	for i := range d.Blocks {
		t := statisticsText(&d.Blocks[i])
		w := len(strings.Fields(t))
		s.Words += w
		s.Chars += countChars(t, true)
		s.CharsNoSpaces += countChars(t, false)
		if w > 0 {
			s.Paragraphs++
		}
	}
	s.ReadingMinutes = readingMinutes(s.Words)
	return s
}

// StatsOfText counts text as the reader sees it: a paragraph, and a block,
// is a line with something on it.
func StatsOfText(text string) Stats {
	s := Stats{Words: len(strings.Fields(text)), Chars: countChars(text, true), CharsNoSpaces: countChars(text, false)}
	for _, line := range strings.Split(text, "\n") {
		if strings.TrimSpace(line) != "" {
			s.Paragraphs++
		}
	}
	s.Blocks = s.Paragraphs
	s.ReadingMinutes = readingMinutes(s.Words)
	return s
}

// SelectionStats counts the selected text, and reports false when nothing
// is selected.
func (d *Doc) SelectionStats() (Stats, bool) {
	if !d.HasSelection() {
		return Stats{}, false
	}
	from, to := d.SelRange()
	var parts []string
	for i := d.Index(from.Block); i >= 0 && i <= d.Index(to.Block) && i < len(d.Blocks); i++ {
		b := &d.Blocks[i]
		r := []rune(b.Text)
		a, c := 0, len(r)
		if b.ID == from.Block {
			a = from.Off
		}
		if b.ID == to.Block {
			c = to.Off
		}
		part := &Block{Kind: b.Kind, Text: string(r[min(a, len(r)):min(c, len(r))])}
		parts = append(parts, statisticsText(part))
	}
	return StatsOfText(strings.Join(parts, "\n")), true
}
