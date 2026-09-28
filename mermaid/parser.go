package mermaid

import (
	"fmt"
	"math"
	"slices"
	"strings"
	"unicode"
)

// Resource limits. A note is untrusted input, arriving by import, paste or
// sync, so everything a diagram can make the parser allocate has a ceiling.
const (
	MaxNodes      = 1000
	MaxEdges      = 2000
	MaxDepth      = 32
	MaxLabelChars = 16 * 1024
	// MaxSourceChars is the ceiling on the whole source, front matter
	// included. It is checked before anything splits or copies the source;
	// checked later, a large enough front-matter block would have been
	// copied and scanned in full before anyone looked at its size.
	MaxSourceChars = 256 * 1024
	// MaxFrontMatterChars is how far the closing `---` of the front matter
	// is looked for, and MaxFrontMatterTitleChars how much of its `title:`
	// is kept. Mermaid's front matter holds a few short keys; these allow a
	// very generous one and keep the title about the size of a node label.
	MaxFrontMatterChars      = 8 * 1024
	MaxFrontMatterTitleChars = 1024
)

// MaxPinnedCoordinate is the largest distance from the origin, in logical
// pixels, a `%% mermaid-flow:pos` line can pin a node's centre at. A
// coordinate beyond it is clamped to it, so the node stays visible at the
// edge. It is Diagram::kMaxPinnedCoordinate of the Qt app's diagrambudget.h.
const MaxPinnedCoordinate = 200000.0

// newParseResult is a ParseResult with the Qt app's defaults where Go's zero
// values differ.
func newParseResult() ParseResult {
	var r ParseResult
	r.Flowchart.ClassDefs = map[string]ClassDef{}
	r.Flowchart.PosLine = NoSpan
	r.Class.ClassDefs = map[string]ClassDef{}
	r.State.ClassDefs = map[string]ClassDef{}
	r.Er.ClassDefs = map[string]ClassDef{}
	return r
}

func (r *ParseResult) diag(line, column int, message string, severity Severity) {
	r.Diagnostics = append(r.Diagnostics, Diagnostic{Line: line, Column: column, Message: message, Severity: severity})
}

// ParseStyleDeclarations reads declarations such as
// `fill:#f9f,stroke:#333,stroke-width:2px` into def. Only fill, stroke,
// stroke-width, stroke-dasharray and font-weight are read; any other
// property is ignored.
func ParseStyleDeclarations(def *ClassDef, styles string) {
	for _, declRaw := range splitSkipEmpty(styles, ",") {
		decl := trimSpace(declRaw)
		colon := strings.IndexByte(decl, ':')
		if colon < 0 {
			continue
		}
		key := toLower(trimSpace(decl[:colon]))
		val := trimSpace(decl[colon+1:])
		switch key {
		case "fill":
			if c := ParseColor(val); c.Set {
				def.Fill = c
				def.HasFill = true
			}
		case "stroke":
			if c := ParseColor(val); c.Set {
				def.Stroke = c
				def.HasStroke = true
			}
		case "stroke-width":
			if w, ok := qtToDouble(strings.ReplaceAll(val, "px", "")); ok {
				def.StrokeWidth = w
			}
		case "stroke-dasharray":
			def.Dashed = val != ""
		case "font-weight":
			def.Bold = strings.Contains(val, "bold")
		}
	}
}

// frontMatterLine is the trimmed line of src starting at from, and where the
// next line starts. Reading lines this way keeps the front-matter scan from
// copying the whole source into a list of lines.
func frontMatterLine(src []rune, from int) (line string, next int) {
	end := indexRuneFrom(src, '\n', from)
	next = end + 1
	if end < 0 {
		end = len(src)
		next = len(src)
	}
	return trimSpace(string(src[from:end])), next
}

