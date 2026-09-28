package export

// The blocks an export renders. The editor models paragraphs, headings, the
// three list kinds, quotes, callouts, code, dividers, images, media and
// tables, and keeps everything else as Raw Markdown. The Qt app models more:
// a $$ fence is a maths block, a code fence whose language is kanban, toc,
// mermaid or query renders as a board, a table of contents, a diagram or a
// query, and a quote is split where its nesting depth changes. Each editor
// block is read here into the block the Qt parser would have made of the same
// Markdown (src/domain/documentserializer.cpp, DocumentSerializer::parse, and
// src/domain/blockkinds.cpp, kindForState), so the renderers can follow the
// Qt block kinds one to one.

import (
	"net/url"
	"path"
	"regexp"
	"slices"
	"strconv"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"

	"github.com/kvit-s/kvit-notes/editor"
)

type kind int

const (
	kParagraph kind = iota
	kHeading
	kBullet
	kNumbered
	kTodo
	kQuote
	kCallout
	kCode
	kMermaid
	kToc
	kKanban
	kQuery
	kDivider
	kMath
	kTable
	kImage
	kMedia
)

func (k kind) isList() bool { return k == kBullet || k == kNumbered || k == kTodo }

// block is one block as the Qt exporter sees it (Block::State).
type block struct {
	kind    kind
	level   int    // a heading's level, 1 to 4
	indent  int    // a list item's nesting, a quote's depth less one
	text    string // the content, without its structural prefix
	checked bool   // a to-do's box; a callout's fold
	lang    string // a code fence's language; a callout's type
	title   string // a callout's title
	attrs   string // the <!--kvit …--> payload
	origin  int    // the index of the editor block this came from
}

// headingLevel is BlockKindDef::headingLevel: 1 to 4, or 0.
func (b *block) headingLevel() int {
	if b.kind == kHeading {
		return b.level
	}
	return 0
}

var reCallout = regexp.MustCompile(`^\[!([A-Za-z][A-Za-z0-9_-]*)\]([+-]?)\s*(.*)$`)

// fromEditor reads editor blocks as the Qt parser would read the same
// Markdown. A leading Raw block holding front matter is left out when
// skipFrontMatter is set, since no Qt export renders a note's metadata as
// part of its body; a leading Raw block that starts with "---" but is not
// front matter by the Qt app's rule is read as a divider and what follows.
func fromEditor(src []editor.Block, skipFrontMatter bool) []block {
	var out []block
	for i, b := range src {
		if i == 0 && b.Kind == editor.Raw && strings.HasPrefix(b.Text, "---") {
			if skipFrontMatter {
				if s := splitFrontMatter(b.Text + "\n"); s.present && s.body == "" {
					continue
				}
			}
			for _, x := range rereadDashes(b.Text) {
				x.origin = i
				out = append(out, x)
			}
			continue
		}
		for _, x := range convert(b) {
			x.origin = i
			out = append(out, x)
		}
	}
	return out
}

// rereadDashes reads text the editor took for front matter as the Qt parser
// reads a body that starts with "---": a divider, then the rest.
func rereadDashes(text string) []block {
	first, rest, _ := strings.Cut(text, "\n")
	line, attrs := stripTag(first)
	var out []block
	if t := trimSpace(line); t == "---" || t == "***" {
		out = append(out, block{kind: kDivider, attrs: attrs})
	} else {
		out = append(out, paragraphs(line, attrs)...)
	}
	parsed := editor.ParseMarkdown(rest)
	for i, b := range parsed {
		if i == 0 && b.Kind == editor.Raw && strings.HasPrefix(b.Text, "---") {
			out = append(out, rereadDashes(b.Text)...)
			continue
		}
		out = append(out, convert(b)...)
	}
	return out
}

// reTag is Kvit's attribute tag at the end of a line
// (src/content/blockattributes.cpp).
var reTag = regexp.MustCompile(`\s*<!--kvit (.*?)-->\s*$`)

