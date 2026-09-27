package export

import (
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/kvit-s/kvit-notes/editor"
)

// htmlCases are Kvit's exporter tests (tests/test_documentexporter.cpp) that
// render one Markdown body to HTML, one row per input, named after the Qt
// test. want must each appear in the page; not must not.
var htmlCases = []struct {
	name string
	md   string
	want []string
	not  []string
}{
	{"testHtmlWrapper", "Hello", []string{"<!DOCTYPE html>", "<style>", "<title>My Title</title>", "<p>Hello</p>"}, nil},
	{"testHeadingCarriesSlugAnchor", "# Getting Started", []string{`<h1 id="getting-started">Getting Started</h1>`}, nil},
	{"testInlineBoldItalicLink", "A **bold** and *italic* and [link](http://x).",
		[]string{"<strong>bold</strong>", "<em>italic</em>", `<a href="http://x">link</a>`}, nil},
	{"testExportedHrefsCarryOnlyNavigationalSchemes/javascript", "[click me](javascript:danger)",
		[]string{"click me"}, []string{`href="javascript:`}},
	{"testExportedHrefsCarryOnlyNavigationalSchemes/data", "[x](data:text/html,<script>danger</script>)",
		nil, []string{`href="data:`}},
	{"testExportedHrefsCarryOnlyNavigationalSchemes/mixed case", "[x](JaVaScRiPt:danger)",
		nil, []string{`href="JaVaScRiPt:`}},
	{"testExportedHrefsCarryOnlyNavigationalSchemes/vbscript", "[x](vbscript:danger)",
		nil, []string{`href="vbscript:`}},
	{"testExportedHrefsCarryOnlyNavigationalSchemes/https", "[a](https://example.com/p)",
		[]string{`<a href="https://example.com/p">`}, nil},
	{"testExportedHrefsCarryOnlyNavigationalSchemes/mailto", "[a](mailto:someone@example.com)",
		[]string{`<a href="mailto:someone@example.com">`}, nil},
	{"testExportedHrefsCarryOnlyNavigationalSchemes/relative", "[a](images/diagram.png)",
		[]string{`<a href="images/diagram.png">`}, nil},
	{"testExportedHrefsCarryOnlyNavigationalSchemes/fragment", "[a](#a-heading)",
		[]string{`<a href="#a-heading">`}, nil},
	{"testEscapedPunctuationExportsBare", `2 \* 3 \* 4`, []string{"2 * 3 * 4"}, []string{`\*`}},
	{"testBulletAndNumberedLists/bullets", "- one\n- two", []string{"<ul><li>one</li><li>two</li></ul>"}, nil},
	{"testBulletAndNumberedLists/numbered", "1. a\n2. b", []string{"<ol><li>a</li><li>b</li></ol>"}, nil},
	{"testTodoCheckboxes", "- [ ] open\n- [x] done", []string{"&#9744; open", "&#9745; done"}, nil},
	{"testQuote", "> quoted", []string{"<blockquote>quoted</blockquote>"}, nil},
	{"testLineBreaksExportAsBreaks/paragraph", "wrapped\nline", []string{"<p>wrapped<br>line</p>"}, nil},
	{"testLineBreaksExportAsBreaks/item", "- one\n  two", []string{"<li>one<br>two</li>"}, nil},
	{"testLineBreaksExportAsBreaks/quote", "> a\n> b", []string{"<blockquote>a<br>b</blockquote>"}, nil},
	{"testLineBreaksExportAsBreaks/bold", "**bold**\nafter", []string{"<strong>bold</strong><br>after"}, nil},
	{"testLineBreaksExportAsBreaks/code", "```\nfirst\nsecond\n```", []string{"first\nsecond"}, []string{"first<br>second"}},
	{"testDivider", "---", []string{"<hr>"}, nil},
	{"testCodeBlockHighlighted", "```python\nreturn 1\n```", []string{"<pre><code>", `<span style="color:`, "return"}, nil},
	{"testCharacterDiagramExports", "```diagram\n┌────┐\n│ <a>│\n└────┘\n```",
		[]string{`<pre class="text-diagram">`, "&lt;a&gt;"}, []string{"<a>"}},
	{"testMermaidHtmlExport", "```mermaid\nflowchart LR\n  A[<x>] --> B\n```",
		[]string{`<pre class="mermaid">`, `<details class="diagram-source">`, "&lt;x&gt;",
			"mermaid@11.16.0/dist/mermaid.esm.min.mjs", "securityLevel: 'strict'", "htmlLabels: false"},
		[]string{"<x>"}},
	{"testMermaidScriptOnlyWithMermaid/none", "# Title\n\nJust prose.", nil,
		[]string{"mermaid.esm.min.mjs", `<pre class="mermaid">`}},
	{"testTable", "| A | B |\n| --- | --- |\n| 1 | 2 |", []string{"<table>", "<th>A</th>", "<td>1</td>"}, nil},
	{"testCallout", "> [!info] Heads up\n> body", []string{`class="callout"`, "Heads up", "body"}, nil},
	{"testTocFenceBecomesAnchorList", "# Intro\n\n```toc\n```\n\n## Details",
		[]string{`class="toc"`, `<a href="#intro">Intro</a>`, `<a href="#details">Details</a>`}, nil},
	{"testInternalLinkAnchor", "See [risks](#risks).", []string{`<a href="#risks">risks</a>`}, nil},
	{"testDisplayMathEmitsMathJaxDelimiters", "$$\na & b < c > d\n$$",
		[]string{`<p class="math-display">\[ a &amp; b &lt; c &gt; d \]</p>`}, []string{"data:image/"}},
	{"testInlineMathEmitsMathJaxDelimiters", "The square $x^2$ grows fast.", []string{`\(x^2\)`}, []string{"$x^2$"}},
	{"testMathJaxScriptTagInjectedOnlyWithMath/with", "Inline $a+b$ math.\n\n$$\nE = mc^2\n$$",
		[]string{"https://cdn.jsdelivr.net/npm/mathjax@3.2.2/es5/tex-svg.min.js"}, nil},
	{"testMathJaxScriptTagInjectedOnlyWithMath/without", "Just prose.", nil, []string{"MathJax", "<script"}},
	{"testLiteralDollarsStayLiteral", "It costs $5 and $6 total.", []string{"It costs $5 and $6 total."},
		[]string{`\(`, "MathJax"}},
	{"testWriteHtmlFile", "# Hi", []string{`<h1 id="hi">Hi</h1>`}, nil},
	{"testNestedListsNestInHtml", "- one\n  - nested\n  - also nested\n- two",
		[]string{"<ul><li>one<ul><li>nested</li><li>also nested</li></ul></li><li>two</li></ul>"}, nil},
	{"testNestedNumberedAndTodoListsNest/ordered", "1. a\n  1. deep\n2. b",
		[]string{"<ol><li>a<ol><li>deep</li></ol></li><li>b</li></ol>"}, nil},
	{"testNestedNumberedAndTodoListsNest/todo", "- [ ] top\n  - [x] child",
		[]string{"&#9744; top<ul><li>&#9745; child</li></ul>"}, nil},
	{"testQueryFenceWithoutACollectionKeepsItsSource", "```query\nfrom: Projects/\nwhere: status = active\n```",
		[]string{"where: status = active"}, []string{`class="query"`}},
	{"testEmbedUrlExportsAsALinkNotAnImage", "![](https://example.com/wiki)",
		[]string{`class="embed"`, `<a href="https://example.com/wiki">`, "example.com"}, []string{"<img"}},
	{"testImageUrlStillExportsAsAnImage", "![cat](https://example.com/cat.png)",
		[]string{"<img", "https://example.com/cat.png"}, []string{`class="embed"`}},
	{"testKanbanCardsCarryLabelsDueDatesAndDescriptions",
		"```kanban\n## To do\n- [ ] Ship the beta #release 📅 2026-08-01\n" +
			"  Needs the installer signed first\n## Done\n- [x] Write the notes\n```",
		[]string{"Ship the beta", `class="chip">release</span>`, `class="chip">&#128197; 2026-08-01</span>`,
			"Needs the installer signed first", "<strong>To do</strong>", "&#9745;"},
		[]string{"Ship the beta 📅"}},
	{"testImagesAreCappedToThePageWidth", "text", []string{"img{max-width:100%}"}, []string{"100%%"}},
	{"testParagraphAlignmentReachesTheExport/center", "centred  <!--kvit align=center-->",
		[]string{`<p style="text-align:center">centred</p>`}, nil},
	{"testParagraphAlignmentReachesTheExport/right", "right  <!--kvit align=right-->",
		[]string{`<p style="text-align:right">right</p>`}, nil},
	{"testHeadingAlignmentReachesTheExport", "# Title  <!--kvit align=center-->",
		[]string{`<h1 id="title" style="text-align:center">Title</h1>`}, nil},
	{"testDropCapCapsTheFirstRenderedCharacter/plain", "Once upon a time  <!--kvit dropcap=3-->",
		[]string{`<span class="dropcap" style="font-size:3.45em">O</span>nce upon a time`}, nil},
	{"testDropCapCapsTheFirstRenderedCharacter/bold", "**Bold** opening  <!--kvit dropcap=3-->",
		[]string{`<strong><span class="dropcap"`, ">B</span>old</strong>"}, nil},
	{"testDropCapCapsTheFirstRenderedCharacter/off", "Small  <!--kvit dropcap=1-->", nil, []string{`<span class="dropcap"`}},
	{"testDropCapCapsTheFirstRenderedCharacter/styled", "Fancy  <!--kvit dropcap=4 dropcapcolor=#c1121f dropcapfont=Georgia-->",
		[]string{"color:#c1121f", "font-family:'Georgia'"}, nil},
	{"testDividerStyleReachesTheExport/dashed", "---  <!--kvit style=dashed thickness=4-->",
		[]string{"border-top-width:4px", "border-top-style:dashed"}, nil},
	{"testDividerStyleReachesTheExport/half", "---  <!--kvit width=50%-->", []string{"width:50%", "margin-left:auto"}, nil},
	{"testDividerStyleReachesTheExport/decorative", "---  <!--kvit style=decorative-->",
		[]string{`class="hr-deco"`, "&#9670;"}, nil},
	{"testUnstyledBlocksExportWithoutStyleAttributes",
		"Plain paragraph\n\n# Plain heading\n\n---\n\n| A | B |\n| --- | --- |\n| 1 | 2 |",
		[]string{"<p>Plain paragraph</p>", `<h1 id="plain-heading">Plain heading</h1>`, "<hr>", "<table><tr><th>A</th>"},
		[]string{"<colgroup>"}},
	{"testCalloutColourOverrideReachesTheExport", "> [!info] Heads up  <!--kvit color=#2970c8-->\n> body",
		[]string{"border-left-color:#2970c8", `<div class="title" style="color:#2970c8">`}, nil},
	{"testTableColumnWidthsReachTheExport", "| A | B |  <!--kvit cols=120,0-->\n| --- | --- |\n| 1 | 2 |",
		[]string{`<colgroup><col style="width:120px"><col></colgroup>`}, nil},
	{"testAttributeColoursThatAreNotColoursAreDropped/injection", "---  <!--kvit color=red;background:url(http://x)-->",
		nil, []string{"background:url", "border-top-color"}},
	{"testAttributeColoursThatAreNotColoursAreDropped/quoted", "> [!info] T  <!--kvit color=\"#fff\"-->\n> body",
		nil, []string{"border-left-color"}},
	{"testAttributeColoursThatAreNotColoursAreDropped/word", "---  <!--kvit color=crimson-->",
		[]string{"border-top-color:crimson"}, nil},
}

