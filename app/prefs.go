package app

// The app's own settings: which panes and modes are on, the panes' widths,
// the note list's order, and the vaults opened recently. They are kept in
// the same settings file as the theme and typography, under the Qt app's
// keys ("view.outline", "panels.sidebarWidth", "session.recentVaults"). The
// file is the Go app's own, which starts as a copy of the Qt app's
// (vault.SeedSettings), so a trial of the Go app never changes what the Qt
// app opens. Without a settings file, as in tests, they last as long as the
// process.

import (
	"slices"

	kvitui "github.com/kvit-s/kvit-ui"
)

// prefs reads and writes the app's settings.
type prefs struct {
	ui  *kvitui.UI
	mem map[string]any
}

func newPrefs(ui *kvitui.UI) *prefs { return &prefs{ui: ui, mem: map[string]any{}} }

func (p *prefs) value(key string) (any, bool) {
	if p.ui.Settings != nil {
		return p.ui.Settings.Value(key)
	}
	v, ok := p.mem[key]
	return v, ok
}

func (p *prefs) set(key string, v any) {
	if p.ui.Settings != nil {
		p.ui.Settings.SetValue(key, v)
		return
	}
	p.mem[key] = v
}

func (p *prefs) bool(key string, fallback bool) bool {
	if v, ok := p.value(key); ok {
		if b, ok := v.(bool); ok {
			return b
		}
	}
	return fallback
}

func (p *prefs) int(key string, fallback int) int {
	if v, ok := p.value(key); ok {
		switch n := v.(type) {
		case float64:
			return int(n)
		case int:
			return n
		}
	}
	return fallback
}

func (p *prefs) string(key, fallback string) string {
	if v, ok := p.value(key); ok {
		if s, ok := v.(string); ok {
			return s
		}
	}
	return fallback
}

func (p *prefs) strings(key string) []string {
	v, _ := p.value(key)
	var out []string
	switch list := v.(type) {
	case []any:
		for _, x := range list {
			if s, ok := x.(string); ok {
				out = append(out, s)
			}
		}
	case []string:
		out = slices.Clone(list)
	}
	return out
}

func (p *prefs) setStrings(key string, list []string) {
	out := make([]any, len(list))
	for i, s := range list {
		out[i] = s
	}
	p.set(key, out)
}

// maxRecentVaults is how many vaults File, Open Recent lists.
const maxRecentVaults = 10

// rememberVault puts a vault first in the recent list.
func (p *prefs) rememberVault(root string) {
	list := slices.DeleteFunc(p.strings("session.recentVaults"), func(s string) bool { return s == root })
	list = append([]string{root}, list...)
	if len(list) > maxRecentVaults {
		list = list[:maxRecentVaults]
	}
	p.setStrings("session.recentVaults", list)
}