// stripTag splits a trailing attribute tag off a line, returning the line
// and the tag's payload in canonical order (BlockAttributes::stripTag).
func stripTag(line string) (string, string) {
	if !strings.Contains(line, "<!--kvit ") {
		return line, ""
	}
	m := reTag.FindStringSubmatchIndex(line)
	if m == nil {
		return line, ""
	}
	return line[:m[0]], canonicalAttrs(trimSpace(line[m[2]:m[3]]))
}

// canonicalAttrs is a payload with its keys in order, the last of a repeated
// key winning (BlockAttributes::canonical).
func canonicalAttrs(payload string) string {
	a := parseAttrs(payload)
	keys := sortedKeys(a)
	out := make([]string, len(keys))
	for i, k := range keys {
		if a[k] == "" {
			out[i] = k
		} else {
			out[i] = k + "=" + a[k]
		}
	}
	return strings.Join(out, " ")
}

func convert(b editor.Block) []block {
	switch b.Kind {
	case editor.Heading1, editor.Heading2, editor.Heading3, editor.Heading4:
		return []block{{kind: kHeading, level: int(b.Kind-editor.Heading1) + 1, text: b.Text, attrs: b.Attrs}}
	case editor.Bullet:
		return []block{{kind: kBullet, indent: b.Indent, text: b.Text, attrs: b.Attrs}}
	case editor.Numbered:
		return []block{{kind: kNumbered, indent: b.Indent, text: b.Text, attrs: b.Attrs}}
	case editor.Todo:
		return []block{{kind: kTodo, indent: b.Indent, text: b.Text, checked: b.Checked, attrs: b.Attrs}}
	case editor.Quote:
		return quotes(b.Text, b.Attrs)
	case editor.Callout:
		// Written back as the quote it was read from, so that a nested line
		// in its body ends it as the Qt parser ends it.
		header := "[!" + b.Lang + "]"
		if b.Checked {
			header += "-"
		}
		if b.Title != "" {
			header += " " + b.Title
		}
		text := header
		if b.Text != "" {
			text += "\n" + b.Text
		}
		return quotes(text, b.Attrs)
	case editor.Table:
		return tableBlocks(strings.Split(b.Text, "\n"), b.Attrs)
	case editor.Image, editor.Media:
		return paragraphs(b.Text, b.Attrs)
	case editor.Code:
		k := kCode
		switch b.Lang {
		case "kanban":
			k = kKanban
		case "toc":
			k = kToc
		case "mermaid":
			k = kMermaid
		case "query":
			k = kQuery
		}
		return []block{{kind: k, text: b.Text, lang: b.Lang, attrs: b.Attrs}}
	case editor.Divider:
		return []block{{kind: kDivider, attrs: b.Attrs}}
	case editor.Math:
		// A display equation's text is its TeX, without the $$ lines.
		return []block{{kind: kMath, text: b.Text, attrs: b.Attrs}}
	case editor.Raw:
		return raw(b.Text, b.Attrs)
	}
	return paragraphs(b.Text, b.Attrs)
}

// paragraphs splits a paragraph around its image lines: the Qt parser makes
// a line that is exactly one image expression an image block of its own,
// whatever is around it.
func paragraphs(text, attrs string) []block {
	var out []block
	var run []string
	flush := func() {
		if len(run) > 0 {
			out = append(out, block{kind: kParagraph, text: strings.Join(run, "\n")})
			run = nil
		}
	}
	for _, line := range strings.Split(text, "\n") {
		if p := parseImageLine(line); p.valid && p.kind != mediaNone {
			flush()
			k := kImage
			if p.kind == mediaAV {
				k = kMedia
			}
			out = append(out, block{kind: k, text: line})
			continue
		}
		run = append(run, line)
	}
	flush()
	if len(out) > 0 {
		out[len(out)-1].attrs = attrs
	}
	return out
}

