package app

// What the reader does to notes and folders from the lists: the menus a
// right-click (or the Menu key, or Shift+F10) opens on a note or a folder,
// renaming, pinning, marking favourites, and moving to the trash; and the
// title an "Untitled" note takes from its first block.

import (
	"errors"
	"fmt"
	"strings"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"
)

// installMenus gives the note list and the sidebar their menus and keys.
func (w *Window) installMenus() {
	w.ui.SetContextMenu(w.list, func(where geom.Point) (string, []kvitui.MenuItem) {
		i := w.list.rowAt(where.Y)
		if where == (geom.Point{}) {
			i = w.list.Current
		}
		if w.scope.Kind == ScopeTrash {
			if i < 0 || i >= len(w.trashList) {
				return "", nil
			}
			return "Trash", w.trashMenu(w.trashList[i])
		}
		if i < 0 || i >= len(w.shown) {
			return "", nil
		}
		return "Note", w.noteMenu(w.shown[i])
	})
	w.ui.SetContextMenu(w.scopes, func(where geom.Point) (string, []kvitui.MenuItem) {
		i := w.scopes.rowAt(where)
		if where == (geom.Point{}) {
			i = w.scopes.currentRow()
		}
		if i < 0 {
			return "", nil
		}
		switch sc := w.scopes.Rows[i].Scope; sc.Kind {
		case ScopeFolder:
			return "Folder", w.folderMenu(sc.Path)
		case ScopeTag:
			return "Tag", w.tagMenu(sc.Path)
		}
		return "", nil
	})
	keys := w.list.KeyDownCallback
	w.list.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		i := w.list.Current
		if w.scope.Kind == ScopeTrash {
			if i >= 0 && i < len(w.trashList) && (key == unison.KeyDelete || key == unison.KeyBackspace) {
				w.confirmDeleteForever(w.trashList[i])
				return true
			}
			return keys(key, mods, repeat)
		}
		if i >= 0 && i < len(w.shown) {
			switch key {
			case unison.KeyDelete, unison.KeyBackspace:
				w.confirmTrash(w.shown[i])
				return true
			case unison.KeyF2:
				w.askRename(w.shown[i])
				return true
			}
		}
		return keys(key, mods, repeat)
	}
}

func (w *Window) noteMenu(e *vault.Entry) []kvitui.MenuItem {
	pin, fav := "Pin to top", "Add to favorites"
	if e.Pinned {
		pin = "Unpin"
	}
	if e.Favorite {
		fav = "Remove from favorites"
	}
	return []kvitui.MenuItem{
		{Text: "Rename…", OnSelect: func() { w.askRename(e) }},
		{Text: pin, OnSelect: func() { w.changeMeta(e, func(p *vault.Page) { p.SetPinned(!e.Pinned) }) }},
		{Text: fav, OnSelect: func() { w.changeMeta(e, func(p *vault.Page) { p.SetFavorite(!e.Favorite) }) }},
		{Separator: true},
		{Text: "Move to trash", Danger: true, OnSelect: func() { w.confirmTrash(e) }},
	}
}

func (w *Window) folderMenu(folder string) []kvitui.MenuItem {
	return []kvitui.MenuItem{
		{Text: "&New note", OnSelect: func() { w.scope = Scope{Kind: ScopeFolder, Path: folder}; w.newNote() }},
		{Text: "New &subfolder…", OnSelect: func() { w.scope = Scope{Kind: ScopeFolder, Path: folder}; w.newFolder() }},
		{Text: "&Rename…", OnSelect: func() { w.askRenameFolder(folder) }},
		{Text: "&Color", Items: w.colorItems(w.Vault.State.Folders[folder].Color, func(c string) {
			w.Vault.State.SetFolderColor(folder, c)
			_ = w.Vault.SaveState()
			w.refreshScopes()
		})},
		{Separator: true},
		{Text: "&Delete…", Danger: true, OnSelect: func() { w.confirmTrashFolder(folder) }},
	}
}

