package mermaid

import (
	"strings"
	"unicode"
)

// TokenKind is what a token of the flowchart lexer is.
type TokenKind int

const (
	TokenWord      TokenKind = iota // a node id or a keyword
	TokenShape                      // a bracketed label; Shape and Text are set
	TokenShapeData                  // an `@{ … }` block after a node id; Text is its body
	TokenEdge                       // a link; Stroke, the arrows, MinLen and EdgeLabel are set
	TokenPipe                       // `|`, around an edge label
	TokenAmp                        // `&`, between the nodes of a list
	TokenSep                        // the end of a statement: a line break or `;`
)

// Token is one token of a flowchart's source. Shape brackets and links are
// whole tokens, with what they mean already read, because a grammar of single
// characters cannot tell them apart; the text between a shape's brackets
// (spaces, punctuation and quotes included) is one token.
type Token struct {
	Kind   TokenKind
	Text   string // a word; a shape's label; a data block's body
	Line   int    // one-based
	Column int    // one-based, in runes
	Offset int    // where the token starts in the source, in runes
	Length int    // the runes the token covers

	// A shape's text between its brackets as written, before quotes are
	// stripped; a brace block's body. LabelOffset is -1 for other tokens.
	LabelOffset int
	LabelLength int

	Shape NodeShape // a shape token's outline

	// A link's stroke, arrowheads and length.
	Stroke     EdgeStroke
	ArrowStart bool
	ArrowEnd   bool
	Invisible  bool // a `~~~` link
	MinLen     int
	EdgeLabel  string // an inline `-- text -->` label; a `|text|` label is separate
}

func newToken(kind TokenKind) Token {
	return Token{Kind: kind, Line: 1, Column: 1, LabelOffset: -1, ArrowEnd: true, MinLen: 1}
}

// shapeBracket is an opening bracket, its closer and the shape they make.
type shapeBracket struct {
	open, close string
	shape       NodeShape
}