// quotes splits a quote by nesting depth and recognises a callout, as the
// Qt parser's quote runs do: each run of lines at one depth is one block,
// and a run whose first line is "[!type] Title" is a callout.
func quotes(text, attrs string) []block {
	type line struct {
		depth int
		rest  string
	}
	var lines []line
	for _, l := range strings.Split(text, "\n") {
		// The editor took one ">" and one space off; the rest are counted
		// here the way the Qt parser counts them.
		depth, rest := 1, l
		for strings.HasPrefix(rest, ">") {
			depth++
			rest = strings.TrimPrefix(rest[1:], " ")
		}
		lines = append(lines, line{depth, rest})
	}
	var out []block
	for i := 0; i < len(lines); {
		j := i
		var run []string
		for j < len(lines) && lines[j].depth == lines[i].depth {
			run = append(run, lines[j].rest)
			j++
		}
		b := block{kind: kQuote, indent: min(max(lines[i].depth-1, 0), 4), text: strings.Join(run, "\n")}
		if m := reCallout.FindStringSubmatch(run[0]); m != nil {
			b = block{kind: kCallout, lang: m[1], checked: m[2] == "-", title: m[3], text: strings.Join(run[1:], "\n")}
		}
		if i == 0 {
			b.attrs = attrs
		}
		out = append(out, b)
		i = j
	}
	return out
}

// raw reads the Markdown the editor keeps verbatim: a pipe table, a $$ fence,
// and anything else, which the Qt parser would read as a paragraph.
func raw(text, attrs string) []block {
	lines := strings.Split(text, "\n")
	first, firstAttrs := stripTag(lines[0])
	if firstAttrs == "" {
		firstAttrs = attrs
	}
	switch {
	case strings.HasPrefix(first, "|"):
		return tableBlocks(append([]string{first}, lines[1:]...), firstAttrs)
	case strings.HasPrefix(first, "$$"):
		t := trimSpace(first)
		if t == "$$" {
			var body []string
			for _, l := range lines[1:] {
				if trimSpace(l) == "$$" {
					break
				}
				body = append(body, l)
			}
			return []block{{kind: kMath, text: strings.Join(body, "\n"), attrs: firstAttrs}}
		}
		if r := []rune(t); len(r) > 4 && strings.HasSuffix(t, "$$") {
			return []block{{kind: kMath, text: trimSpace(string(r[2 : len(r)-2])), attrs: firstAttrs}}
		}
	}
	return paragraphs(text, attrs)
}

// tableBlocks is a pipe table's rows as a table block. A tag left on a data
// row, where Kvit once wrote it, is taken off; rows that do not make a table
// are the paragraph the Qt parser would read them as.
func tableBlocks(lines []string, attrs string) []block {
	rows := make([]string, len(lines))
	for i, l := range lines {
		row, a := stripTag(l)
		if attrs == "" {
			attrs = a
		}
		rows[i] = row
	}
	content := strings.Join(rows, "\n")
	if parseTable(content).valid {
		return []block{{kind: kTable, text: content, attrs: attrs}}
	}
	return paragraphs(content, attrs)
}

// ---- attributes (src/domain/blockkinds/blockstyle.cpp) ----

// parseAttrs is BlockAttributes::parseMap: whitespace-separated key=value
// tokens and bare flags, a flag mapping to "".
func parseAttrs(payload string) map[string]string {
	m := map[string]string{}
	for _, tok := range strings.FieldsFunc(payload, isSpace) {
		key, value, found := strings.Cut(tok, "=")
		if !found {
			m[tok] = ""
		} else if key != "" {
			m[key] = value
		}
	}
	return m
}

func sortedKeys(m map[string]string) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	// QMap orders keys by QString's comparison, which for these ASCII keys is
	// byte order.
	slices.Sort(keys)
	return keys
}

