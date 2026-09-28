package main

// The tray icon (features.md 15.2): the app's icon at the sizes
// packaging/icons has, copied into icons/ so the program carries them, and
// the tray check, which shows the icon on a real desktop, posts one
// notification, prints what the tray reports, and quits.

import (
	"bytes"
	"embed"
	"fmt"
	"image"
	"image/png"
	"time"

	"github.com/kvit-s/kvit-notes/app"
	"github.com/kvit-s/kvit-ui/platform"
	"github.com/richardwilkes/unison"
)

//go:embed icons/*.png
var iconFiles embed.FS

// appIcon is the app's icon at each size it comes in.
func appIcon() []image.Image {
	entries, _ := iconFiles.ReadDir("icons")
	var sizes []image.Image
	for _, e := range entries {
		data, err := iconFiles.ReadFile("icons/" + e.Name())
		if err != nil {
			continue
		}
		if img, err := png.Decode(bytes.NewReader(data)); err == nil {
			sizes = append(sizes, img)
		}
	}
	return sizes
}

// startTray shows the tray icon where the desktop has a notification area.
func startTray() {
	app.StartTray(platform.NewTray(), appIcon()...)
}

// trayCheckID is the ID of the tray check's notification.
const trayCheckID = "tray-check"

// runTrayCheck prints what the tray is and each thing it reports, posts a
// notification, and quits after the check's time. It is what
// --tray-check adds to a vault window.
func runTrayCheck(d time.Duration) {
	t := app.Tray()
	if t == nil {
		fmt.Println("tray: this desktop has no notification area")
		unison.InvokeTaskAfter(app.Quit, d)
		return
	}
	fmt.Printf("tray: available, tooltip %q, notifications %v\n", t.Tooltip(), t.Authorization())
	menu := t.Menu()
	for i, it := range menu {
		if it.Separator {
			continue
		}
		run, text := it.OnSelect, it.Text
		menu[i].OnSelect = func() {
			fmt.Printf("tray: menu %q chosen\n", text)
			if run != nil {
				run()
			}
		}
	}
	t.SetMenu(menu)
	click := t.OnClick
	t.OnClick = func() {
		fmt.Println("tray: icon clicked")
		if click != nil {
			click()
		}
	}
	t.OnNotificationClick = func(id string) { fmt.Printf("tray: notification %q clicked\n", id) }
	posted := t.Notify("Kvit Notes", "Tray check: a notification from the tray icon", trayCheckID)
	fmt.Printf("tray: notification posted %v\n", posted)
	unison.InvokeTaskAfter(func() {
		fmt.Println("tray: check over, quitting")
		app.Quit()
	}, d)
}
