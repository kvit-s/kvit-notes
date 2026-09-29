package export

// Code-block syntax highlighting, as the exporter colours a code block's
// tokens (src/content/codelanguages.cpp): one rule table per language read by
// a shared scanner for the C-like languages, and separate scanners for
// markup, CSS, Markdown and Mermaid. Multi-line constructs (block comments,
// Python's triple-quoted strings) continue from one line to the next.

import (
	"strings"
	"unicode"
)

type token int

const (
	tokPlain token = iota
	tokKeyword
	tokType
	tokString
	tokComment
	tokNumber
)

type hlSpan struct {
	start, length int
	tok           token
}

// States between lines. 0 is normal; the others mean the line
// before ended inside a block comment or a triple-quoted string.
const (
	stNormal = iota
	stBlockComment
	stTripleDouble
	stTripleSingle
)

type family int

const (
	famGeneric family = iota
	famMarkup
	famCSS
	famMarkdown
	famMermaid
)

type rules struct {
	family             family
	keywords           map[string]bool
	types              map[string]bool
	caseInsensitive    bool
	lineComment        string
	blockStart         string
	blockEnd           string
	stringDelims       string
	tripleQuotes       bool
	backtickString     bool
	doubledQuoteEscape bool
	dollarVar          bool
	hashPreproc        bool
	atDecorator        bool
	noFunctionCalls    bool
}

func isIdentStart(c rune) bool { return unicode.IsLetter(c) || c == '_' }
func isIdentPart(c rune) bool  { return isLetterOrNumber(c) || c == '_' }

func emitSpan(out *[]hlSpan, start, n int, t token) {
	if n > 0 {
		*out = append(*out, hlSpan{start, n, t})
	}
}

// scanNumber returns the index just past a number literal starting at i.
func scanNumber(s []rune, i int) int {
	n := len(s)
	j := i
	if s[j] == '0' && j+1 < n && strings.ContainsRune("xXbBoO", s[j+1]) {
		j += 2
		for j < n && (isLetterOrNumber(s[j]) || s[j] == '_') {
			j++
		}
		return j
	}
	for j < n && (unicode.IsDigit(s[j]) || s[j] == '_') {
		j++
	}
	if j < n && s[j] == '.' {
		j++
		for j < n && (unicode.IsDigit(s[j]) || s[j] == '_') {
			j++
		}
	}
	if j < n && (s[j] == 'e' || s[j] == 'E') {
		j++
		if j < n && (s[j] == '+' || s[j] == '-') {
			j++
		}
		for j < n && unicode.IsDigit(s[j]) {
			j++
		}
	}
	for j < n && unicode.IsLetter(s[j]) {
		j++
	}
	return j
}

// scanString returns the index just past a one-line string opened at i, or
// the end of the line when it is not closed.
func scanString(s []rune, i int, delim rune, doubledEscape bool) int {
	n := len(s)
	j := i + 1
	for j < n {
		c := s[j]
		if c == '\\' && !doubledEscape {
			j += 2
			continue
		}
		if c == delim {
			if doubledEscape && j+1 < n && s[j+1] == delim {
				j += 2
				continue
			}
			return j + 1
		}
		j++
	}
	return n
}

