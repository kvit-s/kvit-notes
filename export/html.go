package export

// HTML export, as src/application/documentexporter.cpp builds it for a
// browser: one self-contained page, the stylesheet inlined, images embedded
// as data: URIs, code coloured token by token, tables and task boards as
// static markup, display and inline maths left as TeX for MathJax, and a
// Mermaid diagram left as source for mermaid.js. Each block's markup is the
// toHtml of its Qt block kind (src/domain/blockkinds/).

import (
	"encoding/base64"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// Colors are the theme colours the stylesheet and the code colouring use,
// as CSS colours. An empty field takes the colour the Qt exporter uses when
// no theme is set.
type Colors struct {
	Text           string // body text (#222222)
	Muted          string // secondary text: quotes, captions, counts (#666666)
	Background     string // page background (#ffffff)
	Accent         string // links, a callout's bar (#2970c8)
	Border         string // rules, table and card borders (#dddddd)
	CodeBackground string // code and table headers (#f4f4f2)
	Danger         string // a query's error (#c1121f)
	Highlight      string // ==highlighted== text (#fdf3a9)
	CodeKeyword    string // (#a626a4)
	CodeType       string // types, built-ins, function names (#4078f2)
	CodeString     string // (#50a14f)
	CodeComment    string // (#a0a1a7)
	CodeNumber     string // (#986801)
}

func (c Colors) withDefaults() Colors {
	def := func(v, d string) string {
		if v == "" {
			return d
		}
		return v
	}
	return Colors{
		Text:           def(c.Text, "#222222"),
		Muted:          def(c.Muted, "#666666"),
		Background:     def(c.Background, "#ffffff"),
		Accent:         def(c.Accent, "#2970c8"),
		Border:         def(c.Border, "#dddddd"),
		CodeBackground: def(c.CodeBackground, "#f4f4f2"),
		Danger:         def(c.Danger, "#c1121f"),
		Highlight:      def(c.Highlight, "#fdf3a9"),
		CodeKeyword:    def(c.CodeKeyword, "#a626a4"),
		CodeType:       def(c.CodeType, "#4078f2"),
		CodeString:     def(c.CodeString, "#50a14f"),
		CodeComment:    def(c.CodeComment, "#a0a1a7"),
		CodeNumber:     def(c.CodeNumber, "#986801"),
	}
}

// Options are what an export needs to know beyond the note itself.
type Options struct {
	// Colors are the theme's colours; the zero value is the Qt exporter's
	// colours for no theme.
	Colors Colors
	// NoteDir is the folder of the note, where a relative image path is
	// looked for first. A vault export sets it for each note.
	NoteDir string
	// VaultRoot is the vault's folder, where a relative image path is looked
	// for next. A vault export sets it.
	VaultRoot string
	// SiteRoot is where an image path starting with "/" is looked for when
	// no file on the machine has that path (a website's own root, such as a
	// Hugo vault's static/ folder). Empty means VaultRoot.
	SiteRoot string
	// MaxAttachmentBytes is the largest image embedded in the page; a larger
	// one is left out and its path shown instead. 0 means 64 MiB, the Qt
	// app's budget, and a negative value means no limit.
	MaxAttachmentBytes int64
	// MaxCombinedChars is the largest combined file a vault export will
	// build, in UTF-16 code units as the Qt app counts them. 0 means 128 Mi,
	// the Qt app's budget, and a negative value means no limit.
	MaxCombinedChars int64
	// EmbedPreview, when set, gives the page title and description the
	// preview cache holds for a web address, for the card a web embed
	// exports as. It must not reach the network.
	EmbedPreview func(url string) (title, description string)
}

// The pinned MathJax and Mermaid script tags, exactly as the Qt exporter
// writes them. Each is written once, and only when the page needs it.
const (
	mathJaxScriptTag = "<script async id=\"MathJax-script\" " +
		"src=\"https://cdn.jsdelivr.net/npm/mathjax@3.2.2/es5/tex-svg.min.js\">" +
		"</script>\n"
	mermaidScriptTag = "<script type=\"module\">\n" +
		"  import mermaid from " +
		"'https://cdn.jsdelivr.net/npm/mermaid@11.16.0/dist/mermaid.esm.min.mjs';\n" +
		"  mermaid.initialize({\n" +
		"    startOnLoad: true,\n" +
		"    securityLevel: 'strict',\n" +
		"    htmlLabels: false,\n" +
		"    maxTextSize: 262144,\n" +
		"    maxEdges: 2000\n" +
		"  });\n" +
		"</script>\n"
)

// stylesheet is DocumentExporter::cssBlock with the theme's colours.
func stylesheet(c Colors) string {
	c = c.withDefaults()
	r := strings.NewReplacer(
		"%1", c.Text, "%2", c.Background, "%3", c.Accent, "%4", c.Muted,
		"%5", c.Border, "%6", c.CodeBackground, "%7", c.Danger, "%8", c.Highlight)
	return r.Replace("body{font-family:-apple-system,Segoe UI,Roboto,sans-serif;" +
		"font-size:15px;line-height:1.6;color:%1;background:%2;" +
		"max-width:760px;margin:24px auto;padding:0 16px}" +
		"h1,h2,h3,h4{line-height:1.25;margin:1.2em 0 .4em}" +
		"a{color:%3;text-decoration:none}a:hover{text-decoration:underline}" +
		"code{font-family:monospace;background:%6;padding:1px 4px;border-radius:3px}" +
		"pre{background:%6;padding:12px;border-radius:6px;overflow:auto}" +
		"pre code{background:none;padding:0}" +
		"blockquote{border-left:3px solid %5;margin:0;padding:2px 12px;color:%4}" +
		"table{border-collapse:collapse;margin:8px 0}" +
		"th,td{border:1px solid %5;padding:5px 9px}th{background:%6}" +
		"hr{border:none;border-top:1px solid %5;margin:1.5em 0}" +
		"img{max-width:100%}" +
		".callout{border:1px solid %5;border-left:4px solid %3;border-radius:6px;" +
		"padding:8px 12px;margin:10px 0}" +
		".callout .title{font-weight:bold;margin-bottom:4px}" +
		".kanban{display:flex;gap:12px;align-items:flex-start;flex-wrap:wrap}" +
		".kanban .col{border:1px solid %5;" +
		"border-radius:6px;padding:8px;min-width:140px}" +
		".kanban .col .count{color:%4;font-weight:normal;font-size:12px}" +
		".kanban .card{border:1px solid %5;border-radius:4px;padding:4px 6px;" +
		"margin:5px 0}" +
		".kanban .card .title{font-weight:bold}" +
		".kanban .card .meta{color:%4;font-size:12px}" +
		".kanban .card .chip{border:1px solid %5;border-radius:8px;" +
		"padding:0 6px;margin-right:4px;font-size:11px;color:%4}" +
		".query{border:1px solid %5;border-radius:6px;padding:8px 10px;" +
		"margin:10px 0}" +
		".query .head{font-size:11px;font-weight:bold;color:%4;" +
		"margin-bottom:6px}" +
		".query .head .count{font-weight:normal}" +
		".query .error{color:%7}" +
		".query table{margin:0}" +
		".embed{border:1px solid %5;border-radius:6px;padding:8px 12px;" +
		"margin:10px 0}" +
		".embed .title{font-weight:bold}" +
		".embed .desc{color:%4;font-size:13px;margin-top:2px}" +
		".embed .host{color:%4;font-size:12px;margin-top:4px}" +
		"mark{background:%8;color:%1}" +
		".math-display{text-align:center;margin:1em 0}" +
		"td code,th code{white-space:pre-wrap}" +
		"pre.text-diagram{line-height:1.2}" +
		"pre.text-diagram code{white-space:pre;font-family:" +
		"'Cascadia Code',Consolas,'DejaVu Sans Mono',monospace}" +
		"pre.mermaid{background:none;padding:0;text-align:center}" +
		".diagram-source{margin:2px 0 10px;color:%4;font-size:13px}" +
		".diagram-source pre{margin-top:4px}" +
		"figure{margin:1em 0;text-align:center}" +
		"figcaption{color:%4;font-size:13px;font-style:italic;margin-top:4px}" +
		".dropcap{float:left;line-height:0.82;font-weight:bold;" +
		"padding:0.06em 0.08em 0 0}" +
		"img.bordered{border:1px solid %5}" +
		".hr-deco{display:flex;align-items:center;gap:10px;margin:1.5em 0}" +
		".hr-deco hr{flex:1;margin:0}")
}

// wrapPage is DocumentExporter::wrapHtmlDocument: the page around a body,
// with each script tag only when the body needs it.
func wrapPage(body, title string, c Colors, sawMath, sawMermaid bool) string {
	if title == "" {
		title = "Kvit Export"
	}
	var scripts string
	if sawMath {
		scripts += mathJaxScriptTag
	}
	if sawMermaid {
		scripts += mermaidScriptTag
	}
	return "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n<title>" + esc(title) +
		"</title>\n<style>" + stylesheet(c) + "</style>\n" + scripts + "</head>\n<body>\n" +
		body + "\n</body></html>\n"
}

// headingSlugs are the anchors headings get, one entry per block and "" for
// every other block: the heading's text made into a slug, with "-1", "-2"
// and so on after a repeat (DocumentExporter::headingSlugs).
func headingSlugs(blocks []block) []string {
	slugs := make([]string, len(blocks))
	counts := map[string]int{}
	for i, b := range blocks {
		if b.kind != kHeading {
			continue
		}
		base := baseSlug(displayText(b.text))
		seen := counts[base]
		counts[base] = seen + 1
		if seen == 0 {
			slugs[i] = base
		} else {
			slugs[i] = base + "-" + strconv.Itoa(seen)
		}
	}
	return slugs
}

// baseSlug is DocumentOutline::baseSlug: lower case, letters and digits kept,
// spaces, underscores and hyphens made one hyphen, the rest dropped.
func baseSlug(text string) string {
	var sb strings.Builder
	pending := false
	for _, c := range text {
		switch {
		case isLetterOrNumber(c):
			if pending && sb.Len() > 0 {
				sb.WriteByte('-')
			}
			pending = false
			sb.WriteString(strings.ToLower(string(c)))
		case isSpace(c) || c == '_' || c == '-':
			pending = true
		}
	}
	return sb.String()
}

// renderer holds one render: the options, and the whole document, which the
// table of contents reads even when only some blocks are written.
type renderer struct {
	opt        Options
	colors     Colors
	doc        []block
	docSlugs   []string
	sawMath    bool
	sawMermaid bool
}

func newRenderer(doc []block, opt Options) *renderer {
	return &renderer{opt: opt, colors: opt.Colors.withDefaults(), doc: doc, docSlugs: headingSlugs(doc)}
}

// body is DocumentExporter::buildHtmlBody. A run of list items of one
// flavour (numbered, or bullets and to-dos together) is one list, and an item
// deeper than the one before opens its sublist inside the still-open <li>.
func (r *renderer) body(blocks []block, slugs []string) string {
	var sb strings.Builder
	for i := 0; i < len(blocks); {
		b := blocks[i]
		if b.kind.isList() {
			ordered := b.kind == kNumbered
			openTag, closeTag := "<ul>", "</ul>"
			if ordered {
				openTag, closeTag = "<ol>", "</ol>"
			}
			depth := 0
			for i < len(blocks) && blocks[i].kind.isList() && (blocks[i].kind == kNumbered) == ordered {
				item := blocks[i]
				target := max(0, item.indent) + 1
				if depth == 0 || target > depth {
					for depth < target {
						sb.WriteString(openTag)
						depth++
					}
				} else {
					sb.WriteString("</li>")
					for depth > target {
						sb.WriteString(closeTag + "</li>")
						depth--
					}
				}
				sb.WriteString("<li>" + r.itemHTML(item))
				i++
			}
			if depth > 0 {
				sb.WriteString("</li>")
				for depth > 0 {
					sb.WriteString(closeTag)
					depth--
					if depth > 0 {
						sb.WriteString("</li>")
					}
				}
			}
			continue
		}
		sb.WriteString(r.blockHTML(b, slugs[i]))
		i++
	}
	return sb.String()
}

func (r *renderer) inline(md string) string { return inlineHTML(md, &r.sawMath) }

// itemHTML is a list item's inner markup, without its <li>.
func (r *renderer) itemHTML(b block) string {
	if b.kind == kTodo {
		box := "&#9744; "
		if b.checked {
			box = "&#9745; "
		}
		return box + r.inline(b.text)
	}
	return r.inline(b.text)
}

func (r *renderer) blockHTML(b block, slug string) string {
	a := parseAttrs(b.attrs)
	switch b.kind {
	case kParagraph:
		if b.text == "" {
			return ""
		}
		var decls []string
		if al := textAlign(a, "left"); al != "" {
			decls = append(decls, al)
		}
		return "<p" + styleAttr(decls) + ">" + withDropCap(r.inline(b.text), a) + "</p>"
	case kHeading:
		var decls []string
		if al := textAlign(a, "left"); al != "" {
			decls = append(decls, al)
		}
		tag := "h" + strconv.Itoa(b.level)
		return "<" + tag + ` id="` + esc(slug) + `"` + styleAttr(decls) + ">" + r.inline(b.text) + "</" + tag + ">"
	case kQuote:
		return "<blockquote>" + r.inline(b.text) + "</blockquote>"
	case kCallout:
		heading := b.title
		if heading == "" {
			heading = b.lang
		}
		accent := cssColor(attrStr(a, "color", ""))
		var panelStyle, titleStyle string
		if accent != "" {
			panelStyle = ` style="border-left-color:` + accent + `"`
			titleStyle = ` style="color:` + accent + `"`
		}
		return `<div class="callout"` + panelStyle + `><div class="title"` + titleStyle + ">" +
			esc(heading) + "</div>" + r.inline(b.text) + "</div>"
	case kCode:
		switch b.lang {
		case "diagram", "text-diagram", "ascii-diagram":
			return `<pre class="text-diagram"><code>` + esc(b.text) + "</code></pre>"
		}
		return "<pre><code>" + r.highlighted(b.lang, b.text) + "</code></pre>"
	case kMermaid:
		r.sawMermaid = true
		return `<pre class="mermaid">` + esc(b.text) + "</pre>" +
			`<details class="diagram-source"><summary>Diagram source</summary><pre><code>` +
			esc(b.text) + "</code></pre></details>"
	case kToc:
		return tocHTML(r.doc, r.docSlugs)
	case kKanban:
		return r.boardHTML(parseBoard(b.text))
	case kQuery:
		// A query is answered against the open vault; this package has none
		// to ask, so the spec is written as its source, which is what the Qt
		// exporter writes when no vault is open.
		return "<pre><code>" + esc(b.text) + "</code></pre>"
	case kDivider:
		return dividerHTML(a)
	case kMath:
		r.sawMath = true
		return `<p class="math-display">\[ ` + esc(b.text) + ` \]</p>`
	case kTable:
		return r.tableHTML(parseTable(b.text), a)
	case kImage:
		p := parseImageLine(b.text)
		if p.valid && isEmbedURL(p.path) {
			return r.embedHTML(p, a)
		}
		return r.imageHTML(p, a)
	case kMedia:
		return mediaLinkHTML(parseImageLine(b.text))
	}
	return ""
}

// highlighted is a code block's text with one coloured span per token.
func (r *renderer) highlighted(lang, source string) string {
	src := []rune(source)
	var sb strings.Builder
	pos := 0
	for _, s := range highlightSpans(lang, source) {
		if s.start > pos {
			sb.WriteString(esc(string(src[pos:s.start])))
		}
		piece := esc(string(src[s.start : s.start+s.length]))
		if color := r.tokenColor(s.tok); color != "" {
			sb.WriteString(`<span style="color:` + color + `">` + piece + "</span>")
		} else {
			sb.WriteString(piece)
		}
		pos = s.start + s.length
	}
	if pos < len(src) {
		sb.WriteString(esc(string(src[pos:])))
	}
	return sb.String()
}

func (r *renderer) tokenColor(t token) string {
	switch t {
	case tokKeyword:
		return r.colors.CodeKeyword
	case tokType:
		return r.colors.CodeType
	case tokString:
		return r.colors.CodeString
	case tokComment:
		return r.colors.CodeComment
	case tokNumber:
		return r.colors.CodeNumber
	}
	return ""
}

func dividerHTML(a map[string]string) string {
	style := attrStr(a, "style", "")
	color := cssColor(attrStr(a, "color", ""))
	width := attrStr(a, "width", "")
	var decls []string
	if attrHas(a, "thickness") {
		decls = append(decls, "border-top-width:"+strconv.Itoa(min(max(attrNum(a, "thickness", 2), 1), 12))+"px")
	}
	if style == "dashed" || style == "dotted" {
		decls = append(decls, "border-top-style:"+style)
	}
	if color != "" {
		decls = append(decls, "border-top-color:"+color)
	}
	if percent, err := strconv.Atoi(trimSpace(strings.TrimSuffix(width, "%"))); err == nil && percent > 0 && percent < 100 {
		decls = append(decls, "width:"+strconv.Itoa(percent)+"%", "margin-left:auto", "margin-right:auto")
	}
	if style == "decorative" {
		rule := styleAttr(decls)
		dia := ""
		if color != "" {
			dia = ` style="color:` + color + `"`
		}
		return `<div class="hr-deco"><hr` + rule + `><span class="dia"` + dia + `>&#9670;</span><hr` + rule + "></div>"
	}
	return "<hr" + styleAttr(decls) + ">"
}

func (r *renderer) tableHTML(t table, a map[string]string) string {
	cell := func(s string) string { return strings.ReplaceAll(r.inline(s), "\n", "<br>") }
	var sb strings.Builder
	sb.WriteString("<table>")
	if cols := strings.FieldsFunc(attrStr(a, "cols", ""), func(c rune) bool { return c == ',' }); len(cols) > 0 {
		var group strings.Builder
		anyWidth := false
		for _, raw := range cols {
			if w, err := strconv.Atoi(trimSpace(raw)); err == nil && w > 0 {
				anyWidth = true
				group.WriteString(`<col style="width:` + strconv.Itoa(w) + `px">`)
			} else {
				group.WriteString("<col>")
			}
		}
		if anyWidth {
			sb.WriteString("<colgroup>" + group.String() + "</colgroup>")
		}
	}
	if len(t.headers) > 0 {
		sb.WriteString("<tr>")
		for _, h := range t.headers {
			sb.WriteString("<th>" + cell(h) + "</th>")
		}
		sb.WriteString("</tr>")
	}
	for _, row := range t.rows {
		sb.WriteString("<tr>")
		for _, c := range row {
			sb.WriteString("<td>" + cell(c) + "</td>")
		}
		sb.WriteString("</tr>")
	}
	sb.WriteString("</table>")
	return sb.String()
}

func (r *renderer) boardHTML(b board) string {
	var sb strings.Builder
	sb.WriteString(`<div class="kanban">`)
	for _, col := range b.columns {
		sb.WriteString(`<div class="col"><strong>` + esc(col.name) + `</strong> <span class="count">` +
			strconv.Itoa(len(col.cards)) + "</span>")
		for _, c := range col.cards {
			box := "&#9744; "
			if c.done {
				box = "&#9745; "
			}
			sb.WriteString(`<div class="card"><div class="title">` + box + r.inline(c.title) + "</div>")
			if c.description != "" {
				sb.WriteString(`<div class="meta">` + r.inline(c.description) + "</div>")
			}
			if len(c.labels) > 0 || c.due != "" {
				sb.WriteString(`<div class="meta">`)
				for _, l := range c.labels {
					sb.WriteString(`<span class="chip">` + esc(l) + "</span>")
				}
				if c.due != "" {
					sb.WriteString(`<span class="chip">&#128197; ` + esc(c.due) + "</span>")
				}
				sb.WriteString("</div>")
			}
			sb.WriteString("</div>")
		}
		sb.WriteString("</div>")
	}
	sb.WriteString("</div>")
	return sb.String()
}

// tocHTML is the table of contents as a list of links to the document's
// headings, indented by level from the shallowest one present.
func tocHTML(doc []block, slugs []string) string {
	minLevel := 4
	for _, b := range doc {
		if l := b.headingLevel(); l > 0 {
			minLevel = min(minLevel, l)
		}
	}
	var sb strings.Builder
	sb.WriteString(`<ul class="toc">`)
	for i, b := range doc {
		level := b.headingLevel()
		if level == 0 {
			continue
		}
		slug := ""
		if i < len(slugs) {
			slug = slugs[i]
		}
		sb.WriteString(`<li style="margin-left:` + strconv.Itoa((level-minLevel)*16) + `px"><a href="#` +
			esc(slug) + `">` + esc(displayText(b.text)) + "</a></li>")
	}
	sb.WriteString("</ul>")
	return sb.String()
}

func (r *renderer) imageHTML(p imageExpr, a map[string]string) string {
	uri := r.imageDataURI(p.path)
	var decls []string
	if attrHas(a, "rounded") {
		radius := attrNum(a, "rounded", 0)
		if radius <= 0 {
			radius = 12
		}
		decls = append(decls, "border-radius:"+strconv.Itoa(radius)+"px")
	}
	if attrHas(a, "shadow") {
		decls = append(decls, "box-shadow:0 2px 10px rgba(0,0,0,0.25)")
	}
	var class string
	if attrHas(a, "border") {
		if custom := cssColor(attrStr(a, "border", "")); custom == "" {
			class = ` class="bordered"`
		} else {
			decls = append(decls, "border:1px solid "+custom)
		}
	}
	var img string
	if uri == "" {
		img = "<em>[image: " + esc(p.path) + "]</em>"
	} else {
		img = `<img alt="` + esc(p.alt) + `" src="` + uri + `"`
		if p.width > 0 {
			img += ` width="` + strconv.Itoa(p.width) + `"`
		}
		img += class + styleAttr(decls) + ">"
	}
	var fig []string
	if al := textAlign(a, "center"); al != "" {
		fig = append(fig, al)
	}
	out := "<figure" + styleAttr(fig) + ">" + img
	if p.caption != "" {
		out += "<figcaption>" + escFlowing(p.caption) + "</figcaption>"
	}
	return out + "</figure>"
}

// imageDataURI is the src an image exports with: a web address as it is, a
// local file as a data: URI, or "" when the path names no file or the file
// is over the attachment budget (DocumentExporter::dataUriForImagePath).
func (r *renderer) imageDataURI(stored string) string {
	resolved := resolveSource(stored, r.opt.NoteDir, r.opt.VaultRoot, r.opt.SiteRoot)
	if resolved == "" {
		return ""
	}
	if strings.HasPrefix(resolved, "http") {
		return resolved
	}
	if !filepath.IsAbs(resolved) {
		// A data: address resolves to itself and is not a file, so the Qt
		// exporter shows the placeholder for it.
		return ""
	}
	info, err := os.Stat(resolved)
	if err != nil {
		return ""
	}
	limit := r.opt.MaxAttachmentBytes
	if limit == 0 {
		limit = 64 << 20
	}
	if limit > 0 && info.Size() > limit {
		return ""
	}
	data, err := os.ReadFile(resolved)
	if err != nil {
		return ""
	}
	mime := "image/png"
	lower := strings.ToLower(resolved)
	switch {
	case strings.HasSuffix(lower, ".jpg"), strings.HasSuffix(lower, ".jpeg"):
		mime = "image/jpeg"
	case strings.HasSuffix(lower, ".gif"):
		mime = "image/gif"
	case strings.HasSuffix(lower, ".svg"):
		mime = "image/svg+xml"
	case strings.HasSuffix(lower, ".webp"):
		mime = "image/webp"
	}
	return "data:" + mime + ";base64," + base64.StdEncoding.EncodeToString(data)
}

// resolveSource is ImageAssets::resolveSource: a web or data: address as it
// is, else the first file that exists of the path under the note's folder,
// under the vault, as given, and, for a path starting with "/", under the
// site root. The answer for a file is its absolute path.
func resolveSource(stored, noteDir, root, siteRoot string) string {
	if stored == "" {
		return ""
	}
	if isRemote(stored) || strings.HasPrefix(stored, "data:") {
		return stored
	}
	var candidates []string
	under := func(dir string) string {
		if filepath.IsAbs(stored) {
			return stored
		}
		return filepath.Join(dir, stored)
	}
	if noteDir != "" {
		candidates = append(candidates, under(noteDir))
	}
	if root != "" {
		candidates = append(candidates, under(root))
	}
	if abs, err := filepath.Abs(stored); err == nil {
		candidates = append(candidates, abs)
	}
	site := siteRoot
	if site == "" {
		site = root
	}
	if rel, ok := strings.CutPrefix(stored, "/"); ok && site != "" && rel != "" && !strings.HasPrefix(rel, "/") {
		candidates = append(candidates, filepath.Join(site, rel))
	}
	for _, c := range candidates {
		if info, err := os.Stat(c); err == nil && info.Mode().IsRegular() {
			if abs, err := filepath.Abs(c); err == nil {
				return abs
			}
			return c
		}
	}
	return ""
}

// mediaLinkHTML is an audio or video file as a link line: an export has no
// player (mediaLinkHtml in src/domain/blockkinds/mediakinds.cpp).
func mediaLinkHTML(p imageExpr) string {
	label := p.alt
	if label == "" {
		label = p.path
	}
	href := safeHref(p.path)
	if href == "" {
		return "<p>&#9654; " + esc(label) + "</p>"
	}
	return `<p>&#9654; <a href="` + esc(href) + `">` + esc(label) + "</a></p>"
}

// embedHTML is a web page's preview card as a titled link, with whatever the
// preview cache knows about the page (DocumentExporter::embedCardHtml).
func (r *renderer) embedHTML(p imageExpr, a map[string]string) string {
	var pageTitle, description string
	if r.opt.EmbedPreview != nil {
		pageTitle, description = r.opt.EmbedPreview(p.path)
	}
	label := p.alt
	if label == "" {
		label = pageTitle
	}
	if label == "" {
		label = p.path
	}
	host := urlHost(p.path)
	var decls []string
	if w := attrNum(a, "width", 0); w > 0 {
		decls = append(decls, "max-width:"+strconv.Itoa(w)+"px")
	}
	title := esc(label)
	if href := safeHref(p.path); href != "" {
		title = `<a href="` + esc(href) + `">` + esc(label) + "</a>"
	}
	out := `<div class="embed"` + styleAttr(decls) + `><div class="title">` + title + "</div>"
	if description != "" {
		out += `<div class="desc">` + esc(description) + "</div>"
	}
	if p.caption != "" {
		out += `<div class="desc">` + escFlowing(p.caption) + "</div>"
	}
	if host != "" && label != p.path {
		out += `<div class="host">` + esc(host) + "</div>"
	}
	return out + "</div>"
}
