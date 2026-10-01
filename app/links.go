package app

// Following links and the link dialog. A wiki link opens the note it names,
// the one note whose path ends with it, at its heading when it names one; a
// link naming no note makes it, in the place links.NewNoteFor gives; a name
// several notes share offers them to choose from. A Markdown link opens a
// web address in the browser, a "#heading" in the note itself, and a path
// to a note in the vault. The resolution rules are in the links package.

import (
	"errors"
	"os"
	"path"
	"strings"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/links"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
)

// linkIndex is the vault's notes filed for resolving links, with its table
// of renamed notes.
func (w *Window) linkIndex() *links.Index {
	paths := make([]string, len(w.Vault.Entries))
	for i, e := range w.Vault.Entries {
		paths[i] = e.Path
	}
	ix := links.NewIndex(paths)
	ix.Redirects = links.LoadRedirects(w.Vault.Root)
	return ix
}

// followLink follows a link pressed in the note.
func (w *Window) followLink(ref editor.LinkRef) {
	if !ref.Wiki {
		w.followAddress(ref.Target)
		return
	}
	rs := []rune("[[" + ref.Target + "]]")
	l, ok := links.MatchAt(rs, 0)
	if !ok {
		return
	}
	if l.Note == "" {
		if !w.Editor.GoToHeading(l.Heading) {
			w.say("No heading “" + l.Heading + "” in this note")
		}
		return
	}
	res := w.linkIndex().Resolution(l.Target, true)
	switch res.Status {
	case links.Unique:
		w.openAtHeading(res.Path, l.Heading)
	case links.Ambiguous:
		var items []kvitui.MenuItem
		for _, p := range res.Candidates {
			items = append(items, kvitui.MenuItem{Text: kvitui.PlainMenuText(p), OnSelect: func() { w.openAtHeading(p, l.Heading) }})
		}
		at := geom.NewRect(0, 0, 0, 0)
		if r, ok := w.Editor.CaretRect(); ok {
			at = r
		}
		w.say("Ambiguous link: " + l.Note + " names several notes")
		w.ui.ShowMenuAt(w.Editor, at, "Ambiguous link", items)
	default:
		w.createLinked(l.Note)
	}
}

// openAtHeading opens a note, at a heading when one is named.
func (w *Window) openAtHeading(p, heading string) {
	e := w.Vault.Find(p)
	if e == nil {
		return
	}
	w.openNote(e)
	if heading == "" || !w.Editor.GoToHeading(heading) {
		w.Editor.FocusBlock(0, 0)
	}
}

// createLinked makes the note a link names, where links.NewNoteFor puts
// it, and opens it.
func (w *Window) createLinked(target string) {
	current := ""
	if w.open != nil {
		current = w.open.Path
	}
	nn, ok := links.NewNoteFor(target, current)
	if !ok {
		w.say("Cannot make a note named “" + target + "”")
		return
	}
	if nn.Folder != "" {
		parent := ""
		for _, part := range strings.Split(nn.Folder, "/") {
			rel, err := w.Vault.CreateFolder(parent, part)
			if err != nil && !errors.Is(err, vault.ErrExists) {
				w.fail("Could not make the folder", err)
				return
			}
			if err != nil {
				rel = path.Join(parent, part)
			}
			parent = rel
		}
	}
	var err error
	var e = w.Vault.Find(nn.Path)
	if e == nil {
		if nn.Title == "" {
			e, err = w.Vault.Create(nn.Folder)
		} else {
			e, err = w.Vault.CreateTitled(nn.Folder, nn.Title)
		}
	}
	if err != nil {
		w.fail("Could not make the note", err)
		return
	}
	w.refreshScopes()
	w.refreshList()
	w.openNote(e)
	w.Editor.FocusBlock(0, 0)
}

// followAddress follows a Markdown link's address.
func (w *Window) followAddress(address string) {
	a := strings.TrimSpace(address)
	lower := strings.ToLower(a)
	switch {
	case a == "":
		return
	case strings.HasPrefix(a, "#"):
		if !w.Editor.GoToAnchor(a[1:]) {
			w.say("No heading “" + a[1:] + "” in this note")
		}
		return
	case strings.HasPrefix(lower, "http://"), strings.HasPrefix(lower, "https://"), strings.HasPrefix(lower, "mailto:"):
		if err := unison.OpenBrowser(a); err != nil {
			unison.ClipboardSetText(a)
			w.say("Could not open " + a + "; the address is on the clipboard")
		}
		return
	}
	// A path to a note: beside the note holding the link, else from the
	// top of the vault, with its #heading.
	target, heading, _ := strings.Cut(a, "#")
	target = strings.ReplaceAll(target, "%20", " ")
	if !strings.HasSuffix(strings.ToLower(target), ".md") {
		target += ".md"
	}
	var candidates []string
	if w.open != nil {
		candidates = append(candidates, path.Clean(path.Join(path.Dir(w.open.Path), target)))
	}
	candidates = append(candidates, path.Clean(strings.TrimPrefix(target, "/")))
	for _, c := range candidates {
		if e := w.Vault.Find(c); e != nil {
			w.openNote(e)
			if heading == "" || !w.Editor.GoToAnchor(heading) {
				w.Editor.FocusBlock(0, 0)
			}
			return
		}
	}
	if _, err := os.Stat(a); err == nil {
		_ = unison.OpenBrowser("file://" + a)
		return
	}
	w.say("Nothing found at " + a)
}

