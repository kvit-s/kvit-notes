package export

// HTML to Markdown: the HTML a browser or word processor puts on the
// clipboard, or an HTML file, read into blocks (paragraphs, headings, list
// items, quotes, code listings and tables) with their bold, italic,
// struck-through, code and link runs, and written as Kvit's Markdown.
//
// The HTML is read into a block for each paragraph-like element and each
// line of a <pre>, a line separator (U+2028) for each <br>, list items
// numbered within their list, table cells in a grid, and runs of text with
// their character formats, the page's style sheet applied (css.go). Where a
// browser and the earlier Qt version of Kvit Notes read HTML differently, it
// is read as that version read it, so a paste gives the same Markdown: only
// the elements in blockElems start blocks, and text in any other element is
// part of the paragraph around it; a block element reuses an empty
// paragraph, so an empty list item gives up its number; closing a <div> ends
// its paragraph only when the div held other elements; a paragraph started
// by text after a closed block has no margins; whitespace after a block is
// added to that block; and a table inside a table cell is dropped.

import (
	"html"
	"regexp"
	"strconv"
	"strings"
	"unicode"
	"unicode/utf8"
)

// hfmt is a run's character format: what the converter writes, and in
// extra everything else the format keeps (colour, size, family, underline
// and the like), in one canonical string. Two runs are one fragment only when
// all of it is equal, which is why "<b>x<u>y</u>z</b>" converts to three bold
// runs.
type hfmt struct {
	bold, italic, strike, mono bool
	href                       string
	extra                      string
}

// setExtra sets one property in a format's extra, "" removing it.
func (f *hfmt) setExtra(key, value string) {
	props := map[string]string{}
	for _, kv := range strings.Split(f.extra, ";") {
		if k, v, ok := strings.Cut(kv, "="); ok {
			props[k] = v
		}
	}
	if value == "" {
		delete(props, key)
	} else {
		props[key] = value
	}
	var parts []string
	for _, k := range sortedKeys(props) {
		parts = append(parts, k+"="+props[k])
	}
	f.extra = strings.Join(parts, ";")
}

type hfrag struct {
	text    string
	f       hfmt
	image   bool
	src     string
	keepsWS bool // from preformatted text, whose spaces are content
}

type hlist struct {
	ordered bool
}

type hblock struct {
	frags   []hfrag
	heading int
	list    *hlist // the list the block is an item of
	indent  int    // that list's nesting depth
	item    int    // the block's place in its list, from 0, counted at the end
	inList  bool   // inside a list without being an item, which indents it
	quote   bool   // indented or with margins on both sides
	pre     bool   // lines that do not wrap: a <pre>, or white-space pre or nowrap
	nodes   *[]hnode
}

func (b *hblock) text() string {
	var sb strings.Builder
	for _, f := range b.frags {
		if f.image {
			sb.WriteRune('\uFFFC')
		} else {
			sb.WriteString(f.text)
		}
	}
	return sb.String()
}

type hcell struct {
	nodes            []hnode
	colspan, rowspan int
}

// htable is a table and where it goes. A table inside a table cell has
// nowhere to go: the converter reads a cell's blocks and passes over a
// table in it.
type htable struct {
	rows     [][]*hcell
	parent   *[]hnode
	attached bool
}

// attach puts the table in its frame, once, when its first row starts, so a
// caption written before the rows comes before it.
func (t *htable) attach() {
	if !t.attached && t.parent != nil {
		*t.parent = append(*t.parent, hnode{table: t})
		t.attached = true
	}
}

// hnode is one entry of a frame: a block or a table.
type hnode struct {
	block *hblock
	table *htable
}

type helem struct {
	name     string
	attrs    map[string]string
	f        hfmt
	keepWS   bool // whitespace is preserved inside
	nbsp     bool // white-space nowrap: a space is a no-break space
	nonBreak bool
	// ownL and ownR are the element's own left and right margins; sumL and
	// sumR add those of the block elements around it, up to a table cell or
	// an element that is not a block.
	ownL, ownR float64
	sumL, sumR float64
	chain      bool // a block the margins of the blocks inside it add to
	blockInd   bool
	heading    int
	listDepth  int
	list       *hlist // the nearest enclosing list
	ordered    bool   // for ul and ol, whether the list is numbered
	nodes      *[]hnode
	table      *htable
	inCell     bool
	skipNL     bool // a <pre> that has not had any text yet
	block      bool // an element that starts a block
	hasChild   bool // an element started inside it
}

// The elements that start a block, the ones whose margins the
// blocks inside them add, the table elements, and the elements with no
// content.
var (
	blockElems = wordSet("blockquote center dd div dl dt h1 h2 h3 h4 h5 h6 hr li ol p pre ul td th caption html body")
	chainElems = wordSet("blockquote center dd div dl dt h1 h2 h3 h4 h5 h6 hr li ol p pre ul caption html body table")
	tableElems = wordSet("table tr thead tbody tfoot")
	voidElems  = wordSet("br hr img meta link input col area base wbr embed source track param basefont frame")
	skipElems  = wordSet("script style title")
	pClosers   = wordSet("blockquote center div dl h1 h2 h3 h4 h5 h6 hr ol p pre table ul li dd dt")
)