// stripFrontMatter takes a leading `---` … `---` block off the source before
// lexing, so its fences are never read as links. Only its `title` is used.
// The body is a suffix of the source, so a span in the body moves by
// bodyOffset.
func stripFrontMatter(src []rune) (body []rune, title string, bodyOffset int) {
	first, pos := frontMatterLine(src, 0)
	if first != "---" {
		return src, "", 0
	}
	// The closing fence is looked for only within the front-matter limit.
	// Past it the block counts as not closed, so a block of megabytes is
	// never split off and passed on as not part of the body.
	limit := min(len(src), pos+MaxFrontMatterChars)
	close := -1
	titleValue := ""
	for pos < limit {
		line, next := frontMatterLine(src, pos)
		if line == "---" {
			close = next
			break
		}
		if strings.HasPrefix(line, "title:") {
			titleValue = trimSpace(line[len("title:"):])
		}
		pos = next
	}
	if close < 0 {
		return src, "", 0 // not closed, or too long: left to the lexer
	}
	return src[close:], leftRunes(titleValue, MaxFrontMatterTitleChars), close
}

// directionFromWord reads a direction, including the header symbols of
// flow.jison (flowDb's setDirection).
func directionFromWord(w string) (Direction, bool) {
	switch w {
	case "TB", "TD", "v", "BR":
		return TB, true
	case "BT", "^":
		return BT, true
	case "LR", ">":
		return LR, true
	case "RL", "<":
		return RL, true
	}
	return TB, false
}

// familyFromHeader is the family a header keyword names. `flowchart-elk` and
// `swimlane-beta` are flowchart headers that choose another layout engine in
// Mermaid; Kvit reads them as flowcharts and warns that it lays them out its
// own way.
func familyFromHeader(word string) DiagramType {
	switch {
	case word == "flowchart" || word == "graph" || word == "flowchart-elk" || word == "swimlane-beta":
		return Flowchart
	case hasPrefixFold(word, "sequenceDiagram"):
		return Sequence
	case strings.HasPrefix(word, "classDiagram"):
		return Class
	case strings.HasPrefix(word, "stateDiagram"):
		return State
	case strings.HasPrefix(word, "erDiagram"):
		return Er
	}
	return Unsupported
}

// shapeFromName reads the shape name of an `@{ shape: name }` block (the
// extended shapes of Mermaid 11) as one of Kvit's shapes. An unknown name
// gives Rect, and the caller warns.
func shapeFromName(raw string) (NodeShape, bool) {
	switch toLower(trimSpace(raw)) {
	case "rect", "rectangle", "proc", "process", "sq", "square":
		return ShapeRect, true
	case "rounded", "event":
		return ShapeRoundRect, true
	case "stadium", "pill", "terminal", "start", "stop":
		return ShapeStadium, true
	case "subroutine", "subproc", "fr-rect", "framed-rectangle", "subprocess":
		return ShapeSubroutine, true
	case "cyl", "cylinder", "db", "database":
		return ShapeCylinder, true
	case "circle", "circ":
		return ShapeCircle, true
	case "dbl-circ", "double-circle":
		return ShapeDoubleCircle, true
	case "diam", "diamond", "decision", "question":
		return ShapeRhombus, true
	case "hex", "hexagon", "prepare":
		return ShapeHexagon, true
	case "lean-r", "lean-right", "in-out":
		return ShapeParallelogram, true
	case "lean-l", "lean-left", "out-in":
		return ShapeParallelogramAlt, true
	case "trap-b", "trapezoid-bottom", "trap", "trapezoid", "priority":
		return ShapeTrapezoid, true
	case "trap-t", "trapezoid-top", "inv-trapezoid", "manual":
		return ShapeTrapezoidAlt, true
	case "odd", "flag":
		return ShapeOdd, true
	case "ellipse":
		return ShapeEllipse, true
	}
	return ShapeRect, false
}

const posPrefix = "mermaid-flow:pos"

