package mermaid

import (
	"fmt"
	"strings"
	"unicode"
	"unicode/utf8"
)

// The entity-relationship parser follows erDiagram.jison of mermaid@11.16.0:
// entities (bare, quoted, and `NAME["alias"]`), attribute blocks with types,
// names, PK, FK and UK key lists and quoted comments, relationships with
// every spelling of cardinality (the crow's-foot symbols and the worded
// `one or more` forms), identifying `--` and non-identifying `..`, `.-` and
// `-.` lines (and the worded `to` and `optionally to`), relationship roles,
// direction, classDef, class, style and `:::`, and accTitle and accDescr. It
// is a port of the Qt app's mermaider.cpp.

// erCardWords are the worded cardinalities, longest first.
var erCardWords = []struct {
	word string
	card ErCardinality
}{
	{"one or zero", ZeroOrOne},
	{"zero or one", ZeroOrOne},
	{"zero or more", ZeroOrMore},
	{"zero or many", ZeroOrMore},
	{"one or more", OneOrMore},
	{"one or many", OneOrMore},
	{"many(0)", ZeroOrMore},
	{"many(1)", OneOrMore},
	{"only one", OnlyOne},
	{"many", ZeroOrMore},
	{"one", OnlyOne},
	{"0+", ZeroOrMore},
	{"1+", OneOrMore},
	{"1", OnlyOne},
}

// ParseErDiagram reads an erDiagram's body, front matter already taken off,
// into result.Er. baseOffset is where the body starts in the fence's text.
// Diagnostics give the body's line, counted from one.
func ParseErDiagram(body string, baseOffset int, result *ParseResult) {
	p := erParser{r: result, ast: &result.Er, base: baseOffset, openEntity: -1}
	p.run(body)
}

type erParser struct {
	r                 *ParseResult
	ast               *ErAst
	base              int
	curSpan           Span // the statement being read
	openEntity        int  // the entity whose attribute block is open, or -1
	openLine          int
	entityCapWarned   bool
	relationCapWarned bool
}

func (p *erParser) diag(line int, message string, severity Severity) {
	p.r.diag(line, 1, message, severity)
}

// ensureEntity is the index of the entity written as writtenName, added if
// it is new, or -1 when the name is empty or past the limit.
func (p *erParser) ensureEntity(writtenName string, lineNo int) int {
	id := stripQuotes(writtenName)
	if id == "" {
		return -1
	}
	i := p.ast.IndexOfEntity(id)
	if i < 0 {
		if len(p.ast.Entities) >= MaxNodes {
			if !p.entityCapWarned {
				p.diag(lineNo, fmt.Sprintf("Too many entities (limit %d)", MaxNodes), SeverityError)
				p.entityCapWarned = true
			}
			return -1
		}
		p.ast.Entities = append(p.ast.Entities, ErEntity{
			ID:      id,
			Label:   id,
			Order:   len(p.ast.Entities),
			SrcSpan: p.curSpan,
		})
		i = len(p.ast.Entities) - 1
	}
	return i
}

// addCSS gives entity i the style classes css, each trimmed.
func (p *erParser) addCSS(i int, css []string) {
	for _, c := range css {
		p.ast.Entities[i].CSSClasses = appendUnique(p.ast.Entities[i].CSSClasses, trimSpace(c))
	}
}

// stripCSSSuffix takes a `:::a,b` list of style classes off an entity's
// name, and returns the name trimmed and the classes.
func stripCSSSuffix(name string) (string, []string) {
	var css []string
	if trip := strings.Index(name, ":::"); trip >= 0 {
		css = splitSkipEmpty(trimSpace(name[trip+3:]), ",")
		name = name[:trip]
	}
	return trimSpace(name), css
}