type hconv struct {
	stack  []*helem
	root   []hnode
	rules  []cssRule // the page's style sheets, in the order read
	order  int
	blocks []*hblock // every block, in document order
	cur    *hblock   // the block the cursor is in
	// closed is set when a block element has ended since the cursor's block
	// took text: the next text starts a block of its own. closedIn is the
	// element the ended block was in.
	closed   bool
	closedIn *helem
}

func (c *hconv) top() *helem { return c.stack[len(c.stack)-1] }

// setBlockProps gives a block the format of the element it is for.
func setBlockProps(b *hblock, e *helem) {
	b.heading = e.heading
	b.quote = e.blockInd || (e.sumL > 0 && e.sumR > 0)
	b.pre = e.nonBreak
	b.list, b.indent = nil, 0
	b.inList = e.listDepth > 0
}

func (c *hconv) addBlock(b *hblock) *hblock {
	*b.nodes = append(*b.nodes, hnode{block: b})
	c.blocks = append(c.blocks, b)
	c.cur, c.closed = b, false
	return b
}

// newBlock is a block element's block.
func (c *hconv) newBlock(e *helem) *hblock {
	b := &hblock{nodes: e.nodes}
	setBlockProps(b, e)
	return c.addBlock(b)
}

// newTextBlock is a block started by text rather than by a block element:
// the text after a closed block, or at the very start. It has the format of
// the text's own element, which has no margins and no heading level; only a
// list around it still indents it.
func (c *hconv) newTextBlock(e *helem) *hblock {
	return c.addBlock(&hblock{nodes: e.nodes, inList: e.listDepth > 0})
}

// splitBlock is the block after a line break in preformatted text: a copy of
// the one before.
func (c *hconv) splitBlock(e *helem) *hblock {
	if c.cur == nil || c.cur.nodes != e.nodes {
		return c.newTextBlock(e)
	}
	b := *c.cur
	b.frags = nil
	return c.addBlock(&b)
}

// startBlock starts the block for a block element. It reuses the block the
// cursor is in while that is still empty, whatever made it, so
// "<li><p>text</p></li>" is one list item and an empty item gives up its
// place in the list to the next one. A reused list item stays in its list
// unless another item takes the block.
func (c *hconv) startBlock(e *helem) {
	if c.cur != nil && c.cur.nodes == e.nodes && len(c.cur.frags) == 0 {
		list, indent := c.cur.list, c.cur.indent
		setBlockProps(c.cur, e)
		if list != nil {
			c.cur.list, c.cur.indent, c.cur.inList = list, indent, false
		}
		c.closed = false
	} else {
		c.newBlock(e)
	}
	if e.name == "li" && e.list != nil {
		c.cur.list, c.cur.indent, c.cur.inList = e.list, e.listDepth, false
	}
}

// fresh reports whether text for e would start a block: the cursor's block
// is closed or in another frame.
func (c *hconv) fresh(e *helem) bool {
	return c.cur == nil || c.cur.nodes != e.nodes || c.closed
}

// blockFor is the block text of e goes into: the cursor's block, or, when
// that is closed or in another frame, a new one. An empty closed block takes
// text written straight after it, but not text in an element opened after
// it, which starts a block of its own.
func (c *hconv) blockFor(e *helem) *hblock {
	if c.cur != nil && c.cur.nodes == e.nodes {
		if !c.closed {
			return c.cur
		}
		if len(c.cur.frags) == 0 && c.top() == c.closedIn {
			c.closed = false
			return c.cur
		}
	}
	return c.newTextBlock(e)
}

// objectBlock is the block an image or a line break goes into: the cursor's
// block, even when it is closed.
func (c *hconv) objectBlock(e *helem) *hblock {
	if c.cur != nil && c.cur.nodes == e.nodes {
		return c.cur
	}
	return c.newTextBlock(e)
}

// lastIsSpace reports whether the cursor's block ends in a space or a line
// break, or has nothing yet, so that a space starting the next text collapses
// into it.
func (c *hconv) lastIsSpace() bool {
	if c.cur == nil || len(c.cur.frags) == 0 {
		return true
	}
	last := c.cur.frags[len(c.cur.frags)-1]
	if last.image {
		return false
	}
	if last.text == "" {
		return true
	}
	r, _ := utf8.DecodeLastRuneInString(last.text)
	return r == ' ' || r == '\u2028'
}

var reHTMLSpace = regexp.MustCompile(`[ \t\n\r\f]+`)

