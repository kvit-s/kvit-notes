package highlight

// The scanners, one line at a time, ported rule for rule from
// src/content/codelanguages.cpp. Each takes the state the line before ended
// in and returns the state this line ends in.

import (
	"strings"
	"unicode"
)

// generic is the scanner most languages share (scanGeneric): Python,
// JavaScript, TypeScript, C++, C#, Java, Go, Rust, QML, SQL, Bash and JSON,
// told apart only by their rules.
func (sc *scan) generic(r *rules, st state) state {
	s, n, i := sc.s, len(sc.s), 0

	// Continue a comment or a string left open by the line before.
	if st == blockComment && r.blockEnd != "" {
		end := index(s, r.blockEnd, 0)
		if end < 0 {
			sc.emit(0, n, Comment)
			return blockComment
		}
		i = end + len(r.blockEnd)
		sc.emit(0, i, Comment)
	} else if st == tripleDouble || st == tripleSingle {
		q := `"""`
		if st == tripleSingle {
			q = `'''`
		}
		at := index(s, q, 0)
		if at < 0 {
			sc.emit(0, n, String)
			return st
		}
		i = at + 3
		sc.emit(0, i, String)
	}

	for i < n {
		c := s[i]

		// A line comment runs to the end of the line.
		if r.lineComment != "" && hasAt(s, i, r.lineComment) {
			sc.emit(i, n, Comment)
			return normal
		}

		// A block comment.
		if r.blockStart != "" && hasAt(s, i, r.blockStart) {
			end := index(s, r.blockEnd, i+len(r.blockStart))
			if end < 0 {
				sc.emit(i, n, Comment)
				return blockComment
			}
			end += len(r.blockEnd)
			sc.emit(i, end, Comment)
			i = end
			continue
		}

		// A Python triple-quoted string.
		if r.tripleQuotes && (hasAt(s, i, `"""`) || hasAt(s, i, `'''`)) {
			q, open := `"""`, tripleDouble
			if c == '\'' {
				q, open = `'''`, tripleSingle
			}
			at := index(s, q, i+3)
			if at < 0 {
				sc.emit(i, n, String)
				return open
			}
			sc.emit(i, at+3, String)
			i = at + 3
			continue
		}

		// A string, which ends with its line if it is not closed.
		if strings.ContainsRune(r.quotes, c) {
			end := quoted(s, i, c, r.doubledQuote)
			sc.emit(i, end, String)
			i = end
			continue
		}
		if r.backtick && c == '`' {
			end := quoted(s, i, '`', false)
			sc.emit(i, end, String)
			i = end
			continue
		}

		// A preprocessor directive: a # with only spaces before it.
		if r.hashDirective && c == '#' {
			atStart := true
			for _, b := range s[:i] {
				if !unicode.IsSpace(b) {
					atStart = false
					break
				}
			}
			if atStart {
				j := i + 1
				for j < n && isIdentPart(s[j]) {
					j++
				}
				sc.emit(i, j, Keyword)
				i = j
				continue
			}
		}

		// A Python decorator or a Java annotation.
		if r.decorators && c == '@' && i+1 < n && isIdentStart(s[i+1]) {
			j := i + 1
			for j < n && (isIdentPart(s[j]) || s[j] == '.') {
				j++
			}
			sc.emit(i, j, Type)
			i = j
			continue
		}

		// A shell variable, $name or ${...}.
		if r.dollarVar && c == '$' && i+1 < n {
			j := i + 1
			if s[j] == '{' {
				if end := index(s, "}", j); end < 0 {
					j = n
				} else {
					j = end + 1
				}
			} else {
				for j < n && isIdentPart(s[j]) {
					j++
				}
			}
			sc.emit(i, j, Type)
			i = j
			continue
		}

		if unicode.IsDigit(c) || c == '.' && i+1 < n && unicode.IsDigit(s[i+1]) {
			end := number(s, i)
			sc.emit(i, end, Number)
			i = end
			continue
		}

		// A word: a keyword, a type, or a name called as a function.
		if isIdentStart(c) {
			j := i + 1
			for j < n && isIdentPart(s[j]) {
				j++
			}
			word := string(s[i:j])
			if r.foldCase {
				// Qt lowers İ to an i and a combining dot, as Unicode's
				// full case mapping does, where strings.ToLower makes it a
				// plain i, and so "İN" would be the keyword "in".
				word = strings.ToLower(strings.ReplaceAll(word, "İ", "i̇"))
			}
			switch {
			case r.keywords[word]:
				sc.emit(i, j, Keyword)
			case r.types[word]:
				sc.emit(i, j, Type)
			case r.calls:
				k := j
				for k < n && unicode.IsSpace(s[k]) {
					k++
				}
				if k < n && s[k] == '(' {
					sc.emit(i, j, Type)
				}
			}
			i = j
			continue
		}

		i++
	}
	return normal
}

