package editor

// The document model: a flat list of blocks, each with a kind, an indent
// level and its Markdown source. Nesting is the indent level, not a tree.

import (
	"regexp"
	"slices"
	"strconv"
	"strings"
	"sync/atomic"

	"github.com/kvit-s/kvit-notes/textdiagram"
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
	// Image is a line holding only ![alt](picture), drawn as the picture.
	Image
	// Media is such a line naming a sound or a video.
	Media
	// Callout is a quote headed "[!type] Title": a tinted panel.
	Callout
	// Table is a pipe table, kept as written and drawn as a grid.
	Table
	// Math is a display equation, a "$$ … $$" fence: its text is the TeX
	// between the fences, typeset away from the caret (mathblock.go).
	Math
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
	Image:     "Image",
	Media:     "Media",
	Callout:   "Callout",
	Table:     "Table",
	Math:      "Math",
}

func (k Kind) String() string { return kindNames[k] }

// IsList reports whether a block of this kind takes the list rules: Enter
// continues it, Enter on an empty item leaves it, Backspace at the start
// outdents and then turns it into a paragraph.
func (k Kind) IsList() bool { return k == Bullet || k == Numbered || k == Todo }

// HasInline reports whether the block's text is parsed as inline Markdown.
func (k Kind) HasInline() bool {
	return k != Code && k != Raw && k != Divider && k != Image && k != Media && k != Table && k != Math
}

// isSource reports whether the block is edited as its Markdown in a
// monospace panel: code, what the editor does not model, a table, and an
// equation's TeX.
func (k Kind) isSource() bool { return k == Code || k == Raw || k == Table || k == Math }

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
	// Title is a callout's title.
	Title string
	// Attrs is the block's presentation (alignment, a divider's style and so
	// on) as Kvit stores it: the inside of a "<!--kvit ...-->" comment at the
	// end of the block's Markdown, space-separated key=value tokens and bare
	// flags in key order. The editor keeps it so a note saves unchanged.
	Attrs string
}

var lastBlockID atomic.Int64

func newID() int64 { return lastBlockID.Add(1) }

// NewBlock is a block of a kind holding text, with an identity of its own.
func NewBlock(kind Kind, text string) Block {
	return Block{ID: newID(), Kind: kind, Text: text}
}

var (
	reHeading  = regexp.MustCompile(`^(#{1,6}) (.*)$`)
	reTodo     = regexp.MustCompile(`^([ \t]*)[-*+] \[( |x|X)\] ?(.*)$`)
	reBullet   = regexp.MustCompile(`^([ \t]*)[-*+] (.*)$`)
	reNumbered = regexp.MustCompile(`^([ \t]*)\d+[.)] (.*)$`)
	reQuote    = regexp.MustCompile(`^> ?(.*)$`)
	reDivider  = regexp.MustCompile(`^(\*{3,}|-{3,}|_{3,})\s*$`)
)

// fenceOpen reads a code fence's opening line: three or more backticks or
// tildes and an info string. A backtick fence's info string cannot hold a
// backtick, so such a line opens nothing; a tilde fence's may, and the
// backticks are dropped. It answers the fence's character, its length and the
// info string.
func fenceOpen(line string) (ch byte, n int, info string, ok bool) {
	if line == "" || (line[0] != '`' && line[0] != '~') {
		return 0, 0, "", false
	}
	ch = line[0]
	for n < len(line) && line[n] == ch {
		n++
	}
	if n < 3 {
		return 0, 0, "", false
	}
	info = strings.TrimSpace(line[n:])
	if strings.Contains(info, "`") {
		if ch == '`' {
			return 0, 0, "", false
		}
		info = strings.TrimSpace(strings.ReplaceAll(info, "`", ""))
	}
	return ch, n, info, true
}

// fenceCloses reports whether a line closes a fence of n characters ch: with
// the spaces around it taken off, it is ch alone, at least n of it. A line
// that only starts with the fence ("``` | y |") is the fence's content.
func fenceCloses(line string, ch byte, n int) bool {
	t := strings.TrimSpace(line)
	if len(t) < n {
		return false
	}
	for i := 0; i < len(t); i++ {
		if t[i] != ch {
			return false
		}
	}
	return true
}

// reTag is Kvit's attribute tag at the end of a line.
var reTag = regexp.MustCompile(`\s*<!--kvit (.*?)-->\s*$`)

// stripTag splits a trailing attribute tag off a line, returning the line
// without it and the tag's canonical payload.
func stripTag(line string) (string, string) {
	if !strings.Contains(line, "<!--kvit ") {
		return line, ""
	}
	m := reTag.FindStringSubmatchIndex(line)
	if m == nil {
		return line, ""
	}
	return line[:m[0]], canonicalAttrs(line[m[2]:m[3]])
}

