package export

// Front matter as the exporter handles it (src/content/notefrontmatter.cpp).
// A per-note Markdown export writes the note's front matter the way the
// app's index serializes it (NoteCollection::frontMatterFor), which is the
// canonical form: tags, created, pinned, favorite and goal in that order,
// then every other line as it was. The HTML and plain-text exports leave the
// front matter out.

import (
	"regexp"
	"strconv"
	"strings"
	"time"
)

type fmSplit struct {
	block   string // the block with both fence lines, or ""
	body    string
	present bool
}

func fmLines(text string) []string {
	if text == "" {
		return nil
	}
	lines := strings.Split(text, "\n")
	if strings.HasSuffix(text, "\n") {
		lines = lines[:len(lines)-1]
	}
	for i, l := range lines {
		lines[i] = strings.TrimSuffix(l, "\r")
	}
	return lines
}

func isBlankLine(l string) bool   { return trimSpace(l) == "" }
func isCommentLine(l string) bool { return strings.HasPrefix(trimSpace(l), "#") }

func isListItemLine(l string) bool {
	t := trimSpace(l)
	return strings.HasPrefix(t, "- ") || t == "-"
}

func isContinuationLine(l string) bool {
	return l != "" && (l[0] == ' ' || l[0] == '\t')
}

// splitKeyLine reads "key: value" or "key:" with the key at column 0.
func splitKeyLine(l string) (key, value string, ok bool) {
	if l == "" || isSpace([]rune(l)[0]) {
		return "", "", false
	}
	colon := strings.IndexByte(l, ':')
	if colon <= 0 {
		return "", "", false
	}
	key = l[:colon]
	if strings.IndexFunc(key, isSpace) >= 0 {
		return "", "", false
	}
	if rest := l[colon+1:]; rest != "" && !isSpace([]rune(rest)[0]) {
		return "", "", false
	}
	return key, trimSpace(l[colon+1:]), true
}

func isMappingShaped(l string) bool {
	_, _, key := splitKeyLine(l)
	return isBlankLine(l) || isCommentLine(l) || isListItemLine(l) || isContinuationLine(l) || key
}

// splitFrontMatter is NoteFrontMatter::split: the block and the body, byte
// for byte. A block needs a closing fence, only mapping-shaped lines inside,
// and at least one key, so a note that starts with a divider has none.
func splitFrontMatter(text string) fmSplit {
	res := fmSplit{body: text}
	if !strings.HasPrefix(text, "---") {
		return res
	}
	first, _, hasNL := strings.Cut(text, "\n")
	if strings.TrimSuffix(first, "\r") != "---" || !hasNL {
		return res
	}
	pos := len(first) + 1
	hasKey := false
	for pos < len(text) {
		end := strings.IndexByte(text[pos:], '\n')
		next := len(text)
		if end < 0 {
			end = len(text)
		} else {
			end += pos
			next = end + 1
		}
		line := strings.TrimSuffix(text[pos:end], "\r")
		if line == "---" {
			res.block, res.body, res.present = text[:next], text[next:], hasKey
			if !hasKey {
				res.block, res.body = "", text
			}
			return res
		}
		if !isMappingShaped(line) {
			return res
		}
		if _, _, ok := splitKeyLine(line); ok {
			hasKey = true
		}
		pos = next
	}
	return res
}

type fmMeta struct {
	tags     []string
	created  string // already in the form  writes it; "" when unset
	pinned   bool
	favorite bool
	goal     int
	unknown  []string
}

func unescapeDoubleQuoted(s string) string {
	var sb strings.Builder
	for i := 0; i < len(s); i++ {
		if s[i] == '\\' && i+1 < len(s) && (s[i+1] == '\\' || s[i+1] == '"') {
			i++
		}
		sb.WriteByte(s[i])
	}
	return sb.String()
}

