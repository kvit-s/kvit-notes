package editor

// Inline Markdown: finding the formatted spans in a block's source text, and
// projecting that source onto what is drawn.
//
// The block's Markdown source is the only text there is. The caret and the
// selection are offsets into it. What is drawn is the source with the markers
// of every span removed, except the spans the caret or selection touches,
// whose markers are drawn in a muted colour (features.md 2.2). Because a span
// touched by the caret always shows its markers, the caret never sits next to
// a hidden marker, so every caret offset has one place on screen.

import "unicode"

type spanKind int

const (
	sBold spanKind = iota
	sItalic
	sBoldItalic
	sStrike
	sHighlight
	sUnderline
	sCode
	sLink
	sWiki
)

// span is one formatted span in source rune offsets. [Start, CStart) and
// [CEnd, End) are its markers; [CStart, CEnd) is its content. For a link
// the closing marker is "](url)".
type span struct {
	Kind       spanKind
	Start, End int
	CStart     int
	CEnd       int
}

type delim struct {
	m    string
	kind spanKind
}

// Longest first, so "***" is tried before "**" and "*".
var delims = []delim{
	{"***", sBoldItalic}, {"**", sBold}, {"__", sBold}, {"~~", sStrike},
	{"==", sHighlight}, {"++", sUnderline}, {"*", sItalic}, {"_", sItalic},
}

type iparser struct {
	s   []rune
	out []span
}

// parseInline returns every span in src, outer spans before the spans nested
// in them.
func parseInline(src []rune) []span {
	p := iparser{s: src}
	p.parseRange(0, len(src))
	return p.out
}

func (p *iparser) parseRange(from, to int) {
	for i := from; i < to; {
		if end, ok := p.matchAt(i, to, "", true); ok {
			i = end
			continue
		}
		i++
	}
}

func (p *iparser) has(i int, m string) bool {
	rs := []rune(m)
	if i < 0 || i+len(rs) > len(p.s) {
		return false
	}
	for k, r := range rs {
		if p.s[i+k] != r {
			return false
		}
	}
	return true
}

func (p *iparser) at(i int) rune {
	if i < 0 || i >= len(p.s) {
		return 0
	}
	return p.s[i]
}

func isSpace(r rune) bool { return r == 0 || unicode.IsSpace(r) }
func isWord(r rune) bool  { return unicode.IsLetter(r) || unicode.IsDigit(r) }

// matchAt tries to start a span at i that closes before `to`. It returns the
// offset after the span. With record false it only measures, so the search
// for a closing marker can skip over nested spans without recording them.
// `closing` is the marker whose close is being searched for; a span of that
// same marker cannot open inside it.
func (p *iparser) matchAt(i, to int, closing string, record bool) (int, bool) {
	s := p.s
	switch {
	case s[i] == '\\':
		return min(i+2, to), true
	case s[i] == '`':
		n := 0
		for i+n < to && s[i+n] == '`' {
			n++
		}
		for j := i + n; j < to; j++ {
			if s[j] != '`' {
				continue
			}
			k := 0
			for j+k < to && s[j+k] == '`' {
				k++
			}
			if k == n {
				if record {
					p.out = append(p.out, span{sCode, i, j + k, i + n, j})
				}
				return j + k, true
			}
			j += k - 1
		}
		return 0, false
	case p.has(i, "[["):
		for j := i + 2; j+1 < to; j++ {
			if s[j] == '\n' {
				break
			}
			if s[j] == ']' && s[j+1] == ']' {
				if j == i+2 {
					break
				}
				if record {
					p.out = append(p.out, span{sWiki, i, j + 2, i + 2, j})
				}
				return j + 2, true
			}
		}
		return 0, false
	case s[i] == '[':
		// [text](url): the text may hold nested spans
		j := i + 1
		for j < to && s[j] != ']' {
			if end, ok := p.matchAt(j, to, "", false); ok && end <= to {
				j = end
				continue
			}
			j++
		}
		if j >= to || p.at(j+1) != '(' {
			return 0, false
		}
		k := j + 2
		for k < to && s[k] != ')' && s[k] != '\n' {
			k++
		}
		if k >= to || s[k] != ')' {
			return 0, false
		}
		if record {
			p.out = append(p.out, span{sLink, i, k + 1, i + 1, j})
			p.parseRange(i+1, j)
		}
		return k + 1, true
	}
	if (p.has(i, "https://") || p.has(i, "http://")) && !isWord(p.at(i-1)) {
		// a bare URL is a link with no markers (Kvit's autolink matcher)
		j := i
		for j < to && !isSpace(s[j]) {
			j++
		}
		for j > i && (s[j-1] == '.' || s[j-1] == ',' || s[j-1] == ')' || s[j-1] == ';' || s[j-1] == ':') {
			j--
		}
		if record {
			p.out = append(p.out, span{sLink, i, j, i, j})
		}
		return j, true
	}
	for _, d := range delims {
		if d.m == closing || !p.has(i, d.m) {
			continue
		}
		n := len([]rune(d.m))
		mc := rune(d.m[0])
		next := p.at(i + n)
		if isSpace(next) && next != 0 && !p.has(i+n, d.m) {
			continue // "** x" does not open
		}
		if n == 1 && next == mc {
			continue
		}
		if mc == '_' && isWord(p.at(i-1)) {
			continue // snake_case_name
		}
		close := p.findClose(d.m, i+n, to)
		if close < 0 {
			continue
		}
		end := close + n
		if record {
			p.out = append(p.out, span{d.kind, i, end, i + n, close})
			p.parseRange(i+n, close)
		}
		return end, true
	}
	return 0, false
}

