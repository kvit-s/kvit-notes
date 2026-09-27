package highlight

// The languages Kvit colours, the names it takes for them, and each
// language's rules, from src/content/codelanguages.cpp.

import (
	"slices"
	"strings"
)

// Language is one language the language menu offers.
type Language struct {
	// ID is the language's canonical name, the one Kvit writes on a code
	// fence when the language is picked from the menu.
	ID string
	// Name is what the menu shows for it.
	Name string
	// Aliases are the other names Kvit takes for it, on a code fence or
	// after "code " in the / menu, such as "py" for Python.
	Aliases []string
}

// menu is the language menu's list in its order (supportedLanguages in
// codelanguages.cpp), with the names qml/LanguagePicker.qml shows. The picker
// has names for eleven of the languages and shows the other five by their
// IDs, and so does this list. The aliases are in the order aliasMap in
// codelanguages.cpp lists them.
var menu = []Language{
	{"python", "Python", []string{"py", "python3"}},
	{"javascript", "JavaScript", []string{"js", "node", "jsx", "mjs"}},
	{"typescript", "typescript", []string{"ts", "tsx"}},
	{"cpp", "C++", []string{"c++", "cxx", "cc", "c", "h", "hpp"}},
	{"csharp", "csharp", []string{"cs", "c#"}},
	{"java", "Java", nil},
	{"go", "go", []string{"golang"}},
	{"rust", "rust", []string{"rs"}},
	{"qml", "qml", nil},
	{"html", "HTML", []string{"htm", "xhtml"}},
	{"css", "CSS", nil},
	{"sql", "SQL", []string{"mysql", "postgres", "postgresql"}},
	{"bash", "Bash", []string{"sh", "shell", "zsh"}},
	{"json", "JSON", nil},
	{"xml", "XML", []string{"svg"}},
	{"markdown", "Markdown", []string{"md"}},
}

// canonical maps each name and alias Kvit takes to its language's ID
// (aliasMap in codelanguages.cpp). It has Mermaid, which the menu does not
// offer: a mermaid fence is drawn as a diagram rather than as a code block,
// and Mermaid's colours are for the diagram block's source editor.
var canonical = func() map[string]string {
	m := map[string]string{"mermaid": "mermaid"}
	for _, l := range menu {
		m[l.ID] = l.ID
		for _, a := range l.Aliases {
			m[a] = l.ID
		}
	}
	return m
}()

// Languages lists the languages in the order Kvit's language menu shows them.
func Languages() []Language {
	out := make([]Language, len(menu))
	for i, l := range menu {
		out[i] = l
		out[i].Aliases = slices.Clone(l.Aliases)
	}
	return out
}

// Canonical is the ID a language name or alias stands for, or "" when Kvit
// does not know it. Case and surrounding spaces do not matter.
func Canonical(lang string) string {
	return canonical[strings.ToLower(strings.TrimSpace(lang))]
}

// family is which scanner reads a language.
type family int

const (
	// genericFamily is the shared scanner, reading the language's rules.
	genericFamily family = iota
	markupFamily
	cssFamily
	markdownFamily
	mermaidFamily
)

// rules is how a language is read (Rules in codelanguages.cpp). All but
// family are for the shared scanner.
type rules struct {
	family family
	// keywords are coloured Keyword, and types Type.
	keywords, types map[string]bool
	// foldCase matches a word to keywords and types in any case (SQL).
	foldCase bool
	// lineComment starts a comment that runs to the end of the line.
	lineComment string
	// blockStart and blockEnd enclose a comment that may span lines.
	blockStart, blockEnd string
	// quotes are the runes that open and close a string on one line.
	quotes string
	// tripleQuotes makes """ and ''' open a string that may span lines
	// (Python).
	tripleQuotes bool
	// backtick makes a backtick open a string on one line (JavaScript's
	// template literal, Go's raw string).
	backtick bool
	// doubledQuote makes a doubled quote inside a string stand for one
	// quote, in place of backslash escapes (SQL).
	doubledQuote bool
	// dollarVar colours $name and ${...} as Type (Bash).
	dollarVar bool
	// hashDirective colours a # that starts a line, with the word after it,
	// as Keyword (the C and C# preprocessors).
	hashDirective bool
	// decorators colours @name as Type (Python's decorators, Java's
	// annotations).
	decorators bool
	// calls colours a name followed by "(" as Type.
	calls bool
}

