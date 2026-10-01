package mermaid

import (
	"fmt"
	"regexp"
	"slices"
	"sort"
	"strings"
	"unicode"
)

// On-diagram editing. Every gesture on a drawn diagram becomes an edit of
// the fence's text at the spans the parser recorded, never a rewrite of the
// whole fence from the syntax tree: everything outside the edited span stays
// as it was, comments and spacing included. Each function returns the whole
// new text of the fence, which the editor applies as one undo step, or the
// reason it refused. A flowchart edit is refused rather than applied
// approximately when its result would not parse with no more errors than
// before.

// EditResult is the outcome of an edit.
type EditResult struct {
	OK     bool
	Source string // the fence's whole new text, when OK
	Error  string // why the edit was refused, when not OK
	NewID  string // QuickAddNode: the id it gave the new node
}

// NodePosition is a node's centre in logical pixels, for WriteArrangement.
type NodePosition struct {
	ID   string
	X, Y float64
}

func editFail(why string) EditResult { return EditResult{Error: why} }

func editOK(source string) EditResult { return EditResult{OK: true, Source: source} }

// ---- editing runes ----

// replaceRunes replaces the n runes at pos with with: nothing when pos is
// past the end, and n cut to what is there.
func replaceRunes(rs []rune, pos, n int, with string) []rune {
	if pos < 0 || pos > len(rs) {
		return rs
	}
	n = max(0, min(n, len(rs)-pos))
	out := make([]rune, 0, len(rs)-n+len(with))
	out = append(out, rs[:pos]...)
	out = append(out, []rune(with)...)
	return append(out, rs[pos+n:]...)
}

func insertRunes(rs []rune, pos int, s string) []rune { return replaceRunes(rs, pos, 0, s) }

func removeRunes(rs []rune, pos, n int) []rune { return replaceRunes(rs, pos, n, "") }

// spanOf is the text of span in rs.
func spanOf(rs []rune, s Span) string { return string(midRunes(rs, s.Start, s.Length)) }

// ---- what every flowchart gesture starts from ----

// editStmt is a statement of the fence as the lexer groups them.
type editStmt struct {
	span      Span // from its first token to the end of its last, in the fence
	firstWord string
}

type editCtx struct {
	ok           bool
	err          string
	source       []rune
	pr           ParseResult
	stmts        []editStmt // in source order, the header included
	errorsBefore int
}

// frontMatterOffset is where the body starts after a leading `---` … `---`
// block, or 0.
func frontMatterOffset(src []rune) int {
	lines := strings.Split(string(src), "\n")
	if len(lines) == 0 || trimSpace(lines[0]) != "---" {
		return 0
	}
	for i := 1; i < len(lines); i++ {
		if trimSpace(lines[i]) == "---" {
			offset := 0
			for j := 0; j <= i; j++ {
				offset += runeLen(lines[j]) + 1
			}
			return offset
		}
	}
	return 0
}

func errorCount(pr *ParseResult) int {
	n := 0
	for _, d := range pr.Diagnostics {
		if d.Severity == SeverityError {
			n++
		}
	}
	return n
}

func makeEditCtx(source string) editCtx {
	ctx := editCtx{source: []rune(source), pr: Parse(source)}
	if ctx.pr.Type != Flowchart || !ctx.pr.Supported {
		ctx.err = "On-diagram editing applies to flowcharts only"
		return ctx
	}
	ctx.errorsBefore = errorCount(&ctx.pr)

	base := frontMatterOffset(ctx.source)
	var current []Token
	flush := func() {
		if len(current) == 0 {
			return
		}
		first, last := current[0], current[len(current)-1]
		s := editStmt{span: Span{Start: base + first.Offset, Length: last.Offset + last.Length - first.Offset}}
		if first.Kind == TokenWord {
			s.firstWord = first.Text
		}
		ctx.stmts = append(ctx.stmts, s)
		current = nil
	}
	for _, t := range LexRunes(midRunes(ctx.source, base, -1)) {
		if t.Kind == TokenSep {
			flush()
		} else {
			current = append(current, t)
		}
	}
	flush()
	ctx.ok = true
	return ctx
}

// postChecked accepts newSource only if it still parses as a flowchart with
// no more errors than before.
func postChecked(ctx *editCtx, newSource, newID string) EditResult {
	after := Parse(newSource)
	if after.Type != Flowchart || errorCount(&after) > ctx.errorsBefore {
		return editFail("The edit would leave the source invalid; refused")
	}
	r := editOK(newSource)
	r.NewID = newID
	return r
}

// postCheckedExpecting is postChecked that also checks the edit did what it
// set out to do. A source can parse perfectly and still say what it said
// before, and reporting success for that teaches the user that the gesture
// does nothing.
func postCheckedExpecting(ctx *editCtx, newSource string, intentHolds func(*ParseResult) bool, intentError string) EditResult {
	r := postChecked(ctx, newSource, "")
	if !r.OK {
		return r
	}
	after := Parse(newSource)
	if !intentHolds(&after) {
		return editFail(intentError)
	}
	return r
}

