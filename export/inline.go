package export

// Inline Markdown as the exporter reads it: the span parser of
// src/content/markdownformatter.cpp (MarkdownFormatter::parseSpans), the
// wiki-link grammar of src/content/wikilinkscanner.cpp, the inline HTML of
// src/content/htmlinline.cpp, and the marker-free text of
// src/content/inlinemarkdown.cpp (InlineMarkdown::displayText).
//
// The editor package has its own span parser, which knows fewer span types
// (no superscript, subscript, colour, inline maths or escapes). An export has
// to write the same HTML the app writes, so the parser is ported here
// whole rather than borrowed from the editor.

import (
	"strings"
	"unicode"
)

// fspan is one parsed span, in rune offsets of the text it was parsed from.
// [start, start+openLen) is the opening marker, [end-closeLen, end) the
// closing one. Children are the spans inside the content, in the same
// coordinates; a verbatim span (code, maths, an escape, a wiki link, an
// autolink) has none.
type fspan struct {
	typ      string
	start    int
	end      int
	openLen  int
	closeLen int
	url      string
	color    string
	children []fspan
}

type matcher int

const (
	delimiterPair matcher = iota
	linkMatcher
	autolinkMatcher
	colorMatcher
	mathMatcher
	codeMatcher
	escapeMatcher
	wikiLinkMatcher
)

type spanDef struct {
	name         string
	matcher      matcher
	open         string
	verbatim     bool
	wordBoundary bool
	noSpace      bool
}

// spanDefs is kSpanTypes: the rows are tried in this order at each position
// and the first complete match wins.
var spanDefs = []spanDef{
	{"bolditalic", delimiterPair, "***", false, false, false},
	{"bold", delimiterPair, "**", false, false, false},
	{"italic", delimiterPair, "*", false, false, false},
	{"bolditalic", delimiterPair, "___", false, true, false},
	{"bold", delimiterPair, "__", false, true, false},
	{"italic", delimiterPair, "_", false, true, false},
	{"strike", delimiterPair, "~~", false, false, false},
	{"highlight", delimiterPair, "==", false, false, false},
	{"underline", delimiterPair, "++", false, false, false},
	{"superscript", delimiterPair, "^", false, false, true},
	{"subscript", delimiterPair, "~", false, false, true},
	{"code", codeMatcher, "`", true, false, false},
	{"math", mathMatcher, "$", true, false, false},
	{"escape", escapeMatcher, "\\", true, false, false},
	{"color", colorMatcher, "", false, false, false},
	{"wikilink", wikiLinkMatcher, "[[", true, false, false},
	{"link", linkMatcher, "", false, false, false},
	{"autolink", autolinkMatcher, "", true, false, false},
}

// The parse is bounded as the parser bounds it: nesting stops at 24
// levels, and every scanning loop spends from one budget shared by the whole
// parse, after which the remaining text stays literal.
const (
	maxSpanDepth      = 24
	parseStepFloor    = 2_000_000
	parseStepsPerRune = 512
)

type parseState struct {
	depth  int
	steps  int64
	budget int64
}

func (s *parseState) exhausted() bool { return s.steps > s.budget }

func (s *parseState) spend(n int) bool {
	s.steps += int64(n)
	return s.steps <= s.budget
}

func isLetterOrNumber(r rune) bool { return unicode.IsLetter(r) || unicode.IsNumber(r) }

// isSpace is QChar::isSpace.
func isSpace(r rune) bool { return unicode.IsSpace(r) }

// escapedAt reports whether the character at pos is escaped by an odd run of
// backslashes before it.
func escapedAt(md []rune, pos int) bool {
	n := 0
	for i := pos - 1; i >= 0 && md[i] == '\\'; i-- {
		n++
	}
	return n&1 != 0
}

// indexRunes is string::indexOf for a marker, from a start offset.
func indexRunes(md []rune, marker []rune, from int) int {
	if from < 0 {
		from = 0
	}
outer:
	for i := from; i+len(marker) <= len(md); i++ {
		for k, r := range marker {
			if md[i+k] != r {
				continue outer
			}
		}
		return i
	}
	return -1
}

