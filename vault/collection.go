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
	"path/filepath"
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
