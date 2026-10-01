// Package mathcmd is the list of math commands behind the menu a backslash
// opens while typing TeX, and the ranking that filters it as letters are
// typed. It knows nothing of the editor or of the math engine.
//
// The menu has two modes. On a bare backslash it shows categories laid out
// the way LyX's math toolbar is (Greek, Arrows, Fractions & roots and so on),
// each holding a hand-picked list of entries: Categories and
// ItemsForCategory. Once letters follow the backslash it shows one ranked
// list, ItemsFor, which holds the hand-picked entries and every other
// command the engine can draw, so a command the list does not name still
// completes. The engine's commands are given to New as a function, which in
// the app is mathtex.Commands.
//
// An entry is inserted as a template, such as "\frac{}{}"; its
// CursorOffset says where the caret goes, inside the first empty pair of
// braces. Recently accepted commands lead the categories.
//
// Offsets are rune offsets. Every template is ASCII, so they are also byte
// offsets.
package mathcmd

import (
	"slices"
	"strings"
	"unicode"
)

// MaxRecent is how many recently accepted commands the menu keeps.
const MaxRecent = 12

// RecentlyUsed is the name of the category that holds the recently accepted
// commands. It leads the categories once there are any.
const RecentlyUsed = "Recently used"

// The categories, in the order the menu lists them.
const (
	catGreek      = "Greek"
	catArrows     = "Arrows"
	catBinary     = "Binary operators"
	catRelations  = "Relations"
	catNegated    = "Negated relations"
	catBigOps     = "Big operators"
	catFracRoots  = "Fractions & roots"
	catDelimiters = "Delimiters"
	catAccents    = "Accents & decorations"
	catScripts    = "Scripts & limits"
	catFonts      = "Fonts & styles"
	catFunctions  = "Functions"
	catStructure  = "Structure"
	catMisc       = "Dots & misc"
	catSpacing    = "Spacing"
)

// Entry is one line of the menu.
type Entry struct {
	// Name is the command as typed, such as `\frac` or `^{}`.
	Name        string
	Description string
	// Category is "" for a command the engine knows that the hand-picked
	// list does not name.
	Category string
	// Insert is the text put in place of what was typed, on one line.
	// InsertDisplay is the form for a display math block, on several lines,
	// or "" when Insert serves both.
	Insert        string
	InsertDisplay string
	// CursorOffset is where the caret goes in Insert, or -1 for its end;
	// CursorOffsetDisplay is the same for InsertDisplay, -1 when it is "".
	CursorOffset        int
	CursorOffsetDisplay int
	// Preview is the TeX the menu draws as the entry's picture, or "" to
	// show Name as text.
	Preview string
	// Standalone is whether Insert, its slots filled, is a formula on its
	// own; a fragment such as "&" or "^{}" is not.
	Standalone bool
	// Curated is whether the entry is from the hand-picked list.
	Curated bool
}

// entry is one hand-picked entry as the list holds it.
type entry struct {
	name          string
	description   string
	category      string
	insert        string
	insertDisplay string
	preview       string
	aliases       []string // matched but never shown
	command       string   // the bare command, which the engine list may also hold
	standalone    bool
}

// Model is the menu's list and the recently accepted commands. The zero
// Model is not usable; make one with New.
type Model struct {
	commands   func() []string
	catalog    []entry
	categories []string
	curated    map[string]bool // bare commands the hand-picked list holds
	recent     []string        // names, most recent first

	// OnRecentChanged, when set, is called after NoteUsed changes the
	// recently accepted commands, so the application can save them. Loading
	// them with SetRecentCommands does not call it.
	OnRecentChanged func()
}