func indexRune(md []rune, r rune, from int) int {
	for i := max(from, 0); i < len(md); i++ {
		if md[i] == r {
			return i
		}
	}
	return -1
}

func hasPrefixAt(md []rune, pos int, prefix string) bool {
	i := pos
	for _, r := range prefix {
		if i >= len(md) || md[i] != r {
			return false
		}
		i++
	}
	return true
}

func containsRune(md []rune, r rune) bool { return indexRune(md, r, 0) >= 0 }

func familyHasLongerThan(c rune, n int) bool {
	for _, d := range spanDefs {
		if d.matcher == delimiterPair && len(d.open) > n && rune(d.open[0]) == c {
			return true
		}
	}
	return false
}

func familyHasDouble(c rune) bool {
	for _, d := range spanDefs {
		if d.matcher == delimiterPair && len(d.open) == 2 && rune(d.open[0]) == c {
			return true
		}
	}
	return false
}

// matchDelimiter is the symmetric-marker matcher: the span end, or -1.
func matchDelimiter(md []rune, pos int, marker string, wordBoundary bool, st *parseState, noSpace bool) int {
	mk := []rune(marker)
	c := mk[0]
	n := len(mk)
	if pos+n > len(md) {
		return -1
	}
	for i := 0; i < n; i++ {
		if md[pos+i] != c {
			return -1
		}
	}
	if wordBoundary && pos > 0 && (isLetterOrNumber(md[pos-1]) || md[pos-1] == c) {
		return -1
	}
	if n == 3 && !wordBoundary {
		from := pos + 3
		for {
			closePos := indexRunes(md, mk, from)
			stop := closePos
			if stop < 0 {
				stop = len(md)
			}
			if !st.spend(stop - from + 1) {
				return -1
			}
			if closePos == -1 {
				return -1
			}
			if escapedAt(md, closePos) {
				from = closePos + 1
				continue
			}
			if closePos > pos+3 {
				return closePos + 3
			}
			return -1
		}
	}
	if n < 3 && pos+n < len(md) && md[pos+n] == c && familyHasLongerThan(c, n) {
		return -1
	}
	contentLimit := len(md)
	if noSpace {
		for i := pos + n; i < len(md); i++ {
			if !st.spend(1) {
				return -1
			}
			if isSpace(md[i]) {
				contentLimit = i
				break
			}
		}
	}
	searchStart := pos + n
	for searchStart < len(md) {
		cand := indexRunes(md, mk, searchStart)
		stop := cand
		if stop < 0 {
			stop = len(md)
		}
		if !st.spend(stop - searchStart + 1) {
			return -1
		}
		if cand == -1 || cand > contentLimit {
			return -1
		}
		if cand > pos+n {
			if escapedAt(md, cand) {
				searchStart = cand + 1
				continue
			}
			if md[cand-1] == c {
				searchStart = cand + 1
				continue
			}
			if wordBoundary && cand+n < len(md) && isLetterOrNumber(md[cand+n]) {
				searchStart = cand + 1
				continue
			}
			if n == 1 && familyHasDouble(c) && cand+1 < len(md) && md[cand+1] == c {
				dblClose := indexRunes(md, []rune{c, c}, cand+2)
				if dblClose != -1 && indexRune(md, c, dblClose+2) != -1 {
					searchStart = dblClose + 2
					continue
				}
			}
			return cand + n
		}
		searchStart = cand + 1
	}
	return -1
}

// codeRunScan holds one backtick run's closing candidates, found once for
// the whole run: for each opening length, where the first closing run of
// exactly that length starts, or -1.
type codeRunScan struct {
	runStart, runEnd int
	firstClose       []int
	built            bool
}

func (c *codeRunScan) covers(pos int) bool {
	return c.built && pos >= c.runStart && pos < c.runEnd
}