func (p *iparser) findClose(m string, from, to int) int {
	n := len([]rune(m))
	mc := rune(m[0])
	for j := from; j+n <= to; {
		if p.has(j, m) && (j == from || !isSpace(p.at(j-1))) {
			ok := true
			if n == 1 && p.at(j+1) == mc && j+1 < to {
				ok = false // "*" does not close on the first star of "**"
			}
			if mc == '_' && isWord(p.at(j+n)) {
				ok = false
			}
			if ok {
				return j
			}
		}
		if end, ok := p.matchAt(j, to, m, false); ok && end > j {
			j = end
			continue
		}
		j++
	}
	return -1
}

// runeFlags are the styles a drawn character can carry.
type runeFlags uint16

const (
	fBold runeFlags = 1 << iota
	fItalic
	fStrike
	fHighlight
	fUnderline
	fCode
	fLink
	fMarker
	fSelected
)

func (k spanKind) flags() runeFlags {
	switch k {
	case sBold:
		return fBold
	case sItalic:
		return fItalic
	case sBoldItalic:
		return fBold | fItalic
	case sStrike:
		return fStrike
	case sHighlight:
		return fHighlight
	case sUnderline:
		return fUnderline
	case sCode:
		return fCode
	case sLink, sWiki:
		return fLink
	}
	return 0
}

// projection is what gets drawn for one block's source.
type projection struct {
	Src   []rune
	Spans []span
	Disp  []rune
	flags []runeFlags // per drawn rune
	D2S   []int       // drawn rune -> source offset
	S2D   []int       // source offset (0..len) -> drawn offset
}

// revealed reports whether a span shows its markers for this caret and
// selection. Touching an edge counts (Kvit's BlockEditorEngine rule).
func revealed(sp span, caret, selA, selB int) bool {
	if caret >= sp.Start && caret <= sp.End {
		return true
	}
	return selA != selB && selA < sp.End && selB > sp.Start
}

// project builds the drawn text. reveal is nil for a block without the
// caret, which shows every span rendered.
func project(src []rune, spans []span, reveal func(span) bool) projection {
	hidden := make([]bool, len(src))
	fl := make([]runeFlags, len(src))
	for _, sp := range spans {
		shown := reveal != nil && reveal(sp)
		for i := sp.CStart; i < sp.CEnd; i++ {
			fl[i] |= sp.Kind.flags()
		}
		for _, r := range [][2]int{{sp.Start, sp.CStart}, {sp.CEnd, sp.End}} {
			for i := r[0]; i < r[1]; i++ {
				if shown {
					fl[i] = fMarker
				} else {
					hidden[i] = true
				}
			}
		}
	}
	p := projection{Src: src, Spans: spans, S2D: make([]int, len(src)+1)}
	for i, r := range src {
		p.S2D[i] = len(p.Disp)
		if hidden[i] {
			continue
		}
		p.Disp = append(p.Disp, r)
		p.flags = append(p.flags, fl[i])
		p.D2S = append(p.D2S, i)
	}
	p.S2D[len(src)] = len(p.Disp)
	return p
}

// afterPrev is the source offset right after the drawn rune before d:
// in front of any hidden markers that follow it.
func (p *projection) afterPrev(d int) int {
	if d <= 0 {
		return 0
	}
	return p.D2S[d-1] + 1
}

// beforeNext is the source offset right before the drawn rune at d: past
// any hidden markers in front of it.
func (p *projection) beforeNext(d int) int {
	if d >= len(p.D2S) {
		return len(p.Src)
	}
	return p.D2S[d]
}

// forClick maps a drawn offset the pointer hit to a source offset, putting
// the caret next to the character that was clicked: inside the span on
// that side rather than outside its hidden markers.
func (p *projection) forClick(d int) int {
	a, b := p.afterPrev(d), p.beforeNext(d)
	if a == b {
		return a
	}
	for _, sp := range p.Spans {
		if sp.CEnd == a && sp.CEnd > sp.CStart {
			return a
		}
	}
	return b
}