// parsePosLines reads `%% mermaid-flow:pos …` lines, which pin nodes where
// the reader dragged them. The first such line counts; later ones are
// ignored with a warning. A malformed entry is skipped. obsidian-mermaid-flow
// may add `,width,height` to an entry; it is read and dropped, because a
// node's size always follows from its label and shape.
func parsePosLines(body []rune, baseOffset int, result *ParseResult) {
	ast := &result.Flowchart
	prefix := []rune(posPrefix)
	lineStart := 0
	lineNo := 0
	for lineStart <= len(body) {
		lineNo++
		lineEnd := indexRuneFrom(body, '\n', lineStart)
		if lineEnd < 0 {
			lineEnd = len(body)
		}
		line := body[lineStart:lineEnd]
		trimmed := []rune(trimSpace(string(line)))
		var comment []rune
		if len(trimmed) >= 2 && trimmed[0] == '%' && trimmed[1] == '%' {
			comment = []rune(trimSpace(string(trimmed[2:])))
		}
		// The directive has to be the whole word: `mermaid-flow:position …`
		// is an ordinary comment, and reading it as a pos line would hand it
		// to the next drag to overwrite.
		isPosDirective := strings.HasPrefix(string(comment), posPrefix) &&
			(len(comment) == len(prefix) || unicode.IsSpace(comment[len(prefix)]))
		if isPosDirective {
			if ast.HasPosLine {
				result.diag(lineNo, 1, "Only one `%% mermaid-flow:pos` line is recognized; this one is ignored", SeverityWarning)
			} else {
				parsePosEntries(line, trimmed, comment, baseOffset+lineStart, lineNo, result)
			}
		}
		lineStart = lineEnd + 1
		if lineEnd == len(body) {
			break
		}
	}
}

// parsePosEntries reads the entries of the pos line line, which starts at
// lineOffset in the source; trimmed is the line trimmed and comment what
// follows its `%%`, trimmed.
func parsePosEntries(line, trimmed, comment []rune, lineOffset, lineNo int, result *ParseResult) {
	ast := &result.Flowchart
	ast.HasPosLine = true
	ast.PosLine = Span{Start: lineOffset, Length: len(line)}
	afterPrefix := comment[len([]rune(posPrefix)):]
	entries := []rune(trimSpace(string(afterPrefix)))
	clamped := false
	// Where the entries start in the source, so each entry's id can be found
	// there and renamed in place.
	afterMarker := 2
	for afterMarker < len(trimmed) && unicode.IsSpace(trimmed[afterMarker]) {
		afterMarker++
	}
	entriesStart := lineOffset + leadingSpaces(string(line)) + afterMarker +
		len([]rune(posPrefix)) + leadingSpaces(string(afterPrefix))
	// Walk the entries keeping each one's offset, which splitting would lose.
	scan := 0
	for scan < len(entries) {
		for scan < len(entries) && entries[scan] == ' ' {
			scan++
		}
		if scan >= len(entries) {
			break
		}
		partStart := scan
		for scan < len(entries) && entries[scan] != ' ' {
			scan++
		}
		part := entries[partStart:scan]
		eq := indexRuneFrom(part, '=', 0)
		if eq <= 0 {
			continue // malformed: skipped
		}
		nums := splitSkipEmpty(string(part[eq+1:]), ",")
		if len(nums) != 2 && len(nums) != 4 {
			continue
		}
		x, okX := qtToDouble(nums[0])
		y, okY := qtToDouble(nums[1])
		if !okX || !okY {
			continue
		}
		// A number such as "inf" or "nan" reads as a double, and a centre
		// that is not finite spoils every bound worked out from it, so the
		// entry is dropped. A finite one out of range is clamped, so the node
		// stays visible at the edge.
		if math.IsInf(x, 0) || math.IsNaN(x) || math.IsInf(y, 0) || math.IsNaN(y) {
			clamped = true
			continue
		}
		if math.Abs(x) > MaxPinnedCoordinate || math.Abs(y) > MaxPinnedCoordinate {
			x = max(-MaxPinnedCoordinate, min(x, MaxPinnedCoordinate))
			y = max(-MaxPinnedCoordinate, min(y, MaxPinnedCoordinate))
			clamped = true
		}
		ast.PosEntries = append(ast.PosEntries, PosEntry{
			ID:     string(part[:eq]),
			X:      x,
			Y:      y,
			IDSpan: Span{Start: entriesStart + partStart, Length: eq},
		})
	}
	if clamped {
		result.diag(lineNo, 1, "Arrangement coordinates outside the supported range were clamped", SeverityWarning)
	}
}

