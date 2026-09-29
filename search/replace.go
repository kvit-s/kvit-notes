package search

// Replacing in one note: the find bar's replace field (features.md 7.2). The
// rules are src/domain/documentsearch.cpp's (finalReplacement,
// substituteCaptures, applyPreserveCase, replaceCurrent, replaceAll and
// previewReplacements). The app applies a replacement as one undo step;
// here the functions compute the new Markdown and the caller applies it.

import (
	"strings"
	"unicode"
	"unicode/utf8"
)

// Replacement is the text one match is replaced with
// (DocumentSearch::finalReplacement): for a regular expression, $1 to $9,
// $& and $$ are substituted from the match's captures; with PreserveCase the
// result takes the case of the matched text. text is the display text of the
// match's block.
func Replacement(replacement string, m Match, text string, opts Options) string {
	out := replacement
	if opts.Regex {
		out = SubstituteCaptures(replacement, m.Captures)
	}
	if opts.PreserveCase {
		var matched string
		if opts.Regex && len(m.Captures) > 0 {
			matched = m.Captures[0]
		} else {
			matched = runeSlice(text, m.Start, m.Start+m.Length)
		}
		out = PreserveCase(out, matched)
	}
	return out
}

// SubstituteCaptures fills a regular-expression replacement from a match's
// captures (DocumentSearch::substituteCaptures): $1 to $9 are groups, $& is
// the whole match, $$ is a dollar sign, and anything else is literal ($0
// included). A group the match does not have is empty. The digit is any
// decimal digit the QChar::isDigit knows, so a zero in another script
// names the whole match, as in the app.
func SubstituteCaptures(replacement string, captures []string) string {
	capture := func(i int) string {
		if i < len(captures) {
			return captures[i]
		}
		return ""
	}
	var b strings.Builder
	rs := []rune(replacement)
	for i := 0; i < len(rs); i++ {
		c := rs[i]
		if c != '$' || i+1 >= len(rs) {
			b.WriteRune(c)
			continue
		}
		switch next := rs[i+1]; {
		case next == '$':
			b.WriteByte('$')
			i++
		case next == '&':
			b.WriteString(capture(0))
			i++
		case next != '0' && isDigit(next):
			b.WriteString(capture(digitValue(next)))
			i++
		default:
			b.WriteRune(c)
		}
	}
	return b.String()
}

// PreserveCase gives a replacement the case of the text it replaces
// (DocumentSearch::applyPreserveCase): an all-capitals match of more than
// one character makes it capitals, an all-lower-case match makes it lower
// case, a match with a capital first and no other capital makes it
// capitalised, and a match with mixed case or no letters leaves it as
// typed. Characters are classified one UTF-16 code unit at a time, as in
// , so a letter beyond the Basic Multilingual Plane counts as no letter.
// Changing case uses Go's per-character mappings, where the string uses the
// full ones: "ß" stays "ß" in capitals here and becomes "SS" in the app.
func PreserveCase(replacement, matched string) string {
	hasLetter, allUpper, allLower := false, true, true
	for _, c := range matched {
		if !isLetter(c) {
			continue
		}
		hasLetter = true
		if isLower(c) {
			allUpper = false
		}
		if isUpper(c) {
			allLower = false
		}
	}
	switch {
	case !hasLetter:
		return replacement
	case allUpper && utf8.RuneCountInString(matched) > 1:
		return strings.ToUpper(replacement)
	case allLower:
		return strings.ToLower(replacement)
	}
	first, n := utf8.DecodeRuneInString(matched)
	capitalised := isUpper(first)
	for _, c := range matched[n:] {
		if capitalised && isLetter(c) && isUpper(c) {
			capitalised = false
		}
	}
	if capitalised && replacement != "" {
		out := strings.ToLower(replacement)
		r, n := utf8.DecodeRuneInString(out)
		if r <= 0xFFFF {
			return string(unicode.ToUpper(r)) + out[n:]
		}
		return out
	}
	return replacement
}

// ReplaceOne replaces one match (DocumentSearch::replaceCurrent), as
// selecting it and typing the replacement would (Block.Replace). It returns
// the block's new Markdown and the Markdown offset just after the
// replacement. The app then searches again with the caret there, which
// makes the next remaining match current: parse the new Markdown's spans,
// map the offset with Block.DisplayPos, and pass it to Nearest.
func ReplaceOne(b Block, m Match, replacement string, opts Options) (markdown string, after int) {
	r := Replacement(replacement, m, b.Text(), opts)
	return b.Replace(m.Start, m.Start+m.Length, r)
}