func scanGeneric(r *rules, line []rune, startState int) ([]hlSpan, int) {
	var out []hlSpan
	n := len(line)
	i := 0
	if startState == stBlockComment && r.blockEnd != "" {
		closeAt := indexRunes(line, []rune(r.blockEnd), 0)
		if closeAt < 0 {
			emitSpan(&out, 0, n, tokComment)
			return out, stBlockComment
		}
		end := closeAt + len(r.blockEnd)
		emitSpan(&out, 0, end, tokComment)
		i = end
	} else if startState == stTripleDouble || startState == stTripleSingle {
		closer := `"""`
		if startState == stTripleSingle {
			closer = "'''"
		}
		at := indexRunes(line, []rune(closer), 0)
		if at < 0 {
			emitSpan(&out, 0, n, tokString)
			return out, startState
		}
		emitSpan(&out, 0, at+3, tokString)
		i = at + 3
	}
	for i < n {
		c := line[i]
		if r.lineComment != "" && hasPrefixAt(line, i, r.lineComment) {
			emitSpan(&out, i, n-i, tokComment)
			return out, stNormal
		}
		if r.blockStart != "" && hasPrefixAt(line, i, r.blockStart) {
			closeAt := indexRunes(line, []rune(r.blockEnd), i+len(r.blockStart))
			if closeAt < 0 {
				emitSpan(&out, i, n-i, tokComment)
				return out, stBlockComment
			}
			end := closeAt + len(r.blockEnd)
			emitSpan(&out, i, end-i, tokComment)
			i = end
			continue
		}
		if r.tripleQuotes && (hasPrefixAt(line, i, `"""`) || hasPrefixAt(line, i, "'''")) {
			dbl := line[i] == '"'
			closer := "'''"
			if dbl {
				closer = `"""`
			}
			at := indexRunes(line, []rune(closer), i+3)
			if at < 0 {
				emitSpan(&out, i, n-i, tokString)
				if dbl {
					return out, stTripleDouble
				}
				return out, stTripleSingle
			}
			emitSpan(&out, i, at+3-i, tokString)
			i = at + 3
			continue
		}
		if strings.ContainsRune(r.stringDelims, c) {
			end := scanString(line, i, c, r.doubledQuoteEscape)
			emitSpan(&out, i, end-i, tokString)
			i = end
			continue
		}
		if r.backtickString && c == '`' {
			end := scanString(line, i, '`', false)
			emitSpan(&out, i, end-i, tokString)
			i = end
			continue
		}
		if r.hashPreproc && c == '#' {
			atLineStart := true
			for k := 0; k < i; k++ {
				if !isSpace(line[k]) {
					atLineStart = false
					break
				}
			}
			if atLineStart {
				j := i + 1
				for j < n && isIdentPart(line[j]) {
					j++
				}
				emitSpan(&out, i, j-i, tokKeyword)
				i = j
				continue
			}
		}
		if r.atDecorator && c == '@' && i+1 < n && isIdentStart(line[i+1]) {
			j := i + 1
			for j < n && (isIdentPart(line[j]) || line[j] == '.') {
				j++
			}
			emitSpan(&out, i, j-i, tokType)
			i = j
			continue
		}
		if r.dollarVar && c == '$' && i+1 < n {
			j := i + 1
			if line[j] == '{' {
				closeAt := indexRune(line, '}', j)
				if closeAt < 0 {
					j = n
				} else {
					j = closeAt + 1
				}
			} else {
				for j < n && isIdentPart(line[j]) {
					j++
				}
			}
			emitSpan(&out, i, j-i, tokType)
			i = j
			continue
		}
		if unicode.IsDigit(c) || (c == '.' && i+1 < n && unicode.IsDigit(line[i+1])) {
			end := scanNumber(line, i)
			emitSpan(&out, i, end-i, tokNumber)
			i = end
			continue
		}
		if isIdentStart(c) {
			j := i + 1
			for j < n && isIdentPart(line[j]) {
				j++
			}
			word := string(line[i:j])
			key := word
			if r.caseInsensitive {
				key = strings.ToLower(word)
			}
			switch {
			case r.keywords[key]:
				emitSpan(&out, i, j-i, tokKeyword)
			case r.types[key]:
				emitSpan(&out, i, j-i, tokType)
			case !r.noFunctionCalls:
				k := j
				for k < n && isSpace(line[k]) {
					k++
				}
				if k < n && line[k] == '(' {
					emitSpan(&out, i, j-i, tokType)
				}
			}
			i = j
			continue
		}
		i++
	}
	return out, stNormal
}