func TestHTMLExport(t *testing.T) {
	for _, c := range htmlCases {
		t.Run(c.name, func(t *testing.T) {
			title := ""
			if c.name == "testHtmlWrapper" {
				title = "My Title"
			}
			html := HTMLFromMarkdown(c.md, title, Options{})
			for _, w := range c.want {
				if !strings.Contains(html, w) {
					t.Errorf("missing %q in\n%s", w, bodyOf(html))
				}
			}
			for _, n := range c.not {
				if strings.Contains(html, n) {
					t.Errorf("unexpected %q in\n%s", n, bodyOf(html))
				}
			}
		})
	}
}

// bodyOf is a page without its stylesheet, for failure messages.
func bodyOf(html string) string {
	if i := strings.Index(html, "<body>"); i >= 0 {
		return html[i:]
	}
	return html
}

// Script tags are written once per page, however many blocks need them
// (testMermaidScriptOnlyWithMermaid, testMermaidHtmlExport,
// testMathJaxScriptTagInjectedOnlyWithMath).
func TestScriptTagsAreWrittenOnce(t *testing.T) {
	cases := []struct {
		name, md, needle string
		count            int
	}{
		{"testMermaidScriptOnlyWithMermaid", "```mermaid\nflowchart LR\nA-->B\n```\n\n```mermaid\ngraph TD\nC-->D\n```",
			"mermaid.esm.min.mjs", 1},
		{"testMermaidHtmlExport", "```mermaid\nflowchart LR\n  A[<x>] --> B\n```", "cdn.jsdelivr.net/npm/mermaid@11.16.0", 1},
		{"testMathJaxScriptTagInjectedOnlyWithMath", "Inline $a+b$ math.\n\n$$\nE = mc^2\n$$", "MathJax-script", 1},
	}
	for _, c := range cases {
		if n := strings.Count(HTMLFromMarkdown(c.md, "", Options{}), c.needle); n != c.count {
			t.Errorf("%s: %q appears %d times, want %d", c.name, c.needle, n, c.count)
		}
	}
}

