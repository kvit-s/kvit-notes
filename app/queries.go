package app

// Collection query blocks: the notes of the vault as the query package reads
// them, and the answer for a block's spec, worked out again whenever the
// vault has changed since.

import (
	"github.com/kvit-s/kvit-notes/query"
)

// querySource is the vault as the query package reads it.
type querySource struct{ w *Window }

// Revision changes whenever a note is added, removed or changed: it is
// made from every note's path and time of change.
func (s querySource) Revision() int {
	h := 0
	for _, e := range s.w.Vault.Entries {
		h = h*31 + int(e.Modified.UnixNano()) + len(e.Path)
	}
	return h
}

// Notes are every note with its front matter's fields.
func (s querySource) Notes() []query.Note {
	w := s.w
	w.bodies()
	var out []query.Note
	for _, e := range w.Vault.Entries {
		out = append(out, query.Note{Path: e.Path, Title: e.Title, Folder: e.Folder, Modified: e.Modified,
			Created: createdOf(e), Words: e.Words, Tags: append([]string(nil), e.Tags...),
			Fields: w.bodyCache[e.Path].fields})
	}
	query.SortNotes(out)
	return out
}

// runQuery answers a query block's spec.
func (w *Window) runQuery(body string) query.Answer {
	if w.queries == nil {
		w.queries = &query.Tools{}
		w.queries.SetCollection(querySource{w})
	}
	return w.queries.Run(body)
}