// fail says in the status line that something could not be done.
func (w *Window) fail(what string, err error) {
	switch {
	case errors.Is(err, vault.ErrName):
		w.message = what + ": a name cannot be empty, start or end with a space, start with a dot, or hold a slash"
	case errors.Is(err, vault.ErrExists):
		w.message = what + ": that name is taken"
	default:
		w.message = what + ": " + err.Error()
	}
	w.update()
}

// askName asks for a new name in a dialog.
func (w *Window) askName(title, current string, apply func(name string)) {
	w.askNameWith(title, current, "Rename", apply)
}

// askNameWith asks for a name in a dialog whose button says confirm.
func (w *Window) askNameWith(title, current, confirm string, apply func(name string)) {
	field := kvitui.NewField(w.ui)
	field.Label = "Name"
	field.SetText(current)
	d := kvitui.NewDialog(w.ui, title, field)
	d.ConfirmText = confirm
	d.OnAccept = func() { apply(strings.TrimSpace(field.Text())) }
	edit := field.Edit()
	keys := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		if key == unison.KeyReturn || key == unison.KeyNumPadEnter {
			d.Accept()
			return true
		}
		return keys != nil && keys(key, mods, repeat)
	}
	d.Open(w.Win)
	// The dialog gives its button the focus as it opens; the name is what
	// is being asked for, so the field takes it after that.
	unison.InvokeTask(func() {
		field.Focus()
		field.Edit().SelectAll()
	})
}

func (w *Window) askRename(e *vault.Entry) {
	w.renameInRow(e)
}

// renameInRow renames a note in the note list's row itself: a field over
// the row, seeded with the title, Enter renames, Escape leaves it. When
// the note's row is not shown the dialog asks instead.
func (w *Window) renameInRow(e *vault.Entry) {
	i := -1
	for k, shown := range w.shown {
		if shown == e {
			i = k
			break
		}
	}
	if i < 0 {
		w.askName("Rename note", e.Title, func(name string) { w.applyRename(e, name) })
		return
	}
	ui := w.ui
	field := kvitui.NewField(ui)
	field.Label = "Rename note"
	field.SetText(e.Title)
	commit := func() {
		name := strings.TrimSpace(field.Text())
		if name == "" || name == e.Title {
			return
		}
		w.applyRename(e, name)
	}
	var hide func()
	hide = w.Win.Show(&kvitui.Popup{Panel: kvitui.Width(ui, kvitui.Px(220), field), Anchor: w.list,
		OnEscape:       func() { hide() },
		OnPressOutside: func() { hide() },
		Place: func(bounds geom.Rect, size geom.Size) geom.Rect {
			h := w.list.rowHeight()
			row := geom.NewRect(0, float32(i)*h, w.list.ContentRect(false).Width, h)
			r := w.Win.Content().RectFromRoot(w.list.RectToRoot(row))
			width := min(r.Width, max(size.Width, 100))
			return geom.NewRect(r.X, r.Y, width, min(size.Height, r.Height))
		}})
	edit := field.Edit()
	prev := edit.KeyDownCallback
	edit.KeyDownCallback = func(key unison.KeyCode, mods mod.Modifiers, repeat bool) bool {
		switch key {
		case unison.KeyReturn, unison.KeyNumPadEnter:
			commit()
			hide()
			return true
		case unison.KeyEscape:
			hide()
			return true
		}
		if prev != nil {
			return prev(key, mods, repeat)
		}
		return false
	}
	unison.InvokeTask(func() {
		field.Focus()
		field.Edit().SelectAll()
	})
}

// applyRename renames a note after a dialog asked for its name.
func (w *Window) applyRename(e *vault.Entry, name string) {
	if name == e.Title {
		return
	}
	w.relocate(e, func() error { return w.Vault.Rename(e, name) }, func() {
		w.refreshScopes()
		w.refreshList()
		w.update()
	})
}

