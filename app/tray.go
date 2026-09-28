package app

// The tray icon (features.md 15.2), as the Qt app's SystemIntegration.qml
// and kvitapplication.cpp use it: the icon with the tooltip "Kvit Notes"
// and its menu, New Note, Quick Capture…, Show Kvit and Quit; a click on
// the icon showing the window; and, when the reader turned on
// tray.closeToTray in Settings, closing the last window hiding it with its
// vault still open, where Show Kvit or a click on the icon brings it back.
// The icon is kvit-ui's platform.Tray, shown only where the desktop has a
// notification area.
//
// Native notifications (features.md 15.4) go through the same tray. The Qt
// app posts none of its own: only its tests call SystemTray.notify, since
// the reminders and sync status 15.4 names do not exist (sync is out of
// scope, section 20). So this app posts none either.

import (
	"image"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/platform"
	"github.com/richardwilkes/unison"
)

// closeToTrayKey is the setting that keeps the app running in the tray when
// its last window is closed; off unless the reader turns it on.
const closeToTrayKey = "tray.closeToTray"

var (
	// tray is the app's icon while it is shown; nil where the desktop has
	// no notification area.
	tray *platform.Tray
	// mainWindows are the windows closing to the tray applies to: the
	// vault windows and the note-file windows, in the order they opened.
	mainWindows []*kvitui.Window
	// hiddenToTray is the window the last close hid, which Show Kvit brings
	// back.
	hiddenToTray *kvitui.Window
	// quitting is set while the app quits, when every window closes rather
	// than hiding.
	quitting bool
)

// StartTray shows the app's tray icon, where the desktop has a notification
// area, with the Qt app's tooltip and menu. icon is the app's icon at the
// sizes there are.
func StartTray(t *platform.Tray, icon ...image.Image) {
	if !t.Available() {
		return
	}
	tray = t
	t.SetIcon(icon...)
	t.SetTooltip("Kvit Notes")
	t.SetMenu([]platform.TrayItem{
		{Text: "New Note", OnSelect: trayNewNote},
		{Text: "Quick Capture…", OnSelect: trayCapture},
		{Separator: true},
		{Text: "Show Kvit", OnSelect: ShowKvit},
		{Separator: true},
		{Text: "Quit", OnSelect: Quit},
	})
	t.OnClick = ShowKvit
	t.Show()
}

// Tray is the app's tray icon while it is shown, or nil.
func Tray() *platform.Tray { return tray }

// AllowQuit is unison's AllowQuitCallback: a quit, however it was asked
// for, closes every window rather than hiding the last one. A window that
// refuses to close leaves the app running, and the setting applies again.
func AllowQuit() bool {
	quitting = true
	unison.InvokeTask(func() { quitting = false })
	return true
}

// Quitting is unison's QuittingCallback: it takes the icon out of the
// notification area, where Windows would otherwise leave it until the
// pointer passed over it.
func Quitting() {
	if tray != nil {
		tray.Close()
		tray = nil
	}
}

// Quit is the tray's Quit: it asks every window to close, which saves its
// note, and ends the app when all have (the Qt app's quit from the tray).
func Quit() {
	quitting = true
	unison.AttemptQuit()
	quitting = false
}

// ClosesToTray makes a window one that closing to the tray applies to:
// when it is the last one open, the icon is shown and the reader asked for
// it, closing it runs save and hides it instead. A window closed while
// another is hidden in the tray closes, and the app goes on running for the
// hidden one. The vault windows are
// made so as they open; the command makes its note-file windows so.
func ClosesToTray(ui *kvitui.UI, win *kvitui.Window, save func()) {
	mainWindows = append(mainWindows, win)
	win.AllowCloseCallback = func() bool {
		if quitting || tray == nil || !tray.Visible() || !newPrefs(ui).bool(closeToTrayKey, false) || otherWindowOpen(win) {
			return true
		}
		if save != nil {
			save()
		}
		win.Hide()
		hiddenToTray = win
		return false
	}
	win.WillCloseCallback = chain(win.WillCloseCallback, func() {
		for i, m := range mainWindows {
			if m == win {
				mainWindows = append(mainWindows[:i], mainWindows[i+1:]...)
				break
			}
		}
		if hiddenToTray == win {
			hiddenToTray = nil
		}
	})
}

// otherWindowOpen reports whether a window closing to the tray applies to,
// other than win, is open, on the screen or hidden in the tray.
func otherWindowOpen(win *kvitui.Window) bool {
	for _, m := range mainWindows {
		if m != win && m.IsValid() {
			return true
		}
	}
	return false
}

// trayWindow is the window the tray acts on: the one the last close hid, or
// the one in front, or the one opened last.
func trayWindow() *kvitui.Window {
	if hiddenToTray != nil && hiddenToTray.IsValid() {
		return hiddenToTray
	}
	var last *kvitui.Window
	for _, m := range mainWindows {
		if !m.IsValid() {
			continue
		}
		if m.Window == unison.ActiveWindow() {
			return m
		}
		last = m
	}
	return last
}

// trayVault is the vault window the tray's New Note and Quick Capture act
// on: the tray's window when it is a vault window, else the vault window
// opened last; nil with none open, when the two do nothing, as in the Qt
// app, where capture needs somewhere to put the note.
func trayVault() *Window {
	target := trayWindow()
	var last *Window
	for _, w := range windows {
		if !w.Win.IsValid() {
			continue
		}
		if w.Win == target {
			return w
		}
		last = w
	}
	return last
}

// ShowKvit brings the app's window forward, showing it if the last close
// hid it.
func ShowKvit() {
	if win := trayWindow(); win != nil {
		hiddenToTray = nil
		win.ToFront()
	}
}

// trayNewNote makes a note in the vault window, as Ctrl+N does, and shows
// the window, which the Qt app leaves where it is: a note made in a window
// hidden in the tray could not be seen.
func trayNewNote() {
	w := trayVault()
	if w == nil || w.Vault.ReadOnly {
		return
	}
	if w.Win == hiddenToTray {
		hiddenToTray = nil
	}
	w.Win.ToFront()
	w.newNote()
}

// trayCapture opens quick capture over the vault window, which stays
// hidden if it is.
func trayCapture() {
	if w := trayVault(); w != nil && !w.Vault.ReadOnly {
		w.openCapture()
	}
}