// chopLeftCardinality takes the cardinality off the end of the left side of
// a relationship.
func chopLeftCardinality(side *string) (ErCardinality, bool) {
	*side = trimSpace(*side)
	for _, s := range []struct {
		tok  string
		card ErCardinality
	}{{"}o", ZeroOrMore}, {"}|", OneOrMore}, {"|o", ZeroOrOne}, {"||", OnlyOne}} {
		if strings.HasSuffix(*side, s.tok) {
			*side = trimSpace((*side)[:len(*side)-2])
			return s.card, true
		}
	}
	if strings.HasSuffix(*side, "u") {
		before, _ := utf8.DecodeLastRuneInString((*side)[:len(*side)-1])
		if len(*side) == 1 || unicode.IsSpace(before) {
			*side = trimSpace((*side)[:len(*side)-1])
			return MdParent, true
		}
	}
	for _, w := range erCardWords {
		head, ok := cutSuffixFold(*side, w.word)
		if !ok {
			continue
		}
		// The word has to stand on its own.
		if before, size := utf8.DecodeLastRuneInString(head); size > 0 && !unicode.IsSpace(before) {
			continue
		}
		*side = trimSpace(head)
		return w.card, true
	}
	return OnlyOne, false
}

// chopRightCardinality takes the cardinality off the start of the right side
// of a relationship.
func chopRightCardinality(side *string) (ErCardinality, bool) {
	*side = trimSpace(*side)
	for _, s := range []struct {
		tok  string
		card ErCardinality
	}{{"o{", ZeroOrMore}, {"|{", OneOrMore}, {"o|", ZeroOrOne}, {"||", OnlyOne}} {
		if strings.HasPrefix(*side, s.tok) {
			*side = trimSpace((*side)[2:])
			return s.card, true
		}
	}
	for _, w := range erCardWords {
		tail, ok := cutPrefixFold(*side, w.word)
		if !ok {
			continue
		}
		if after, size := utf8.DecodeRuneInString(tail); size > 0 && !unicode.IsSpace(after) {
			continue
		}
		*side = trimSpace(tail)
		return w.card, true
	}
	return OnlyOne, false
}

// findRelationLine is where the line of a relationship is: leftEnd is where
// the left side ends and rightStart where the right side starts. It looks
// first for `--` (identifying) or `..`, `.-` or `-.` (not identifying)
// outside quotes, then for the words ` to ` and ` optionally to `.
func findRelationLine(line string) (leftEnd, rightStart int, identifying, ok bool) {
	inQuote := false
	for i := 0; i+1 < len(line); i++ {
		a := line[i]
		if a == '"' {
			inQuote = !inQuote
			continue
		}
		if inQuote {
			continue
		}
		b := line[i+1]
		if a == '-' && b == '-' {
			return i, i + 2, true, true
		}
		if (a == '.' && b == '.') || (a == '.' && b == '-') || (a == '-' && b == '.') {
			return i, i + 2, false, true
		}
	}
	inQuote = false
	for i, r := range line {
		if r == '"' {
			inQuote = !inQuote
			continue
		}
		if inQuote || !unicode.IsSpace(r) {
			continue
		}
		pos := i + utf8.RuneLen(r)
		rest := line[pos:]
		// The right side starts after `optionally to` or `to`, without the
		// space that follows them.
		if hasPrefixFold(rest, "optionally to ") {
			tail, _ := cutPrefixFold(rest, "optionally to")
			return pos, len(line) - len(tail), false, true
		}
		if hasPrefixFold(rest, "to ") {
			tail, _ := cutPrefixFold(rest, "to")
			return pos, len(line) - len(tail), true, true
		}
	}
	return 0, 0, false, false
}

