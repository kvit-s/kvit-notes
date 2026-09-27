package editor

// The document model: a flat list of blocks, each with a kind, an indent
// level and its Markdown source. Nesting is the indent level, as in Kvit
// (block-arch.md), not a tree.

import (
	"regexp"
	"strconv"
	"strings"
	"sync/atomic"
)

// Kind is what a block is: a paragraph, a heading, a list item and so on.
type Kind int

// The block kinds.
const (
	Paragraph Kind = iota
	Heading1
	Heading2
	Heading3
	Heading4
	Bullet
	Numbered
	Todo
	Quote
	Code
	Divider
	// Raw keeps Markdown the editor does not model (tables, display math,
	// front matter) verbatim, so a note survives a load and save unchanged.
	Raw
)

var kindNames = [...]string{
	Paragraph: "Paragraph",
	Heading1:  "Heading 1",
	Heading2:  "Heading 2",
	Heading3:  "Heading 3",
	Heading4:  "Heading 4",
	Bullet:    "Bulleted list",
	Numbered:  "Numbered list",
	Todo:      "To-do",
	Quote:     "Quote",
	Code:      "Code",
	Divider:   "Divider",
	Raw:       "Unsupported Markdown",
}

func (k Kind) String() string { return kindNames[k] }

// IsList reports whether a block of this kind takes the list rules: Enter
// continues it, Enter on an empty item leaves it, Backspace at the start
// outdents and then turns it into a paragraph.
func (k Kind) IsList() bool { return k == Bullet || k == Numbered || k == Todo }

// HasInline reports whether the block's text is parsed as inline Markdown.
func (k Kind) HasInline() bool { return k != Code && k != Raw && k != Divider }

// IsText reports whether the block is edited as text at all.
func (k Kind) IsText() bool { return k != Divider }

// IsHeading reports whether the block is one of the four headings.
func (k Kind) IsHeading() bool { return k >= Heading1 && k <= Heading4 }

// MaxIndent is the deepest a list item nests.
const MaxIndent = 4

// Block is one block of a note.
type Block struct {
	ID      int64
	Kind    Kind
	Indent  int
	Text    string
	Checked bool
	Lang    string
}

var lastBlockID atomic.Int64

func newID() int64 { return lastBlockID.Add(1) }

// NewBlock is a block of a kind holding text, with an identity of its own.
func NewBlock(kind Kind, text string) Block {
	return Block{ID: newID(), Kind: kind, Text: text}
}

var (
	reHeading  = regexp.MustCompile(`^(#{1,4}) (.*)$`)
	reTodo     = regexp.MustCompile(`^([ \t]*)[-*+] \[( |x|X)\] ?(.*)$`)
	reBullet   = regexp.MustCompile(`^([ \t]*)[-*+] (.*)$`)
	reNumbered = regexp.MustCompile(`^([ \t]*)\d+[.)] (.*)$`)
	reQuote    = regexp.MustCompile(`^> ?(.*)$`)
	reDivider  = regexp.MustCompile(`^(\*{3,}|-{3,}|_{3,})\s*$`)
	reFence    = regexp.MustCompile("^(```+|~~~+)\\s*(\\S*)")
)

func indentOf(ws string) int {
	n := 0
	for _, r := range ws {
		if r == '\t' {
			n += 4
		} else {
			n++
		}
	}
	return min(n/2, MaxIndent)
}

func startsBlock(line string) bool {
	return reHeading.MatchString(line) || reBullet.MatchString(line) || reNumbered.MatchString(line) ||
		reQuote.MatchString(line) || reDivider.MatchString(line) || reFence.MatchString(line) ||
		strings.HasPrefix(line, "|") || strings.HasPrefix(line, "$$")
}

