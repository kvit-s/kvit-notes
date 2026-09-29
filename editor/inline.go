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

import (
	"strings"
	"unicode"

	"github.com/kvit-s/kvit-notes/links"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/kvit-s/kvit-ui/text"
)

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
	sSup   // ^sup^, content without spaces (Pandoc's rule)
	sSub   // ~sub~, likewise
	sMath  // $x^2$, typeset away from the caret (mathinline.go)
	sColor // <span style="color:VALUE">…</span>
)

// span is one formatted span in source rune offsets. [Start, CStart) and
// [CEnd, End) are its markers; [CStart, CEnd) is its content. For a link
// the closing marker is "](url)".
type span struct {
	Kind       spanKind
	Start, End int
	CStart     int
	CEnd       int
	// Color is a colour span's value: #rgb, #rrggbb or a CSS colour name.
	Color string
}

type delim struct {
	m    string
	kind spanKind
}

// Longest first, so "***" is tried before "**" and "*".
var delims = []delim{
	{"***", sBoldItalic}, {"**", sBold}, {"__", sBold}, {"~~", sStrike},
	{"==", sHighlight}, {"++", sUnderline}, {"*", sItalic}, {"_", sItalic},
	{"^", sSup}, {"~", sSub},
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
					p.out = append(p.out, span{Kind: sCode, Start: i, End: j + k, CStart: i + n, CEnd: j})
				}
				return j + k, true
			}
			j += k - 1
		}
		return 0, false
	case p.has(i, "[["):
		// A wiki link by the shared grammar (links.MatchAt, the port of
		// WikiLinkScanner::matchAt): with an alias the opening marker
		// swallows "target|", so only the alias shows away from the caret,
		// as in the app.
		if l, ok := links.MatchAt(s, i); ok && i+l.Length <= to {
			if record {
				cstart := i + 2
				if l.AliasStart >= 0 {
					cstart = i + 2 + l.TargetLength + 1
				}
				p.out = append(p.out, span{Kind: sWiki, Start: i, End: i + l.Length, CStart: cstart, CEnd: i + l.Length - 2})
			}
			return i + l.Length, true
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
			p.out = append(p.out, span{Kind: sLink, Start: i, End: k + 1, CStart: i + 1, CEnd: j})
			p.parseRange(i+1, j)
		}
		return k + 1, true
	}
	if s[i] == '$' {
		return p.matchMath(i, to, record)
	}
	if s[i] == '<' {
		if end, ok := p.matchColor(i, to, record); ok {
			return end, true
		}
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
			p.out = append(p.out, span{Kind: sLink, Start: i, End: j, CStart: i, CEnd: j})
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
		limit := to
		noSpace := d.kind == sSup || d.kind == sSub
		if noSpace {
			// The content may not hold a space, so "either ~5 or ~3" stays
			// as it is written.
			for k := i + n; k < to; k++ {
				if unicode.IsSpace(s[k]) {
					limit = k
					break
				}
			}
		}
		close := p.findClose(d.m, i+n, limit)
		if close < 0 || close == i+n && noSpace {
			continue
		}
		end := close + n
		if record {
			p.out = append(p.out, span{Kind: d.kind, Start: i, End: end, CStart: i + n, CEnd: close})
			p.parseRange(i+n, close)
		}
		return end, true
	}
	return 0, false
}

// matchMath reads inline math, $…$, by Pandoc's rule as Kvit reads it: the
// opening $ is followed by a character other than a space or another $, the
// content is one line, and the first unescaped $ after it closes it only
// when it follows a character other than a space and is not followed by a
// digit, so "$5 and $6" stays prose.
func (p *iparser) matchMath(i, to int, record bool) (int, bool) {
	s := p.s
	if i+1 >= to || isSpace(s[i+1]) || s[i+1] == '$' || p.at(i-1) == '$' {
		return 0, false
	}
	for j := i + 1; j < to; j++ {
		switch s[j] {
		case '\n':
			return 0, false
		case '\\':
			j++
		case '$':
			if isSpace(s[j-1]) || unicode.IsDigit(p.at(j+1)) {
				return 0, false
			}
			if record {
				p.out = append(p.out, span{Kind: sMath, Start: i, End: j + 1, CStart: i + 1, CEnd: j})
			}
			return j + 1, true
		}
	}
	return 0, false
}

// colorOpen is the length of a colour span's opening tag at i and its
// value, by Kvit's exact grammar (markdownformatter.cpp, matchColorOpen):
// <span style="color:VALUE"> with either quote, VALUE #rgb, #rrggbb or a
// run of ASCII letters, spaces allowed round it. Anything else is not one.
func (p *iparser) colorOpen(i, to int) (int, string) {
	const prefix, property = "<span style=", "color:"
	if !p.has(i, prefix) {
		return -1, ""
	}
	k := i + len(prefix)
	q := p.at(k)
	if q != '"' && q != '\'' {
		return -1, ""
	}
	k++
	if !p.has(k, property) {
		return -1, ""
	}
	k += len(property)
	spaces := func() {
		for k < to && (p.s[k] == ' ' || p.s[k] == '\t') {
			k++
		}
	}
	spaces()
	start := k
	if p.at(k) == '#' {
		k++
		digits := 0
		for k < to && isHexDigit(p.s[k]) {
			k++
			digits++
		}
		if digits != 3 && digits != 6 {
			return -1, ""
		}
	} else {
		for k < to && (p.s[k] >= 'a' && p.s[k] <= 'z' || p.s[k] >= 'A' && p.s[k] <= 'Z') {
			k++
		}
		if k == start {
			return -1, ""
		}
	}
	value := string(p.s[start:k])
	spaces()
	if p.at(k) != q || p.at(k+1) != '>' {
		return -1, ""
	}
	return k + 2 - i, value
}