// Edit is one block's Markdown after a replacement.
type Edit struct {
	Block    int
	Markdown string
}

// ReplaceAll replaces every match (DocumentSearch::replaceAll): block by
// block in document order, and inside a block from the last match to the
// first, so the display offsets of the matches still to be replaced stay
// where they were. The matches are the ones Find returned for these blocks.
// Between two replacements in one block the Markdown has changed, so its
// spans are found again with parse, the editor's parser (the app parses
// again at every replacement too). It returns one edit per block that
// changed, and how many matches were replaced.
func ReplaceAll(blocks []Block, matches []Match, replacement string, opts Options,
	parse func(markdown string) []Span) (edits []Edit, replaced int) {
	for i := 0; i < len(matches); {
		j := i
		for j < len(matches) && matches[j].Block == matches[i].Block {
			j++
		}
		index := matches[i].Block
		if index < 0 || index >= len(blocks) {
			i = j
			continue
		}
		b := blocks[index]
		text := b.Text()
		for k := j - 1; k >= i; k-- {
			if k < j-1 && !b.Verbatim {
				b.Spans = parse(b.Markdown)
			}
			m := matches[k]
			b.Markdown, _ = b.Replace(m.Start, m.Start+m.Length, Replacement(replacement, m, text, opts))
			replaced++
		}
		edits = append(edits, Edit{Block: index, Markdown: b.Markdown})
		i = j
	}
	return edits, replaced
}

// PreviewRow is one line of the list shown before replacing everything
// (DocumentSearch::previewReplacements): the match with up to 30 characters
// either side of it, cut at the ends of its line, with "…" where more text
// was cut off.
type PreviewRow struct {
	Block       int
	Prefix      string
	Matched     string
	Replacement string
	Suffix      string
}

// Preview lists what replacing every match would do, from the same matches
// and texts that Find used and ReplaceAll will use.
func Preview(texts []string, matches []Match, replacement string, opts Options) []PreviewRow {
	const context = 30
	rows := make([]PreviewRow, 0, len(matches))
	for _, m := range matches {
		text := ""
		if m.Block >= 0 && m.Block < len(texts) {
			text = texts[m.Block]
		}
		rs := []rune(text)
		start := min(m.Start, len(rs))
		end := min(m.Start+m.Length, len(rs))
		lineStart := 0
		for i := start - 1; i >= 0; i-- {
			if rs[i] == '\n' {
				lineStart = i + 1
				break
			}
		}
		lineEnd := len(rs)
		for i := end; i < len(rs); i++ {
			if rs[i] == '\n' {
				lineEnd = i
				break
			}
		}
		prefixFrom := max(lineStart, start-context)
		prefix := string(rs[prefixFrom:start])
		if prefixFrom > lineStart {
			prefix = "…" + prefix
		}
		suffixTo := min(lineEnd, end+context)
		suffix := string(rs[end:suffixTo])
		if suffixTo < lineEnd {
			suffix += "…"
		}
		rows = append(rows, PreviewRow{
			Block:       m.Block,
			Prefix:      prefix,
			Matched:     string(rs[start:end]),
			Replacement: Replacement(replacement, m, text, opts),
			Suffix:      suffix,
		})
	}
	return rows
}

// runeSlice is s from rune a to rune b, clamped to s.
func runeSlice(s string, a, b int) string {
	rs := []rune(s)
	a = max(0, min(a, len(rs)))
	b = max(a, min(b, len(rs)))
	return string(rs[a:b])
}

// isLetter, isLower, isUpper and isDigit are QChar's, asked of one UTF-16
// code unit: a character beyond the Basic Multilingual Plane is a surrogate
// pair to , and a surrogate is none of them.
func isLetter(r rune) bool { return r <= 0xFFFF && unicode.IsLetter(r) }
func isLower(r rune) bool  { return r <= 0xFFFF && unicode.Is(unicode.Ll, r) }
func isUpper(r rune) bool  { return r <= 0xFFFF && unicode.Is(unicode.Lu, r) }
func isDigit(r rune) bool  { return r <= 0xFFFF && unicode.Is(unicode.Nd, r) }

// digitValue is QChar::digitValue for a decimal digit. Unicode encodes each
// script's digits as a run from zero to nine.
func digitValue(r rune) int {
	for _, rg := range unicode.Nd.R16 {
		if lo, hi := rune(rg.Lo), rune(rg.Hi); lo <= r && r <= hi {
			return int(r-lo) % 10
		}
	}
	return 0
}