// Parse reads a Mermaid fence. It finds the family from the header and, for
// the families Kvit draws, builds the syntax tree with diagnostics whose
// lines and columns count from one. Parsing goes on past a bad statement, so
// one bad line still leaves the later diagnostics useful. A family Kvit does
// not draw gets only the unsupported-family diagnostic, and its source is
// never discarded. Label text is plain text, never HTML.
func Parse(source string) ParseResult {
	result := newParseResult()

	// The limit covers the whole source and is checked before anything
	// splits or copies it.
	if runeLen(source) > MaxSourceChars {
		result.diag(1, 1, fmt.Sprintf("Diagram source exceeds %d KiB", MaxSourceChars/1024), SeverityError)
		return result
	}

	bodyRunes, frontTitle, bodyOffset := stripFrontMatter([]rune(source))
	body := string(bodyRunes)

	// Find the header keyword on the first line with content before any
	// family's lexing, because the families split text very differently.
	headerWord := ""
	headerLine, headerColumn := 1, 1
	for i, l := range strings.Split(body, "\n") {
		if cut := strings.Index(l, "%%"); cut >= 0 {
			l = l[:cut]
		}
		t := trimSpace(l)
		if t == "" {
			continue
		}
		j := strings.IndexFunc(t, func(r rune) bool { return unicode.IsSpace(r) || r == ';' })
		if j < 0 {
			j = len(t)
		}
		headerWord = t[:j]
		headerLine = i + 1
		headerColumn = leadingSpaces(l) + 1
		if headerWord != "" {
			break // a line starting with `;` gives no word, and the search goes on
		}
	}

	if headerWord == "" {
		result.diag(1, 1, "Empty diagram — expected a diagram type declaration such as `flowchart`", SeverityError)
		return result
	}

	result.Type = familyFromHeader(headerWord)
	result.FamilyName = headerWord

	switch result.Type {
	case Sequence:
		result.Supported = true
		ParseSequence(body, bodyOffset, &result)
		if result.Sequence.Title == "" {
			result.Sequence.Title = frontTitle
		}
		if len(result.Sequence.Participants) == 0 {
			result.diag(headerLine, headerColumn, "Sequence diagram has no participants", SeverityWarning)
		}
		return result
	case Class:
		result.Supported = true
		ParseClassDiagram(body, bodyOffset, &result)
		if result.Class.Title == "" {
			result.Class.Title = frontTitle
		}
		if len(result.Class.Classes) == 0 {
			result.diag(headerLine, headerColumn, "Class diagram has no classes", SeverityWarning)
		}
		return result
	case State:
		result.Supported = true
		ParseStateDiagram(body, bodyOffset, &result)
		if result.State.Title == "" {
			result.State.Title = frontTitle
		}
		if len(result.State.States) == 0 {
			result.diag(headerLine, headerColumn, "State diagram has no states", SeverityWarning)
		}
		return result
	case Er:
		result.Supported = true
		ParseErDiagram(body, bodyOffset, &result)
		if result.Er.Title == "" {
			result.Er.Title = frontTitle
		}
		if len(result.Er.Entities) == 0 {
			result.diag(headerLine, headerColumn, "ER diagram has no entities", SeverityWarning)
		}
		return result
	case Flowchart:
	default:
		result.Supported = false
		result.diag(headerLine, headerColumn, "Unsupported Mermaid diagram type in this Kvit version: "+headerWord, SeverityError)
		return result
	}

	// Group the tokens into statements at each separator.
	var statements [][]Token
	var current []Token
	for _, t := range LexRunes(bodyRunes) {
		if t.Kind == TokenSep {
			if len(current) > 0 {
				statements = append(statements, current)
				current = nil
			}
		} else {
			current = append(current, t)
		}
	}
	if len(current) > 0 {
		statements = append(statements, current)
	}

	// The header is the first statement that starts with a word.
	headerStatement := -1
	for s, st := range statements {
		if len(st) > 0 && st[0].Kind == TokenWord {
			headerStatement = s
			break
		}
	}
	if headerStatement < 0 {
		result.diag(1, 1, "Empty diagram — expected a diagram type declaration such as `flowchart`", SeverityError)
		return result
	}

	// A flowchart: the header's direction, then the statements.
	result.Supported = true
	header := statements[headerStatement]
	if headerWord != "flowchart" && headerWord != "graph" {
		h := header[0]
		result.diag(h.Line, h.Column, fmt.Sprintf("`%s` selects a layout engine Kvit does not ship; rendered with the standard layered layout", headerWord), SeverityWarning)
	}
	if len(header) >= 2 && header[1].Kind == TokenWord {
		if d, ok := directionFromWord(header[1].Text); ok {
			result.Flowchart.Direction = d
		}
	}

	fp := flowParser{r: &result, ast: &result.Flowchart, base: bodyOffset}
	fp.run(statements, headerStatement, frontTitle)
	parsePosLines(bodyRunes, bodyOffset, &result)

	if len(result.Flowchart.Nodes) == 0 {
		result.diag(1, 1, "Flowchart has no nodes", SeverityWarning)
	}
	return result
}