func scanMarkup(line []rune, startState int) ([]hlSpan, int) {
	var out []hlSpan
	n := len(line)
	i := 0
	if startState == stBlockComment {
		closeAt := indexRunes(line, []rune("-->"), 0)
		if closeAt < 0 {
			emitSpan(&out, 0, n, tokComment)
			return out, stBlockComment
		}
		emitSpan(&out, 0, closeAt+3, tokComment)
		i = closeAt + 3
	}
	for i < n {
		if hasPrefixAt(line, i, "<!--") {
			closeAt := indexRunes(line, []rune("-->"), i+4)
			if closeAt < 0 {
				emitSpan(&out, i, n-i, tokComment)
				return out, stBlockComment
			}
			emitSpan(&out, i, closeAt+3-i, tokComment)
			i = closeAt + 3
			continue
		}
		if line[i] == '<' {
			j := i + 1
			for j < n && (line[j] == '/' || line[j] == '!' || line[j] == '?') {
				j++
			}
			nameStart := j
			for j < n && (isIdentPart(line[j]) || line[j] == '-' || line[j] == ':') {
				j++
			}
			if j > nameStart {
				emitSpan(&out, nameStart, j-nameStart, tokKeyword)
			}
			for j < n && line[j] != '>' {
				a := line[j]
				switch {
				case a == '"' || a == '\'':
					end := scanString(line, j, a, false)
					emitSpan(&out, j, end-j, tokString)
					j = end
				case isIdentStart(a):
					k := j + 1
					for k < n && (isIdentPart(line[k]) || line[k] == '-' || line[k] == ':') {
						k++
					}
					emitSpan(&out, j, k-j, tokType)
					j = k
				default:
					j++
				}
			}
			if j < n {
				i = j + 1
			} else {
				i = n
			}
			continue
		}
		if line[i] == '&' {
			semi := indexRune(line, ';', i)
			if semi > i && semi-i <= 10 {
				emitSpan(&out, i, semi+1-i, tokNumber)
				i = semi + 1
				continue
			}
		}
		i++
	}
	return out, stNormal
}

func scanCSS(line []rune, startState int) ([]hlSpan, int) {
	var out []hlSpan
	n := len(line)
	i := 0
	if startState == stBlockComment {
		closeAt := indexRunes(line, []rune("*/"), 0)
		if closeAt < 0 {
			emitSpan(&out, 0, n, tokComment)
			return out, stBlockComment
		}
		emitSpan(&out, 0, closeAt+2, tokComment)
		i = closeAt + 2
	}
	for i < n {
		c := line[i]
		if hasPrefixAt(line, i, "/*") {
			closeAt := indexRunes(line, []rune("*/"), i+2)
			if closeAt < 0 {
				emitSpan(&out, i, n-i, tokComment)
				return out, stBlockComment
			}
			emitSpan(&out, i, closeAt+2-i, tokComment)
			i = closeAt + 2
			continue
		}
		if c == '"' || c == '\'' {
			end := scanString(line, i, c, false)
			emitSpan(&out, i, end-i, tokString)
			i = end
			continue
		}
		if c == '@' {
			j := i + 1
			for j < n && (isIdentPart(line[j]) || line[j] == '-') {
				j++
			}
			emitSpan(&out, i, j-i, tokKeyword)
			i = j
			continue
		}
		if c == '!' {
			j := i + 1
			for j < n && unicode.IsLetter(line[j]) {
				j++
			}
			emitSpan(&out, i, j-i, tokKeyword)
			i = j
			continue
		}
		if c == '#' {
			j := i + 1
			for j < n && (isIdentPart(line[j]) || line[j] == '-') {
				j++
			}
			body := line[i+1 : j]
			hex := len(body) == 3 || len(body) == 4 || len(body) == 6 || len(body) == 8
			for _, h := range body {
				l := unicode.ToLower(h)
				if !((h >= '0' && h <= '9') || (l >= 'a' && l <= 'f')) {
					hex = false
					break
				}
			}
			t := tokType
			if hex {
				t = tokNumber
			}
			emitSpan(&out, i, j-i, t)
			i = j
			continue
		}
		if c == '.' && i+1 < n && isIdentStart(line[i+1]) {
			j := i + 1
			for j < n && (isIdentPart(line[j]) || line[j] == '-') {
				j++
			}
			emitSpan(&out, i, j-i, tokType)
			i = j
			continue
		}
		if unicode.IsDigit(c) || (c == '.' && i+1 < n && unicode.IsDigit(line[i+1])) {
			end := scanNumber(line, i)
			if end < n && line[end] == '%' {
				end++
			}
			emitSpan(&out, i, end-i, tokNumber)
			i = end
			continue
		}
		if isIdentStart(c) {
			j := i + 1
			for j < n && (isIdentPart(line[j]) || line[j] == '-') {
				j++
			}
			k := j
			for k < n && isSpace(line[k]) {
				k++
			}
			if k < n && line[k] == ':' {
				emitSpan(&out, i, j-i, tokType)
			}
			i = j
			continue
		}
		i++
	}
	return out, stNormal
}

