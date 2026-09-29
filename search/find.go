package search

// Finding in one note: the find bar (features.md 7.1). The rules are
// src/domain/documentsearch.cpp's (DocumentSearch::recompute, scanText and
// compiledPattern). A note is searched as the text of each block as the
// reader sees it, so a query matches across the markers of a span ("is bold"
// in "This is **bold** text") and never matches a marker; a code block's
// text is its source, so its asterisks match. A divider has no text.

import (
	"regexp"
	"sort"
	"strings"
	"unicode/utf8"
)

// Options are the find bar's toggles.
type Options struct {
	// CaseSensitive compares letters exactly; otherwise by their simple case
	// folding, as the ::CaseInsensitive does.
	CaseSensitive bool
	// WholeWord accepts a match only where a word starts and where one ends,
	// by the rule of the regular expression \b: the characters either side
	// of each end of the match differ in whether they are word characters.
	// Those are letters and digits of any script and the underscore; with
	// Regex the pattern is wrapped in \b(?:...)\b, whose word characters are
	// the ASCII ones, as in .
	WholeWord bool
	// Regex takes the query as a regular expression. The syntax is Go's
	// (RE2), which is the PCRE2 without backreferences and lookaround; a
	// query that uses those does not compile. As in , . does not match a
	// line break; unlike , $ matches only at the end of a block's text,
	// not also before a line break that ends it.
	Regex bool
	// PreserveCase gives a replacement the case of the text it replaces. It
	// changes what replacements are, never what matches.
	PreserveCase bool
	// Within limits the matches to part of the note: the find bar's "in
	// selection" toggle. Nil is the whole note.
	Within Domain
}

// Match is one match: a range of the display text of one block.
type Match struct {
	Block  int // the block's index in the note
	Start  int // the first rune of the match
	Length int // how many runes it covers, at least one
	// Captures are a regular expression's captured texts: the whole match,
	// then each group, empty for a group that took no part. Nil for a plain
	// query.
	Captures []string
}

// Pattern is a query compiled with its options, ready to scan many blocks.
// The find bar compiles once per change of query or option
// (DocumentSearch::recompute), not once per block.
type Pattern struct {
	opts   Options
	re     *regexp.Regexp
	needle string // the query, folded unless the search is case-sensitive
	length int    // the query's length in runes
}

// Compile compiles a query. The error is not nil only for a regular
// expression that does not compile, which the find bar shows as an error
// state with no matches (DocumentSearch::patternError). An empty query
// compiles and finds nothing.
func Compile(query string, opts Options) (*Pattern, error) {
	p := &Pattern{opts: opts, length: utf8.RuneCountInString(query)}
	if query == "" {
		return p, nil
	}
	if opts.Regex {
		pattern := query
		if opts.WholeWord {
			pattern = `\b(?:` + query + `)\b`
		}
		if !opts.CaseSensitive {
			pattern = `(?i)` + pattern
		}
		re, err := regexp.Compile(pattern)
		if err != nil {
			return nil, err
		}
		p.re = re
		return p, nil
	}
	p.needle = query
	if !opts.CaseSensitive {
		p.needle = foldString(query)
	}
	return p, nil
}

// Find compiles a query and finds it in a note's blocks, given as the text
// of each block as the reader sees it (Block.Text).
func Find(texts []string, query string, opts Options) ([]Match, error) {
	p, err := Compile(query, opts)
	if err != nil {
		return nil, err
	}
	return p.Find(texts), nil
}

// Find finds the pattern in a note's blocks, in document order, keeping the
// matches inside the options' domain.
func (p *Pattern) Find(texts []string) []Match {
	var out []Match
	for i, text := range texts {
		n := len(out)
		out = p.scan(text, i, out)
		if p.opts.Within != nil {
			kept := out[:n]
			for _, m := range out[n:] {
				if p.opts.Within.contains(m) {
					kept = append(kept, m)
				}
			}
			out = kept
		}
	}
	return out
}

// Scan finds the pattern in one block's text, ignoring the domain. The
// matches have block 0.
func (p *Pattern) Scan(text string) []Match { return p.scan(text, 0, nil) }