// flowParser reads one flowchart's statements.
type flowParser struct {
	r             *ParseResult
	ast           *FlowchartAst
	subgraphStack []int // indexes into ast.Subgraphs
	base          int   // where the body starts in the source
	stmtSpan      Span  // the statement being read
	edgeCount     int
	nodeCapWarned bool
	edgeCapWarned bool
}

func (p *flowParser) diag(line, column int, message string, severity Severity) {
	p.r.diag(line, column, message, severity)
}

// ensureNode is the index of the node id, added if it is new, or -1 past the
// node limit. at is the token that names it.
func (p *flowParser) ensureNode(id string, at Token) int {
	index := p.ast.IndexOfNode(id)
	if index < 0 {
		if len(p.ast.Nodes) >= MaxNodes {
			if !p.nodeCapWarned {
				p.diag(at.Line, at.Column, fmt.Sprintf("Too many nodes (limit %d); extra nodes are not rendered", MaxNodes), SeverityError)
				p.nodeCapWarned = true
			}
			return -1
		}
		p.ast.Nodes = append(p.ast.Nodes, Node{
			ID:        id,
			Label:     id,
			Order:     len(p.ast.Nodes),
			IDSpan:    NoSpan,
			LabelSpan: NoSpan,
			ShapeSpan: NoSpan,
		})
		index = len(p.ast.Nodes) - 1
	}
	// Every mention gets a span, the first one as the declaration. The id
	// can be part of a longer token (`A:::hot`, `A,B`).
	if within := strings.Index(at.Text, id); within >= 0 && id != "" {
		span := Span{Start: p.base + at.Offset + runeLen(at.Text[:within]), Length: runeLen(id)}
		n := &p.ast.Nodes[index]
		if !n.IDSpan.Valid() {
			n.IDSpan = span
		}
		n.RefSpans = append(n.RefSpans, span)
	}
	// Membership is recorded on every mention inside an open subgraph, so a
	// node declared earlier and only listed inside still belongs to it.
	if len(p.subgraphStack) > 0 {
		sg := &p.ast.Subgraphs[p.subgraphStack[len(p.subgraphStack)-1]]
		if !slices.Contains(sg.NodeIDs, id) {
			sg.NodeIDs = append(sg.NodeIDs, id)
		}
	}
	return index
}

// appendUnique appends s to list unless it is there already.
func appendUnique(list []string, s string) []string {
	if slices.Contains(list, s) {
		return list
	}
	return append(list, s)
}

