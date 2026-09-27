package app

// Acting on several notes at once (features.md 8.3, Kvit's note list bulk
// bar): Ctrl+click picks notes in the list, Shift+click a run of them, and a
// bar over the list then pins, marks as favourite, tags or moves to the
// trash every note picked, or clears the pick.

import (
	"fmt"
	"slices"

	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
)

// newBulkRow is the bar shown while notes are picked.
func (w *Window) newBulkRow() *unison.Panel {
	ui := w.ui
	w.bulkCount = kvitui.NewLabel(ui, "")
	w.bulkCount.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true, VAlign: align.Middle})
	pin := kvitui.NewIconButton(ui, "push-pin", "Pin or unpin the notes picked")
	pin.OnClick = func() {
		on := !w.allPicked(func(e *vault.Entry) bool { return e.Pinned })
		w.eachPicked(func(p *vault.Page) { p.SetPinned(on) })
	}
	fav := kvitui.NewIconButton(ui, "star", "Add the notes picked to favorites, or take them off")
	fav.OnClick = func() {
		on := !w.allPicked(func(e *vault.Entry) bool { return e.Favorite })
		w.eachPicked(func(p *vault.Page) { p.SetFavorite(on) })
	}
	tag := kvitui.NewIconButton(ui, "hash", "Tag the notes picked")
	tag.OnClick = func() {
		w.askNameWith("Tag the notes picked", "", "Tag", func(name string) {
			if name == "" {
				return
			}
			w.eachPicked(func(p *vault.Page) {
				if !slices.Contains(p.Tags(), name) {
					p.SetTags(append(p.Tags(), name))
				}
			})
		})
	}
	trash := kvitui.NewIconButton(ui, "trash", "Move the notes picked to the trash")
	trash.OnClick = w.confirmTrashPicked
	clearB := kvitui.NewIconButton(ui, "close", "Clear the pick")
	clearB.OnClick = w.list.ClearSelection
	row := kvitui.Row(ui, kvitui.SizeSpaceSnug, w.bulkCount, pin, fav, tag, trash, clearB)
	row.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	return row
}

// picked are the notes picked in the list.
func (w *Window) picked() []*vault.Entry {
	var out []*vault.Entry
	for _, i := range w.list.SelectedRows() {
		if i < len(w.shown) && w.scope.Kind != ScopeTrash {
			out = append(out, w.shown[i])
		}
	}
	return out
}

func (w *Window) allPicked(test func(*vault.Entry) bool) bool {
	for _, e := range w.picked() {
		if !test(e) {
			return false
		}
	}
	return true
}

// eachPicked changes the front matter of every note picked.
func (w *Window) eachPicked(change func(p *vault.Page)) {
	w.saveNow()
	for _, e := range w.picked() {
		p, err := w.Vault.Load(e.Path)
		if err == nil {
			change(p)
			err = w.Vault.Save(e, p)
		}
		if err != nil {
			w.fail("Could not change "+e.Title, err)
		}
		if e == w.open {
			w.reload()
		}
	}
	w.refreshScopes()
	w.refreshList()
}

// confirmTrashPicked moves the notes picked to the trash, once agreed.
func (w *Window) confirmTrashPicked() {
	notes := w.picked()
	if len(notes) == 0 {
		return
	}
	d := kvitui.NewDialog(w.ui, fmt.Sprintf("Move %d notes to the trash?", len(notes)))
	d.Detail = "They can be put back from the trash."
	d.ConfirmText, d.Destructive = "Move to trash", true
	d.OnAccept = func() {
		w.list.ClearSelection()
		for _, e := range notes {
			w.trash(e)
		}
	}
	d.Open(w.Win)
}

// showBulk shows the bar while notes are picked.
func (w *Window) showBulk() {
	n := len(w.list.SelectedRows())
	w.bulkCount.Text = fmt.Sprintf("%d selected", n)
	w.bulkCount.MarkForRedraw()
	on := n > 0 && w.scope.Kind != ScopeTrash
	if on == (w.bulkRow.Parent() != nil) {
		return
	}
	if on {
		w.listHead.AddChild(w.bulkRow)
	} else {
		w.bulkRow.RemoveFromParent()
	}
	w.relayout()
}
