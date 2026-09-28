package mermaid

import (
	"fmt"
	"strings"
	"unicode"
)

// The sequence parser follows sequenceDiagram.jison of mermaid@11.16.0:
// participants and actors with aliases, every arrow, the +/- activation
// shorthand, loop, alt, opt, par, critical, break and rect blocks, boxes,
// notes, autonumber, titles and the accessibility statements. What Kvit does
// not draw (links, link, properties and details, a participant's `@{…}`
// configuration, create and destroy, central `()` connections) is kept with a
// warning, never refused as an unknown statement. It is a port of the Qt
// app's mermaidsequence.cpp.

// seqArrow is a message arrow of sequenceDiagram.jison with its line and
// head.
type seqArrow struct {
	tok   string
	line  SeqLine
	head  SeqHead
	bidir bool
}

// seqArrows are tried in order at each position, so the longest arrow there
// wins. The half-head and reverse arrows of the grammar are drawn as the
// nearest ordinary arrow.
var seqArrows = []seqArrow{
	{"<<-->>", SeqDotted, HeadFilled, true},
	{"<<->>", SeqSolid, HeadFilled, true},
	{`--|\`, SeqDotted, HeadFilled, false},
	{"--|/", SeqDotted, HeadFilled, false},
	{`--\\`, SeqDotted, HeadOpen, false},
	{"--//", SeqDotted, HeadOpen, false},
	{"/|--", SeqDotted, HeadFilled, false},
	{`\|--`, SeqDotted, HeadFilled, false},
	{"//--", SeqDotted, HeadOpen, false},
	{`\\--`, SeqDotted, HeadOpen, false},
	{"-->>", SeqDotted, HeadFilled, false},
	{`-|\`, SeqSolid, HeadFilled, false},
	{"-|/", SeqSolid, HeadFilled, false},
	{`-\\`, SeqSolid, HeadOpen, false},
	{"-//", SeqSolid, HeadOpen, false},
	{"/|-", SeqSolid, HeadFilled, false},
	{`\|-`, SeqSolid, HeadFilled, false},
	{"//-", SeqSolid, HeadOpen, false},
	{`\\-`, SeqSolid, HeadOpen, false},
	{"->>", SeqSolid, HeadFilled, false},
	{"--x", SeqDotted, HeadCross, false},
	{"--)", SeqDotted, HeadPoint, false},
	{"-->", SeqDotted, HeadOpen, false},
	{"-x", SeqSolid, HeadCross, false},
	{"-)", SeqSolid, HeadPoint, false},
	{"->", SeqSolid, HeadOpen, false},
}

// decodeEntities decodes Mermaid's `#code;` escapes, named and decimal, which
// are how a sequence label writes `<`, `;`, `#` and the like.
func decodeEntities(s string) string {
	s = strings.ReplaceAll(s, "#lt;", "<")
	s = strings.ReplaceAll(s, "#gt;", ">")
	s = strings.ReplaceAll(s, "#amp;", "&")
	s = strings.ReplaceAll(s, "#quot;", `"`)
	rs := []rune(s)
	from := 0
	for {
		hash := indexRuneFrom(rs, '#', from)
		if hash < 0 {
			break
		}
		j := hash + 1
		for j < len(rs) && unicode.IsDigit(rs[j]) {
			j++
		}
		if j > hash+1 && j < len(rs) && rs[j] == ';' {
			code, _ := qtToInt(string(rs[hash+1 : j]))
			// A surrogate half is not a character.
			if code > 0 && code < 0x110000 && !(code >= 0xD800 && code <= 0xDFFF) {
				rs = append(rs[:hash], append([]rune{rune(code)}, rs[j+1:]...)...)
				from = hash + 1
				continue
			}
		}
		from = hash + 1
	}
	return string(rs)
}

// stripWrap takes off the `wrap:` or `nowrap:` prefix a message may have
// (after an optional `:`, as the LINE rule allows) and decodes escapes.
// Wrapping itself is left to the layout.
func stripWrap(s string) string {
	s = trimSpace(s)
	probe := s
	if strings.HasPrefix(probe, ":") {
		probe = trimSpace(probe[1:])
	}
	for _, w := range []string{"nowrap:", "wrap:"} {
		if rest, ok := cutPrefixFold(probe, w); ok {
			s = trimSpace(rest)
			break
		}
	}
	return decodeEntities(s)
}

// ParseSequence reads a sequenceDiagram's body, front matter already taken
// off, into result.Sequence. baseOffset is where the body starts in the
// fence's text. Diagnostics give the body's line, counted from one.
func ParseSequence(body string, baseOffset int, result *ParseResult) {
	p := seqParser{r: result, ast: &result.Sequence, base: baseOffset, boxIndex: -1}
	p.run(body)
}

type seqScope struct {
	isBox bool
	block SeqBlock
	line  int
}

type seqParser struct {
	r                    *ParseResult
	ast                  *SequenceAst
	base                 int
	curSpan              Span // the statement being read
	scopes               []seqScope
	boxIndex             int
	participantCapWarned bool
	eventCapWarned       bool
	centralConnWarned    bool
}

func (p *seqParser) diag(line int, message string, severity Severity) {
	p.r.diag(line, 1, message, severity)
}

// newSeqEvent is a SeqEvent with the Qt app's defaults.
func newSeqEvent(kind SeqEventKind, lineNo int) SeqEvent {
	return SeqEvent{
		Kind:            kind,
		Placement:       PlaceOver,
		AutonumberShown: true,
		AutonumberStart: 1,
		AutonumberStep:  1,
		SrcLine:         lineNo,
		SrcSpan:         NoSpan,
	}
}

func (p *seqParser) addEvent(e SeqEvent) {
	if !e.SrcSpan.Valid() {
		e.SrcSpan = p.curSpan
	}
	if len(p.ast.Events) >= MaxEdges {
		if !p.eventCapWarned {
			p.diag(e.SrcLine, fmt.Sprintf("Too many statements (limit %d); the rest are not rendered", MaxEdges), SeverityError)
			p.eventCapWarned = true
		}
		return
	}
	p.ast.Events = append(p.ast.Events, e)
}

// ensureParticipant is the index of the participant id, added if it is new,
// or -1 when id is empty or past the limit. A declaration inside a `box`
// puts the participant in it.
func (p *seqParser) ensureParticipant(id string, lineNo int, declaration bool) int {
	if id == "" {
		return -1
	}
	i := p.ast.IndexOfParticipant(id)
	if i < 0 {
		if len(p.ast.Participants) >= MaxNodes {
			if !p.participantCapWarned {
				p.diag(lineNo, fmt.Sprintf("Too many participants (limit %d)", MaxNodes), SeverityError)
				p.participantCapWarned = true
			}
			return -1
		}
		p.ast.Participants = append(p.ast.Participants, SeqParticipant{
			ID:       id,
			Label:    stripQuotes(id),
			BoxIndex: -1,
			Order:    len(p.ast.Participants),
			SrcSpan:  p.curSpan,
		})
		i = len(p.ast.Participants) - 1
	}
	if declaration && p.boxIndex >= 0 && p.ast.Participants[i].BoxIndex < 0 {
		p.ast.Participants[i].BoxIndex = p.boxIndex
	}
	return i
}

func (p *seqParser) parseParticipant(rest string, actorFigure bool, lineNo int) {
	// A participant's `@{ … }` configuration is kept in the source and not
	// read.
	if cfg := strings.Index(rest, "@{"); cfg >= 0 {
		close := strings.IndexByte(rest[cfg:], '}')
		p.diag(lineNo, "Participant configuration (`@{...}`) is ignored in Kvit", SeverityWarning)
		tail := ""
		if close >= 0 {
			tail = " " + trimSpace(rest[cfg+close+1:])
		}
		rest = trimSpace(trimSpace(rest[:cfg]) + tail)
	}
	// `participant A as Alice`: the alias starts after the first ` as `, in
	// any case.
	id := rest
	label := ""
	rs := []rune(rest)
	for i := 0; i+4 <= len(rs); i++ {
		if strings.EqualFold(string(rs[i:i+4]), " as ") {
			id = trimSpace(string(rs[:i]))
			label = stripWrap(string(rs[i+4:]))
			break
		}
	}
	if id == "" {
		p.diag(lineNo, "Expected a participant name", SeverityError)
		return
	}
	i := p.ensureParticipant(id, lineNo, true)
	if i < 0 {
		return
	}
	if label != "" {
		p.ast.Participants[i].Label = leftRunes(stripQuotes(label), MaxLabelChars)
	}
	if actorFigure {
		p.ast.Participants[i].ActorFigure = true
	}
}

func (p *seqParser) parseBox(rest string, lineNo int) {
	if p.boxIndex >= 0 {
		p.diag(lineNo, "`box` cannot nest", SeverityError)
		return
	}
	var box SeqBox
	title := stripWrap(rest)
	// The first word may be a colour: a name, #hex, or rgb() or rgba().
	if title != "" {
		first := ""
		if hasPrefixFold(title, "rgb") {
			if close := strings.IndexByte(title, ')'); close > 0 {
				first = title[:close+1]
			}
		}
		if first == "" {
			first, _, _ = strings.Cut(title, " ")
		}
		switch {
		case strings.EqualFold(first, "transparent"):
			title = trimSpace(title[len(first):])
		case hasPrefixFold(first, "rgb"):
			// rgb(r,g,b[,a]): the components are read and the alpha
			// dropped.
			fr := []rune(first)
			open := indexRuneFrom(fr, '(', 0)
			parts := splitSkipEmpty(string(midRunes(fr, open+1, len(fr)-open-2)), ",")
			if len(parts) >= 3 {
				rr, ok1 := qtToInt(parts[0])
				gg, ok2 := qtToInt(parts[1])
				bb, ok3 := qtToInt(parts[2])
				if ok1 && ok2 && ok3 {
					box.Color = ColorRGB(rr, gg, bb)
					title = trimSpace(title[len(first):])
				}
			}
		default:
			if c := ParseColor(first); c.Set {
				box.Color = c
				title = trimSpace(title[len(first):])
			}
		}
	}
	box.Title = title
	p.ast.Boxes = append(p.ast.Boxes, box)
	p.boxIndex = len(p.ast.Boxes) - 1
	p.scopes = append(p.scopes, seqScope{isBox: true, line: lineNo})
}

func (p *seqParser) parseNote(rest string, lineNo int) {
	e := newSeqEvent(EventNote, lineNo)
	var tail string
	var ok bool
	if tail, ok = keyword(rest, "left of", true); ok {
		e.Placement = PlaceLeftOf
	} else if tail, ok = keyword(rest, "right of", true); ok {
		e.Placement = PlaceRightOf
	} else if tail, ok = keyword(rest, "over", true); ok {
		e.Placement = PlaceOver
	} else {
		p.diag(lineNo, "Expected `left of`, `right of`, or `over` after `note`", SeverityError)
		return
	}
	colon := strings.IndexByte(tail, ':')
	if colon < 0 {
		p.diag(lineNo, "Expected `:` and note text", SeverityError)
		return
	}
	actors := trimSpace(tail[:colon])
	e.Text = leftRunes(stripWrap(tail[colon+1:]), MaxLabelChars)
	parts := splitSkipEmpty(actors, ",")
	if len(parts) == 0 {
		p.diag(lineNo, "Expected an actor after `note`", SeverityError)
		return
	}
	if len(parts) > 2 {
		p.diag(lineNo, "`note over` takes at most two actors", SeverityWarning)
	}
	e.From = trimSpace(parts[0])
	if e.Placement == PlaceOver && len(parts) >= 2 {
		e.To = trimSpace(parts[1])
	}
	if p.ensureParticipant(e.From, lineNo, false) < 0 {
		return
	}
	if e.To != "" && p.ensureParticipant(e.To, lineNo, false) < 0 {
		return
	}
	p.addEvent(e)
}

func (p *seqParser) parseAutonumber(rest string, lineNo int) {
	e := newSeqEvent(EventAutonumber, lineNo)
	parts := splitSkipEmpty(rest, " ")
	if len(parts) > 0 && strings.EqualFold(parts[0], "off") {
		e.AutonumberShown = false
	} else {
		if len(parts) >= 1 {
			if start, ok := qtToInt(parts[0]); ok {
				e.AutonumberStart = start
			}
		}
		if len(parts) >= 2 {
			if step, ok := qtToInt(parts[1]); ok {
				e.AutonumberStep = step
			}
		}
	}
	p.addEvent(e)
}

// parseMessage reads `A->>B: text`. It reports false when the line has no
// arrow, and true when it was a message, even one with an error.
func (p *seqParser) parseMessage(line string, lineNo int) bool {
	// The first position with an arrow; the longest arrow there wins.
	arrowPos := -1
	var arrow seqArrow
	for i := 0; i < len(line) && arrowPos < 0; i++ {
		for _, d := range seqArrows {
			if strings.HasPrefix(line[i:], d.tok) {
				arrowPos = i
				arrow = d
				break
			}
		}
	}
	if arrowPos < 0 {
		return false
	}

	e := newSeqEvent(EventMessage, lineNo)
	e.Line = arrow.line
	e.Head = arrow.head
	e.Bidirectional = arrow.bidir

	fromPart := trimSpace(line[:arrowPos])
	rest := trimSpace(line[arrowPos+len(arrow.tok):])

	// A central `()` connection is drawn as a plain message.
	central := false
	if strings.HasSuffix(fromPart, "()") {
		fromPart = trimSpace(fromPart[:len(fromPart)-2])
		central = true
	}
	if strings.HasPrefix(rest, "()") {
		rest = trimSpace(rest[2:])
		central = true
	}
	if central && !p.centralConnWarned {
		p.diag(lineNo, "Central connections (`()`) are rendered as plain messages", SeverityWarning)
		p.centralConnWarned = true
	}

	// The +/- activation shorthand before the target.
	if strings.HasPrefix(rest, "+") {
		e.ActivateTarget = true
		rest = trimSpace(rest[1:])
	} else if strings.HasPrefix(rest, "-") {
		e.DeactivateSource = true
		rest = trimSpace(rest[1:])
	}

	colon := strings.IndexByte(rest, ':')
	if fromPart == "" {
		p.diag(lineNo, "Expected an actor before the arrow", SeverityError)
		return true
	}
	if colon < 0 {
		p.diag(lineNo, "Expected `:` and message text after the arrow", SeverityError)
		return true
	}
	e.From = fromPart
	e.To = trimSpace(rest[:colon])
	e.Text = leftRunes(stripWrap(rest[colon+1:]), MaxLabelChars)
	if e.To == "" {
		p.diag(lineNo, "Expected a target actor", SeverityError)
		return true
	}
	if p.ensureParticipant(e.From, lineNo, false) < 0 || p.ensureParticipant(e.To, lineNo, false) < 0 {
		return true
	}
	p.addEvent(e)
	return true
}

// seqBlockKeywords open a fragment. `par_over` is drawn as `par`.
var seqBlockKeywords = []struct {
	kw    string
	block SeqBlock
}{
	{"loop", BlockLoop}, {"alt", BlockAlt}, {"opt", BlockOpt}, {"par_over", BlockPar},
	{"par", BlockPar}, {"critical", BlockCritical}, {"break", BlockBreak}, {"rect", BlockRect},
}

// seqDividers divide a fragment, and are checked against the fragment they
// are in.
var seqDividers = []struct {
	kw     string
	block  SeqBlock
	inside string
}{
	{"else", BlockAlt, "alt"},
	{"and", BlockPar, "par"},
	{"option", BlockCritical, "critical"},
}

func (p *seqParser) parseStatement(line string, lineNo int) {
	kw := func(k string) (string, bool) { return keyword(line, k, true) }

	if rest, ok := kw("sequencediagram"); ok {
		if rest != "" {
			p.parseStatement(rest, lineNo)
		}
		return
	}
	if rest, ok := kw("participant"); ok {
		p.parseParticipant(rest, false, lineNo)
		return
	}
	if rest, ok := kw("actor"); ok {
		p.parseParticipant(rest, true, lineNo)
		return
	}
	if rest, ok := kw("create"); ok {
		// The create and destroy lifecycle is not drawn; the participant is.
		p.diag(lineNo, "`create` lifecycles are rendered as ordinary participants", SeverityWarning)
		if inner, ok := keyword(rest, "participant", true); ok {
			p.parseParticipant(inner, false, lineNo)
		} else if inner, ok := keyword(rest, "actor", true); ok {
			p.parseParticipant(inner, true, lineNo)
		} else {
			p.parseParticipant(rest, false, lineNo)
		}
		return
	}
	if rest, ok := kw("destroy"); ok {
		p.diag(lineNo, "`destroy` lifecycles are rendered as ordinary participants", SeverityWarning)
		p.ensureParticipant(trimSpace(rest), lineNo, false)
		return
	}
	if rest, ok := kw("box"); ok {
		if len(p.scopes) >= MaxDepth {
			p.diag(lineNo, fmt.Sprintf("Nesting too deep (limit %d)", MaxDepth), SeverityError)
			return
		}
		p.parseBox(rest, lineNo)
		return
	}
	if _, ok := kw("end"); ok {
		if len(p.scopes) == 0 {
			p.diag(lineNo, "`end` without an open block", SeverityError)
			return
		}
		top := p.scopes[len(p.scopes)-1]
		p.scopes = p.scopes[:len(p.scopes)-1]
		if top.isBox {
			p.boxIndex = -1
		} else {
			p.addEvent(newSeqEvent(EventBlockEnd, lineNo))
		}
		return
	}
	rest, ok := kw("activate")
	if !ok {
		rest, ok = kw("deactivate")
	}
	if ok {
		kind := EventDeactivate
		if hasPrefixFold(line, "activate") {
			kind = EventActivate
		}
		e := newSeqEvent(kind, lineNo)
		e.From = trimSpace(rest)
		if p.ensureParticipant(e.From, lineNo, false) < 0 {
			return
		}
		p.addEvent(e)
		return
	}
	if rest, ok := kw("note"); ok {
		p.parseNote(rest, lineNo)
		return
	}
	if rest, ok := kw("autonumber"); ok {
		p.parseAutonumber(rest, lineNo)
		return
	}
	for _, b := range seqBlockKeywords {
		if rest, ok := kw(b.kw); ok {
			if len(p.scopes) >= MaxDepth {
				p.diag(lineNo, fmt.Sprintf("Nesting too deep (limit %d)", MaxDepth), SeverityError)
				return
			}
			e := newSeqEvent(EventBlockStart, lineNo)
			e.Block = b.block
			e.BlockLabel = leftRunes(stripWrap(rest), MaxLabelChars)
			p.addEvent(e)
			p.scopes = append(p.scopes, seqScope{block: b.block, line: lineNo})
			return
		}
	}
	for _, d := range seqDividers {
		if rest, ok := kw(d.kw); ok {
			// The innermost scope that is not a box.
			var top *seqScope
			for i := len(p.scopes) - 1; i >= 0; i-- {
				if !p.scopes[i].isBox {
					top = &p.scopes[i]
					break
				}
			}
			if top == nil || top.block != d.block {
				p.diag(lineNo, fmt.Sprintf("`%s` outside a `%s` block", d.kw, d.inside), SeverityError)
				return
			}
			e := newSeqEvent(EventBlockDivider, lineNo)
			e.Block = d.block
			e.BlockLabel = leftRunes(stripWrap(rest), MaxLabelChars)
			p.addEvent(e)
			return
		}
	}
	if rest, ok := kw("title"); ok {
		p.ast.Title = rest
		return
	}
	if rest, ok := cutPrefixFold(line, "title:"); ok {
		p.ast.Title = trimSpace(rest)
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
	// Popup-menu statements: kept, never drawn.
	for _, k := range []string{"links", "link", "properties", "details"} {
		if rest, ok := kw(k); ok {
			p.diag(lineNo, fmt.Sprintf("`%s` popups are not supported in Kvit", k), SeverityWarning)
			if colon := strings.IndexByte(rest, ':'); colon > 0 {
				p.ensureParticipant(trimSpace(rest[:colon]), lineNo, false)
			}
			return
		}
	}

	if p.parseMessage(line, lineNo) {
		return
	}

	p.diag(lineNo, "Unrecognized sequence statement: "+leftRunes(line, 40), SeverityError)
}

// accDescrText is the description of a one-line `accDescr: text` or
// `accDescr { text }`, and false when the line has neither `:` nor `{`.
func accDescrText(line string) (string, bool) {
	colon := strings.IndexByte(line, ':')
	brace := strings.IndexByte(line, '{')
	if colon >= 0 && (brace < 0 || colon < brace) {
		return trimSpace(line[colon+1:]), true
	}
	if brace >= 0 {
		return trimSpace(strings.ReplaceAll(line[brace+1:], "}", "")), true
	}
	return "", false
}

// accDescrLine adds one line of a multi-line `accDescr { … }` to descr, and
// reports whether the line closes it.
func accDescrLine(descr *string, raw string) (closed bool) {
	close := strings.IndexByte(raw, '}')
	piece := raw
	if close >= 0 {
		piece = raw[:close]
	}
	if t := trimSpace(piece); t != "" {
		if *descr != "" {
			*descr += " "
		}
		*descr += t
	}
	return close >= 0
}

// accDescrOpens reports whether probe opens a multi-line `accDescr {` with
// no `}` on the same line, and sets descr to any text after the brace.
func accDescrOpens(descr *string, probe string, fold bool) bool {
	isAcc := strings.HasPrefix(probe, "accDescr")
	if fold {
		isAcc = hasPrefixFold(probe, "accDescr")
	}
	if !isAcc || !strings.Contains(probe, "{") || strings.Contains(probe, "}") {
		return false
	}
	if first := trimSpace(probe[strings.IndexByte(probe, '{')+1:]); first != "" {
		*descr = first
	}
	return true
}

// lineOffsets is where each of lines starts in their joined text, in runes,
// with one more entry for the end.
func lineOffsets(lines []string) []int {
	offsets := make([]int, len(lines)+1)
	for i, l := range lines {
		offsets[i+1] = offsets[i] + runeLen(l) + 1
	}
	return offsets
}

func (p *seqParser) run(body string) {
	// The body is split as it is (a `\r` at the end of a line trims away
	// with the other white space), so statement spans are spans of the
	// fence's text.
	physical := strings.Split(body, "\n")
	lineOffset := lineOffsets(physical)

	inAccDescr := false
	for li, raw := range physical {
		if inAccDescr {
			if accDescrLine(&p.ast.AccDescr, raw) {
				inAccDescr = false
			}
			continue
		}

		// Comments: `%%` to the end of the line, and a whole line starting
		// with `#`. A `#` inside a line stays, because it begins entity
		// escapes such as `#lt;`.
		if cut := strings.Index(raw, "%%"); cut >= 0 {
			raw = raw[:cut]
		}
		probe := trimSpace(raw)
		if strings.HasPrefix(probe, "#") {
			continue
		}
		if accDescrOpens(&p.ast.AccDescr, probe, true) {
			inAccDescr = true
			continue
		}

		// `;` separates statements as a line break does, except where it
		// ends a `#code;` escape (`#lt;`, `#59;`), which stays in the text.
		rs := []rune(raw)
		var segments [][]rune
		segStart := 0
		for i, r := range rs {
			if r != ';' {
				continue
			}
			j := i - 1
			for j > segStart && isLetterOrNumber(rs[j]) {
				j--
			}
			if j >= segStart && rs[j] == '#' && j < i-1 {
				continue // an escape, not a separator
			}
			segments = append(segments, rs[segStart:i])
			segStart = i + 1
		}
		segments = append(segments, rs[segStart:])
		segOffset := 0
		for _, segment := range segments {
			seg := string(segment)
			if line := trimSpace(seg); line != "" {
				p.curSpan = Span{Start: p.base + lineOffset[li] + segOffset + leadingSpaces(seg), Length: runeLen(line)}
				p.parseStatement(line, li+1)
			}
			segOffset += len(segment) + 1
		}
	}

	for _, s := range p.scopes {
		if s.isBox {
			p.diag(s.line, "`box` is missing its `end`", SeverityError)
		} else {
			p.diag(s.line, "Block is missing its `end`", SeverityError)
		}
	}
}
