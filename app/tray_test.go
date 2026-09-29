package app

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/platform"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/toolbox/v2/uti"
	"github.com/richardwilkes/unison/drag"
)

// withTray shows a test tray, which stands in for the desktop's, over a
// session's window, as the command's startTray does.
func (s *session) withTray() *platform.Tray {
	s.t.Helper()
	t := platform.NewTestTray(platform.Authorized)
	s.do(func() { StartTray(t) })
	s.t.Cleanup(func() { tray, hiddenToTray, quitting = nil, nil, false })
	return t
}

// choose chooses a line of the tray's menu and waits for it to run.
func (s *session) choose(t *platform.Tray, text string) {
	s.t.Helper()
	if !t.Choose(text) {
		s.t.Fatalf("no tray menu line %q", text)
	}
	s.screen.Sync()
}

func (s *session) shown() bool {
	var shown bool
	s.do(func() { shown = s.w.Win.IsVisible() })
	return shown
}

// features.md 15.2: the icon has the app's tooltip and menu.
func TestTheTrayIconAndItsMenu(t *testing.T) {
	s := openVault(t, demo)
	tr := s.withTray()
	if !tr.Visible() || tr.Tooltip() != "Kvit Notes" {
		t.Fatalf("tray shown %v, tooltip %q", tr.Visible(), tr.Tooltip())
	}
	var lines []string
	for _, it := range tr.Menu() {
		if it.Separator {
			lines = append(lines, "---")
		} else {
			lines = append(lines, it.Text)
		}
	}
	if want := []string{"New Note", "Quick Capture…", "---", "Show Kvit", "---", "Quit"}; !slices.Equal(lines, want) {
		t.Errorf("tray menu %q, want %q", lines, want)
	}
}

// New Note makes a note in the vault window and shows it; Quick Capture…
// opens the capture window over it.
func TestTheTrayMenuActsOnTheVaultWindow(t *testing.T) {
	s := openVault(t, demo)
	tr := s.withTray()
	before := len(s.listed())
	s.choose(tr, "New Note")
	if got := len(s.listed()); got != before+1 {
		t.Errorf("New Note: %d notes listed, was %d", got, before)
	}
	if !s.shown() {
		t.Error("New Note should show the window")
	}
	s.choose(tr, "Quick Capture…")
	var capture bool
	s.do(func() { capture = s.w.capture != nil && s.w.capture.IsValid() })
	if !capture {
		t.Error("Quick Capture… should open the capture window")
	}
}

// With tray.closeToTray on, closing the last window hides it with its vault
// open, and Show Kvit or a click on the icon brings it back.
func TestClosingTheLastWindowHidesItInTheTray(t *testing.T) {
	s := openVault(t, notes{"A.md": "Alpha\n"})
	tr := s.withTray()
	s.do(func() { newPrefs(s.w.ui).set(closeToTrayKey, true) })
	s.do(func() { s.w.Editor.FocusBlock(0, 0) })
	s.screen.Type("Edited ")
	var closed bool
	s.do(func() { closed = s.w.Win.AttemptClose() })
	if closed || s.shown() {
		t.Fatalf("closed %v, shown %v: the window should only hide", closed, s.shown())
	}
	if got := s.file("A.md"); got != "Edited Alpha\n" {
		t.Errorf("hiding should save the note first: %q", got)
	}
	s.choose(tr, "Show Kvit")
	if !s.shown() {
		t.Fatal("Show Kvit should bring the window back")
	}
	s.do(func() { s.w.Win.AttemptClose() })
	tr.Click()
	s.screen.Sync()
	if !s.shown() {
		t.Error("a click on the icon should bring the window back")
	}
}

// A window that is not the last one closes; so does the last one with the
// setting off, which ends the app.
func TestClosingToTheTrayIsOnlyForTheLastWindowAndOnlyWhenAsked(t *testing.T) {
	s := openVault(t, demo)
	s.withTray()
	var other *Window
	s.do(func() {
		newPrefs(s.w.ui).set(closeToTrayKey, true)
		var err error
		if other, err = OpenVault(s.w.ui, t.TempDir()); err != nil {
			t.Fatal(err)
		}
	})
	var closed bool
	s.do(func() { closed = other.Win.AttemptClose() })
	if !closed {
		t.Error("a window other than the last should close")
	}
	// With the first window hidden in the tray, a window opened later
	// closes rather than hiding too, and Show Kvit brings back the first.
	s.do(func() {
		s.w.Win.AttemptClose()
		var err error
		if other, err = OpenVault(s.w.ui, t.TempDir()); err != nil {
			t.Fatal(err)
		}
		closed = other.Win.AttemptClose()
	})
	if !closed || s.shown() {
		t.Errorf("the later window should close (closed %v) with the first still hidden (shown %v)", closed, s.shown())
	}
	s.do(ShowKvit)
	if !s.shown() {
		t.Error("Show Kvit should bring back the window hidden first")
	}
	s.do(func() {
		newPrefs(s.w.ui).set(closeToTrayKey, false)
		closed = s.w.Win.AttemptClose()
	})
	if !closed {
		t.Error("with the setting off, the last window should close")
	}
	waitDone(t, s)
}