func attrHas(a map[string]string, key string) bool {
	_, ok := a[key]
	return ok
}

func attrStr(a map[string]string, key, fallback string) string {
	if v, ok := a[key]; ok && v != "" {
		return v
	}
	return fallback
}

func attrNum(a map[string]string, key string, fallback int) int {
	if v, err := strconv.Atoi(trimSpace(attrStr(a, key, ""))); err == nil {
		return v
	}
	return fallback
}

func utf16Len(s string) int {
	n := 0
	for _, r := range s {
		if r > 0xFFFF {
			n += 2
		} else {
			n++
		}
	}
	return n
}

// cssColor is an attribute value as a CSS colour, or "" when it is not one:
// a hex colour of 3, 4, 6 or 8 digits, or a bare colour word.
func cssColor(v string) string {
	if v == "" || utf16Len(v) > 32 {
		return ""
	}
	if digits, ok := strings.CutPrefix(v, "#"); ok {
		switch len(digits) {
		case 3, 4, 6, 8:
		default:
			return ""
		}
		for _, c := range digits {
			if !isHexDigit(c) {
				return ""
			}
		}
		return v
	}
	for _, c := range v {
		if !unicode.IsLetter(c) {
			return ""
		}
	}
	return v
}

// cssFontFamily is an attribute value as a quoted CSS family name, or "".
func cssFontFamily(v string) string {
	if v == "" || utf16Len(v) > 64 {
		return ""
	}
	for _, c := range v {
		if !(isLetterOrNumber(c) || c == ' ' || c == '-') {
			return ""
		}
	}
	return "'" + v + "'"
}

func styleAttr(decls []string) string {
	if len(decls) == 0 {
		return ""
	}
	return ` style="` + strings.Join(decls, ";") + `"`
}

// textAlign is a text-align declaration, or "" when the block is aligned the
// way its kind aligns by default.
func textAlign(a map[string]string, kindDefault string) string {
	align := attrStr(a, "align", kindDefault)
	if align == kindDefault {
		return ""
	}
	switch align {
	case "center", "right", "justify", "left":
		return "text-align:" + align
	}
	return ""
}

// withDropCap wraps the first character a reader sees in a drop-cap span
// when the block asks for one of two lines or more.
func withDropCap(html string, a map[string]string) string {
	lines := attrNum(a, "dropcap", 0)
	if lines < 2 || html == "" {
		return html
	}
	start := 0
	for start < len(html) && html[start] == '<' {
		closeAt := strings.IndexByte(html[start:], '>')
		if closeAt < 0 {
			return html
		}
		start += closeAt + 1
	}
	if start >= len(html) || html[start] == '\\' {
		return html
	}
	// One character. A character outside the Basic Multilingual Plane is two
	// UTF-16 units to Qt, and the Qt exporter wraps only the first of them;
	// the whole character is wrapped here, which keeps the output valid.
	_, size := utf8.DecodeRuneInString(html[start:])
	end := start + size
	if html[start] == '&' {
		if semi := strings.IndexByte(html[start:], ';'); semi > 0 && utf16Len(html[start:start+semi]) <= 10 {
			end = start + semi + 1
		}
	}
	decls := []string{"font-size:" + strconv.FormatFloat(float64(lines)*1.15, 'f', 2, 64) + "em"}
	if c := cssColor(attrStr(a, "dropcapcolor", "")); c != "" {
		decls = append(decls, "color:"+c)
	}
	if f := cssFontFamily(attrStr(a, "dropcapfont", "")); f != "" {
		decls = append(decls, "font-family:"+f)
	}
	return html[:start] + `<span class="dropcap"` + styleAttr(decls) + ">" + html[start:end] + "</span>" + html[end:]
}

// ---- pipe tables (src/content/tabledata.cpp) ----

type table struct {
	valid   bool
	headers []string
	rows    [][]string
}

var reBreak = regexp.MustCompile(`(?i)[ \t]*<br\s*/?>`)