func isHexDigit(r rune) bool {
	return r >= '0' && r <= '9' || r >= 'a' && r <= 'f' || r >= 'A' && r <= 'F'
}

// matchColor reads a colour span, finding its </span> by balancing the
// colour spans nested in it; an unclosed or empty one stays text.
func (p *iparser) matchColor(i, to int, record bool) (int, bool) {
	n, value := p.colorOpen(i, to)
	if n < 0 {
		return 0, false
	}
	const closeTag = "</span>"
	depth := 0
	for k := i + n; k < to; {
		if m, _ := p.colorOpen(k, to); m > 0 {
			depth++
			k += m
			continue
		}
		if p.has(k, closeTag) {
			if depth == 0 {
				if k == i+n {
					return 0, false
				}
				end := k + len(closeTag)
				if record {
					p.out = append(p.out, span{Kind: sColor, Start: i, End: end, CStart: i + n, CEnd: k, Color: value})
					p.parseRange(i+n, k)
				}
				return end, true
			}
			depth--
			k += len(closeTag)
			continue
		}
		k++
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
type runeFlags uint32

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
	fSup
	fSub
	fMath
	fBlank // drawn in no colour: a drop cap's letter in the text
	// A code block's syntax colours (the highlight package's classes).
	fCodeKeyword
	fCodeType
	fCodeString
	fCodeComment
	fCodeNumber
	fMatch        // a find bar match
	fMatchCurrent // the find bar's current match
	fMathBox      // a typeset $…$ span, laid out as its formula's box (mathinline.go)
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
	case sSup:
		return fSup
	case sSub:
		return fSub
	case sMath:
		return fMath
	}
	return 0
}

// projection is what gets drawn for one block's source.
type projection struct {
	Src   []rune
	Spans []span
	Disp  []rune
	flags []runeFlags // per drawn rune
	// colors is each drawn rune's text colour from a colour span, "" for
	// none; nil when the block has no colour span.
	colors []string
	D2S    []int // drawn rune -> source offset
	S2D    []int // source offset (0..len) -> drawn offset
	// boxes are the typeset $…$ spans by the drawn offset of the one
	// character each is drawn as, which stands for the whole span.
	boxes map[int]*inlineBox
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
	var colors []string
	for _, sp := range spans {
		if sp.Kind == sColor {
			if colors == nil {
				colors = make([]string, len(src))
			}
			// Spans come outer first, so a nested colour wins.
			for i := sp.CStart; i < sp.CEnd; i++ {
				colors[i] = sp.Color
			}
		}
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
		if colors != nil {
			p.colors = append(p.colors, colors[i])
		}
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
	if bx := p.boxes[d-1]; bx != nil {
		return bx.span.End
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

// cssColors are the CSS colour names a colour span may use, those the
// app's text colour dialog and other editors write.
var cssColors = map[string]string{
	"black": "#000000", "white": "#ffffff", "gray": "#808080", "grey": "#808080", "silver": "#c0c0c0",
	"red": "#ff0000", "maroon": "#800000", "orange": "#ffa500", "yellow": "#ffff00", "olive": "#808000",
	"lime": "#00ff00", "green": "#008000", "teal": "#008080", "aqua": "#00ffff", "cyan": "#00ffff",
	"blue": "#0000ff", "navy": "#000080", "purple": "#800080", "fuchsia": "#ff00ff", "magenta": "#ff00ff",
	"pink": "#ffc0cb", "brown": "#a52a2a", "gold": "#ffd700", "indigo": "#4b0082", "violet": "#ee82ee",
	"crimson": "#dc143c", "coral": "#ff7f50", "tomato": "#ff6347", "salmon": "#fa8072", "orchid": "#da70d6",
	"darkred": "#8b0000", "darkgreen": "#006400", "darkblue": "#00008b", "darkorange": "#ff8c00",
	"darkgray": "#a9a9a9", "darkgrey": "#a9a9a9", "lightgray": "#d3d3d3", "lightgrey": "#d3d3d3",
	"steelblue": "#4682b4", "royalblue": "#4169e1", "seagreen": "#2e8b57", "forestgreen": "#228b22",
	"firebrick": "#b22222", "chocolate": "#d2691e", "slategray": "#708090", "slategrey": "#708090",
}

// parseColor reads a colour span's value; an unknown name is no colour.
func parseColor(v string) (text.Color, bool) {
	if v == "" {
		return text.Color{}, false
	}
	if v[0] != '#' {
		hex, ok := cssColors[strings.ToLower(v)]
		if !ok {
			return text.Color{}, false
		}
		v = hex
	}
	c, err := palette.ParseHex(v)
	if err != nil {
		return text.Color{}, false
	}
	return colour(c), true
}