// text adds a run of text as the source has it, entities not yet decoded;
// last says whether nothing follows it.
func (c *hconv) text(raw string, last bool) {
	e := c.top()
	if e.table != nil && !e.inCell {
		// Text in a table but outside its cells has no place in the grid.
		return
	}
	s := html.UnescapeString(raw)
	if !e.keepWS {
		s = reHTMLSpace.ReplaceAllString(s, " ")
		if s == " " && c.closed && c.cur != nil && c.cur.nodes == e.nodes {
			// Whitespace after a block. A single whitespace character
			// between a block and the next tag is dropped; anything else
			// is added to the block the cursor is still in.
			if len(raw) == 1 && !last && c.closedIn == e {
				return
			}
			if !c.lastIsSpace() {
				c.cur.frags = append(c.cur.frags, hfrag{text: s, f: e.f})
			}
			return
		}
		if c.fresh(e) || c.lastIsSpace() {
			s = strings.TrimPrefix(s, " ")
		}
		if s == "" {
			return
		}
		if e.nbsp {
			s = strings.ReplaceAll(s, " ", "\u00a0")
		}
		b := c.blockFor(e)
		b.frags = append(b.frags, hfrag{text: s, f: e.f})
		return
	}
	s = strings.ReplaceAll(strings.ReplaceAll(s, "\r\n", "\n"), "\r", "\n")
	for i := len(c.stack) - 1; i >= 0; i-- {
		if c.stack[i].name == "pre" {
			if c.stack[i].skipNL {
				s = strings.TrimPrefix(s, "\n")
				c.stack[i].skipNL = false
			}
			break
		}
	}
	// Each line of preformatted text is a block of its own, a blank line an
	// empty one.
	for i, seg := range strings.Split(s, "\n") {
		if i > 0 {
			c.blockFor(e)
			c.splitBlock(e)
		}
		if seg != "" {
			b := c.blockFor(e)
			b.frags = append(b.frags, hfrag{text: seg, f: e.f, keepsWS: true})
		}
	}
}

// canonFamily is a font-family list as one comparable string.
func canonFamily(v string) string {
	var out []string
	for _, f := range strings.Split(v, ",") {
		out = append(out, strings.Trim(strings.ToLower(strings.TrimSpace(f)), `"'`))
	}
	return strings.Join(out, ",")
}

var basicColors = map[string]string{
	"black": "#000000", "silver": "#c0c0c0", "gray": "#808080", "grey": "#808080", "white": "#ffffff",
	"maroon": "#800000", "red": "#ff0000", "purple": "#800080", "fuchsia": "#ff00ff", "green": "#008000",
	"lime": "#00ff00", "olive": "#808000", "yellow": "#ffff00", "navy": "#000080", "blue": "#0000ff",
	"teal": "#008080", "aqua": "#00ffff", "orange": "#ffa500",
}

var reRGB = regexp.MustCompile(`^rgba?\(\s*([\d.]+%?)\s*,\s*([\d.]+%?)\s*,\s*([\d.]+%?)\s*(?:,\s*([\d.]+%?)\s*)?\)$`)

// canonColor is a CSS colour as one comparable string, so that "#f00",
// "#ff0000", "rgb(255,0,0)" and "red" are the same colour, as colour reads
// them. Named colours beyond the basic ones are compared by name.
func canonColor(v string) string {
	v = strings.ToLower(strings.TrimSpace(v))
	if hex, ok := basicColors[v]; ok {
		return hex
	}
	if h, ok := strings.CutPrefix(v, "#"); ok && len(h) == 3 {
		return "#" + string([]byte{h[0], h[0], h[1], h[1], h[2], h[2]})
	}
	if m := reRGB.FindStringSubmatch(v); m != nil {
		channel := func(s string) int {
			if p, ok := strings.CutSuffix(s, "%"); ok {
				f, _ := strconv.ParseFloat(p, 64)
				return int(f*255/100 + 0.5)
			}
			f, _ := strconv.ParseFloat(s, 64)
			return int(f)
		}
		out := "#"
		for _, s := range m[1:4] {
			out += strconv.FormatInt(int64(0x100+min(channel(s), 255)), 16)[1:]
		}
		if m[4] != "" {
			if a, _ := strconv.ParseFloat(strings.TrimSuffix(m[4], "%"), 64); a < 1 {
				out += strconv.FormatFloat(a, 'f', 2, 64)
			}
		}
		return out
	}
	return v
}

func monoFamily(families string) bool {
	l := strings.ToLower(families)
	return strings.Contains(l, "mono") || strings.Contains(l, "courier") || strings.Contains(l, "consolas")
}

// cssMargin is a CSS margin as a number whose sign is what the converter
// needs: pixels as they are, other units scaled roughly to pixels, and auto,
// which counts as a margin, as one.
func cssMargin(v string) float64 {
	v = strings.ToLower(strings.TrimSpace(v))
	if v == "auto" {
		return 1
	}
	end := 0
	for end < len(v) && (v[end] == '.' || v[end] == '-' || v[end] == '+' || (v[end] >= '0' && v[end] <= '9')) {
		end++
	}
	f, err := strconv.ParseFloat(v[:end], 64)
	if err != nil {
		return 0
	}
	switch v[end:] {
	case "em", "rem":
		f *= 16
	case "pt":
		f *= 4.0 / 3
	}
	return f
}