// Quit closes every window, the hidden one too, and ends the app.
func TestQuitFromTheTrayEndsTheApp(t *testing.T) {
	s := openVault(t, notes{"A.md": "Alpha\n"})
	tr := s.withTray()
	s.do(func() { newPrefs(s.w.ui).set(closeToTrayKey, true) })
	s.do(func() { s.w.Win.AttemptClose() })
	if s.shown() {
		t.Fatal("the window should be hidden in the tray")
	}
	tr.Choose("Quit")
	waitDone(t, s)
}

func waitDone(t *testing.T, s *session) {
	t.Helper()
	select {
	case <-s.screen.Done():
	case <-time.After(5 * time.Second):
		t.Fatal("the app did not end")
	}
}

// The General section of Settings, with the tray setting, is there only
// with a tray, as the app shows its tray setting only where one exists.
func TestTheTraySettingIsShownOnlyWithATray(t *testing.T) {
	s := openVault(t, demo)
	s.press("File")
	s.press("Settings…")
	if s.has("General") {
		t.Error("General should not be offered without a tray")
	}
	s.press("Close")
	s.withTray()
	s.press("File")
	s.press("Settings…")
	s.press("General")
	s.press("Keep running in the tray when the window is closed")
	if !newPrefs(s.w.ui).bool(closeToTrayKey, false) {
		t.Error("the check should turn tray.closeToTray on")
	}
}

// has reports whether a screen reader finds a button by this name.
func (s *session) has(name string) bool {
	tree := s.screen.AccessibilityTree(s.w.Win.Window)
	for _, n := range tree.Nodes {
		if n.Name == name {
			return true
		}
	}
	return false
}

// features.md 15.3: a note of the vault dropped on the window opens in it;
// a Markdown file from elsewhere opens on its own; anything else is not
// taken.
func TestDroppedNoteFilesOpen(t *testing.T) {
	s := openVault(t, notes{"A.md": "Alpha\n", "B.md": "Beta\n"})
	outside := filepath.Join(t.TempDir(), "Elsewhere.md")
	if err := os.WriteFile(outside, []byte("Elsewhere\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	var opened []string
	saved := OpenFile
	OpenFile = func(_ *kvitui.UI, p string) { opened = append(opened, p) }
	t.Cleanup(func() { OpenFile = saved })

	drop := func(paths ...string) drag.Op {
		var data []byte
		for _, p := range paths {
			data = append(data, []byte(p+"\n")...)
		}
		var to geom.Point
		s.do(func() { to = s.screen.PanelCenter(s.w.Editor) })
		return s.screen.DropExternal(geom.NewPoint(5, 5), to, 3, drag.Copy|drag.Move,
			drag.Data{Type: uti.FileURL, Data: data}).Op
	}
	s.clickRow(0)
	first := s.openTitle()
	other := "B"
	if first == "B" {
		other = "A"
	}
	drop(filepath.Join(s.root, other+".md"))
	if s.openTitle() != other {
		t.Errorf("dropping %s.md should open it in the window, open is %q", other, s.openTitle())
	}
	drop(outside)
	if !slices.Equal(opened, []string{outside}) {
		t.Errorf("a file from outside the vault should open on its own: %q", opened)
	}
	picture := filepath.Join(t.TempDir(), "photo.png")
	if op := drop(picture); op == drag.None {
		t.Errorf("a picture should be taken by the editor: op %v", op)
	}
	if !slices.Equal(opened, []string{outside}) {
		t.Errorf("a picture should not open as a note: opened %q", opened)
	}
	var found bool
	s.do(func() {
		for _, b := range s.w.Editor.Doc.Blocks {
			if b.Kind == editor.Image && strings.Contains(b.Text, "photo.png") {
				found = true
			}
		}
	})
	if !found {
		t.Errorf("a dropped picture should land as an image block")
	}
}