// scan appends one block's matches to out. Matches never overlap, and a
// regular expression's empty matches are skipped, since a match must cover
// at least one character for stepping and replacing to move on.
func (p *Pattern) scan(text string, block int, out []Match) []Match {
	if text == "" || p.length == 0 {
		return out
	}
	if p.re != nil {
		runes, bytes := 0, 0
		for _, loc := range p.re.FindAllStringSubmatchIndex(text, -1) {
			if loc[1] == loc[0] {
				continue
			}
			runes += utf8.RuneCountInString(text[bytes:loc[0]])
			bytes = loc[0]
			caps := make([]string, len(loc)/2)
			for g := range caps {
				if loc[2*g] >= 0 {
					caps[g] = text[loc[2*g]:loc[2*g+1]]
				}
			}
			out = append(out, Match{Block: block, Start: runes,
				Length: utf8.RuneCountInString(caps[0]), Captures: caps})
		}
		return out
	}

	hay := text
	if !p.opts.CaseSensitive {
		hay = foldString(text)
	}
	runes, bytes := 0, 0
	for from := 0; from < len(hay); {
		i := strings.Index(hay[from:], p.needle)
		if i < 0 {
			break
		}
		at := from + i
		end := at + len(p.needle)
		if p.opts.WholeWord && !wordEdges(hay, at, end) {
			_, n := utf8.DecodeRuneInString(hay[at:])
			from = at + n
			continue
		}
		runes += utf8.RuneCountInString(hay[bytes:at])
		bytes = at
		out = append(out, Match{Block: block, Start: runes, Length: p.length})
		from = end
	}
	return out
}

// wordEdges reports whether hay[at:end] starts and ends on word boundaries
// by \b's rule: at each edge, the characters on the two sides differ in
// whether they are word characters (beyond the text counts as not).
func wordEdges(hay string, at, end int) bool {
	before, after := false, false
	if at > 0 {
		r, _ := utf8.DecodeLastRuneInString(hay[:at])
		before = isWordChar(r)
	}
	if end < len(hay) {
		r, _ := utf8.DecodeRuneInString(hay[end:])
		after = isWordChar(r)
	}
	first, _ := utf8.DecodeRuneInString(hay[at:end])
	last, _ := utf8.DecodeLastRuneInString(hay[at:end])
	return before != isWordChar(first) && isWordChar(last) != after
}

// Domain is the part of a note a search is kept inside: the blocks or the
// text that were selected when the find bar's "in selection" toggle was
// armed (DocumentSearch::setBlockDomain and setTextDomain). The app
// remembers the blocks by their ids so the domain survives blocks moving;
// here the caller passes the blocks' current indexes.
type Domain interface {
	contains(m Match) bool
}

type blockDomain map[int]bool

func (d blockDomain) contains(m Match) bool { return d[m.Block] }

// InBlocks keeps matches inside the given blocks. With no blocks it is no
// limit.
func InBlocks(blocks ...int) Domain {
	if len(blocks) == 0 {
		return nil
	}
	d := blockDomain{}
	for _, b := range blocks {
		d[b] = true
	}
	return d
}

type textDomain struct{ startBlock, startPos, endBlock, endPos int }

func (d textDomain) contains(m Match) bool {
	switch {
	case m.Block < d.startBlock || m.Block > d.endBlock:
		return false
	case m.Block == d.startBlock && m.Start < d.startPos:
		return false
	case m.Block == d.endBlock && m.Start+m.Length > d.endPos:
		return false
	}
	return true
}

// InText keeps matches that lie entirely inside a range of text, from a
// display offset in one block to a display offset in another. The ends may
// come in either order. A negative block is no limit.
func InText(startBlock, startPos, endBlock, endPos int) Domain {
	if startBlock < 0 || endBlock < 0 {
		return nil
	}
	if startBlock > endBlock || (startBlock == endBlock && startPos > endPos) {
		startBlock, endBlock = endBlock, startBlock
		startPos, endPos = endPos, startPos
	}
	return textDomain{startBlock, startPos, endBlock, endPos}
}

// Nearest is the index of the match the find bar makes current when the
// query or an option changes: the first match at or after the caret, or
// the note's first match when none follows it (DocumentSearch::recompute).
// The caret is a block and a display offset; a block of -1 means there is no
// caret. It is -1 when there are no matches. After a replacement the app
// puts the caret just after the replacement, which is how it moves on to
// the next match.
func Nearest(matches []Match, block, pos int) int {
	if len(matches) == 0 {
		return -1
	}
	i := sort.Search(len(matches), func(i int) bool {
		m := matches[i]
		return m.Block > block || (m.Block == block && m.Start >= pos)
	})
	if i == len(matches) {
		return 0
	}
	return i
}

// Next is the match after current, wrapping from the last to the first
// (DocumentSearch::next). With no current match (-1) it is the first. It is
// -1 when there are no matches.
func Next(current, count int) int {
	switch {
	case count <= 0:
		return -1
	case current < 0:
		return 0
	}
	return (current + 1) % count
}

// Previous is the match before current, wrapping from the first to the last
// (DocumentSearch::previous). With no current match (-1) it is the last.
func Previous(current, count int) int {
	switch {
	case count <= 0:
		return -1
	case current < 0:
		return count - 1
	}
	return (current - 1 + count) % count
}