// applyShapeData applies an `@{ key: value, … }` block (flow.jison
// shapeData). `shape` and `label` are read; any other key is ignored with a
// warning, because configuration keys are allowed only one by one.
func (p *flowParser) applyShapeData(nodeIndex int, data Token) {
	if nodeIndex < 0 {
		return
	}
	node := &p.ast.Nodes[nodeIndex]
	node.HasShapeData = true
	// Split into entries at commas and line breaks outside quotes, keeping
	// each entry's offset in the block so a value can be found in the source.
	type entry struct {
		text  []rune
		start int
	}
	text := []rune(data.Text)
	var entries []entry
	var cur []rune
	curStart := 0
	inQuote := false
	for i, ch := range text {
		switch {
		case ch == '"':
			inQuote = !inQuote
			cur = append(cur, ch)
		case !inQuote && (ch == ',' || ch == '\n'):
			entries = append(entries, entry{cur, curStart})
			cur = nil
			curStart = i + 1
		default:
			cur = append(cur, ch)
		}
	}
	entries = append(entries, entry{cur, curStart})
	for _, e := range entries {
		entryRaw := e.text
		entry := trimSpace(string(entryRaw))
		if entry == "" {
			continue
		}
		// Where the trimmed entry starts in the block's body.
		entryStart := e.start
		for entryStart < len(text) && unicode.IsSpace(text[entryStart]) && entryStart-e.start < len(entryRaw) {
			entryStart++
		}
		colon := strings.IndexByte(entry, ':')
		if colon < 0 {
			p.diag(data.Line, data.Column, "Malformed shape data entry: "+entry, SeverityWarning)
			continue
		}
		key := toLower(trimSpace(entry[:colon]))
		val := stripQuotePair(trimSpace(entry[colon+1:]), '"')
		switch key {
		case "shape":
			sh, known := shapeFromName(val)
			if !known {
				p.diag(data.Line, data.Column, fmt.Sprintf("Unknown shape \"%s\"; drawn as a rectangle", val), SeverityWarning)
			}
			node.Shape = sh
		case "label":
			node.Label = leftRunes(val, MaxLabelChars)
			// The value's span, quotes included, so an edit rewrites the
			// entry rather than adding brackets the block would override.
			rawVal := entry[colon+1:]
			valStart := entryStart + runeLen(entry[:colon]) + 1
			lead := leadingSpaces(rawVal)
			valLen := runeLen(trimSpace(rawVal))
			if data.LabelOffset >= 0 && valLen > 0 {
				node.LabelSpan = Span{Start: p.base + data.LabelOffset + valStart + lead, Length: valLen}
				node.LabelInShapeData = true
			}
		default:
			p.diag(data.Line, data.Column, fmt.Sprintf("Shape data key \"%s\" is ignored in Kvit", key), SeverityWarning)
		}
	}
}

// parseGroup reads `A & B & C`, each node with its optional shape, class and
// data block, from st[*idx], and returns the ids.
func (p *flowParser) parseGroup(st []Token, idx *int) []string {
	var ids []string
	for *idx < len(st) {
		t := st[*idx]
		if t.Kind != TokenWord {
			break
		}
		// A `:::class` at the end of the id.
		word := t.Text
		cls := ""
		if trip := strings.Index(word, ":::"); trip >= 0 {
			cls = word[trip+3:]
			word = word[:trip]
		}
		*idx++
		ni := p.ensureNode(word, t)
		// A shape and label straight after the id.
		if *idx < len(st) && st[*idx].Kind == TokenShape {
			sh := st[*idx]
			if ni >= 0 {
				label := sh.Text
				shape := sh.Shape
				if runeLen(label) > MaxLabelChars {
					p.diag(sh.Line, sh.Column, fmt.Sprintf("Label exceeds %d characters", MaxLabelChars), SeverityError)
					label = leftRunes(label, MaxLabelChars)
				}
				// The old `[|prop:value|text]` form of a vertex with
				// properties: the properties are ignored with a warning and
				// the text kept.
				if shape == ShapeRect && strings.HasPrefix(label, "|") {
					second := strings.IndexByte(label[1:], '|') + 1
					colon := strings.IndexByte(label, ':')
					if second > 0 && colon > 0 && colon < second {
						p.diag(sh.Line, sh.Column, "Node properties (`[|prop:value|…]`) are ignored in Kvit", SeverityWarning)
						label = trimSpace(label[second+1:])
					}
				}
				n := &p.ast.Nodes[ni]
				n.Shape = shape
				n.Label = label
				n.ShapeSpan = Span{Start: p.base + sh.Offset, Length: sh.Length}
				if sh.LabelOffset >= 0 {
					n.LabelSpan = Span{Start: p.base + sh.LabelOffset, Length: sh.LabelLength}
				}
			}
			*idx++
		}
		// A `:::class` written as its own token after the shape.
		if *idx < len(st) && st[*idx].Kind == TokenWord && strings.HasPrefix(st[*idx].Text, ":::") {
			cls = st[*idx].Text[3:]
			*idx++
		}
		// An `@{ shape: …, label: … }` block after the vertex.
		if *idx < len(st) && st[*idx].Kind == TokenShapeData {
			p.applyShapeData(ni, st[*idx])
			*idx++
		}
		if ni >= 0 && cls != "" {
			p.ast.Nodes[ni].Classes = appendUnique(p.ast.Nodes[ni].Classes, cls)
		}
		if ni >= 0 {
			ids = append(ids, word)
		}
		if *idx < len(st) && st[*idx].Kind == TokenAmp {
			*idx++
			continue
		}
		break
	}
	return ids
}