// applyStyle applies the CSS declarations the converter reads.
// "!important" has no weight, so it is dropped.
func applyStyle(e *helem, style string) {
	for _, decl := range strings.Split(style, ";") {
		prop, value, ok := strings.Cut(decl, ":")
		if !ok {
			continue
		}
		prop = strings.ToLower(strings.TrimSpace(prop))
		value = strings.TrimSpace(strings.TrimSuffix(strings.TrimSpace(value), "!important"))
		lv := strings.ToLower(value)
		switch prop {
		case "font-weight":
			switch lv {
			case "bold", "bolder":
				e.f.bold = true
			case "normal", "lighter":
				e.f.bold = false
			default:
				if n, err := strconv.Atoi(lv); err == nil {
					e.f.bold = n >= 700
				}
			}
		case "font-style":
			e.f.italic = lv == "italic" || lv == "oblique"
		case "text-decoration", "text-decoration-line":
			e.f.strike = strings.Contains(lv, "line-through")
			for _, d := range []string{"underline", "overline"} {
				if strings.Contains(lv, d) {
					e.f.setExtra(d, "1")
				} else {
					e.f.setExtra(d, "")
				}
			}
		case "font-family":
			e.f.mono = monoFamily(value)
			e.f.setExtra("family", canonFamily(value))
		case "font":
			if monoFamily(value) {
				e.f.mono = true
			}
			for _, w := range strings.Fields(lv) {
				switch w {
				case "bold", "bolder":
					e.f.bold = true
				case "italic", "oblique":
					e.f.italic = true
				}
			}
			e.f.setExtra("font", lv)
		case "color", "background", "background-color":
			e.f.setExtra(prop, canonColor(value))
		case "font-size", "vertical-align", "font-variant", "text-transform", "letter-spacing", "word-spacing":
			e.f.setExtra(prop, lv)
		case "white-space":
			switch lv {
			case "pre":
				e.keepWS, e.nbsp, e.nonBreak = true, false, true
			case "pre-wrap":
				e.keepWS, e.nbsp, e.nonBreak = true, false, false
			case "nowrap":
				e.keepWS, e.nbsp, e.nonBreak = false, true, true
			case "normal", "pre-line":
				e.keepWS, e.nbsp, e.nonBreak = false, false, false
			}
		case "margin-left":
			e.ownL = cssMargin(value)
		case "margin-right":
			e.ownR = cssMargin(value)
		case "margin":
			parts := strings.Fields(value)
			switch len(parts) {
			case 1:
				e.ownL, e.ownR = cssMargin(parts[0]), cssMargin(parts[0])
			case 2, 3:
				e.ownL, e.ownR = cssMargin(parts[1]), cssMargin(parts[1])
			case 4:
				e.ownL, e.ownR = cssMargin(parts[3]), cssMargin(parts[1])
			}
		case "-qt-block-indent":
			if n, err := strconv.Atoi(lv); err == nil && n > 0 {
				e.blockInd = true
			}
		case "list-style-type", "list-style":
			for _, w := range strings.Fields(lv) {
				switch w {
				case "decimal", "lower-alpha", "upper-alpha", "lower-roman", "upper-roman",
					"lower-latin", "upper-latin":
					e.ordered = true
				case "disc", "circle", "square", "none":
					e.ordered = false
				}
			}
		}
	}
}

// closeUntil pops elements down to the nearest open one named in names,
// unless a boundary element comes first: the end tags HTML leaves implied.
func (c *hconv) closeUntil(names, boundary map[string]bool) {
	for i := len(c.stack) - 1; i > 0; i-- {
		n := c.stack[i].name
		if names[n] {
			c.popTo(i)
			return
		}
		if boundary[n] {
			return
		}
	}
}

var (
	pScope    = wordSet("table td th caption html")
	liScope   = wordSet("ul ol table td th")
	ddScope   = wordSet("dl table td th")
	cellScope = wordSet("tr table")
	rowScope  = wordSet("table")
)

