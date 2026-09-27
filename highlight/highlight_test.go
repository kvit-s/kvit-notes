package highlight

import (
	"slices"
	"strings"
	"testing"
	"unicode/utf8"
)

// find is the rune offset of needle's first occurrence in text.
func find(t *testing.T, text, needle string) int {
	t.Helper()
	i := strings.Index(text, needle)
	if i < 0 {
		t.Fatalf("%q is not in %q", needle, text)
	}
	return utf8.RuneCountInString(text[:i])
}

// check is one thing a test expects of a text's spans: that a span is
// exactly needle's first occurrence and is drawn as class, or, when at is
// set, only that needle's first rune is drawn as class (Plain when no span
// covers it). They are covers and tokenAt in Kvit's
// tests/test_codelanguages.cpp.
type check struct {
	needle string
	class  Class
	at     bool
}

func whole(needle string, c Class) check { return check{needle, c, false} }
func at(needle string, c Class) check    { return check{needle, c, true} }

// classAt is the class the rune at pos is drawn as.
func classAt(spans []Span, pos int) Class {
	for _, s := range spans {
		if pos >= s.Start && pos < s.End {
			return s.Class
		}
	}
	return Plain
}

// className is c's name in a failure message.
func className(c Class) string {
	return []string{"Plain", "Keyword", "Type", "String", "Comment", "Number"}[c]
}

// expect fails the test for each check the spans of text do not meet.
func expect(t *testing.T, lang, text string, checks []check) {
	t.Helper()
	spans := Highlight(lang, text)
	for _, c := range checks {
		pos := find(t, text, c.needle)
		if c.at {
			if got := classAt(spans, pos); got != c.class {
				t.Errorf("%s %q: %q is drawn as %s, want %s", lang, text, c.needle, className(got), className(c.class))
			}
			continue
		}
		want := Span{pos, pos + utf8.RuneCountInString(c.needle), c.class}
		if !slices.Contains(spans, want) {
			t.Errorf("%s %q: no %s span for %q in %v", lang, text, className(c.class), c.needle, spans)
		}
	}
}

// The menu offers Kvit's sixteen languages in Kvit's order, under the names
// its language picker shows (supportedSetIncludesSourceFileLanguages).
func TestLanguagesAreKvitsMenu(t *testing.T) {
	var ids, names []string
	for _, l := range Languages() {
		ids = append(ids, l.ID)
		names = append(names, l.Name)
	}
	wantIDs := []string{"python", "javascript", "typescript", "cpp", "csharp", "java",
		"go", "rust", "qml", "html", "css", "sql", "bash", "json", "xml", "markdown"}
	if !slices.Equal(ids, wantIDs) {
		t.Errorf("IDs %q, want %q", ids, wantIDs)
	}
	wantNames := []string{"Python", "JavaScript", "typescript", "C++", "csharp", "Java",
		"go", "rust", "qml", "HTML", "CSS", "SQL", "Bash", "JSON", "XML", "Markdown"}
	if !slices.Equal(names, wantNames) {
		t.Errorf("names %q, want %q", names, wantNames)
	}
	// Every language on the menu has rules, and the list is a copy.
	for _, l := range Languages() {
		if table[l.ID] == nil {
			t.Errorf("%s has no rules", l.ID)
		}
	}
	Languages()[0].Aliases[0] = "changed"
	if Languages()[0].Aliases[0] != "py" {
		t.Error("changing the list Languages returns changed the menu")
	}
}

