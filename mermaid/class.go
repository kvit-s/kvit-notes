package mermaid

import (
	"fmt"
	"strings"
	"unicode"
	"unicode/utf8"
)

// The class parser follows classDiagram.jison of mermaid@11.16.0: classes
// with labels, names in backquotes, generics, members and methods kept as
// text, every relation end (extension, composition, aggregation, dependency,
// lollipop) on solid or dotted lines with cardinalities and labels,
// annotations, one level of namespaces, notes, direction, and classDef,
// style and cssClass. Interactivity (click, callback, link, href) is kept
// with a warning, never refused as an unknown statement. It is a port of the
// Qt app's mermaidclass.cpp.

// stripClassComment cuts a `%%` comment off a line, outside quotes and
// backquotes.
func stripClassComment(raw string) string {
	inQuote, inBq := false, false
	for i := 0; i+1 < len(raw); i++ {
		switch c := raw[i]; {
		case c == '"' && !inBq:
			inQuote = !inQuote
		case c == '`' && !inQuote:
			inBq = !inBq
		case c == '%' && raw[i+1] == '%' && !inQuote && !inBq:
			return raw[:i]
		}
	}
	return raw
}

// findLineToken is where the first `--` or `..` outside quotes and
// backquotes is, or -1, and whether it is dotted.
func findLineToken(s string) (int, bool) {
	inQuote, inBq := false, false
	for i := 0; i+1 < len(s); i++ {
		c := s[i]
		if c == '"' && !inBq {
			inQuote = !inQuote
			continue
		}
		if c == '`' && !inQuote {
			inBq = !inBq
			continue
		}
		if inQuote || inBq {
			continue
		}
		if c == '-' && s[i+1] == '-' {
			return i, false
		}
		if c == '.' && s[i+1] == '.' {
			return i, true
		}
	}
	return -1, false
}

// cleanClassName is a class name as written, trimmed and out of its
// backquotes.
func cleanClassName(s string) string {
	return stripQuotePair(trimSpace(s), '`')
}

// displayLabel is the label shown for a class id: `Name~T~` shows as
// `Name<T>`.
func displayLabel(id string) string {
	t1 := strings.IndexByte(id, '~')
	t2 := strings.LastIndexByte(id, '~')
	if t1 >= 0 && t2 > t1 {
		return id[:t1] + "<" + id[t1+1:t2] + ">" + id[t2+1:]
	}
	return id
}

// ParseClassDiagram reads a classDiagram's body, front matter already taken
// off, into result.Class. baseOffset is where the body starts in the fence's
// text. Diagnostics give the body's line, counted from one.
func ParseClassDiagram(body string, baseOffset int, result *ParseResult) {
	p := classParser{r: result, ast: &result.Class, base: baseOffset}
	p.run(body)
}

type classScopeKind int

const (
	namespaceScope classScopeKind = iota
	classBodyScope
)

type classScope struct {
	kind  classScopeKind
	index int // the namespace's or the class's index
	line  int
}

type classParser struct {
	r                 *ParseResult
	ast               *ClassAst
	base              int
	curSpan           Span // the statement being read
	scopes            []classScope
	classCapWarned    bool
	relationCapWarned bool
}

func (p *classParser) diag(line int, message string, severity Severity) {
	p.r.diag(line, 1, message, severity)
}

// ensureClass is the index of the class written as writtenName, added if it
// is new, or -1 when the name is empty or past the limit. A class first met
// inside a namespace's body belongs to it.
func (p *classParser) ensureClass(writtenName string, lineNo int) int {
	id := cleanClassName(writtenName)
	if id == "" {
		return -1
	}
	i := p.ast.IndexOfClass(id)
	if i < 0 {
		if len(p.ast.Classes) >= MaxNodes {
			if !p.classCapWarned {
				p.diag(lineNo, fmt.Sprintf("Too many classes (limit %d)", MaxNodes), SeverityError)
				p.classCapWarned = true
			}
			return -1
		}
		c := ClassNode{
			ID:             id,
			Label:          displayLabel(id),
			NamespaceIndex: -1,
			Order:          len(p.ast.Classes),
			SrcSpan:        p.curSpan,
		}
		for s := len(p.scopes) - 1; s >= 0; s-- {
			if p.scopes[s].kind == namespaceScope {
				c.NamespaceIndex = p.scopes[s].index
				ns := &p.ast.Namespaces[c.NamespaceIndex]
				ns.ClassIDs = append(ns.ClassIDs, id)
				break
			}
		}
		p.ast.Classes = append(p.ast.Classes, c)
		i = len(p.ast.Classes) - 1
	}
	return i
}

