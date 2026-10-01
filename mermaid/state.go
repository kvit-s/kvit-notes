package mermaid

import (
	"fmt"
	"strings"
)

// The state parser follows stateDiagram.jison of mermaid@11.16.0:
// stateDiagram and stateDiagram-v2, states with descriptions (`s1 : text`,
// `state "long" as s1`), transitions with labels, `[*]` start and end states
// scoped to their composite state, composite states with their own bodies
// and local direction, <<fork>>, <<join>> and <<choice>> (and the [[…]]
// spellings), notes (left or right, on one line or up to `end note`, and
// floating `note "x" as n`), classDef, class, style and `:::`, and accTitle
// and accDescr. `scale`, the `--` dividers of concurrent regions, and click
// and href are kept with a warning, never refused as unknown statements.

// stripQuotedComment cuts a `%%` comment outside quotes off a line.
func stripQuotedComment(raw string) string {
	inQuote := false
	for i := 0; i+1 < len(raw); i++ {
		c := raw[i]
		if c == '"' {
			inQuote = !inQuote
		} else if c == '%' && raw[i+1] == '%' && !inQuote {
			return raw[:i]
		}
	}
	return raw
}

// firstLabelColon is the first `:` of s that is not part of the first
// `:::styleClass` separator, or -1.
func firstLabelColon(s string) int {
	trip := strings.Index(s, ":::")
	for i := 0; i < len(s); i++ {
		if s[i] != ':' || (trip >= 0 && i >= trip && i <= trip+2) {
			continue
		}
		return i
	}
	return -1
}

// ParseStateDiagram reads a stateDiagram's body, front matter already taken
// off, into result.State. baseOffset is where the body starts in the fence's
// text. Diagnostics give the body's line, counted from one.
func ParseStateDiagram(body string, baseOffset int, result *ParseResult) {
	p := stateParser{r: result, ast: &result.State, base: baseOffset}
	p.run(body)
}

type stateParser struct {
	r                   *ParseResult
	ast                 *StateAst
	base                int
	curSpan             Span     // the statement being read
	scope               []string // the ids of the open composite states
	scopeLines          []int
	stateCapWarned      bool
	transitionCapWarned bool
	dividerWarned       bool
	// A note being read up to its `end note`.
	inNote      bool
	pendingNote StateNote
}

func (p *stateParser) diag(line int, message string, severity Severity) {
	p.r.diag(line, 1, message, severity)
}

func (p *stateParser) scopeID() string {
	if len(p.scope) == 0 {
		return ""
	}
	return p.scope[len(p.scope)-1]
}

// ensureState is the index of the state written as writtenID, added with
// kindIfNew if it is new, or -1 when the id is empty or past the limit.
func (p *stateParser) ensureState(writtenID string, lineNo int, kindIfNew StateKind) int {
	id := trimSpace(writtenID)
	if id == "" {
		return -1
	}
	i := p.ast.IndexOfState(id)
	if i < 0 {
		if len(p.ast.States) >= MaxNodes {
			if !p.stateCapWarned {
				p.diag(lineNo, fmt.Sprintf("Too many states (limit %d)", MaxNodes), SeverityError)
				p.stateCapWarned = true
			}
			return -1
		}
		s := StateNode{
			ID:          id,
			Label:       id,
			Kind:        kindIfNew,
			ParentIndex: -1,
			Order:       len(p.ast.States),
			SrcSpan:     p.curSpan,
		}
		if len(p.scope) > 0 {
			s.ParentIndex = p.ast.IndexOfState(p.scope[len(p.scope)-1])
		}
		p.ast.States = append(p.ast.States, s)
		i = len(p.ast.States) - 1
	}
	return i
}