// Every name and alias of Kvit's alias map resolves to its language, in any
// case and with spaces around it (aliasesResolve).
func TestAliases(t *testing.T) {
	// The rows of Kvit's aliasesResolve test.
	for alias, id := range map[string]string{
		"py": "python", "PY": "python", "js": "javascript", "node": "javascript",
		"c++": "cpp", "cxx": "cpp", "c": "cpp", "sh": "bash", "shell": "bash",
		"md": "markdown", "postgres": "sql", "svg": "xml", "ts": "typescript",
		"tsx": "typescript", "rs": "rust", "golang": "go", "cs": "csharp",
		"c#": "csharp", "  Python  ": "python",
	} {
		if got := Canonical(alias); got != id {
			t.Errorf("Canonical(%q) = %q, want %q", alias, got, id)
		}
	}
	// The whole of aliasMap in src/content/codelanguages.cpp, a language's
	// ID first and then its aliases.
	qt := map[string]string{}
	for _, names := range []string{
		"python py python3",
		"javascript js node jsx mjs",
		"cpp c++ cxx cc c h hpp",
		"java",
		"go golang",
		"rust rs",
		"typescript ts tsx",
		"csharp cs c#",
		"qml",
		"html htm xhtml",
		"css",
		"sql mysql postgres postgresql",
		"bash sh shell zsh",
		"json",
		"xml svg",
		"markdown md",
		"mermaid",
	} {
		f := strings.Fields(names)
		for _, name := range f {
			qt[name] = f[0]
		}
	}
	for name, id := range qt {
		for _, form := range []string{name, strings.ToUpper(name), "\t" + name + " "} {
			if got := Canonical(form); got != id {
				t.Errorf("Canonical(%q) = %q, want %q", form, got, id)
			}
		}
	}
	if len(canonical) != len(qt) {
		t.Errorf("Kvit takes %d names, this package %d", len(qt), len(canonical))
	}
	// A language's aliases on the menu are the names that resolve to it, and
	// each colours as the language does.
	src := "def f(): # c\n  return 'x' /* y */"
	for _, l := range Languages() {
		for _, a := range l.Aliases {
			if Canonical(a) != l.ID {
				t.Errorf("%s lists %q, which resolves to %q", l.ID, a, Canonical(a))
			}
			if !slices.Equal(Highlight(" "+strings.ToUpper(a), src), Highlight(l.ID, src)) {
				t.Errorf("%q colours differently from %s", a, l.ID)
			}
		}
	}
}

// An unknown or empty language colours nothing
// (unknownLanguageIsEmptyAndPaintsNothing).
func TestUnknownLanguage(t *testing.T) {
	if got := Canonical("brainfuck"); got != "" {
		t.Errorf("Canonical(brainfuck) = %q", got)
	}
	if got := Highlight("brainfuck", "def x(): pass"); got != nil {
		t.Errorf("brainfuck: %v", got)
	}
	if got := Highlight("", "anything"); got != nil {
		t.Errorf("no language: %v", got)
	}
	// "plain", which the language picker offers as "Plain code", is not a
	// language either.
	if got := Highlight("plain", "if x: return 1"); got != nil {
		t.Errorf("plain: %v", got)
	}
}

// Mermaid is coloured but is not on the menu: a mermaid fence is a diagram
// block, and its colours serve the diagram's source editor
// (mermaidIsHighlightedButNotOffered).
func TestMermaidIsHighlightedButNotOffered(t *testing.T) {
	if got := Canonical("Mermaid"); got != "mermaid" {
		t.Errorf("Canonical(Mermaid) = %q", got)
	}
	if len(Highlight("mermaid", "flowchart LR")) == 0 {
		t.Error("mermaid colours nothing")
	}
	for _, l := range Languages() {
		if l.ID == "mermaid" {
			t.Error("mermaid is on the menu")
		}
	}
}

