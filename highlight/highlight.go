// Package highlight colours the text of a code block by language, as Kvit's
// code blocks do (features.md 1.2.7). It is a port of the Qt app's
// src/content/codelanguages.cpp: a table of words and of comment and string
// markers for each language, read by one scanner shared by most languages,
// and scanners of their own for HTML and XML, CSS, Markdown and Mermaid. Each
// run the scanners find is drawn in one of the theme's five code colours
// (codeKeyword, codeType, codeString, codeComment, codeNumber); everything
// else is drawn in the text colour.
//
// A code block is coloured a line at a time. A block comment, an HTML
// comment or a Python triple-quoted string left open at the end of a line
// continues on the next, so each line starts in the state the line before it
// ended in, as the Qt highlighter's block state does.
package highlight

// Class is what a token is drawn as. Each class but Plain is one of the
// theme's five code colours.
type Class int

const (
	// Plain is drawn in the ordinary text colour. Highlight never returns a
	// span of it.
	Plain Class = iota
	// Keyword is a language's reserved word, a C or C# preprocessor
	// directive, an HTML or XML tag's name, a CSS at-rule or !important, a
	// Markdown heading, and a Mermaid statement word (codeKeyword).
	Keyword
	// Type is a type or built-in name, a name called as a function, a Python
	// decorator or Java annotation, a shell variable, an HTML attribute's
	// name, a CSS selector or property name, a Markdown list marker or link
	// target, and a Mermaid link or layout direction (codeType).
	Type
	// String is a quoted string, a Markdown code span or fenced block, and a
	// Mermaid label (codeString).
	String
	// Comment is a comment, and a Markdown block quote (codeComment).
	Comment
	// Number is a number, an HTML entity, and a CSS colour such as #ff0000
	// (codeNumber).
	Number
)

// Span is a coloured run of a code block's text, in rune offsets.
type Span struct {
	Start, End int
	Class      Class
}

// Highlight colours a whole code block's text for a language (any name or
// alias Kvit accepts, case-insensitive; an unknown or empty language gives no
// spans). The spans are in order and do not overlap, and plain text between
// them has none. No span covers a newline: a comment that spans lines is one
// span on each line (highlightSpans in codelanguages.cpp).
func Highlight(lang, text string) []Span {
	r := table[Canonical(lang)]
	if r == nil {
		return nil
	}
	rs := []rune(text)
	var sc scan
	st := normal
	for start := 0; ; {
		end := start
		for end < len(rs) && rs[end] != '\n' {
			end++
		}
		sc.s, sc.base = rs[start:end], start
		st = sc.line(r, st)
		if end == len(rs) {
			break
		}
		start = end + 1
	}
	return sc.out
}

// state is what a line leaves open for the next line to continue (the
// carry-state of codelanguages.cpp). It only has a meaning between lines of
// one code block in one language.
type state int

const (
	normal state = iota
	// blockComment is inside a /* */ comment, an HTML <!-- --> comment, a
	// Mermaid %%{ }%% directive, or a Markdown ``` fence.
	blockComment
	// tripleDouble is inside a Python """ string.
	tripleDouble
	// tripleSingle is inside a Python ''' string.
	tripleSingle
)

// scan is the line of a code block being coloured, and the block's spans so
// far.
type scan struct {
	// s is the line, without its newline.
	s []rune
	// base is the offset of the line's first rune in the whole text.
	base int
	out  []Span
}

// emit adds a span for the line's runes [start, end), unless it is empty.
func (sc *scan) emit(start, end int, c Class) {
	if end > start {
		sc.out = append(sc.out, Span{sc.base + start, sc.base + end, c})
	}
}

// line colours one line, starting in the state the line before it ended in,
// and returns the state it ends in (highlightLine in codelanguages.cpp).
func (sc *scan) line(r *rules, st state) state {
	switch r.family {
	case markupFamily:
		return sc.markup(st)
	case cssFamily:
		return sc.css(st)
	case markdownFamily:
		return sc.markdown(st)
	case mermaidFamily:
		return sc.mermaid(st)
	}
	return sc.generic(r, st)
}
