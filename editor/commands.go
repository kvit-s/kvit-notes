package editor

// The commands a window's toolbar gives the editor: the kind of the caret's
// block, alignment, inline formats and text colour, and inserting a new block
// of a kind below the caret's.

import (
	"slices"

	kvitui "github.com/kvit-s/kvit-ui"
)

// KindChoice is a kind of block by the name the toolbar gives it.
type KindChoice struct {
	Name string
	Kind Kind
}

// ToolbarKinds are the kinds the toolbar's block type list offers, in its
// order.
var ToolbarKinds = []KindChoice{
	{"Text", Paragraph}, {"Heading 1", Heading1}, {"Heading 2", Heading2}, {"Heading 3", Heading3},
	{"Heading 4", Heading4}, {"Bulleted List", Bullet}, {"Numbered List", Numbered}, {"To-do", Todo},
	{"Quote", Quote}, {"Code Block", Code}, {"Callout", Callout}, {"Divider", Divider},
}

// TextColors are the colours the text colour menus offer: the name each
// shows and the value written into the note.
var TextColors = []struct{ Name, Value string }{
	{"Red", "#e05c5c"}, {"Orange", "#e0a04c"}, {"Green", "#58a866"},
	{"Blue", "#4a90d9"}, {"Purple", "#9068c8"}, {"Pink", "#d06ca8"},
}

// targets are the blocks a command acts on: the block selection, or the
// caret's block.
func (e *Editor) targets() []int64 {
	if ids := e.SelectedBlocks(); len(ids) > 0 {
		return ids
	}
	if b := e.Doc.CaretBlock(); b != nil {
		return []int64{b.ID}
	}
	return nil
}

// done finishes a command from outside the editor: the keyboard goes back
// to the note and the rows are measured again.
func (e *Editor) done() {
	e.RequestFocus()
	e.changed()
	e.touched()
}

// TurnInto changes the kind of the caret's block, or of every selected
// block.
func (e *Editor) TurnInto(k Kind) {
	if e.Doc.ReadOnly {
		return
	}
	for _, id := range e.targets() {
		e.Doc.Convert(id, k)
	}
	e.done()
}

// Align sets the alignment of the caret's block or the selected blocks:
// "left", "center" or "right".
func (e *Editor) Align(value string) {
	if e.Doc.ReadOnly {
		return
	}
	if value == "left" {
		value = ""
	}
	e.Doc.SetAttr(e.targets(), "align", value)
	e.done()
}

// Alignment is the caret's block's alignment: "left", "center" or "right".
func (e *Editor) Alignment() string {
	if b := e.Doc.CaretBlock(); b != nil {
		if a, ok := b.Attr("align"); ok && a != "" {
			return a
		}
	}
	return "left"
}

// Format wraps the selection in an inline marker, or unwraps it.
func (e *Editor) Format(marker string) {
	if e.Doc.ReadOnly {
		return
	}
	e.Doc.ToggleFormat(marker)
	e.done()
}

// SetColor colours the selected text; "" takes the colour away.
func (e *Editor) SetColor(value string) {
	if e.Doc.ReadOnly {
		return
	}
	e.Doc.SetColor(value)
	e.done()
}

// ColorItems are the text colour menu's lines: each colour, and Remove
// color when the selection has one.
func (e *Editor) ColorItems() []kvitui.MenuItem {
	current := e.Doc.CurrentColor()
	var items []kvitui.MenuItem
	for _, c := range TextColors {
		items = append(items, kvitui.MenuItem{Text: c.Name, Checked: current == c.Value,
			OnSelect: func() { e.SetColor(c.Value) }})
	}
	return append(items, kvitui.MenuItem{Separator: true},
		kvitui.MenuItem{Text: "Remove color", Disabled: current == "", OnSelect: func() { e.SetColor("") }})
}

// insertAt adds an empty paragraph below the caret's block, or at the end
// of the note, and puts the caret in it.
func (e *Editor) insertAt() int64 {
	d := e.Doc
	i := len(d.Blocks) - 1
	if b := d.CaretBlock(); b != nil && d.Focused {
		i = d.Index(b.ID)
	}
	nb := NewBlock(Paragraph, "")
	d.Edit("insert block", func() {
		d.Blocks = slices.Insert(d.Blocks, i+1, nb)
		d.SetCaret(nb.ID, 0)
	})
	e.clearBlockSel()
	return nb.ID
}

// InsertItems are the toolbar's Insert menu: each kind of the block type
// list, then the kinds that start with content of their own.
func (e *Editor) InsertItems() []kvitui.MenuItem {
	var items []kvitui.MenuItem
	for _, c := range ToolbarKinds {
		items = append(items, kvitui.MenuItem{Text: c.Name, Disabled: e.Doc.ReadOnly, OnSelect: func() {
			id := e.insertAt()
			e.Doc.Convert(id, c.Kind)
			e.done()
		}})
	}
	items = append(items, kvitui.MenuItem{Separator: true})
	for _, name := range []string{"Table", "Math", "Image"} {
		label := name
		if name == "Math" {
			label = "Math Block"
		}
		items = append(items, kvitui.MenuItem{Text: label, Disabled: e.Doc.ReadOnly, OnSelect: func() {
			id := e.insertAt()
			for _, it := range menuItems {
				if it.name == name {
					e.applyItem(id, it)
				}
			}
			e.done()
		}})
	}
	return items
}
