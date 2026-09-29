package links

import (
	"encoding/json"
	"errors"
	"io"
	"io/fs"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
)

// The table of renamed notes, .kvit/redirects.json, from the app's
// src/repository/linkredirects.h.
//
// When a note is renamed or moved with its links updated, the table records
// that the old path is now the new one, and a link naming the old path
// resolves through it while the notes holding such links are rewritten.
// An entry lasts exactly as long as some note still links through it, so a
// table found when a vault opens is a rewrite that was interrupted, and the
// rewrite runs again. The table is local to the vault: another program sees
// the links it covers as broken until the rewrite finishes.
//
// The file is compact JSON:
//
//	{"redirects":[{"from":"Target.md","to":"Renamed.md"}],"version":1}
//
// and it is deleted when the table is empty.

// maxRedirectBytes is the largest redirects.json read. A larger one was not
// written by Kvit, and is read as an empty table.
const maxRedirectBytes = 16 << 20

// Redirect is one entry of the table.
type Redirect struct {
	// From is the note's path before the rename, To where it is now.
	From, To string
}

// Redirects is a vault's table of renamed notes. The zero value is an
// empty table.
type Redirects struct {
	entries []Redirect
	byBase  map[string][]int // basename of NormalizeTarget(From) -> entries
}

// errUnsound is returned when .kvit is a link or not a directory, which
// the app refuses to write through (src/repository/vaultpaths.cpp).
var errUnsound = errors.New("links: .kvit is not a directory of the vault's own")