func scanMarkdown(line []rune, startState int) ([]hlSpan, int) {
	var out []hlSpan
	n := len(line)
	lead := 0
	for lead < n && isSpace(line[lead]) {
		lead++
	}
	if hasPrefixAt(line, lead, "```") {
		emitSpan(&out, lead, n-lead, tokString)
		if startState == stBlockComment {
			return out, stNormal
		}
		return out, stBlockComment
	}
	if startState == stBlockComment {
		emitSpan(&out, 0, n, tokString)
		return out, stBlockComment
	}
	if lead < n && line[lead] == '#' {
		emitSpan(&out, lead, n-lead, tokKeyword)
		return out, stNormal
	}
	if lead < n && line[lead] == '>' {
		emitSpan(&out, lead, n-lead, tokComment)
		return out, stNormal
	}
	i := 0
	if lead < n && (line[lead] == '-' || line[lead] == '*' || line[lead] == '+') && lead+1 < n && line[lead+1] == ' ' {
		emitSpan(&out, lead, 1, tokType)
		i = lead + 1
	} else {
		j := lead
		for j < n && unicode.IsDigit(line[j]) {
			j++
		}
		if j > lead && j < n && line[j] == '.' {
			emitSpan(&out, lead, j-lead+1, tokType)
			i = j + 1
		}
	}
	for i < n {
		if line[i] == '`' {
			closeAt := indexRune(line, '`', i+1)
			end := n
			if closeAt >= 0 {
				end = closeAt + 1
			}
			emitSpan(&out, i, end-i, tokString)
			i = end
			continue
		}
		if line[i] == '(' && i > 0 && line[i-1] == ']' {
			closeAt := indexRune(line, ')', i+1)
			end := n
			if closeAt >= 0 {
				end = closeAt + 1
			}
			emitSpan(&out, i, end-i, tokType)
			i = end
			continue
		}
		i++
	}
	return out, stNormal
}

var mermaidKeywords = wordSet("graph flowchart sequenceDiagram classDiagram stateDiagram " +
	"erDiagram journey gantt pie quadrantChart mindmap timeline gitGraph requirementDiagram " +
	"block sankey xychart packet architecture kanban radar treemap " +
	"subgraph end direction participant actor activate deactivate note loop alt else opt " +
	"par and rect critical option break autonumber create destroy box link links class " +
	"classDef click call callback href style linkStyle state namespace title accTitle " +
	"accDescr section dateFormat axisFormat excludes todayMarker commit branch checkout merge as")

var mermaidModifiers = wordSet("LR RL TB TD BT of over left right")

func isLinkChar(c rune) bool {
	return c == '-' || c == '=' || c == '.' || c == '<' || c == '>' || c == '~'
}

func scanMermaidLabel(s []rune, i int) int {
	open := s[i]
	closer := '}'
	switch open {
	case '[':
		closer = ']'
	case '(':
		closer = ')'
	}
	n := len(s)
	depth := 0
	j := i
	for j < n {
		c := s[j]
		if c == '"' {
			j = scanString(s, j, '"', false)
			continue
		}
		if c == open {
			depth++
		} else if c == closer {
			depth--
			if depth == 0 {
				return j + 1
			}
		}
		j++
	}
	return n
}