// resolveID is the index of the state written, with a `:::styleClass`
// appended to css. `[*]` is the start or end state of the open composite.
func (p *stateParser) resolveID(written string, asTarget bool, lineNo int, css *[]string) int {
	written = trimSpace(written)
	if trip := strings.Index(written, ":::"); trip >= 0 {
		*css = append(*css, trimSpace(written[trip+3:]))
		written = trimSpace(written[:trip])
	}
	if written == "[*]" {
		// The start and end states are keyed by their composite state, with
		// NUL around the kind. A line of source cannot hold a NUL, so no
		// state the reader writes, `__start__` included, can take the same
		// entry.
		kind, name := StateStart, "start"
		if asTarget {
			kind, name = StateEnd, "end"
		}
		i := p.ensureState("\x00"+name+"\x00"+p.scopeID(), lineNo, kind)
		if i >= 0 {
			p.ast.States[i].Label = ""
		}
		return i
	}
	return p.ensureState(written, lineNo, StateNormal)
}

// addCSS gives state i the style classes css.
func (p *stateParser) addCSS(i int, css []string) {
	for _, c := range css {
		p.ast.States[i].CSSClasses = appendUnique(p.ast.States[i].CSSClasses, c)
	}
}

// parseTransition reads `A --> B : label`. It reports false when the line
// has no `-->`, and true when it was a transition, even one with an error.
func (p *stateParser) parseTransition(line string, lineNo int) bool {
	pos := strings.Index(line, "-->")
	if pos < 0 {
		return false
	}
	left := trimSpace(line[:pos])
	right := trimSpace(line[pos+3:])
	label := ""
	if colon := firstLabelColon(right); colon >= 0 {
		label = leftRunes(trimSpace(right[colon+1:]), MaxLabelChars)
		right = trimSpace(right[:colon])
	}
	if left == "" || right == "" {
		p.diag(lineNo, "Expected states on both sides of `-->`", SeverityError)
		return true
	}
	var cssLeft, cssRight []string
	a := p.resolveID(left, false, lineNo, &cssLeft)
	b := p.resolveID(right, true, lineNo, &cssRight)
	if a < 0 || b < 0 {
		return true
	}
	p.addCSS(a, cssLeft)
	p.addCSS(b, cssRight)
	if len(p.ast.Transitions) >= MaxEdges {
		if !p.transitionCapWarned {
			p.diag(lineNo, fmt.Sprintf("Too many transitions (limit %d)", MaxEdges), SeverityError)
			p.transitionCapWarned = true
		}
		return true
	}
	p.ast.Transitions = append(p.ast.Transitions, StateTransition{
		From:    p.ast.States[a].ID,
		To:      p.ast.States[b].ID,
		Label:   label,
		Order:   len(p.ast.Transitions),
		SrcSpan: p.curSpan,
	})
	return true
}

// stateKindMarkers mark a state as a fork, join or choice.
var stateKindMarkers = []struct {
	tok  string
	kind StateKind
}{
	{"<<fork>>", StateFork}, {"[[fork]]", StateFork},
	{"<<join>>", StateJoin}, {"[[join]]", StateJoin},
	{"<<choice>>", StateChoice}, {"[[choice]]", StateChoice},
}

func (p *stateParser) parseStateDecl(rest string, lineNo int) {
	rest = trimSpace(rest)
	// `state Name <<fork>>` and the other markers.
	for _, k := range stateKindMarkers {
		if id, ok := cutSuffixFold(rest, k.tok); ok {
			if i := p.ensureState(trimSpace(id), lineNo, k.kind); i >= 0 {
				p.ast.States[i].Kind = k.kind
				p.ast.States[i].Label = ""
			}
			return
		}
	}
	openBody := false
	if strings.HasSuffix(rest, "{") {
		openBody = true
		rest = trimSpace(rest[:len(rest)-1])
	}
	id := rest
	label := ""
	// `state "Long description" as s2`
	if strings.HasPrefix(rest, `"`) {
		if close := strings.IndexByte(rest[1:], '"'); close >= 0 {
			label = rest[1 : close+1]
			tail := trimSpace(rest[close+2:])
			as, ok := cutPrefixFold(tail, "as ")
			if !ok {
				p.diag(lineNo, "Expected `as <id>` after the state description", SeverityError)
				return
			}
			id = trimSpace(as)
		}
	}
	var css []string
	if trip := strings.Index(id, ":::"); trip >= 0 {
		css = append(css, trimSpace(id[trip+3:]))
		id = trimSpace(id[:trip])
	}
	if strings.Contains(id, " ") {
		p.diag(lineNo, "State name must be a single word: "+id, SeverityError)
		return
	}
	i := p.ensureState(id, lineNo, StateNormal)
	if i < 0 {
		return
	}
	if label != "" {
		p.ast.States[i].Label = leftRunes(label, MaxLabelChars)
	}
	p.addCSS(i, css)
	if openBody {
		if len(p.scope) >= MaxDepth {
			p.diag(lineNo, fmt.Sprintf("Composite nesting too deep (limit %d)", MaxDepth), SeverityError)
			return
		}
		p.ast.States[i].Composite = true
		p.scope = append(p.scope, p.ast.States[i].ID)
		p.scopeLines = append(p.scopeLines, lineNo)
	}
}