// Kvit's tests of its highlighter, from tests/test_codelanguages.cpp, each
// under its name there.
func TestKvitCases(t *testing.T) {
	cases := []struct {
		name, lang, src string
		checks          []check
	}{
		{"pythonKeywordsTypesStringsComments", "python",
			"def greet(name):  # say hi\n    return 'hello'",
			[]check{whole("def", Keyword), whole("return", Keyword), whole("# say hi", Comment),
				whole("'hello'", String), whole("greet", Type)}},
		{"pythonNumbersAndDecorator", "python", "@decorator\nx = 0xFF + 3.14e2",
			[]check{whole("@decorator", Type), whole("0xFF", Number), whole("3.14e2", Number)}},
		{"pythonTripleQuotedStringSpansLines", "python", "a = \"\"\"line one\nstill string\n\"\"\"\nb = 1",
			[]check{at("line one", String), at("still string", String), whole("1", Number)}},
		{"javascriptTemplateAndBlockComment", "javascript", "const s = `hi ${x}`; /* multi\nline */ let n = 42;",
			[]check{whole("const", Keyword), whole("let", Keyword), whole("`hi ${x}`", String),
				whole("42", Number), at("multi", Comment), at("line */", Comment)}},
		{"cppPreprocessorAndTypes", "cpp", "#include <vector>\nint main() { return 0; }",
			[]check{whole("#include", Keyword), whole("int", Type), whole("return", Keyword),
				whole("0", Number), whole("main", Type)}},
		{"cppLineCommentAndString", "cpp", `auto x = "a\"b"; // trailing`,
			[]check{whole("auto", Keyword), whole(`"a\"b"`, String), whole("// trailing", Comment)}},
		{"javaAnnotationAndKeywords", "java", "@Override\npublic final int x = 5;",
			[]check{whole("@Override", Type), whole("public", Keyword), whole("final", Keyword),
				whole("int", Type), whole("5", Number)}},
		{"goRuleTableCoversAllTokenClasses", "go", `func main() { var n int = 42; println("go") } // note`,
			[]check{whole("func", Keyword), whole("int", Type), whole(`"go"`, String),
				whole("// note", Comment), whole("42", Number)}},
		{"rustRuleTableCoversAllTokenClasses", "rust", `fn main() { let n: i32 = 42; println("rust"); } // note`,
			[]check{whole("fn", Keyword), whole("i32", Type), whole(`"rust"`, String),
				whole("// note", Comment), whole("42", Number)}},
		{"typescriptRuleTableCoversAllTokenClasses", "ts",
			"interface Row { value: string } const n = 42; // note\nconst s = \"ts\";",
			[]check{whole("interface", Keyword), whole("string", Type), whole(`"ts"`, String),
				whole("// note", Comment), whole("42", Number)}},
		{"csharpRuleTableCoversAllTokenClasses", "cs", `public class App { string s = "cs"; int n = 42; } // note`,
			[]check{whole("public", Keyword), whole("string", Type), whole(`"cs"`, String),
				whole("// note", Comment), whole("42", Number)}},
		{"qmlRuleTableCoversAllTokenClasses", "qml", `Rectangle { property string label: "qml"; width: 42 } // note`,
			[]check{whole("property", Keyword), whole("Rectangle", Type), whole(`"qml"`, String),
				whole("// note", Comment), whole("42", Number)}},
		{"sqlCaseInsensitiveKeywordsAndDashComment", "sql", "SELECT * from users -- all\nWHERE id = 'x''y';",
			[]check{whole("SELECT", Keyword), whole("from", Keyword), whole("WHERE", Keyword),
				whole("-- all", Comment), whole("'x''y'", String)}},
		{"bashVariablesAndComment", "bash", "echo $HOME # comment\nfor i in ${list}; do :; done",
			[]check{whole("echo", Type), whole("$HOME", Type), whole("# comment", Comment),
				whole("for", Keyword), whole("${list}", Type), whole("done", Keyword)}},
		{"jsonStringsNumbersLiterals", "json", `{"key": true, "n": -1.5, "z": null}`,
			[]check{whole(`"key"`, String), whole("true", Keyword), whole("null", Keyword), whole("1.5", Number)}},
		{"htmlTagsAttributesCommentsEntities", "html", `<a href="x">t</a><!-- note -->&amp;`,
			[]check{whole("a", Keyword), whole("href", Type), whole(`"x"`, String),
				whole("<!-- note -->", Comment), whole("&amp;", Number)}},
		{"xmlCommentSpansLines", "xml", "<root><!-- a\nb -->\n<x/></root>",
			[]check{at("a\n", Comment), at("b -->", Comment), whole("root", Keyword)}},
		{"cssSelectorsPropertiesColorsComments", "css", ".btn { color: #ff0000; width: 20px; } /* c */",
			[]check{whole(".btn", Type), whole("color", Type), whole("#ff0000", Number),
				whole("20px", Number), whole("/* c */", Comment)}},
		{"cssAtRuleAndImportant", "css", "@media screen { a { color: red !important; } }",
			[]check{whole("@media", Keyword), whole("!important", Keyword)}},
		{"markdownHeadingsListsCodeLinks", "markdown", "# Title\n- item `code`\n> quote\n[t](http://u)",
			[]check{at("# Title", Keyword), whole("`code`", String), at("> quote", Comment),
				whole("(http://u)", Type)}},
		{"mermaidFlowchartHeaderLinksAndLabels", "mermaid",
			"flowchart LR\n    A[Start] --> B{Decision}\n    B -->|yes| C[Done]",
			[]check{whole("flowchart", Keyword), whole("LR", Type), whole("-->", Type),
				whole("[Start]", String), whole("{Decision}", String), whole("|yes|", String),
				at("A[Start]", Plain)}},
		{"mermaidLabelsNestAndSurviveBracketsInText", "mermaid",
			"graph TD\n  A((Round)) --> B[\"a ] b\"]\n  C{{Hex}}",
			[]check{whole("((Round))", String), whole("{{Hex}}", String), whole(`["a ] b"]`, String)}},
		{"mermaidCommentsAndDirectives", "mermaid", "%% a note\nflowchart LR\n  A --> B %% trailing",
			[]check{whole("%% a note", Comment), whole("%% trailing", Comment), whole("flowchart", Keyword)}},
		{"mermaidSequenceMessagesAndNotes", "mermaid",
			"sequenceDiagram\n    Alice->>John: Hello John\n    note right of John: thinking\n    loop every minute\n    end",
			[]check{whole("sequenceDiagram", Keyword), whole("->>", Type), whole(": Hello John", String),
				whole("note", Keyword), whole(": thinking", String), whole("loop", Keyword), whole("end", Keyword)}},
		{"mermaidColonOnlyLabelsAfterALink", "mermaid", "classDiagram\n  Animal : +int age",
			[]check{at(": +int age", Plain)}},
		{"mermaidColonOnlyLabelsAfterALink", "mermaid", "flowchart LR\n  style A fill:#f9f",
			[]check{at("fill:", Plain)}},
		{"mermaidColonOnlyLabelsAfterALink", "mermaid", "stateDiagram-v2\n  A --> B : event",
			[]check{whole(": event", String)}},
		{"mermaidClassArrowheadsKeepTheirBar", "mermaid", "classDiagram\n  Animal <|-- Duck\n  A ..|> B",
			[]check{whole("<|--", Type), whole("..|>", Type)}},
		{"wholeTextEqualsThreadedLines", "cpp", "x = 1 /* c\nd */ y = 2",
			[]check{whole("1", Number), whole("2", Number), at("/* c", Comment), at("d */", Comment)}},
	}
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) { expect(t, c.lang, c.src, c.checks) })
	}
}