// canonicalAttrs orders a payload's tokens by key, the last of a repeated key
// winning.
func canonicalAttrs(payload string) string {
	byKey := map[string]string{}
	for _, tok := range strings.Fields(payload) {
		key, _, _ := strings.Cut(tok, "=")
		byKey[key] = tok
	}
	keys := make([]string, 0, len(byKey))
	for k := range byKey {
		keys = append(keys, k)
	}
	slices.Sort(keys)
	out := make([]string, len(keys))
	for i, k := range keys {
		out[i] = byKey[k]
	}
	return strings.Join(out, " ")
}

// Attr is one of a block's presentation attributes: the value of key=value
// in its payload, "" for a bare flag, and whether it is there at all.
func (b *Block) Attr(key string) (string, bool) {
	for _, tok := range strings.Fields(b.Attrs) {
		k, v, _ := strings.Cut(tok, "=")
		if k == key {
			return v, true
		}
	}
	return "", false
}

// attachTag appends a payload to Markdown as Kvit's attribute tag.
func attachTag(md, attrs string) string {
	if attrs == "" {
		return md
	}
	return md + "  <!--kvit " + attrs + "-->"
}

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

// opensFence reports whether a line opens a code fence.
func opensFence(line string) bool {
	_, _, _, ok := fenceOpen(line)
	return ok
}