// The inline elements that set a format. Any other element's text is plain.
func formatFor(name string, attrs map[string]string, e *helem) {
	switch name {
	case "b", "strong", "th":
		e.f.bold = true
	case "h1", "h2", "h3", "h4", "h5", "h6":
		e.f.bold = true
		e.f.setExtra("size", name)
		e.heading = int(name[1] - '0')
	case "i", "em", "cite", "var", "dfn", "address":
		e.f.italic = true
	case "s":
		e.f.strike = true
	case "u":
		e.f.setExtra("underline", "1")
	case "sub", "sup":
		e.f.setExtra("valign", name)
	case "big", "small":
		e.f.setExtra("size", name)
	case "code", "tt", "kbd", "samp":
		e.f.mono = true
		e.f.setExtra("family", "courier")
	case "nobr":
		e.nbsp = true
	case "pre":
		e.f.mono = true
		e.f.setExtra("family", "courier")
		e.keepWS, e.nbsp, e.nonBreak, e.skipNL = true, false, true, true
	case "a":
		if href, ok := attrs["href"]; ok {
			e.f.href = href
		}
		if n, ok := attrs["name"]; ok {
			e.f.setExtra("anchor", n)
		}
	case "font":
		if face, ok := attrs["face"]; ok {
			e.f.mono = monoFamily(face)
			e.f.setExtra("family", canonFamily(face))
		}
		if col, ok := attrs["color"]; ok {
			e.f.setExtra("color", canonColor(col))
		}
		if size, ok := attrs["size"]; ok {
			e.f.setExtra("size", size)
		}
	case "blockquote":
		e.ownL, e.ownR = 40, 40
	case "dd":
		e.ownL = 30
	case "ul", "ol":
		// The type attribute is read on either element: a numbering style
		// makes a numbered list, a bullet style a bulleted one.
		e.ordered = name == "ol"
		switch t := attrs["type"]; t {
		case "1", "a", "A", "i", "I":
			e.ordered = true
		default:
			switch strings.ToLower(t) {
			case "disc", "circle", "square":
				e.ordered = false
			}
		}
	}
}

func (c *hconv) start(name string, attrs map[string]string, selfClosing bool) {
	if pClosers[name] {
		c.closeUntil(wordSet("p"), pScope)
	}
	switch name {
	case "li":
		c.closeUntil(wordSet("li"), liScope)
	case "dt", "dd":
		c.closeUntil(wordSet("dt dd"), ddScope)
	case "td", "th":
		c.closeUntil(wordSet("td th"), cellScope)
	case "tr":
		c.closeUntil(wordSet("tr td th"), rowScope)
	case "thead", "tbody", "tfoot":
		c.closeUntil(wordSet("thead tbody tfoot tr td th"), rowScope)
	}

	parent := c.top()
	parent.hasChild = true
	e := *parent
	e.name, e.attrs, e.skipNL, e.hasChild, e.block = name, attrs, false, false, blockElems[name]
	e.ownL, e.ownR, e.chain = 0, 0, chainElems[name]
	// The element's own format, then the style sheets' rules for it, weakest
	// first, then its style attribute.
	formatFor(name, attrs, &e)
	for _, r := range matchingRules(c.rules, &e, c.stack[1:]) {
		applyStyle(&e, r.decls)
	}
	if style, ok := attrs["style"]; ok {
		applyStyle(&e, style)
	}
	e.sumL, e.sumR = e.ownL, e.ownR
	if parent.chain {
		e.sumL += parent.sumL
		e.sumR += parent.sumR
	}
	if !e.block && name != "pre" {
		// Whether a block's lines may break is the block's own format; an
		// inline element's white-space reaches only its spaces.
		e.nonBreak = parent.nonBreak
	}
	switch name {
	case "ul", "ol":
		e.listDepth = parent.listDepth + 1
		e.list = &hlist{ordered: e.ordered}
	case "caption":
		// A caption's text is kept, as a block before its table.
		e.inCell = true
	case "table":
		t := &htable{}
		if !parent.inCell || parent.table == nil {
			t.parent = parent.nodes
		}
		e.table, e.inCell = t, false
		c.closed = true
	case "tr", "thead", "tbody", "tfoot":
		if e.table != nil {
			e.table.attach()
			if name == "tr" {
				e.table.rows = append(e.table.rows, nil)
			}
		}
		c.closed = true
	case "td", "th":
		if e.table != nil && !parent.inCell {
			t := e.table
			t.attach()
			if len(t.rows) == 0 {
				t.rows = append(t.rows, nil)
			}
			cell := &hcell{colspan: spanAttr(attrs["colspan"]), rowspan: spanAttr(attrs["rowspan"])}
			t.rows[len(t.rows)-1] = append(t.rows[len(t.rows)-1], cell)
			e.nodes, e.inCell = &cell.nodes, true
			// A quote or a heading around the table does not reach into its
			// cells, and the margins of the blocks in a cell add up from the
			// cell.
			e.sumL, e.sumR, e.chain, e.blockInd, e.heading = 0, 0, false, false, 0
		}
	case "br":
		b := c.objectBlock(&e)
		b.frags = append(b.frags, hfrag{text: "\u2028", f: e.f})
	case "img":
		b := c.objectBlock(&e)
		b.frags = append(b.frags, hfrag{image: true, src: attrs["src"], f: e.f})
	case "hr":
		c.startBlock(&e)
		c.closed = true
	}
	if voidElems[name] || selfClosing {
		return
	}
	c.stack = append(c.stack, &e)
	if e.block {
		c.startBlock(&e)
	}
}