// lineSpans colours one line on its own, starting in state st, as Kvit's
// tests call highlightLine.
func lineSpans(lang, text string, st state) ([]Span, state) {
	sc := scan{s: []rune(text)}
	st = sc.line(table[Canonical(lang)], st)
	return sc.out, st
}

// A line that leaves a comment open hands its state to the next line, which
// is all comment until the comment closes (perLineStateThreadsBlockComment,
// mermaidDirectiveSpansLines).
func TestLineState(t *testing.T) {
	for _, c := range []struct {
		name, lang       string
		open, mid, close string
	}{
		{"perLineStateThreadsBlockComment", "cpp", "int x; /* open", "still comment", "done */ int y;"},
		{"mermaidDirectiveSpansLines", "mermaid", "%%{init: {", "  'theme': 'dark'", "} }%%"},
	} {
		_, st := lineSpans(c.lang, c.open, normal)
		if st == normal {
			t.Errorf("%s: %q leaves nothing open", c.name, c.open)
		}
		spans, st := lineSpans(c.lang, c.mid, st)
		if want := []Span{{0, utf8.RuneCountInString(c.mid), Comment}}; !slices.Equal(spans, want) {
			t.Errorf("%s: %q gives %v, want %v", c.name, c.mid, spans, want)
		}
		if st == normal {
			t.Errorf("%s: %q closes the comment", c.name, c.mid)
		}
		if _, st = lineSpans(c.lang, c.close, st); st != normal {
			t.Errorf("%s: %q leaves state %d, want it closed", c.name, c.close, st)
		}
	}
}