// The page is exactly what the Qt exporter writes for a small note, stylesheet
// aside: the wrapper, the anchors, and no stray separators between blocks.
func TestHTMLPageShape(t *testing.T) {
	html := HTMLFromMarkdown("# One\n\nText with **bold**.\n\n- a\n- b", "Note", Options{})
	want := "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\n<title>Note</title>\n<style>" +
		stylesheet(Colors{}) + "</style>\n</head>\n<body>\n" +
		`<h1 id="one">One</h1><p>Text with <strong>bold</strong>.</p><ul><li>a</li><li>b</li></ul>` +
		"\n</body></html>\n"
	if html != want {
		t.Errorf("page:\n%s\nwant:\n%s", html, want)
	}
	if !strings.Contains(HTMLFromMarkdown("x", "", Options{}), "<title>Kvit Export</title>") {
		t.Error("an untitled page is not titled Kvit Export")
	}
}

// Only some blocks are written, but the table of contents still reads the
// whole note, and a selected heading keeps the anchor it has in the note
// (testBlockSubsetKeepsDocumentContext).
func TestSelectionKeepsDocumentContext(t *testing.T) {
	blocks := editor.ParseMarkdown("# Intro\n\n```toc\n```\n\n## Details\n\n# Intro")
	if len(blocks) != 4 {
		t.Fatalf("parsed %d blocks, want 4", len(blocks))
	}
	html := HTMLFromSelection(blocks, []int{1}, "", Options{})
	for _, w := range []string{`class="toc"`, `<a href="#intro">Intro</a>`, `<a href="#details">Details</a>`,
		`<a href="#intro-1">Intro</a>`} {
		if !strings.Contains(html, w) {
			t.Errorf("missing %q", w)
		}
	}
	if strings.Contains(html, "<h1") || strings.Contains(html, "<h2") {
		t.Error("a heading that was not selected was written")
	}
	if text := TextFromSelection(blocks, []int{1}, Options{}); !strings.Contains(text, "Intro\n  Details\nIntro") {
		t.Errorf("text selection: %q", text)
	}
	dup := HTMLFromSelection(blocks, []int{3}, "", Options{})
	if !strings.Contains(dup, `id="intro-1"`) || strings.Contains(dup, `id="intro"`) {
		t.Errorf("the second Intro lost its anchor: %s", bodyOf(dup))
	}
	written, err := Selection(blocks, []int{1}, "Subset", FormatHTML, Options{})
	if err != nil || !strings.Contains(string(written), `href="#details"`) || strings.Contains(string(written), "<h2") {
		t.Errorf("selection as HTML: %v %s", err, bodyOf(string(written)))
	}
}