func (w *Window) askRenameFolder(folder string) {
	w.askName("Rename folder", pathBase(folder), func(name string) {
		w.relocateFolder(folder, func() (string, error) { return w.Vault.RenameFolder(folder, name) }, func(to string) {
			w.folderRenamed(folder, to)
		})
	})
}

// folderRenamed follows a folder's new name in the sidebar and the list.
func (w *Window) folderRenamed(folder, to string) {
	if w.scope.Kind == ScopeFolder && (w.scope.Path == folder || strings.HasPrefix(w.scope.Path, folder+"/")) {
		w.scope.Path = to + strings.TrimPrefix(w.scope.Path, folder)
	}
	w.refreshScopes()
	w.refreshList()
	w.update()
}

func pathBase(p string) string {
	if i := strings.LastIndex(p, "/"); i >= 0 {
		return p[i+1:]
	}
	return p
}

// confirmTrash asks before moving a note to the trash.
func (w *Window) confirmTrash(e *vault.Entry) {
	d := kvitui.NewDialog(w.ui, "Move “"+e.Title+"” to the trash?")
	d.Detail = "It can be taken back out of .kvit/trash in the vault's folder."
	d.ConfirmText, d.Destructive = "Move to trash", true
	d.OnAccept = func() { w.trash(e) }
	d.Open(w.Win)
}

func (w *Window) trash(e *vault.Entry) {
	if e == w.open {
		w.saveNow()
	}
	if err := w.Vault.Trash(e); err != nil {
		w.fail("Could not move the note to the trash", err)
		return
	}
	if e == w.open {
		w.open, w.page = nil, nil
		w.Editor.SetDoc(editor.NewDoc(nil))
		w.tags.SetTags(nil)
	}
	w.refreshScopes()
	w.refreshList()
	if w.open == nil && len(w.shown) > 0 {
		w.openNote(w.shown[min(max(w.list.Current, 0), len(w.shown)-1)])
	}
	w.update()
}

func (w *Window) confirmTrashFolder(folder string) {
	d := kvitui.NewDialog(w.ui, "Move the folder “"+pathBase(folder)+"” to the trash?")
	d.Detail = "Everything in it goes with it, and can be taken back out of .kvit/trash in the vault's folder."
	d.ConfirmText, d.Destructive = "Move to trash", true
	d.OnAccept = func() {
		if w.open != nil && strings.HasPrefix(w.open.Path, folder+"/") {
			w.saveNow()
			w.open, w.page = nil, nil
			w.Editor.SetDoc(editor.NewDoc(nil))
			w.tags.SetTags(nil)
		}
		if err := w.Vault.TrashFolder(folder); err != nil {
			w.fail("Could not move the folder to the trash", err)
			return
		}
		if w.scope.Kind == ScopeFolder && (w.scope.Path == folder || strings.HasPrefix(w.scope.Path, folder+"/")) {
			w.scope = Scope{Kind: ScopeAll}
		}
		w.refreshScopes()
		w.refreshList()
		w.update()
	}
	d.Open(w.Win)
}

// changeMeta changes a note's front matter and saves it: the open note's
// page, or the note read from its file.
func (w *Window) changeMeta(e *vault.Entry, change func(p *vault.Page)) {
	p := w.page
	if e != w.open {
		var err error
		if p, err = w.Vault.Load(e.Path); err != nil {
			w.fail("Could not change the note", err)
			return
		}
	} else {
		w.saveNow()
	}
	change(p)
	if err := w.Vault.Save(e, p); err != nil {
		w.fail("Could not change the note", err)
		return
	}
	w.refreshScopes()
	w.refreshList()
}