func scanMermaid(line []rune, startState int) ([]hlSpan, int) {
	var out []hlSpan
	n := len(line)
	i := 0
	if startState == stBlockComment {
		closeAt := indexRunes(line, []rune("}%%"), 0)
		if closeAt < 0 {
			emitSpan(&out, 0, n, tokComment)
			return out, stBlockComment
		}
		emitSpan(&out, 0, closeAt+3, tokComment)
		i = closeAt + 3
	}
	colonOpensLabel := false
	for i < n {
		c := line[i]
		if c == '%' && i+1 < n && line[i+1] == '%' {
			if hasPrefixAt(line, i, "%%{") {
				closeAt := indexRunes(line, []rune("}%%"), i+3)
				if closeAt < 0 {
					emitSpan(&out, i, n-i, tokComment)
					return out, stBlockComment
				}
				emitSpan(&out, i, closeAt+3-i, tokComment)
				i = closeAt + 3
				continue
			}
			emitSpan(&out, i, n-i, tokComment)
			return out, stNormal
		}
		if c == '"' {
			j := scanString(line, i, '"', false)
			emitSpan(&out, i, j-i, tokString)
			i = j
			continue
		}
		if c == '[' || c == '{' || c == '(' {
			j := scanMermaidLabel(line, i)
			emitSpan(&out, i, j-i, tokString)
			i = j
			continue
		}
		if isLinkChar(c) {
			j := i
			for j < n {
				if isLinkChar(line[j]) {
					j++
					continue
				}
				if line[j] == '|' && j+1 < n && isLinkChar(line[j+1]) {
					j++
					continue
				}
				break
			}
			if j-i >= 2 && j < n && (line[j] == 'o' || line[j] == 'x') && (j+1 >= n || !isIdentPart(line[j+1])) {
				j++
			}
			if j-i >= 2 {
				emitSpan(&out, i, j-i, tokType)
				colonOpensLabel = true
			}
			i = j
			continue
		}
		if c == '|' {
			closeAt := indexRune(line, '|', i+1)
			if closeAt > i {
				emitSpan(&out, i, closeAt+1-i, tokString)
				i = closeAt + 1
				continue
			}
			i++
			continue
		}
		if c == ':' && colonOpensLabel {
			stop := indexRunes(line, []rune("%%"), i)
			if stop < 0 {
				stop = n
			}
			emitSpan(&out, i, stop-i, tokString)
			i = stop
			continue
		}
		if isIdentStart(c) {
			j := i
			for j < n && isIdentPart(line[j]) {
				j++
			}
			word := string(line[i:j])
			if mermaidKeywords[word] {
				emitSpan(&out, i, j-i, tokKeyword)
				if word == "note" {
					colonOpensLabel = true
				}
			} else if mermaidModifiers[word] {
				emitSpan(&out, i, j-i, tokType)
			}
			i = j
			continue
		}
		if unicode.IsDigit(c) {
			j := scanNumber(line, i)
			emitSpan(&out, i, j-i, tokNumber)
			i = j
			continue
		}
		i++
	}
	return out, stNormal
}

func wordSet(words string) map[string]bool {
	m := map[string]bool{}
	for _, w := range strings.Fields(words) {
		m[w] = true
	}
	return m
}

func generic(keywords, types string) *rules {
	return &rules{family: famGeneric, keywords: wordSet(keywords), types: wordSet(types)}
}