// shapeBrackets are tried in order, longest opener first so `([` wins over
// `(`. One opener can have two closers (the trapezoids of flow.jison), so a
// closer that is not found passes on to the next entry.
var shapeBrackets = []shapeBracket{
	{"(((", ")))", ShapeDoubleCircle},
	{"([", "])", ShapeStadium},
	{"[[", "]]", ShapeSubroutine},
	{"[(", ")]", ShapeCylinder},
	{"((", "))", ShapeCircle},
	{"{{", "}}", ShapeHexagon},
	{"(-", "-)", ShapeEllipse},
	{"[/", "/]", ShapeParallelogram},
	{"[/", `\]`, ShapeTrapezoid},
	{`[\`, `\]`, ShapeParallelogramAlt},
	{`[\`, "/]", ShapeTrapezoidAlt},
	{"[", "]", ShapeRect},
	{"(", ")", ShapeRoundRect},
	{"{", "}", ShapeRhombus},
	{">", "]", ShapeOdd},
}

// Lex splits a flowchart's source into tokens, with a TokenSep at every
// statement boundary and one at the end. `%%` comments are dropped. The
// source is not rewritten: a `\r` counts as a line break where the lexer
// looks for one, so offsets stay offsets into the text as stored.
func Lex(source string) []Token {
	return LexRunes([]rune(source))
}

// LexRunes is Lex on a source already split into runes.
func LexRunes(src []rune) []Token {
	l := &lexer{src: src, line: 1}
	l.run()
	return l.tokens
}

type lexer struct {
	src       []rune
	tokens    []Token
	pos       int
	line      int
	lineStart int
	inPipe    bool // between `|` and `|` a `>` is label text, not a shape
}

// at is the rune at i, or 0 past the end.
func (l *lexer) at(i int) rune {
	if i < len(l.src) {
		return l.src[i]
	}
	return 0
}

func (l *lexer) column() int { return l.pos - l.lineStart + 1 }

// hasAt reports whether the source holds s at i.
func (l *lexer) hasAt(i int, s string) bool {
	for _, r := range s {
		if i >= len(l.src) || l.src[i] != r {
			return false
		}
		i++
	}
	return true
}

func (l *lexer) last() *Token {
	if len(l.tokens) == 0 {
		return nil
	}
	return &l.tokens[len(l.tokens)-1]
}

func (l *lexer) emitSep() {
	l.inPipe = false // a statement boundary closes any pipe label
	// Runs of separators and blank lines make one boundary.
	if t := l.last(); t != nil && t.Kind == TokenSep {
		return
	}
	t := newToken(TokenSep)
	t.Line = l.line
	t.Column = l.column()
	t.Offset = l.pos
	l.tokens = append(l.tokens, t)
}

func (l *lexer) run() {
	for l.pos < len(l.src) {
		c := l.src[l.pos]
		switch {
		case c == '\n':
			l.emitSep()
			l.pos++
			l.line++
			l.lineStart = l.pos
			continue
		case c == '\r':
			// In CRLF the `\n` makes the line break; a lone CR (the old
			// Mac ending) makes it itself.
			if l.at(l.pos+1) != '\n' {
				l.emitSep()
				l.line++
				l.lineStart = l.pos + 1
			}
			l.pos++
			continue
		case c == ' ' || c == '\t':
			l.pos++
			continue
		case c == ';':
			l.emitSep()
			l.pos++
			continue
		}
		if l.matchComment() || l.matchBraceBlock() {
			continue
		}
		if c == '|' || c == '&' {
			kind := TokenPipe
			if c == '&' {
				kind = TokenAmp
			}
			t := newToken(kind)
			t.Line = l.line
			t.Column = l.column()
			t.Offset = l.pos
			t.Length = 1
			l.tokens = append(l.tokens, t)
			if c == '|' {
				l.inPipe = !l.inPipe
			}
			l.pos++
			continue
		}
		if l.matchEdge() || l.matchShape() {
			continue
		}
		l.matchWord()
	}
	l.emitSep()
}

// matchComment skips a `%%` comment, a one-line `%%{init}%%` directive
// included, up to the end of the line.
func (l *lexer) matchComment() bool {
	if l.at(l.pos) != '%' || l.at(l.pos+1) != '%' {
		return false
	}
	for l.pos < len(l.src) && l.src[l.pos] != '\n' {
		l.pos++
	}
	return true
}

func (l *lexer) matchShape() bool {
	for _, b := range shapeBrackets {
		if !l.hasAt(l.pos, b.open) {
			continue
		}
		// The odd shape `A>text]` exists only straight after a node id
		// (flow.jison: `idString TAGEND text SQE` with no SPACE token), so a
		// `>` anywhere else, in a pipe label or a `graph >` header, stays a
		// word.
		if b.shape == ShapeOdd {
			prev := l.last()
			if l.inPipe || l.pos == 0 || prev == nil || prev.Kind != TokenWord ||
				unicode.IsSpace(l.src[l.pos-1]) {
				continue
			}
		}
		startCol := l.column()
		contentStart := l.pos + len(b.open)
		// Find the closer, skipping quoted text so a `]` inside "…" does not
		// close the shape.
		found := -1
		inQuote := false
		for i := contentStart; i < len(l.src); i++ {
			ch := l.src[i]
			if ch == '"' {
				inQuote = !inQuote
				continue
			}
			if ch == '\n' {
				break // a shape does not span lines
			}
			if !inQuote && l.hasAt(i, b.close) {
				found = i
				break
			}
		}
		if found < 0 {
			continue // this closer is not there: try the next pair
		}
		// The label loses its quotes and is plain text, never HTML.
		label := trimSpace(string(l.src[contentStart:found]))
		label = stripQuotePair(label, '"')
		// A Markdown string "`text`" (flow.jison md_string) keeps its
		// backticks after the quotes go; its text is taken as plain text.
		if s, ok := cutPair(label, '`'); ok {
			label = trimSpace(s)
		}
		t := newToken(TokenShape)
		t.Shape = b.shape
		t.Text = label
		t.Line = l.line
		t.Column = startCol
		t.Offset = l.pos
		t.Length = found + len(b.close) - l.pos
		t.LabelOffset = contentStart
		t.LabelLength = found - contentStart
		l.tokens = append(l.tokens, t)
		l.pos = found + len(b.close)
		return true
	}
	return false
}