// ParseMarkdown reads a note into blocks. It covers the block syntax the
// editor edits; anything else becomes a Raw block holding its lines.
func ParseMarkdown(src string) []Block {
	src = strings.ReplaceAll(src, "\r\n", "\n")
	lines := strings.Split(src, "\n")
	var out []Block
	i := 0
	// Front matter.
	if len(lines) > 0 && lines[0] == "---" {
		for j := 1; j < len(lines); j++ {
			if lines[j] == "---" {
				out = append(out, NewBlock(Raw, strings.Join(lines[:j+1], "\n")))
				i = j + 1
				break
			}
		}
	}
	for i < len(lines) {
		line := lines[i]
		if strings.TrimSpace(line) == "" {
			i++
			continue
		}
		if m := reFence.FindStringSubmatch(line); m != nil {
			fence := m[1]
			j := i + 1
			for j < len(lines) && !strings.HasPrefix(strings.TrimSpace(lines[j]), fence) {
				j++
			}
			b := NewBlock(Code, strings.Join(lines[i+1:min(j, len(lines))], "\n"))
			b.Lang = m[2]
			out = append(out, b)
			i = j + 1
			continue
		}
		if strings.HasPrefix(line, "$$") {
			j := i + 1
			if strings.TrimSpace(line) == "$$" {
				for j < len(lines) && strings.TrimSpace(lines[j]) != "$$" {
					j++
				}
				j++
			}
			j = min(j, len(lines))
			out = append(out, NewBlock(Raw, strings.Join(lines[i:j], "\n")))
			i = j
			continue
		}
		if strings.HasPrefix(line, "|") {
			j := i
			for j < len(lines) && strings.HasPrefix(lines[j], "|") {
				j++
			}
			out = append(out, NewBlock(Raw, strings.Join(lines[i:j], "\n")))
			i = j
			continue
		}
		if m := reHeading.FindStringSubmatch(line); m != nil {
			out = append(out, NewBlock(Heading1+Kind(len(m[1])-1), m[2]))
			i++
			continue
		}
		if reDivider.MatchString(line) {
			out = append(out, NewBlock(Divider, ""))
			i++
			continue
		}
		if m := reTodo.FindStringSubmatch(line); m != nil {
			b := NewBlock(Todo, m[3])
			b.Indent = indentOf(m[1])
			b.Checked = m[2] != " "
			out = append(out, b)
			i++
			continue
		}
		if m := reBullet.FindStringSubmatch(line); m != nil {
			b := NewBlock(Bullet, m[2])
			b.Indent = indentOf(m[1])
			out = append(out, b)
			i++
			continue
		}
		if m := reNumbered.FindStringSubmatch(line); m != nil {
			b := NewBlock(Numbered, m[2])
			b.Indent = indentOf(m[1])
			out = append(out, b)
			i++
			continue
		}
		if reQuote.MatchString(line) {
			var body []string
			for i < len(lines) {
				m := reQuote.FindStringSubmatch(lines[i])
				if m == nil {
					break
				}
				body = append(body, m[1])
				i++
			}
			out = append(out, NewBlock(Quote, strings.Join(body, "\n")))
			continue
		}
		var body []string
		for i < len(lines) && strings.TrimSpace(lines[i]) != "" && (len(body) == 0 || !startsBlock(lines[i])) {
			body = append(body, lines[i])
			i++
		}
		out = append(out, NewBlock(Paragraph, strings.Join(body, "\n")))
	}
	return out
}

// ListNumber is the number a numbered block shows: one more than the
// numbered siblings directly above it at the same depth. Deeper items in
// between do not break the run, so a nested list restarts at 1.
func ListNumber(blocks []Block, i int) int {
	n := 1
	for j := i - 1; j >= 0; j-- {
		b := blocks[j]
		if b.Kind.IsList() && b.Indent > blocks[i].Indent {
			continue
		}
		if b.Kind == Numbered && b.Indent == blocks[i].Indent {
			n++
			continue
		}
		break
	}
	return n
}

// BlockMarkdown writes one block as Markdown. number is the list number for
// a numbered block.
func BlockMarkdown(b Block, number int) string {
	pad := strings.Repeat("  ", b.Indent)
	prefixLines := func(first, rest string) string {
		ls := strings.Split(b.Text, "\n")
		for i := range ls {
			if i == 0 {
				ls[i] = first + ls[i]
			} else {
				ls[i] = rest + ls[i]
			}
		}
		return strings.Join(ls, "\n")
	}
	switch b.Kind {
	case Heading1, Heading2, Heading3, Heading4:
		return strings.Repeat("#", int(b.Kind-Heading1)+1) + " " + b.Text
	case Bullet:
		return prefixLines(pad+"- ", pad+"  ")
	case Numbered:
		p := pad + strconv.Itoa(number) + ". "
		return prefixLines(p, strings.Repeat(" ", len(p)))
	case Todo:
		mark := "[ ] "
		if b.Checked {
			mark = "[x] "
		}
		return prefixLines(pad+"- "+mark, pad+"      ")
	case Quote:
		return prefixLines("> ", "> ")
	case Code:
		return "```" + b.Lang + "\n" + b.Text + "\n```"
	case Divider:
		return "---"
	}
	return b.Text
}

// Serialize writes the whole note. Blocks are separated by a blank line,
// except consecutive list items, which form one Markdown list.
func Serialize(blocks []Block) string {
	var sb strings.Builder
	for i, b := range blocks {
		if i > 0 {
			if b.Kind.IsList() && blocks[i-1].Kind.IsList() {
				sb.WriteString("\n")
			} else {
				sb.WriteString("\n\n")
			}
		}
		sb.WriteString(BlockMarkdown(b, ListNumber(blocks, i)))
	}
	sb.WriteString("\n")
	return sb.String()
}
