package export

// The part of CSS the HTML converter needs. QTextDocument applies a page's
// <style> rules as well as each element's style attribute, and what they say
// reaches the Markdown: a rule can make text bold, italic, struck through or
// monospace, keep its spaces, or give a block margins on both sides, which the
// Qt converter reads as a quote. So the rules are read here too: type, class,
// id and attribute selectors, joined by descendant and child combinators, in
// order of specificity, with @media rules for the screen. Pseudo-classes never
// match, as nothing is hovered in a document being converted.

import (
	"regexp"
	"slices"
	"strings"
)

type cssAttrSel struct {
	name, value string
	hasValue    bool
}

// cssCompound is one compound selector: p.note#x[title].
type cssCompound struct {
	tag     string // "" or "*" for any element
	id      string
	classes []string
	attrs   []cssAttrSel
	never   bool // uses something that cannot match here
}

type cssRule struct {
	parts []cssCompound // left to right
	child []bool        // child[i]: parts[i] is a child, not a descendant, of parts[i-1]
	decls string
	spec  int
	order int
}

var reCSSComment = regexp.MustCompile(`(?s)/\*.*?\*/`)

// parseStyleSheet reads the rules of a <style> element. The "<!--" and
// "-->" older pages wrap a style sheet in are not part of it.
func parseStyleSheet(src string, order *int) []cssRule {
	src = strings.NewReplacer("<!--", " ", "-->", " ").Replace(reCSSComment.ReplaceAllString(src, ""))
	return parseCSSRules(src, order)
}

func parseCSSRules(s string, order *int) []cssRule {
	var rules []cssRule
	for {
		s = strings.TrimSpace(s)
		if s == "" {
			return rules
		}
		if s[0] == '@' {
			brace := strings.IndexByte(s, '{')
			semi := strings.IndexByte(s, ';')
			if brace < 0 || (semi >= 0 && semi < brace) {
				if semi < 0 {
					return rules
				}
				s = s[semi+1:]
				continue
			}
			end := matchingBrace(s, brace)
			prelude := strings.ToLower(s[:brace])
			if strings.HasPrefix(prelude, "@media") && (strings.Contains(prelude, "screen") || strings.Contains(prelude, "all")) {
				rules = append(rules, parseCSSRules(s[brace+1:end], order)...)
			}
			if end >= len(s) {
				return rules
			}
			s = s[end+1:]
			continue
		}
		brace := strings.IndexByte(s, '{')
		if brace < 0 {
			return rules
		}
		closeAt := strings.IndexByte(s[brace:], '}')
		if closeAt < 0 {
			closeAt = len(s) - brace
		}
		decls := s[brace+1 : brace+closeAt]
		for _, sel := range strings.Split(s[:brace], ",") {
			if r, ok := parseSelector(sel); ok {
				r.decls = decls
				r.order = *order
				*order++
				rules = append(rules, r)
			}
		}
		if brace+closeAt >= len(s) {
			return rules
		}
		s = s[brace+closeAt+1:]
	}
}

func matchingBrace(s string, open int) int {
	depth := 0
	for i := open; i < len(s); i++ {
		switch s[i] {
		case '{':
			depth++
		case '}':
			depth--
			if depth == 0 {
				return i
			}
		}
	}
	return len(s)
}

func parseSelector(sel string) (cssRule, bool) {
	var r cssRule
	sel = strings.TrimSpace(strings.ReplaceAll(sel, ">", " > "))
	if sel == "" {
		return r, false
	}
	nextChild := false
	ids, classes, types := 0, 0, 0
	for _, tok := range strings.Fields(sel) {
		if tok == ">" {
			nextChild = true
			continue
		}
		c := parseCompound(tok)
		if c.tag != "" && c.tag != "*" {
			types++
		}
		if c.id != "" {
			ids++
		}
		classes += len(c.classes) + len(c.attrs)
		r.parts = append(r.parts, c)
		r.child = append(r.child, nextChild)
		nextChild = false
	}
	if len(r.parts) == 0 {
		return r, false
	}
	r.spec = ids*10000 + classes*100 + types
	return r, true
}

func isIdentByte(b byte) bool {
	return b == '-' || b == '_' || b >= '0' && b <= '9' || b >= 'a' && b <= 'z' || b >= 'A' && b <= 'Z' || b >= 0x80
}

func parseCompound(tok string) cssCompound {
	var c cssCompound
	i := 0
	ident := func() string {
		start := i
		for i < len(tok) && isIdentByte(tok[i]) {
			i++
		}
		return tok[start:i]
	}
	if i < len(tok) && tok[i] == '*' {
		c.tag = "*"
		i++
	} else {
		c.tag = strings.ToLower(ident())
	}
	for i < len(tok) {
		switch tok[i] {
		case '.':
			i++
			c.classes = append(c.classes, ident())
		case '#':
			i++
			c.id = ident()
		case '[':
			end := strings.IndexByte(tok[i:], ']')
			if end < 0 {
				c.never = true
				return c
			}
			inner := tok[i+1 : i+end]
			i += end + 1
			name, value, has := strings.Cut(inner, "=")
			if strings.ContainsAny(name, "~|^$*") {
				c.never = true
			}
			c.attrs = append(c.attrs, cssAttrSel{strings.ToLower(strings.TrimSpace(name)), strings.Trim(value, `"'`), has})
		default:
			// A pseudo-class, a sibling combinator or anything else this
			// reader does not follow.
			c.never = true
			return c
		}
	}
	return c
}

func (c *cssCompound) matches(e *helem) bool {
	if c.never || (c.tag != "" && c.tag != "*" && c.tag != e.name) {
		return false
	}
	if c.id != "" && e.attrs["id"] != c.id {
		return false
	}
	classes := strings.Fields(e.attrs["class"])
	for _, cl := range c.classes {
		if !slices.Contains(classes, cl) {
			return false
		}
	}
	for _, a := range c.attrs {
		v, ok := e.attrs[a.name]
		if !ok || (a.hasValue && v != a.value) {
			return false
		}
	}
	return true
}

// matches reports whether the rule selects e, whose ancestors are path,
// the nearest last.
func (r *cssRule) matches(e *helem, path []*helem) bool {
	last := len(r.parts) - 1
	if !r.parts[last].matches(e) {
		return false
	}
	return r.matchUp(last, path)
}

// matchUp matches parts[:k] against the ancestors in path, given that
// parts[k] matched the element just below path.
func (r *cssRule) matchUp(k int, path []*helem) bool {
	if k == 0 {
		return true
	}
	if r.child[k] {
		if len(path) == 0 || !r.parts[k-1].matches(path[len(path)-1]) {
			return false
		}
		return r.matchUp(k-1, path[:len(path)-1])
	}
	for i := len(path) - 1; i >= 0; i-- {
		if r.parts[k-1].matches(path[i]) && r.matchUp(k-1, path[:i]) {
			return true
		}
	}
	return false
}

// matchingRules are the rules that select e, weakest first, so applying them
// in order leaves the strongest in force.
func matchingRules(rules []cssRule, e *helem, path []*helem) []*cssRule {
	var out []*cssRule
	for i := range rules {
		if rules[i].matches(e, path) {
			out = append(out, &rules[i])
		}
	}
	slices.SortStableFunc(out, func(a, b *cssRule) int {
		if a.spec != b.spec {
			return a.spec - b.spec
		}
		return a.order - b.order
	})
	return out
}
