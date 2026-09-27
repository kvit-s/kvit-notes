package export

import (
	"html"
	"net/url"
	"strings"
	"testing"
)

// htmlMDCases are Kvit's HtmlToMarkdown tests (tests/test_htmltomarkdown.cpp)
// that compare the whole result, one row per input, named after the Qt test.
var htmlMDCases = []struct{ name, html, want string }{
	{"testHeadings/h1", "<h1>Title</h1>", "# Title"},
	{"testHeadings/h2", "<h2>Section</h2>", "## Section"},
	{"testHeadings/h3", "<h3>Sub</h3>", "### Sub"},
	{"testHeadings/h6", "<h6>Deep</h6>", "###### Deep"},
	{"testInlineEmphasis/b", "<p><b>bold</b></p>", "**bold**"},
	{"testInlineEmphasis/strong", "<p><strong>bold</strong></p>", "**bold**"},
	{"testInlineEmphasis/i", "<p><i>it</i></p>", "*it*"},
	{"testInlineEmphasis/em", "<p><em>it</em></p>", "*it*"},
	{"testInlineEmphasis/s", "<p><s>gone</s></p>", "~~gone~~"},
	{"testInlineEmphasis/spaces outside", "<p>a <b>b</b> c</p>", "a **b** c"},
	{"testInlineCodeAndPre/code", "<p>run <code>ls -l</code> now</p>", "run `ls -l` now"},
	{"testLinks/plain", `<p><a href="https://example.com">site</a></p>`, "[site](https://example.com)"},
	{"testLinks/bold link", `<p><a href="https://e.com"><b>bold link</b></a></p>`, "[**bold link**](https://e.com)"},
	{"testUnorderedList", "<ul><li>one</li><li>two</li></ul>", "- one\n\n- two"},
	{"testBlockquote", "<blockquote>quoted</blockquote>", "> quoted"},
	{"testParagraphSeparation/two", "<p>one</p><p>two</p>", "one\n\ntwo"},
	{"testParagraphSeparation/empty", "<p>one</p><p></p><p></p><p>two</p>", "one\n\ntwo"},
	{"testEntitiesDecode", "<p>a &amp; b &lt; c</p>", "a & b < c"},
	{"testEmptyAndPlainInput/empty", "", ""},
	{"testEmptyAndPlainInput/blank", "   ", ""},
	{"testEmptyAndPlainInput/text", "just text", "just text"},
	{"testMarkdownCharactersInTextAreEscaped/stars", "<p>2 * 3 * 4</p>", `2 \* 3 \* 4`},
	{"testMarkdownCharactersInTextAreEscaped/underscores", "<p>a_b_c</p>", `a\_b\_c`},
	{"testBlockLeadingConstructsAreEscaped/heading", "<p># literal heading</p>", `\# literal heading`},
	{"testBlockLeadingConstructsAreEscaped/hash tag", "<p>#tag</p>", `\#tag`},
	{"testBlockLeadingConstructsAreEscaped/dash list", "<p>- literal</p>", `\- literal`},
	{"testBlockLeadingConstructsAreEscaped/plus list", "<p>+ literal</p>", `\+ literal`},
	{"testBlockLeadingConstructsAreEscaped/quote", "<p>&gt; not a quote</p>", `\> not a quote`},
	{"testBlockLeadingConstructsAreEscaped/divider", "<p>---</p>", `\---`},
	{"testBlockLeadingConstructsAreEscaped/pipe row", "<p>| a | b |</p>", `\| a | b |`},
	{"testBlockLeadingConstructsAreEscaped/inside list item", "<ul><li># literal</li></ul>", `- \# literal`},
	{"testBlockLeadingConstructsAreEscaped/inside quote", "<blockquote># literal</blockquote>", `> \# literal`},
	{"testBlockLeadingConstructsAreEscaped/mid-paragraph hash", "<p>issue #42</p>", "issue #42"},
	{"testMultiLinePreIsOneFence/blank line", "<pre>one\n\nthree</pre>", "```\none\n\nthree\n```"},
	{"testMultiLinePreIsOneFence/mixed", "<p>before</p><pre>a\nb</pre><p>after</p>", "before\n\n```\na\nb\n```\n\nafter"},
	{"testMultiLinePreIsOneFence/sans pre", "<pre style=\"font-family: Arial\">a\nb</pre>", "```\na\nb\n```"},
}

func TestHTMLToMarkdown(t *testing.T) {
	for _, c := range htmlMDCases {
		if got := HTMLToMarkdown(c.html); got != c.want {
			t.Errorf("%s: %q\n got %q\nwant %q", c.name, c.html, got, c.want)
		}
	}
}