func (ctx *editCtx) findNode(id string) *Node {
	if i := ctx.pr.Flowchart.IndexOfNode(id); i >= 0 {
		return &ctx.pr.Flowchart.Nodes[i]
	}
	return nil
}

func stmtMentionsNode(s editStmt, n *Node) bool {
	for _, ref := range n.RefSpans {
		if ref.Start >= s.span.Start && ref.End() <= s.span.End() {
			return true
		}
	}
	return false
}

// mentionsOtherNode reports whether s mentions a node other than n.
func (ctx *editCtx) mentionsOtherNode(s editStmt, n *Node) bool {
	for i := range ctx.pr.Flowchart.Nodes {
		other := &ctx.pr.Flowchart.Nodes[i]
		if other.ID != n.ID && stmtMentionsNode(s, other) {
			return true
		}
	}
	return false
}

// lastMention is the index of the last statement that mentions n, or -1.
func (ctx *editCtx) lastMention(n *Node) int {
	anchor := -1
	for i, s := range ctx.stmts {
		if stmtMentionsNode(s, n) {
			anchor = i
		}
	}
	return anchor
}

// removalRange is what removing a statement takes out: its whole line and
// the line break after it. When another statement shares the line (with
// `;`), only the statement and one `;` beside it go.
func (ctx *editCtx) removalRange(stmtIndex int) (int, int) {
	src := ctx.source
	s := ctx.stmts[stmtIndex]
	lineStart := s.span.Start
	for lineStart > 0 && src[lineStart-1] != '\n' {
		lineStart--
	}
	lineEnd := s.span.End()
	for lineEnd < len(src) && src[lineEnd] != '\n' {
		lineEnd++
	}
	if lineEnd < len(src) {
		lineEnd++ // the line break goes with the line
	}
	shared := false
	for i, o := range ctx.stmts {
		if i != stmtIndex && o.span.End() > lineStart && o.span.Start < lineEnd {
			shared = true
		}
	}
	if !shared {
		return lineStart, lineEnd
	}
	// A shared line: the statement and one `;` next to it.
	start, end := s.span.Start, s.span.End()
	probe := end
	for probe < len(src) && src[probe] == ' ' {
		probe++
	}
	if probe < len(src) && src[probe] == ';' {
		end = probe + 1
	} else {
		probe = start - 1
		for probe > 0 && src[probe] == ' ' {
			probe--
		}
		if probe >= 0 && src[probe] == ';' {
			start = probe
		}
	}
	return start, end
}

// removeRanges removes each [start, end) range, the last first, skipping a
// range that overlaps one already removed.
func removeRanges(src []rune, ranges [][2]int) []rune {
	sorted := slices.Clone(ranges)
	sort.SliceStable(sorted, func(a, b int) bool { return sorted[a][0] > sorted[b][0] })
	out := slices.Clone(src)
	lastStart := len(out) + 1
	for _, r := range sorted {
		if r[1] > lastStart {
			continue // overlaps a range already removed
		}
		out = removeRunes(out, r[0], r[1]-r[0])
		lastStart = r[0]
	}
	return out
}

// indentAt is the spaces and tabs that start the line holding offset.
func indentAt(src []rune, offset int) string {
	lineStart := offset
	for lineStart > 0 && src[lineStart-1] != '\n' {
		lineStart--
	}
	i := lineStart
	for i < len(src) && (src[i] == ' ' || src[i] == '\t') {
		i++
	}
	return string(src[lineStart:i])
}

// insertStatementAfter puts statementText on a new line after the statement
// at stmtIndex, indented as it is. The pos line is a comment, never a
// statement, so an insertion never lands after it unless a statement does.
func (ctx *editCtx) insertStatementAfter(stmtIndex int, statementText string) []rune {
	s := ctx.stmts[stmtIndex]
	lineEnd := s.span.End()
	for lineEnd < len(ctx.source) && ctx.source[lineEnd] != '\n' {
		lineEnd++
	}
	return insertRunes(ctx.source, lineEnd, "\n"+indentAt(ctx.source, s.span.Start)+statementText)
}

// shapeDelimiters are the brackets of each shape (the vertex forms of
// flow.jison).
func shapeDelimiters(shape NodeShape) (string, string) {
	switch shape {
	case ShapeRoundRect:
		return "(", ")"
	case ShapeStadium:
		return "([", "])"
	case ShapeSubroutine:
		return "[[", "]]"
	case ShapeCylinder:
		return "[(", ")]"
	case ShapeCircle:
		return "((", "))"
	case ShapeDoubleCircle:
		return "(((", ")))"
	case ShapeEllipse:
		return "(-", "-)"
	case ShapeRhombus:
		return "{", "}"
	case ShapeHexagon:
		return "{{", "}}"
	case ShapeParallelogram:
		return "[/", "/]"
	case ShapeParallelogramAlt:
		return `[\`, `\]`
	case ShapeTrapezoid:
		return "[/", `\]`
	case ShapeTrapezoidAlt:
		return `[\`, "/]"
	case ShapeOdd:
		return ">", "]"
	}
	return "[", "]"
}

// renderLabel is a label as written between brackets: quoted only when the
// text needs it. A double quote cannot be written at all, because the STR
// token of flow.jison cannot hold one, and refuse is then true.
func renderLabel(text string) (rendered string, refuse bool) {
	if strings.Contains(text, `"`) {
		return "", true
	}
	if text == "" || text != trimSpace(text) || strings.ContainsAny(text, "[](){}<>|;&`") {
		return `"` + text + `"`, false
	}
	return text, false
}