// parseRelationship reads `A ||--o{ B : role`. It reports false when the
// line has no relationship line, and true when it was a relationship, even
// one with an error.
func (p *erParser) parseRelationship(line string, lineNo int) bool {
	leftEnd, rightStart, identifying, ok := findRelationLine(line)
	if !ok {
		return false
	}
	left := trimSpace(line[:leftEnd])
	right := trimSpace(line[rightStart:])

	// The role follows the first `:` outside quotes that is not a `:::`.
	role := ""
	haveRole := false
	inQuote := false
	for i := 0; i < len(right); i++ {
		c := right[i]
		if c == '"' {
			inQuote = !inQuote
			continue
		}
		if c != ':' || inQuote {
			continue
		}
		if strings.HasPrefix(right[i:], ":::") {
			i += 2
			continue
		}
		role = leftRunes(stripQuotes(trimSpace(right[i+1:])), MaxLabelChars)
		right = trimSpace(right[:i])
		haveRole = true
		break
	}

	cardLeft, haveLeft := chopLeftCardinality(&left)
	cardRight, haveRight := chopRightCardinality(&right)
	if !haveLeft || !haveRight {
		p.diag(lineNo, "Expected a cardinality on both sides of the relationship", SeverityError)
		return true
	}
	if !haveRole {
		p.diag(lineNo, "Expected `:` and a relationship label", SeverityError)
		return true
	}

	left, cssLeft := stripCSSSuffix(left)
	right, cssRight := stripCSSSuffix(right)
	a := p.ensureEntity(left, lineNo)
	b := p.ensureEntity(right, lineNo)
	if a < 0 || b < 0 {
		return true
	}
	p.addCSS(a, cssLeft)
	p.addCSS(b, cssRight)

	if len(p.ast.Relationships) >= MaxEdges {
		if !p.relationCapWarned {
			p.diag(lineNo, fmt.Sprintf("Too many relationships (limit %d)", MaxEdges), SeverityError)
			p.relationCapWarned = true
		}
		return true
	}
	p.ast.Relationships = append(p.ast.Relationships, ErRelationship{
		From:        p.ast.Entities[a].ID,
		To:          p.ast.Entities[b].ID,
		FromCard:    cardLeft,
		ToCard:      cardRight,
		Identifying: identifying,
		Label:       role,
		Order:       len(p.ast.Relationships),
		SrcSpan:     p.curSpan,
	})
	return true
}

// parseAttributeLine reads `type name PK, FK "comment"` inside an entity's
// block.
func (p *erParser) parseAttributeLine(line string, lineNo int) {
	// Split at white space outside quotes (comments) and backquotes (words
	// taken literally). The quotes and backquotes are dropped.
	var tokens []string
	var quoted []bool
	var cur strings.Builder
	inQuote, inBq, curQuoted := false, false, false
	for _, c := range line {
		if c == '"' && !inBq {
			inQuote = !inQuote
			curQuoted = true
			continue
		}
		if c == '`' && !inQuote {
			inBq = !inBq
			continue
		}
		if unicode.IsSpace(c) && !inQuote && !inBq {
			if cur.Len() > 0 {
				tokens = append(tokens, cur.String())
				quoted = append(quoted, curQuoted)
				cur.Reset()
				curQuoted = false
			}
			continue
		}
		cur.WriteRune(c)
	}
	if cur.Len() > 0 {
		tokens = append(tokens, cur.String())
		quoted = append(quoted, curQuoted)
	}
	if len(tokens) == 0 {
		return
	}
	if len(tokens) < 2 {
		p.diag(lineNo, "Expected an attribute type and name", SeverityError)
		return
	}
	attr := ErAttribute{
		Type: leftRunes(tokens[0], MaxLabelChars),
		Name: leftRunes(tokens[1], MaxLabelChars),
	}
	for i := 2; i < len(tokens); i++ {
		if quoted[i] {
			attr.Comment = leftRunes(tokens[i], MaxLabelChars)
			continue
		}
		// A key list: PK, FK or UK, perhaps joined by commas.
		keys := splitSkipEmpty(strings.ToUpper(tokens[i]), ",")
		allKeys := len(keys) > 0
		for _, k := range keys {
			if k != "PK" && k != "FK" && k != "UK" {
				allKeys = false
			}
		}
		if allKeys {
			attr.Keys = append(attr.Keys, keys...)
		} else {
			p.diag(lineNo, "Unexpected attribute token: "+tokens[i], SeverityWarning)
		}
	}
	e := &p.ast.Entities[p.openEntity]
	e.Attributes = append(e.Attributes, attr)
}

func (p *erParser) parseEntityLine(line string, lineNo int) {
	openBlock := false
	if strings.HasSuffix(line, "{") {
		openBlock = true
		line = trimSpace(line[:len(line)-1])
	}
	line, css := stripCSSSuffix(line)
	// `NAME["alias"]` and `NAME[alias]`
	label := ""
	if strings.HasSuffix(line, "]") {
		if open := strings.IndexByte(line, '['); open > 0 {
			label = stripQuotes(line[open+1 : len(line)-1])
			line = trimSpace(line[:open])
		}
	}
	i := p.ensureEntity(line, lineNo)
	if i < 0 {
		return
	}
	if label != "" {
		p.ast.Entities[i].Label = leftRunes(label, MaxLabelChars)
	}
	p.addCSS(i, css)
	if openBlock {
		p.openEntity = i
		p.openLine = lineNo
	}
}