// addMemberText adds a member line to a class: an annotation when it is
// `<<…>>`, a method when it has `(`, an attribute otherwise.
func (p *classParser) addMemberText(classIndex int, text string) {
	if classIndex < 0 {
		return
	}
	t := trimSpace(text)
	if t == "" {
		return
	}
	c := &p.ast.Classes[classIndex]
	if len(t) >= 4 && strings.HasPrefix(t, "<<") && strings.HasSuffix(t, ">>") {
		c.Annotation = trimSpace(t[2 : len(t)-2])
		return
	}
	if strings.Contains(t, "(") {
		c.Methods = append(c.Methods, leftRunes(t, MaxLabelChars))
	} else {
		c.Attributes = append(c.Attributes, leftRunes(t, MaxLabelChars))
	}
}

// chopEndMarker takes a UML end marker off the side of a relation next to
// its line, if there is one: the end of the left side, or the start of the
// right.
func chopEndMarker(side *string, leftSide bool) ClassRelEnd {
	probe := *side
	has := func(tok string) bool {
		if leftSide {
			return strings.HasSuffix(probe, tok)
		}
		return strings.HasPrefix(probe, tok)
	}
	chop := func(n int) {
		if leftSide {
			*side = (*side)[:len(*side)-n]
		} else {
			*side = (*side)[n:]
		}
		*side = trimSpace(*side)
	}
	switch {
	case has("<|") || has("|>"):
		chop(2)
		return RelExtension
	case has("()"):
		chop(2)
		return RelLollipop
	case has("*"):
		chop(1)
		return RelComposition
	case has("<") || has(">"):
		chop(1)
		return RelDependency
	}
	// `o` is aggregation only as a token of its own, never the end of a
	// name.
	if leftSide && strings.HasSuffix(probe, "o") {
		before, _ := utf8.DecodeLastRuneInString(probe[:len(probe)-1])
		if len(probe) == 1 || unicode.IsSpace(before) || before == '"' {
			chop(1)
			return RelAggregation
		}
	}
	if !leftSide && strings.HasPrefix(probe, "o") {
		after, _ := utf8.DecodeRuneInString(probe[1:])
		if len(probe) == 1 || unicode.IsSpace(after) || after == '"' {
			chop(1)
			return RelAggregation
		}
	}
	return RelNone
}

// parseRelation reads `A "1" <|-- "many" B : label`. It reports false when
// the line has no relation line, and true when it was a relation, even one
// with an error.
func (p *classParser) parseRelation(line string, lineNo int) bool {
	pos, dotted := findLineToken(line)
	if pos < 0 {
		return false
	}
	// A `:` before the line makes it `Class : member text`: the LABEL token
	// wins in Mermaid, so `A : --flag` is a member, not a relation.
	inQuote := false
	for i := 0; i < pos; i++ {
		if line[i] == '"' {
			inQuote = !inQuote
		} else if line[i] == ':' && !inQuote {
			return false
		}
	}

	rel := ClassRelation{Dotted: dotted}
	left := trimSpace(line[:pos])
	right := line[pos+2:]

	// A `: label` at the end, outside quotes.
	inQuote = false
	for i := 0; i < len(right); i++ {
		if right[i] == '"' {
			inQuote = !inQuote
		} else if right[i] == ':' && !inQuote {
			rel.Label = leftRunes(trimSpace(right[i+1:]), MaxLabelChars)
			right = right[:i]
			break
		}
	}
	right = trimSpace(right)

	rel.FromEnd = chopEndMarker(&left, true)
	rel.ToEnd = chopEndMarker(&right, false)

	// Quoted cardinalities next to the relation.
	if strings.HasSuffix(left, `"`) {
		// QString::lastIndexOf from size-2; from -1 searches the whole of a
		// one-character string.
		open := 0
		if len(left) > 1 {
			open = strings.LastIndexByte(left[:len(left)-1], '"')
		}
		if open >= 0 {
			if open+1 <= len(left)-1 {
				rel.FromCard = left[open+1 : len(left)-1]
			}
			left = trimSpace(left[:open])
		}
	}
	if strings.HasPrefix(right, `"`) {
		if close := strings.IndexByte(right[1:], '"'); close >= 0 {
			rel.ToCard = right[1 : close+1]
			right = trimSpace(right[close+2:])
		}
	}

	if left == "" || right == "" {
		p.diag(lineNo, "Expected class names on both sides of the relation", SeverityError)
		return true
	}
	a := p.ensureClass(left, lineNo)
	b := p.ensureClass(right, lineNo)
	if a < 0 || b < 0 {
		return true
	}
	if len(p.ast.Relations) >= MaxEdges {
		if !p.relationCapWarned {
			p.diag(lineNo, fmt.Sprintf("Too many relationships (limit %d)", MaxEdges), SeverityError)
			p.relationCapWarned = true
		}
		return true
	}
	rel.From = p.ast.Classes[a].ID
	rel.To = p.ast.Classes[b].ID
	rel.Order = len(p.ast.Relations)
	rel.SrcSpan = p.curSpan
	p.ast.Relations = append(p.ast.Relations, rel)
	return true
}

