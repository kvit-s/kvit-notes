package links

// Finding [[wiki links]] in Markdown, from the app's
// src/content/wikilinkscanner.cpp. The editor's inline parser and the
// vault-wide scan use the same grammar there, so a link the editor draws is
// a link the backlinks pane counts.

// Link is one [[wiki link]] in a text. Every position is a rune offset into
// the text; a part the link does not have has a start of -1.
type Link struct {
	// Start and Length cover the whole link, brackets included.
	Start, Length int
	// TargetStart and TargetLength cover everything between "[[" and the
	// "|" or "]]": the note, and the "#" and heading if there is one.
	TargetStart, TargetLength int
	// NoteStart and NoteLength cover the note part with the spaces around
	// it left out. It is empty, but still placed, for [[#heading]].
	NoteStart, NoteLength int
	// HeadingStart and HeadingLength cover the heading after "#", spaces
	// left out.
	HeadingStart, HeadingLength int
	// AliasStart and AliasLength cover the text after "|", spaces left
	// out.
	AliasStart, AliasLength int
	// Target is the target part with the spaces around it removed:
	// "note#heading", "note", or "#heading".
	Target string
	// Note is the note part, "" for [[#heading]], which links to a heading
	// of the note it is in.
	Note string
	// Heading and Alias are the parts after "#" and "|", "" when absent.
	Heading, Alias string
}

// isEscapedAt reports whether the character at pos follows an odd number of
// backslashes.
func isEscapedAt(text []rune, pos int) bool {
	n := 0
	for i := pos - 1; i >= 0 && text[i] == '\\'; i-- {
		n++
	}
	return n&1 != 0
}

// runLength is how many times ch repeats from pos.
func runLength(text []rune, pos int, ch rune) int {
	end := pos
	for end < len(text) && text[end] == ch {
		end++
	}
	return end - pos
}

// lineEnd is the position after the newline ending the line holding pos,
// or the end of the text.
func lineEnd(text []rune, pos int) int {
	for i := pos; i < len(text); i++ {
		if text[i] == '\n' {
			return i + 1
		}
	}
	return len(text)
}

// indexOf finds the first ch at or after from, or -1.
func indexOf(text []rune, ch rune, from int) int {
	for i := max(from, 0); i < len(text); i++ {
		if text[i] == ch {
			return i
		}
	}
	return -1
}

// inlineMathEnd is the end of a $math$ span opening at pos, or -1, by the
// Pandoc rule the editor uses: no space after the opening dollar, none
// before the closing one, no digit straight after it, and all on one line.
// The first unescaped dollar decides.
func inlineMathEnd(text []rune, pos int) int {
	start := pos + 1
	if start >= len(text) || isSpace(text[start]) || text[start] == '$' {
		return -1
	}
	for i := start; i < len(text); i++ {
		if text[i] == '\n' {
			return -1
		}
		if text[i] != '$' || isEscapedAt(text, i) {
			continue
		}
		spaceBefore := isSpace(text[i-1])
		digitAfter := i+1 < len(text) && isDigit(text[i+1])
		if !spaceBefore && !digitAfter && i > start {
			return i + 1
		}
		return -1
	}
	return -1
}

// displayMathEnd is the end of a $$math$$ span opening at pos, or -1. It
// may run over several lines.
func displayMathEnd(text []rune, pos int) int {
	for i := pos + 2; i+1 < len(text); i++ {
		if text[i] == '$' && text[i+1] == '$' && !isEscapedAt(text, i) {
			return i + 2
		}
	}
	return -1
}

// trimRange narrows [begin, end) of text past the spaces at both ends.
func trimRange(text []rune, begin, end int) (int, int) {
	for begin < end && isSpace(text[begin]) {
		begin++
	}
	for end > begin && isSpace(text[end-1]) {
		end--
	}
	return begin, end
}