func spanAttr(v string) int {
	if n, err := strconv.Atoi(strings.TrimSpace(v)); err == nil && n > 1 {
		return n
	}
	return 1
}

// popTo closes the elements from the top of the stack down to index i. A
// closed block ends the cursor's block for the text after it: a <div> only
// when it had elements inside and the text does not end in a line break,
// every other block always.
func (c *hconv) popTo(i int) {
	closed := false
	for len(c.stack) > i {
		e := c.top()
		c.stack = c.stack[:len(c.stack)-1]
		switch {
		case e.name == "table":
			if e.table != nil {
				e.table.attach()
			}
			closed = true
		case tableElems[e.name]:
			closed = true
		case e.name == "div":
			if e.hasChild && c.cur != nil && !(len(c.blocks) == 1 && len(c.cur.frags) == 0) {
				lastLS := false
				if n := len(c.cur.frags); n > 0 && !c.cur.frags[n-1].image {
					r, _ := utf8.DecodeLastRuneInString(c.cur.frags[n-1].text)
					lastLS = r == '\u2028'
				}
				closed = closed || !lastLS
			}
		case e.block:
			closed = true
		}
	}
	if closed {
		c.closed, c.closedIn = true, c.top()
	}
}

func (c *hconv) end(name string) {
	for i := len(c.stack) - 1; i > 0; i-- {
		if c.stack[i].name == name {
			c.popTo(i)
			return
		}
	}
}

// numberItems gives each list item its place in its list, counting only the
// blocks that are items when the document is complete.
func (c *hconv) numberItems() {
	counts := map[*hlist]int{}
	for _, b := range c.blocks {
		if b.list != nil {
			b.item = counts[b.list]
			counts[b.list]++
		}
	}
}

// parseHTML reads HTML into the root frame's blocks and tables.
func parseHTML(src string) []hnode {
	c := &hconv{}
	rootElem := &helem{name: "#root"}
	rootElem.nodes = &c.root
	c.stack = []*helem{rootElem}
	i := 0
	for i < len(src) {
		lt := strings.IndexByte(src[i:], '<')
		if lt < 0 {
			c.text(src[i:], true)
			break
		}
		if lt > 0 {
			c.text(src[i:i+lt], false)
		}
		i += lt
		switch {
		case strings.HasPrefix(src[i:], "<!--"):
			if end := strings.Index(src[i+4:], "-->"); end >= 0 {
				i += 4 + end + 3
			} else {
				i = len(src)
			}
			continue
		case strings.HasPrefix(src[i:], "<!") || strings.HasPrefix(src[i:], "<?"):
			if end := strings.IndexByte(src[i:], '>'); end >= 0 {
				i += end + 1
			} else {
				i = len(src)
			}
			continue
		}
		name, attrs, selfClosing, closing, n := readTag(src[i:])
		if n == 0 {
			c.text("<", false)
			i++
			continue
		}
		i += n
		if closing {
			c.end(name)
			continue
		}
		if skipElems[name] && !selfClosing {
			closeTag := "</" + name
			if end := strings.Index(strings.ToLower(src[i:]), closeTag); end >= 0 {
				if name == "style" {
					c.rules = append(c.rules, parseStyleSheet(src[i:i+end], &c.order)...)
				}
				i += end
				if gt := strings.IndexByte(src[i:], '>'); gt >= 0 {
					i += gt + 1
				} else {
					i = len(src)
				}
			} else {
				i = len(src)
			}
			continue
		}
		c.start(name, attrs, selfClosing)
	}
	c.numberItems()
	return c.root
}

func isNameByte(b byte) bool {
	return b >= 'a' && b <= 'z' || b >= 'A' && b <= 'Z' || b >= '0' && b <= '9' || b == '-' || b == ':' || b == '_'
}

// readTag reads one tag at the start of s: its lower-case name, attributes
// with entities decoded, and how many bytes it took, or 0 when s does not
// start with a tag.
func readTag(s string) (name string, attrs map[string]string, selfClosing, closing bool, n int) {
	i := 1
	if i < len(s) && s[i] == '/' {
		closing = true
		i++
	}
	start := i
	if i >= len(s) || !(s[i] >= 'a' && s[i] <= 'z' || s[i] >= 'A' && s[i] <= 'Z') {
		return "", nil, false, false, 0
	}
	for i < len(s) && isNameByte(s[i]) {
		i++
	}
	name = strings.ToLower(s[start:i])
	attrs = map[string]string{}
	for i < len(s) {
		for i < len(s) && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f') {
			i++
		}
		if i >= len(s) {
			return name, attrs, selfClosing, closing, len(s)
		}
		if s[i] == '>' {
			return name, attrs, selfClosing, closing, i + 1
		}
		if s[i] == '/' {
			selfClosing = true
			i++
			continue
		}
		ks := i
		for i < len(s) && !strings.ContainsRune(" \t\n\r\f/>=", rune(s[i])) {
			i++
		}
		key := strings.ToLower(s[ks:i])
		if key == "" {
			// A stray "=": step over it.
			i++
			continue
		}
		for i < len(s) && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') {
			i++
		}
		value := ""
		if i < len(s) && s[i] == '=' {
			i++
			for i < len(s) && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') {
				i++
			}
			if i < len(s) && (s[i] == '"' || s[i] == '\'') {
				q := s[i]
				end := strings.IndexByte(s[i+1:], q)
				if end < 0 {
					value = s[i+1:]
					i = len(s)
				} else {
					value = s[i+1 : i+1+end]
					i += end + 2
				}
			} else {
				vs := i
				for i < len(s) && !strings.ContainsRune(" \t\n\r\f>", rune(s[i])) {
					i++
				}
				value = s[vs:i]
			}
		}
		if _, dup := attrs[key]; !dup {
			attrs[key] = html.UnescapeString(value)
		}
	}
	return name, attrs, selfClosing, closing, len(s)
}