// edgeOpText is an edge's arrow written with another stroke, keeping its
// arrowheads, its extra length and any inline label.
func edgeOpText(e *Edge, stroke EdgeStroke) string {
	runChar := "-"
	if stroke == StrokeThick {
		runChar = "="
	}
	extra := max(0, e.MinLen-1)
	var op string
	if stroke == StrokeDotted {
		// `-.->`, with more dots for more length; `-.-` without a head.
		op = "-" + strings.Repeat(".", 1+extra) + "-"
	} else {
		op = strings.Repeat(runChar, 2+extra)
	}
	if e.Label != "" && !e.PipeSpan.Valid() {
		// An inline label: `-- text -->`, `-. text .-` or `== text ==>`.
		arrow := ""
		if e.ArrowEnd {
			arrow = ">"
		}
		head := strings.Repeat(runChar, 2) + arrow
		tail := strings.Repeat(runChar, 2)
		if stroke == StrokeDotted {
			head = ".-" + arrow
			tail = "-."
		}
		if e.ArrowStart {
			tail = "<" + tail
		}
		return tail + " " + e.Label + " " + head
	}
	if e.ArrowEnd {
		op += ">"
	}
	if e.ArrowStart {
		op = "<" + op
	}
	return op
}

// bracketBalance is the number of brackets opened in s and not closed.
func bracketBalance(s []rune) int {
	open := 0
	for _, c := range s {
		switch c {
		case '[', '(', '{':
			open++
		case ']', ')', '}':
			open--
		}
	}
	return open
}

// refTextInStatement is the text of a mention of n in statement s: the id
// with the brackets or `:::class` written straight after it, reading no
// further than stopAt when it is above 0. It is the bare id when the mention
// is in an `&` group.
func (ctx *editCtx) refTextInStatement(n *Node, s editStmt, stopAt int) string {
	src := ctx.source
	for _, ref := range n.RefSpans {
		if ref.Start < s.span.Start || ref.End() > s.span.End() {
			continue
		}
		limit := s.span.End()
		if stopAt > 0 {
			limit = min(stopAt, s.span.End())
		}
		// Take what is attached to the id, up to white space.
		end := ref.End()
		for end < limit && !unicode.IsSpace(src[end]) {
			end++
		}
		segment := midRunes(src, ref.Start, end-ref.Start)
		if slices.Contains(segment, '&') {
			return n.ID
		}
		// A label with spaces leaves a bracket open: go on to its closer.
		text := segment
		extendedEnd := end
		for bracketBalance(text) > 0 && extendedEnd < limit {
			extendedEnd++
			text = midRunes(src, ref.Start, extendedEnd-ref.Start)
		}
		if bracketBalance(text) != 0 || slices.Contains(text, '&') {
			return n.ID
		}
		return string(text)
	}
	return n.ID
}

// qRound is the nearest int to d, halves away from zero.
func qRound(d float64) int {
	if d >= 0 {
		return int(d + 0.5)
	}
	return int(d - 0.5)
}

// posLineFor is the pos line for positions, in the order given (the source
// order of the nodes), with each centre rounded to whole pixels and no size,
// in the `id=x,y` form obsidian-mermaid-flow writes. An id that form cannot
// hold is left out. An id with `%` followed by a digit in it (a flowchart id
// may hold one, as in `n%2`) is written as it is.
func posLineFor(positions []NodePosition) string {
	var b strings.Builder
	b.WriteString("%% " + posPrefix)
	for _, p := range positions {
		if p.ID == "" || strings.ContainsAny(p.ID, "= \t") {
			continue
		}
		fmt.Fprintf(&b, " %s=%d,%d", p.ID, qRound(p.X), qRound(p.Y))
	}
	return b.String()
}

// WriteArrangement writes the positions of every node on the diagram into
// the pos line, which pins them where the reader dragged them. positions
// holds every node's centre, in source order, so an entry for a node that
// was deleted or renamed disappears. Only the pos line changes: it is
// replaced where it is, or added as the body's last line. The same positions
// always give the same line.
func WriteArrangement(source string, positions []NodePosition) EditResult {
	pr := Parse(source)
	if pr.Type != Flowchart || !pr.Supported {
		return editFail("Manual arrangement applies to flowcharts only")
	}
	if len(positions) == 0 {
		return editFail("No node positions to write")
	}
	line := posLineFor(positions)
	switch {
	case pr.Flowchart.HasPosLine && pr.Flowchart.PosLine.Valid():
		// Only the pos line is replaced; every other character stays.
		s := pr.Flowchart.PosLine
		return editOK(string(replaceRunes([]rune(source), s.Start, s.Length, line)))
	case strings.HasSuffix(source, "\n"):
		return editOK(source + line + "\n")
	case source == "":
		return editOK(line)
	}
	return editOK(source + "\n" + line)
}

