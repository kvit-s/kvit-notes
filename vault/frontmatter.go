package vault

// A note's front matter: the YAML block between "---" lines at the top of a
// note, where Kvit keeps a note's tags, creation date, pinned and favourite
// marks and writing goal (src/content/notefrontmatter.cpp). Other tools keep
// their own keys there. The Qt app rebuilds the block whenever it writes a
// key; this one is more careful: the block is kept as its lines, and a change
// rewrites only the key it changes, in the form the Qt app writes, so
// everything else comes back byte for byte and the Qt app reads the result
// as it would its own.

import (
	"regexp"
	"strconv"
	"strings"
	"time"
)

// frontMatter is a front matter block as its lines, without the "---"
// delimiters.
type frontMatter struct {
	lines []string
}

// reKeyLine is a mapping key at the start of a line: no whitespace in the
// key, and the colon followed by whitespace or the end of the line.
var reKeyLine = regexp.MustCompile(`^[^\s:#-][^\s:]*:(\s|$)`)

// mappingLine reports whether a line can be part of front matter, and
// whether it is a key line: Kvit takes a block as front matter only when
// every line is blank, a comment, a list item, an indented continuation or a
// key, and at least one is a key. A note that starts with a divider is not
// front matter.
func mappingLine(l string) (ok, key bool) {
	t := strings.TrimSpace(l)
	switch {
	case t == "":
		return true, false
	case strings.HasPrefix(t, "#"):
		return true, false
	case t == "-" || strings.HasPrefix(t, "- "):
		return true, false
	case strings.HasPrefix(l, " ") || strings.HasPrefix(l, "\t"):
		return true, false
	case reKeyLine.MatchString(l):
		return true, true
	}
	return false, false
}

// splitFrontMatter separates a note's front matter from its body. A note
// without front matter has none, and its body is the whole text.
func splitFrontMatter(text string) (fm *frontMatter, body string) {
	if !strings.HasPrefix(text, "---\n") && !strings.HasPrefix(text, "---\r\n") {
		return nil, text
	}
	rest := text[strings.Index(text, "\n")+1:]
	var lines []string
	keys := 0
	for {
		nl := strings.Index(rest, "\n")
		line := rest
		if nl >= 0 {
			line = rest[:nl]
		}
		line = strings.TrimRight(line, "\r")
		if line == "---" {
			if keys == 0 {
				return nil, text
			}
			if nl < 0 {
				return &frontMatter{lines: lines}, ""
			}
			return &frontMatter{lines: lines}, rest[nl+1:]
		}
		ok, key := mappingLine(line)
		if !ok || nl < 0 {
			return nil, text
		}
		if key {
			keys++
		}
		lines = append(lines, line)
		rest = rest[nl+1:]
	}
}

// text writes the block back with its delimiters.
func (f *frontMatter) text() string {
	return "---\n" + strings.Join(f.lines, "\n") + "\n---\n"
}

// find is the index of the line holding a top-level key, and the end of its
// value (the list and continuation lines below it), or -1.
func (f *frontMatter) find(key string) (at, end int) {
	at, end = -1, -1
	for i, l := range f.lines {
		if _, isKey := mappingLine(l); !isKey {
			continue
		}
		k, _, _ := strings.Cut(l, ":")
		if k != key {
			continue
		}
		// The last one wins, as in the Qt app.
		at, end = i, i+1
		for end < len(f.lines) {
			if _, isKey := mappingLine(f.lines[end]); isKey {
				break
			}
			t := strings.TrimSpace(f.lines[end])
			if t == "" || strings.HasPrefix(t, "#") && !strings.HasPrefix(f.lines[end], " ") {
				break
			}
			end++
		}
	}
	return at, end
}

// get is a scalar key's value, unquoted, and whether it is there.
func (f *frontMatter) get(key string) (string, bool) {
	at, _ := f.find(key)
	if at < 0 {
		return "", false
	}
	_, v, _ := strings.Cut(f.lines[at], ":")
	return unquote(strings.TrimSpace(v)), true
}

// list is a key's list value, written either inline ([a, "b,c"]) or as a
// block of "- item" lines, or a single scalar as a one-item list.
func (f *frontMatter) list(key string) []string {
	at, end := f.find(key)
	if at < 0 {
		return nil
	}
	_, v, _ := strings.Cut(f.lines[at], ":")
	v = strings.TrimSpace(v)
	var out []string
	switch {
	case strings.HasPrefix(v, "[") && strings.HasSuffix(v, "]"):
		for _, item := range splitFlow(v[1 : len(v)-1]) {
			if item = unquote(strings.TrimSpace(item)); item != "" {
				out = append(out, item)
			}
		}
	case v != "":
		out = append(out, unquote(v))
	default:
		for _, l := range f.lines[at+1 : end] {
			if item, ok := strings.CutPrefix(strings.TrimSpace(l), "- "); ok {
				out = append(out, unquote(strings.TrimSpace(item)))
			}
		}
	}
	return out
}

// splitFlow splits a flow list's inside at the commas outside quotes.
func splitFlow(s string) []string {
	var out []string
	var quote rune
	start := 0
	for i, r := range s {
		switch {
		case quote != 0 && r == '\\' && quote == '"':
			continue
		case quote != 0 && r == quote && (i == 0 || s[i-1] != '\\'):
			quote = 0
		case quote == 0 && (r == '"' || r == '\''):
			quote = r
		case quote == 0 && r == ',':
			out = append(out, s[start:i])
			start = i + 1
		}
	}
	return append(out, s[start:])
}