func splitCells(line string) []string {
	s := []rune(trimSpace(line))
	var cells []string
	var cur []rune
	flush := func() {
		cells = append(cells, reBreak.ReplaceAllString(trimSpace(string(cur)), "\n"))
		cur = nil
	}
	for i := 0; i < len(s); i++ {
		if s[i] == '\\' && i+1 < len(s) && s[i+1] == '|' {
			cur = append(cur, '|')
			i++
			continue
		}
		if s[i] == '|' {
			flush()
			continue
		}
		cur = append(cur, s[i])
	}
	flush()
	if len(cells) > 1 && cells[0] == "" {
		cells = cells[1:]
	}
	if len(cells) > 1 && cells[len(cells)-1] == "" {
		cells = cells[:len(cells)-1]
	}
	return cells
}

var reDelimCell = regexp.MustCompile(`^:?-+:?$`)

func isDelimiterRow(line string) bool {
	cells := splitCells(line)
	if len(cells) == 0 {
		return false
	}
	for _, c := range cells {
		if !reDelimCell.MatchString(trimSpace(c)) {
			return false
		}
	}
	return true
}

func parseTable(markdown string) table {
	var t table
	lines := strings.Split(markdown, "\n")
	if len(lines) < 2 || !strings.Contains(lines[0], "|") || !isDelimiterRow(lines[1]) ||
		len(splitCells(lines[0])) != len(splitCells(lines[1])) {
		return t
	}
	t.headers = splitCells(lines[0])
	cols := len(t.headers)
	for _, l := range lines[2:] {
		if trimSpace(l) == "" {
			continue
		}
		row := splitCells(l)
		for len(row) < cols {
			row = append(row, "")
		}
		t.rows = append(t.rows, row[:cols])
	}
	t.valid = true
	return t
}

// ---- image expressions (src/content/imageassets.cpp) ----

type mediaKind int

const (
	mediaNone mediaKind = iota
	mediaImage
	mediaAV
)

type imageExpr struct {
	valid   bool
	alt     string
	path    string
	caption string
	width   int
	kind    mediaKind
}

var (
	reImage   = regexp.MustCompile(`^!\[((?:\\.|[^\]\\])*)\]\((.*)\)$`)
	reCaption = regexp.MustCompile(`^(.*?)\s+"((?:\\.|[^"\\])*)"$`)
	reWidth   = regexp.MustCompile(`^(\d+)(?:x\d+)?$`)
)

func unescapeField(text string) string {
	var sb strings.Builder
	r := []rune(text)
	for i := 0; i < len(r); i++ {
		if r[i] == '\\' && i+1 < len(r) {
			i++
			if r[i] == 'n' {
				sb.WriteRune('\n')
			} else {
				sb.WriteRune(r[i])
			}
			continue
		}
		sb.WriteRune(r[i])
	}
	return sb.String()
}

func lastUnescapedBar(text string) int {
	for i := len(text) - 1; i >= 0; i-- {
		if text[i] != '|' {
			continue
		}
		slashes := 0
		for j := i - 1; j >= 0 && text[j] == '\\'; j-- {
			slashes++
		}
		if slashes&1 == 0 {
			return i
		}
	}
	return -1
}

// parseImageLine is ImageAssets::parseLine: one ![alt|width](path "caption")
// expression that is the whole line.
func parseImageLine(line string) imageExpr {
	var p imageExpr
	m := reImage.FindStringSubmatch(line)
	if m == nil {
		return p
	}
	altPart, inner := m[1], m[2]
	pth, caption := inner, ""
	if cm := reCaption.FindStringSubmatch(inner); cm != nil {
		pth, caption = cm[1], unescapeField(cm[2])
	}
	if pth == "" {
		return p
	}
	alt, width := altPart, 0
	if bar := lastUnescapedBar(altPart); bar >= 0 {
		if wm := reWidth.FindStringSubmatch(trimSpace(altPart[bar+1:])); wm != nil {
			width, _ = strconv.Atoi(wm[1])
			alt = altPart[:bar]
		}
	}
	p = imageExpr{valid: true, alt: unescapeField(alt), path: pth, caption: caption, width: width, kind: kindForExtension(pth)}
	if p.kind == mediaNone && isRemote(pth) {
		p.kind = mediaImage
	}
	return p
}