func (c *codeRunScan) build(md []rune, pos int, st *parseState) {
	start := pos
	for start > 0 && md[start-1] == '`' {
		start--
	}
	end := pos
	for end < len(md) && md[end] == '`' {
		end++
	}
	c.runStart, c.runEnd, c.built = start, end, true
	longest := end - start
	c.firstClose = make([]int, longest+1)
	for i := range c.firstClose {
		c.firstClose[i] = -1
	}
	st.spend(longest)
	i := end
	for i < len(md) {
		if md[i] != '`' {
			i++
			if !st.spend(1) {
				return
			}
			continue
		}
		run := 0
		for i+run < len(md) && md[i+run] == '`' {
			run++
		}
		if run <= longest && c.firstClose[run] < 0 {
			c.firstClose[run] = i
		}
		i += run
		if !st.spend(run) {
			return
		}
	}
}

func canStartWith(d spanDef, c rune) bool {
	switch d.matcher {
	case delimiterPair:
		return c == rune(d.open[0])
	case codeMatcher:
		return c == '`'
	case mathMatcher:
		return c == '$'
	case escapeMatcher:
		return c == '\\'
	case colorMatcher:
		return c == '<'
	case wikiLinkMatcher, linkMatcher:
		return c == '['
	case autolinkMatcher:
		return c == 'h'
	}
	return true
}

func isColorSpace(c rune) bool {
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'
}

func isHexDigit(c rune) bool {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')
}

