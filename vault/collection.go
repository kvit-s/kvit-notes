package vault

// .kvit/collection.json, the vault's workspace state, in the Qt app's format
// (src/repository/collectionstatestore.cpp): folder colours and which folders
// are closed, the last note open, the manual order of notes in each folder,
// and tag colours. The Qt app rebuilds the file from its own state on every
// write, so every field it has is kept here too, whether or not this app
// uses it yet.

import (
	"bytes"
	"encoding/json"
	"os"
	"path"
	"path/filepath"
	"slices"
	"sort"
	"time"
)

// Collection is what collection.json holds. Its fields are in the order
// the Qt app writes them, which is alphabetical.
type Collection struct {
	// Folders holds the folders with a colour or closed in the sidebar;
	// a folder that is not here is open and has no colour.
	Folders map[string]FolderState `json:"folders,omitempty"`
	// LastOpenNote is the path of the note open when the vault was last
	// closed.
	LastOpenNote string `json:"lastOpenNote,omitempty"`
	// ManualOrder is each folder's notes in the order the reader put them,
	// by file name; "" is the top of the vault.
	ManualOrder map[string][]string `json:"manualOrder,omitempty"`
	// TagColors is each tag's colour as "#rrggbb".
	TagColors map[string]string `json:"tagColors,omitempty"`
}

// FolderState is one folder's entry in collection.json.
type FolderState struct {
	Color    string `json:"color,omitempty"`
	Expanded bool   `json:"expanded"`
}

// maxCollection is the largest collection.json read, as in the Qt app.
const maxCollection = 64 << 20

// loadCollection reads collection.json; a missing, damaged or oversized file
// gives the defaults, as in the Qt app.
func loadCollection(root string) *Collection {
	c := &Collection{}
	path := filepath.Join(root, ".kvit", "collection.json")
	info, err := os.Stat(path)
	if err != nil || info.Size() > maxCollection {
		return c
	}
	data, err := os.ReadFile(path)
	if err != nil || json.Unmarshal(data, c) != nil {
		return &Collection{}
	}
	return c
}

// FolderExpanded reports whether a folder is open in the sidebar.
func (c *Collection) FolderExpanded(folder string) bool {
	if s, ok := c.Folders[folder]; ok {
		return s.Expanded
	}
	return true
}

// SetFolderExpanded opens or closes a folder in the sidebar.
func (c *Collection) SetFolderExpanded(folder string, expanded bool) {
	s := c.Folders[folder]
	s.Expanded = expanded
	if s.Expanded && s.Color == "" {
		delete(c.Folders, folder)
		return
	}
	if c.Folders == nil {
		c.Folders = map[string]FolderState{}
	}
	c.Folders[folder] = s
}

// encode writes the file as the Qt app does: indented by four spaces, keys
// in order, ending with a line break.
func (c *Collection) encode() ([]byte, error) {
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetIndent("", "    ")
	enc.SetEscapeHTML(false)
	if err := enc.Encode(c); err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

// SetFolderColor gives a folder a colour ("#rrggbb"), or takes it away
// with "".
func (c *Collection) SetFolderColor(folder, color string) {
	s, ok := c.Folders[folder]
	if !ok {
		s.Expanded = true
	}
	s.Color = color
	if s.Expanded && s.Color == "" {
		delete(c.Folders, folder)
		return
	}
	if c.Folders == nil {
		c.Folders = map[string]FolderState{}
	}
	c.Folders[folder] = s
}

// SetTagColor gives a tag a colour, or takes it away with "".
func (c *Collection) SetTagColor(tag, color string) {
	if color == "" {
		delete(c.TagColors, tag)
		return
	}
	if c.TagColors == nil {
		c.TagColors = map[string]string{}
	}
	c.TagColors[tag] = color
}

// ManualOrder is a folder's notes in the order the reader put them: the
// names collection.json lists that still exist, then the rest, oldest
// first (NoteCollection::manualOrder).
func (v *Vault) ManualOrder(folder string) []*Entry {
	var out []*Entry
	listed := map[*Entry]bool{}
	for _, name := range v.State.ManualOrder[folder] {
		if e := v.Find(path.Join(folder, name)); e != nil && !listed[e] {
			out = append(out, e)
			listed[e] = true
		}
	}
	var rest []*Entry
	for _, e := range v.Entries {
		if e.Folder == folder && !listed[e] {
			rest = append(rest, e)
		}
	}
	sort.SliceStable(rest, func(a, b int) bool {
		ca, cb := createdTime(rest[a]), createdTime(rest[b])
		if !ca.Equal(cb) {
			return ca.Before(cb)
		}
		return rest[a].Path < rest[b].Path
	})
	return append(out, rest...)
}

// createdTime is when a note was made: its front matter's date, else its
// file's time.
func createdTime(e *Entry) time.Time {
	if !e.Created.IsZero() {
		return e.Created
	}
	return e.Modified
}

// SetManualPosition moves a note to a place in its folder's manual order,
// and keeps the order in collection.json.
func (v *Vault) SetManualPosition(e *Entry, position int) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	order := slices.DeleteFunc(v.ManualOrder(e.Folder), func(x *Entry) bool { return x == e })
	position = max(0, min(position, len(order)))
	order = slices.Insert(order, position, e)
	names := make([]string, len(order))
	for i, x := range order {
		names[i] = path.Base(x.Path)
	}
	if v.State.ManualOrder == nil {
		v.State.ManualOrder = map[string][]string{}
	}
	v.State.ManualOrder[e.Folder] = names
	return v.SaveState()
}