// The rest of the Qt converter's tests, which check parts of the result.
func TestHTMLToMarkdownParts(t *testing.T) {
	fenced := HTMLToMarkdown("<pre>int main() {}</pre>")
	if !strings.HasPrefix(fenced, "```") || !strings.Contains(fenced, "int main() {}") || !strings.HasSuffix(fenced, "```") {
		t.Errorf("testInlineCodeAndPre: %q", fenced)
	}
	ordered := HTMLToMarkdown("<ol><li>one</li><li>two</li></ol>")
	if !strings.Contains(ordered, "1. one") || !strings.Contains(ordered, "2. two") {
		t.Errorf("testOrderedList: %q", ordered)
	}
	nested := HTMLToMarkdown("<ul><li>outer</li><ul><li>inner</li></ul></ul>")
	if !strings.Contains(nested, "- outer") || !strings.Contains(nested, "  - inner") {
		t.Errorf("testNestedList: %q", nested)
	}
	table := HTMLToMarkdown("<table><tr><th>A</th><th>B</th></tr><tr><td>1</td><td>2</td></tr></table>")
	for _, w := range []string{"| A | B |", "| --- | --- |", "| 1 | 2 |"} {
		if !strings.Contains(table, w) {
			t.Errorf("testTable: %q lacks %q", table, w)
		}
	}
	codeSpan := HTMLToMarkdown("<p><code>inline_code_run</code></p>")
	if !strings.Contains(codeSpan, "`") {
		t.Errorf("testMultiLinePreIsOneFence code span: %q", codeSpan)
	}
}

func TestHasStructure(t *testing.T) {
	cases := []struct {
		html string
		want bool
	}{
		{"<h1>x</h1>", true},
		{"<ul><li>x</li></ul>", true},
		{"<span><b>x</b></span>", true},
		{`<a href="u">x</a>`, true},
		{"<html><body>plain</body></html>", false},
		{"plain", false},
		{`<p>a<img src="x.png">b</p>`, true},
	}
	for _, c := range cases {
		if got := HasStructure(c.html); got != c.want {
			t.Errorf("HasStructure(%q) = %v", c.html, got)
		}
	}
}

// An inline code span's fence is longer than any backtick run inside it, and
// reading the result back recovers the code exactly
// (testInlineCodeChoosesFenceLongerThanContent).
func TestInlineCodeFenceFitsItsContent(t *testing.T) {
	for _, code := range []string{"ls -l", "a ` b", "a `` b", "a ``` b", "`x", "x`", "``"} {
		md := HTMLToMarkdown("<p>run <code>" + html.EscapeString(code) + "</code> now</p>")
		r := []rune(md)
		var found *fspan
		for _, s := range parseSpans(r) {
			if s.typ == "code" {
				found = &s
			}
		}
		if found == nil {
			t.Errorf("%q: no code span in %q", code, md)
			continue
		}
		inner := string(r[found.start+found.openLen : found.end-found.closeLen])
		if len(inner) >= 2 && strings.HasPrefix(inner, " ") && strings.HasSuffix(inner, " ") {
			inner = inner[1 : len(inner)-1]
		}
		if inner != code {
			t.Errorf("%q: produced %q, read back %q", code, md, inner)
		}
	}
}

// A <pre> fence outruns a backtick run inside it (testCodeFenceChoosesFenceLongerThanContent,
// testMultiLinePreIsOneFence).
func TestCodeFenceFitsItsContent(t *testing.T) {
	md := HTMLToMarkdown("<pre>a ``` b</pre>")
	opener, _, _ := strings.Cut(md, "\n")
	if len(opener) < 4 || !strings.Contains(md, "a ``` b") || strings.Count(md, opener) != 2 {
		t.Errorf("single line: %q", md)
	}
	ticks := HTMLToMarkdown("<pre>plain\n``` inside</pre>")
	if !strings.HasPrefix(ticks, "````") || !strings.HasSuffix(ticks, "````") {
		t.Errorf("run on a later line: %q", ticks)
	}
	drawing := HTMLToMarkdown("<pre>┌──────────┐\n│ Editor   │\n│ (QML)    │\n└────┬─────┘</pre>")
	lines := strings.Split(drawing, "\n")
	if strings.Count(drawing, "```") != 2 || len(lines) != 6 || lines[1] != "┌──────────┐" ||
		lines[2] != "│ Editor   │" || lines[3] != "│ (QML)    │" || lines[4] != "└────┬─────┘" {
		t.Errorf("drawing: %q", drawing)
	}
}

