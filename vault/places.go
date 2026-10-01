package vault

// Where a vault is found when none is named: the vaults open when the app
// last closed (session.openVaults in the settings), else Documents/Kvit; and
// the app's settings file, which on the first start is a copy of the file at
// SettingsPath.

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"runtime"
)

// SettingsPath is the settings file of Kvit Notes versions built with Qt:
// settings.json in the application configuration folder for organisation
// "Kvit" and application "Kvit Notes".
func SettingsPath() string {
	var base string
	switch runtime.GOOS {
	case "windows":
		base = os.Getenv("LOCALAPPDATA")
	case "darwin":
		home, _ := os.UserHomeDir()
		base = filepath.Join(home, "Library", "Preferences")
	default:
		base = os.Getenv("XDG_CONFIG_HOME")
		if base == "" {
			home, _ := os.UserHomeDir()
			base = filepath.Join(home, ".config")
		}
	}
	return filepath.Join(base, "Kvit", "Kvit Notes", "settings.json")
}

// OpenVaults are the vaults the file at SettingsPath says were open, those
// that still exist.
func OpenVaults() []string { return OpenVaultsIn(SettingsPath()) }

// OpenVaultsIn are the vaults a settings file says were open, those that
// still exist.
func OpenVaultsIn(settings string) []string {
	data, err := os.ReadFile(settings)
	if err != nil {
		return nil
	}
	var s struct {
		OpenVaults []string `json:"session.openVaults"`
	}
	if json.Unmarshal(data, &s) != nil {
		return nil
	}
	var out []string
	for _, p := range s.OpenVaults {
		if info, err := os.Stat(p); err == nil && info.IsDir() {
			out = append(out, p)
		}
	}
	return out
}

// DefaultRoot is the vault opened when none is named and none was open:
// Kvit in the reader's Documents folder.
func DefaultRoot() string {
	return filepath.Join(documentsDir(), "Kvit")
}

// SeedSettings makes the app's own settings file, the first time it runs, a
// copy of the file at SettingsPath, so the theme, typography, panes and
// vaults are kept. The file at SettingsPath is never written.
func SeedSettings(goSettings string) {
	if _, err := os.Stat(goSettings); !errors.Is(err, os.ErrNotExist) {
		return
	}
	data, err := os.ReadFile(SettingsPath())
	if err != nil || !json.Valid(data) {
		return
	}
	if os.MkdirAll(filepath.Dir(goSettings), 0o755) == nil {
		_ = os.WriteFile(goSettings, data, 0o644)
	}
}