func extensionOf(p string) string {
	if q := strings.IndexByte(p, '?'); q >= 0 {
		p = p[:q]
	}
	if h := strings.IndexByte(p, '#'); h >= 0 {
		p = p[:h]
	}
	name := path.Base(strings.ReplaceAll(p, "\\", "/"))
	if dot := strings.LastIndexByte(name, '.'); dot >= 0 && strings.HasSuffix(p, name) {
		return strings.ToLower(name[dot+1:])
	}
	return ""
}

func kindForExtension(p string) mediaKind {
	switch extensionOf(p) {
	case "png", "jpg", "jpeg", "gif", "webp", "svg", "bmp":
		return mediaImage
	case "mp3", "wav", "ogg", "flac", "m4a", "mp4", "webm", "mkv", "mov":
		return mediaAV
	}
	return mediaNone
}

// urlScheme is the scheme a URL starts with, or "".
func urlScheme(s string) string {
	for i, c := range s {
		if c == ':' {
			return s[:i]
		}
		letter := isASCIILetter(c)
		if (i == 0 && !letter) || (i > 0 && !letter && !(c >= '0' && c <= '9') && c != '+' && c != '-' && c != '.') {
			return ""
		}
	}
	return ""
}

func isRemote(s string) bool {
	sc := strings.ToLower(urlScheme(s))
	return sc == "http" || sc == "https"
}

// isEmbedURL reports whether an image expression's URL names a web page
// rather than an image or media file, so it renders as a preview card.
func isEmbedURL(u string) bool {
	u = trimSpace(u)
	return isRemote(u) && kindForExtension(u) == mediaNone
}

func urlHost(u string) string {
	parsed, err := url.Parse(u)
	if err != nil {
		return ""
	}
	return strings.ToLower(parsed.Hostname())
}

// ---- task boards (src/content/kanbandata.cpp, the reading half) ----

type card struct {
	title       string
	done        bool
	labels      []string
	due         string
	description string
}

type column struct {
	name  string
	cards []card
}

type board struct {
	columns  []column
	preamble []string
}

var (
	reLabel     = regexp.MustCompile(`(^|\s)(\\*)#(?:"((?:\\.|[^"\\])*)"|([^\s#]*))`)
	reDue       = regexp.MustCompile(`(\\*)\x{1F4C5}\s*(\d{4}-\d{2}-\d{2})`)
	reCard      = regexp.MustCompile(`^[-*] \[( |x|X)\] ?(.*)$`)
	reCardStamp = regexp.MustCompile(`\s*<!--kvit((?:\s+\w+=[0-9-]+)*)\s*-->\s*$`)
)

func isRealDate(s string) bool {
	_, err := time.Parse("2006-01-02", s)
	return err == nil
}

func unescapeLabel(text string) string {
	var sb strings.Builder
	r := []rune(text)
	for i := 0; i < len(r); i++ {
		if r[i] == '\\' && i+1 < len(r) && (r[i+1] == '\\' || r[i+1] == '"') {
			i++
		}
		sb.WriteRune(r[i])
	}
	return sb.String()
}

type edit struct {
	start, length int
	replacement   string
}

func applyEdits(text string, edits []edit) string {
	for i := len(edits) - 1; i >= 0; i-- {
		e := edits[i]
		text = text[:e.start] + e.replacement + text[e.start+e.length:]
	}
	return text
}

// simplified is QString::simplified: whitespace runs to one space, trimmed.
func simplified(s string) string { return strings.Join(strings.FieldsFunc(s, isSpace), " ") }

