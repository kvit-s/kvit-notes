package app

// The math command menu's list (package mathcmd, the menu a backslash opens
// in math): one per interface, shared by every editor that types math, so
// the commands used recently in one window lead the menu in all of them. It
// completes from the math engine's commands, and keeps the recently used
// ones in the setting "math.recentCommands", the key the app uses
// (SessionPersistence).

import (
	kvitui "github.com/kvit-s/kvit-ui"

	"github.com/kvit-s/kvit-notes/mathcmd"
	"github.com/kvit-s/kvit-notes/mathtex"
)

// mathCommandLists are the lists made so far, one per interface.
var mathCommandLists = map[*kvitui.UI]*mathcmd.Model{}

// MathCommands is the math command menu's list for ui, to give each editor
// through SetMathCommands. It is made the first time it is asked for: its
// recently used commands are read from the settings, and written back each
// time a command is chosen.
func MathCommands(ui *kvitui.UI) *mathcmd.Model {
	if m := mathCommandLists[ui]; m != nil {
		return m
	}
	p := newPrefs(ui)
	m := mathcmd.New(mathtex.Commands)
	m.SetRecentCommands(p.strings("math.recentCommands"))
	m.OnRecentChanged = func() { p.setStrings("math.recentCommands", m.RecentCommands()) }
	mathCommandLists[ui] = m
	return m
}