// IsPlainRelativePath reports whether p is a path inside a vault in its
// plain form, the test every path in redirects.json must pass
// (VaultPaths::isPlainRelativePath): not empty, not absolute, no
// backslash, and no empty, "." or ".." segment. A path starting with a
// drive letter and a colon, or with a colon (a resource path), counts as
// absolute on every system, as it does for the app on Windows.
func IsPlainRelativePath(p string) bool {
	if p == "" || strings.HasPrefix(p, "/") || strings.HasPrefix(p, ":") || strings.Contains(p, `\`) {
		return false
	}
	if len(p) >= 2 && p[1] == ':' && (p[0]|0x20) >= 'a' && (p[0]|0x20) <= 'z' {
		return false
	}
	for _, seg := range strings.Split(p, "/") {
		if seg == "" || seg == "." || seg == ".." {
			return false
		}
	}
	return true
}

// kvitDir is the vault's .kvit directory, or errUnsound when it is a
// symbolic link, a junction, or not a directory. A missing one is sound.
func kvitDir(root string) (string, error) {
	dir := filepath.Join(root, ".kvit")
	info, err := os.Lstat(dir)
	switch {
	case errors.Is(err, fs.ErrNotExist):
		return dir, nil
	case err != nil:
		return "", err
	case info.Mode()&(fs.ModeSymlink|fs.ModeIrregular) != 0 || !info.IsDir():
		return "", errUnsound
	}
	return dir, nil
}

// LoadRedirects reads the table of the vault at root. A missing, unreadable,
// damaged or oversized file is an empty table, as is one under a .kvit that
// is a link. An entry whose paths are not plain relative paths, or whose two
// paths are the same, is left out, so a crafted file cannot send a link
// outside the vault.
func LoadRedirects(root string) *Redirects {
	r := &Redirects{}
	defer r.reindex()
	dir, err := kvitDir(root)
	if err != nil {
		return r
	}
	f, err := os.Open(filepath.Join(dir, "redirects.json"))
	if err != nil {
		return r
	}
	defer f.Close()
	data, err := io.ReadAll(io.LimitReader(f, maxRedirectBytes+1))
	if err != nil || len(data) > maxRedirectBytes {
		return r
	}
	var top map[string]json.RawMessage
	var list []json.RawMessage
	if json.Unmarshal(data, &top) != nil || json.Unmarshal(top["redirects"], &list) != nil {
		return r
	}
	for _, raw := range list {
		var e map[string]any
		_ = json.Unmarshal(raw, &e)
		from, _ := e["from"].(string)
		to, _ := e["to"].(string)
		if IsPlainRelativePath(from) && IsPlainRelativePath(to) && from != to {
			r.entries = append(r.entries, Redirect{from, to})
		}
	}
	return r
}

// Save writes the table to the vault at root, through a temporary file and
// a rename, creating .kvit if it is missing; an empty table deletes the
// file instead.
func (r *Redirects) Save(root string) error {
	dir, err := kvitDir(root)
	if err != nil {
		return err
	}
	path := filepath.Join(dir, "redirects.json")
	if len(r.entries) == 0 {
		if err := os.Remove(path); err != nil && !errors.Is(err, fs.ErrNotExist) {
			return err
		}
		return nil
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	// Creating it can race a link appearing in its place.
	if _, err := kvitDir(root); err != nil {
		return err
	}
	return writeAtomic(path, r.encode())
}

// encode is the table as the app's JSON document writes it in compact
// form: keys in alphabetical order, no spaces, no newline at the end.
func (r *Redirects) encode() []byte {
	b := []byte(`{"redirects":[`)
	for i, e := range r.entries {
		if i > 0 {
			b = append(b, ',')
		}
		b = append(b, `{"from":`...)
		b = appendJSONString(b, e.From)
		b = append(b, `,"to":`...)
		b = appendJSONString(b, e.To)
		b = append(b, '}')
	}
	return append(b, `],"version":1}`...)
}

// appendJSONString appends s as a JSON string with the escapes: quote,
// backslash and the control characters, everything else as UTF-8.
func appendJSONString(b []byte, s string) []byte {
	b = append(b, '"')
	for _, c := range []byte(s) {
		switch c {
		case '"', '\\':
			b = append(b, '\\', c)
		case '\b':
			b = append(b, `\b`...)
		case '\f':
			b = append(b, `\f`...)
		case '\n':
			b = append(b, `\n`...)
		case '\r':
			b = append(b, `\r`...)
		case '\t':
			b = append(b, `\t`...)
		default:
			if c < 0x20 {
				b = append(b, `\u00`...)
				b = strconv.AppendUint(b, uint64(c>>4), 16)
				b = strconv.AppendUint(b, uint64(c&0xf), 16)
			} else {
				b = append(b, c)
			}
		}
	}
	return append(b, '"')
}

// writeAtomic writes a file through a temporary file beside it and a
// rename, so a reader never sees half of it.
func writeAtomic(target string, data []byte) error {
	tmp, err := os.CreateTemp(filepath.Dir(target), "."+filepath.Base(target)+".*")
	if err != nil {
		return err
	}
	name := tmp.Name()
	_, err = tmp.Write(data)
	if err == nil {
		err = tmp.Sync()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Chmod(name, 0o644)
	}
	if err == nil {
		err = os.Rename(name, target)
	}
	if err != nil {
		_ = os.Remove(name)
	}
	return err
}

// Len is the number of entries.
func (r *Redirects) Len() int { return len(r.entries) }

// Entries is a copy of the entries, oldest first.
func (r *Redirects) Entries() []Redirect { return slices.Clone(r.entries) }

// Record records that the note at from is now at to, and reports whether
// the table changed. The table never holds a chain: an entry that led to
// from now leads to to, so A to B then B to C is stored as A to C. An entry
// from to is dropped, because a note now stands there, and so is an older
// entry from from. It records nothing when either path is not a plain
// relative path or the two are the same.
func (r *Redirects) Record(from, to string) bool {
	if !IsPlainRelativePath(from) || !IsPlainRelativePath(to) || from == to {
		return false
	}
	for i := len(r.entries) - 1; i >= 0; i-- {
		e := &r.entries[i]
		if e.To == from {
			e.To = to
		}
		if e.From == to || e.From == from || e.From == e.To {
			r.entries = slices.Delete(r.entries, i, i+1)
		}
	}
	r.entries = append(r.entries, Redirect{from, to})
	r.reindex()
	return true
}

// RecordFolder records a folder rename from oldPrefix to newPrefix: one
// entry for each note now under newPrefix, since an entry names a note and
// never a folder. paths are the vault's note paths after the rename. It
// reports whether the table changed.
func (r *Redirects) RecordFolder(oldPrefix, newPrefix string, paths []string) bool {
	changed := false
	for _, p := range paths {
		if rest, ok := strings.CutPrefix(p, newPrefix+"/"); ok && r.Record(oldPrefix+"/"+rest, p) {
			changed = true
		}
	}
	return changed
}

// Retarget follows a note that entries lead to when it moves from from to
// to without a redirect of its own: renamed with links left alone, or
// carried by a folder rename. An entry that now leads back to where it
// started is dropped. It reports whether the table changed.
func (r *Redirects) Retarget(from, to string) bool {
	if len(r.entries) == 0 || !IsPlainRelativePath(to) {
		return false
	}
	changed := false
	for i := len(r.entries) - 1; i >= 0; i-- {
		if r.entries[i].To != from {
			continue
		}
		r.entries[i].To = to
		if r.entries[i].From == to {
			r.entries = slices.Delete(r.entries, i, i+1)
		}
		changed = true
	}
	if changed {
		r.reindex()
	}
	return changed
}

// DropFrom drops the entries from path, because a note now stands there,
// and reports whether there were any.
func (r *Redirects) DropFrom(path string) bool {
	n := len(r.entries)
	r.entries = slices.DeleteFunc(r.entries, func(e Redirect) bool { return e.From == path })
	if len(r.entries) == n {
		return false
	}
	r.reindex()
	return true
}

// RetainFrom keeps only the entries whose From is in keep, and reports
// whether it dropped any.
func (r *Redirects) RetainFrom(keep map[string]bool) bool {
	n := len(r.entries)
	r.entries = slices.DeleteFunc(r.entries, func(e Redirect) bool { return !keep[e.From] })
	if len(r.entries) == n {
		return false
	}
	r.reindex()
	return true
}

// Lookup is the entry a normalized target (see NormalizeTarget) names by the
// same rule links name notes: the whole old path or its last segments.
// Several entries with different destinations are ambiguous and answer
// nothing, as several notes of one name do.
func (r *Redirects) Lookup(normalized string) (Redirect, bool) {
	if normalized == "" || len(r.entries) == 0 {
		return Redirect{}, false
	}
	var found *Redirect
	for _, i := range r.byBase[baseName(normalized)] {
		e := &r.entries[i]
		if !PathMatchesTarget(e.From, normalized) {
			continue
		}
		if found != nil && found.To != e.To {
			return Redirect{}, false
		}
		found = e
	}
	if found == nil {
		return Redirect{}, false
	}
	return *found, true
}

// TargetFor is where the entry a normalized target names leads, or "".
func (r *Redirects) TargetFor(normalized string) string {
	e, _ := r.Lookup(normalized)
	return e.To
}

// reindex files the entries by the basename of their old path, so a lookup
// is a map probe rather than a walk of a folder rename's worth of entries.
func (r *Redirects) reindex() {
	r.byBase = map[string][]int{}
	for i, e := range r.entries {
		key := baseName(NormalizeTarget(e.From))
		r.byBase[key] = append(r.byBase[key], i)
	}
}
