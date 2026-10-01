package links

import (
	"maps"
	"slices"
	"strings"
)

// Links after a rename or move. Kvit Notes records a redirect from the old
// path to the new one and returns at once, so every link keeps resolving;
// then it rewrites the notes holding those links a few at a time, and drops
// the redirect once nothing links through it.
//
// What Kvit Notes does when a note is renamed or moved from old to new with
// "update links" chosen, in this package's terms:
//
//	ix.Remove(old)
//	ix.Add(new)                       // save the table if this reports true
//	ix.Redirects.Retarget(old, new)   // older names of the note follow it
//	ix.Redirects.Record(old, new)
//	ix.PruneRedirects(bodies)         // then save the table
//	for each note but the one open in the editor, a few at a time:
//		if ix.NeedsRewrite(body): body, n = ix.RewriteRedirected(body)
//	ix.PruneRedirects(bodies)         // save the table if this reports true
//
// The open note's text is rewritten in memory with RewriteRedirected, as one
// change the reader can undo, and its file waits for the next pass after it
// is closed. With "leave links alone" only the first three steps happen. A
// folder rename re-files each note under it the same way and records its
// redirects with Redirects.RecordFolder.

// Key is the form of a link's note part a rename compares: lowercased and
// one ".md" dropped, leading slashes kept.
func Key(note string) string { return strings.TrimSuffix(lower(note), ".md") }

// RewriteTargets replaces the note part of every link whose Key is in keys
// with replacement, and reports how many it replaced. The spaces inside the
// brackets, the "#heading" and the "|alias" are kept as they were, and links
// in code and math are left alone: with keys {"old"} and replacement "New",
// "[[ Old ]] [[old#H|x]] [[Old.md]]" becomes "[[ New ]] [[New#H|x]] [[New]]".
func RewriteTargets(text string, keys map[string]bool, replacement string) (string, int) {
	if len(keys) == 0 {
		return text, 0
	}
	var hits []Link
	for _, l := range Scan(text) {
		if k := Key(l.Note); k != "" && keys[k] {
			hits = append(hits, l)
		}
	}
	if len(hits) == 0 {
		return text, 0
	}
	// The text is spliced by byte, so every byte outside the replaced note
	// parts stays as it was, even where it is not valid UTF-8.
	offsets := make([]int, 0, len(text)+1)
	for i := range text {
		offsets = append(offsets, i)
	}
	offsets = append(offsets, len(text))
	var b strings.Builder
	last := 0
	for _, l := range hits {
		b.WriteString(text[last:offsets[l.NoteStart]])
		b.WriteString(replacement)
		last = offsets[l.NoteStart+l.NoteLength]
	}
	b.WriteString(text[last:])
	return b.String(), len(hits)
}

// redirected is where the redirect table sends a link's note part, or ""
// when it does not: only a note part that names no note at all goes
// through a redirect, never one that resolves or is ambiguous, and only to
// a note that exists.
func (ix *Index) redirected(note string) string {
	if ix.Redirects == nil || ix.Redirects.Len() == 0 {
		return ""
	}
	normalized := NormalizeTarget(note)
	if normalized == "" || ix.Resolution(normalized, false).Status != Missing {
		return ""
	}
	if to := ix.Redirects.TargetFor(normalized); ix.Has(to) {
		return to
	}
	return ""
}

// Replacement is the note part a link written as note gets once it points
// at newPath. A link written with a path keeps a path, the whole new one
// without ".md", because shortening it would widen what it matches; a bare
// name becomes the new title when that alone resolves to newPath, else the
// path. A leading "/" is kept.
func (ix *Index) Replacement(note, newPath string) string {
	prefix := ""
	if strings.HasPrefix(note, "/") {
		prefix = "/"
	}
	if strings.Contains(strings.TrimLeft(note, "/"), "/") {
		return prefix + trimMD(newPath)
	}
	title := trimMD(baseName(newPath))
	if ix.Resolution(title, false).Path == newPath {
		return prefix + title
	}
	return prefix + trimMD(newPath)
}