func isASCIILetter(c rune) bool { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') }

// matchColorOpen recognises <span style="color:VALUE"> at pos: the marker's
// length and the value, or -1.
func matchColorOpen(md []rune, pos int) (int, string) {
	const prefix = "<span style="
	if !hasPrefixAt(md, pos, prefix) {
		return -1, ""
	}
	i := pos + len(prefix)
	if i >= len(md) {
		return -1, ""
	}
	quote := md[i]
	if quote != '"' && quote != '\'' {
		return -1, ""
	}
	i++
	if !hasPrefixAt(md, i, "color:") {
		return -1, ""
	}
	i += len("color:")
	for i < len(md) && isColorSpace(md[i]) {
		i++
	}
	valueStart := i
	if i < len(md) && md[i] == '#' {
		i++
		digits := 0
		for i < len(md) && isHexDigit(md[i]) {
			i++
			digits++
		}
		if digits != 3 && digits != 6 {
			return -1, ""
		}
	} else {
		for i < len(md) && isASCIILetter(md[i]) {
			i++
		}
		if i == valueStart {
			return -1, ""
		}
	}
	valueEnd := i
	for i < len(md) && isColorSpace(md[i]) {
		i++
	}
	if i >= len(md) || md[i] != quote {
		return -1, ""
	}
	i++
	if i >= len(md) || md[i] != '>' {
		return -1, ""
	}
	i++
	return i - pos, string(md[valueStart:valueEnd])
}

// wikiMatch is WikiLinkScanner::matchAt: a [[target#heading|alias]] at pos.
// It returns the occurrence's length, the target's length (before any "|"),
// the trimmed target, and whether it has an alias.
func wikiMatch(md []rune, pos int) (length, targetLen int, target string, alias, ok bool) {
	if pos < 0 || pos+1 >= len(md) || md[pos] != '[' || md[pos+1] != '[' || escapedAt(md, pos) {
		return
	}
	closeAt := indexRunes(md, []rune("]]"), pos+2)
	if closeAt < 0 {
		return
	}
	inner := md[pos+2 : closeAt]
	if len(inner) == 0 || containsRune(inner, '\n') || containsRune(inner, '[') || containsRune(inner, ']') {
		return
	}
	pipe := indexRune(inner, '|', 0)
	targetPart := inner
	var aliasPart []rune
	if pipe >= 0 {
		targetPart = inner[:pipe]
		aliasPart = inner[pipe+1:]
		if strings.TrimSpace(string(aliasPart)) == "" || containsRune(aliasPart, '|') {
			return
		}
	}
	hash := indexRune(targetPart, '#', 0)
	note := targetPart
	var heading []rune
	if hash >= 0 {
		note = targetPart[:hash]
		heading = targetPart[hash+1:]
	}
	if containsRune(heading, '#') || (hash >= 0 && strings.TrimSpace(string(heading)) == "") ||
		(strings.TrimSpace(string(note)) == "" && hash < 0) {
		return
	}
	return closeAt + 2 - pos, len(targetPart), trimSpace(string(targetPart)), pipe >= 0, true
}

// trimSpace is string::trimmed: whitespace by QChar::isSpace.
func trimSpace(s string) string { return strings.TrimFunc(s, isSpace) }

const escapable = "*_~^=+`[]\\$#->|"

func matchTypeAt(d spanDef, md []rune, pos int, sp *fspan, st *parseState, runs *codeRunScan) bool {
	if st.exhausted() {
		return false
	}
	if pos > 0 && escapedAt(md, pos) {
		return false
	}
	switch d.matcher {
	case delimiterPair:
		end := matchDelimiter(md, pos, d.open, d.wordBoundary, st, d.noSpace)
		if end < 0 {
			return false
		}
		sp.openLen = len(d.open)
		sp.closeLen = len(d.open)
		sp.end = end
	case codeMatcher:
		if md[pos] != '`' {
			return false
		}
		if !runs.covers(pos) {
			runs.build(md, pos, st)
		}
		openLen := runs.runEnd - pos
		closeAt := -1
		if openLen < len(runs.firstClose) {
			closeAt = runs.firstClose[openLen]
		}
		if closeAt < 0 || closeAt == pos+openLen {
			return false
		}
		sp.openLen = openLen
		sp.closeLen = openLen
		sp.end = closeAt + openLen
	case linkMatcher:
		if md[pos] != '[' {
			return false
		}
		closeBracket := indexRunes(md, []rune("]("), pos+1)
		stop := closeBracket
		if stop < 0 {
			stop = len(md)
		}
		if !st.spend(stop - pos) {
			return false
		}
		if closeBracket <= pos+1 {
			return false
		}
		text := md[pos+1 : closeBracket]
		if containsRune(text, '[') || containsRune(text, ']') || containsRune(text, '\n') {
			return false
		}
		closeParen := indexRune(md, ')', closeBracket+2)
		stop = closeParen
		if stop < 0 {
			stop = len(md)
		}
		if !st.spend(stop - closeBracket) {
			return false
		}
		if closeParen == -1 {
			return false
		}
		url := md[closeBracket+2 : closeParen]
		if containsRune(url, ' ') || containsRune(url, '\n') || containsRune(url, '(') {
			return false
		}
		sp.openLen = 1
		sp.closeLen = closeParen - closeBracket + 1
		sp.end = closeParen + 1
		sp.url = string(url)
	case colorMatcher:
		openLen, value := matchColorOpen(md, pos)
		if openLen < 0 {
			return false
		}
		const closeTag = "</span>"
		depth := 0
		i := pos + openLen
		closeAt := -1
		for i < len(md) {
			if !st.spend(1) {
				return false
			}
			if nest, _ := matchColorOpen(md, i); nest > 0 {
				depth++
				i += nest
				continue
			}
			if hasPrefixAt(md, i, closeTag) {
				if depth == 0 {
					closeAt = i
					break
				}
				depth--
				i += len(closeTag)
				continue
			}
			i++
		}
		if closeAt < 0 || closeAt == pos+openLen {
			return false
		}
		sp.openLen = openLen
		sp.closeLen = len(closeTag)
		sp.end = closeAt + len(closeTag)
		sp.color = value
	case mathMatcher:
		if md[pos] != '$' {
			return false
		}
		contentStart := pos + 1
		if contentStart >= len(md) {
			return false
		}
		after := md[contentStart]
		if isSpace(after) || after == '$' {
			return false
		}
		closeAt := -1
		for i := contentStart; i < len(md); i++ {
			if !st.spend(1) {
				return false
			}
			c := md[i]
			if c == '\n' {
				return false
			}
			if c == '$' {
				if escapedAt(md, i) {
					continue
				}
				spaceBefore := isSpace(md[i-1])
				digitAfter := i+1 < len(md) && unicode.IsDigit(md[i+1])
				if !spaceBefore && !digitAfter && i > contentStart {
					closeAt = i
				}
				break
			}
		}
		if closeAt < 0 {
			return false
		}
		sp.openLen = 1
		sp.closeLen = 1
		sp.end = closeAt + 1
	case wikiLinkMatcher:
		length, targetLen, target, alias, ok := wikiMatch(md, pos)
		if !ok {
			return false
		}
		sp.openLen = 2
		if alias {
			sp.openLen = targetLen + 3
		}
		sp.closeLen = 2
		sp.end = pos + length
		sp.url = "kvit-note:" + target
	case escapeMatcher:
		if md[pos] != '\\' || pos+1 >= len(md) || !strings.ContainsRune(escapable, md[pos+1]) {
			return false
		}
		sp.openLen = 1
		sp.closeLen = 0
		sp.end = pos + 2
	case autolinkMatcher:
		if pos > 0 && (isLetterOrNumber(md[pos-1]) || md[pos-1] == '/') {
			return false
		}
		schemeLen := 0
		switch {
		case hasPrefixAt(md, pos, "https://"):
			schemeLen = 8
		case hasPrefixAt(md, pos, "http://"):
			schemeLen = 7
		default:
			return false
		}
		end := pos + schemeLen
		for end < len(md) && !isSpace(md[end]) && md[end] != ')' && md[end] != ']' {
			end++
			if !st.spend(1) {
				return false
			}
		}
		for end > pos+schemeLen && strings.ContainsRune(".,;:!?", md[end-1]) {
			end--
		}
		if end == pos+schemeLen {
			return false
		}
		sp.openLen = 0
		sp.closeLen = 0
		sp.end = end
		sp.url = string(md[pos:end])
	}
	sp.start = pos
	sp.typ = d.name
	return true
}

func shiftSpan(sp *fspan, by int) {
	sp.start += by
	sp.end += by
	for i := range sp.children {
		shiftSpan(&sp.children[i], by)
	}
}

// parseSpans returns the spans of an inline Markdown text.
func parseSpans(md []rune) []fspan {
	st := &parseState{budget: parseStepFloor + parseStepsPerRune*int64(len(md))}
	return parseSpansWith(md, st)
}

func parseSpansWith(md []rune, st *parseState) []fspan {
	var spans []fspan
	var runs codeRunScan
	pos := 0
	for pos < len(md) {
		matched := false
		lead := md[pos]
		for _, d := range spanDefs {
			if !canStartWith(d, lead) {
				continue
			}
			var sp fspan
			if !matchTypeAt(d, md, pos, &sp, st, &runs) {
				continue
			}
			contentLen := sp.end - sp.start - sp.openLen - sp.closeLen
			if !d.verbatim && contentLen > 0 && st.depth < maxSpanDepth {
				content := md[pos+sp.openLen : pos+sp.openLen+contentLen]
				st.depth++
				sp.children = parseSpansWith(content, st)
				st.depth--
				for i := range sp.children {
					shiftSpan(&sp.children[i], sp.start+sp.openLen)
				}
			}
			pos = sp.end
			spans = append(spans, sp)
			matched = true
			break
		}
		if !matched {
			pos++
			st.spend(1)
		}
	}
	return spans
}

// displayText is InlineMarkdown::displayText: the text with the markers of
// every span, at every depth, removed. It is what a plain-text export writes
// for prose (BlockText::renderedFully) and what heading anchors are made from.
func displayText(markdown string) string {
	md := []rune(markdown)
	var out []rune
	pos := 0
	var walk func(spans []fspan)
	walk = func(spans []fspan) {
		for _, sp := range spans {
			out = append(out, md[pos:sp.start]...)
			pos = sp.start + sp.openLen
			walk(sp.children)
			out = append(out, md[pos:sp.end-sp.closeLen]...)
			pos = sp.end
		}
	}
	walk(parseSpans(md))
	out = append(out, md[pos:]...)
	return string(out)
}

// esc escapes &, <, > and " for HTML, and nothing else: an apostrophe is left
// alone (src/content/htmlinline.cpp, HtmlInline::esc).
func esc(text string) string {
	if !strings.ContainsAny(text, `&<>"`) {
		return text
	}
	var sb strings.Builder
	for _, r := range text {
		switch r {
		case '&':
			sb.WriteString("&amp;")
		case '<':
			sb.WriteString("&lt;")
		case '>':
			sb.WriteString("&gt;")
		case '"':
			sb.WriteString("&quot;")
		default:
			sb.WriteRune(r)
		}
	}
	return sb.String()
}

// escFlowing is Esc with each newline written as <br>, for prose, where a
// bare newline would collapse to a space (HtmlInline::escFlowing).
func escFlowing(text string) string {
	return strings.ReplaceAll(esc(text), "\n", "<br>")
}

// safeHref is the link target an exported document may put in an href, or
// "" when it may not (HtmlInline::safeHref). Only schemes that navigate are
// allowed through: http, https, mailto, ftp, ftps, file, tel and sms. A
// relative reference, which has no scheme, is kept.
func safeHref(url string) string {
	var probe []rune
	for _, c := range url {
		if c == '\t' || c == '\n' || c == '\r' {
			continue
		}
		probe = append(probe, c)
	}
	for len(probe) > 0 && probe[0] <= 0x20 {
		probe = probe[1:]
	}
	colon := -1
	for i, c := range probe {
		if c == ':' {
			colon = i
			break
		}
		letter := isASCIILetter(c)
		rest := letter || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.'
		if (i == 0 && !letter) || (i > 0 && !rest) {
			return url
		}
	}
	if colon < 0 {
		return url
	}
	switch strings.ToLower(string(probe[:colon])) {
	case "http", "https", "mailto", "ftp", "ftps", "file", "tel", "sms":
		return url
	}
	return ""
}

// inlineHTML renders a block's inline Markdown as HTML, walking the span tree
// so nesting survives (HtmlInline::renderInline). Inline maths becomes a
// MathJax \( … \) and sets *sawMath.
func inlineHTML(markdown string, sawMath *bool) string {
	md := []rune(markdown)
	var list func(spans []fspan, lo, hi int) string
	one := func(sp fspan) string {
		cs, ce := sp.start+sp.openLen, sp.end-sp.closeLen
		var inner string
		switch sp.typ {
		case "code", "autolink", "math", "escape":
			inner = esc(string(md[cs:ce]))
		default:
			inner = list(sp.children, cs, ce)
		}
		switch sp.typ {
		case "math":
			if sawMath != nil {
				*sawMath = true
			}
			return `\(` + inner + `\)`
		case "bold":
			return "<strong>" + inner + "</strong>"
		case "italic":
			return "<em>" + inner + "</em>"
		case "bolditalic":
			return "<strong><em>" + inner + "</em></strong>"
		case "strike":
			return "<s>" + inner + "</s>"
		case "underline":
			return "<u>" + inner + "</u>"
		case "highlight":
			return "<mark>" + inner + "</mark>"
		case "code":
			return "<code>" + inner + "</code>"
		case "superscript":
			return "<sup>" + inner + "</sup>"
		case "subscript":
			return "<sub>" + inner + "</sub>"
		case "color":
			return `<span style="color:` + esc(sp.color) + `">` + inner + "</span>"
		case "link", "autolink":
			href := safeHref(sp.url)
			if href == "" {
				return inner
			}
			return `<a href="` + esc(href) + `">` + inner + "</a>"
		}
		return inner
	}
	list = func(spans []fspan, lo, hi int) string {
		var sb strings.Builder
		pos := lo
		for _, sp := range spans {
			if sp.start < lo || sp.end > hi {
				continue
			}
			if pos < sp.start {
				sb.WriteString(escFlowing(string(md[pos:sp.start])))
			}
			sb.WriteString(one(sp))
			pos = sp.end
		}
		if pos < hi {
			sb.WriteString(escFlowing(string(md[pos:hi])))
		}
		return sb.String()
	}
	return list(parseSpans(md), 0, len(md))
}