// autoTitle names an "Untitled" note after its first block once the reader
// has finished it: the caret has left it. It happens once, while the note still has the automatic name.
func (w *Window) autoTitle() {
	e, d := w.open, w.Editor.Doc
	if e == nil || !e.Untitled() || len(d.Blocks) == 0 {
		return
	}
	first := d.Blocks[0]
	// Only the kinds whose text describes the note name it: a note that
	// opens with code, a table, a picture or an equation is left alone.
	switch first.Kind {
	case editor.Paragraph, editor.Heading1, editor.Heading2, editor.Heading3, editor.Heading4,
		editor.Bullet, editor.Numbered, editor.Todo, editor.Quote, editor.Callout:
	default:
		return
	}
	if d.Focused && d.Caret.Block == first.ID {
		return
	}
	r := []rune(first.Text)
	title := vault.TitleFromText(string(r[:min(len(r), 120)]))
	if title == "" || title == e.Title {
		return
	}
	w.saveNow()
	if err := w.Vault.Rename(e, title); err != nil {
		// A taken name, or one that cannot be a file name, leaves the note
		// as it is.
		return
	}
	w.refreshList()
}

// colorItems are a menu of the theme's palette for a folder or tag, the
// current colour ticked, and No color.
func (w *Window) colorItems(current string, set func(color string)) []kvitui.MenuItem {
	names := tokens.ColorPaletteNames()
	var items []kvitui.MenuItem
	for i, c := range tokens.ColorPalette() {
		hex := c.Hex()
		items = append(items, kvitui.MenuItem{Text: names[i], Checked: strings.EqualFold(current, hex), OnSelect: func() { set(hex) }})
	}
	return append(items, kvitui.MenuItem{Separator: true}, kvitui.MenuItem{Text: "No color", Checked: current == "",
		OnSelect: func() { set("") }})
}

// tagMenu is a tag's menu in the sidebar.
func (w *Window) tagMenu(tag string) []kvitui.MenuItem {
	return []kvitui.MenuItem{
		{Text: "&Rename…", OnSelect: func() { w.askRenameTag(tag) }},
		{Text: "&Color", Items: w.colorItems(w.Vault.State.TagColors[tag], func(c string) {
			w.Vault.State.SetTagColor(tag, c)
			_ = w.Vault.SaveState()
			w.refreshScopes()
		})},
		{Separator: true},
		{Text: "&Delete…", Danger: true, OnSelect: func() { w.confirmDeleteTag(tag) }},
	}
}

// askRenameTag renames a tag on every note, asking first when the new name
// is a tag already, since that merges the two.
func (w *Window) askRenameTag(tag string) {
	w.askName("Edit Tag", tag, func(name string) {
		if name == "" || name == tag {
			return
		}
		apply := func() {
			w.saveNow()
			if err := w.Vault.RenameTag(tag, name); err != nil {
				w.fail("Could not rename the tag", err)
			}
			if w.scope.Kind == ScopeTag && w.scope.Path == tag {
				w.scope.Path = name
			}
			if w.open != nil {
				w.reload()
			}
			w.refreshScopes()
			w.refreshList()
		}
		if w.Vault.TagCount(name) == 0 {
			apply()
			return
		}
		d := kvitui.NewDialog(w.ui, "Merge Tags")
		d.Detail = fmt.Sprintf("Merge “%s” into “%s”? %d note(s) will be retagged.", tag, name, w.Vault.TagCount(tag))
		d.ConfirmText = "Merge"
		d.OnAccept = apply
		d.Open(w.Win)
	})
}

// confirmDeleteTag takes a tag off every note, once agreed.
func (w *Window) confirmDeleteTag(tag string) {
	d := kvitui.NewDialog(w.ui, "Delete Tag")
	d.Detail = fmt.Sprintf("Remove tag “%s” from %d note(s)?", tag, w.Vault.TagCount(tag))
	d.ConfirmText, d.Destructive = "Remove", true
	d.OnAccept = func() {
		w.saveNow()
		if err := w.Vault.DeleteTag(tag); err != nil {
			w.fail("Could not delete the tag", err)
		}
		if w.scope.Kind == ScopeTag && w.scope.Path == tag {
			w.scope = Scope{Kind: ScopeAll}
		}
		if w.open != nil {
			w.reload()
		}
		w.refreshScopes()
		w.refreshList()
	}
	d.Open(w.Win)
}