// MatchAt reports whether a wiki link starts at pos, and what it holds.
// The grammar is [[note]], [[note|alias]], [[note#heading]],
// [[note#heading|alias]] and [[#heading]]: nothing between the brackets may
// be a bracket or a newline, there is at most one "|" and one "#", and the
// alias and heading may not be blank. A "[[" after an odd number of
// backslashes is not a link. MatchAt does not know about code and math;
// Scan does.
func MatchAt(text []rune, pos int) (Link, bool) {
	if pos < 0 || pos+1 >= len(text) || text[pos] != '[' || text[pos+1] != '[' || isEscapedAt(text, pos) {
		return Link{}, false
	}
	close := -1
	for i := pos + 2; i+1 < len(text); i++ {
		if text[i] == ']' && text[i+1] == ']' {
			close = i
			break
		}
	}
	if close < 0 {
		return Link{}, false
	}
	inner := text[pos+2 : close]
	if len(inner) == 0 {
		return Link{}, false
	}
	pipe := -1
	for i, r := range inner {
		switch r {
		case '\n', '[', ']':
			return Link{}, false
		case '|':
			if pipe < 0 {
				pipe = i
			}
		}
	}
	targetEnd := len(inner)
	if pipe >= 0 {
		targetEnd = pipe
		alias := inner[pipe+1:]
		if b, e := trimRange(alias, 0, len(alias)); b == e || indexOf(alias, '|', 0) >= 0 {
			return Link{}, false
		}
	}
	hash := indexOf(inner[:targetEnd], '#', 0)
	noteEnd := targetEnd
	if hash >= 0 {
		noteEnd = hash
		heading := inner[hash+1 : targetEnd]
		if indexOf(heading, '#', 0) >= 0 {
			return Link{}, false
		}
		if b, e := trimRange(heading, 0, len(heading)); b == e {
			return Link{}, false
		}
	} else if b, e := trimRange(inner, 0, noteEnd); b == e {
		return Link{}, false
	}

	base := pos + 2
	l := Link{
		Start:        pos,
		Length:       close + 2 - pos,
		TargetStart:  base,
		TargetLength: targetEnd,
		HeadingStart: -1,
		AliasStart:   -1,
	}
	b, e := trimRange(inner, 0, targetEnd)
	l.Target = string(inner[b:e])
	b, e = trimRange(inner, 0, noteEnd)
	l.NoteStart, l.NoteLength, l.Note = base+b, e-b, string(inner[b:e])
	if hash >= 0 {
		b, e = trimRange(inner, hash+1, targetEnd)
		l.HeadingStart, l.HeadingLength, l.Heading = base+b, e-b, string(inner[b:e])
	}
	if pipe >= 0 {
		b, e = trimRange(inner, pipe+1, len(inner))
		l.AliasStart, l.AliasLength, l.Alias = base+b, e-b, string(inner[b:e])
	}
	return l, true
}

// Scan finds every wiki link in a note's Markdown, in order, leaving out
// those the editor shows verbatim: fenced code (``` or ~~~, three or more,
// closed by a run of the same character at least as long with nothing after
// it but spaces), inline code of any number of backticks, $inline math$,
// $$display math$$, and a "[[" after an odd number of backslashes. An inline
// code span that never closes hides only its backticks.
func Scan(text string) []Link {
	rs := []rune(text)
	var out []Link
	pos, fenceLength := 0, 0
	var fenceChar rune
	for pos < len(rs) {
		if pos == 0 || rs[pos-1] == '\n' {
			marker := pos
			for marker < len(rs) && rs[marker] != '\n' && isSpace(rs[marker]) {
				marker++
			}
			var ch rune
			if marker < len(rs) {
				ch = rs[marker]
			}
			run := 0
			if ch == '`' || ch == '~' {
				run = runLength(rs, marker, ch)
			}
			if fenceLength > 0 {
				if ch == fenceChar && run >= fenceLength {
					rest := marker + run
					for rest < len(rs) && rs[rest] != '\n' && isSpace(rs[rest]) {
						rest++
					}
					if rest == len(rs) || rs[rest] == '\n' {
						fenceLength, fenceChar = 0, 0
					}
				}
				pos = lineEnd(rs, pos)
				continue
			}
			if run >= 3 {
				fenceLength, fenceChar = run, ch
				pos = lineEnd(rs, pos)
				continue
			}
		}

		if rs[pos] == '`' && !isEscapedAt(rs, pos) {
			ticks := runLength(rs, pos, '`')
			close := -1
			for c := pos + ticks; c < len(rs); {
				c = indexOf(rs, '`', c)
				if c < 0 {
					break
				}
				run := runLength(rs, c, '`')
				if run == ticks {
					close = c
					break
				}
				c += run
			}
			if close >= 0 {
				pos = close + ticks
			} else {
				pos += ticks
			}
			continue
		}

		if rs[pos] == '$' && !isEscapedAt(rs, pos) {
			var end int
			if pos+1 < len(rs) && rs[pos+1] == '$' {
				end = displayMathEnd(rs, pos)
			} else {
				end = inlineMathEnd(rs, pos)
			}
			if end >= 0 {
				pos = end
				continue
			}
		}

		if l, ok := MatchAt(rs, pos); ok {
			out = append(out, l)
			pos += l.Length
			continue
		}
		pos++
	}
	return out
}

// Targets is the outgoing links of a note body, in order and with
// repeats: each link's Target, heading kept and alias dropped. A
// [[#heading]] link stays inside its note and is left out. This is the
// list the app keeps for every note (WikiLinkIndex::extractLinks in
// src/repository/wikilinkindex.cpp).
func Targets(body string) []string {
	var out []string
	for _, l := range Scan(body) {
		if l.Note != "" {
			out = append(out, l.Target)
		}
	}
	return out
}