func (p *erParser) parseStatement(line string, lineNo int) {
	if p.openEntity >= 0 {
		if line == "}" {
			p.openEntity = -1
			return
		}
		if strings.HasSuffix(line, "}") {
			if attr := trimSpace(line[:len(line)-1]); attr != "" {
				p.parseAttributeLine(attr, lineNo)
			}
			p.openEntity = -1
			return
		}
		p.parseAttributeLine(line, lineNo)
		return
	}

	kw := func(k string) (string, bool) { return keyword(line, k, true) }

	if rest, ok := kw("erDiagram"); ok {
		if rest != "" {
			p.parseStatement(rest, lineNo)
		}
		return
	}
	if rest, ok := kw("direction"); ok {
		if d, ok := directionName(rest); ok {
			p.ast.Direction = d
		}
		return
	}
	if rest, ok := kw("title"); ok {
		p.ast.Title = rest
		return
	}
	if hasPrefixFold(line, "accTitle") {
		if colon := strings.IndexByte(line, ':'); colon >= 0 {
			p.ast.AccTitle = trimSpace(line[colon+1:])
		}
		return
	}
	if hasPrefixFold(line, "accDescr") {
		if text, ok := accDescrText(line); ok {
			p.ast.AccDescr = text
		}
		return
	}
	if rest, ok := kw("classDef"); ok {
		if space := strings.IndexByte(rest, ' '); space > 0 {
			for _, name := range splitSkipEmpty(rest[:space], ",") {
				name = trimSpace(name)
				def := p.ast.ClassDefs[name]
				ParseStyleDeclarations(&def, rest[space+1:])
				p.ast.ClassDefs[name] = def
			}
		}
		return
	}
	if rest, ok := kw("class"); ok {
		if space := strings.IndexByte(rest, ' '); space > 0 {
			cls := trimSpace(rest[space+1:])
			for _, id := range splitSkipEmpty(rest[:space], ",") {
				if i := p.ensureEntity(trimSpace(id), lineNo); i >= 0 && cls != "" {
					p.ast.Entities[i].CSSClasses = appendUnique(p.ast.Entities[i].CSSClasses, cls)
				}
			}
		}
		return
	}
	if rest, ok := kw("style"); ok {
		if space := strings.IndexByte(rest, ' '); space > 0 {
			for _, idRaw := range splitSkipEmpty(rest[:space], ",") {
				id := trimSpace(idRaw)
				synth := "__style_" + id
				def := p.ast.ClassDefs[synth]
				ParseStyleDeclarations(&def, rest[space+1:])
				p.ast.ClassDefs[synth] = def
				if i := p.ensureEntity(id, lineNo); i >= 0 {
					p.ast.Entities[i].CSSClasses = appendUnique(p.ast.Entities[i].CSSClasses, synth)
				}
			}
		}
		return
	}

	if p.parseRelationship(line, lineNo) {
		return
	}

	p.parseEntityLine(line, lineNo)
}

func (p *erParser) run(body string) {
	physical := strings.Split(body, "\n")
	lineOffset := lineOffsets(physical)

	inAccDescr := false
	for li := range physical {
		raw := stripQuotedComment(physical[li])
		if inAccDescr {
			if accDescrLine(&p.ast.AccDescr, raw) {
				inAccDescr = false
			}
			continue
		}
		probe := trimSpace(raw)
		if probe == "" || strings.HasPrefix(probe, "#") {
			continue
		}
		p.curSpan = Span{Start: p.base + lineOffset[li] + leadingSpaces(raw), Length: runeLen(probe)}
		if accDescrOpens(&p.ast.AccDescr, probe, true) {
			inAccDescr = true
			continue
		}
		p.parseStatement(probe, li+1)
	}

	if p.openEntity >= 0 {
		p.diag(p.openLine, "Entity block is missing its `}`", SeverityError)
	}
}
