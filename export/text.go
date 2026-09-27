package export

// Plain-text export, as src/application/documentexporter.cpp
// (DocumentExporter::buildPlainText) and each Qt block kind's toPlainText
// write it: the text a reader sees, with a structural prefix where the
// structure would otherwise be lost ("# " for a heading, "- " or "1. " for a
// list item, "> " for a quote), each block followed by a blank line.

import (
	"strconv"
	"strings"
)

// maxIndent is Block::MaxIndentLevel, the deepest list nesting.
const maxIndent = 4

// plainText writes blocks as text. doc is the whole document, which the
// table of contents reads.
func (r *renderer) plainText(blocks []block) string {
	var lines []string
	// One numbering counter per indent level: a block at level L resets the
	// deeper levels, a block at L that is not numbered resets L, and anything
	// outside the list family resets them all.
	var counters [maxIndent + 1]int
	for _, b := range blocks {
		ordinal := 1
		if !b.kind.isList() {
			counters = [maxIndent + 1]int{}
		} else {
			level := min(max(b.indent, 0), maxIndent)
			for deeper := level + 1; deeper <= maxIndent; deeper++ {
				counters[deeper] = 0
			}
			if b.kind == kNumbered {
				counters[level]++
				ordinal = counters[level]
			} else {
				counters[level] = 0
			}
		}
		if text := r.blockText(b, ordinal); text != "" {
			lines = append(lines, text)
		}
		lines = append(lines, "")
	}
	return strings.Join(lines, "\n")
}

// indentLines is BlockText::indent: every non-blank line indented.
func indentLines(text string, spaces int) string {
	pad := strings.Repeat(" ", spaces)
	lines := strings.Split(text, "\n")
	for i, l := range lines {
		if l != "" {
			lines[i] = pad + l
		}
	}
	return strings.Join(lines, "\n")
}

func listPad(indent int) string { return strings.Repeat("  ", max(0, indent)) }

func (r *renderer) blockText(b block, ordinal int) string {
	switch b.kind {
	case kParagraph:
		return displayText(b.text)
	case kHeading:
		return strings.Repeat("#", b.level) + " " + displayText(b.text)
	case kBullet:
		return listPad(b.indent) + "- " + displayText(b.text)
	case kNumbered:
		return listPad(b.indent) + strconv.Itoa(ordinal) + ". " + displayText(b.text)
	case kTodo:
		box := "[ ] "
		if b.checked {
			box = "[x] "
		}
		return listPad(b.indent) + box + displayText(b.text)
	case kQuote:
		lines := strings.Split(displayText(b.text), "\n")
		for i, l := range lines {
			lines[i] = "> " + l
		}
		return strings.Join(lines, "\n")
	case kCallout:
		head := "[" + strings.ToUpper(b.lang) + "]"
		if b.title != "" {
			head += " " + displayText(b.title)
		}
		if b.text == "" {
			return head
		}
		return head + "\n" + indentLines(displayText(b.text), 2)
	case kCode, kMath, kQuery:
		// Code and TeX as written. A query is answered against the open
		// vault, which this package does not have, so its spec is written, as
		// the Qt exporter writes it when no vault is open.
		return b.text
	case kMermaid:
		return "[mermaid diagram]\n" + b.text
	case kToc:
		return tocText(r.doc)
	case kKanban:
		return boardText(parseBoard(b.text))
	case kDivider:
		return "---"
	case kTable:
		t := parseTable(b.text)
		headers := make([]string, len(t.headers))
		for i, h := range t.headers {
			headers[i] = displayText(h)
		}
		rows := make([][]string, len(t.rows))
		for i, row := range t.rows {
			rows[i] = make([]string, len(row))
			for j, c := range row {
				rows[i][j] = displayText(c)
			}
		}
		return alignedTable(headers, rows)
	case kImage, kMedia:
		p := parseImageLine(b.text)
		if b.kind == kImage && p.valid && isEmbedURL(p.path) {
			return r.embedText(p)
		}
		if !p.valid {
			return displayText(b.text)
		}
		label := "image"
		if b.kind == kMedia {
			label = "media"
		}
		out := "[" + label
		if p.alt != "" {
			out += ": " + p.alt
		}
		out += "] " + p.path
		if p.caption != "" {
			out += "\n" + indentLines(displayText(p.caption), 2)
		}
		return out
	}
	return ""
}

func (r *renderer) embedText(p imageExpr) string {
	label := p.alt
	if label == "" && r.opt.EmbedPreview != nil {
		label, _ = r.opt.EmbedPreview(p.path)
	}
	out := "[embed"
	if label != "" {
		out += ": " + label
	}
	out += "] " + p.path
	if p.caption != "" {
		out += "\n" + indentLines(displayText(p.caption), 2)
	}
	return out
}

// tocText is the table of contents as text: each heading on a line,
// indented two spaces a level below the shallowest one present.
func tocText(doc []block) string {
	minLevel := 4
	for _, b := range doc {
		if l := b.headingLevel(); l > 0 {
			minLevel = min(minLevel, l)
		}
	}
	var out []string
	for _, b := range doc {
		if l := b.headingLevel(); l > 0 {
			out = append(out, strings.Repeat(" ", 2*(l-minLevel))+displayText(b.text))
		}
	}
	return strings.Join(out, "\n")
}

func boardText(b board) string {
	var out []string
	for _, l := range b.preamble {
		if trimSpace(l) != "" {
			out = append(out, l)
		}
	}
	for _, col := range b.columns {
		if len(out) > 0 {
			out = append(out, "")
		}
		out = append(out, col.name+" ("+strconv.Itoa(len(col.cards))+")")
		for _, c := range col.cards {
			line := "  [ ] "
			if c.done {
				line = "  [x] "
			}
			line += displayText(c.title)
			for _, l := range c.labels {
				line += "  #" + l
			}
			if c.due != "" {
				line += "  (due " + c.due + ")"
			}
			out = append(out, line)
			if c.description != "" {
				out = append(out, indentLines(displayText(c.description), 6))
			}
		}
	}
	return strings.Join(out, "\n")
}

// alignedTable is BlockText::alignedTable: the header row, a rule under it,
// then the rows, each column as wide as its widest cell (counted in UTF-16
// units, as Qt counts them).
func alignedTable(headers []string, rows [][]string) string {
	columns := len(headers)
	for _, row := range rows {
		columns = max(columns, len(row))
	}
	if columns == 0 {
		return ""
	}
	cellAt := func(row []string, c int) string {
		if c < len(row) {
			return simplified(row[c])
		}
		return ""
	}
	widths := make([]int, columns)
	for c := range columns {
		widths[c] = utf16Len(cellAt(headers, c))
		for _, row := range rows {
			widths[c] = max(widths[c], utf16Len(cellAt(row, c)))
		}
	}
	renderRow := func(row []string) string {
		cells := make([]string, columns)
		for c := range columns {
			s := cellAt(row, c)
			cells[c] = s + strings.Repeat(" ", max(0, widths[c]-utf16Len(s)))
		}
		return strings.TrimRight(strings.Join(cells, " | "), " ")
	}
	var out []string
	if len(headers) > 0 {
		out = append(out, renderRow(headers))
		rule := make([]string, columns)
		for c := range columns {
			rule[c] = strings.Repeat("-", widths[c])
		}
		out = append(out, strings.Join(rule, "-+-"))
	}
	for _, row := range rows {
		out = append(out, renderRow(row))
	}
	return strings.Join(out, "\n")
}
