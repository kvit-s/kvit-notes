package ignore

import (
	"regexp"
	"strings"
	"unicode/utf8"
)

// The translation of one gitignore line into a regular expression.

// rule is one compiled line of an ignore file or of the settings list.
type rule struct {
	// expression matches a path relative to the folder the rule belongs
	// to, either the path itself or one of its ancestors followed by "/".
	expression *regexp.Regexp
	// negated is set for a line starting with "!", which includes again
	// what an earlier line excluded.
	negated bool
	// directoryOnly is set for a line ending in "/", which matches only a
	// folder and what is below it.
	directoryOnly bool
	// descendantCapture is the number of the group that captures what
	// follows the match: "" at the end of the path, "/" when the matched
	// name is a folder above the path.
	descendantCapture int
}

// ruleGroup is the rules of one file, applied to the paths below baseDir.
// baseDir is "" for the root folder, git's exclude file and the settings
// list.
type ruleGroup struct {
	baseDir string
	rules   []rule
}

// matches reports whether the rule matches p, a path relative to the rule's
// folder.
func (r rule) matches(p string, isDirectory bool) bool {
	if !r.directoryOnly {
		return r.expression.MatchString(p)
	}
	m := r.expression.FindStringSubmatchIndex(p)
	if m == nil {
		return false
	}
	// A slash after the match means the matched folder is an ancestor of
	// this path. A match to the end of the path is a folder only when the
	// caller says the entry itself is a folder.
	suffix := p[m[2*r.descendantCapture]:m[2*r.descendantCapture+1]]
	return suffix == "/" || isDirectory
}

// compilePatterns compiles each line and keeps the ones that are rules. Blank
// lines, comments and lines whose expression does not compile are dropped.
func compilePatterns(patterns []string, baseDir string) ruleGroup {
	group := ruleGroup{baseDir: cleanRelative(baseDir)}
	for _, pattern := range patterns {
		if r, ok := compileRule(pattern); ok {
			group.rules = append(group.rules, r)
		}
	}
	return group
}

// markerIsEscaped reports whether the character at byte offset at follows
// an odd number of backslashes, which makes it literal.
func markerIsEscaped(text string, at int) bool {
	slashes := 0
	for i := at - 1; i >= 0 && text[i] == '\\'; i-- {
		slashes++
	}
	return slashes%2 != 0
}

// globRegularExpression translates the body of a gitignore pattern into a
// regular expression. "*" matches within one path segment, "**" across
// segments, and "**/" any number of leading folders including none. "?"
// matches one character other than "/". "[...]" is copied as a character
// class, with a leading "!" turned into "^". A backslash makes the next
// character literal, and every other character is literal.
//
// Literal characters are written with regexp.QuoteMeta, which escapes only
// the characters that are special. A backslash before every character but
// ASCII letters, digits and "_" would match the same text, but Go's regexp
// rejects a backslash before a letter outside ASCII.
func globRegularExpression(glob string) string {
	var result strings.Builder
	result.Grow(len(glob) * 2)
	for i := 0; i < len(glob); i++ {
		c := glob[i]
		switch {
		case c == '*':
			if i+1 < len(glob) && glob[i+1] == '*' {
				for i+1 < len(glob) && glob[i+1] == '*' {
					i++
				}
				if i+1 < len(glob) && glob[i+1] == '/' {
					i++
					result.WriteString("(?:.*/)?")
				} else {
					result.WriteString(".*")
				}
			} else {
				result.WriteString("[^/]*")
			}
		case c == '?':
			result.WriteString("[^/]")
		case c == '[':
			// The class ends at the first "]" after the "[", and needs at
			// least one character in it; otherwise the "[" is literal.
			if end := strings.IndexByte(glob[i+1:], ']'); end > 0 {
				klass := glob[i+1 : i+1+end]
				if strings.HasPrefix(klass, "!") {
					klass = "^" + klass[1:]
				}
				result.WriteString("[" + klass + "]")
				i += 1 + end
			} else {
				result.WriteString(`\[`)
			}
		case c == '\\' && i+1 < len(glob):
			_, size := utf8.DecodeRuneInString(glob[i+1:])
			result.WriteString(regexp.QuoteMeta(glob[i+1 : i+1+size]))
			i += size
		default:
			_, size := utf8.DecodeRuneInString(glob[i:])
			result.WriteString(regexp.QuoteMeta(glob[i : i+size]))
			i += size - 1
		}
	}
	return result.String()
}

// compileRule compiles one line of an ignore file. It reports false for a
// line that is not a rule: blank, a comment, nothing left after "!" or "/",
// or an expression that does not compile.
func compileRule(pattern string) (rule, bool) {
	var r rule

	pattern = strings.TrimSuffix(pattern, "\r")
	// Git discards unescaped trailing spaces. Escaped spaces are literal and
	// lose only their escaping backslash, which globRegularExpression
	// removes.
	for strings.HasSuffix(pattern, " ") && !markerIsEscaped(pattern, len(pattern)-1) {
		pattern = pattern[:len(pattern)-1]
	}
	if pattern == "" || strings.HasPrefix(pattern, "#") {
		return r, false
	}
	if strings.HasPrefix(pattern, `\#`) {
		pattern = pattern[1:]
	}

	if strings.HasPrefix(pattern, "!") {
		r.negated = true
		pattern = pattern[1:]
	} else if strings.HasPrefix(pattern, `\!`) {
		pattern = pattern[1:]
	}
	if pattern == "" {
		return r, false
	}

	if strings.HasSuffix(pattern, "/") && !markerIsEscaped(pattern, len(pattern)-1) {
		r.directoryOnly = true
		pattern = pattern[:len(pattern)-1]
	}
	anchored := strings.HasPrefix(pattern, "/")
	if anchored {
		pattern = pattern[1:]
	}
	if pattern == "" {
		return r, false
	}

	// A pattern with a slash in it matches from the rule's folder; one
	// without matches a name at any depth below it.
	pathPattern := anchored || strings.Contains(pattern, "/")
	body := globRegularExpression(pattern)
	var expression string
	if pathPattern {
		expression = "^" + body + "($|/)"
		r.descendantCapture = 1
	} else {
		expression = "(^|/)" + body + "($|/)"
		r.descendantCapture = 2
	}
	re, err := regexp.Compile(expression)
	if err != nil {
		return r, false
	}
	r.expression = re
	return r, true
}

// pathBelowBase returns relativePath relative to baseDir, or "" when it is
// baseDir itself or not below it.
func pathBelowBase(relativePath, baseDir string) string {
	if baseDir == "" {
		return relativePath
	}
	if relativePath == baseDir {
		return ""
	}
	prefix := baseDir + "/"
	if strings.HasPrefix(relativePath, prefix) {
		return relativePath[len(prefix):]
	}
	return ""
}