// ResetArrangement removes the pos line, and its line break, in one edit, so
// the diagram is laid out automatically again. A source without a pos line
// comes back unchanged.
func ResetArrangement(source string) EditResult {
	pr := Parse(source)
	if !pr.Flowchart.HasPosLine || !pr.Flowchart.PosLine.Valid() {
		return editOK(source)
	}
	src := []rune(source)
	span := pr.Flowchart.PosLine
	start, end := span.Start, span.End()
	if end < len(src) && src[end] == '\n' {
		end++ // the line's own line break
	} else if start > 0 && src[start-1] == '\n' {
		start-- // the last line, with no line break: the one before it goes
	}
	return editOK(string(removeRunes(src, start, end-start)))
}

// ---- flowchart gestures ----

// SetNodeLabel replaces the text between the node's brackets, quoted only
// when the new text needs it. A node without brackets gets `[label]` after
// the id that declares it.
func SetNodeLabel(source, nodeID, newLabel string) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	n := ctx.findNode(nodeID)
	if n == nil {
		return editFail("Unknown node: " + nodeID)
	}
	rendered, refuse := renderLabel(newLabel)
	if refuse {
		return editFail("Labels cannot contain a double quote")
	}
	var out []rune
	switch {
	case n.LabelInShapeData && n.LabelSpan.Valid():
		// The label is a quoted value in an `@{ … }` block. It is known to
		// hold no double quote.
		out = replaceRunes(ctx.source, n.LabelSpan.Start, n.LabelSpan.Length, `"`+newLabel+`"`)
	case n.LabelSpan.Valid():
		out = replaceRunes(ctx.source, n.LabelSpan.Start, n.LabelSpan.Length, rendered)
	case n.HasShapeData:
		// A block with no `label:` entry. `[…]` after the id would be the
		// label here and be overridden by the block in mermaid.js, so the
		// two would disagree about what the node says.
		return editFail("This node is declared with `@{ … }`; add a `label:` entry to it to rename the node")
	case n.IDSpan.Valid():
		out = insertRunes(ctx.source, n.IDSpan.End(), "["+rendered+"]")
	default:
		return editFail("The node has no editable declaration")
	}
	// The edit is done only if the new label comes out of the parser.
	// Reporting success while the source still says something else is worse
	// than refusing, because the user sees no reason to try again.
	return postCheckedExpecting(&ctx, string(out), func(after *ParseResult) bool {
		i := after.Flowchart.IndexOfNode(nodeID)
		return i >= 0 && after.Flowchart.Nodes[i].Label == newLabel
	}, "The label did not take effect; refused")
}

// SetNodeShape rewrites the brackets around the node's label as written.
func SetNodeShape(source, nodeID string, shape NodeShape) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	n := ctx.findNode(nodeID)
	if n == nil {
		return editFail("Unknown node: " + nodeID)
	}
	open, close := shapeDelimiters(shape)
	if n.HasShapeData {
		// The block's `shape:` entry decides the shape and would override
		// brackets written here, so no edit would read the same in this
		// parser and in mermaid.js.
		return editFail("This node's shape is set by its `@{ … }` block; edit the `shape:` entry there")
	}
	var out []rune
	switch {
	case n.ShapeSpan.Valid() && n.LabelSpan.Valid():
		rawLabel := spanOf(ctx.source, n.LabelSpan)
		out = replaceRunes(ctx.source, n.ShapeSpan.Start, n.ShapeSpan.Length, open+rawLabel+close)
	case n.IDSpan.Valid():
		if shape == ShapeRect {
			return editOK(source) // already the default: nothing to do
		}
		out = insertRunes(ctx.source, n.IDSpan.End(), open+n.Label+close)
	default:
		return editFail("The node has no editable declaration")
	}
	return postChecked(&ctx, string(out), "")
}

var (
	nodeIDPattern = regexp.MustCompile(`^[A-Za-z0-9_][A-Za-z0-9_-]*\n?$`)
	reservedIDs   = []string{"subgraph", "end", "direction", "classDef", "class", "style", "click", "linkStyle", "flowchart", "graph"}
)

