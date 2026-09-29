package links

import (
	"slices"
	"unicode/utf16"
)

// The backlinks pane, from WikiLinkIndex::backlinksTo in the app's
// src/repository/wikilinkindex.cpp.

// Backlink is one note that links to another, as a row of the backlinks
// pane. The pane shows the note's title, which is the vault's to give.
type Backlink struct {
	// Path is the linking note.
	Path string
	// Count is how many links in it resolve to the note.
	Count int
	// Contexts are the lines those links are on, each once, in order, with
	// the spaces around them removed and cut to 200 UTF-16 code units.
	Contexts []string
}

// contextLimit is the length a context line is cut to, in UTF-16 code
// units as the app counts.
const contextLimit = 200

// Backlinks lists the notes that link to the note at path, sorted by path
// as the app sorts (code unit by code unit, so capitals first). bodies
// holds every note's body, front matter removed, by path. A note's links to
// itself are not listed, and a link resolves as Resolve says, through the
// redirect table too.
func (ix *Index) Backlinks(path string, bodies map[string]string) []Backlink {
	if path == "" {
		return nil
	}
	paths := make([]string, 0, len(bodies))
	for p := range bodies {
		if p != path {
			paths = append(paths, p)
		}
	}
	slices.SortFunc(paths, compareUTF16)

	var out []Backlink
	for _, p := range paths {
		body := bodies[p]
		row := Backlink{Path: p}
		lineSeen := map[int]bool{}
		rs := []rune(body)
		for _, l := range Scan(body) {
			if l.Note == "" || ix.Resolve(l.Target) != path {
				continue
			}
			row.Count++
			start := l.Start
			for start > 0 && rs[start-1] != '\n' {
				start--
			}
			if lineSeen[start] {
				continue
			}
			lineSeen[start] = true
			end := indexOf(rs, '\n', l.Start)
			if end < 0 {
				end = len(rs)
			}
			row.Contexts = append(row.Contexts, cutUTF16(trim(string(rs[start:end])), contextLimit))
		}
		if row.Count > 0 {
			out = append(out, row)
		}
	}
	return out
}

// cutUTF16 keeps the first n UTF-16 code units of s, never splitting a
// character in two.
func cutUTF16(s string, n int) string {
	units := 0
	for i, r := range s {
		units += utf16.RuneLen(r)
		if units > n {
			return s[:i]
		}
	}
	return s
}