// knownKeys are the keys Kvit writes, in the order it writes them.
var knownKeys = []string{"tags", "created", "pinned", "favorite", "goal"}

// set writes a key's line in place, or adds it among Kvit's own keys in the
// Qt app's order; an empty value removes the key.
func (f *frontMatter) set(key, line string) {
	at, end := f.find(key)
	if line == "" {
		if at >= 0 {
			f.lines = append(f.lines[:at], f.lines[end:]...)
		}
		return
	}
	if at >= 0 {
		f.lines = append(f.lines[:at], append([]string{line}, f.lines[end:]...)...)
		return
	}
	insert := 0
	for _, k := range knownKeys {
		if k == key {
			break
		}
		if _, e := f.find(k); e > insert {
			insert = e
		}
	}
	f.lines = append(f.lines[:insert], append([]string{line}, f.lines[insert:]...)...)
}

// quoteTag writes a tag as the Qt app writes one in a flow list: quoted when
// it holds a character a flow list would read differently, or has space
// around it.
func quoteTag(t string) string {
	if strings.ContainsAny(t, ",[]#:'\"\\") || strings.TrimSpace(t) != t {
		return `"` + strings.ReplaceAll(strings.ReplaceAll(t, `\`, `\\`), `"`, `\"`) + `"`
	}
	return t
}

func unquote(s string) string {
	if len(s) >= 2 && s[0] == '"' && s[len(s)-1] == '"' {
		var b strings.Builder
		for i := 1; i < len(s)-1; i++ {
			if s[i] == '\\' && i+1 < len(s)-1 {
				i++
			}
			b.WriteByte(s[i])
		}
		return b.String()
	}
	if len(s) >= 2 && s[0] == '\'' && s[len(s)-1] == '\'' {
		return strings.ReplaceAll(s[1:len(s)-1], "''", "'")
	}
	return s
}

// Page is a note as loaded: its front matter and its Markdown body.
type Page struct {
	fm *frontMatter // nil when the note has none
	// Body is the note's Markdown after the front matter, with line breaks
	// as "\n".
	Body string
	// raw is the file as read, which a save compares with.
	raw string
	// reshapes is whether saving the body unedited would change it, and
	// loaded the body as read, so a save knows whether the body was
	// rewritten or only the front matter changed.
	reshapes bool
	loaded   string
}

// parsePage splits a note's text into a page.
func parsePage(raw string) *Page {
	fm, body := splitFrontMatter(raw)
	body = strings.ReplaceAll(body, "\r\n", "\n")
	return &Page{fm: fm, Body: body, raw: raw, loaded: body}
}

// Text is the page as written to its file.
func (p *Page) Text() string {
	if p.fm == nil || len(p.fm.lines) == 0 {
		return p.Body
	}
	return p.fm.text() + p.Body
}

func (p *Page) ensure() *frontMatter {
	if p.fm == nil {
		p.fm = &frontMatter{}
	}
	return p.fm
}

// Tags are the note's tags.
func (p *Page) Tags() []string {
	if p.fm == nil {
		return nil
	}
	return p.fm.list("tags")
}

// SetTags changes the note's tags; none removes the key.
func (p *Page) SetTags(tags []string) {
	var clean []string
	seen := map[string]bool{}
	for _, t := range tags {
		if t = strings.TrimSpace(t); t != "" && !seen[t] {
			seen[t] = true
			clean = append(clean, quoteTag(t))
		}
	}
	line := ""
	if len(clean) > 0 {
		line = "tags: [" + strings.Join(clean, ", ") + "]"
	}
	p.ensure().set("tags", line)
}

func (p *Page) flag(key string) bool {
	if p.fm == nil {
		return false
	}
	v, _ := p.fm.get(key)
	return strings.EqualFold(v, "true")
}

func (p *Page) setFlag(key string, on bool) {
	line := ""
	if on {
		line = key + ": true"
	}
	p.ensure().set(key, line)
}

// Pinned reports whether the note is pinned to the top of the list.
func (p *Page) Pinned() bool { return p.flag("pinned") }

// SetPinned pins or unpins the note.
func (p *Page) SetPinned(on bool) { p.setFlag("pinned", on) }

// Favorite reports whether the note is a favourite.
func (p *Page) Favorite() bool { return p.flag("favorite") }

// SetFavorite marks or unmarks the note as a favourite.
func (p *Page) SetFavorite(on bool) { p.setFlag("favorite", on) }

// Created is the note's creation time from its front matter, or zero.
func (p *Page) Created() time.Time {
	if p.fm == nil {
		return time.Time{}
	}
	v, _ := p.fm.get("created")
	for _, layout := range []string{"2006-01-02T15:04:05", time.RFC3339, "2006-01-02T15:04:05.000", "2006-01-02"} {
		if t, err := time.ParseInLocation(layout, v, time.Local); err == nil {
			return t
		}
	}
	return time.Time{}
}

// Goal is the note's writing goal in words, or 0.
func (p *Page) Goal() int {
	if p.fm == nil {
		return 0
	}
	v, _ := p.fm.get("goal")
	n, err := strconv.Atoi(v)
	if err != nil || n < 0 {
		return 0
	}
	return n
}