// RenameNode replaces every mention of the node's id the parser recorded,
// and its entry in the pos line. Comments and quoted labels are left alone.
func RenameNode(source, oldID, newID string) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	n := ctx.findNode(oldID)
	if n == nil {
		return editFail("Unknown node: " + oldID)
	}
	// The pattern's `\n?` accepts a new id followed by one line break.
	if !nodeIDPattern.MatchString(newID) {
		return editFail("Node ids use letters, digits, `_`, and `-`")
	}
	if slices.Contains(reservedIDs, newID) {
		return editFail(fmt.Sprintf("`%s` is a reserved word", newID))
	}
	if ctx.findNode(newID) != nil {
		return editFail("A node named " + newID + " already exists")
	}
	// Every mention is replaced, the last first. The pos line is keyed by
	// node id, so its entry is renamed in the same edit: as a second edit it
	// could apply by half, and left alone the node would lose its pinned
	// position at the next layout.
	refs := slices.Clone(n.RefSpans)
	for _, pe := range ctx.pr.Flowchart.PosEntries {
		if pe.ID == oldID && pe.IDSpan.Valid() {
			refs = append(refs, pe.IDSpan)
		}
	}
	sort.SliceStable(refs, func(a, b int) bool { return refs[a].Start > refs[b].Start })
	out := ctx.source
	lastStart := len(out) + 1
	for _, ref := range refs {
		if ref.End() > lastStart {
			continue // the same span recorded twice
		}
		out = replaceRunes(out, ref.Start, ref.Length, newID)
		lastStart = ref.Start
	}
	return postChecked(&ctx, string(out), "")
}

// DeleteNode removes the statements that declare the node and every edge
// statement that mentions it, whole statements only.
func DeleteNode(source, nodeID string) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	n := ctx.findNode(nodeID)
	if n == nil {
		return editFail("Unknown node: " + nodeID)
	}
	var removals [][2]int
	for i, s := range ctx.stmts {
		if !stmtMentionsNode(s, n) {
			continue
		}
		switch s.firstWord {
		case "class", "style":
			// Refused, rather than applied approximately, when the
			// statement styles other nodes too.
			if ctx.mentionsOtherNode(s, n) {
				return editFail(nodeID + " is styled together with other nodes; edit the source instead")
			}
		case "subgraph":
			return editFail(nodeID + " names a subgraph; edit the source instead")
		case "classDef", "click", "linkStyle":
			return editFail(nodeID + " is referenced from retained syntax; edit the source instead")
		}
		// A declaration or a chain of edges: removed whole.
		start, end := ctx.removalRange(i)
		removals = append(removals, [2]int{start, end})
	}
	if len(removals) == 0 {
		return editFail("No statements declare " + nodeID)
	}
	return postChecked(&ctx, string(removeRanges(ctx.source, removals)), "")
}

// DeleteEdge removes the statement of edge edgeIndex. A chain is split so
// its other links stay.
func DeleteEdge(source string, edgeIndex int) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	edges := ctx.pr.Flowchart.Edges
	if edgeIndex < 0 || edgeIndex >= len(edges) {
		return editFail("Unknown edge")
	}
	victim := edges[edgeIndex]
	if !victim.StmtSpan.Valid() {
		return editFail("The edge has no locatable statement")
	}

	// The edges of the same statement.
	var mates []int
	for i, e := range edges {
		if e.StmtSpan == victim.StmtSpan {
			mates = append(mates, i)
		}
	}
	stmtIndex := -1
	for i, s := range ctx.stmts {
		if s.span.Start == victim.StmtSpan.Start {
			stmtIndex = i
		}
	}
	if stmtIndex < 0 {
		return editFail("The edge statement cannot be located")
	}

	if len(mates) == 1 {
		start, end := ctx.removalRange(stmtIndex)
		return postChecked(&ctx, string(removeRanges(ctx.source, [][2]int{{start, end}})), "")
	}

	// Split the chain so the links not deleted stay.
	s := ctx.stmts[stmtIndex]
	indent := indentAt(ctx.source, s.span.Start)
	var lines []string
	for _, i := range mates {
		if i == edgeIndex {
			continue
		}
		e := &edges[i]
		from, to := ctx.findNode(e.From), ctx.findNode(e.To)
		if from == nil || to == nil {
			return editFail("The chain cannot be split safely")
		}
		fromText := ctx.refTextInStatement(from, s, e.OpSpan.Start)
		// The target's text ends at the next arrow of the statement.
		stopAt := -1
		for _, j := range mates {
			if st := edges[j].OpSpan.Start; st > e.OpSpan.Start && (stopAt < 0 || st < stopAt) {
				stopAt = st
			}
		}
		toText := ctx.refTextInStatement(to, s, stopAt)
		op := spanOf(ctx.source, e.OpSpan)
		if e.PipeSpan.Valid() {
			op += spanOf(ctx.source, e.PipeSpan)
		}
		lines = append(lines, fromText+" "+trimSpace(op)+" "+toText)
	}
	replacement := strings.Join(lines, "\n"+indent)
	return postChecked(&ctx, string(replaceRunes(ctx.source, s.span.Start, s.span.Length, replacement)), "")
}

