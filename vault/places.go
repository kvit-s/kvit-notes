package vault

// Where a vault is found when none is named: the vaults open when the app
// last closed (session.openVaults in the settings), else Documents/Kvit, as
// the Qt app does (src/qml/windowregistry.cpp, src/qml/processservices.cpp);
// and the Go app's settings file, which starts as a copy of the Qt app's.

import (
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"runtime"
)

// SettingsPath is the Qt app's own settings file: settings.json in
// Qt's application configuration folder for organisation "Kvit" and
// application "Kvit Notes".
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

// OpenVaults are the vaults the Qt app had open when it last closed, those
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

// SeedSettings makes the Go app's own settings file, the first time it
// runs, a copy of the Qt app's, so the theme, typography, panes and vaults
// carry over without the Go app ever writing the Qt app's file.
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
