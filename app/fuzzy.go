package app

// Ranking notes by how well their names match what is typed, for the quick
// switcher and [[ completion (Kvit's src/content/fuzzymatch.h and
// QuickSwitcherModel::itemsFor): a name that starts with it first, then one
// with a word that starts with it, then one holding its letters in order;
// within each, the note changed most recently first. The title and the
// path are both tried.

import (
	"slices"
	"strings"

	"github.com/kvit-s/kvit-notes/vault"
)

// Match tiers, best first.
const (
	tierPrefix = iota
	tierWordPrefix
	tierSubsequence
	tierNone
)

func isSubsequence(needle, haystack string) bool {
	n := []rune(needle)
	k := 0
	for _, r := range haystack {
		if k < len(n) && r == n[k] {
			k++
		}
	}
	return k == len(n)
}

// tierFor is the best tier of a lowercased query over a note's names.
func tierFor(query string, names ...string) int {
	best := tierNone
	for _, name := range names {
		lowered := strings.ToLower(name)
		if strings.HasPrefix(lowered, query) {
			return tierPrefix
		}
		if best > tierWordPrefix {
			for _, word := range strings.Fields(lowered) {
				if strings.HasPrefix(word, query) {
					best = tierWordPrefix
					break
				}
			}
		}
		if best > tierSubsequence && isSubsequence(query, lowered) {
			best = tierSubsequence
		}
	}
	return best
}

// rankNotes are the notes whose title or path match a query, best first,
// at most limit of them (0 for all).
func rankNotes(entries []*vault.Entry, query string, limit int) []*vault.Entry {
	q := strings.ToLower(strings.TrimSpace(query))
	type ranked struct {
		tier int
		e    *vault.Entry
	}
	var rs []ranked
	for _, e := range entries {
		tier := tierPrefix
		if q != "" {
			if tier = tierFor(q, e.Title, e.Path); tier == tierNone {
				continue
			}
		}
		rs = append(rs, ranked{tier, e})
	}
	slices.SortStableFunc(rs, func(a, b ranked) int {
		if a.tier != b.tier {
			return a.tier - b.tier
		}
		return b.e.Modified.Compare(a.e.Modified)
	})
	var out []*vault.Entry
	for _, r := range rs {
		if limit > 0 && len(out) >= limit {
			break
		}
		out = append(out, r.e)
	}
	return out
}