// words is a set of the space-separated words in list.
func words(list string) map[string]bool {
	m := map[string]bool{}
	for _, w := range strings.Fields(list) {
		m[w] = true
	}
	return m
}

// table is each language's rules, by ID (registry in codelanguages.cpp).
var table = map[string]*rules{
	"python": {
		keywords: words(`and as assert async await break class continue
			def del elif else except finally for from
			global if import in is lambda nonlocal not or
			pass raise return try while with yield match case`),
		types: words(`True False None self cls int str float bool
			list dict set tuple bytes object print len
			range super type isinstance Exception`),
		lineComment:  "#",
		quotes:       `"'`,
		tripleQuotes: true,
		decorators:   true,
		calls:        true,
	},
	"javascript": {
		keywords: words(`break case catch class const continue debugger
			default delete do else export extends finally
			for function if import in instanceof let new
			return super switch this throw try typeof var
			void while with yield async await of static get set`),
		types: words(`true false null undefined NaN Infinity console
			document window Math JSON Object Array String
			Number Boolean Promise Map Set Symbol`),
		lineComment: "//",
		blockStart:  "/*",
		blockEnd:    "*/",
		quotes:      `"'`,
		backtick:    true,
		calls:       true,
	},
	"cpp": {
		keywords: words(`alignas alignof and asm auto break case catch
			class const constexpr const_cast continue decltype
			default delete do dynamic_cast else enum explicit
			export extern for friend goto if inline mutable
			namespace new noexcept operator or private protected
			public register reinterpret_cast return sizeof static
			static_assert static_cast struct switch template this
			throw try typedef typename union using virtual
			volatile while not nullptr override final`),
		types: words(`bool char char8_t char16_t char32_t double float
			int long short signed unsigned void wchar_t true
			false size_t string vector map set std uint8_t
			int32_t int64_t uint32_t uint64_t`),
		lineComment:   "//",
		blockStart:    "/*",
		blockEnd:      "*/",
		quotes:        `"'`,
		hashDirective: true,
		calls:         true,
	},
	"java": {
		keywords: words(`abstract assert break case catch class const
			continue default do else enum extends final
			finally for goto if implements import instanceof
			interface native new package private protected
			public return static strictfp super switch
			synchronized this throw throws transient try void
			volatile while var record yield sealed permits`),
		types: words(`boolean byte char double float int long short
			String Object Integer Boolean Double List Map
			Set true false null System Math Exception`),
		lineComment: "//",
		blockStart:  "/*",
		blockEnd:    "*/",
		quotes:      `"'`,
		decorators:  true,
		calls:       true,
	},
	"go": {
		keywords: words(`break default func interface select case
			defer go map struct chan else goto
			package switch const fallthrough if range
			type continue for import return var`),
		types: words(`bool byte complex64 complex128 error float32
			float64 int int8 int16 int32 int64 rune
			string uint uint8 uint16 uint32 uint64
			uintptr true false nil append cap close
			copy delete len make new panic print
			println recover`),
		lineComment: "//",
		blockStart:  "/*",
		blockEnd:    "*/",
		quotes:      `"'`,
		backtick:    true,
		calls:       true,
	},
	"rust": {
		keywords: words(`as async await break const continue crate
			dyn else enum extern false fn for if
			impl in let loop match mod move mut
			pub ref return self Self static struct
			super trait true type unsafe use where
			while yield`),
		types: words(`bool char str String i8 i16 i32 i64
			i128 isize u8 u16 u32 u64 u128 usize
			f32 f64 Option Result Vec Box Some
			None Ok Err`),
		lineComment: "//",
		blockStart:  "/*",
		blockEnd:    "*/",
		quotes:      `"'`,
		calls:       true,
	},
	// TypeScript is JavaScript's rules with the type system's declarations
	// and built-in types added.
	"typescript": {
		keywords: words(`abstract as async await break case catch
			class const constructor continue declare
			default delete do else enum export extends
			finally for from function get if implements
			import in infer instanceof interface keyof
			let namespace new of private protected
			public readonly return satisfies set static
			super switch this throw try type typeof
			var void while with yield`),
		types: words(`any bigint boolean never number object
			string symbol unknown undefined null Array
			Date Error Map Promise Record Set Partial
			Required Readonly Pick Omit`),
		lineComment: "//",
		blockStart:  "/*",
		blockEnd:    "*/",
		quotes:      `"'`,
		backtick:    true,
		calls:       true,
	},
	"csharp": {
		keywords: words(`abstract as async await base break case
			catch checked class const continue default
			delegate do else enum event explicit extern
			finally fixed for foreach goto if implicit
			in interface internal is lock namespace new
			operator out override params private protected
			public readonly record ref return sealed
			sizeof stackalloc static struct switch this
			throw try typeof unchecked unsafe using
			virtual volatile while yield`),
		types: words(`bool byte char decimal double float int
			long object sbyte short string uint ulong
			ushort void dynamic var true false null
			String Object Task List Dictionary IEnumerable`),
		lineComment:   "//",
		blockStart:    "/*",
		blockEnd:      "*/",
		quotes:        `"'`,
		hashDirective: true,
		calls:         true,
	},
	// QML is JavaScript's scanner with QML's object and property
	// declarations added as keywords and types.
	"qml": {
		keywords: words(`as break case catch const continue default
			delete do else enum export extends finally
			for function if import in instanceof let
			new of pragma property readonly required
			return signal switch this throw try typeof
			var void while with yield on id`),
		types: words(`bool color date double font int list
			matrix4x4 point quaternion real rect size
			string url variant var Item Rectangle Text
			MouseArea Component QtObject ApplicationWindow
			true false null undefined Qt`),
		lineComment: "//",
		blockStart:  "/*",
		blockEnd:    "*/",
		quotes:      `"'`,
		backtick:    true,
		calls:       true,
	},
	"sql": {
		keywords: words(`select from where insert into values update set
			delete create table drop alter add column index
			view join inner left right outer full on group
			by order having limit offset distinct as and or
			not null is in like between exists union all
			primary key foreign references default unique check
			constraint begin commit rollback transaction case
			when then else end asc desc count sum avg
			min max with`),
		types: words(`int integer varchar char text boolean date
			datetime timestamp decimal numeric float double
			serial bigint smallint real blob`),
		foldCase:     true,
		lineComment:  "--",
		blockStart:   "/*",
		blockEnd:     "*/",
		quotes:       "'",
		doubledQuote: true,
		calls:        true,
	},
	"bash": {
		keywords: words(`if then else elif fi case esac for while
			until do done in function select time return
			break continue local export readonly declare shift
			exit source`),
		types: words(`echo printf cd ls cp mv rm mkdir cat grep
			sed awk find test read set unset true false`),
		lineComment: "#",
		quotes:      `"'`,
		dollarVar:   true,
	},
	"json": {
		keywords: words(`true false null`),
		quotes:   `"`,
	},
	"html":     {family: markupFamily},
	"xml":      {family: markupFamily},
	"css":      {family: cssFamily},
	"markdown": {family: markdownFamily},
	"mermaid":  {family: mermaidFamily},
}

// mermaidKeywords are Mermaid's words that open a diagram and the statement
// words its diagram kinds share (mermaidKeywords in codelanguages.cpp). Node
// names are left out on purpose: a node is named whatever the author likes,
// and leaving names in the text colour is what makes the coloured words
// stand out. Case matters, as it does in Mermaid (classDef,
// sequenceDiagram).
var mermaidKeywords = words(`
	graph flowchart sequenceDiagram classDiagram stateDiagram
	erDiagram journey gantt pie quadrantChart mindmap
	timeline gitGraph requirementDiagram block sankey
	xychart packet architecture kanban radar treemap
	subgraph end direction participant actor activate
	deactivate note loop alt else opt par and
	rect critical option break autonumber create
	destroy box link links class classDef click
	call callback href style linkStyle state namespace
	title accTitle accDescr section dateFormat axisFormat
	excludes todayMarker commit branch checkout merge
	as`)

// mermaidModifiers are the layout directions and the placement words that
// qualify a statement, coloured Type, so that "flowchart LR" reads as a
// statement and its argument (mermaidModifiers in codelanguages.cpp).
var mermaidModifiers = words(`LR RL TB TD BT of over left right`)