func stripMatchingQuotes(s string) string {
	if len(s) >= 2 {
		if s[0] == '"' && s[len(s)-1] == '"' {
			return unescapeDoubleQuoted(s[1 : len(s)-1])
		}
		if s[0] == '\'' && s[len(s)-1] == '\'' {
			return s[1 : len(s)-1]
		}
	}
	return s
}

func parseTagsValue(value string) ([]string, bool) {
	var tags []string
	if value == "" {
		return tags, true
	}
	if strings.HasPrefix(value, "[") && strings.HasSuffix(value, "]") {
		inner := []rune(value[1 : len(value)-1])
		var parts []string
		var part []rune
		var quote rune
		for i := 0; i < len(inner); i++ {
			c := inner[i]
			switch {
			case quote != 0:
				if c == '\\' && quote == '"' && i+1 < len(inner) {
					part = append(part, c, inner[i+1])
					i++
					continue
				}
				if c == quote {
					quote = 0
				}
				part = append(part, c)
			case c == '"' || c == '\'':
				quote = c
				part = append(part, c)
			case c == ',':
				parts = append(parts, string(part))
				part = nil
			default:
				part = append(part, c)
			}
		}
		parts = append(parts, string(part))
		for _, p := range parts {
			if tag := stripMatchingQuotes(trimSpace(p)); tag != "" {
				tags = append(tags, tag)
			}
		}
		return tags, true
	}
	if strings.HasPrefix(value, "[") {
		return nil, false
	}
	if tag := stripMatchingQuotes(value); tag != "" {
		tags = append(tags, tag)
	}
	return tags, true
}

// reISODate is the ISO 8601 form the ::ISODate reads: a date, optionally
// a time, optionally a zone.
var reISODate = regexp.MustCompile(`^(\d{4})-(\d{2})-(\d{2})(?:[T ](\d{2}):(\d{2})(?::(\d{2})(?:[.,]\d+)?)?(Z|[+-]\d{2}(?::?\d{2})?)?)?$`)

// isoCreated reads a created value and writes it back the way
// date-time::toString(::ISODate) does: seconds always, no fraction, "Z"
// for UTC, the offset when one was given, nothing for local time. A date
// alone is the start of that day.
func isoCreated(raw string) (string, bool) {
	m := reISODate.FindStringSubmatch(stripMatchingQuotes(raw))
	if m == nil {
		return "", false
	}
	num := func(s string) int { n, _ := strconv.Atoi(s); return n }
	y, mo, d := num(m[1]), num(m[2]), num(m[3])
	hh, mm, ss := 0, 0, 0
	if m[4] != "" {
		hh, mm = num(m[4]), num(m[5])
		if m[6] != "" {
			ss = num(m[6])
		}
	}
	// 24:00:00 is the end of the day, which  reads as midnight of the next.
	endOfDay := hh == 24 && mm == 0 && ss == 0
	if endOfDay {
		hh = 0
	}
	t := time.Date(y, time.Month(mo), d, hh, mm, ss, 0, time.UTC)
	if t.Year() != y || int(t.Month()) != mo || t.Day() != d || t.Hour() != hh || t.Minute() != mm || t.Second() != ss {
		return "", false
	}
	if endOfDay {
		t = t.AddDate(0, 0, 1)
	}
	out := t.Format("2006-01-02T15:04:05")
	switch zone := m[7]; {
	case zone == "Z":
		out += "Z"
	case zone != "":
		digits := strings.ReplaceAll(zone[1:], ":", "")
		oh, om := num(digits[:2]), 0
		if len(digits) >= 4 {
			om = num(digits[2:4])
		}
		if oh == 0 && om == 0 {
			// reads a zero offset as UTC.
			out += "Z"
		} else {
			out += zone[:1] + digits[:2] + ":" + strconv.Itoa(100 + om)[1:]
		}
	}
	return out, true
}