// parseChain reads `A --> B -->|label| C …` from st[start], adding an edge
// for every pair of nodes the groups on each side of a link name.
func (p *flowParser) parseChain(st []Token, start int) {
	idx := start
	left := p.parseGroup(st, &idx)
	if len(left) == 0 {
		if idx < len(st) {
			p.diag(st[idx].Line, st[idx].Column, "Expected a node", SeverityError)
		}
		return
	}
	for idx < len(st) {
		// An `e1@` edge id (flow.jison LINK_ID) before the link.
		edgeID := ""
		beforeID := idx
		if st[idx].Kind == TokenWord && len(st[idx].Text) > 1 && strings.HasSuffix(st[idx].Text, "@") &&
			idx+1 < len(st) && st[idx+1].Kind == TokenEdge {
			edgeID = st[idx].Text[:len(st[idx].Text)-1]
			idx++
		}
		if st[idx].Kind != TokenEdge {
			idx = beforeID
			break
		}
		edge := st[idx]
		idx++
		label := edge.EdgeLabel
		// A pipe label `-->|text|` after the link.
		pipeSpan := NoSpan
		if idx < len(st) && st[idx].Kind == TokenPipe {
			pipeStart := st[idx].Offset
			idx++
			var parts []string
			for idx < len(st) && st[idx].Kind != TokenPipe {
				if st[idx].Kind == TokenWord || st[idx].Kind == TokenShape {
					parts = append(parts, st[idx].Text)
				}
				idx++
			}
			if idx < len(st) && st[idx].Kind == TokenPipe {
				pipeSpan = Span{Start: p.base + pipeStart, Length: st[idx].Offset + 1 - pipeStart}
				idx++ // the closing pipe
			}
			label = stripQuotePair(strings.Join(parts, " "), '"')
		}
		right := p.parseGroup(st, &idx)
		if len(right) == 0 {
			p.diag(edge.Line, edge.Column, "Edge has no target node", SeverityError)
			break
		}
		for _, f := range left {
			for _, t := range right {
				if p.edgeCount >= MaxEdges {
					if !p.edgeCapWarned {
						p.diag(edge.Line, edge.Column, fmt.Sprintf("Too many edges (limit %d)", MaxEdges), SeverityError)
						p.edgeCapWarned = true
					}
					return
				}
				p.ast.Edges = append(p.ast.Edges, Edge{
					From:       f,
					To:         t,
					Label:      label,
					ID:         edgeID,
					Stroke:     edge.Stroke,
					ArrowStart: edge.ArrowStart,
					ArrowEnd:   edge.ArrowEnd,
					Invisible:  edge.Invisible,
					MinLen:     edge.MinLen,
					Order:      p.edgeCount,
					OpSpan:     Span{Start: p.base + edge.Offset, Length: edge.Length},
					PipeSpan:   pipeSpan,
					StmtSpan:   p.stmtSpan,
				})
				p.edgeCount++
			}
		}
		left = right
	}
}

// wordsFrom is the text of the word tokens of st from index from on.
func wordsFrom(st []Token, from int) []string {
	var words []string
	for _, t := range st[from:] {
		if t.Kind == TokenWord {
			words = append(words, t.Text)
		}
	}
	return words
}