// say puts a short message in the status line.
func (w *Window) say(message string) {
	w.message = message
	w.update()
}

// openLinkDialog inserts a link at the selection, or edits the link the
// caret is in (Ctrl+K).
func (w *Window) openLinkDialog() {
	ed := w.Editor
	d := ed.Doc
	if d.ReadOnly {
		return
	}
	id, start, end, text, address, editing := ed.LinkAtCaret()
	if !editing {
		b := d.CaretBlock()
		if b == nil || !b.Kind.HasInline() || d.CrossBlock() {
			return
		}
		from, to := d.SelRange()
		id, start, end = b.ID, from.Off, to.Off
		text = editor.PlainText(string([]rune(b.Text)[start:end]))
	}
	before := d.Block(id).Text
	ui := w.ui
	textField := kvitui.NewField(ui)
	textField.Label = "Text"
	textField.SetText(text)
	addr := kvitui.NewField(ui)
	addr.Label = "URL"
	addr.Placeholder = "https:// or #heading"
	addr.SetText(address)
	parts := []unison.Paneler{settingRow(ui, "Text", textField), settingRow(ui, "URL", addr)}
	levels, texts, anchors := ed.Headings()
	if len(texts) > 0 {
		options := []kvitui.Option{{Value: "", Label: "— choose a heading —"}}
		for k := range texts {
			options = append(options, kvitui.Option{Value: anchors[k], Label: strings.Repeat("   ", levels[k]-1) + texts[k]})
		}
		pick := kvitui.NewSelect(ui, "Or link to a heading", options...)
		pick.OnChoose = func(anchor string) {
			if anchor == "" {
				return
			}
			addr.SetText("#" + anchor)
			if strings.TrimSpace(textField.Text()) == "" {
				for k := range anchors {
					if anchors[k] == anchor {
						textField.SetText(texts[k])
					}
				}
			}
		}
		parts = append(parts, settingRow(ui, "Or link to a heading", pick))
	}
	title := "Insert Link"
	if editing {
		title = "Edit Link"
	}
	var dlg *kvitui.Dialog
	// write splices a replacement in where the link was, unless the note
	// changed while the dialog was open.
	write := func(replacement string, caret int) {
		if b := d.Block(id); b == nil || b.Text != before {
			w.say("The note changed while the link dialog was open, so the link was not inserted")
			return
		}
		ed.ReplaceRange(id, start, end, replacement, caret)
	}
	if editing {
		remove := kvitui.NewButton(ui, "Remove link")
		remove.OnClick = func() {
			dlg.Accept()
			dlg.OnAccept = nil
			write(text, start+len([]rune(text)))
		}
		remove.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Start})
		parts = append(parts, remove)
	}
	dlg = kvitui.NewDialog(ui, title, parts...)
	dlg.ConfirmText = "OK"
	onAccept := func() {
		t, a := strings.TrimSpace(textField.Text()), strings.TrimSpace(addr.Text())
		if a == "" {
			return
		}
		if t == "" {
			t = a
		}
		t = strings.NewReplacer("[", `\[`, "]", `\]`).Replace(t)
		link := "[" + t + "](" + strings.ReplaceAll(a, " ", "%20") + ")"
		write(link, start+len([]rune(link)))
	}
	dlg.OnAccept = func() { onAccept() }
	for _, f := range []*kvitui.Field{textField, addr} {
		edit := f.Edit()
		keys := edit.KeyDownCallback
		edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
			if key == unison.KeyReturn || key == unison.KeyNumPadEnter {
				dlg.Accept()
				return true
			}
			return keys != nil && keys(key, mods, repeat)
		}
	}
	dlg.Open(w.Win)
	unison.InvokeTask(func() { addr.Focus() })
}

// completeLink is what typing after "[[" offers: the notes ranked by how
// well their names match, each written the shortest way that still names
// it; after a "#", the headings of the note named before it, or of this
// note for "[[#".
func (w *Window) completeLink(query string) []editor.Completion {
	ix := w.linkIndex()
	if note, heading, ok := strings.Cut(query, "#"); ok {
		note = strings.TrimSpace(note)
		var heads []string
		if note == "" {
			_, heads, _ = w.Editor.Headings()
		} else if p := ix.Resolve(note); p != "" {
			if page, err := w.Vault.Load(p); err == nil {
				heads = links.Headings(page.Body)
			}
		}
		var out []editor.Completion
		for _, h := range heads {
			if heading == "" || strings.Contains(strings.ToLower(h), strings.ToLower(heading)) {
				out = append(out, editor.Completion{Label: h, Detail: "heading", Insert: note + "#" + h})
			}
		}
		return out
	}
	var out []editor.Completion
	for _, e := range rankNotes(w.Vault.Entries, query, 8) {
		out = append(out, editor.Completion{Label: e.Title, Detail: e.Folder, Insert: ix.CompletionTarget(e.Path, e.Title)})
	}
	return out
}