// markup is the scanner for HTML and XML (scanMarkup). A tag's name is a
// Keyword, its attributes' names are Types and their quoted values Strings;
// an entity such as &amp; is a Number, and a comment may span lines.
func (sc *scan) markup(st state) state {
	s, n, i := sc.s, len(sc.s), 0

	if st == blockComment {
		end := index(s, "-->", 0)
		if end < 0 {
			sc.emit(0, n, Comment)
			return blockComment
		}
		i = end + 3
		sc.emit(0, i, Comment)
	}

	for i < n {
		if hasAt(s, i, "<!--") {
			end := index(s, "-->", i+4)
			if end < 0 {
				sc.emit(i, n, Comment)
				return blockComment
			}
			sc.emit(i, end+3, Comment)
			i = end + 3
			continue
		}
		if s[i] == '<' {
			// The tag's name, after any '/', '!' or '?'.
			j := i + 1
			for j < n && (s[j] == '/' || s[j] == '!' || s[j] == '?') {
				j++
			}
			name := j
			for j < n && (isIdentPart(s[j]) || s[j] == '-' || s[j] == ':') {
				j++
			}
			sc.emit(name, j, Keyword)
			// Its attributes, up to the '>'.
			for j < n && s[j] != '>' {
				a := s[j]
				switch {
				case a == '"' || a == '\'':
					end := quoted(s, j, a, false)
					sc.emit(j, end, String)
					j = end
				case isIdentStart(a):
					k := j + 1
					for k < n && (isIdentPart(s[k]) || s[k] == '-' || s[k] == ':') {
						k++
					}
					sc.emit(j, k, Type)
					j = k
				default:
					j++
				}
			}
			i = n
			if j < n {
				i = j + 1
			}
			continue
		}
		if s[i] == '&' {
			if semi := index(s, ";", i); semi > i && semi-i <= 10 {
				sc.emit(i, semi+1, Number)
				i = semi + 1
				continue
			}
		}
		i++
	}
	return normal
}

// css is the scanner for CSS (scanCss): comments, which may span lines,
// strings, at-rules, !important, colours, selectors, numbers with their
// units, and a property's name before its ':'.
func (sc *scan) css(st state) state {
	s, n, i := sc.s, len(sc.s), 0

	if st == blockComment {
		end := index(s, "*/", 0)
		if end < 0 {
			sc.emit(0, n, Comment)
			return blockComment
		}
		i = end + 2
		sc.emit(0, i, Comment)
	}

	for i < n {
		c := s[i]
		if hasAt(s, i, "/*") {
			end := index(s, "*/", i+2)
			if end < 0 {
				sc.emit(i, n, Comment)
				return blockComment
			}
			sc.emit(i, end+2, Comment)
			i = end + 2
			continue
		}
		if c == '"' || c == '\'' {
			end := quoted(s, i, c, false)
			sc.emit(i, end, String)
			i = end
			continue
		}
		// An at-rule, such as @media.
		if c == '@' {
			j := i + 1
			for j < n && (isIdentPart(s[j]) || s[j] == '-') {
				j++
			}
			sc.emit(i, j, Keyword)
			i = j
			continue
		}
		// !important.
		if c == '!' {
			j := i + 1
			for j < n && unicode.IsLetter(s[j]) {
				j++
			}
			sc.emit(i, j, Keyword)
			i = j
			continue
		}
		// A colour such as #ff0000 is a Number; an id selector is a Type.
		if c == '#' {
			j := i + 1
			for j < n && (isIdentPart(s[j]) || s[j] == '-') {
				j++
			}
			body := s[i+1 : j]
			hex := len(body) == 3 || len(body) == 4 || len(body) == 6 || len(body) == 8
			for _, h := range body {
				l := unicode.ToLower(h)
				if !(h >= '0' && h <= '9' || l >= 'a' && l <= 'f') {
					hex = false
					break
				}
			}
			if hex {
				sc.emit(i, j, Number)
			} else {
				sc.emit(i, j, Type)
			}
			i = j
			continue
		}
		// A class selector.
		if c == '.' && i+1 < n && isIdentStart(s[i+1]) {
			j := i + 1
			for j < n && (isIdentPart(s[j]) || s[j] == '-') {
				j++
			}
			sc.emit(i, j, Type)
			i = j
			continue
		}
		// A number with its unit or percent sign.
		if unicode.IsDigit(c) || c == '.' && i+1 < n && unicode.IsDigit(s[i+1]) {
			end := number(s, i)
			if end < n && s[end] == '%' {
				end++
			}
			sc.emit(i, end, Number)
			i = end
			continue
		}
		// A word is a property's name when a ':' follows it.
		if isIdentStart(c) {
			j := i + 1
			for j < n && (isIdentPart(s[j]) || s[j] == '-') {
				j++
			}
			k := j
			for k < n && unicode.IsSpace(s[k]) {
				k++
			}
			if k < n && s[k] == ':' {
				sc.emit(i, j, Type)
			}
			i = j
			continue
		}
		i++
	}
	return normal
}