func (p *flowParser) parseStatement(st []Token) {
	if len(st) == 0 {
		return
	}
	head := st[0]
	if head.Kind == TokenWord {
		kw := head.Text
		switch {
		case kw == "subgraph":
			if len(p.subgraphStack) >= MaxDepth {
				p.diag(head.Line, head.Column, fmt.Sprintf("Subgraph nesting too deep (limit %d)", MaxDepth), SeverityError)
				return
			}
			var sg Subgraph
			idx := 1
			// `subgraph My Long Title`: the title (textNoTags) can be several
			// words. The first word is also the subgraph's id; the words
			// joined are its title.
			var words []string
			for idx < len(st) && st[idx].Kind == TokenWord {
				words = append(words, st[idx].Text)
				idx++
			}
			if len(words) > 0 {
				sg.ID = words[0]
				sg.Title = stripQuotePair(strings.Join(words, " "), '"')
			}
			if idx < len(st) && st[idx].Kind == TokenShape {
				sg.Title = st[idx].Text
			}
			p.ast.Subgraphs = append(p.ast.Subgraphs, sg)
			p.subgraphStack = append(p.subgraphStack, len(p.ast.Subgraphs)-1)
			return
		case kw == "end":
			if len(p.subgraphStack) == 0 {
				p.diag(head.Line, head.Column, "`end` without a matching `subgraph`", SeverityError)
			} else {
				p.subgraphStack = p.subgraphStack[:len(p.subgraphStack)-1]
			}
			return
		case kw == "direction":
			if len(st) >= 2 && st[1].Kind == TokenWord {
				if d, ok := directionFromWord(st[1].Text); ok {
					if len(p.subgraphStack) > 0 {
						sg := &p.ast.Subgraphs[p.subgraphStack[len(p.subgraphStack)-1]]
						sg.Direction = d
						sg.HasDirection = true
					} else {
						p.ast.Direction = d
					}
				}
			}
			return
		case kw == "classDef":
			// `classDef a,b styles` defines every name in the list.
			if len(st) >= 2 && st[1].Kind == TokenWord {
				styles := strings.Join(wordsFrom(st, 2), " ")
				for _, name := range splitSkipEmpty(st[1].Text, ",") {
					def := p.ast.ClassDefs[name]
					ParseStyleDeclarations(&def, styles)
					p.ast.ClassDefs[name] = def
				}
			}
			return
		case kw == "class":
			// `class A,B,C className`
			if len(st) >= 3 && st[1].Kind == TokenWord && st[len(st)-1].Kind == TokenWord {
				cls := st[len(st)-1].Text
				for _, id := range splitSkipEmpty(st[1].Text, ",") {
					if ni := p.ensureNode(id, st[1]); ni >= 0 {
						p.ast.Nodes[ni].Classes = appendUnique(p.ast.Nodes[ni].Classes, cls)
					}
				}
			}
			return
		case kw == "style":
			// `style A fill:#f9f,…` styles one node. It becomes a class of
			// its own, so the scene applies it like any other.
			if len(st) >= 3 && st[1].Kind == TokenWord {
				id := st[1].Text
				synth := "__style_" + id
				def := p.ast.ClassDefs[synth]
				ParseStyleDeclarations(&def, strings.Join(wordsFrom(st, 2), " "))
				p.ast.ClassDefs[synth] = def
				if ni := p.ensureNode(id, st[1]); ni >= 0 {
					p.ast.Nodes[ni].Classes = appendUnique(p.ast.Nodes[ni].Classes, synth)
				}
			}
			return
		case kw == "click" || kw == "linkStyle":
			p.diag(head.Line, head.Column, fmt.Sprintf("`%s` is ignored in Kvit (interactivity and link styling are not supported)", kw), SeverityWarning)
			return
		case strings.HasPrefix(kw, "accTitle") || strings.HasPrefix(kw, "accDescr"):
			var rest []string
			// The keyword token has the text after it when written
			// `accTitle:x`.
			if colon := strings.IndexByte(kw, ':'); colon >= 0 && colon+1 < len(kw) {
				rest = append(rest, kw[colon+1:])
			}
			for _, t := range st[1:] {
				if t.Kind == TokenWord || t.Kind == TokenShape {
					rest = append(rest, t.Text)
				}
			}
			text := trimSpace(strings.Join(rest, " "))
			if strings.HasPrefix(kw, "accTitle") {
				p.ast.AccTitle = text
			} else {
				p.ast.AccDescr = text
			}
			return
		}
	}
	// Anything else is a chain of nodes and links.
	p.parseChain(st, 0)
}

func (p *flowParser) run(statements [][]Token, headerStatement int, accTitle string) {
	if accTitle != "" {
		p.ast.AccTitle = accTitle
	}
	for s, st := range statements {
		if s == headerStatement {
			continue
		}
		if len(st) > 0 {
			first, last := st[0], st[len(st)-1]
			p.stmtSpan = Span{Start: p.base + first.Offset, Length: last.Offset + last.Length - first.Offset}
		}
		p.parseStatement(st)
	}
}