// An image is looked for beside the note, embedded as a data: address, and
// its presentation attributes reach the markup
// (testImageEffectsAndAlignmentReachTheExport).
func TestImageEffectsAndAlignment(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "pic.png"), []byte("PNGDATA"), 0o644); err != nil {
		t.Fatal(err)
	}
	opt := Options{NoteDir: dir, VaultRoot: dir}
	cases := []struct {
		md        string
		want, not []string
	}{
		{"![alt](pic.png)  <!--kvit align=left rounded=8 shadow border-->",
			[]string{"border-radius:8px", "box-shadow:", `<figure style="text-align:left">`, `<img alt="alt"`,
				`class="bordered"`, "img.bordered{border:1px solid", "data:image/png;base64,UE5HREFUQQ=="}, nil},
		{"![alt](pic.png)  <!--kvit border=#c1121f-->", []string{"border:1px solid #c1121f"}, []string{`class="bordered"`}},
		{"![alt](pic.png)  <!--kvit rounded-->", []string{"border-radius:12px"}, nil},
		{"![alt](pic.png)", []string{"<figure>"}, nil},
		{"![alt](missing.png)", []string{"<em>[image: missing.png]</em>"}, []string{"<img"}},
	}
	for _, c := range cases {
		html := HTMLFromMarkdown(c.md, "", opt)
		for _, w := range c.want {
			if !strings.Contains(html, w) {
				t.Errorf("%s: missing %q", c.md, w)
			}
		}
		for _, n := range c.not {
			if strings.Contains(html, n) {
				t.Errorf("%s: unexpected %q", c.md, n)
			}
		}
	}
}