// Every language colours a keyword, a string, a comment and a number, each
// where the language has it: JSON has no comments, and Kvit's Markdown pass
// colours no numbers.
func TestEveryLanguage(t *testing.T) {
	cases := map[string]struct {
		src    string
		checks []check
	}{
		"python": {"if x: # c\n    return 'a' + 1.5",
			[]check{whole("if", Keyword), whole("'a'", String), whole("# c", Comment), whole("1.5", Number)}},
		"javascript": {"let s = \"a\"; // c\nreturn 0x1F;",
			[]check{whole("let", Keyword), whole(`"a"`, String), whole("// c", Comment), whole("0x1F", Number)}},
		"typescript": {"let n: number = 7; // c\nconst s = 'b';",
			[]check{whole("let", Keyword), whole("number", Type), whole("'b'", String),
				whole("// c", Comment), whole("7", Number)}},
		"cpp": {`return "a" + 2; // c`,
			[]check{whole("return", Keyword), whole(`"a"`, String), whole("// c", Comment), whole("2", Number)}},
		"csharp": {`return "a" + 3; // c`,
			[]check{whole("return", Keyword), whole(`"a"`, String), whole("// c", Comment), whole("3", Number)}},
		"java": {`return "a" + 4L; // c`,
			[]check{whole("return", Keyword), whole(`"a"`, String), whole("// c", Comment), whole("4L", Number)}},
		"go": {`return "a" + 5 // c`,
			[]check{whole("return", Keyword), whole(`"a"`, String), whole("// c", Comment), whole("5", Number)}},
		"rust": {"let s = \"a\"; // c\nreturn 6;",
			[]check{whole("let", Keyword), whole(`"a"`, String), whole("// c", Comment), whole("6", Number)}},
		"qml": {"property int x: 7 // c\nproperty string s: \"a\"",
			[]check{whole("property", Keyword), whole(`"a"`, String), whole("// c", Comment), whole("7", Number)}},
		"html": {`<p class="a">&amp;</p> <!-- c -->`,
			[]check{whole("p", Keyword), whole("class", Type), whole(`"a"`, String),
				whole("<!-- c -->", Comment), whole("&amp;", Number)}},
		"css": {`@media print { a { width: 8px; content: "a"; } } /* c */`,
			[]check{whole("@media", Keyword), whole(`"a"`, String), whole("/* c */", Comment), whole("8px", Number)}},
		"sql": {"SELECT 'a', 9 -- c",
			[]check{whole("SELECT", Keyword), whole("'a'", String), whole("-- c", Comment), whole("9", Number)}},
		"bash": {"if true; then echo 'a' 10; fi # c",
			[]check{whole("if", Keyword), whole("'a'", String), whole("# c", Comment), whole("10", Number)}},
		"json": {`{"a": true, "n": 11} // c`,
			[]check{whole("true", Keyword), whole(`"a"`, String), whole("11", Number), at("// c", Plain)}},
		"xml": {`<x a="s">&lt;</x><!-- c -->`,
			[]check{whole("x", Keyword), whole(`"s"`, String), whole("<!-- c -->", Comment), whole("&lt;", Number)}},
		"markdown": {"# Heading 1\n> quote 2\n- `code` 3",
			[]check{whole("# Heading 1", Keyword), whole("> quote 2", Comment), whole("`code`", String),
				whole("-", Type), at("3", Plain)}},
		"mermaid": {"pie title Pets\n  \"Dogs\" : 386 %% c",
			[]check{whole("pie", Keyword), whole(`"Dogs"`, String), whole("%% c", Comment), whole("386", Number)}},
	}
	ids := []string{"mermaid"}
	for _, l := range Languages() {
		ids = append(ids, l.ID)
	}
	for _, id := range ids {
		c, ok := cases[id]
		if !ok {
			t.Errorf("%s has no case", id)
			continue
		}
		t.Run(id, func(t *testing.T) { expect(t, id, c.src, c.checks) })
	}
}