// SetEdgeStroke rewrites edge edgeIndex's arrow with another stroke, keeping
// an inline label.
func SetEdgeStroke(source string, edgeIndex int, stroke EdgeStroke) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	edges := ctx.pr.Flowchart.Edges
	if edgeIndex < 0 || edgeIndex >= len(edges) {
		return editFail("Unknown edge")
	}
	e := &edges[edgeIndex]
	if !e.OpSpan.Valid() {
		return editFail("The edge has no locatable arrow")
	}
	if e.Invisible {
		return editFail("Invisible links have no style")
	}
	// `A & B --> C` makes several edges from one arrow in the source.
	// Rewriting it would restyle every edge of the group, so the gesture is
	// refused rather than applied to links the user did not pick.
	sharing := 0
	for _, other := range edges {
		if other.OpSpan.Valid() && other.OpSpan == e.OpSpan {
			sharing++
		}
	}
	if sharing > 1 {
		return editFail(fmt.Sprintf("This link is written as a group (`A & B --> C`) and shares one arrow with %d others; write it on its own line to style it separately", sharing-1))
	}
	return postChecked(&ctx, string(replaceRunes(ctx.source, e.OpSpan.Start, e.OpSpan.Length, edgeOpText(e, stroke))), "")
}

// InsertEdge adds a `from --> to` statement after the last statement that
// mentions the from node, indented as it is, and so before the pos line.
func InsertEdge(source, fromID, toID string) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	from, to := ctx.findNode(fromID), ctx.findNode(toID)
	if from == nil || to == nil {
		return editFail("Both ends must be existing nodes")
	}
	anchor := ctx.lastMention(from)
	if anchor < 0 {
		anchor = len(ctx.stmts) - 1
	}
	if anchor < 0 {
		return editFail("Nowhere to insert the edge")
	}
	return postChecked(&ctx, string(ctx.insertStatementAfter(anchor, fromID+" --> "+toID)), "")
}

// QuickAddNode adds a new node linked from fromID, as one statement declaring
// it and its edge. The new node's id is one not in use, and is returned in
// NewID.
func QuickAddNode(source, fromID string) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	from := ctx.findNode(fromID)
	if from == nil {
		return editFail("Unknown node: " + fromID)
	}
	newID := ""
	for k := 1; k < MaxNodes; k++ {
		if id := fmt.Sprintf("node%d", k); ctx.findNode(id) == nil {
			newID = id
			break
		}
	}
	if newID == "" {
		// Every name is taken. Using the last one tried would write a second
		// node under an id that exists, which the parser reads back as one
		// node: the new box would become a relabelling of an old one, with
		// the old one's edges.
		return editFail("This diagram already has a node under every name a new one could be given")
	}
	anchor := ctx.lastMention(from)
	if anchor < 0 {
		return editFail("Nowhere to insert the node")
	}
	return postChecked(&ctx, string(ctx.insertStatementAfter(anchor, fromID+" --> "+newID+"[New node]")), newID)
}

// colorName is c as `#rrggbb` in lower case.
func colorName(c Color) string { return fmt.Sprintf("#%02x%02x%02x", c.R, c.G, c.B) }

var kvitClassStatement = regexp.MustCompile(`^class[\t\n\v\f\r ]+([^\t\n\v\f\r ]+)[\t\n\v\f\r ]+(kvit_style_[0-9]+)\n?$`)

// SetNodeStyle colours a node with a classDef named kvit_style_N and a class
// statement. A kvit_style classDef with the same colours is used again; no
// existing classDef is ever rewritten. A colour that is not set is left out.
func SetNodeStyle(source, nodeID string, fill, stroke Color) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	n := ctx.findNode(nodeID)
	if n == nil {
		return editFail("Unknown node: " + nodeID)
	}
	if !fill.Set && !stroke.Set {
		return editFail("No style to apply")
	}

	var decls []string
	if fill.Set {
		decls = append(decls, "fill:"+colorName(fill))
	}
	if stroke.Set {
		decls = append(decls, "stroke:"+colorName(stroke))
	}
	decl := strings.Join(decls, ",")

	// A kvit_style classDef with the same declarations is used again, the
	// first of them by name, so the choice does not change from run to run.
	defs := ctx.pr.Flowchart.ClassDefs
	names := make([]string, 0, len(defs))
	for name := range defs {
		names = append(names, name)
	}
	sort.Strings(names)
	className := ""
	for _, name := range names {
		if !strings.HasPrefix(name, "kvit_style_") {
			continue
		}
		d := defs[name]
		fillMatch := d.HasFill == fill.Set && (!fill.Set || d.Fill == fill)
		strokeMatch := d.HasStroke == stroke.Set && (!stroke.Set || d.Stroke == stroke)
		if fillMatch && strokeMatch {
			className = name
			break
		}
	}
	newDefLine := ""
	if className == "" {
		for k := 1; k < MaxNodes; k++ {
			className = fmt.Sprintf("kvit_style_%d", k)
			if _, taken := defs[className]; !taken {
				break
			}
		}
		newDefLine = "classDef " + className + " " + decl
	}

	// Update the node's own `class <node> kvit_style_N` statement where it
	// is, or add one after the node's last mention.
	out := ctx.source
	updated := false
	for i := len(ctx.stmts) - 1; i >= 0 && !updated; i-- {
		s := ctx.stmts[i]
		if s.firstWord != "class" {
			continue
		}
		m := kvitClassStatement.FindStringSubmatch(spanOf(ctx.source, s.span))
		if m == nil || m[1] != nodeID {
			continue
		}
		out = replaceRunes(out, s.span.Start, s.span.Length, "class "+nodeID+" "+className)
		updated = true
	}
	if !updated {
		anchor := ctx.lastMention(n)
		if anchor < 0 {
			return editFail("Nowhere to apply the style")
		}
		out = ctx.insertStatementAfter(anchor, "class "+nodeID+" "+className)
	}
	if newDefLine != "" {
		// The classDef goes at the end of the body, before a pos line.
		defCtx := makeEditCtx(string(out))
		if !defCtx.ok {
			return editFail(defCtx.err)
		}
		switch pl := defCtx.pr.Flowchart.PosLine; {
		case defCtx.pr.Flowchart.HasPosLine && pl.Valid():
			out = insertRunes(out, pl.Start, newDefLine+"\n")
		case len(out) > 0 && out[len(out)-1] == '\n':
			out = insertRunes(out, len(out), newDefLine+"\n")
		default:
			out = insertRunes(out, len(out), "\n"+newDefLine)
		}
	}
	return postChecked(&ctx, string(out), "")
}

