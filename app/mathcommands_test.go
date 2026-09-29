package app

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"testing"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/mod"

	"github.com/kvit-s/kvit-notes/mathcmd"
)

// The commands chosen from the math menu are written to the settings file
// under the app's key, and the next start reads them back, so they lead
// the menu again.
func TestMathRecentCommandsRoundTrip(t *testing.T) {
	path := filepath.Join(t.TempDir(), "ui.json")
	ui, err := kvitui.New(kvitui.Options{IgnoreDesktop: true, SettingsPath: path})
	if err != nil {
		t.Fatal(err)
	}
	m := MathCommands(ui)
	if MathCommands(ui) != m {
		t.Error("each call should give the same list")
	}
	m.NoteUsed(`\alpha`)
	m.NoteUsed(`\frac`)
	if err := ui.Settings.Flush(); err != nil {
		t.Fatal(err)
	}
	data, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(data), `"math.recentCommands"`) {
		t.Errorf("the settings file has no math.recentCommands:\n%s", data)
	}

	again, err := kvitui.New(kvitui.Options{IgnoreDesktop: true, SettingsPath: path})
	if err != nil {
		t.Fatal(err)
	}
	restored := MathCommands(again)
	if got := restored.RecentCommands(); !slices.Equal(got, []string{`\frac`, `\alpha`}) {
		t.Errorf("read back %q", got)
	}
	if cats := restored.Categories(); cats[0] != mathcmd.RecentlyUsed {
		t.Errorf("the first category is %q", cats[0])
	}
	if rows := restored.ItemsForCategory(mathcmd.RecentlyUsed); len(rows) != 2 || rows[0].Insert != `\frac{}{}` {
		t.Errorf("recent rows: %+v", rows)
	}
}

// The vault window's editor uses the shared list: a command chosen in it is
// saved as recently used.
func TestVaultWindowSavesMathCommandsUsed(t *testing.T) {
	s := openVault(t, notes{"Note.md": "note\n"})
	s.do(func() { s.w.Editor.FocusBlock(0, 4) })
	s.screen.Type(`$\frac`)
	s.screen.Sync()
	s.screen.KeyPress(unison.KeyReturn, mod.None)
	s.screen.Sync()
	var saved []string
	var text string
	s.do(func() {
		saved = newPrefs(s.w.ui).strings("math.recentCommands")
		text = s.w.Editor.Doc.Blocks[0].Text
	})
	if text != `note$\frac{}{}$` {
		t.Errorf("the note: %q", text)
	}
	if !slices.Equal(saved, []string{`\frac`}) {
		t.Errorf("saved %q", saved)
	}
}