func parseCardBody(rest string, c *card) {
	body := rest
	var edits []edit
	for _, m := range reDue.FindAllStringSubmatchIndex(body, -1) {
		slashes := m[3] - m[2]
		kept := strings.Repeat(`\`, slashes/2)
		if slashes%2 == 1 {
			edits = append(edits, edit{m[2], slashes, kept})
			continue
		}
		date := body[m[4]:m[5]]
		if c.due != "" || !isRealDate(date) {
			continue
		}
		c.due = date
		edits = append(edits, edit{m[2], m[1] - m[2], kept})
	}
	body = applyEdits(body, edits)
	edits = nil
	for _, m := range reLabel.FindAllStringSubmatchIndex(body, -1) {
		slashes := m[5] - m[4]
		var label string
		if m[6] >= 0 {
			label = unescapeLabel(body[m[6]:m[7]])
		} else {
			label = body[m[8]:m[9]]
		}
		kept := strings.Repeat(`\`, slashes/2)
		if slashes%2 == 1 || label == "" {
			edits = append(edits, edit{m[4], slashes, kept})
			continue
		}
		c.labels = append(c.labels, label)
		edits = append(edits, edit{m[4], m[1] - m[4], kept})
	}
	body = applyEdits(body, edits)
	c.title = simplified(body)
}

func leadingIndent(line string) string {
	i := 0
	for i < len(line) && (line[i] == ' ' || line[i] == '\t') {
		i++
	}
	return line[:i]
}

// parseBoard reads a kanban fence into columns of cards (KanbanData::parse),
// keeping only what an export shows.
func parseBoard(content string) board {
	var b board
	if content == "" {
		return b
	}
	col, desc := -1, -1
	indents := map[[2]int]string{}
	rawDesc := map[[2]int]bool{}
	var pending []string
	keep := func(raw string) {
		if col < 0 {
			b.preamble = append(b.preamble, raw)
		}
	}
	flush := func() {
		for _, p := range pending {
			keep(p)
		}
		pending = nil
	}
	for _, raw := range strings.Split(content, "\n") {
		if desc >= 0 && trimSpace(raw) == "" {
			pending = append(pending, raw)
			continue
		}
		if strings.HasPrefix(raw, "## ") {
			flush()
			b.columns = append(b.columns, column{name: trimSpace(raw[3:])})
			col, desc = len(b.columns)-1, -1
			continue
		}
		if col < 0 {
			flush()
			keep(raw)
			continue
		}
		cm := reCard.FindStringSubmatch(trimSpace(raw))
		indented := strings.HasPrefix(raw, "  ") || strings.HasPrefix(raw, "\t")
		if cm != nil && !indented {
			flush()
			c := card{done: cm[1] != " "}
			body := cm[2]
			if sm := reCardStamp.FindStringIndex(body); sm != nil {
				body = body[:sm[0]]
			}
			parseCardBody(body, &c)
			b.columns[col].cards = append(b.columns[col].cards, c)
			desc = len(b.columns[col].cards) - 1
			continue
		}
		if indented && desc >= 0 {
			key := [2]int{col, desc}
			c := &b.columns[col].cards[desc]
			if len(pending) > 0 && !rawDesc[key] {
				flush()
				desc = -1
				continue
			}
			for range pending {
				c.description += "\n"
			}
			pending = nil
			if !rawDesc[key] {
				indents[key] = leadingIndent(raw)
			}
			text := raw
			if ind := indents[key]; ind != "" && strings.HasPrefix(text, ind) {
				text = text[len(ind):]
			} else {
				text = trimSpace(text)
			}
			if c.description == "" {
				c.description = text
			} else {
				c.description += "\n" + text
			}
			rawDesc[key] = true
			continue
		}
		flush()
		desc = -1
		keep(raw)
	}
	flush()
	return b
}