func (p *stateParser) parseNote(rest string, lineNo int) {
	var note StateNote
	tail, ok := keyword(rest, "left of", true)
	if ok {
		note.LeftOf = true
	} else if tail, ok = keyword(rest, "right of", true); ok {
		note.LeftOf = false
	} else if strings.HasPrefix(rest, `"`) {
		// A floating note: `note "text" as id`.
		if close := strings.IndexByte(rest[1:], '"'); close >= 0 {
			note.Text = leftRunes(rest[1:close+1], MaxLabelChars)
			p.ast.Notes = append(p.ast.Notes, note)
		}
		return
	} else {
		p.diag(lineNo, "Expected `left of`, `right of`, or a quoted note text", SeverityError)
		return
	}
	if colon := strings.IndexByte(tail, ':'); colon >= 0 {
		note.StateID = trimSpace(tail[:colon])
		note.Text = leftRunes(trimSpace(tail[colon+1:]), MaxLabelChars)
		if p.ensureState(note.StateID, lineNo, StateNormal) >= 0 {
			p.ast.Notes = append(p.ast.Notes, note)
		}
		return
	}
	// The note's text is on the lines up to `end note`.
	note.StateID = trimSpace(tail)
	if p.ensureState(note.StateID, lineNo, StateNormal) < 0 {
		return
	}
	p.pendingNote = note
	p.inNote = true
}

