package vault

// Renaming, merging and deleting tags across the vault: every note carrying
// the tag has its front matter rewritten, the rest of each note left as it
// was. Renaming onto a tag some notes already have merges the two, a note
// keeping one of them. The tag's colour follows the new name unless that one
// has its own.

import (
	"errors"
	"slices"
	"strings"
)

// ErrSomeNotes is returned when some notes could not be rewritten; the
// others were.
var ErrSomeNotes = errors.New("some notes could not be rewritten")

// retag rewrites the tags of every note carrying tag with change.
func (v *Vault) retag(tag string, change func(tags []string) []string) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	failed := false
	for _, e := range v.Entries {
		if !slices.Contains(e.Tags, tag) {
			continue
		}
		p, err := v.Load(e.Path)
		if err == nil {
			p.SetTags(change(p.Tags()))
			err = v.Save(e, p)
		}
		if err != nil {
			failed = true
		}
	}
	if failed {
		return ErrSomeNotes
	}
	return nil
}

// RenameTag renames a tag on every note, merging it into a tag of the new
// name where there is one.
func (v *Vault) RenameTag(old, name string) error {
	name = strings.TrimSpace(name)
	if name == "" {
		return ErrName
	}
	if name == old {
		return nil
	}
	err := v.retag(old, func(tags []string) []string {
		var out []string
		for _, t := range tags {
			if t == old {
				t = name
			}
			if !slices.Contains(out, t) {
				out = append(out, t)
			}
		}
		return out
	})
	if c, ok := v.State.TagColors[old]; ok {
		if _, has := v.State.TagColors[name]; !has {
			v.State.SetTagColor(name, c)
		}
		v.State.SetTagColor(old, "")
	}
	_ = v.SaveState()
	return err
}

// DeleteTag takes a tag off every note.
func (v *Vault) DeleteTag(tag string) error {
	err := v.retag(tag, func(tags []string) []string {
		return slices.DeleteFunc(tags, func(t string) bool { return t == tag })
	})
	v.State.SetTagColor(tag, "")
	_ = v.SaveState()
	return err
}

// TagCount is how many notes carry a tag.
func (v *Vault) TagCount(tag string) int {
	n := 0
	for _, e := range v.Entries {
		if slices.Contains(e.Tags, tag) {
			n++
		}
	}
	return n
}
