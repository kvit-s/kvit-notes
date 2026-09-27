package main

// The note window: a strip of formatting buttons, the editor in a scrolling
// region, and a status line saying what the caret is in and whether the note
// is saved. It stands in for Kvit's window chrome, which the full app adds
// later (the plan's step 6).

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// noteWindow is one window editing one note.
type noteWindow struct {
	ui      *kvitui.UI
	win     *kvitui.Window
	ed      *editor.Editor
	region  *kvitui.Region
	kind    *kvitui.Label
	status  *kvitui.StatusBar
	path    string
	message string // what the last save said
}

// toolbarHeight is the formatting strip's height in design pixels, as Kvit's
// toolbar is.
const toolbarHeight = 36

// newNoteWindow opens a window editing doc, which is saved to path.
func newNoteWindow(ui *kvitui.UI, doc *editor.Doc, path string) (*noteWindow, error) {
	w, err := kvitui.NewWindow(ui, title(path))
	if err != nil {
		return nil, err
	}
	n := &noteWindow{ui: ui, win: w, path: path}
	n.ed = editor.New(ui, doc)
	n.ed.OnChange = n.update
	n.region = kvitui.NewRegion(ui, n.ed)
	n.region.Padding = kvitui.Px(0)
	n.status = kvitui.NewStatusBar(ui)

	body := unison.NewPanel()
	body.SetLayout(&unison.FlexLayout{Columns: 1, HAlign: align.Fill, VAlign: align.Fill})
	strip := n.toolbar()
	strip.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	n.region.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	body.AddChild(strip)
	body.AddChild(n.region)
	w.SetBody(body)
	w.SetStatusBar(n.status)
	w.SidebarVisible = false
	w.OnKeyDown = func(key unison.KeyCode, mods mod.Modifiers, _ bool) bool {
		if key == unison.KeyS && mods.OSMenuCommandDown() {
			n.save()
			return true
		}
		return false
	}
	n.update()
	return n, nil
}

func title(path string) string {
	if path == "" {
		return "Kvit Notes"
	}
	return filepath.Base(path) + " — Kvit Notes"
}

// toolbar is the strip across the top: what the caret's block is, and the
// inline formats a button can toggle.
func (n *noteWindow) toolbar() *unison.Panel {
	ui := n.ui
	n.kind = kvitui.NewLabel(ui, "Block type")
	n.kind.Role = kvitui.RoleBody
	parts := []unison.Paneler{kvitui.Width(ui, kvitui.Px(130), n.kind)}
	for _, f := range []struct{ label, name, marker string }{
		{"B", "Bold", "**"}, {"I", "Italic", "*"}, {"U", "Underline", "++"},
		{"S", "Strikethrough", "~~"}, {"<>", "Inline code", "`"}, {"H", "Highlight", "=="},
	} {
		b := kvitui.NewButton(ui, f.label)
		b.Form = kvitui.ButtonQuiet
		b.Explanation = f.name
		b.OnClick = func() {
			n.ed.Doc.ToggleFormat(f.marker)
			n.ed.RequestFocus()
			n.ed.Refresh()
		}
		parts = append(parts, b)
	}
	row := kvitui.Row(ui, kvitui.SizeSpaceSnug, parts...)
	row.SetBorder(kvitui.Insets(ui, kvitui.Px(4), kvitui.Px(10), kvitui.Px(4), kvitui.Px(10)))
	row.DrawCallback = func(gc *unison.Canvas, _ geom.Rect) {
		t := ui.Theme.Tokens()
		r := row.ContentRect(true)
		gc.DrawRect(r, kvitui.Color(t.FooterBackground).Paint(gc, r, paintstyle.Fill))
		line := geom.NewRect(r.X, r.Bottom()-float32(ui.Interface.Hairline()), r.Width, float32(ui.Interface.Hairline()))
		gc.DrawRect(line, kvitui.Color(t.Border).Paint(gc, line, paintstyle.Fill))
	}
	return kvitui.Height(ui, kvitui.Px(toolbarHeight), row)
}

// update shows the caret's block and the note's counts.
func (n *noteWindow) update() {
	d := n.ed.Doc
	st := "Saved"
	if d.Dirty {
		st = "Unsaved"
	}
	if n.message != "" {
		st = n.message
	}
	n.status.Activity = st
	var facts []string
	kind := "Block type"
	if b := d.CaretBlock(); b != nil && d.Focused {
		i := d.Index(b.ID)
		ln, col := 1, 1
		r := []rune(b.Text)
		for _, c := range r[:min(d.Caret.Off, len(r))] {
			if c == '\n' {
				ln, col = ln+1, 1
			} else {
				col++
			}
		}
		facts = append(facts, fmt.Sprintf("Block %d · Ln %d, Col %d", i+1, ln, col))
		kind = b.Kind.String()
	} else if ids := n.ed.SelectedBlocks(); len(ids) > 0 {
		facts = append(facts, fmt.Sprintf("%d blocks selected", len(ids)))
	}
	if n.path != "" {
		facts = append(facts, filepath.Base(n.path))
	} else {
		facts = append(facts, "Not saved to disk")
	}
	words, chars := d.Words()
	facts = append(facts, fmt.Sprintf("%d blocks", len(d.Blocks)), fmt.Sprintf("%d words", words), fmt.Sprintf("%d chars", chars))
	n.status.Facts = facts
	n.kind.Text = kind
	n.status.MarkForLayoutAndRedraw()
	n.kind.MarkForRedraw()
}

// save writes the note to its file.
func (n *noteWindow) save() {
	d := n.ed.Doc
	switch {
	case n.path == "":
		n.message = "No file to save to"
	default:
		if err := os.WriteFile(n.path, []byte(editor.Serialize(d.Blocks)), 0o644); err != nil {
			n.message = "Save failed: " + err.Error()
		} else {
			d.Dirty = false
			n.message = ""
		}
	}
	n.update()
}

// loadDoc reads a note, or returns the sample note for an empty path.
func loadDoc(path string) (*editor.Doc, error) {
	if path == "" {
		return editor.NewDoc(editor.ParseMarkdown(sampleNote)), nil
	}
	data, err := os.ReadFile(path)
	if os.IsNotExist(err) {
		return editor.NewDoc(nil), nil
	}
	if err != nil {
		return nil, err
	}
	return editor.NewDoc(editor.ParseMarkdown(string(data))), nil
}

const sampleNote = `# Welcome to Kvit Notes

The quick **brown** fox has *seven* cubs. Markers such as the stars show only around the caret.

## Things to try

- Type ` + "`# `" + ` at the start of an empty paragraph to make a heading
- Type ` + "`/`" + ` in an empty block for the block menu
- [ ] Drag a block by the handle that appears on hover
- [x] Undo anything with Ctrl+Z

> A quotation keeps its lines
> together.

` + "```" + `
func main() {
    fmt.Println("code keeps its indentation")
}
` + "```" + `

---

Bold with Ctrl+B, italic with Ctrl+I, ==highlight==, ~~strike~~, ++underline++ and [links](https://kvit.app).
`