// New makes the menu's list. commands gives every command the math engine
// can draw, without backslashes; nil, or a function returning nil while math
// is off, leaves the hand-picked list alone.
func New(commands func() []string) *Model {
	m := &Model{commands: commands, curated: map[string]bool{}}
	m.categories = []string{catGreek, catArrows, catBinary, catRelations, catNegated,
		catBigOps, catFracRoots, catDelimiters, catAccents, catScripts,
		catFonts, catFunctions, catStructure, catMisc, catSpacing}

	m.addSymbols(catGreek, "alpha", "beta", "gamma", "delta", "epsilon", "varepsilon",
		"zeta", "eta", "theta", "vartheta", "iota", "kappa", "lambda", "mu", "nu", "xi",
		"pi", "varpi", "rho", "varrho", "sigma", "varsigma", "tau", "upsilon",
		"phi", "varphi", "chi", "psi", "omega",
		"Gamma", "Delta", "Theta", "Lambda", "Xi", "Pi", "Sigma", "Upsilon",
		"Phi", "Psi", "Omega")

	m.addSymbol(catArrows, "to", "Right arrow", "rightarrow", "arrow")
	m.addSymbol(catArrows, "gets", "Left arrow", "leftarrow")
	m.addSymbol(catArrows, "Rightarrow", "Implies", "implies")
	m.addSymbol(catArrows, "Leftarrow", "Implied by")
	m.addSymbol(catArrows, "Leftrightarrow", "If and only if", "iff", "equivalent")
	m.addSymbol(catArrows, "mapsto", "Maps to")
	m.addSymbols(catArrows, "leftrightarrow",
		"longrightarrow", "longleftarrow", "longleftrightarrow",
		"Longrightarrow", "Longleftarrow", "longmapsto",
		"uparrow", "downarrow", "updownarrow", "Uparrow", "Downarrow", "Updownarrow",
		"hookrightarrow", "hookleftarrow",
		"rightharpoonup", "rightharpoondown", "leftharpoonup", "leftharpoondown",
		"rightleftharpoons", "nearrow", "searrow", "swarrow", "nwarrow")

	m.addSymbol(catBinary, "pm", "Plus or minus", "plusminus")
	m.addSymbol(catBinary, "times", "Multiplication cross", "cross", "multiply")
	m.addSymbol(catBinary, "cdot", "Center dot", "dot product")
	m.addSymbol(catBinary, "div", "Division sign", "divide")
	m.addSymbol(catBinary, "setminus", "Set difference")
	m.addSymbols(catBinary, "mp", "ast", "star", "circ", "bullet", "cap", "cup",
		"uplus", "sqcap", "sqcup", "vee", "wedge", "wr", "oplus", "ominus",
		"otimes", "oslash", "odot", "dagger", "ddagger", "amalg", "diamond",
		"bigtriangleup", "bigtriangledown", "triangleleft", "triangleright")

	m.addSymbol(catRelations, "le", "Less than or equal", "leq", "<=")
	m.addSymbol(catRelations, "ge", "Greater than or equal", "geq", ">=")
	m.addSymbol(catRelations, "ne", "Not equal", "neq", "!=")
	m.addSymbol(catRelations, "approx", "Approximately equal")
	m.addSymbol(catRelations, "equiv", "Identical / equivalent")
	m.addSymbol(catRelations, "propto", "Proportional to")
	m.addSymbol(catRelations, "in", "Element of", "element", "member")
	m.addSymbols(catRelations, "sim", "simeq", "cong", "doteq", "prec", "preceq",
		"succ", "succeq", "ll", "gg", "subset", "supset", "subseteq", "supseteq",
		"sqsubseteq", "sqsupseteq", "ni", "vdash", "dashv", "models", "perp", "mid",
		"parallel", "asymp", "smile", "frown", "bowtie")

	m.addSymbol(catNegated, "notin", "Not an element of")
	m.addSymbols(catNegated, "nless", "ngtr", "nleq", "ngeq", "nsubseteq", "nsupseteq",
		"nsim", "ncong", "nmid", "nparallel", "nprec", "nsucc", "nvdash",
		"nRightarrow", "nLeftarrow")

	m.addSymbol(catBigOps, "sum", "Sum", "sigma")
	m.addSymbol(catBigOps, "prod", "Product")
	m.addSymbol(catBigOps, "int", "Integral", "integral")
	m.addSymbol(catBigOps, "oint", "Contour integral")
	m.addSymbols(catBigOps, "iint", "iiint", "coprod", "bigcap", "bigcup", "bigsqcup",
		"bigvee", "bigwedge", "bigoplus", "bigotimes", "bigodot", "biguplus")

	m.addTemplate(template{category: catFracRoots, name: `\frac`, insert: `\frac{}{}`,
		preview: `\frac{a}{b}`, description: "Fraction", aliases: []string{"fraction", "over"}})
	m.addTemplate(template{category: catFracRoots, name: `\tfrac`, insert: `\tfrac{}{}`,
		preview: `\tfrac{a}{b}`, description: "Inline-size fraction"})
	m.addTemplate(template{category: catFracRoots, name: `\dfrac`, insert: `\dfrac{}{}`,
		preview: `\dfrac{a}{b}`, description: "Display-size fraction"})
	m.addTemplate(template{category: catFracRoots, name: `\binom`, insert: `\binom{}{}`,
		preview: `\binom{n}{k}`, description: "Binomial coefficient",
		aliases: []string{"choose", "combination"}})
	m.addTemplate(template{category: catFracRoots, name: `\sqrt`, insert: `\sqrt{}`,
		preview: `\sqrt{x}`, description: "Square root", aliases: []string{"root", "radical"}})
	m.addTemplate(template{category: catFracRoots, name: `\sqrt[n]`, insert: `\sqrt[]{}`,
		preview: `\sqrt[n]{x}`, description: "nth root",
		aliases: []string{"root", "nthroot", "cube root"}})

	m.addTemplate(template{category: catDelimiters, name: `\left(\right)`,
		insert: `\left( \right)`, preview: `\left( x \right)`,
		description: "Parentheses, auto-sized", aliases: []string{"paren", "parentheses", "()"}})
	m.addTemplate(template{category: catDelimiters, name: `\left[\right]`,
		insert: `\left[ \right]`, preview: `\left[ x \right]`,
		description: "Brackets, auto-sized", aliases: []string{"bracket", "[]"}})
	m.addTemplate(template{category: catDelimiters, name: `\left\{\right\}`,
		insert: `\left\{ \right\}`, preview: `\left\{ x \right\}`,
		description: "Braces, auto-sized", aliases: []string{"brace", "set", "{}"}})
	m.addTemplate(template{category: catDelimiters, name: `\left|\right|`,
		insert: `\left| \right|`, preview: `\left| x \right|`,
		description: "Absolute value, auto-sized", aliases: []string{"abs", "modulus"}})
	m.addTemplate(template{category: catDelimiters, name: `\left\langle\right\rangle`,
		insert: `\left\langle \right\rangle`, preview: `\left\langle x \right\rangle`,
		description: "Angle brackets, auto-sized", aliases: []string{"angle brackets", "inner product"}})
	m.addSymbols(catDelimiters, "langle", "rangle", "lceil", "rceil", "lfloor", "rfloor")
	m.addTemplate(template{category: catDelimiters, name: `\|`, insert: `\|`, preview: `\|`,
		description: "Double vertical bar", aliases: []string{"Vert", "norm"}})

	for _, a := range [...]struct{ cmd, desc string }{
		{"hat", "Hat accent"}, {"widehat", "Wide hat"},
		{"bar", "Bar accent"}, {"overline", "Overline"},
		{"underline", "Underline"}, {"vec", "Vector arrow"},
		{"dot", "Dot accent"}, {"ddot", "Double dot accent"},
		{"tilde", "Tilde accent"}, {"widetilde", "Wide tilde"},
		{"check", "Check accent"}, {"breve", "Breve accent"},
		{"acute", "Acute accent"}, {"grave", "Grave accent"},
		{"mathring", "Ring accent"},
		{"overrightarrow", "Arrow over"},
		{"overleftarrow", "Left arrow over"},
	} {
		m.addTemplate(template{category: catAccents, name: `\` + a.cmd, insert: `\` + a.cmd + "{}",
			preview: `\` + a.cmd + "{x}", description: a.desc})
	}
	m.addTemplate(template{category: catAccents, name: `\overbrace`, insert: `\overbrace{}`,
		preview: `\overbrace{abc}`, description: "Brace over"})
	m.addTemplate(template{category: catAccents, name: `\underbrace`, insert: `\underbrace{}`,
		preview: `\underbrace{abc}`, description: "Brace under"})

	m.addTemplate(template{category: catScripts, name: "^{}", insert: "^{}", preview: "x^{2}",
		description: "Superscript", aliases: []string{"sup", "superscript", "power", "exponent"},
		fragment: true})
	m.addTemplate(template{category: catScripts, name: "_{}", insert: "_{}", preview: "x_{i}",
		description: "Subscript", aliases: []string{"sub", "subscript", "index"}, fragment: true})
	m.addTemplate(template{category: catScripts, name: "_{}^{}", insert: "_{}^{}",
		preview: "x_{i}^{2}", description: "Sub- and superscript",
		aliases: []string{"subsup"}, fragment: true})
	m.addTemplate(template{category: catScripts, name: `\overset`, insert: `\overset{}{}`,
		preview: `\overset{a}{=}`, description: "Symbol above another"})
	m.addTemplate(template{category: catScripts, name: `\underset`, insert: `\underset{}{}`,
		preview: `\underset{a}{=}`, description: "Symbol below another"})
	m.addTemplate(template{category: catScripts, name: `\stackrel`, insert: `\stackrel{}{}`,
		preview: `\stackrel{a}{=}`, description: "Stack above a relation"})
	m.addTemplate(template{category: catScripts, name: `\limits`, insert: `\limits`,
		preview: `\sum\limits_{i=0}^{n}`, description: "Force limits above/below", fragment: true})
	m.addTemplate(template{category: catScripts, name: `\nolimits`, insert: `\nolimits`,
		preview: `\int\nolimits_{0}^{1}`, description: "Force limits to the side", fragment: true})

	for _, f := range [...]struct{ cmd, sample, desc, alias string }{
		{"mathbb", "R", "Blackboard bold", "blackboard"},
		{"mathcal", "L", "Calligraphic", "calligraphic"},
		{"mathfrak", "g", "Fraktur", "fraktur"},
		{"mathrm", "d", "Upright roman", "roman"},
		{"mathbf", "v", "Bold", "bold"},
		{"mathit", "f", "Italic", "italic"},
		{"mathsf", "S", "Sans-serif", "sans"},
		{"mathtt", "t", "Typewriter", "monospace"},
		{"mathscr", "F", "Script", "script"},
	} {
		m.addTemplate(template{category: catFonts, name: `\` + f.cmd, insert: `\` + f.cmd + "{}",
			preview: `\` + f.cmd + "{" + f.sample + "}", description: f.desc,
			aliases: []string{f.alias}})
	}
	m.addTemplate(template{category: catFonts, name: `\boldsymbol`, insert: `\boldsymbol{}`,
		preview: `\boldsymbol{\alpha}`, description: "Bold symbol", aliases: []string{"bold greek"}})
	m.addTemplate(template{category: catFonts, name: `\text`, insert: `\text{}`,
		preview: `\text{text}`, description: "Upright text in math",
		aliases: []string{"words", "label"}})

	m.addSymbols(catFunctions, "sin", "cos", "tan", "cot", "sec", "csc",
		"arcsin", "arccos", "arctan", "sinh", "cosh", "tanh", "coth",
		"log", "ln", "lg", "exp", "lim", "limsup", "liminf", "max", "min",
		"sup", "inf", "det", "dim", "ker", "gcd", "hom", "arg", "deg", "Pr")
	m.addTemplate(template{category: catFunctions, name: `\operatorname`,
		insert: `\operatorname{}`, preview: `\operatorname{lcm}`,
		description: "Custom upright operator"})

	// Environments; in a display block they are inserted over several lines.
	for _, mx := range [...]struct{ env, desc string }{
		{"pmatrix", "Matrix in parentheses"},
		{"bmatrix", "Matrix in brackets"},
		{"Bmatrix", "Matrix in braces"},
		{"vmatrix", "Determinant bars"},
		{"Vmatrix", "Matrix in double bars"},
		{"matrix", "Matrix, no delimiters"},
		{"smallmatrix", "Small inline matrix"},
	} {
		begin, end := `\begin{`+mx.env+"}", `\end{`+mx.env+"}"
		m.addTemplate(template{category: catStructure, name: begin,
			insert: begin + ` & \\ & ` + end, preview: begin + `a&b\\c&d` + end,
			description: mx.desc, aliases: []string{mx.env, "matrix"},
			insertDisplay: begin + "\n & \\\\\n & \n" + end})
	}
	m.addTemplate(template{category: catStructure, name: `\begin{cases}`,
		insert: `\begin{cases} & \\ & \end{cases}`, preview: `\begin{cases}a&x>0\\b&x\le 0\end{cases}`,
		description: "Piecewise cases", aliases: []string{"cases", "piecewise"},
		insertDisplay: "\\begin{cases}\n & \\\\\n & \n\\end{cases}"})
	m.addTemplate(template{category: catStructure, name: `\begin{aligned}`,
		insert: `\begin{aligned} &= \\ &= \end{aligned}`, preview: `\begin{aligned}a&=b\\c&=d\end{aligned}`,
		description: "Aligned equations", aliases: []string{"aligned", "align"},
		insertDisplay: "\\begin{aligned}\n &= \\\\\n &= \n\\end{aligned}"})
	m.addTemplate(template{category: catStructure, name: `\begin{array}`,
		insert: `\begin{array}{cc} & \\ & \end{array}`, preview: `\begin{array}{cc}a&b\\c&d\end{array}`,
		description: "Array with column spec", aliases: []string{"array", "table"},
		insertDisplay: "\\begin{array}{cc}\n & \\\\\n & \n\\end{array}"})
	m.addTemplate(template{category: catStructure, name: `\\`, insert: `\\`,
		description: "New row / line break",
		aliases:     []string{`\`, "newline", "row", "linebreak"}, fragment: true})
	m.addTemplate(template{category: catStructure, name: "&", insert: "&",
		description: "Next cell / alignment point",
		aliases:     []string{"cell", "align", "ampersand"}, fragment: true})

	m.addSymbol(catMisc, "infty", "Infinity", "infinity")
	m.addSymbol(catMisc, "partial", "Partial derivative", "derivative")
	m.addSymbol(catMisc, "nabla", "Nabla / gradient", "del", "gradient")
	m.addSymbol(catMisc, "emptyset", "Empty set")
	m.addSymbol(catMisc, "neg", "Logical not", "lnot")
	m.addSymbols(catMisc, "cdots", "ldots", "vdots", "ddots", "forall", "exists", "nexists",
		"hbar", "ell", "aleph", "Re", "Im", "wp", "prime", "angle", "triangle",
		"top", "bot", "surd", "imath", "jmath")

	m.addTemplate(template{category: catSpacing, name: `\quad`, insert: `\quad`,
		preview: `a\quad b`, description: "Quad space", aliases: []string{"space"}})
	m.addTemplate(template{category: catSpacing, name: `\qquad`, insert: `\qquad`,
		preview: `a\qquad b`, description: "Double quad space"})
	m.addTemplate(template{category: catSpacing, name: `\,`, insert: `\,`, preview: `a\,b`,
		description: "Thin space", aliases: []string{"thinspace", "space"}, fragment: true})
	m.addTemplate(template{category: catSpacing, name: `\;`, insert: `\;`, preview: `a\;b`,
		description: "Thick space", aliases: []string{"thickspace"}, fragment: true})
	m.addTemplate(template{category: catSpacing, name: `\:`, insert: `\:`, preview: `a\:b`,
		description: "Medium space", aliases: []string{"medspace"}, fragment: true})
	m.addTemplate(template{category: catSpacing, name: `\!`, insert: `\!`, preview: `a\!b`,
		description: "Negative thin space", aliases: []string{"negspace"}, fragment: true})
	return m
}

// addSymbol adds a command inserted as itself, such as `\alpha`. An empty
// description shows the command's name.
func (m *Model) addSymbol(category, command, description string, aliases ...string) {
	if description == "" {
		description = command
	}
	name := `\` + command
	m.catalog = append(m.catalog, entry{name: name, command: command, category: category,
		description: description, insert: name, preview: name, aliases: aliases, standalone: true})
	m.curated[command] = true
}

func (m *Model) addSymbols(category string, commands ...string) {
	for _, c := range commands {
		m.addSymbol(category, c, "")
	}
}

// template is an entry inserted as something other than its name.
// fragment marks one that is not a formula on its own.
type template struct {
	category, name, insert, preview, description, insertDisplay string
	aliases                                                     []string
	fragment                                                    bool
}

func (m *Model) addTemplate(t template) {
	e := entry{name: t.name, category: t.category, description: t.description,
		insert: t.insert, insertDisplay: t.insertDisplay, preview: t.preview,
		aliases: t.aliases, standalone: !t.fragment}
	// The bare command the engine list may also hold: the letters after the
	// backslash (`\sqrt[n]` and `\sqrt{}` are both "sqrt"). A name without
	// a backslash ("^{}", "&") has none.
	if rest, ok := strings.CutPrefix(t.name, `\`); ok {
		n := 0
		for _, r := range rest {
			if !unicode.IsLetter(r) {
				break
			}
			n += len(string(r))
		}
		e.command = rest[:n]
	}
	m.catalog = append(m.catalog, e)
	if e.command != "" {
		m.curated[e.command] = true
	}
}

// engineCommands is the engine's command list, or nil.
func (m *Model) engineCommands() []string {
	if m.commands == nil {
		return nil
	}
	return m.commands()
}

// runeIndex is the rune offset of the first sub in s, or -1.
func runeIndex(s, sub string) int {
	i := strings.Index(s, sub)
	if i < 0 {
		return -1
	}
	return len([]rune(s[:i]))
}

// CaretOffset is where the caret goes in a freshly inserted template: inside
// its first empty {} or [] pair, else at its first alignment '&', else after
// the opening half of a \left…\right pair, else -1 for its end.
func CaretOffset(insert string) int {
	brace, bracket := runeIndex(insert, "{}"), runeIndex(insert, "[]")
	switch {
	case brace >= 0 && (bracket < 0 || brace < bracket):
		return brace + 1
	case bracket >= 0:
		return bracket + 1
	}
	if amp := runeIndex(insert, "&"); amp >= 0 {
		return amp
	}
	if right := runeIndex(insert, ` \right`); right >= 0 {
		return right + 1
	}
	return -1
}

func (e *entry) row() Entry {
	r := Entry{Name: e.name, Description: e.description, Category: e.category,
		Insert: e.insert, InsertDisplay: e.insertDisplay,
		CursorOffset: CaretOffset(e.insert), CursorOffsetDisplay: -1,
		Preview: e.preview, Standalone: e.standalone, Curated: true}
	if e.insertDisplay != "" {
		r.CursorOffsetDisplay = CaretOffset(e.insertDisplay)
	}
	return r
}

// enumeratedRow is a command the engine knows that the hand-picked list
// does not name: inserted as itself, drawn as itself, in no category.
func enumeratedRow(command string) Entry {
	name := `\` + command
	return Entry{Name: name, Insert: name, CursorOffset: -1, CursorOffsetDisplay: -1,
		Preview: name, Standalone: true}
}

// candidates are the words a query is matched against: the name without
// its backslash, the bare command when it differs, and the aliases.
func (e *entry) candidates() []string {
	bare := strings.TrimPrefix(e.name, `\`)
	out := []string{bare}
	if e.command != "" && e.command != bare {
		out = append(out, e.command)
	}
	return append(out, e.aliases...)
}

// Categories are the category names in the menu's order, led by
// RecentlyUsed once a command has been accepted.
func (m *Model) Categories() []string {
	var out []string
	if len(m.recent) > 0 {
		out = append(out, RecentlyUsed)
	}
	return append(out, m.categories...)
}

// ItemsForCategory are the entries of one category, in the list's order.
// For RecentlyUsed they are the recently accepted commands, most recent
// first; one the hand-picked list does not name is shown while the engine
// still knows it, and left out otherwise (a stale saved setting).
func (m *Model) ItemsForCategory(category string) []Entry {
	var rows []Entry
	if category == RecentlyUsed {
		known := m.engineCommands()
		for _, name := range m.recent {
			found := false
			for i := range m.catalog {
				if m.catalog[i].name == name {
					rows = append(rows, m.catalog[i].row())
					found = true
					break
				}
			}
			if bare, ok := strings.CutPrefix(name, `\`); !found && ok && slices.Contains(known, bare) {
				rows = append(rows, enumeratedRow(bare))
			}
		}
		return rows
	}
	for i := range m.catalog {
		if m.catalog[i].category == category {
			rows = append(rows, m.catalog[i].row())
		}
	}
	return rows
}

// How well a query matches a word; smaller is better.
const (
	prefixMatch = iota
	substringMatch
	subsequenceMatch
	noMatch
)

// quality is how well a query matches an entry: exactCase 0 when the case
// matches and 1 when it matches only ignoring case, then the kind of match.
// Case outranks the kind of match.
type quality struct{ exactCase, tier int }

func isSubsequence(needle, haystack []rune) bool {
	n := 0
	for h := 0; h < len(haystack) && n < len(needle); h++ {
		if haystack[h] == needle[n] {
			n++
		}
	}
	return n == len(needle)
}

func tierOf(candidate, query string, foldCase bool) int {
	if foldCase {
		candidate, query = strings.ToLower(candidate), strings.ToLower(query)
	}
	switch {
	case strings.HasPrefix(candidate, query):
		return prefixMatch
	case strings.Contains(candidate, query):
		return substringMatch
	case isSubsequence([]rune(query), []rune(candidate)):
		return subsequenceMatch
	}
	return noMatch
}

// bestQuality is the best match of a query among an entry's words: a match
// in the right case beats every match that ignores case.
func bestQuality(candidates []string, query string) quality {
	best := quality{exactCase: 1, tier: noMatch}
	for _, c := range candidates {
		exact := tierOf(c, query, false)
		if exact != noMatch && (best.exactCase > 0 || exact < best.tier) {
			best = quality{exactCase: 0, tier: exact}
			if exact == prefixMatch {
				return best // nothing beats a prefix in the right case
			}
			continue
		}
		if best.exactCase == 0 {
			continue // only a better match in the right case can win now
		}
		if loose := tierOf(c, query, true); loose < best.tier {
			best.tier = loose
		}
	}
	return best
}

// ItemsFor is the completion list for query, the text typed after the
// backslash (without it). An empty query lists everything, the hand-picked
// entries first. Otherwise matches are ranked: those in the query's case
// before those that match only ignoring case (TeX tells \omega from \Omega,
// and both are offered for "ome"); then a prefix before a match inside the
// word before a match of scattered letters; then hand-picked entries before
// the engine's other commands; then by name.
func (m *Model) ItemsFor(query string) []Entry {
	q := strings.TrimFunc(query, unicode.IsSpace)
	var rows []Entry
	if q == "" {
		for i := range m.catalog {
			rows = append(rows, m.catalog[i].row())
		}
		for _, c := range m.engineCommands() {
			if !m.curated[c] {
				rows = append(rows, enumeratedRow(c))
			}
		}
		return rows
	}

	type ranked struct {
		quality
		enumerated int // 0 hand-picked, 1 the engine's
		sortName   string
		row        Entry
	}
	var matches []ranked
	for i := range m.catalog {
		e := &m.catalog[i]
		if qu := bestQuality(e.candidates(), q); qu.tier != noMatch {
			matches = append(matches, ranked{qu, 0, strings.ToLower(e.name), e.row()})
		}
	}
	for _, c := range m.engineCommands() {
		if m.curated[c] {
			continue
		}
		if qu := bestQuality([]string{c}, q); qu.tier != noMatch {
			matches = append(matches, ranked{qu, 1, strings.ToLower(c), enumeratedRow(c)})
		}
	}
	slices.SortStableFunc(matches, func(a, b ranked) int {
		switch {
		case a.exactCase != b.exactCase:
			return a.exactCase - b.exactCase
		case a.tier != b.tier:
			return a.tier - b.tier
		case a.enumerated != b.enumerated:
			return a.enumerated - b.enumerated
		}
		return strings.Compare(a.sortName, b.sortName)
	})
	for _, r := range matches {
		rows = append(rows, r.row)
	}
	return rows
}

// NoteUsed records an accepted command by its name, such as `\frac`, at the
// front of the recently accepted ones, and calls OnRecentChanged.
func (m *Model) NoteUsed(name string) {
	if name == "" {
		return
	}
	m.recent = slices.DeleteFunc(m.recent, func(n string) bool { return n == name })
	m.recent = append([]string{name}, m.recent...)
	if len(m.recent) > MaxRecent {
		m.recent = m.recent[:MaxRecent]
	}
	if m.OnRecentChanged != nil {
		m.OnRecentChanged()
	}
}

// RecentCommands are the recently accepted commands' names, most recent
// first: what the app saves in the setting math.recentCommands.
func (m *Model) RecentCommands() []string { return slices.Clone(m.recent) }

// SetRecentCommands loads saved recently accepted commands, dropping empty
// and repeated names and keeping at most MaxRecent. It does not call
// OnRecentChanged, since loading saved state must not save it again.
func (m *Model) SetRecentCommands(names []string) {
	m.recent = nil
	for _, n := range names {
		if n == "" || slices.Contains(m.recent, n) {
			continue
		}
		m.recent = append(m.recent, n)
		if len(m.recent) == MaxRecent {
			break
		}
	}
}