func startsBlock(line string) bool {
	return reHeading.MatchString(line) || reBullet.MatchString(line) || reNumbered.MatchString(line) ||
		reQuote.MatchString(line) || reDivider.MatchString(line) || opensFence(line) ||
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
	// afterList is whether the line just read was a list item or one of its
	// continuation lines: only then does an indented line join the item
	// above, which is how a wrapped item survives a round trip.
	afterList := false
	for i < len(lines) {
		prevList := afterList
		afterList = false
		line, attrs := stripTag(lines[i])
		if strings.TrimSpace(line) == "" && attrs == "" {
			i++
			continue
		}
		if ch, n, info, ok := fenceOpen(line); ok {
			j := i + 1
			for ; j < len(lines); j++ {
				if fenceCloses(lines[j], ch, n) {
					break
				}
				// Kvit once wrote a fence's tag after its closer, so a
				// closer with a tag still closes, and gives the fence its
				// tag when the opener has none; the tag on any other line
				// is content.
				if bare, legacy := stripTag(lines[j]); legacy != "" && fenceCloses(bare, ch, n) {
					if attrs == "" {
						attrs = legacy
					}
					break
				}
			}
			b := NewBlock(Code, strings.Join(lines[i+1:min(j, len(lines))], "\n"))
			if words := strings.Fields(info); len(words) > 0 {
				b.Lang = words[0]
			}
			b.Attrs = attrs
			// A fence arriving from outside the note (a note opened, Markdown
			// pasted) holding a character diagram is tagged and straightened.
			// Its whole info string decides, though only its first word is
			// kept.
			lang, text := textdiagram.Ingest(info, b.Text)
			if lang != info {
				b.Lang = lang
			}
			b.Text = text
			out = append(out, b)
			i = j + 1
			continue
		}
		if b, next, ok := parseMathFence(lines, i, line, attrs); ok {
			out = append(out, b)
			i = next
			continue
		}
		if strings.HasPrefix(line, "|") {
			j := i
			for j < len(lines) && strings.HasPrefix(lines[j], "|") {
				j++
			}
			tableLines := append([]string{line}, lines[i+1:j]...)
			tb := NewBlock(Table, strings.Join(tableLines, "\n"))
			tb.Attrs = attrs
			out = append(out, tb)
			i = j
			continue
		}
		one := func(b Block) {
			b.Attrs = attrs
			out = append(out, b)
			i++
		}
		// A lone image expression at the margin is an image or media block.
		if ref, ok := ParseImageLine(line); ok {
			kind := Image
			if ref.Media {
				kind = Media
			}
			one(NewBlock(kind, line))
			continue
		}
		if reDivider.MatchString(line) {
			one(NewBlock(Divider, ""))
			continue
		}
		if m := reTodo.FindStringSubmatch(line); m != nil {
			b := NewBlock(Todo, m[3])
			b.Indent = indentOf(m[1])
			b.Checked = m[2] != " "
			one(b)
			afterList = true
			continue
		}
		if m := reBullet.FindStringSubmatch(line); m != nil {
			b := NewBlock(Bullet, m[2])
			b.Indent = indentOf(m[1])
			one(b)
			afterList = true
			continue
		}
		if m := reNumbered.FindStringSubmatch(line); m != nil {
			b := NewBlock(Numbered, m[2])
			b.Indent = indentOf(m[1])
			one(b)
			afterList = true
			continue
		}
		// A continuation line: indented, with no marker of its own, directly
		// under a list item, belongs to that item.
		if rest := strings.TrimLeft(line, " \t"); prevList && rest != line && rest != "" && len(out) > 0 && out[len(out)-1].Kind.IsList() {
			item := &out[len(out)-1]
			item.Text += "\n" + rest
			if item.Attrs == "" {
				item.Attrs = attrs
			}
			afterList = true
			i++
			continue
		}
		// Five and six hashes are a fourth-level heading.
		if m := reHeading.FindStringSubmatch(line); m != nil {
			one(NewBlock(Heading1+Kind(min(len(m[1]), 4)-1), m[2]))
			continue
		}
		if reQuote.MatchString(line) {
			var body []string
			var quoteAttrs string
			for i < len(lines) {
				l, a := stripTag(lines[i])
				m := reQuote.FindStringSubmatch(l)
				if m == nil {
					break
				}
				if a != "" {
					quoteAttrs = a
				}
				body = append(body, m[1])
				i++
			}
			b := NewBlock(Quote, strings.Join(body, "\n"))
			if m := reCallout.FindStringSubmatch(body[0]); m != nil {
				b = NewBlock(Callout, strings.Join(body[1:], "\n"))
				b.Lang, b.Checked, b.Title = m[1], m[2] == "-", m[3]
			}
			b.Attrs = quoteAttrs
			out = append(out, b)
			continue
		}
		var body []string
		var paraAttrs string
		for i < len(lines) && strings.TrimSpace(lines[i]) != "" && (len(body) == 0 || !startsBlock(lines[i])) {
			l, a := stripTag(lines[i])
			if a != "" {
				paraAttrs = a
			}
			body = append(body, l)
			i++
		}
		b := NewBlock(Paragraph, strings.Join(body, "\n"))
		b.Attrs = paraAttrs
		out = append(out, b)
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
// a numbered block. A block's attribute tag trails its last line, or a code
// fence's opening line, whose closing fence must stay bare to close it.
func BlockMarkdown(b Block, number int) string {
	md := blockMarkdown(b, number)
	if b.Attrs == "" {
		return md
	}
	if b.Kind == Code || b.Kind == Table || b.Kind == Math {
		first, rest, found := strings.Cut(md, "\n")
		if !found {
			return attachTag(first, b.Attrs)
		}
		return attachTag(first, b.Attrs) + "\n" + rest
	}
	return attachTag(md, b.Attrs)
}

func blockMarkdown(b Block, number int) string {
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
		// An empty line is written as ">", so no line ends in a space.
		var out []string
		for _, l := range strings.Split(b.Text, "\n") {
			if l == "" {
				out = append(out, ">")
			} else {
				out = append(out, "> "+l)
			}
		}
		return strings.Join(out, "\n")
	case Callout:
		return calloutMarkdown(b)
	case Code:
		return "```" + b.Lang + "\n" + b.Text + "\n```"
	case Math:
		return "$$\n" + b.Text + "\n$$"
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

// Summarize is what a note list shows of a note and what search reads: the
// start of its text as drawn, up to 120 characters, its word count, and its
// whole text as drawn, one block to a line. Dividers count for nothing.
func Summarize(body string) (snippet string, words int, text string) {
	const snippetLength = 120
	var sb, all strings.Builder
	for _, b := range ParseMarkdown(body) {
		if b.Kind == Divider {
			continue
		}
		drawn := b.Text
		if b.Kind.HasInline() {
			src := []rune(b.Text)
			drawn = string(project(src, parseInline(src), nil).Disp)
		}
		words += len(strings.Fields(drawn))
		all.WriteString(drawn)
		all.WriteByte('\n')
		if sb.Len() < snippetLength && b.Text != "" {
			if sb.Len() > 0 {
				sb.WriteByte(' ')
			}
			sb.WriteString(strings.Join(strings.Fields(drawn), " "))
		}
	}
	snippet = sb.String()
	if r := []rune(snippet); len(r) > snippetLength {
		snippet = string(r[:snippetLength])
	}
	return snippet, words, all.String()
}

// PlainText is a block's inline Markdown as it is drawn: the markers of
// its formatted spans left out.
func PlainText(src string) string {
	r := []rune(src)
	return string(project(r, parseInline(r), nil).Disp)
}