func (p *classParser) parseClassDecl(rest string, lineNo int) {
	rest = trimSpace(rest)
	openBody := false
	if strings.HasSuffix(rest, "{") {
		openBody = true
		rest = trimSpace(rest[:len(rest)-1])
	}
	// `class Name <<annotation>>`
	annotation := ""
	if annStart := strings.Index(rest, "<<"); annStart >= 0 {
		if annEnd := strings.Index(rest[annStart:], ">>"); annEnd > 0 {
			annEnd += annStart
			annotation = trimSpace(rest[annStart+2 : annEnd])
			rest = trimSpace(rest[:annStart] + rest[annEnd+2:])
		}
	}
	// `class Name:::styleClass`
	cssClass := ""
	if trip := strings.Index(rest, ":::"); trip >= 0 {
		cssClass = trimSpace(rest[trip+3:])
		rest = trimSpace(rest[:trip])
	}
	// `class Name["Display label"]`
	label := ""
	if strings.HasSuffix(rest, "]") {
		if open := strings.IndexByte(rest, '['); open > 0 {
			label = stripQuotePair(trimSpace(rest[open+1:len(rest)-1]), '"')
			rest = trimSpace(rest[:open])
		}
	}
	if rest == "" {
		p.diag(lineNo, "Expected a class name", SeverityError)
		return
	}
	i := p.ensureClass(rest, lineNo)
	if i < 0 {
		return
	}
	c := &p.ast.Classes[i]
	if label != "" {
		c.Label = leftRunes(label, MaxLabelChars)
	}
	if annotation != "" {
		c.Annotation = annotation
	}
	if cssClass != "" {
		c.CSSClasses = appendUnique(c.CSSClasses, cssClass)
	}
	if openBody {
		if len(p.scopes) >= MaxDepth {
			p.diag(lineNo, fmt.Sprintf("Nesting too deep (limit %d)", MaxDepth), SeverityError)
			return
		}
		p.scopes = append(p.scopes, classScope{classBodyScope, i, lineNo})
	}
}

// addCSSClass gives class i the style class cls, unless i is -1 or cls is
// empty.
func (p *classParser) addCSSClass(i int, cls string) {
	if i >= 0 && cls != "" {
		p.ast.Classes[i].CSSClasses = appendUnique(p.ast.Classes[i].CSSClasses, cls)
	}
}