// matchBraceBlock reads an `@{ … }` shape-data block after a node id
// (`A@{ shape: circle }`, flow.jison shapeData) or the body of a multi-line
// `accDescr { … }`. Both can span lines and hold quoted text with `}` in it.
// The first becomes a TokenShapeData and the second a TokenShape; any other
// `{` is left to the rhombus bracket.
func (l *lexer) matchBraceBlock() bool {
	prev := l.last()
	if l.at(l.pos) != '{' || prev == nil || prev.Kind != TokenWord {
		return false
	}
	shapeData := len(prev.Text) > 1 && strings.HasSuffix(prev.Text, "@")
	accBlock := strings.HasPrefix(prev.Text, "accDescr")
	if !shapeData && !accBlock {
		return false
	}
	startCol := l.column()
	startLine := l.line
	line := l.line
	lineStart := l.lineStart
	found := -1
	inQuote := false
	for i := l.pos + 1; i < len(l.src); i++ {
		ch := l.src[i]
		if ch == '"' {
			inQuote = !inQuote
		} else if ch == '\n' {
			line++
			lineStart = i + 1
		} else if !inQuote && ch == '}' {
			found = i
			break
		}
	}
	if found < 0 {
		return false // not closed: the ordinary rules take over
	}
	t := newToken(TokenShape)
	if shapeData {
		t.Kind = TokenShapeData
	}
	t.Text = string(l.src[l.pos+1 : found])
	t.Line = startLine
	t.Column = startCol
	t.Offset = l.pos
	if shapeData {
		t.Offset = l.pos - 1 // shape data starts at the `@`
	}
	t.Length = found + 1 - t.Offset
	// Where the body is, so the parser can give each `key: value` inside it a
	// span and an edit can rewrite one in place.
	t.LabelOffset = l.pos + 1
	t.LabelLength = found - l.pos - 1
	if shapeData {
		prev.Text = prev.Text[:len(prev.Text)-1] // the `@` is not part of the id
		prev.Length--
	}
	l.tokens = append(l.tokens, t)
	l.pos = found + 1
	l.line = line
	l.lineStart = lineStart
	return true
}

func isLinkRune(r rune) bool { return r == '-' || r == '=' || r == '.' }