// parseFrontMatter is NoteFrontMatter::parse over a block with its fences.
func parseFrontMatter(block string) fmMeta {
	var meta fmMeta
	lines := fmLines(block)
	if len(lines) > 0 && lines[0] == "---" {
		lines = lines[1:]
	}
	if len(lines) > 0 && lines[len(lines)-1] == "---" {
		lines = lines[:len(lines)-1]
	}
	for i := 0; i < len(lines); i++ {
		text := lines[i]
		key, value, ok := splitKeyLine(text)
		if !ok {
			meta.unknown = append(meta.unknown, text)
			continue
		}
		switch key {
		case "tags":
			tags, ok := parseTagsValue(value)
			if !ok {
				meta.unknown = append(meta.unknown, text)
				continue
			}
			for value == "" && i+1 < len(lines) && isListItemLine(lines[i+1]) {
				item := trimSpace(trimSpace(lines[i+1])[1:])
				if tag := stripMatchingQuotes(item); tag != "" {
					tags = append(tags, tag)
				}
				i++
			}
			meta.tags = tags
		case "created":
			if c, ok := isoCreated(value); ok {
				meta.created = c
			} else {
				meta.unknown = append(meta.unknown, text)
			}
		case "pinned", "favorite":
			var b bool
			switch strings.ToLower(value) {
			case "true":
				b = true
			case "false":
			default:
				meta.unknown = append(meta.unknown, text)
				continue
			}
			if key == "pinned" {
				meta.pinned = b
			} else {
				meta.favorite = b
			}
		case "goal":
			if g, err := strconv.Atoi(trimSpace(value)); err == nil && g > 0 {
				meta.goal = g
			} else {
				meta.unknown = append(meta.unknown, text)
			}
		default:
			meta.unknown = append(meta.unknown, text)
			for i+1 < len(lines) && (isListItemLine(lines[i+1]) || isContinuationLine(lines[i+1])) {
				meta.unknown = append(meta.unknown, lines[i+1])
				i++
			}
		}
	}
	return meta
}

func serializeTag(tag string) string {
	quote := tag != trimSpace(tag) || strings.ContainsAny(tag, ",[]#:'\"\\")
	if !quote {
		return tag
	}
	var sb strings.Builder
	sb.WriteByte('"')
	for _, c := range tag {
		if c == '\\' || c == '"' {
			sb.WriteByte('\\')
		}
		sb.WriteRune(c)
	}
	sb.WriteByte('"')
	return sb.String()
}

// serializeFrontMatter is NoteFrontMatter::serialize: "" when there is
// nothing to write.
func serializeFrontMatter(meta fmMeta) string {
	var lines []string
	if len(meta.tags) > 0 {
		quoted := make([]string, len(meta.tags))
		for i, t := range meta.tags {
			quoted[i] = serializeTag(t)
		}
		lines = append(lines, "tags: ["+strings.Join(quoted, ", ")+"]")
	}
	if meta.created != "" {
		lines = append(lines, "created: "+meta.created)
	}
	if meta.pinned {
		lines = append(lines, "pinned: true")
	}
	if meta.favorite {
		lines = append(lines, "favorite: true")
	}
	if meta.goal > 0 {
		lines = append(lines, "goal: "+strconv.Itoa(meta.goal))
	}
	lines = append(lines, meta.unknown...)
	if len(lines) == 0 {
		return ""
	}
	return "---\n" + strings.Join(lines, "\n") + "\n---\n"
}

// SplitNote separates a note file's front matter from its body by the
// app's rule (NoteFrontMatter::split). The front matter is returned with its
// "---" lines, byte for byte, and is "" when the note has none; front matter
// plus body is always the text passed in.
func SplitNote(text string) (frontMatter, body string) {
	s := splitFrontMatter(text)
	return s.block, s.body
}

// CanonicalFrontMatter is a note's front matter as the app writes it when
// it exports the note as Markdown (NoteCollection::frontMatterFor): the keys
// Kvit knows in its own order and form, then every other line unchanged, or
// "" when nothing is left to write.
func CanonicalFrontMatter(frontMatter string) string {
	if frontMatter == "" {
		return ""
	}
	return serializeFrontMatter(parseFrontMatter(frontMatter))
}