func (p *classParser) parseStatement(line string, lineNo int) {
	// Inside a class body every line is member text until the `}`.
	if n := len(p.scopes); n > 0 && p.scopes[n-1].kind == classBodyScope {
		if line == "}" {
			p.scopes = p.scopes[:n-1]
			return
		}
		if strings.HasSuffix(line, "}") {
			p.addMemberText(p.scopes[n-1].index, line[:len(line)-1])
			p.scopes = p.scopes[:n-1]
			return
		}
		p.addMemberText(p.scopes[n-1].index, line)
		return
	}

	if line == "}" {
		if len(p.scopes) == 0 {
			p.diag(lineNo, "`}` without an open block", SeverityError)
			return
		}
		p.scopes = p.scopes[:len(p.scopes)-1]
		return
	}

	kw := func(k string) (string, bool) { return keyword(line, k, false) }

	if strings.HasPrefix(line, "classDiagram") {
		// The header; anything after it on the line is a statement.
		rest := line[len("classDiagram"):]
		if strings.HasPrefix(line, "classDiagram-v2") {
			rest = line[len("classDiagram-v2"):]
		}
		if rest = trimSpace(rest); rest != "" {
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
	if rest, ok := kw("namespace"); ok {
		openBody := false
		if strings.HasSuffix(rest, "{") {
			openBody = true
			rest = trimSpace(rest[:len(rest)-1])
		}
		p.ast.Namespaces = append(p.ast.Namespaces, ClassNamespace{Name: cleanClassName(rest)})
		if openBody {
			if len(p.scopes) >= MaxDepth {
				p.diag(lineNo, fmt.Sprintf("Nesting too deep (limit %d)", MaxDepth), SeverityError)
				return
			}
			p.scopes = append(p.scopes, classScope{namespaceScope, len(p.ast.Namespaces) - 1, lineNo})
		}
		return
	}
	if rest, ok := kw("class"); ok {
		p.parseClassDecl(rest, lineNo)
		return
	}
	if rest, ok := kw("note"); ok {
		var note ClassNote
		if tail, ok := strings.CutPrefix(rest, "for "); ok {
			tail = trimSpace(tail)
			if quote := strings.IndexByte(tail, '"'); quote > 0 {
				note.ForClass = cleanClassName(tail[:quote])
				tail = tail[quote:]
			}
			rest = tail
			if note.ForClass != "" {
				p.ensureClass(note.ForClass, lineNo)
			}
		}
		note.Text = leftRunes(stripQuotePair(trimSpace(rest), '"'), MaxLabelChars)
		if note.Text != "" {
			p.ast.Notes = append(p.ast.Notes, note)
		}
		return
	}
	if rest, ok := kw("classDef"); ok {
		if space := strings.IndexByte(rest, ' '); space > 0 {
			styles := rest[space+1:]
			for _, name := range splitSkipEmpty(rest[:space], ",") {
				name = trimSpace(name)
				def := p.ast.ClassDefs[name]
				ParseStyleDeclarations(&def, styles)
				p.ast.ClassDefs[name] = def
			}
		}
		return
	}
	if rest, ok := kw("style"); ok {
		if space := strings.IndexByte(rest, ' '); space > 0 {
			id := cleanClassName(rest[:space])
			synth := "__style_" + id
			def := p.ast.ClassDefs[synth]
			ParseStyleDeclarations(&def, rest[space+1:])
			p.ast.ClassDefs[synth] = def
			p.addCSSClass(p.ensureClass(id, lineNo), synth)
		}
		return
	}
	if rest, ok := kw("cssClass"); ok {
		// cssClass "A,B" styleName
		close := strings.LastIndexByte(rest, '"')
		open := strings.IndexByte(rest, '"')
		if open == 0 && close > open {
			cls := trimSpace(rest[close+1:])
			for _, id := range splitSkipEmpty(rest[1:close], ",") {
				p.addCSSClass(p.ensureClass(trimSpace(id), lineNo), cls)
			}
		}
		return
	}
	for _, k := range []string{"click", "callback", "link"} {
		if _, ok := kw(k); ok {
			p.diag(lineNo, fmt.Sprintf("`%s` is ignored in Kvit (interactivity is not supported)", k), SeverityWarning)
			return
		}
	}
	if strings.HasPrefix(line, "accTitle") {
		if colon := strings.IndexByte(line, ':'); colon >= 0 {
			p.ast.AccTitle = trimSpace(line[colon+1:])
		}
		return
	}
	if strings.HasPrefix(line, "accDescr") {
		if text, ok := accDescrText(line); ok {
			p.ast.AccDescr = text
		}
		return
	}
	// `<<annotation>> ClassName`
	if strings.HasPrefix(line, "<<") {
		if end := strings.Index(line, ">>"); end > 0 {
			ann := trimSpace(line[2:end])
			if i := p.ensureClass(line[end+2:], lineNo); i >= 0 {
				p.ast.Classes[i].Annotation = ann
			}
			return
		}
	}

	// Relations come before members, because a `:` may follow a relation.
	if p.parseRelation(line, lineNo) {
		return
	}

	// `ClassName:::styleClass` and `ClassName : member text`
	if trip := strings.Index(line, ":::"); trip > 0 {
		p.addCSSClass(p.ensureClass(line[:trip], lineNo), trimSpace(line[trip+3:]))
		return
	}
	if colon := strings.IndexByte(line, ':'); colon > 0 {
		p.addMemberText(p.ensureClass(line[:colon], lineNo), line[colon+1:])
		return
	}

	// A class name alone is a member statement that does nothing in Mermaid.
	bareWord := line != ""
	for _, c := range line {
		if !isLetterOrNumber(c) && c != '_' && c != '.' && c != '~' && c != '`' {
			bareWord = false
		}
	}
	if bareWord {
		return
	}

	p.diag(lineNo, "Unrecognized class-diagram statement: "+leftRunes(line, 40), SeverityError)
}

// directionName reads a `direction` statement's value.
func directionName(s string) (Direction, bool) {
	switch s {
	case "TB", "TD":
		return TB, true
	case "BT":
		return BT, true
	case "LR":
		return LR, true
	case "RL":
		return RL, true
	}
	return TB, false
}

func (p *classParser) run(body string) {
	physical := strings.Split(body, "\n")
	lineOffset := lineOffsets(physical)

	inAccDescr := false
	for li := range physical {
		raw := stripClassComment(physical[li])
		if inAccDescr {
			if accDescrLine(&p.ast.AccDescr, raw) {
				inAccDescr = false
			}
			continue
		}
		probe := trimSpace(raw)
		if accDescrOpens(&p.ast.AccDescr, probe, false) {
			inAccDescr = true
			continue
		}
		if probe == "" {
			continue
		}
		p.curSpan = Span{Start: p.base + lineOffset[li] + leadingSpaces(raw), Length: runeLen(probe)}
		p.parseStatement(probe, li+1)
	}

	for _, s := range p.scopes {
		if s.kind == classBodyScope {
			p.diag(s.line, "Class body is missing its `}`", SeverityError)
		} else {
			p.diag(s.line, "Namespace is missing its `}`", SeverityError)
		}
	}
}