// languageRules is the rule table for each canonical language id.
var languageRules = func() map[string]*rules {
	t := map[string]*rules{}
	r := generic("and as assert async await break class continue def del elif else except finally for from "+
		"global if import in is lambda nonlocal not or pass raise return try while with yield match case",
		"True False None self cls int str float bool list dict set tuple bytes object print len "+
			"range super type isinstance Exception")
	r.lineComment, r.stringDelims, r.tripleQuotes, r.atDecorator = "#", `"'`, true, true
	t["python"] = r

	r = generic("break case catch class const continue debugger default delete do else export extends finally "+
		"for function if import in instanceof let new return super switch this throw try typeof var "+
		"void while with yield async await of static get set",
		"true false null undefined NaN Infinity console document window Math JSON Object Array String "+
			"Number Boolean Promise Map Set Symbol")
	r.lineComment, r.blockStart, r.blockEnd, r.stringDelims, r.backtickString = "//", "/*", "*/", `"'`, true
	t["javascript"] = r

	r = generic("alignas alignof and asm auto break case catch class const constexpr const_cast continue "+
		"decltype default delete do dynamic_cast else enum explicit export extern for friend goto if "+
		"inline mutable namespace new noexcept operator or private protected public register "+
		"reinterpret_cast return sizeof static static_assert static_cast struct switch template this "+
		"throw try typedef typename union using virtual volatile while not nullptr override final",
		"bool char char8_t char16_t char32_t double float int long short signed unsigned void wchar_t "+
			"true false size_t string vector map set std uint8_t int32_t int64_t uint32_t uint64_t")
	r.lineComment, r.blockStart, r.blockEnd, r.stringDelims, r.hashPreproc = "//", "/*", "*/", `"'`, true
	t["cpp"] = r

	r = generic("abstract assert break case catch class const continue default do else enum extends final "+
		"finally for goto if implements import instanceof interface native new package private "+
		"protected public return static strictfp super switch synchronized this throw throws "+
		"transient try void volatile while var record yield sealed permits",
		"boolean byte char double float int long short String Object Integer Boolean Double List Map "+
			"Set true false null System Math Exception")
	r.lineComment, r.blockStart, r.blockEnd, r.stringDelims, r.atDecorator = "//", "/*", "*/", `"'`, true
	t["java"] = r

	r = generic("break default func interface select case defer go map struct chan else goto package "+
		"switch const fallthrough if range type continue for import return var",
		"bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune string "+
			"uint uint8 uint16 uint32 uint64 uintptr true false nil append cap close copy delete len "+
			"make new panic print println recover")
	r.lineComment, r.blockStart, r.blockEnd, r.stringDelims, r.backtickString = "//", "/*", "*/", `"'`, true
	t["go"] = r

	r = generic("as async await break const continue crate dyn else enum extern false fn for if impl in "+
		"let loop match mod move mut pub ref return self Self static struct super trait true type "+
		"unsafe use where while yield",
		"bool char str String i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 Option "+
			"Result Vec Box Some None Ok Err")
	r.lineComment, r.blockStart, r.blockEnd, r.stringDelims = "//", "/*", "*/", `"'`
	t["rust"] = r

	r = generic("abstract as async await break case catch class const constructor continue declare default "+
		"delete do else enum export extends finally for from function get if implements import in "+
		"infer instanceof interface keyof let namespace new of private protected public readonly "+
		"return satisfies set static super switch this throw try type typeof var void while with yield",
		"any bigint boolean never number object string symbol unknown undefined null Array Date Error "+
			"Map Promise Record Set Partial Required Readonly Pick Omit")
	r.lineComment, r.blockStart, r.blockEnd, r.stringDelims, r.backtickString = "//", "/*", "*/", `"'`, true
	t["typescript"] = r

	r = generic("abstract as async await base break case catch checked class const continue default "+
		"delegate do else enum event explicit extern finally fixed for foreach goto if implicit in "+
		"interface internal is lock namespace new operator out override params private protected "+
		"public readonly record ref return sealed sizeof stackalloc static struct switch this throw "+
		"try typeof unchecked unsafe using virtual volatile while yield",
		"bool byte char decimal double float int long object sbyte short string uint ulong ushort void "+
			"dynamic var true false null String Object Task List Dictionary IEnumerable")
	r.lineComment, r.blockStart, r.blockEnd, r.stringDelims, r.hashPreproc = "//", "/*", "*/", `"'`, true
	t["csharp"] = r

	r = generic("as break case catch const continue default delete do else enum export extends finally "+
		"for function if import in instanceof let new of pragma property readonly required return "+
		"signal switch this throw try typeof var void while with yield on id",
		"bool color date double font int list matrix4x4 point quaternion real rect size string url "+
			"variant var Item Rectangle Text MouseArea Component QtObject ApplicationWindow true false "+
			"null undefined Qt")
	r.lineComment, r.blockStart, r.blockEnd, r.stringDelims, r.backtickString = "//", "/*", "*/", `"'`, true
	t["qml"] = r

	r = generic("select from where insert into values update set delete create table drop alter add "+
		"column index view join inner left right outer full on group by order having limit offset "+
		"distinct as and or not null is in like between exists union all primary key foreign "+
		"references default unique check constraint begin commit rollback transaction case when "+
		"then else end asc desc count sum avg min max with",
		"int integer varchar char text boolean date datetime timestamp decimal numeric float double "+
			"serial bigint smallint real blob")
	r.caseInsensitive, r.lineComment, r.blockStart, r.blockEnd = true, "--", "/*", "*/"
	r.stringDelims, r.doubledQuoteEscape = "'", true
	t["sql"] = r

	r = generic("if then else elif fi case esac for while until do done in function select time return "+
		"break continue local export readonly declare shift exit source",
		"echo printf cd ls cp mv rm mkdir cat grep sed awk find test read set unset true false")
	r.lineComment, r.stringDelims, r.dollarVar, r.noFunctionCalls = "#", `"'`, true, true
	t["bash"] = r

	r = generic("true false null", "")
	r.stringDelims, r.noFunctionCalls = `"`, true
	t["json"] = r

	t["html"] = &rules{family: famMarkup}
	t["xml"] = &rules{family: famMarkup}
	t["css"] = &rules{family: famCSS}
	t["markdown"] = &rules{family: famMarkdown}
	t["mermaid"] = &rules{family: famMermaid}
	return t
}()

