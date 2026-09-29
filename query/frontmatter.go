package query

// Reading a front matter value as a query sees it. Note.Fields holds each
// key's value as written in the file; these helpers are the typed accessors
// the query code calls on it, from src/content/notefrontmatter.cpp
// (NoteFrontMatter::Metadata::fieldString and fieldList, with the quote
// handling they share with the tags parser).

import "strings"

// fieldString is Metadata::fieldString: the trimmed value with one matching
// pair of outer quotes removed.
func fieldString(raw string) string {
	return stripMatchingQuotes(strings.TrimSpace(raw))
}

// fieldList is Metadata::fieldList: the items of a value written as a YAML
// inline list ("[a, "b, c"]"), or of a plain value split at commas ("a, b").
// Each item is trimmed and unquoted, and empty items are dropped. A value
// that opens a list without closing it has no items.
func fieldList(raw string) []string {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return nil
	}
	if strings.HasPrefix(raw, "[") {
		items, ok := parseInlineList(raw)
		if !ok {
			return nil
		}
		return items
	}
	var out []string
	for _, part := range strings.Split(raw, ",") {
		if item := stripMatchingQuotes(strings.TrimSpace(part)); item != "" {
			out = append(out, item)
		}
	}
	return out
}

// parseInlineList is the "[...]" branch of NoteFrontMatter::parseTagsValue.
// Commas split items except inside a quoted item, and inside double quotes a
// backslash keeps the character after it in the item, so an escaped quote
// does not end the quoted run. It fails when the value does not end with
// "]".
func parseInlineList(value string) ([]string, bool) {
	if !strings.HasPrefix(value, "[") || !strings.HasSuffix(value, "]") || len(value) < 2 {
		return nil, false
	}
	inner := value[1 : len(value)-1]
	var parts []string
	var part strings.Builder
	var quote byte // 0 outside a quoted run
	for i := 0; i < len(inner); i++ {
		c := inner[i]
		switch {
		case quote != 0:
			if c == '\\' && quote == '"' && i+1 < len(inner) {
				part.WriteByte(c)
				i++
				part.WriteByte(inner[i])
				continue
			}
			if c == quote {
				quote = 0
			}
			part.WriteByte(c)
		case c == '"' || c == '\'':
			quote = c
			part.WriteByte(c)
		case c == ',':
			parts = append(parts, part.String())
			part.Reset()
		default:
			part.WriteByte(c)
		}
	}
	parts = append(parts, part.String())
	var items []string
	for _, p := range parts {
		if item := stripMatchingQuotes(strings.TrimSpace(p)); item != "" {
			items = append(items, item)
		}
	}
	return items, true
}

// stripMatchingQuotes removes one pair of matching outer quotes. Inside
// double quotes it also undoes the two escapes Kvit writes there, \\ and \";
// any other backslash stays as written.
func stripMatchingQuotes(s string) string {
	if len(s) >= 2 {
		first, last := s[0], s[len(s)-1]
		if first == '"' && last == '"' {
			return unescapeDoubleQuoted(s[1 : len(s)-1])
		}
		if first == '\'' && last == '\'' {
			return s[1 : len(s)-1]
		}
	}
	return s
}

func unescapeDoubleQuoted(s string) string {
	if !strings.Contains(s, `\`) {
		return s
	}
	var out strings.Builder
	for i := 0; i < len(s); i++ {
		c := s[i]
		if c == '\\' && i+1 < len(s) && (s[i+1] == '\\' || s[i+1] == '"') {
			out.WriteByte(s[i+1])
			i++
			continue
		}
		out.WriteByte(c)
	}
	return out.String()
}