// A link address with parentheses is percent-encoded so it fits
// (testLinkDestinationWithParenthesesSurvives).
func TestLinkDestinationWithParentheses(t *testing.T) {
	md := HTMLToMarkdown(`<p><a href="http://x/a_(b)_c">wiki</a></p>`)
	var link string
	for _, s := range parseSpans([]rune(md)) {
		if s.typ == "link" {
			link = s.url
		}
	}
	if link != "http://x/a_%28b%29_c" {
		t.Fatalf("link %q in %q", link, md)
	}
	if u, err := url.PathUnescape(link); err != nil || u != "http://x/a_(b)_c" {
		t.Errorf("decoded %q", u)
	}
}

// An <img> becomes a Markdown image, its address encoded like a link's, and
// a paragraph that is only an image reads back as an image block
// (testImagesBecomeMarkdownImages).
func TestImagesBecomeMarkdownImages(t *testing.T) {
	md := HTMLToMarkdown(`<p>before<img src="http://h/pic.png">after</p>`)
	if !strings.Contains(md, "![](http://h/pic.png)") || !strings.HasPrefix(md, "before") || !strings.HasSuffix(md, "after") {
		t.Errorf("inline image: %q", md)
	}
	if spaced := HTMLToMarkdown(`<p><img src="a b(c).png"></p>`); !strings.Contains(spaced, "![](a%20b%28c%29.png)") {
		t.Errorf("spaced: %q", spaced)
	}
	lone := HTMLToMarkdown(`<p><img src="pic.png"></p>`)
	if p := parseImageLine(trimSpace(lone)); !p.valid || p.kind != mediaImage || p.path != "pic.png" {
		t.Errorf("lone image: %q", lone)
	}
}

// What QTextDocument does with HTML the Qt tests do not cover, and the
// converter therefore sees: a <br> is a line separator inside the paragraph,
// whitespace between elements makes no paragraph, and a list's items are
// numbered within it.
func TestHTMLToMarkdownReadsLikeQTextDocument(t *testing.T) {
	cases := []struct{ html, want string }{
		{"<p>a<br>b</p>", "a\u2028b"},
		{"<ul>\n  <li>one</li>\n  <li>two</li>\n</ul>", "- one\n\n- two"},
		{"<ol><li>a</li><li>b<ol><li>x</li></ol></li><li>c</li></ol>", "1. a\n\n2. b\n\n  1. x\n\n3. c"},
		{"<li><p>para</p></li>", "para"},
		{"<ol><li><p>a</p></li><li>b</li></ol>", "1. a\n\n2. b"},
		{"<ol><li>a</li><li></li><li>b</li></ol>", "1. a\n\n2. b"},
		{"<ul><li>a<p>b</p>c</li></ul>", "- a\n\n> b\n\n> c"},
		{"<div>a</div>b", "ab"},
		{"<div><div>a</div></div>b", "a\n\nb"},
		{"<p>a</p><p></p><pre>x</pre><p></p><pre>y</pre>", "a\n\n```\nx\ny\n```"},
		{"<p>a <del>d</del> <strike>s</strike> <s>t</s></p>", "a d s ~~t~~"},
		{"<p><nobr>x y</nobr> z</p>", "x\u00a0y z"},
		{"<p style=\"white-space:nowrap\">no wrap</p>", "```\nno\u00a0wrap\n```"},
		{"<table><caption>Cap</caption><tr><td>a</td></tr></table>", "Cap\n\n| a |\n| --- |"},
		{"<blockquote><p>a</p><p>b</p></blockquote>", "> a\n\n> b"},
		{"<p>x</p><hr><p>y</p>", "x\n\ny"},
		{"<p><b>a</b><b>b</b></p>", "**ab**"},
		{"<p><span style=\"font-weight:bold\">w</span> <span style=\"font-style:italic\">i</span></p>", "**w** *i*"},
		{"<p><a name=\"x\">anchor</a></p>", "anchor"},
		{"<table><tr><td colspan=\"2\">wide</td></tr><tr><td>1</td><td>2</td></tr></table>",
			"| wide | wide |\n| --- | --- |\n| 1 | 2 |"},
		{"<p>a | b</p>", "a | b"},
		{"<html><head><title>T</title><style>p{}</style></head><body><p>x</p><script>y()</script></body></html>", "x"},
		{"<p>one<p>two", "one\n\ntwo"},
		{"<pre>\nfirst</pre>", "```\nfirst\n```"},
		{"<p style=\"margin-left:40px;margin-right:40px\">indented</p>", "> indented"},
		{"<section>a</section><section>b</section>", "ab"},
	}
	for _, c := range cases {
		if got := HTMLToMarkdown(c.html); got != c.want {
			t.Errorf("%q:\n got %q\nwant %q", c.html, got, c.want)
		}
	}
}