// A comment, an HTML comment, a Mermaid directive, a Markdown fence and a
// Python triple-quoted string go on over the lines after the one that opens
// them, as one span on each line, and the text after them is coloured again.
func TestSpanLines(t *testing.T) {
	block := "a /* one\ntwo\nthree */ b"
	cases := []struct {
		lang, src string
		class     Class
		after     check
	}{
		{"javascript", block, Comment, at("b", Plain)},
		{"typescript", block, Comment, at("b", Plain)},
		{"cpp", block, Comment, at("b", Plain)},
		{"csharp", block, Comment, at("b", Plain)},
		{"java", block, Comment, at("b", Plain)},
		{"go", block, Comment, at("b", Plain)},
		{"rust", block, Comment, at("b", Plain)},
		{"qml", block, Comment, at("b", Plain)},
		{"sql", block, Comment, at("b", Plain)},
		{"css", block, Comment, at("b", Plain)},
		{"html", "<p><!-- one\ntwo\nthree --><b>", Comment, whole("b", Keyword)},
		{"xml", "<p><!-- one\ntwo\nthree --><b>", Comment, whole("b", Keyword)},
		{"mermaid", "%%{ one\ntwo\nthree }%% graph", Comment, whole("graph", Keyword)},
		{"markdown", "```\none\ntwo\nthree\n```\n# h", String, whole("# h", Keyword)},
		{"python", "a = \"\"\"one\ntwo\nthree\"\"\" + 1", String, whole("1", Number)},
		{"python", "a = '''one\ntwo\nthree''' + 1", String, whole("1", Number)},
	}
	for _, c := range cases {
		expect(t, c.lang, c.src, []check{at("one", c.class), at("two", c.class), at("three", c.class), c.after})
		// The middle line is one span of its own.
		pos := find(t, c.src, "two")
		if want := (Span{pos, pos + 3, c.class}); !slices.Contains(Highlight(c.lang, c.src), want) {
			t.Errorf("%s %q: no span %v for the middle line", c.lang, c.src, want)
		}
	}
}

// A quoted string ends with its line when it is not closed, and the next
// line is code again. That holds for JavaScript's template literals and Go's
// raw strings too, which those languages let span lines: Kvit colours only
// their first line.
func TestStringsEndWithTheirLine(t *testing.T) {
	for _, l := range Languages() {
		r := table[l.ID]
		if r.family != genericFamily {
			continue
		}
		q := string([]rune(r.quotes)[0])
		expect(t, l.ID, "x = "+q+"one\ntwo", []check{at("one", String), at("two", Plain)})
		if r.backtick {
			expect(t, l.ID, "x = `one\ntwo", []check{at("one", String), at("two", Plain)})
		}
	}
}

// A letter or digit outside Unicode's Basic Multilingual Plane, such as 𝑥
// or 𠀀, is a letter here, where Qt, which reads text as UTF-16 units, sees
// the two halves of its surrogate pair and takes neither for a letter. So
// "if𝑥" is one word here, where Qt colours its "if" as a keyword, and an
// HTML entity is at most ten runes long here, where Qt counts ten UTF-16
// units. These are the only places the port differs from the Qt highlighter.
func TestLettersOutsideTheBMP(t *testing.T) {
	expect(t, "python", "if𝑥 = 1 and 𠀀x(2)", []check{at("if", Plain), whole("𠀀x", Type)})
	expect(t, "html", "&😀😀😀😀😀;", []check{whole("&😀😀😀😀😀;", Number)})
}