// An image over the attachment budget is left out rather than embedded
// (testOversizedAttachmentIsSkippedNotInlined, for one note).
func TestOversizedImageIsLeftOut(t *testing.T) {
	dir := t.TempDir()
	big := strings.Repeat("Z", 200*1024)
	if err := os.WriteFile(filepath.Join(dir, "big.png"), []byte(big), 0o644); err != nil {
		t.Fatal(err)
	}
	md := "# With image\n\n![](big.png)\n"
	if html := HTMLFromMarkdown(md, "", Options{NoteDir: dir}); !strings.Contains(html, "data:image/png;base64,") {
		t.Error("an image under the budget was not embedded")
	}
	html := HTMLFromMarkdown(md, "", Options{NoteDir: dir, MaxAttachmentBytes: 1024})
	if strings.Contains(html, "data:image/png") || !strings.Contains(html, "With image") {
		t.Errorf("an image over the budget: %s", bodyOf(html))
	}
}

// Inline spans render as htmlinline.cpp renders them, one row per span type.
func TestInlineHTML(t *testing.T) {
	cases := []struct{ md, want string }{
		{"**b** *i* ***bi*** ~~s~~ ==h== ++u++", "<strong>b</strong> <em>i</em> <strong><em>bi</em></strong> <s>s</s> <mark>h</mark> <u>u</u>"},
		{"H~2~O and x^2^", "H<sub>2</sub>O and x<sup>2</sup>"},
		{"`a < b`", "<code>a &lt; b</code>"},
		{"``has ` tick``", "<code>has ` tick</code>"},
		{`<span style="color:#c1121f">red</span>`, `<span style="color:#c1121f">red</span>`},
		{"[[Note|shown]] and [[Other]]", "shown and Other"},
		{"see https://example.com/a.", `see <a href="https://example.com/a">https://example.com/a</a>.`},
		{"**[docs](https://x)**", `<strong><a href="https://x">docs</a></strong>`},
		{"snake_case_name", "snake_case_name"},
		{`\*not\*`, "*not*"},
		{"a \"q\" & b", "a &quot;q&quot; &amp; b"},
		{"it's", "it's"},
	}
	for _, c := range cases {
		if got := inlineHTML(c.md, nil); got != c.want {
			t.Errorf("%q:\n got %q\nwant %q", c.md, got, c.want)
		}
	}
}