func (l *lexer) matchEdge() bool {
	src := l.src
	n := len(src)
	c := src[l.pos]
	// `~~~` is the invisible link: ranked like an edge, drawn as nothing.
	if c == '~' {
		i := l.pos
		for i < n && src[i] == '~' {
			i++
		}
		count := i - l.pos
		if count < 3 {
			return false
		}
		t := newToken(TokenEdge)
		t.Line = l.line
		t.Column = l.column()
		t.Offset = l.pos
		t.Length = count
		t.Invisible = true
		t.ArrowEnd = false
		t.MinLen = max(1, count-2)
		l.tokens = append(l.tokens, t)
		l.pos = i
		return true
	}
	if c != '<' && c != '-' && c != '=' && c != '.' && c != 'o' && c != 'x' {
		return false
	}

	start := l.pos
	startCol := l.column()
	i := l.pos
	arrowStart := false
	// An arrowhead at the start: `<`, or `o` or `x` right before the link.
	if src[i] == '<' {
		arrowStart = true
		i++
	} else if (src[i] == 'o' || src[i] == 'x') && (l.at(i+1) == '-' || l.at(i+1) == '=') {
		arrowStart = true
		i++
	}
	// The link itself starts with a stroke character.
	if !isLinkRune(l.at(i)) {
		return false
	}

	// Take the whole link, strokes and arrowheads, and an inline
	// `-- text -->` label when the second half ends in an arrowhead.
	bodyStart := i
	for i < n && isLinkRune(src[i]) {
		i++
	}
	firstRunEnd := i

	inlineLabel := ""
	arrowEnd := false
	save := i
	if i < n && (src[i] == ' ' || src[i] == '\t') {
		j := i
		for j < n && (src[j] == ' ' || src[j] == '\t') {
			j++
		}
		textStart := j
		// The label runs up to the stroke run that ends in an arrowhead.
		// Stopping at the first `-`, `=` or `.` would cut ordinary text:
		// `-- well-known v1.2 -->` would end the label at the hyphen and
		// read `-known v1.2 --` as links.
		textEnd, run2 := -1, -1
		for j < n && src[j] != '\n' {
			if !isLinkRune(src[j]) {
				j++
				continue
			}
			r := j
			for r < n && isLinkRune(src[r]) {
				r++
			}
			head := l.at(r)
			// `>` always closes a link. `o` and `x` also begin words, so
			// they close one only when no letter or digit follows;
			// otherwise `-- a-ok --> B` would end at `-o`.
			closes := head == '>' ||
				((head == 'o' || head == 'x') && (r+1 >= n || !isLetterOrNumber(src[r+1])))
			if closes {
				textEnd = j
				run2 = r
				break
			}
			j = r // a hyphen or dot inside the label
		}
		if textEnd < 0 {
			textEnd = j
			run2 = j
		}
		hasHead := run2 < n && (src[run2] == '>' || src[run2] == 'o' || src[run2] == 'x')
		text := trimSpace(string(src[textStart:textEnd]))
		if run2 > textEnd && hasHead && text != "" {
			inlineLabel = text
			i = run2 + 1
			arrowEnd = true
			firstRunEnd = run2 // stroke and length are read from the whole link
		} else {
			i = save // not an inline label: look for an arrowhead below
		}
	}

	if !arrowEnd {
		// An arrowhead straight after the strokes: `-->`, `--x`, `--o`.
		if i < n && (src[i] == '>' || src[i] == 'o' || src[i] == 'x') {
			arrowEnd = true
			i++
		}
	}

	// The stroke and the ranks spanned come from the characters taken.
	stroke := StrokeSolid
	dots, equals, dashCount := false, false, 0
	for _, ch := range src[bodyStart:firstRunEnd] {
		switch ch {
		case '.':
			dots = true
		case '=':
			equals = true
			dashCount++
		case '-':
			dashCount++
		}
	}
	if dots {
		stroke = StrokeDotted
	} else if equals {
		stroke = StrokeThick
	}
	minLen := max(1, dashCount-1)

	// A single stroke character with no arrowhead is not a link (a stray `-`
	// in a word): a link needs two, or an arrowhead.
	if dashCount < 2 && !arrowEnd && stroke != StrokeDotted {
		l.pos = start
		return false
	}

	t := newToken(TokenEdge)
	t.Line = l.line
	t.Column = startCol
	t.Offset = start
	t.Length = i - start
	t.Stroke = stroke
	t.ArrowStart = arrowStart
	t.ArrowEnd = arrowEnd
	t.MinLen = minLen
	t.EdgeLabel = inlineLabel
	l.tokens = append(l.tokens, t)
	l.pos = i
	return true
}

func (l *lexer) matchWord() {
	startCol := l.column()
	start := l.pos
loop:
	for l.pos < len(l.src) {
		ch := l.src[l.pos]
		switch ch {
		case '\n', ' ', '\t', ';', '|', '&':
			break loop
		case '[', '(', '{', ']', ')', '}':
			// A shape bracket starts the next token.
			break loop
		case '-':
			// A hyphen between word characters is part of the word
			// (`stroke-width`, `my-node`); one that starts a link (`-->`,
			// `-.->`) ends it.
			if l.pos > start && isLetterOrNumber(l.at(l.pos+1)) {
				l.pos++
				continue
			}
			break loop
		case '=', '<':
			break loop
		case '>':
			if !l.inPipe {
				break loop // may open an odd shape; in a pipe label it is text
			}
		case '~':
			if l.at(l.pos+1) == '~' {
				break loop // the start of a `~~~` link
			}
		case '.':
			if l.at(l.pos+1) == '-' {
				break loop // the start of a link written `.->`
			}
		}
		l.pos++
	}
	if l.pos == start {
		// Nothing taken (a lone character the link and shape rules turned
		// down): take one rune so the lexer always moves. A lone `<` or `>`
		// is kept as a word, because it names a direction in a header
		// (`graph <` is RL, `graph >` is LR in flow.jison).
		ch := l.src[l.pos]
		l.pos++
		if ch == '<' || ch == '>' {
			t := newToken(TokenWord)
			t.Text = string(ch)
			t.Line = l.line
			t.Column = startCol
			t.Offset = l.pos - 1
			t.Length = 1
			l.tokens = append(l.tokens, t)
		}
		return
	}
	t := newToken(TokenWord)
	t.Text = string(l.src[start:l.pos])
	t.Line = l.line
	t.Column = startCol
	t.Offset = start
	t.Length = l.pos - start
	l.tokens = append(l.tokens, t)
}