// markdown is the scanner for Markdown shown in a code block (scanMarkdown),
// a light pass: a heading line is a Keyword, a block quote line a Comment, a
// list marker a Type, a code span a String, a link's target a Type, and a
// ``` fence with everything inside it a String.
func (sc *scan) markdown(st state) state {
	s, n := sc.s, len(sc.s)
	lead := 0
	for lead < n && unicode.IsSpace(s[lead]) {
		lead++
	}

	// A fence opens or closes a fenced block; blockComment is inside one.
	if hasAt(s, lead, "```") {
		sc.emit(lead, n, String)
		if st == blockComment {
			return normal
		}
		return blockComment
	}
	if st == blockComment {
		sc.emit(0, n, String)
		return blockComment
	}

	if lead < n && s[lead] == '#' {
		sc.emit(lead, n, Keyword)
		return normal
	}
	if lead < n && s[lead] == '>' {
		sc.emit(lead, n, Comment)
		return normal
	}

	i := 0
	if lead < n && (s[lead] == '-' || s[lead] == '*' || s[lead] == '+') && lead+1 < n && s[lead+1] == ' ' {
		sc.emit(lead, lead+1, Type)
		i = lead + 1
	} else {
		// An ordered list's number and its '.'.
		j := lead
		for j < n && unicode.IsDigit(s[j]) {
			j++
		}
		if j > lead && j < n && s[j] == '.' {
			sc.emit(lead, j+1, Type)
			i = j + 1
		}
	}

	for i < n {
		if s[i] == '`' {
			end := n
			if at := index(s, "`", i+1); at >= 0 {
				end = at + 1
			}
			sc.emit(i, end, String)
			i = end
			continue
		}
		if s[i] == '(' && i > 0 && s[i-1] == ']' {
			end := n
			if at := index(s, ")", i+1); at >= 0 {
				end = at + 1
			}
			sc.emit(i, end, Type)
			i = end
			continue
		}
		i++
	}
	return normal
}

// mermaid is the scanner for Mermaid (scanMermaid). Mermaid is a notation of
// one statement per line rather than a C-like language, and what it means is
// in five things: the %% comment and the %%{ }%% directive, the diagram and
// statement words, the links, the labels in brackets or quotes, and the
// message text after a colon. Everything else, node names above all, stays
// in the text colour.
func (sc *scan) mermaid(st state) state {
	s, n, i := sc.s, len(sc.s), 0

	// A directive left open by the line before.
	if st == blockComment {
		end := index(s, "}%%", 0)
		if end < 0 {
			sc.emit(0, n, Comment)
			return blockComment
		}
		i = end + 3
		sc.emit(0, i, Comment)
	}

	// Whether a colon on this line starts label text. A sequence message and
	// an edge label come after a link, and a note's text after "note"; a
	// class member (Animal : +int age) and a style (style A fill:#f9f) are
	// neither, and stay plain.
	colonLabel := false

	for i < n {
		c := s[i]

		if c == '%' && i+1 < n && s[i+1] == '%' {
			if hasAt(s, i, "%%{") {
				end := index(s, "}%%", i+3)
				if end < 0 {
					sc.emit(i, n, Comment)
					return blockComment
				}
				sc.emit(i, end+3, Comment)
				i = end + 3
				continue
			}
			sc.emit(i, n, Comment)
			return normal
		}
		if c == '"' {
			j := quoted(s, i, '"', false)
			sc.emit(i, j, String)
			i = j
			continue
		}
		if c == '[' || c == '{' || c == '(' {
			j := mermaidLabel(s, i)
			sc.emit(i, j, String)
			i = j
			continue
		}
		if isLinkRune(c) {
			j := i
			for j < n {
				if isLinkRune(s[j]) {
					j++
					continue
				}
				// A class diagram's arrowhead bar (<|--, ..|>) is part of
				// the link; a flowchart's edge label bar (-->|yes|) is not,
				// and what follows the bar tells them apart.
				if s[j] == '|' && j+1 < n && isLinkRune(s[j+1]) {
					j++
					continue
				}
				break
			}
			// A flowchart link may end in a letter: --o, --x.
			if j-i >= 2 && j < n && (s[j] == 'o' || s[j] == 'x') && (j+1 >= n || !isIdentPart(s[j+1])) {
				j++
			}
			if j-i >= 2 {
				sc.emit(i, j, Type)
				colonLabel = true
			}
			i = j
			continue
		}
		if c == '|' {
			if end := index(s, "|", i+1); end > i {
				sc.emit(i, end+1, String)
				i = end + 1
				continue
			}
			i++
			continue
		}
		if c == ':' && colonLabel {
			stop := index(s, "%%", i)
			if stop < 0 {
				stop = n
			}
			sc.emit(i, stop, String)
			i = stop
			continue
		}
		if isIdentStart(c) {
			j := i
			for j < n && isIdentPart(s[j]) {
				j++
			}
			word := string(s[i:j])
			if mermaidKeywords[word] {
				sc.emit(i, j, Keyword)
				if word == "note" {
					colonLabel = true
				}
			} else if mermaidModifiers[word] {
				sc.emit(i, j, Type)
			}
			i = j
			continue
		}
		if unicode.IsDigit(c) {
			j := number(s, i)
			sc.emit(i, j, Number)
			i = j
			continue
		}
		i++
	}
	return normal
}