// A code block's tokens are coloured with the theme's code colours, and the
// text between them is escaped.
func TestCodeHighlighting(t *testing.T) {
	cases := []struct{ lang, code, want string }{
		{"python", "return 1", `<span style="color:#a626a4">return</span> <span style="color:#986801">1</span>`},
		{"go", "x := \"a<b\" // c", `x := <span style="color:#50a14f">&quot;a&lt;b&quot;</span> <span style="color:#a0a1a7">// c</span>`},
		{"js", "/* a\nb */ f()", `<span style="color:#a0a1a7">/* a</span>` + "\n" + `<span style="color:#a0a1a7">b */</span> <span style="color:#4078f2">f</span>()`},
		{"nosuch", "if x", "if x"},
		{"html", `<a href="x">`, `&lt;<span style="color:#a626a4">a</span> <span style="color:#4078f2">href</span>=<span style="color:#50a14f">&quot;x&quot;</span>&gt;`},
	}
	r := newRenderer(nil, Options{})
	for _, c := range cases {
		if got := r.highlighted(c.lang, c.code); got != c.want {
			t.Errorf("%s %q:\n got %s\nwant %s", c.lang, c.code, got, c.want)
		}
	}
}

// A note's front matter is never part of an export's body. The editor keeps a
// file's front matter as a leading block when it opens a loose file, and that
// block is left out; a body that starts with "---" but has no front matter by
// the Qt app's rule is a divider and what follows, as the Qt parser reads it.
func TestLeadingDashes(t *testing.T) {
	withFM := HTMLFromBlocks(editor.ParseMarkdown("---\ntags: [a]\n---\n# T"), "", Options{})
	if strings.Contains(withFM, "<hr>") || strings.Contains(withFM, "tags") || !strings.Contains(withFM, `<h1 id="t">T</h1>`) {
		t.Errorf("front matter block: %s", bodyOf(withFM))
	}
	divided := HTMLFromMarkdown("---\n\ntext\n\n---", "", Options{})
	if !strings.Contains(divided, "<hr><p>text</p><hr>") {
		t.Errorf("divider-led body: %s", bodyOf(divided))
	}
	// HTMLFromMarkdown is given a body, so a second front-matter-shaped block
	// in it is text, as it is to the Qt exporter.
	body := HTMLFromMarkdown("---\nnote: buy milk\n---\nBody", "", Options{})
	if !strings.Contains(body, "<hr><p>note: buy milk</p><hr><p>Body</p>") {
		t.Errorf("front matter shaped body: %s", bodyOf(body))
	}
	if text := TextFromBlocks(editor.ParseMarkdown("---\ntags: [a]\n---\nx"), Options{}); text != "x\n" {
		t.Errorf("text: %q", text)
	}
}