// ---- writing Markdown ----

// escapeInline backslash-escapes the characters that would read back as
// inline Markdown: * _ ` [ ] and the backslash.
func escapeInline(text string) string {
	var sb strings.Builder
	for _, c := range text {
		if strings.ContainsRune("*_`[]\\", c) {
			sb.WriteByte('\\')
		}
		sb.WriteRune(c)
	}
	return sb.String()
}

// escapeBlockLeading escapes a first character that would start a heading,
// list item, quote or table row: # - + > |.
func escapeBlockLeading(text string) string {
	if text != "" && strings.ContainsRune("#-+>|", []rune(text)[0]) {
		return `\` + text
	}
	return text
}

func longestBacktickRun(text string) int {
	longest, run := 0, 0
	for _, c := range text {
		if c == '`' {
			run++
			longest = max(longest, run)
		} else {
			run = 0
		}
	}
	return longest
}

// inlineCodeSpan wraps text in a backtick run longer than any inside it,
// padded with a space when the text starts or ends with a backtick.
func inlineCodeSpan(text string) string {
	fence := strings.Repeat("`", longestBacktickRun(text)+1)
	if strings.HasPrefix(text, "`") || strings.HasSuffix(text, "`") {
		text = " " + text + " "
	}
	return fence + text + fence
}

// encodeLinkDestination percent-encodes the characters a [text](dest)
// destination cannot hold: ( ) space < >.
func encodeLinkDestination(href string) string {
	return strings.NewReplacer("(", "%28", ")", "%29", " ", "%20", "<", "%3C", ">", "%3E").Replace(href)
}

func fencedListing(text string) string {
	fence := strings.Repeat("`", max(3, longestBacktickRun(text)+1))
	return fence + "\n" + text + "\n" + fence
}

// blockIsPreformatted is a block that becomes a fenced listing: one from a
// <pre> (or white-space pre or nowrap), or one whose every run is monospace.
func blockIsPreformatted(b *hblock) bool {
	if b == nil || b.text() == "" {
		return false
	}
	if b.pre {
		return true
	}
	saw := false
	for _, f := range b.frags {
		if !f.image && f.text == "" {
			continue
		}
		saw = true
		if !f.f.mono {
			return false
		}
	}
	return saw
}

// fragments merges neighbouring runs of one format into one.
func fragments(b *hblock) []hfrag {
	var out []hfrag
	for _, f := range b.frags {
		if n := len(out); n > 0 && !f.image && !out[n-1].image && out[n-1].f == f.f {
			out[n-1].text += f.text
			continue
		}
		out = append(out, f)
	}
	return out
}

func inlineMarkdown(b *hblock, suppressBold bool) string {
	var sb strings.Builder
	for _, frag := range fragments(b) {
		if frag.image {
			if frag.src != "" {
				sb.WriteString("![](" + encodeLinkDestination(frag.src) + ")")
			}
			continue
		}
		text := strings.ReplaceAll(frag.text, "\uFFFC", "")
		if text == "" {
			continue
		}
		code := frag.f.mono
		if !code {
			text = escapeInline(text)
		}
		r := []rune(text)
		lead := 0
		for lead < len(r) && unicode.IsSpace(r[lead]) {
			lead++
		}
		trail := len(r)
		for trail > lead && unicode.IsSpace(r[trail-1]) {
			trail--
		}
		if lead >= trail {
			sb.WriteString(text)
			continue
		}
		body := string(r[lead:trail])
		if code {
			body = inlineCodeSpan(body)
		}
		if !suppressBold && frag.f.bold {
			body = "**" + body + "**"
		}
		if frag.f.italic {
			body = "*" + body + "*"
		}
		if frag.f.strike {
			body = "~~" + body + "~~"
		}
		if frag.f.href != "" {
			body = "[" + body + "](" + encodeLinkDestination(frag.f.href) + ")"
		}
		sb.WriteString(string(r[:lead]) + body + string(r[trail:]))
	}
	return sb.String()
}