// mermaidLabel is the end of the label whose bracket is at s[i], such as
// [Start], ((Round)) or {"text"}: just past the bracket that closes it, or
// the end of the line when none does, as while it is being typed. Brackets
// are counted, so a doubled bracket closes in the right place, and a quoted
// string inside is skipped, so a bracket in its text does not end the label
// (scanMermaidLabel).
func mermaidLabel(s []rune, i int) int {
	open, shut := s[i], '}'
	switch open {
	case '[':
		shut = ']'
	case '(':
		shut = ')'
	}
	depth := 0
	for j := i; j < len(s); {
		switch c := s[j]; {
		case c == '"':
			j = quoted(s, j, '"', false)
			continue
		case c == open:
			depth++
		case c == shut:
			depth--
			if depth == 0 {
				return j + 1
			}
		}
		j++
	}
	return len(s)
}

// isLinkRune reports whether c is one a Mermaid link (-->, ==>, -.->, <|..)
// is made of. The edge label's bar is not one: it is told apart by what
// follows it.
func isLinkRune(c rune) bool {
	return strings.ContainsRune("-=.<>~", c)
}

// number is the end of the number starting at s[i], a digit or a '.' before
// a digit (scanNumber): a 0x, 0b or 0o number, or digits with underscores, a
// fraction and an exponent, followed by any letters, such as C++'s f and u or
// a CSS unit.
func number(s []rune, i int) int {
	n, j := len(s), i
	if s[j] == '0' && j+1 < n && strings.ContainsRune("xXbBoO", s[j+1]) {
		j += 2
		for j < n && isIdentPart(s[j]) {
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

// quoted is the end of the string whose opening quote q is at s[i]: just
// past its closing quote, or the end of the line when it has none
// (scanString). A backslash escapes the rune after it, except when doubled
// is set, as for SQL, where a doubled quote stands for one quote instead.
func quoted(s []rune, i int, q rune, doubled bool) int {
	n, j := len(s), i+1
	for j < n {
		c := s[j]
		if c == '\\' && !doubled {
			j += 2
			continue
		}
		if c == q {
			if doubled && j+1 < n && s[j+1] == q {
				j += 2
				continue
			}
			return j + 1
		}
		j++
	}
	return n
}

// isIdentStart reports whether a word can start with c: a letter or '_'.
func isIdentStart(c rune) bool {
	return unicode.IsLetter(c) || c == '_'
}

// isIdentPart reports whether a word can go on with c: a letter, a number or
// '_'.
func isIdentPart(c rune) bool {
	return unicode.IsLetter(c) || unicode.IsNumber(c) || c == '_'
}

// hasAt reports whether s has sub at offset i.
func hasAt(s []rune, i int, sub string) bool {
	for _, r := range sub {
		if i >= len(s) || s[i] != r {
			return false
		}
		i++
	}
	return true
}

// index is the offset of the first sub in s at or after from, or -1 when
// there is none.
func index(s []rune, sub string, from int) int {
	for i := from; i < len(s); i++ {
		if hasAt(s, i, sub) {
			return i
		}
	}
	return -1
}