// RewriteRedirected rewrites every link in text that resolves only through
// the redirect table so it names the note directly, and reports how many
// it rewrote. This is the rewrite Kvit Notes applies to each note after a
// rename, and to the open note's text in memory. "[[Target]]" and
// "[[target#Head|alias]]" become "[[Renamed]]" and "[[Renamed#Head|alias]]"
// after Target.md was renamed to Renamed.md.
func (ix *Index) RewriteRedirected(text string) (string, int) {
	if ix.Redirects == nil || ix.Redirects.Len() == 0 {
		return text, 0
	}
	byReplacement := map[string]map[string]bool{}
	for _, l := range Scan(text) {
		if l.Note == "" {
			continue
		}
		to := ix.redirected(l.Note)
		if to == "" {
			continue
		}
		r := ix.Replacement(l.Note, to)
		if byReplacement[r] == nil {
			byReplacement[r] = map[string]bool{}
		}
		byReplacement[r][Key(l.Note)] = true
	}
	total := 0
	for _, r := range slices.Sorted(maps.Keys(byReplacement)) {
		var n int
		text, n = RewriteTargets(text, byReplacement[r], r)
		total += n
	}
	return text, total
}

// NeedsRewrite reports whether a note body has a link that resolves only
// through the redirect table, which is what puts the note in the rewrite
// pass after a rename.
func (ix *Index) NeedsRewrite(body string) bool {
	for _, t := range Targets(body) {
		if ix.redirected(t) != "" {
			return true
		}
	}
	return false
}

// PruneRedirects drops every redirect no link needs any more, and reports
// whether it dropped any, so the caller saves the table. A redirect is
// still needed while some note's link names no note and resolves through
// it, and its destination exists. bodies holds every note's body by path.
func (ix *Index) PruneRedirects(bodies map[string]string) bool {
	if ix.Redirects == nil || ix.Redirects.Len() == 0 {
		return false
	}
	named := map[string]bool{}
	for _, body := range bodies {
		for _, t := range Targets(body) {
			normalized := NormalizeTarget(t)
			if normalized == "" || ix.Resolution(normalized, false).Status != Missing {
				continue
			}
			if e, ok := ix.Redirects.Lookup(normalized); ok {
				named[e.From] = true
			}
		}
	}
	for _, e := range ix.Redirects.Entries() {
		if !ix.Has(e.To) {
			delete(named, e.From)
		}
	}
	return ix.Redirects.RetainFrom(named)
}

// Referrer is a note holding links a rename would rewrite, as the rename
// dialog counts them before the rename.
type Referrer struct {
	Path string
	// Keys are the Key forms of the note parts in it that name the note.
	Keys []string
	// Count is how many links in it that is.
	Count int
}

// Referrers lists, before a note at path is renamed or moved, the notes
// with links to it and how many, sorted by path. The note's links to
// itself count. bodies holds every note's body by path.
func (ix *Index) Referrers(path string, bodies map[string]string) []Referrer {
	var out []Referrer
	for p, body := range bodies {
		links := Scan(body)
		keys := map[string]bool{}
		for _, l := range links {
			if l.Note != "" && ix.Resolve(l.Note) == path {
				keys[Key(l.Note)] = true
			}
		}
		if len(keys) == 0 {
			continue
		}
		r := Referrer{Path: p, Keys: slices.Sorted(maps.Keys(keys))}
		for _, l := range links {
			if keys[Key(l.Note)] {
				r.Count++
			}
		}
		out = append(out, r)
	}
	slices.SortFunc(out, func(a, b Referrer) int { return compareUTF16(a.Path, b.Path) })
	return out
}

// FolderReferrers lists, before the folder at prefix is renamed, the notes
// with links whose note part starts with the folder's path, and how many,
// sorted by path. Only those links change; a bare [[Note]] to a note in
// the folder still resolves after the rename.
func FolderReferrers(prefix string, bodies map[string]string) []Referrer {
	lowered := lower(prefix) + "/"
	var out []Referrer
	for p, body := range bodies {
		n := 0
		for _, l := range Scan(body) {
			if strings.HasPrefix(lower(strings.TrimLeft(l.Note, "/")), lowered) {
				n++
			}
		}
		if n > 0 {
			out = append(out, Referrer{Path: p, Count: n})
		}
	}
	slices.SortFunc(out, func(a, b Referrer) int { return compareUTF16(a.Path, b.Path) })
	return out
}