// ReparentNode moves the statement that declares the node on its own into
// the subgraph subgraphID, or out to the top level when subgraphID is empty.
// It is refused when the node is in a subgraph through edge statements
// there, which moving one declaration cannot change.
func ReparentNode(source, nodeID, subgraphID string) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	n := ctx.findNode(nodeID)
	if n == nil {
		return editFail("Unknown node: " + nodeID)
	}

	// The node's own declaration: a statement that mentions no other node.
	// Also count the statements mentioning it at all, because edge chains
	// keep it in a subgraph.
	declIndex := -1
	mentions := 0
	for i, s := range ctx.stmts {
		switch s.firstWord {
		case "subgraph", "end", "class", "classDef", "style", "direction":
			continue
		}
		if !stmtMentionsNode(s, n) {
			continue
		}
		mentions++
		if !ctx.mentionsOtherNode(s, n) && declIndex < 0 {
			declIndex = i
		}
	}
	// Membership that edge statements inside a subgraph give cannot be
	// moved by moving a declaration: refused rather than applied
	// approximately.
	memberOfSubgraph := false
	for _, sg := range ctx.pr.Flowchart.Subgraphs {
		if slices.Contains(sg.NodeIDs, nodeID) && sg.ID != subgraphID {
			memberOfSubgraph = true
		}
	}
	if memberOfSubgraph && (declIndex < 0 || mentions > 1) {
		return editFail(nodeID + " is placed by edge statements inside a subgraph; edit the source instead")
	}

	var declText string
	if declIndex >= 0 {
		declText = spanOf(ctx.source, ctx.stmts[declIndex].span)
	} else {
		declText = ctx.refTextInStatement(n, editStmt{span: Span{Start: 0, Length: len(ctx.source)}}, -1)
	}

	if subgraphID == "" {
		// Out to the top level: at the end of the body.
		var removals [][2]int
		if declIndex >= 0 {
			start, end := ctx.removalRange(declIndex)
			removals = append(removals, [2]int{start, end})
		}
		out := string(removeRanges(ctx.source, removals))
		if strings.HasSuffix(out, "\n") {
			out += declText + "\n"
		} else {
			out += "\n" + declText
		}
		return postChecked(&ctx, out, "")
	}

	// Into the subgraph: before its own `end`.
	opener := regexp.MustCompile(`^subgraph[\t\n\v\f\r ]+` + regexp.QuoteMeta(subgraphID) + `([\t\n\v\f\r ]|\[|\n?$)`)
	depth, openIndex, endIndex := 0, -1, -1
	for i, s := range ctx.stmts {
		if s.firstWord == "subgraph" {
			if openIndex < 0 && opener.MatchString(spanOf(ctx.source, s.span)) {
				openIndex = i
				depth = 0
			} else if openIndex >= 0 {
				depth++
			}
		} else if s.firstWord == "end" && openIndex >= 0 {
			if depth == 0 {
				endIndex = i
				break
			}
			depth--
		}
	}
	if openIndex < 0 || endIndex < 0 {
		return editFail("Unknown subgraph: " + subgraphID)
	}
	lineStart := ctx.stmts[endIndex].span.Start
	for lineStart > 0 && ctx.source[lineStart-1] != '\n' {
		lineStart--
	}
	insertion := indentAt(ctx.source, ctx.stmts[openIndex].span.Start) + "  " + declText + "\n"
	out := insertRunes(ctx.source, lineStart, insertion)
	// Remove the old declaration, moved along by the insertion when after it.
	if declIndex >= 0 {
		start, end := ctx.removalRange(declIndex)
		shift := 0
		if start >= lineStart {
			shift = runeLen(insertion)
		}
		out = removeRunes(out, start+shift, end-start)
	}
	return postChecked(&ctx, string(out), "")
}