func (p *stateParser) parseStatement(line string, lineNo int) {
	kw := func(k string) (string, bool) { return keyword(line, k, true) }

	if rest, ok := cutPrefixFold(line, "stateDiagram"); ok {
		if v2, ok := cutPrefixFold(line, "stateDiagram-v2"); ok {
			rest = v2
		}
		if rest = trimSpace(rest); rest != "" {
			p.parseStatement(rest, lineNo)
		}
		return
	}
	if line == "}" {
		if len(p.scope) == 0 {
			p.diag(lineNo, "`}` without an open composite state", SeverityError)
			return
		}
		p.scope = p.scope[:len(p.scope)-1]
		p.scopeLines = p.scopeLines[:len(p.scopeLines)-1]
		return
	}
	if rest, ok := kw("direction"); ok {
		// Inside a composite state a direction applies to that body only in
		// Mermaid; Kvit uses the top level's and ignores the others.
		if d, ok := directionName(rest); ok && len(p.scope) == 0 {
			p.ast.Direction = d
		}
		return
	}
	if rest, ok := kw("state"); ok {
		p.parseStateDecl(rest, lineNo)
		return
	}
	if rest, ok := kw("note"); ok {
		p.parseNote(rest, lineNo)
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
		// `class id1,id2 styleClass`
		if space := strings.IndexByte(rest, ' '); space > 0 {
			cls := trimSpace(rest[space+1:])
			for _, id := range splitSkipEmpty(rest[:space], ",") {
				if i := p.ensureState(trimSpace(id), lineNo, StateNormal); i >= 0 && cls != "" {
					p.addCSS(i, []string{cls})
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
				if i := p.ensureState(id, lineNo, StateNormal); i >= 0 {
					p.addCSS(i, []string{synth})
				}
			}
		}
		return
	}
	if _, ok := kw("hide"); ok {
		// `hide empty description`: Kvit never draws an empty description
		// compartment, so there is nothing to do.
		return
	}
	if _, ok := kw("scale"); ok {
		p.diag(lineNo, "`scale` is ignored in Kvit", SeverityWarning)
		return
	}
	_, click := kw("click")
	_, href := kw("href")
	if click || href {
		p.diag(lineNo, "`click` is ignored in Kvit (interactivity is not supported)", SeverityWarning)
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
	if line == "--" {
		if !p.dividerWarned {
			p.diag(lineNo, "Concurrent regions (`--`) render without dividers in Kvit", SeverityWarning)
			p.dividerWarned = true
		}
		return
	}

	if p.parseTransition(line, lineNo) {
		return
	}

	// `A : description` adds a description line.
	trip := strings.Index(line, ":::")
	if colon := firstLabelColon(line); colon > 0 {
		var css []string
		if i := p.resolveID(line[:colon], false, lineNo, &css); i >= 0 {
			p.ast.States[i].Descriptions = append(p.ast.States[i].Descriptions,
				leftRunes(trimSpace(line[colon+1:]), MaxLabelChars))
			p.addCSS(i, css)
		}
		return
	}

	// An id alone, perhaps with `:::class`, declares the state.
	if !strings.Contains(line, " ") || trip >= 0 {
		var css []string
		if i := p.resolveID(line, false, lineNo, &css); i >= 0 {
			p.addCSS(i, css)
		}
		return
	}

	p.diag(lineNo, "Unrecognized state-diagram statement: "+leftRunes(line, 40), SeverityError)
}

func (p *stateParser) run(body string) {
	physical := strings.Split(body, "\n")
	lineOffset := lineOffsets(physical)

	inAccDescr := false
	for li := range physical {
		raw := stripQuotedComment(physical[li])

		if p.inNote {
			if strings.EqualFold(trimSpace(raw), "end note") {
				p.pendingNote.Text = leftRunes(trimSpace(p.pendingNote.Text), MaxLabelChars)
				p.ast.Notes = append(p.ast.Notes, p.pendingNote)
				p.pendingNote = StateNote{}
				p.inNote = false
			} else {
				if p.pendingNote.Text != "" {
					p.pendingNote.Text += "\n"
				}
				p.pendingNote.Text += trimSpace(raw)
			}
			continue
		}
		if inAccDescr {
			if accDescrLine(&p.ast.AccDescr, raw) {
				inAccDescr = false
			}
			continue
		}
		probe := trimSpace(raw)
		if strings.HasPrefix(probe, "#") {
			continue
		}
		if accDescrOpens(&p.ast.AccDescr, probe, true) {
			inAccDescr = true
			continue
		}
		if probe == "" {
			continue
		}

		// `;` separates statements as a line break does.
		var segments []string
		inQuote := false
		segStart := 0
		for i := 0; i < len(probe); i++ {
			if probe[i] == '"' {
				inQuote = !inQuote
			} else if probe[i] == ';' && !inQuote {
				segments = append(segments, probe[segStart:i])
				segStart = i + 1
			}
		}
		segments = append(segments, probe[segStart:])
		probeLead := leadingSpaces(raw)
		segOffset := 0
		for _, segment := range segments {
			if stmt := trimSpace(segment); stmt != "" {
				p.curSpan = Span{
					Start:  p.base + lineOffset[li] + probeLead + segOffset + leadingSpaces(segment),
					Length: runeLen(stmt),
				}
				p.parseStatement(stmt, li+1)
			}
			segOffset += runeLen(segment) + 1
		}
	}

	if p.inNote {
		p.diag(len(physical), "Note is missing its `end note`", SeverityError)
	}
	for _, line := range p.scopeLines {
		p.diag(line, "Composite state is missing its `}`", SeverityError)
	}
}