func blockToMarkdown(b *hblock) string {
	if blockIsPreformatted(b) {
		return fencedListing(b.text())
	}
	content := trimSpace(inlineMarkdown(b, b.heading >= 1))
	if b.list != nil {
		if content == "" {
			return ""
		}
		pad := strings.Repeat("  ", max(0, b.indent-1))
		if b.list.ordered {
			return pad + strconv.Itoa(max(1, b.item+1)) + ". " + escapeBlockLeading(content)
		}
		return pad + "- " + escapeBlockLeading(content)
	}
	if content == "" {
		return ""
	}
	if b.heading >= 1 && b.heading <= 6 {
		return strings.Repeat("#", b.heading) + " " + content
	}
	if b.quote || b.inList {
		return "> " + escapeBlockLeading(content)
	}
	return escapeBlockLeading(content)
}

// tableMarkdown is a pipe table with the header rule after the first row.
// A cell covered by another's colspan or rowspan repeats that cell's text.
func tableMarkdown(t *htable) string {
	var grid [][]*hcell
	for r, row := range t.rows {
		for len(grid) <= r {
			grid = append(grid, nil)
		}
		col := 0
		for _, cell := range row {
			for col < len(grid[r]) && grid[r][col] != nil {
				col++
			}
			for dr := 0; dr < cell.rowspan && r+dr < len(t.rows); dr++ {
				for len(grid) <= r+dr {
					grid = append(grid, nil)
				}
				for dc := 0; dc < cell.colspan; dc++ {
					for len(grid[r+dr]) <= col+dc {
						grid[r+dr] = append(grid[r+dr], nil)
					}
					grid[r+dr][col+dc] = cell
				}
			}
			col += cell.colspan
		}
	}
	columns := 0
	for _, row := range grid {
		columns = max(columns, len(row))
	}
	var rows []string
	for r, row := range grid {
		cells := make([]string, columns)
		for c := 0; c < columns; c++ {
			if c >= len(row) || row[c] == nil {
				continue
			}
			var parts []string
			for _, n := range row[c].nodes {
				if n.block == nil {
					continue
				}
				if line := trimSpace(inlineMarkdown(n.block, r == 0)); line != "" {
					parts = append(parts, line)
				}
			}
			cells[c] = strings.ReplaceAll(strings.Join(parts, " "), "|", `\|`)
		}
		rows = append(rows, "| "+strings.Join(cells, " | ")+" |")
		if r == 0 {
			sep := make([]string, columns)
			for c := range sep {
				sep[c] = "---"
			}
			rows = append(rows, "| "+strings.Join(sep, " | ")+" |")
		}
	}
	return strings.Join(rows, "\n")
}

// frameMarkdown writes a frame's blocks and tables, one Markdown block each,
// separated by blank lines. A run of preformatted blocks is one listing, with
// blank blocks inside it kept when more of the listing follows.
func frameMarkdown(nodes []hnode) string {
	var parts []string
	for i := 0; i < len(nodes); i++ {
		n := nodes[i]
		if n.table != nil {
			if len(n.table.rows) > 0 {
				if md := tableMarkdown(n.table); md != "" {
					parts = append(parts, md)
				}
			}
			continue
		}
		if blockIsPreformatted(n.block) {
			run := []string{n.block.text()}
			lastCode := 0
			for p := i + 1; p < len(nodes) && nodes[p].table == nil; p++ {
				next := nodes[p].block
				blank := next.text() == ""
				if !blank && !blockIsPreformatted(next) {
					break
				}
				run = append(run, next.text())
				if !blank {
					lastCode = len(run) - 1
					i = p
				}
			}
			parts = append(parts, fencedListing(strings.Join(run[:lastCode+1], "\n")))
			continue
		}
		if line := blockToMarkdown(n.block); line != "" {
			parts = append(parts, line)
		}
	}
	return strings.Join(parts, "\n\n")
}

var reBlankRun = regexp.MustCompile(`\n{3,}`)

// HTMLToMarkdown converts HTML to Kvit's Markdown, blocks separated by a
// blank line, and returns "" for HTML with no text.
// Text that would read back as Markdown syntax is escaped, a code span or
// fence is made longer than any backtick run inside it, and a link or image
// address is percent-encoded where Markdown cannot hold it.
func HTMLToMarkdown(src string) string {
	if trimSpace(src) == "" {
		return ""
	}
	md := frameMarkdown(parseHTML(src))
	return trimSpace(reBlankRun.ReplaceAllString(md, "\n\n"))
}

var reStructure = regexp.MustCompile(`(?i)<\s*(h[1-6]|p|br|hr|ul|ol|li|blockquote|pre|code|a|img|table|tr|td|th|strong|b|em|i|del|s|strike|u)\b`)

// HasStructure reports whether HTML has any element worth converting, beyond
// a wrapper around plain text; without any, its plain text is better.
func HasStructure(src string) bool { return reStructure.MatchString(src) }
