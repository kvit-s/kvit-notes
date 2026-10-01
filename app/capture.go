package app

// Quick capture: a small window with one text area, from File or Ctrl+Alt+N.
// Ctrl+Enter or Save note makes a note of the text at the top of the vault,
// named from its first line; Escape or Cancel closes the window. When the
// note cannot be written, the window stays open with the text in it.

import (
	"strings"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
)

// openCapture shows the quick capture window, or brings it forward.
func (w *Window) openCapture() {
	if w.capture != nil && w.capture.IsValid() {
		w.capture.ToFront()
		return
	}
	ui := w.ui
	win, err := kvitui.NewWindow(ui, "Quick capture")
	if err != nil {
		w.fail("Could not open quick capture", err)
		return
	}
	title := kvitui.NewLabel(ui, "Quick capture")
	title.Role = kvitui.RoleTitle
	area := kvitui.NewTextArea(ui)
	area.Label = "Note"
	area.Placeholder = "Jot a note… (Ctrl+Enter to save, Esc to cancel)"
	problem := kvitui.NewLabel(ui, "")
	problem.Ink, problem.Wrap = kvitui.InkDanger, true
	cancel := kvitui.NewButton(ui, "Cancel")
	save := kvitui.NewButton(ui, "Save note")
	save.Form = kvitui.ButtonPrimary
	closeIt := func() { win.Dispose() }
	doSave := func() {
		text := strings.TrimSpace(area.Text())
		if text == "" {
			closeIt()
			return
		}
		e, err := w.Vault.Capture(text + "\n")
		if err != nil {
			problem.Text = "Could not save the note — the notes folder may be read-only. The text is still here."
			problem.MarkForLayoutAndRedraw()
			win.Content().MarkForLayoutRecursively()
			return
		}
		w.refreshScopes()
		w.refreshList()
		w.message = "Captured “" + e.Title + "”"
		w.update()
		closeIt()
	}
	cancel.OnClick = closeIt
	save.OnClick = doSave
	win.OnKeyDown = func(key unison.KeyCode, mods mod.Modifiers, _ bool) bool {
		switch {
		case key == unison.KeyEscape && mods == 0:
			closeIt()
			return true
		case (key == unison.KeyReturn || key == unison.KeyNumPadEnter) && mods.OSMenuCommandDown():
			doSave()
			return true
		}
		return false
	}
	area.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	buttons := kvitui.Row(ui, kvitui.SizeSpaceSnug, cancel, save)
	buttons.SetLayoutData(&unison.FlexLayoutData{HAlign: align.End, HGrab: true})
	body := kvitui.Column(ui, kvitui.SizeSpace, title, area, problem, buttons)
	body.SetBorder(kvitui.Padding(ui, kvitui.SizeSpaceLoose))
	win.SetBody(body)
	win.SidebarVisible = false
	win.SetContentRect(geom.NewRect(0, 0, float32(ui.Interface.Px(460)), float32(ui.Interface.Px(240))))
	if r := w.Win.FrameRect(); r.Width > 0 {
		win.SetFrameRect(geom.NewRect(r.X+(r.Width-win.FrameRect().Width)/2, r.Y+r.Height/4, win.FrameRect().Width, win.FrameRect().Height))
	}
	w.capture = win
	win.ToFront()
	unison.InvokeTask(area.Focus)
}