var languageAliases = map[string]string{
	"python": "python", "py": "python", "python3": "python",
	"javascript": "javascript", "js": "javascript", "node": "javascript", "jsx": "javascript", "mjs": "javascript",
	"cpp": "cpp", "c++": "cpp", "cxx": "cpp", "cc": "cpp", "c": "cpp", "h": "cpp", "hpp": "cpp",
	"java": "java",
	"go":   "go", "golang": "go",
	"rust": "rust", "rs": "rust",
	"typescript": "typescript", "ts": "typescript", "tsx": "typescript",
	"csharp": "csharp", "cs": "csharp", "c#": "csharp",
	"qml":  "qml",
	"html": "html", "htm": "html", "xhtml": "html",
	"css": "css",
	"sql": "sql", "mysql": "sql", "postgres": "sql", "postgresql": "sql",
	"bash": "bash", "sh": "bash", "shell": "bash", "zsh": "bash",
	"json": "json",
	"xml":  "xml", "svg": "xml",
	"markdown": "markdown", "md": "markdown",
	"mermaid": "mermaid",
}

func canonicalLanguage(name string) string {
	return languageAliases[strings.ToLower(trimSpace(name))]
}

// highlightSpans classifies a whole code block's text, keeping state from
// line to line, in rune offsets of the whole text. An unknown language has
// no spans.
func highlightSpans(language, text string) []hlSpan {
	canon := canonicalLanguage(language)
	if canon == "" {
		return nil
	}
	r := languageRules[canon]
	var out []hlSpan
	state := stNormal
	offset := 0
	for _, l := range strings.Split(text, "\n") {
		line := []rune(l)
		var spans []hlSpan
		switch r.family {
		case famMarkup:
			spans, state = scanMarkup(line, state)
		case famCSS:
			spans, state = scanCSS(line, state)
		case famMarkdown:
			spans, state = scanMarkdown(line, state)
		case famMermaid:
			spans, state = scanMermaid(line, state)
		default:
			spans, state = scanGeneric(r, line, state)
		}
		for _, s := range spans {
			out = append(out, hlSpan{s.start + offset, s.length, s.tok})
		}
		offset += len(line) + 1
	}
	return out
}