// ReorderNode swaps the statement that declares the node on its own with the
// statement before it (delta below 0) or after it. It works in automatic
// layout only.
func ReorderNode(source, nodeID string, delta int) EditResult {
	ctx := makeEditCtx(source)
	if !ctx.ok {
		return editFail(ctx.err)
	}
	if ctx.pr.Flowchart.HasPosLine {
		return editFail("Reordering applies in auto layout only")
	}
	n := ctx.findNode(nodeID)
	if n == nil {
		return editFail("Unknown node: " + nodeID)
	}

	// The statement that first mentions the node must declare it alone.
	declIndex := -1
	for i, s := range ctx.stmts {
		if !stmtMentionsNode(s, n) {
			continue
		}
		if ctx.mentionsOtherNode(s, n) {
			return editFail(nodeID + " is first declared in a chain; edit the source instead")
		}
		declIndex = i
		break
	}
	if declIndex < 0 {
		return editFail("No standalone declaration for " + nodeID)
	}
	other := declIndex + 1
	if delta < 0 {
		other = declIndex - 1
	}
	if other <= 0 || other >= len(ctx.stmts) {
		return editFail("Nothing to swap with")
	}
	a := ctx.stmts[min(declIndex, other)]
	b := ctx.stmts[max(declIndex, other)]
	textA, textB := spanOf(ctx.source, a.span), spanOf(ctx.source, b.span)
	out := replaceRunes(ctx.source, b.span.Start, b.span.Length, textA)
	out = replaceRunes(out, a.span.Start, a.span.Length, textB)
	return postChecked(&ctx, string(out), "")
}

// ---- reordering a sequence diagram ----

// swapSpans swaps the text of two spans that do not overlap, replacing the
// later first so the earlier's offsets stay right.
func swapSpans(src []rune, a, b Span) []rune {
	first, second := a, b
	if b.Start < a.Start {
		first, second = b, a
	}
	textFirst, textSecond := spanOf(src, first), spanOf(src, second)
	out := replaceRunes(src, second.Start, second.Length, textFirst)
	return replaceRunes(out, first.Start, first.Length, textSecond)
}

func seqPostChecked(oldSource, newSource string) EditResult {
	before := Parse(oldSource)
	after := Parse(newSource)
	if after.Type != Sequence || errorCount(&after) > errorCount(&before) {
		return editFail("The edit would leave the source invalid; refused")
	}
	return editOK(newSource)
}

// MoveSequenceMessage moves a message up (delta below 0) or down. Time runs
// down the statements, so the message's statement swaps places with the
// next message's, one for one. eventIndex indexes SequenceAst.Events and must
// be a message.
func MoveSequenceMessage(source string, eventIndex, delta int) EditResult {
	pr := Parse(source)
	if pr.Type != Sequence || !pr.Supported {
		return editFail("Message reordering applies to sequence diagrams only")
	}
	events := pr.Sequence.Events
	if eventIndex < 0 || eventIndex >= len(events) || events[eventIndex].Kind != EventMessage {
		return editFail("No message selected")
	}
	moving := events[eventIndex]
	if !moving.SrcSpan.Valid() {
		return editFail("The message cannot be located")
	}
	step := 1
	if delta < 0 {
		step = -1
	}
	// The next message in that direction.
	other := -1
	for i := eventIndex + step; i >= 0 && i < len(events); i += step {
		if events[i].Kind == EventMessage {
			other = i
			break
		}
	}
	if other < 0 {
		where := "bottom"
		if delta < 0 {
			where = "top"
		}
		return editFail("The message is already at the " + where)
	}
	neighbour := events[other]
	if !neighbour.SrcSpan.Valid() || neighbour.SrcSpan.Start == moving.SrcSpan.Start {
		return editFail("The messages share a statement; edit the source instead")
	}
	return seqPostChecked(source, string(swapSpans([]rune(source), moving.SrcSpan, neighbour.SrcSpan)))
}

// MoveSequenceParticipant moves a participant left (delta below 0) or right.
// Participants stand in the order they are declared, so its `participant` or
// `actor` statement swaps places with its neighbour's. A participant that
// is only used, never declared, has no statement to move and is refused.
func MoveSequenceParticipant(source, id string, delta int) EditResult {
	pr := Parse(source)
	if pr.Type != Sequence || !pr.Supported {
		return editFail("Participant reordering applies to sequence diagrams only")
	}
	parts := pr.Sequence.Participants
	i := pr.Sequence.IndexOfParticipant(id)
	if i < 0 {
		return editFail("Unknown participant: " + id)
	}
	j := i + 1
	if delta < 0 {
		j = i - 1
	}
	if j < 0 || j >= len(parts) {
		return editFail("The participant is already at the edge")
	}
	src := []rune(source)
	isDeclaration := func(p SeqParticipant) bool {
		if !p.SrcSpan.Valid() {
			return false
		}
		text := spanOf(src, p.SrcSpan)
		return hasPrefixFold(text, "participant") || hasPrefixFold(text, "actor")
	}
	if !isDeclaration(parts[i]) || !isDeclaration(parts[j]) {
		return editFail("Both participants need explicit `participant` declarations to reorder")
	}
	return seqPostChecked(source, string(swapSpans(src, parts[i].SrcSpan, parts[j].SrcSpan)))
}
